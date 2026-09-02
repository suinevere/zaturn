#!/usr/bin/env python3
"""/*----------------------
 | gen_map_tga.py
 | Description: Re-encodes a picture into the one TGA shape the Saturn side can
 |   read, in place.
 |
 |   tga_decode in saturn/src/video/title.cxx accepts exactly one thing: an
 |   UNCOMPRESSED COLOUR-MAPPED TGA, image type 1, 8 bits per pixel, with a
 |   colour map of at most 256 entries at 24 or 32 bits, no more than 1024x512.
 |   It refuses everything else outright and the caller draws the back colour
 |   instead, which on the map page is a flat brown field -- so a wrong file
 |   does not crash, it just silently shows no parchment, which is the hardest
 |   kind of wrong to notice.
 |
 |   Index 0 is left unused and the picture is quantised to the remaining 255.
 |   tga_decode marks entry 0 transparent (`c.Opaque = (i == 0) ? 0 : 1`), so
 |   any pixel that landed there would punch through to the back colour. MAP.TGA
 |   does that deliberately over 15% of its area; a picture dropped in without
 |   that intention should be opaque everywhere, and the way to guarantee it is
 |   to never emit index 0 at all.
 |
 |   Rows are written bottom-up with descriptor 0, which is what MAP.TGA does
 |   and what tga_decode's flip expects.
 | Author: suinevere
 | Dependencies: argparse, pathlib, struct, sys, PIL
 | Globals: W, H, RESERVED
 | Run: python tools/gen_map_tga.py saturn/cd/data/TGA/MAP2.TGA ...
 ----------------------*/"""
import argparse
import pathlib
import struct
import sys

from PIL import Image

W, H = 320, 240
RESERVED = 1
"""W / H / RESERVED

Description: The screen the map page draws into, and how many palette entries
    are held back at the front. One, because index 0 is the transparent entry.
Author: suinevere
"""


def describe(path):
    """/*----------------------
     | describe
     | Description: One TGA's header fields, for saying what a file was before
     |     it is replaced.
     | Author: suinevere
     | Dependencies: struct
     | Globals: N/A
     | Params: path -- the file
     | Returns: a dict of header fields, or None when it is too short
     ----------------------*/"""
    b = path.read_bytes()[:18]
    if len(b) < 18:
        return None
    (idlen, cmaptype, imgtype, _first, cmaplen, cmapbits,
     _x, _y, w, h, depth, desc) = struct.unpack("<BBBHHBHHHHBB", b)
    return {"idlen": idlen, "cmaptype": cmaptype, "imgtype": imgtype,
            "cmaplen": cmaplen, "cmapbits": cmapbits, "w": w, "h": h,
            "depth": depth, "desc": desc}


def acceptable(d):
    """/*----------------------
     | acceptable
     | Description: Whether tga_decode would take this header. Mirrors its
     |     checks exactly rather than approximating them -- a converter that
     |     passed something the console then refused would be worse than none.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: d -- describe()'s result
     | Returns: True when the console would decode it
     ----------------------*/"""
    if not d:
        return False
    return (d["cmaptype"] == 1 and d["imgtype"] == 1 and d["depth"] == 8
            and 0 < d["cmaplen"] <= 256 and d["cmapbits"] in (24, 32)
            and 0 < d["w"] <= 1024 and 0 < d["h"] <= 512
            and 18 + d["idlen"] + d["cmaplen"] * (d["cmapbits"] // 8) <= 2048)


def encode(img):
    """/*----------------------
     | encode
     | Description: One RGB image as the bytes of an uncompressed colour-mapped
     |     TGA: 18-byte header, 256 BGR triples, then one index per pixel with
     |     the bottom row first.
     | Author: suinevere
     | Dependencies: struct, PIL
     | Globals: W, H, RESERVED
     | Params: img -- a PIL image
     | Returns: the file bytes
     ----------------------*/"""
    img = img.convert("RGB").resize((W, H), Image.LANCZOS)
    q = img.quantize(colors=256 - RESERVED, method=Image.MEDIANCUT)
    pal = q.getpalette()[: 3 * (256 - RESERVED)]

    out = bytearray(struct.pack("<BBBHHBHHHHBB", 0, 1, 1, 0, 256, 24, 0, 0,
                                W, H, 8, 0))
    out += bytes(3)
    for i in range(256 - RESERVED):
        r, g, b = pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]
        out += bytes((b, g, r))

    px = q.tobytes()
    for row in range(H - 1, -1, -1):
        line = px[row * W:(row + 1) * W]
        out += bytes(v + RESERVED for v in line)
    return bytes(out)


def main(argv):
    """/*----------------------
     | main
     | Description: Converts each named file in place, leaving one that is
     |     already acceptable alone.
     | Author: suinevere
     | Dependencies: argparse, pathlib, PIL
     | Globals: N/A
     | Params: argv -- command-line arguments
     | Returns: 0, or 2 when a file is missing
     ----------------------*/"""
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--force", action="store_true",
                    help="re-encode even a file the console would already take")
    args = ap.parse_args(argv)

    for name in args.files:
        p = pathlib.Path(name)
        if not p.is_file():
            print(f"gen_map_tga: {p} is missing", file=sys.stderr)
            return 2
        was = describe(p)
        if acceptable(was) and not args.force:
            print(f"{p.name:10s} already type {was['imgtype']} {was['depth']}bpp "
                  f"{was['w']}x{was['h']} -- left alone")
            continue
        data = encode(Image.open(p))
        p.write_bytes(data)
        now = describe(p)
        print(f"{p.name:10s} type {was['imgtype']} {was['depth']}bpp "
              f"{was['w']}x{was['h']} -> type {now['imgtype']} {now['depth']}bpp "
              f"{now['w']}x{now['h']}, {len(data)} bytes")
        if not acceptable(now):
            print(f"gen_map_tga: {p.name} still would not decode", file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
