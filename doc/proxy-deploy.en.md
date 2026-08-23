# Running the proxy

The proxy turns the GitHub API into something an Amiga can read without
TLS. It needs nothing beyond the Python standard library.

## Trying it locally

```sh
make proxy                      # port 8099, temporary key
curl "http://127.0.0.1:8099/v1/releases?repo=jens-maus%2Famissl&max=3"
```

## On a server

```sh
GITFETCH_SECRET=$(openssl rand -base64 32) \
GITFETCH_BIND=127.0.0.1 \
GITFETCH_PORT=8080 \
GITFETCH_TOKEN=ghp_...        # optional
python3 gitfetch_proxy.py
```

| Variable | Meaning | Default |
|---|---|---|
| `GITFETCH_SECRET` | HMAC key for asset ids | temporary, new each start |
| `GITFETCH_TOKEN` | GitHub PAT; raises the limit from 60 to 5000 per hour | empty |
| `GITFETCH_TTL` | cache per repository, in seconds | 600 |
| `GITFETCH_LINK_TTL` | how long an asset id stays valid | 3600 |
| `GITFETCH_MAX_ASSET` | largest file to pass through | 256 MB |

Set `GITFETCH_SECRET` to a fixed value in production. Without one the proxy
picks a new key on every restart, and every asset link handed out before
that becomes invalid.

## Behind nginx

The important part, and the thing this setup trips over:

**The hostname must serve plain HTTP on port 80.** No redirect to HTTPS, no
HSTS header. Without AmiSSL the Amiga cannot follow a redirect to `https://`.
Most nginx configurations force HTTPS these days, so it has to be switched
off explicitly for this hostname.

```nginx
server {
    listen 80;
    server_name amiga.example.com;

    # Deliberately no 'return 301 https://...' and no
    # Strict-Transport-Security: the client is an Amiga without TLS.

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_buffering off;        # stream assets, do not buffer first
        proxy_read_timeout 300s;
    }
}
```

If the domain is in an HSTS preload list, pick a different one that is not:
a browser that has remembered HSTS will force HTTPS, which makes testing
confusing.

## Checking it

```sh
curl -v http://amiga.example.com/v1/hello
```

Look for: no `301`/`308`, no `Strict-Transport-Security`, and
`Content-Type: text/plain; charset=ISO-8859-1`.

## As a systemd service

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

## Pointing the Amiga at it

Open the settings from the menu (or right-Amiga+I), switch off "Connect to
GitHub directly", and fill in the address and port. Press Save and it is
remembered across reboots.

From the Shell it also works:

```
SetEnv GitFetch/Backend proxy
SetEnv GitFetch/ProxyHost amiga.example.com
SetEnv GitFetch/ProxyPort 80
Copy ENV:GitFetch ENVARC:GitFetch ALL
```

Note that AmigaDOS environment variable names are case-insensitive: `ENV:`
is a directory, and the Amiga file system makes no distinction. So
`GitFetch/ProxyHost` and `GitFetch/Proxyhost` are the same variable, and
removing one removes the other.
