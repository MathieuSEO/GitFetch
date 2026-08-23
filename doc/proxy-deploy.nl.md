# De proxy op de server zetten

De proxy vertaalt de GitHub API naar iets dat een Amiga zonder TLS kan
lezen. Hij heeft geen dependencies buiten de Python-standaardbibliotheek.

## Snel lokaal proberen

```sh
make proxy                      # poort 8099, tijdelijke sleutel
curl "http://127.0.0.1:8099/v1/releases?repo=jens-maus%2Famissl&max=3"
```

## Op de VPS

```sh
GITFETCH_SECRET=$(openssl rand -base64 32) \
GITFETCH_BIND=127.0.0.1 \
GITFETCH_PORT=8080 \
GITFETCH_TOKEN=ghp_...        # optioneel
python3 gitfetch_proxy.py
```

| Variabele | Betekenis | Standaard |
|---|---|---|
| `GITFETCH_SECRET` | HMAC-sleutel voor asset-ids | tijdelijk, per start nieuw |
| `GITFETCH_TOKEN` | GitHub PAT; tilt de limiet van 60 naar 5000 per uur | leeg |
| `GITFETCH_TTL` | cache per repo, in seconden | 600 |
| `GITFETCH_LINK_TTL` | geldigheid van een asset-id | 3600 |
| `GITFETCH_MAX_ASSET` | maximale bestandsgrootte | 256 MB |

Zet `GITFETCH_SECRET` vast in productie. Zonder vaste sleutel krijgt de
proxy bij elke herstart een nieuwe, en zijn alle eerder uitgedeelde
asset-links meteen ongeldig.

## nginx

Het belangrijkste punt, en de valkuil waar deze opzet op stukloopt:

**Het subdomein moet gewoon HTTP op poort 80 serveren.** Geen redirect naar
HTTPS, geen HSTS-header. De Amiga heeft in fase 1 geen TLS en volgt geen
redirect naar `https://`. De meeste nginx-configuraties forceren
tegenwoordig HTTPS; dat moet hier expliciet uit.

```nginx
server {
    listen 80;
    server_name amiga.voorbeeld.nl;

    # Bewust geen 'return 301 https://...' en geen Strict-Transport-Security:
    # de client is een Amiga zonder TLS.

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_buffering off;        # assets streamen, niet eerst bufferen
        proxy_read_timeout 300s;
    }
}
```

Staat het domein in een HSTS-preloadlijst, kies dan een apart domein dat
daar niet in zit — een browser die HSTS onthouden heeft, dwingt HTTPS af,
en dan is het lastig testen.

## Controleren

```sh
curl -v http://amiga.voorbeeld.nl/v1/hello
```

Let op: geen `301`/`308`, geen `Strict-Transport-Security`, en
`Content-Type: text/plain; charset=ISO-8859-1`.

## Als systemd-service

```ini
[Unit]
Description=GitFetch proxy
After=network.target

[Service]
ExecStart=/usr/bin/python3 /opt/gitfetch/gitfetch_proxy.py
Environment=GITFETCH_SECRET=...
Environment=GITFETCH_BIND=127.0.0.1
Environment=GITFETCH_PORT=8080
Restart=on-failure
User=gitfetch

[Install]
WantedBy=multi-user.target
```

## Op de Amiga instellen

```
SetEnv GitFetch/ProxyHost amiga.voorbeeld.nl
SetEnv GitFetch/ProxyPort 80
SetEnv GitFetch/DestDir Work:Downloads
Copy ENV:GitFetch ENVARC:GitFetch ALL
```

Of via de ToolTypes van het icoon: `PROXYHOST`, `PROXYPORT`, `DESTDIR`,
`MAXRELEASES`. ENV: wint van ToolTypes, zodat je vanuit de Shell iets
anders kunt proberen zonder het icoon aan te passen.
