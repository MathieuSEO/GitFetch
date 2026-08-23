#!/usr/bin/env python3
"""
Maakt een LhA-archief.

Er is op deze machine geen inpakker te krijgen (lhasa kan alleen uitpakken),
dus schrijven we het formaat zelf. Met methode -lh0- wordt er niet
gecomprimeerd; dat is een geldig .lha-bestand dat elke Amiga uitpakt. Wie
een kleiner archief wil, pakt dit op de Amiga uit en maakt er met LhA
opnieuw een met compressie.
"""

import os
import struct
import sys
import time


def crc16(data):
    """CRC-16/ARC, zoals LHA die gebruikt."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def dos_time(mtime):
    t = time.localtime(mtime)
    year = max(1980, t.tm_year) - 1980
    date = (year << 9) | (t.tm_mon << 5) | t.tm_mday
    tm = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return (date << 16) | tm


def entry(path_in_archive, data, mtime):
    # LHA level 0. Paden gescheiden met '/', wat Amiga-LhA begrijpt.
    name = path_in_archive.encode("latin-1")

    body = (b"-lh0-" +
            struct.pack("<I", len(data)) +      # gecomprimeerde grootte
            struct.pack("<I", len(data)) +      # oorspronkelijke grootte
            struct.pack("<I", dos_time(mtime)) +
            bytes([0x20]) +                     # bestandsattribuut
            bytes([0x00]) +                     # header-niveau 0
            bytes([len(name)]) + name +
            struct.pack("<H", crc16(data)))

    header_size = len(body)
    checksum = sum(body) & 0xFF
    return bytes([header_size, checksum]) + body + data


def build(archive, root, members):
    out = bytearray()
    for rel in members:
        full = os.path.join(root, rel)
        with open(full, "rb") as fh:
            data = fh.read()
        out += entry(rel, data, os.path.getmtime(full))
    out += b"\x00"          # afsluitende nul-byte
    with open(archive, "wb") as fh:
        fh.write(bytes(out))
    return len(out)


def collect(root, tops):
    found = []
    for top in tops:
        full = os.path.join(root, top)
        if os.path.isfile(full):
            found.append(top)
            continue
        for dirpath, dirnames, filenames in os.walk(full):
            dirnames.sort()
            for fn in sorted(filenames):
                if fn == ".DS_Store":
                    continue
                p = os.path.join(dirpath, fn)
                found.append(os.path.relpath(p, root))
    return found


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Gebruik: make_lha.py <archief> <map> <onderdeel> ...")
        sys.exit(2)
    archive, root, tops = sys.argv[1], sys.argv[2], sys.argv[3:]
    members = collect(root, tops)
    size = build(archive, root, members)
    print("%s: %d bestanden, %d bytes" % (archive, len(members), size))
    for m in members:
        print("   ", m)
