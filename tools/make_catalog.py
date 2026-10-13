#!/usr/bin/env python3
"""
Catalogs voor GitFetch, zonder FlexCat of CatComp.

De Engelse teksten staan in src/locale.c, de volgorde (en dus het nummer
van elke tekst) in de opsomming in include/gflocale.h. Daaruit volgt de
catalogbeschrijving (.cd); een vertaling (.ct) noemt per id de tekst in
een andere taal, en daar maakt dit script een .catalog van die
locale.library kan laden.

    make_catalog.py cd    <uit.cd>              beschrijving schrijven
    make_catalog.py check <taal.ct> [...]       vertaling controleren
    make_catalog.py build <taal.ct> <uit.catalog>

De controle is het belangrijkste deel. Een vertaling met een %s waar het
origineel %ld heeft, laat het programma op de Amiga onderuitgaan, en dat
is aan de tekst niet te zien. Dus: elke id vertaald, geen onbekende ids,
en per tekst dezelfde printf-codes in dezelfde volgorde.

Bestanden zijn ISO-8859-1, zoals op de Amiga gebruikelijk; dan zijn ze
daar ook met FlexCat of een gewone editor te bewerken.
"""

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "include", "gflocale.h")
TABLE = os.path.join(ROOT, "src", "locale.c")
VERSION_H = os.path.join(ROOT, "include", "gitfetch.h")

# printf-codes zoals de C-code ze gebruikt; %% telt niet mee.
FORMAT = re.compile(r"%(?!%)[-+ #0]*\d*(?:\.\d+)?l?[a-zA-Z]")


def fail(msg):
    sys.stderr.write("make_catalog: %s\n" % msg)
    sys.exit(1)


# --- de bron lezen ---------------------------------------------------------

def read_ids():
    text = open(HEADER, encoding="latin-1").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    body = text[text.index("enum {") + 6:text.index("MSG_LAST")]
    ids = re.findall(r"\b(MSG_[A-Z0-9_]+)\b", body)
    if len(ids) != len(set(ids)):
        fail("dubbele id in gflocale.h")
    return ids


def unescape_c(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"',
                        "'": "'"}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def read_builtin():
    """De tabel builtin[] uit locale.c, als lijst van strings."""
    text = open(TABLE, encoding="latin-1").read()
    start = text.index("builtin[MSG_LAST] = {")
    body = text[text.index("{", start) + 1:text.index("};", start)]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)

    entries, current, i = [], None, 0
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            current = (current or "") + unescape_c(body[i + 1:j])
            i = j + 1
        elif c == ",":
            entries.append(current)
            current = None
            i += 1
        else:
            i += 1
    if current is not None:
        entries.append(current)
    return entries


def read_strings():
    ids, texts = read_ids(), read_builtin()
    if len(ids) != len(texts):
        fail("%d ids in gflocale.h, maar %d teksten in locale.c"
             % (len(ids), len(texts)))
    return list(zip(ids, texts))


def read_version():
    text = open(VERSION_H, encoding="latin-1").read()
    version = re.search(r'#define GF_VERSION\s+"([^"]+)"', text).group(1)
    date = re.search(r'#define GF_DATE\s+"([^"]+)"', text).group(1)
    return version, date


# --- .cd en .ct ------------------------------------------------------------

def escape_ct(s):
    return (s.replace("\\", "\\\\").replace("\n", "\\n")
             .replace("\t", "\\t"))


def unescape_ct(s):
    return unescape_c(s)


def write_cd(path):
    lines = [
        "; GitFetch.cd -- gegenereerd uit src/locale.c door",
        "; tools/make_catalog.py. Niet met de hand bewerken: pas de Engelse",
        "; tekst aan in locale.c en draai 'make catalogs'.",
        ";",
    ]
    for number, (ident, text) in enumerate(read_strings()):
        lines.append("%s (%d//)" % (ident, number))
        lines.append(escape_ct(text))
    data = "\n".join(lines) + "\n"
    with open(path, "w", encoding="latin-1", newline="\n") as f:
        f.write(data)


def read_ct(path):
    """Geeft (kop, {id: tekst}) terug. Kop bevat language en version."""
    header, strings, order = {}, {}, []
    lines = open(path, encoding="latin-1").read().split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.startswith("##"):
            parts = line[2:].strip().split(None, 1)
            if len(parts) == 2:
                header[parts[0].lower()] = parts[1].strip()
            continue
        if not line.strip() or line.startswith(";"):
            continue
        ident = line.split()[0]
        if not ident.startswith("MSG_"):
            fail("%s: regel %d: verwachtte een MSG_-id, kreeg %r"
                 % (path, i, line))
        text = ""
        # De tekst staat op de volgende regel; een \ aan het eind laat hem
        # doorlopen, zoals bij FlexCat.
        while i < len(lines):
            part = lines[i]
            i += 1
            if part.endswith("\\") and not part.endswith("\\\\"):
                text += part[:-1]
                continue
            text += part
            break
        if ident in strings:
            fail("%s: %s staat er twee keer in" % (path, ident))
        strings[ident] = unescape_ct(text)
        order.append(ident)
    return header, strings


