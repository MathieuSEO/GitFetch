# Setting up the cross-compiler (macOS)

GitFetch is built with bebbo's `amiga-gcc`. Note that the project is **no
longer on GitHub** (that URL returns 404) but on Codeberg.

```sh
brew install autoconf automake texinfo lhasa gnu-sed make bison \
             gmp mpfr libmpc
git clone https://codeberg.org/bebbo/amiga-gcc.git ~/src/amiga-gcc
```

Installing into `$HOME/opt/amiga` rather than `/opt/amiga` avoids needing
sudo.

## Which gcc branch

The branch is set in `.repos` (created from `default-repos` on the first
run). The default is `amiga6` — gcc 6.5 from 2018, and that **will not
build** against a modern macOS SDK with clang 17: `system.h` collides with
`sys/resource.h` and with the declaration of `strsignal`. Point it at a
more recent branch:

```sh
sed -i '' 's/amiga6/amiga13.4/' .repos
```

Available branches can be listed with
`git ls-remote --heads https://franke.ms/git/bebbo/gcc`.

## NDK 3.9, not 3.2

```sh
make binutils gcc libnix libgcc fd2sfd fd2pragma sfdc ndk \
     NDK=3.9 PREFIX="$HOME/opt/amiga" -j4
```

`NDK=3.9` is essential for this project: only that NDK ships the ReAction
headers (`classes/window.h`, `gadgets/layout.h`, `gadgets/listbrowser.h`,
`gadgets/fuelgauge.h`, `gadgets/getfile.h`). Without the flag the build
picks NDK 3.2.

`gdb` and `libSDL12` are deliberately left out of that list — not needed
here, and they only add build time.

## Four traps on recent macOS

**1. `gsed` has to be on the PATH.** bebbo's Makefile uses `gsed` to read
the repository URLs and branches from `.repos`. Without it `gcc_BRANCH`
ends up empty, turning `git clone -b $(BRANCH) --depth 16 $(URL)` into
`git clone -b --depth 16 URL`: `-b` swallows `--depth` and git complains
that repository "16" does not exist. A confusing message with a simple
cause.

**2. The zlib inside gcc breaks `fdopen`.** That version tests for
`TARGET_OS_MAC`, meaning classic Mac OS; modern SDKs define that symbol
too, after which zlib defines `fdopen` away as a macro — right before
`stdio.h`, which declares it. The script `patch-zlib.py` (idempotent) adds
`&& !defined(__APPLE__)`. Run it again after a re-clone.

**3. Half-broken Command Line Tools.** On this system
`/Library/Developer/CommandLineTools/usr/include/c++/v1` held 3 files
instead of 189, and that near-empty directory took precedence over the
complete one in the SDK — so clang++ could not find `<algorithm>`. Check
with:

```sh
echo '#include <algorithm>' | g++ -x c++ -fsyntax-only -
```

Workaround without reinstalling:

```sh
export CPLUS_INCLUDE_PATH="$(xcrun --show-sdk-path)/usr/include/c++/v1"
```

The real fix is reinstalling the Command Line Tools.

**4. binutils-gdb wants to see gmp/mpfr.** Even when not building gdb,
configure refuses without them. `brew install gmp mpfr` and then:

```sh
export CPPFLAGS="-I/opt/homebrew/include"
export LDFLAGS="-L/opt/homebrew/lib"
```

## Checking

```sh
$HOME/opt/amiga/bin/m68k-amigaos-gcc --version
ls $HOME/opt/amiga/m68k-amigaos/ndk-include/classes/window.h
ls $HOME/opt/amiga/m68k-amigaos/ndk-include/gadgets/listbrowser.h
```

Then from the project directory:

```sh
make amiga AMIGA_PREFIX=$HOME/opt/amiga
```

## Creating ReAction objects with gcc

