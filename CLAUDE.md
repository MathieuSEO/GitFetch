# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

GitFetch: a ReAction GUI program for AmigaOS 3.5+ (68020+, target machines can have 2 MB RAM) that lists GitHub releases and downloads an asset. It can reach GitHub two ways: directly over TLS via AmiSSL (the default), or over plain HTTP through a small Python proxy in `server/`. Written in C89-style C for bebbo's `m68k-amigaos-gcc` with the NDK 3.9, cross-compiled on macOS.

Language conventions: code comments, Makefile comments, test output and the proxy's docstrings are in **Dutch**. The README, `doc/*.md` and commit messages are in **English**. User-facing strings in the program are English (see Locale below).

## Commands

Host-side (any machine with `cc` and `python3`, no cross-compiler needed):

```sh
make test        # test/test_parse.c: protocol parser, URL normalisation, asset choice (UBSan)
make test-json   # streaming JSON parser vs test/json_reference.py, at chunk sizes 1/3/13/511/4096
                 # (the reference imports select_assets()/source_name() from the proxy,
                 #  so this also checks both backends pick the same files)
make fuzz        # 500k rounds of malformed input against parse/url/json
make test-proxy  # server/test_proxy.py: HMAC signing, allowlist, text conversion, release list
make proxy       # run the proxy locally on :8099 (GITFETCH_SECRET defaults to a test key)
make test-net    # end-to-end: real net.c + backend_proxy.c through a POSIX shim against a running proxy
                 #   make test-net PROXY_HOST=127.0.0.1 PROXY_PORT=8099 REPO=jens-maus/amissl
make check-installer  # static check of pkg/Install (paren balance, conflicting copyfiles options)
make catalogs    # check catalogs/*.ct against locale.c (ids, printf codes) and build .catalog files
```

Amiga builds (need the toolchain; `AMIGA_PREFIX` defaults to `/opt/amiga`, on this machine usually `$HOME/opt/amiga`):

```sh
make amiga AMIGA_PREFIX=$HOME/opt/amiga   # build/GitFetch + build/gftest
make gitfetch-debug                       # -DGF_DEBUG -O1: GF_TRACE* macros print each step to the Shell
make dist                                 # dist/GitFetch.lha (icons, guide, installer, proxy)
make aminet                               # dist/aminet/GitFetch.{lha,readme}
```

There is no per-test selector: each test file is a single `main()` calling `test_*()` functions in sequence. To run one, comment out the others in `main()` or build the binary directly with the same flags as the Makefile target.

`doc/toolchain.md` covers toolchain setup (Codeberg, not GitHub; `amiga13.4` branch; `NDK=3.9` is required for the ReAction headers) and the macOS build traps.

## Architecture

**Backend abstraction.** The GUI only calls `gf_fetch_releases()` / `gf_download_asset()` (`include/gfbackend.h`). `src/backend.c` dispatches on `prefs->backend` to `backend_proxy.c` (plain HTTP via `net.c`, line protocol parsed by `parse.c`) or `backend_native.c` (AmiSSL to `api.github.com`, response fed through the streaming JSON parser `json.c`). Both fill the same fixed-size `struct ReleaseList` (`include/gitfetch.h`), so the GUI never knows which backend was used. `gfbackend_impl.h` is internal to the backends.

**Fixed-size data model.** `ReleaseList` is one ~120 KB `AllocVec` block with fixed char arrays; the `GF_MAX_*` lengths in `gitfetch.h` match exactly what the proxy truncates to (`server/gitfetch_proxy.py` has the same constants). Changing a limit means changing both sides, and memory cost is a real concern (see the `GF_MAX_ASSETS` comment). Error codes are negative `GF_ERR_*`; `errors.c` maps them to English text for gftest and host tests, `gf_error_str()` in `locale.c` to translatable text for the GUI.

**Streaming everywhere.** `net.c` hands body chunks to a sink callback instead of buffering; the JSON parser is a byte-at-a-time state machine that only collects the few keys it needs (a 25-release GitHub response is ~240 KB, mostly release notes). Parsers must give identical results regardless of chunk boundaries — that's what `test-json` and the streaming-equivalence test verify.

**Worker process.** All network I/O runs in a separate process (`worker.c`, `include/gfworker.h`), because bsdsocket.library must not be shared between tasks and the GUI must stay responsive for the Stop button. Communication: `struct GFJob` message with ReplyMsg on completion, a progress signal plus `volatile ULONG sofar/total` (32-bit writes are atomic on 68k), and `SIGBREAKF_CTRL_C` to abort. Only the worker opens bsdsocket.

**Proxy protocol.** Described in `doc/protocol.md`: tab-separated `R`/`A` lines in ISO-8859-1, `#STATUS`, `#MORE <release> <n>` for files that did not fit, and a mandatory `#END` (a missing `#END` is an error, never a partial list). Asset ids are HMAC-signed and time-limited; upstream hosts are allowlisted. The proxy is stdlib-only Python — keep it that way.

