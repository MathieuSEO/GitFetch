# GitFetch

Fetch GitHub releases on an Amiga.

Nearly all new Amiga software is published through GitHub Releases, which
is awkward on the machine itself: no browser for the platform can handle
the CSS and JavaScript the site relies on. Getting to the newest `.lha`
usually means walking over to a modern computer.

GitFetch does that part for you. Type a GitHub address or `owner/repo`,
pick a release and a file, and it lands on your hard drive.

It downloads, and nothing else. Unpacking and installing stay in your own
hands, with the tools you already trust.

![GitFetch on AmigaOS 3.9](images/main-window.jpg)

Saved repositories on the left, releases in the middle, the files in that
release on the right. Running on an A1200 with a Blizzard 060.

## Requirements

- AmigaOS 3.5 or newer, for the ReAction classes
- 68020 or better
- A screen of 640x400 or larger
- A TCP/IP stack: Roadshow, AmiTCP, Miami or Genesis
- [AmiSSL](https://github.com/jens-maus/amissl) 5 or newer
- A correctly set system clock

That last one is easy to overlook. An Amiga without a working
battery-backed clock starts in 1978, and every TLS certificate then looks
"not yet valid", so the connection fails with an error that says nothing
about dates. GitFetch checks the date before connecting and says so
plainly. It does not set the clock itself; that belongs to a proper time
program such as the NTP client built into Roadshow.

## Two ways to reach GitHub

GitHub only speaks HTTPS, which an Amiga cannot do on its own. There are
two answers, switchable in the settings.

**Directly, through AmiSSL.** No server of your own. GitHub sends its full
reply — around 240 KB for 25 releases, mostly release notes that get
thrown away — and all of it passes through TLS decryption on a 68k.

**Through a proxy.** A small Python script, included, using only the
standard library. It turns the same reply into about 5 KB of plain text,
already converted to the Amiga character set, and caches it so GitHub's
limit of 60 requests per hour is not an issue. Faster and lighter, but it
only works while that server runs.

The setting for how many releases to fetch matters most on a direct
connection: `latest` costs a fraction of `all`.

![Settings](images/settings.jpg)

Fields that do not apply to your choice are greyed out: with a direct
connection the proxy details are irrelevant, and the other way round the
certificate check is.

## Building

Cross-compiled with [bebbo's amiga-gcc](https://codeberg.org/bebbo/amiga-gcc)
and the AmigaOS 3.9 NDK. See [doc/toolchain.md](doc/toolchain.md) for the
full setup, including four pitfalls that will otherwise cost you an
evening.

```sh
make test        # host tests: parser and URL normalisation
make test-proxy  # proxy: signing, allowlist, text conversion
make test-json   # JSON parser against a reference implementation
make fuzz        # 500,000 rounds of malformed input
make amiga       # cross-compile
make dist        # build the distribution archive
```

The first four run on any machine with a C compiler and Python; no Amiga
or cross-compiler needed. The parsers are deliberately written so they can
be tested that way.

## Layout

| Path | What |
|---|---|
| `src/` | the Amiga program |
| `include/` | shared headers |
| `server/` | the proxy; standard library only |
| `test/` | host tests, fuzzer, and a POSIX shim to exercise the network code |
| `tools/` | icon and LhA writers, because neither exists on macOS |
| `pkg/` | guide, installer script and Aminet description |
| `doc/` | toolchain notes, protocol, wishlist |

## Notes for the curious

A few things turned out to be more interesting than expected, and are
written up in [doc/toolchain.md](doc/toolchain.md):

- `printf` on AmigaOS reads 32-bit arguments: `%ld` with a `(long)` cast,
  never `%d`. Getting this wrong produced a bug that looked like a network
  fault for a whole evening.
- Most ReAction classes do not register under their name, so
  `NewObject(NULL, "string.gadget", ...)` returns NULL. You have to ask
  the library for the class pointer. Confusingly, `button.gadget` does
  accept the name form.
- NDK headers document tags that the OS 3.9 classes do not have. An
  unknown tag is silently ignored, which looks exactly like a bug in your
  own code.

## In its natural habitat

![AmigaOS 3.9 desktop](images/desktop.jpg)

AmigaOS 3.9, 68060 at 50 MHz, Voodoo3 through a Mediator. Downloading
AmiSSL, which is what makes the direct connection work in the first place.

## Thanks

Jens Maus and everyone behind AmiSSL. Amiga Cafe, <https://amiga.cafe>.
RVO, for my Amiga revival. Darren Banfi, whose Mint projects started this.
And every (vibe) coder still keeping our oldest girlfriend alive.

## Licence

Freeware. Use it, share it, take it apart.

(c) 2026 Mathieu Burgerhout