The macros in the NDK (`WindowObject ... End`) rely on `reaction.lib`, a
SAS/C library gcc can do nothing with. So you create the objects yourself
with `NewObject()` — but mind **how**.

The obvious approach is to name the class:

```c
obj = NewObject(NULL, "string.gadget", ...);   /* returns NULL */
```

That does not work. Most ReAction classes do not register under their
name; you have to ask the library for the class pointer:

```c
#include <proto/string.h>
obj = NewObject(STRING_GetClass(), NULL, ...);
```

Confusingly it is mixed. From `reaction/reaction_macros.h`:

```c
#define ButtonObject   NewObject( NULL, "button.gadget"      /* name works */
#define StringObject   NewObject( STRING_GetClass(), NULL    /* name does not */
#define WindowObject   NewObject( WINDOW_GetClass(), NULL
#define LayoutObject   NewObject( LAYOUT_GetClass(), NULL
```

`button.gadget` accepts the name form, almost nothing else does. Because
`NewObject()` simply returns `NULL` without saying why, it looks as though
the class is missing while `OpenLibrary()` succeeded perfectly well. Use
`XXX_GetClass()` everywhere and include the matching `proto/` header; you
still have to open the library base (`StringBase`, `WindowBase`, ...)
yourself.

Passing **both** the pointer and the name is safer still:

```c
obj = NewObject(cls.ptr, cls.name, ...);
```

If `GetClass()` unexpectedly returns NULL, `NewObject(NULL, NULL, ...)`
would follow — and intuition crashes on that with a Line-1111 trap rather
than returning NULL politely. With the name as a second chance that cannot
happen.

Two more things absent from NDK 3.9 but present in OS4 documentation:
`LISTBROWSER_Striping` and `WINDOW_NewMenu`. For the menu you build a
GadTools menu strip yourself (`CreateMenus` + `LayoutMenus`) and hand it
over through `WINDOW_MenuStrip`.

## Not all ReAction tags exist in OS 3.9

The NDK headers also describe tags added in later versions. The comment
above them says so, and that is easy to miss:

```c
#define CHOOSER_LabelArray  (CHOOSER_Dummy+12)
    /* (STRPTR *) A null terminated array of strings ... New for v45.2 */
```

The classes in OS 3.9 are version 44. Such a tag is then ignored: no
error, no compiler warning, just an empty chooser. Use the older route in
that case — here `CHOOSER_Labels` with an exec list filled by
`AllocChooserNode()`.

Watch for a "New for vXX" note on every tag and compare it with the
version you pass to `OpenLibrary()`.

## AmigaDOS environment variables are case-insensitive

`ENV:` is an ordinary directory, and the Amiga file system draws no
distinction between upper and lower case. So `GitFetch/Proxyhost` and
`GitFetch/ProxyHost` are the same variable — an `Unsetenv` of one throws
away the other.

## printf on AmigaOS: %ld, not %d

The printf family on AmigaOS reads 32-bit arguments. With `%d` and an
`int` you get zero — without a warning from the compiler, because as far
as C is concerned the call is perfectly fine.

```c
sprintf(path, "...&max=%d",  (int)n);    /* yields max=0  */
sprintf(path, "...&max=%ld", (long)n);   /* yields max=25 */
```

This cost an evening of searching: the URL arrived at the proxy with
`max=0`, which then fell back to exactly one release. Everything further
along — network, parser, list — worked correctly and reported "no errors",
so it looked like a problem in the receiving.

Use `%ld` with an explicit `(long)` cast for every integer, including
where `int` would do.

## TLS on an Amiga: set the clock first

An Amiga without a working battery-backed clock starts in 1978. Every
certificate then looks "not yet valid" and the TLS handshake fails — with
an OpenSSL error that says nothing about the date:

```
error:1E800066:HTTP routines::error sending
```

```
date                       ; check
date 21-aug-26 17:00:00    ; correct
```

GitFetch now checks this in advance and reports it in plain language.
