# The GITFETCH protocol

Plain HTTP, ISO-8859-1, line and tab based. Chosen deliberately: on a 68k
this can be handled with `FGets()` and `strchr()`, without allocations,
without a state machine, and without a JSON parser.

The proxy does the work that is expensive on an Amiga: flattening the
JSON, transliterating UTF-8 to ISO-8859-1 (release titles contain emoji),
stripping tabs and control characters, and truncating fields to the
maximum lengths from `include/gitfetch.h`.

## GET /v1/releases?repo=owner/name&max=15

```
#GITFETCH 1
#STATUS OK
#REPO jens-maus/amissl
R<TAB>0<TAB>5.27<TAB>2026-04-08<TAB>0<TAB>AmiSSL 5.27
A<TAB>0<TAB>0<TAB>AmiSSL-5.27-OS3.lha<TAB>4294781<TAB>/v1/asset?id=...
A<TAB>0<TAB>1<TAB>AmiSSL-5.27-SDK.lha<TAB>2583127<TAB>/v1/asset?id=...
R<TAB>1<TAB>5.26<TAB>2026-01-28<TAB>0<TAB>AmiSSL 5.26
#END
```

| Line | Fields |
|---|---|
| `R` | release index, tag, date (YYYY-MM-DD), prerelease (0/1), title |
| `A` | release index, asset index, filename, size in bytes, download path |

Lines starting with `#` are control lines. Assets must follow their own
`R` line directly; an `A` whose release index does not match the last
release read is ignored rather than attached to the wrong release.

Error:

```
#GITFETCH 1
#STATUS ERR 404 Repository niet gevonden
#END
```

**`#END` is required.** Without it the transfer was cut short, and the
client reports an error. Showing half a list as though it were the whole
one is worse than an error message: you would miss the newest release
without noticing.

## GET /v1/asset?id=&lt;opaque&gt;

Replies with `200`, a `Content-Length` (needed for the progress bar) and
the bytes of the file, streamed straight through from GitHub.

The `id` is an HMAC-signed, time-limited reference to owner/repo/asset-id.
Without that signature the proxy would be an open relay through which
anyone could pull bandwidth off the server. The upstream is additionally
restricted to `api.github.com` and the `*.githubusercontent.com` hosts.

Redirecting to the real GitHub URL is not an option: that is HTTPS only,
and in phase 1 the Amiga has no TLS.

## GET /v1/hello

```
#GITFETCH 1
#STATUS OK
#TIME 1787315581
#END
```

For a "test the connection" button.
