# De cross-compiler opzetten (macOS)

GitFetch wordt gebouwd met bebbo's `amiga-gcc`. Let op: dat project staat
**niet meer op GitHub** (die URL geeft 404) maar op Codeberg.

```sh
brew install autoconf automake texinfo lhasa gnu-sed make bison \
             gmp mpfr libmpc
git clone https://codeberg.org/bebbo/amiga-gcc.git ~/src/amiga-gcc
```

Installeren in `$HOME/opt/amiga` in plaats van `/opt/amiga` scheelt sudo.

## Welke gcc-branch

De branch staat in `.repos` (aangemaakt uit `default-repos` bij de eerste
run). De standaard is `amiga6` — gcc 6.5 uit 2018, en die **bouwt niet**
tegen een moderne macOS-SDK met clang 17: `system.h` botst met
`sys/resource.h` en met de declaratie van `strsignal`. Zet hem op een
recentere branch:

```sh
sed -i '' 's/amiga6/amiga13.4/' .repos
```

Beschikbare branches zijn op te vragen met
`git ls-remote --heads https://franke.ms/git/bebbo/gcc`.

## NDK 3.9, niet 3.2

```sh
make binutils gcc libnix libgcc fd2sfd fd2pragma sfdc ndk \
     NDK=3.9 PREFIX="$HOME/opt/amiga" -j4
```

`NDK=3.9` is essentieel voor dit project: alleen die NDK levert de
ReAction-headers (`classes/window.h`, `gadgets/layout.h`,
`gadgets/listbrowser.h`, `gadgets/fuelgauge.h`, `gadgets/getfile.h`).
Zonder die vlag pakt de build NDK 3.2.

`gdb` en `libSDL12` staan bewust niet in die lijst — die zijn hier niet
nodig en kosten alleen bouwtijd.

## Drie valkuilen op recente macOS

**1. `gsed` moet in het PATH staan.** Bebbo's Makefile gebruikt `gsed` om
de repo-URL's en branches uit `.repos` te lezen. Ontbreekt het, dan wordt
`gcc_BRANCH` leeg en verandert `git clone -b $(BRANCH) --depth 16 $(URL)`
in `git clone -b --depth 16 URL`: `-b` slikt `--depth` en git klaagt dat
repository "16" niet bestaat. Verwarrende foutmelding, simpele oorzaak.

**2. De zlib in gcc sloopt `fdopen`.** Die versie test op `TARGET_OS_MAC`
en bedoelt daarmee klassiek Mac OS; moderne SDK's definiëren dat symbool
ook, waarna zlib `fdopen` als macro wegdefinieert — vlak vóór `stdio.h`,
die `fdopen` juist declareert. Het script `patch-zlib.py` (idempotent)
zet er `&& !defined(__APPLE__)` bij. Na een herclone opnieuw draaien.

**3. Half-kapotte Command Line Tools.** Op dit systeem bevatte
`/Library/Developer/CommandLineTools/usr/include/c++/v1` maar 3 bestanden
in plaats van 189, en die lege map kreeg voorrang op de volledige versie in
de SDK — waardoor clang++ `<algorithm>` niet vond. Te controleren met:

```sh
echo '#include <algorithm>' | g++ -x c++ -fsyntax-only -
```

Workaround zonder herinstallatie:

```sh
export CPLUS_INCLUDE_PATH="$(xcrun --show-sdk-path)/usr/include/c++/v1"
```

De echte oplossing is de Command Line Tools opnieuw installeren.

**4. binutils-gdb wil gmp/mpfr zien.** Ook als je gdb niet bouwt, weigert
configure zonder. `brew install gmp mpfr` en dan:

```sh
export CPPFLAGS="-I/opt/homebrew/include"
export LDFLAGS="-L/opt/homebrew/lib"
```

## Controleren

```sh
$HOME/opt/amiga/bin/m68k-amigaos-gcc --version
ls $HOME/opt/amiga/m68k-amigaos/ndk-include/classes/window.h
ls $HOME/opt/amiga/m68k-amigaos/ndk-include/gadgets/listbrowser.h
```

Daarna vanuit de projectmap:

```sh
make amiga AMIGA_PREFIX=$HOME/opt/amiga
```

## ReAction-objecten aanmaken met gcc

De macro's uit de NDK (`WindowObject ... End`) leunen op `reaction.lib`,
een SAS/C-bibliotheek waar gcc niets mee kan. Je maakt de objecten dus
zelf met `NewObject()` -- maar let op **hoe**.

