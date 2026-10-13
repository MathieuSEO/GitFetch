#!/usr/bin/env python3
"""
Tests voor de proxy: ondertekening van asset-ids, de upstream-allowlist en
de tekstconversie. Geen netwerk nodig -- deze draaien zonder GitHub aan te
raken, zodat ze de rate limit niet opsnoepen.

    python3 server/test_proxy.py
"""

import importlib.util
import os
import sys

os.environ.setdefault("GITFETCH_SECRET", "test-sleutel")
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "gp", os.path.join(HERE, "gitfetch_proxy.py"))
gp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gp)
gp.SECRET = b"test-sleutel"

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("  FAIL: %s" % what)


def expect_reject(fn, what, fragment=None):
    try:
        fn()
    except ValueError as exc:
        if fragment and fragment not in str(exc):
            failures.append(what)
            print("  FAIL: %s -- verkeerde reden: %s" % (what, exc))
        return
    failures.append(what)
    print("  FAIL: %s werd geaccepteerd" % what)


def test_signing():
    print("ondertekening van asset-ids")
    token = gp.sign_asset("jens-maus", "amissl", 12345)
    check(gp.verify_asset(token) == ("jens-maus", "amissl", 12345),
          "geldig id valideert")

    payload, mac = token.rsplit(".", 1)
    owner, repo, asset_id, expiry = payload.split("/")

    # De handtekening moet elk veld dekken, niet alleen het geheel.
    expect_reject(lambda: gp.verify_asset(token[:-4] + "aaaa"),
                  "geknoeide handtekening", "does not match")
    expect_reject(lambda: gp.verify_asset(
        "%s/%s/99999/%s.%s" % (owner, repo, expiry, mac)), "aangepast asset-id")
    expect_reject(lambda: gp.verify_asset(
        "%s/andere/%s/%s.%s" % (owner, asset_id, expiry, mac)), "aangepaste repo")
    expect_reject(lambda: gp.verify_asset("onzin"), "onzin-id", "Invalid")

    original_ttl = gp.LINK_TTL
    gp.LINK_TTL = -10
    stale = gp.sign_asset("a", "b", 1)
    gp.LINK_TTL = original_ttl
    expect_reject(lambda: gp.verify_asset(stale), "verlopen id", "expired")

    gp.SECRET = b"andere-sleutel"
    expect_reject(lambda: gp.verify_asset(token), "id van een andere sleutel")
    gp.SECRET = b"test-sleutel"


def test_allowlist():
    print("upstream-allowlist")
    cases = [
        ("https://objects.githubusercontent.com/x", True),
        ("https://release-assets.githubusercontent.com/y", True),
        ("https://api.github.com/z", True),
        ("https://evil.example.com/payload", False),
        ("http://127.0.0.1:8080/admin", False),
        ("https://githubusercontent.com.evil.nl/x", False),
        ("file:///etc/passwd", False),
    ]
    for url, allowed in cases:
        try:
            gp.check_host(url)
            got = True
        except ValueError:
            got = False
        check(got == allowed, "%s hoort %s te zijn"
              % (url, "toegestaan" if allowed else "geweigerd"))


def test_text():
    print("tekstconversie naar ISO-8859-1")
    cases = [
        ("Release \U0001F680 1.0", "Release 1.0"),
        ("Versie één — café", "Versie één - café"),
        ("a\tb\tc", "a b c"),
        ("r1\nr2", "r1 r2"),
        ("bel\x07 en\x00 null", "bel en null"),
        ("‘slim’ “quotes”", "'slim' \"quotes\""),
        ("A → B …", "A -> B ..."),
    ]
    for src, want in cases:
        got = gp.to_latin1(src)
        check(got == want, "%r -> %r (kreeg %r)" % (src, want, got))

    # Wat er ook binnenkomt: het resultaat moet in Latin-1 passen en geen
    # tabs bevatten, anders loopt het veldformaat in de war.
    for src in ["日本語", "\U0001F600" * 10, "x" * 500, "\t\t\t"]:
        got = gp.to_latin1(src)
        got.encode("latin-1")
        check("\t" not in got, "geen tab in uitvoer voor %r" % src[:12])

    check(len(gp.field("x" * 500, gp.MAX_TITLE)) == gp.MAX_TITLE,
          "titel wordt afgekapt op MAX_TITLE")