**Which files a release shows.** At most `GF_MAX_ASSETS` (12). When there are more, `gf_asset_offer()` in `src/assets.c` lets archive/disk formats push others out; `Release.nskipped` counts what fell off. The last entry is the source-code zip (`<repo>-<tag>-source.zip`, size 0, recognised by `gf_asset_is_source()`). `json.c` (direct) and `select_assets()` in the proxy implement the same rules — change both.

**Host testability.** `parse.c`, `url.c`, `json.c`, `assets.c` are deliberately platform-independent. `test/shim/` provides just enough fake `exec`/`dos`/`socket` headers and a POSIX implementation for `net.c` + `backend_proxy.c` to run on the host; `native_stub.c` stubs out the AmiSSL backend. New Amiga calls used by those modules need a shim entry.

**GUI.** `gui.c` (main window: bookmarks / releases / assets listbrowsers), `prefswin.c` (settings), `bookmarks.c` (plain text file, one repo per line, saved to both `ENV:` and `ENVARC:GitFetch/Bookmarks`, read from `ENV:` first). `prefs.c` loads defaults → icon ToolTypes → `ENV:GitFetch/*`, ENV winning. `main.c` takes an optional `REPO` argument (ReadArgs) and fetches it on startup. `notify.c` sends a RexxMsg straight to the `HERALD`, `RANCHERO` or `RINGHIO` port (no RexxMast needed) when a download ends. `gftest.c` is a CLI tool sharing all non-GUI code, for testing the network stack without a window.

**Locale.** Every UI string goes through `gf_str(MSG_*)`. New strings go at the **end** of the enum in `include/gflocale.h` and of the table in `src/locale.c` (the position is the catalog id; inserting shifts every translation). Then regenerate `catalogs/GitFetch.cd` (`python3 tools/make_catalog.py cd catalogs/GitFetch.cd`) and add the line to each `catalogs/<language>.ct` (ISO-8859-1). `tools/make_catalog.py` writes the IFF CTLG itself — no FlexCat. Catalogs ship in `Catalogs/<language>/` beside the program (`PROGDIR:Catalogs/`); the `LANGUAGE` tooltype/ENV overrides Locale prefs. Use `snprintf` whenever a `gf_str()` result is a format, so a long translation truncates instead of overflowing.

## Amiga-specific pitfalls (from doc/toolchain.md — these have each cost real debugging time)

- printf family: always `%ld` with a `(long)` cast; `%d` silently prints 0. `Printf` args in `GF_TRACE` are cast to `ULONG`/`LONG`.
- Create ReAction objects with `NewObject(XXX_GetClass(), "xxx.gadget", ...)` — the name alone returns NULL for most classes, NULL/NULL crashes.
- NDK headers list tags marked "New for v45+" that OS 3.9's v44 classes silently ignore (e.g. `CHOOSER_LabelArray`, `LISTBROWSER_Striping`, `WINDOW_NewMenu`). Check the version note on every tag.
- `WINDOW_Position` overrides `WA_Left`/`WA_Top`; the window tag list is built at runtime so it can be omitted when a saved position exists.
- getfile.gadget needs `asl.library` opened by the program or it silently does nothing.
- `ENV:` names are case-insensitive.
- `--gc-sections` drops anything unreferenced, so `$VER:` and `$STACK:` are kept with `-Wl,-u,_version_tag` / `-Wl,-u,_stack_size`. `$STACK:` is only read by the OS 3.2/4 Shell; on 3.5/3.9 the stack comes from libnix's swapstack module (`unsigned long __stack` in `main.c`/`gftest.c`, pulled in by `-Wl,-u,___stkinit`). Verify with a non-stripped build and `-Wl,-Map`.
- AmigaOS headers type strings as `STRPTR` (`unsigned char *`); `-Wno-pointer-sign` is intentional.

## Releases and packaging

The version appears in several places that must be bumped together: `VERSION` in `Makefile`, `GF_VERSION` and `GF_DATE` in `include/gitfetch.h` (used by both `$VER:` strings and the About box), the `## version` line in `catalogs/*.ct`, the intro in `pkg/Install`, the title and history in `pkg/GitFetch.guide`, and `pkg/GitFetch.readme`.

`dist/` is disposable and rebuilt from `build/` and `pkg/`. Edit the guide, installer and Aminet readme in `pkg/`, never in `dist/`; `make check-dist` refuses to proceed if `dist/` holds edits that differ from `pkg/`. `make dist` uses a real LhA at `$(AMIGA_PREFIX)/bin/amiga-lha` (or `LHA=`) and falls back to the uncompressed `tools/make_lha.py`. Icons are generated by `tools/make_icon.py`.

`doc/wishlist.md` records what was deliberately left out (unpacking, NTP, ARexx) and the memory cost of each idea — check it before adding features.