Het ligt voor de hand om de class op naam aan te spreken:

```c
obj = NewObject(NULL, "string.gadget", ...);   /* geeft NULL terug */
```

Dat werkt niet. De meeste ReAction-classes registreren zich niet onder hun
naam; je moet de class-pointer bij de library opvragen:

```c
#include <proto/string.h>
obj = NewObject(STRING_GetClass(), NULL, ...);
```

Verwarrend genoeg is het gemengd. In `reaction/reaction_macros.h` staat:

```c
#define ButtonObject   NewObject( NULL, "button.gadget"      /* naam mag */
#define StringObject   NewObject( STRING_GetClass(), NULL    /* naam mag niet */
#define WindowObject   NewObject( WINDOW_GetClass(), NULL
#define LayoutObject   NewObject( LAYOUT_GetClass(), NULL
```

`button.gadget` accepteert de naamvorm, vrijwel al het andere niet. Omdat
`NewObject()` gewoon `NULL` teruggeeft zonder te zeggen waarom, lijkt het
alsof de class ontbreekt terwijl `OpenLibrary()` prima slaagde. Gebruik
overal `XXX_GetClass()` en includeer de bijbehorende `proto/`-header; de
library base (`StringBase`, `WindowBase`, ...) moet je nog steeds zelf
openen.

Twee dingen die er ook niet zijn in NDK 3.9, maar wel in OS4-documentatie
opduiken: `LISTBROWSER_Striping` en `WINDOW_NewMenu`. Voor het menu maak je
zelf een GadTools-menustrip (`CreateMenus` + `LayoutMenus`) en geef je die
mee via `WINDOW_MenuStrip`.

## AmigaDOS-omgevingsvariabelen zijn hoofdletter-ongevoelig

`ENV:` is gewoon een directory, en het Amiga-bestandssysteem maakt geen
onderscheid tussen hoofd- en kleine letters. `GitFetch/Proxyhost` en
`GitFetch/ProxyHost` zijn dus dezelfde variabele -- een `Unsetenv` van de
ene gooit de andere ook weg.

## printf op AmigaOS: %ld, niet %d

De printf-varianten op AmigaOS lezen 32-bit argumenten. Met `%d` en een
`int` komt er nul uit -- zonder waarschuwing van de compiler, want naar C
gemeten klopt de aanroep gewoon.

```c
sprintf(path, "...&max=%d",  (int)n);    /* levert max=0  */
sprintf(path, "...&max=%ld", (long)n);   /* levert max=25 */
```

Dit kostte een avond zoeken: de URL kwam met `max=0` bij de proxy aan, die
daarop terugviel op precies een release. Alles verderop -- netwerk, parser,
lijst -- werkte correct en meldde netjes "geen fouten", dus het leek een
probleem in de ontvangst.

Gebruik `%ld` met een expliciete `(long)`-cast voor elk geheel getal, ook
waar `int` volstaat.

## TLS op een Amiga: zet eerst de klok

Een Amiga zonder werkende accu-klok begint in 1978. Elk certificaat is dan
"nog niet geldig" en de TLS-handshake mislukt -- met een OpenSSL-fout die
niets over de datum zegt:

```
error:1E800066:HTTP routines::error sending
```

```
date                       ; controleren
date 21-aug-26 17:00:00    ; goedzetten
```

GitFetch controleert dit tegenwoordig vooraf en meldt het in gewone taal.

## ReAction-tags bestaan niet allemaal in OS 3.9

De NDK-headers beschrijven ook tags die pas in latere versies zijn
toegevoegd. Het commentaar erboven vermeldt dat, en dat is makkelijk over
het hoofd te zien:

```c
#define CHOOSER_LabelArray  (CHOOSER_Dummy+12)
    /* (STRPTR *) A null terminated array of strings ... New for v45.2 */
```

De classes van OS 3.9 zijn versie 44. Zo'n tag wordt dan genegeerd: geen
foutmelding, geen waarschuwing van de compiler, gewoon een lege keuzelijst.
Gebruik in dat geval de oudere weg -- hier `CHOOSER_Labels` met een exec-lijst
gevuld door `AllocChooserNode()`.

Let bij elke tag op een "New for vXX"-aantekening en vergelijk die met de
versie waarmee je `OpenLibrary()` aanroept.