def test_error_format():
    print("foutformaat")
    body = gp.build_error(404, "Repository not found")
    check(body.startswith("#GITFETCH 1\n"), "begint met de protocolregel")
    check("#STATUS ERR 404 Repository not found" in body, "status-regel")
    check(body.endswith("#END\n"), "eindigt met #END")


def test_selection():
    print("keuze van de bestanden")
    names = ["win-%d.exe" % i for i in range(11)] + ["a.lha", "b.lzx", "c.ADF"]
    kept, skipped = gp.select_assets([(n,) for n in names])
    got = [k[0] for k in kept]
    check(len(got) == 12 and skipped == 2, "12 bewaard, 2 afgevallen (%d)" % skipped)
    check(got[-3:] == ["a.lha", "b.lzx", "c.ADF"],
          "archieven verdringen de rest, in hun eigen volgorde: %r" % got[-3:])
    check(got[:9] == names[:9], "de voorste bestanden blijven staan")

    kept, skipped = gp.select_assets([("x-%d.lha" % i,) for i in range(14)])
    check([k[0] for k in kept] == ["x-%d.lha" % i for i in range(12)] and skipped == 2,
          "bij gelijke rang wint wat eerst kwam")

    url = "https://api.github.com/repos/Vesuri/aloneinthedark/zipball/0.91"
    check(gp.source_name(url, "0.91") == "aloneinthedark-0.91-source.zip",
          "naam broncode-zip: %s" % gp.source_name(url, "0.91"))
    long_name = gp.source_name(url.replace("aloneinthedark", "r" * 80), "v" * 31)
    check(len(long_name) == gp.MAX_NAME and long_name.endswith("v" * 31 + "-source.zip"),
          "lange naam ingekort, tag blijft heel: %s" % long_name)


def test_build_releases():
    print("releaselijst")
    def asset(i, name):
        return {"id": i, "name": name, "size": 100 + i}
    def zipball(tag):
        return "https://api.github.com/repos/Own/Repo/zipball/%s" % tag
    fake = [
        {"draft": True, "tag_name": "concept", "assets": [], "zipball_url": zipball("c")},
        {"tag_name": "2.0", "name": "Twee", "published_at": "2026-10-10T10:00:00Z",
         "assets": [asset(i, "pkg-%d.deb" % i) for i in range(12)] + [asset(99, "Repo.lha")],
         "zipball_url": zipball("2.0")},
        {"tag_name": "1.0", "name": "", "published_at": "2026-01-01T10:00:00Z",
         "assets": [], "zipball_url": zipball("1.0")},
    ]
    original = gp.gh_get_json
    gp.gh_get_json = lambda path: fake
    try:
        body = gp.build_releases("own", "repo", 25)
    finally:
        gp.gh_get_json = original
    lines = body.splitlines()

    r_lines = [l for l in lines if l.startswith("R\t")]
    check([l.split("\t")[1] for l in r_lines] == ["0", "1"],
          "concept overgeslagen, indexen doorlopend: %r" % r_lines)
    a2 = [l.split("\t") for l in lines if l.startswith("A\t0\t")]
    check(len(a2) == 12 and a2[-1][3] == "Repo.lha",
          "lha verdringt een .deb in een volle release")
    check("#MORE 0 2" in lines, "#MORE telt het .deb-bestand en de broncode-zip")
    a1 = [l.split("\t") for l in lines if l.startswith("A\t1\t")]
    check(len(a1) == 1 and a1[0][3] == "Repo-1.0-source.zip" and a1[0][4] == "0",
          "release zonder bestanden krijgt de broncode-zip: %r" % a1)
    check(not any(l.startswith("#MORE 1") for l in lines), "geen #MORE zonder reden")
    check(lines[-1] == "#END", "eindigt met #END")


if __name__ == "__main__":
    test_signing()
    test_allowlist()
    test_text()
    test_error_format()
    test_selection()
    test_build_releases()
    if failures:
        print("\n%d controle(s) mislukt" % len(failures))
        sys.exit(1)
    print("\nalle controles geslaagd")
