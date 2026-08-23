#!/usr/bin/env python3
"""
GitFetch proxy -- vertaalt de GitHub Releases API naar een plain-HTTP,
regel-gebaseerd formaat dat een AmigaOS 3.x client zonder TLS en zonder
JSON-parser kan verwerken.

Endpoints:
    GET /v1/hello                       health check
    GET /v1/releases?repo=owner/name    releaselijst in GITFETCH-formaat
    GET /v1/asset?id=<opaque>           asset-bytes, gestreamd vanaf GitHub

Configuratie via omgevingsvariabelen:
    GITFETCH_PORT       poort om op te luisteren        (default 8080)
    GITFETCH_BIND       adres om aan te binden          (default 127.0.0.1)
    GITFETCH_SECRET     HMAC-sleutel voor asset-ids     (verplicht in productie)
    GITFETCH_TOKEN      GitHub PAT, optioneel           (60/uur -> 5000/uur)
    GITFETCH_TTL        cache-TTL in seconden           (default 600)
    GITFETCH_LINK_TTL   geldigheid asset-id in seconden (default 3600)
    GITFETCH_MAX_ASSET  max assetgrootte in bytes       (default 268435456)

Draait op de Python standaardbibliotheek, geen dependencies.
"""

import base64
import hashlib
import hmac
import json
import os
import sys
import threading
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROTO_VERSION = 1
USER_AGENT = "GitFetch-Proxy/0.1 (+https://github.com/)"

PORT = int(os.environ.get("GITFETCH_PORT", "8080"))
BIND = os.environ.get("GITFETCH_BIND", "127.0.0.1")
SECRET = os.environ.get("GITFETCH_SECRET", "").encode("utf-8")
TOKEN = os.environ.get("GITFETCH_TOKEN", "")
CACHE_TTL = int(os.environ.get("GITFETCH_TTL", "600"))
LINK_TTL = int(os.environ.get("GITFETCH_LINK_TTL", "3600"))
MAX_ASSET = int(os.environ.get("GITFETCH_MAX_ASSET", str(256 * 1024 * 1024)))

# Alleen deze hosts mogen als upstream dienen. Zonder allowlist zou een
# gemanipuleerde redirect de proxy in een open relay veranderen.
ALLOWED_HOSTS = {
    "api.github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
    "github.com",
    "codeload.github.com",
}

# Veldlengtes; gelijk aan het datamodel van de Amiga-client, zodat die
# nooit hoeft af te kappen of dynamisch geheugen hoeft te beheren.
MAX_TAG = 31
MAX_DATE = 11
MAX_TITLE = 79
MAX_NAME = 63
MAX_RELEASES = 25
MAX_ASSETS_PER_RELEASE = 12   # gelijk aan GF_MAX_ASSETS in gitfetch.h


# --------------------------------------------------------------------------
# Tekstconversie: UTF-8 -> ISO-8859-1
# --------------------------------------------------------------------------

# Interpunctie die Latin-1 niet kent maar wel een redelijke ASCII-tegenhanger
# heeft. Zonder deze tabel verdwijnen em-dashes en slimme quotes spoorloos.
PUNCT_MAP = {
    "\u2018": "'", "\u2019": "'", "\u201a": "'", "\u201b": "'",
    "\u201c": '"', "\u201d": '"', "\u201e": '"', "\u201f": '"',
    "\u2013": "-", "\u2014": "-", "\u2015": "-", "\u2212": "-",
    "\u2026": "...", "\u2022": "*", "\u00b7": "*",
    "\u2192": "->", "\u2190": "<-", "\u2264": "<=", "\u2265": ">=",
    "\u00a0": " ", "\u2009": " ", "\u200b": "",
}


def to_latin1(text):
    """Zet willekeurige UTF-8 om naar iets dat een Amiga kan tonen.

    Accenten blijven behouden waar Latin-1 dat toelaat, vallen anders terug
    op hun ASCII-basis. Emoji en andere niet-representeerbare tekens
    verdwijnen. Tabs en control-chars gaan eruit omdat tab het veldscheidings-
    teken van het protocol is.
    """
    if not text:
        return ""
    text = unicodedata.normalize("NFC", str(text))
    out = []
    for ch in text:
        code = ord(ch)
        if ch in "\t\r\n":
            out.append(" ")
            continue
        if code < 32 or code == 127:
            continue
        if ch in PUNCT_MAP:
            out.append(PUNCT_MAP[ch])
            continue
        try:
            ch.encode("latin-1")
            out.append(ch)
        except UnicodeEncodeError:
            decomposed = unicodedata.normalize("NFKD", ch)
            out.append("".join(c for c in decomposed if ord(c) < 128))
    return " ".join("".join(out).split())


def field(text, maxlen):
    return to_latin1(text)[:maxlen]


# --------------------------------------------------------------------------
# Ondertekende asset-ids
# --------------------------------------------------------------------------

