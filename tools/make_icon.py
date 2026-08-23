#!/usr/bin/env python3
"""
Maakt een AmigaOS-icoon (.info).

Het formaat is binair en goed gedocumenteerd: een DiskObject-kop met daarin
een Gadget, gevolgd door een of twee Image-structuren met planaire
bitmapdata, en tot slot de ToolTypes.

Zonder icoon is een programma op Workbench onzichtbaar, en dat is voor veel
Amiga-gebruikers hetzelfde als niet bestaan.
"""

import struct
import sys

# Kleuren van het standaard Workbench-palet:
#   0 = grijs (achtergrond)  1 = zwart  2 = wit  3 = blauw
GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3

WBDRAWER, WBTOOL, WBPROJECT = 2, 3, 4

GFLG_GADGIMAGE  = 0x0004
GFLG_GADGHBOX   = 0x0002     # geselecteerd = omgekeerd getekend
GTYP_BOOLGADGET = 0x0001
GACT_RELVERIFY  = 0x0001


def planar(pixels, width, height, depth):
    """Zet een tabel met kleurnummers om naar Amiga-bitplanes.

    Elke plane bevat een bit per pixel, per rij afgerond op hele words.
    """
    words = (width + 15) // 16
    out = bytearray()
    for plane in range(depth):
        for y in range(height):
            row = 0
            for x in range(words * 16):
                bit = 0
                if x < width and (pixels[y][x] >> plane) & 1:
                    bit = 1
                row = (row << 1) | bit
            out += row.to_bytes(words * 2, "big")
    return bytes(out)


def image_chunk(pixels, width, height, depth=2):
    """Een Image-structuur plus zijn bitmapdata."""
    img = struct.pack(">hhHHH IBB I",
                      0, 0,            # LeftEdge, TopEdge
                      width, height, depth,
                      1,               # ImageData (niet-nul = volgt hierna)
                      (1 << depth) - 1,  # PlanePick
                      0,               # PlaneOnOff
                      0)               # NextImage
    return img + planar(pixels, width, height, depth)


def build(pixels, sel_pixels, width, height, icon_type, tooltypes,
          stack=65536, default_tool=None):
    depth = 2
    has_sel = sel_pixels is not None

    gadget = struct.pack(">I hhhh HHH II I I I H I",
                         0,                     # NextGadget
                         0, 0, width, height,   # positie en formaat
                         GFLG_GADGIMAGE | (GFLG_GADGHBOX if has_sel else 0),
                         GACT_RELVERIFY,
                         GTYP_BOOLGADGET,
                         1,                     # GadgetRender: volgt
                         1 if has_sel else 0,   # SelectRender
                         0,                     # GadgetText
                         0,                     # MutualExclude
                         0,                     # SpecialInfo
                         0,                     # GadgetID
                         0)                     # UserData
    assert len(gadget) == 44, len(gadget)

    disk = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BB I I II I I I",
        icon_type, 0,                    # do_Type + opvulbyte
        1 if default_tool else 0,        # do_DefaultTool
        1 if tooltypes else 0,           # do_ToolTypes
        0x80000000, 0x80000000,          # CurrentX/Y: NO_ICON_POSITION
        0,                               # DrawerData
        0,                               # ToolWindow
        stack)
    assert len(disk) == 78, len(disk)

    out = bytearray(disk)
    out += image_chunk(pixels, width, height, depth)
    if has_sel:
        out += image_chunk(sel_pixels, width, height, depth)

    if default_tool:
        data = default_tool.encode("latin-1") + b"\0"
        out += struct.pack(">I", len(data)) + data

    if tooltypes:
        # Aantal + 1, maal 4: zo verwacht icon.library het.
        out += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for tt in tooltypes:
            data = tt.encode("latin-1") + b"\0"
            out += struct.pack(">I", len(data)) + data

    return bytes(out)


def draw_tool(width=46, height=24):
    """Een pijl omlaag boven een bakje: iets binnenhalen en neerzetten."""
    px = [[GREY] * width for _ in range(height)]

    def hline(x0, x1, y, col):
        for x in range(max(0, x0), min(width, x1 + 1)):
            if 0 <= y < height:
                px[y][x] = col

    cx = width // 2

    # schacht van de pijl
    for y in range(1, 8):
        hline(cx - 2, cx + 1, y, BLUE)
        px[y][cx - 2] = BLACK
        px[y][cx + 1] = BLACK

    # punt van de pijl: breed beginnen, naar beneden toelopen
    for i in range(7):
        x0, x1 = cx - 7 + i, cx + 6 - i
        hline(x0, x1, 8 + i, BLUE)
        if x0 >= 0:
            px[8 + i][x0] = BLACK
        if x1 < width:
            px[8 + i][x1] = BLACK
    hline(cx - 7, cx + 6, 8, BLACK)

    # bakje eronder
    top, bot = 16, height - 2
    for y in range(top, bot + 1):
        px[y][4] = BLACK
        px[y][width - 5] = BLACK
        if top < y < bot:
            hline(5, width - 6, y, WHITE)
    hline(4, width - 5, bot, BLACK)
    hline(4, width - 5, top, BLACK)
    return px


def draw_drawer(width=46, height=24):
    px = [[GREY] * width for _ in range(height)]

    def rect(x0, y0, x1, y1, col, fill=False):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if fill or x in (x0, x1) or y in (y0, y1):
                    if 0 <= y < height and 0 <= x < width:
                        px[y][x] = col

    rect(3, 5, width - 4, height - 3, BLACK)
    rect(4, 6, width - 5, height - 4, WHITE, fill=True)
    rect(3, 2, width // 2, 5, BLACK)
    rect(4, 3, width // 2 - 1, 4, BLUE, fill=True)
    return px


def invert(px):
    """Geselecteerde staat: wit en blauw wisselen, zwart blijft."""
    swap = {WHITE: BLUE, BLUE: WHITE}
    return [[swap.get(c, c) for c in row] for row in px]


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "tool"
    path = sys.argv[2] if len(sys.argv) > 2 else "out.info"

    if what == "drawer":
        px = draw_drawer()
        data = build(px, invert(px), len(px[0]), len(px), WBDRAWER, None,
                     stack=4096)
    elif what == "installer":
        px = draw_tool()
        data = build(px, invert(px), len(px[0]), len(px), WBPROJECT, None,
                     stack=32768, default_tool="Installer")
    elif what == "guide":
        px = draw_tool()
        data = build(px, invert(px), len(px[0]), len(px), WBPROJECT, None,
                     stack=4096, default_tool="SYS:Utilities/MultiView")
    else:
        px = draw_tool()
        data = build(px, invert(px), len(px[0]), len(px), WBTOOL, [
            "BACKEND=native",
            "(BACKEND=proxy|native)",
            "DESTDIR=RAM:",
            "MAXRELEASES=1",
            "(PROXYHOST=address of your own proxy)",
            "(PROXYPORT=8080)",
        ])

    open(path, "wb").write(data)
    print("%s: %d bytes" % (path, len(data)))