def check(path):
    """Geeft een lijst met problemen; leeg als alles klopt."""
    problems = []
    source = read_strings()
    header, strings = read_ct(path)
    known = {ident for ident, _ in source}

    if "language" not in header:
        problems.append("## language ontbreekt")
    # FVER in de catalog komt uit gitfetch.h; de kopregel is voor wie met
    # FlexCat werkt, en hoort dan wel hetzelfde te zeggen.
    version, date = read_version()
    want = "$VER: GitFetch.catalog %s (%s)" % (version, date)
    if header.get("version") != want:
        problems.append("## version hoort '%s' te zijn, staat er '%s'"
                        % (want, header.get("version", "")))
    for ident, english in source:
        if ident not in strings:
            problems.append("%s is niet vertaald" % ident)
            continue
        got = strings[ident]
        if FORMAT.findall(got) != FORMAT.findall(english):
            problems.append("%s: printf-codes %s, het origineel heeft %s"
                            % (ident, FORMAT.findall(got),
                               FORMAT.findall(english)))
        # Geen veiligheidsgrens -- die zit in de code -- maar een
        # vertaling die zo veel langer is, past niet in het venster.
        if len(got) > len(english) * 1.6 + 20:
            problems.append("%s: vertaling is veel langer dan het origineel "
                            "(%d tegen %d tekens)"
                            % (ident, len(got), len(english)))
        # Teksten die aan elkaar geplakt worden, hebben hun spatie aan
        # het begin of eind nodig; een editor haalt die er makkelijk af.
        for where, test in (("begin", str.startswith), ("eind", str.endswith)):
            if test(english, " ") != test(got, " "):
                problems.append("%s: spatie aan het %s hoort er %s"
                                % (ident, where,
                                   "wel" if test(english, " ") else "niet"))
        if "_" in english and got.count("_") != 1:
            problems.append("%s: hoort precies een _ (sneltoets) te hebben"
                            % ident)
        if "_" not in english and "_" in got:
            problems.append("%s: _ maakt hier een sneltoets die het origineel "
                            "niet heeft" % ident)
        try:
            got.encode("latin-1")
        except UnicodeEncodeError:
            problems.append("%s: tekens buiten ISO-8859-1" % ident)
    for ident in strings:
        if ident not in known:
            problems.append("%s bestaat niet (meer) in gflocale.h" % ident)
    return problems


# --- .catalog --------------------------------------------------------------

def chunk(cid, data):
    pad = b"\0" if len(data) & 1 else b""
    return cid + struct.pack(">I", len(data)) + data + pad


def build(ct_path, out_path):
    problems = check(ct_path)
    if problems:
        fail("%s klopt niet:\n  %s" % (ct_path, "\n  ".join(problems)))

    header, strings = read_ct(ct_path)
    version, date = read_version()

    strs = b""
    for number, (ident, _) in enumerate(read_strings()):
        # Lengte inclusief de afsluitende NUL, opgevuld tot een veelvoud
        # van vier; zo staat er altijd een NUL achter de tekst.
        data = strings[ident].encode("latin-1") + b"\0"
        data += b"\0" * (-len(data) % 4)
        strs += struct.pack(">II", number, len(strings[ident]) + 1) + data

    fver = ("$VER: GitFetch.catalog %s (%s)" % (version, date)).encode() + b"\0"
    body = (b"CTLG"
            + chunk(b"FVER", fver)
            + chunk(b"LANG", header["language"].encode("latin-1") + b"\0")
            + chunk(b"CSET", b"\0" * 32)
            + chunk(b"STRS", strs))
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(b"FORM" + struct.pack(">I", len(body)) + body)


def main(argv):
    if len(argv) >= 2 and argv[0] == "cd":
        write_cd(argv[1])
    elif len(argv) >= 2 and argv[0] == "check":
        bad = False
        for path in argv[1:]:
            problems = check(path)
            for p in problems:
                print("%s: %s" % (os.path.basename(path), p))
            bad = bad or bool(problems)
            if not problems:
                print("%s: in orde" % os.path.basename(path))
        return 1 if bad else 0
    elif len(argv) == 3 and argv[0] == "build":
        build(argv[1], argv[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