def sign_asset(owner, repo, asset_id):
    """Maak een opaque, TTL-begrensde verwijzing naar een release-asset."""
    expiry = int(time.time()) + LINK_TTL
    payload = "%s/%s/%d/%d" % (owner, repo, asset_id, expiry)
    mac = hmac.new(SECRET, payload.encode("utf-8"), hashlib.sha256).digest()[:12]
    # Geen extra base64-laag om de payload heen: alle tekens zijn al URL-veilig
    # (owner/repo zijn gevalideerd) en dat scheelt ~30% lengte in het
    # path-veld van de Amiga-client.
    return payload + "." + base64.urlsafe_b64encode(mac).rstrip(b"=").decode("ascii")


def verify_asset(token):
    """Valideer een asset-id en geef (owner, repo, asset_id) terug."""
    try:
        payload, mac_b64 = token.rsplit(".", 1)
        owner, repo, asset_id, expiry = payload.split("/")
    except Exception:
        raise ValueError("ongeldig asset-id")

    expected = hmac.new(SECRET, payload.encode("utf-8"), hashlib.sha256).digest()[:12]
    given = base64.urlsafe_b64decode(mac_b64 + "=" * (-len(mac_b64) % 4))
    if not hmac.compare_digest(expected, given):
        raise ValueError("handtekening klopt niet")
    if int(expiry) < time.time():
        raise ValueError("asset-id is verlopen")
    return owner, repo, int(asset_id)


# --------------------------------------------------------------------------
# GitHub API
# --------------------------------------------------------------------------

class NoRedirect(urllib.request.HTTPRedirectHandler):
    """Redirects zelf afhandelen, zodat de Authorization-header nooit
    meelift naar de storage-host (GitHub antwoordt daar met 400)."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


_no_redirect_opener = urllib.request.build_opener(NoRedirect)
_plain_opener = urllib.request.build_opener()

_cache = {}
_cache_lock = threading.Lock()


def gh_headers(accept="application/vnd.github+json"):
    headers = {
        "Accept": accept,
        "User-Agent": USER_AGENT,
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if TOKEN:
        headers["Authorization"] = "Bearer " + TOKEN
    return headers


def gh_get_json(path):
    url = "https://api.github.com" + path
    req = urllib.request.Request(url, headers=gh_headers())
    with _no_redirect_opener.open(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def check_host(url):
    host = urllib.parse.urlparse(url).hostname or ""
    if host not in ALLOWED_HOSTS:
        raise ValueError("host niet toegestaan: %s" % host)
    return url


# --------------------------------------------------------------------------
# Protocol-opbouw
# --------------------------------------------------------------------------

def build_error(code, message):
    return "#GITFETCH %d\n#STATUS ERR %d %s\n#END\n" % (
        PROTO_VERSION, code, to_latin1(message))


def build_releases(owner, repo, limit):
    releases = gh_get_json("/repos/%s/%s/releases?per_page=%d"
                           % (urllib.parse.quote(owner), urllib.parse.quote(repo),
                              min(limit, MAX_RELEASES)))
    lines = [
        "#GITFETCH %d" % PROTO_VERSION,
        "#STATUS OK",
        "#REPO %s/%s" % (field(owner, 64), field(repo, 64)),
    ]
    for r_idx, rel in enumerate(releases[:limit]):
        if rel.get("draft"):
            continue
        tag = field(rel.get("tag_name") or "", MAX_TAG)
        published = (rel.get("published_at") or rel.get("created_at") or "")[:10]
        date = field(published, MAX_DATE)
        title = field(rel.get("name") or "", MAX_TITLE) or tag or "(naamloos)"
        prerelease = 1 if rel.get("prerelease") else 0
        lines.append("R\t%d\t%s\t%s\t%d\t%s" % (r_idx, tag, date, prerelease, title))

        assets = rel.get("assets") or []
        for a_idx, asset in enumerate(assets[:MAX_ASSETS_PER_RELEASE]):
            name = field(asset.get("name") or "", MAX_NAME) or ("asset-%d" % a_idx)
            size = int(asset.get("size") or 0)
            path = "/v1/asset?id=" + sign_asset(owner, repo, int(asset["id"]))
            lines.append("A\t%d\t%d\t%s\t%d\t%s" % (r_idx, a_idx, name, size, path))

        # Broncode-tarball als er geen bijgevoegde bestanden zijn: dan valt er
        # tenminste iets te downloaden in plaats van een lege lijst.
        if not assets and rel.get("zipball_url"):
            lines.append("A\t%d\t0\t%s\t0\t%s" % (
                r_idx,
                field("%s-source.zip" % tag, MAX_NAME),
                "/v1/asset?id=" + sign_asset(owner, repo, -1 - r_idx)))

    lines.append("#END")
    return "\n".join(lines) + "\n"


def cached_releases(owner, repo, limit):
    key = "%s/%s/%d" % (owner.lower(), repo.lower(), limit)
    now = time.time()
    with _cache_lock:
        hit = _cache.get(key)
        if hit and hit[0] > now:
            return hit[1]

    try:
        body = build_releases(owner, repo, limit)
        ttl = CACHE_TTL
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            body = build_error(404, "Repository niet gevonden")
        elif exc.code == 403:
            body = build_error(403, "GitHub rate limit bereikt")
        else:
            body = build_error(exc.code, "GitHub gaf een fout")
        ttl = 60  # korte negatieve cache, anders is de rate limit zo op
    except Exception as exc:
        return build_error(502, "Kan GitHub niet bereiken: %s" % exc)

    with _cache_lock:
        _cache[key] = (now + ttl, body)
    return body


# --------------------------------------------------------------------------
# HTTP-server
# --------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"   # de Amiga-client praat HTTP/1.0, geen keep-alive
    server_version = "GitFetch"
    sys_version = ""

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    # -- helpers ----------------------------------------------------------

    def send_text(self, body, status=200):
        data = body.encode("latin-1", errors="replace")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=ISO-8859-1")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    # -- routes -----------------------------------------------------------

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        route = parsed.path.rstrip("/") or "/"

        if route in ("/v1/hello", "/"):
            self.send_text("#GITFETCH %d\n#STATUS OK\n#TIME %d\n#END\n"
                           % (PROTO_VERSION, int(time.time())))
        elif route == "/v1/releases":
            self.route_releases(query)
        elif route == "/v1/asset":
            self.route_asset(query)
        else:
            self.send_text(build_error(404, "Onbekend endpoint"), status=404)

    def route_releases(self, query):
        repo_arg = (query.get("repo") or [""])[0].strip().strip("/")
        parts = repo_arg.split("/")
        if len(parts) != 2 or not all(parts):
            self.send_text(build_error(400, "Verwacht repo=owner/naam"), status=400)
            return
        owner, repo = parts
        if not all(c.isalnum() or c in "-._" for c in owner + repo):
            self.send_text(build_error(400, "Ongeldige repo-naam"), status=400)
            return
        try:
            limit = max(1, min(int((query.get("max") or ["15"])[0]), MAX_RELEASES))
        except ValueError:
            limit = 15
        self.send_text(cached_releases(owner, repo, limit))

    def route_asset(self, query):
        token = (query.get("id") or [""])[0]
        try:
            owner, repo, asset_id = verify_asset(token)
        except ValueError as exc:
            self.send_text(build_error(403, str(exc)), status=403)
            return

        try:
            url = self.resolve_asset_url(owner, repo, asset_id)
        except urllib.error.HTTPError as exc:
            self.send_text(build_error(exc.code, "GitHub weigerde de asset"),
                           status=502)
            return
        except Exception as exc:
            self.send_text(build_error(502, "Kan asset niet opvragen: %s" % exc),
                           status=502)
            return

        self.stream_asset(url)

    def resolve_asset_url(self, owner, repo, asset_id):
        """Vraag GitHub om de uiteindelijke storage-URL van een asset."""
        if asset_id < 0:
            # Negatieve id = broncode-zip van release-index (-1 - idx).
            releases = gh_get_json("/repos/%s/%s/releases?per_page=%d"
                                   % (owner, repo, MAX_RELEASES))
            return check_host(releases[-1 - asset_id]["zipball_url"])

        api = ("https://api.github.com/repos/%s/%s/releases/assets/%d"
               % (owner, repo, asset_id))
        req = urllib.request.Request(api, headers=gh_headers("application/octet-stream"))
        try:
            with _no_redirect_opener.open(req, timeout=30) as resp:
                # Geen redirect: GitHub levert de bytes direct.
                return check_host(resp.geturl())
        except urllib.error.HTTPError as exc:
            if exc.code in (301, 302, 303, 307, 308):
                return check_host(exc.headers["Location"])
            raise

    def stream_asset(self, url):
        # Zonder Authorization-header: de storage-host wijst getekende
        # verzoeken af, en de URL bevat zijn eigen handtekening al.
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT,
                                                   "Accept": "*/*"})
        try:
            upstream = _plain_opener.open(req, timeout=60)
        except Exception as exc:
            self.send_text(build_error(502, "Download mislukt: %s" % exc), status=502)
            return

        with upstream:
            length = upstream.headers.get("Content-Length")
            if length and int(length) > MAX_ASSET:
                self.send_text(build_error(413, "Bestand te groot"), status=413)
                return

            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            if length:
                self.send_header("Content-Length", length)
            self.end_headers()
            if self.command == "HEAD":
                return

            sent = 0
            while True:
                chunk = upstream.read(32768)
                if not chunk:
                    break
                sent += len(chunk)
                if sent > MAX_ASSET:
                    break
                try:
                    self.wfile.write(chunk)
                except (BrokenPipeError, ConnectionResetError):
                    # Amiga heeft afgebroken; niets aan de hand.
                    break


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    global SECRET
    if not SECRET:
        SECRET = base64.b64encode(os.urandom(32))
        sys.stderr.write(
            "WAARSCHUWING: GITFETCH_SECRET niet gezet, tijdelijke sleutel "
            "gegenereerd. Asset-links overleven een herstart niet.\n")
    sys.stderr.write("GitFetch proxy luistert op http://%s:%d/  (token: %s)\n"
                     % (BIND, PORT, "ja" if TOKEN else "nee, 60 req/uur"))
    Server((BIND, PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
