#!/usr/bin/env python3
"""/*----------------------
 | gen_tga.py
 | Description: Converts any image into the uncompressed 8bpp colour-mapped TGA
 |     that saturn/src/video/title.cxx's tga_decode accepts, for the pictures
 |     under saturn/cd/data/TGA.
 |
 |     Run by hand, never by the build. It is what survives of make_tga.py, then
 |     gen_title_art.py, then gen_logo_tga.py -- each of which converted a tree
 |     of pictures for a system that has since been retired, and the last of
 |     which was deleted once the boot logo was the only TGA left. The disc has
 |     three now (the logo, the title screen and the map parchment), all of them
 |     committed, so nothing needs converting on a build and this exists only
 |     for the day one of them is redrawn.
 |
 |     Three constraints keep a hand-rolled encoder necessary rather than an
 |     image-editor export, the first two inherited unchanged:
 |
 |     1. 8bpp paletted, never truecolor. SRL's VRAM::AutoAllocateBmp doubles
 |        the VDP2 bitmap container for RGB555, so a 512x256 container becomes
 |        256KB and spans the A0/A1 bank boundary; a bank-spanning bitmap
 |        renders as static because slBitMapNbg0 never reserves the second bank.
 |        At 8bpp the container is exactly 128KB and fits one bank.
 |     2. Index 0 is transparent and no opaque pixel may land on it. VDP2 reads
 |        index 0 on a scroll screen as transparent, and tga_decode builds its
 |        CLUT with Opaque = 0 for that entry alone, so an opaque pixel there
 |        punches a back-colour hole. The splash's fade depends on it too: the
 |        hardware colour offset cannot darken what shows through a hole, so a
 |        logo whose black fill landed on index 0 would fade its glyphs and
 |        leave the surround fixed (see splash.cxx).
 |     3. A source alpha channel is honoured rather than flattened, because
 |        index 0 is exactly what it wants: the map parchment's torn edges are
 |        transparent and are meant to show the layer behind. Transparent
 |        pixels are painted over with an opaque colour before quantizing so
 |        they cannot spend palette slots on a colour nothing will draw, then
 |        forced back to 0 afterwards.
 |
 |     The TGA is written by hand because PIL re-optimizes the palette on save,
 |     which silently undoes constraint 2.
 | Author: suinevere
 | Dependencies: PIL.Image, argparse, collections, pathlib, struct, sys
 | Globals: ALPHA_CUT, MAX_W, MAX_H, SECTOR
 | Run: python tools/gen_tga.py <source image> <dest .TGA>
 ----------------------*/"""
import argparse
import collections
import pathlib
import struct
import sys

from PIL import Image

# Below this the source pixel is a hole, at or above it the pixel is drawn.
# Halfway, because nothing on this disc uses partial alpha for anything but the
# antialiased rim of a torn edge and VDP2 has no partial transparency to give it.
ALPHA_CUT = 128

# tga_decode's own bounds, restated so a picture it would refuse is refused here
# instead -- on the Saturn the refusal is a silent blank screen.
MAX_W = 1024
MAX_H = 512

# tga_decode reads the header and the colormap out of the first sector and
# refuses a file whose pixels start past it. 18 + 256*3 is 786, so this can only
# bite a hand-edited file, but it is the check that would be silent.
SECTOR = 2048


def encode_tga(im):
    """
    ----------------------
    | encode_tga
    | Description: Packs an image into a complete 8bpp colour-mapped TGA, index
    |   0 reserved for transparency, colormap entries stored BGR and rows
    |   bottom-to-top per the TGA spec.
    | Author: suinevere
    | Dependencies: PIL.Image, collections, struct
    | Globals: ALPHA_CUT
    | Params: im -- a PIL Image in any mode
    | Returns: (the complete TGA file as bytes, how many pixels are transparent)
    ----------------------
    """
    rgba = im.convert("RGBA")
    w, h = rgba.size
    alpha = rgba.getchannel("A")
    holes = [i for i, a in enumerate(alpha.tobytes()) if a < ALPHA_CUT]

    # Paint the holes over with the commonest opaque colour before quantizing.
    # Left as they came, a transparent region's own colour -- usually the black
    # an editor leaves under an erased area -- takes palette slots away from the
    # picture that is actually drawn.
    rgb = rgba.convert("RGB")
    if holes:
        opaque = [rgb.getpixel((i % w, i // w))
                  for i, a in enumerate(alpha.tobytes()) if a >= ALPHA_CUT]
        if not opaque:
            raise SystemExit("every pixel is transparent -- nothing to encode")
        fill = collections.Counter(opaque).most_common(1)[0][0]
        px = rgb.load()
        for i in holes:
            px[i % w, i // w] = fill

    q = rgb.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    idx = bytearray(b + 1 for b in q.tobytes())
    for i in holes:
        idx[i] = 0

    ncolors = max(q.tobytes()) + 1
    flat = q.getpalette()[: ncolors * 3]
    palette = [(0, 0, 0)] + [tuple(flat[i * 3: i * 3 + 3]) for i in range(ncolors)]

    if not holes and 0 in idx:
        raise AssertionError("index 0 must stay unused (VDP2 reads it as transparent)")
    if max(idx) >= len(palette):
        raise AssertionError("palette overflow: indices must fit the colormap")
    if not 0 < len(palette) <= 256:
        raise AssertionError("colormap must hold 1 to 256 entries")

    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,              # no image ID
        1,              # colormap present
        1,              # uncompressed paletted
        0,              # colormap start
        len(palette),   # colormap length
        24,             # colormap entry depth
        0, 0,           # origin x/y
        w, h,
        8,              # 8bpp indices
        0x00,           # bottom-left origin, no alpha bits
    )
    cmap = b"".join(bytes((b, g, r)) for (r, g, b) in palette)
    if len(header) + len(cmap) > SECTOR:
        raise AssertionError("header and colormap must fit tga_decode's first sector")
    rows = [bytes(idx[y * w: (y + 1) * w]) for y in range(h)]
    body = b"".join(reversed(rows))  # bottom-left origin: rows bottom-to-top
    return header + cmap + body, len(holes)


def convert(src, dst):
    """
    ----------------------
    | convert
    | Description: Converts one image file into one TGA, refusing rather than
    |   resampling a source the hardware cannot take -- a picture is uploaded
    |   into a fixed VDP2 bitmap and is not something to resize blind.
    | Author: suinevere
    | Dependencies: PIL.Image, encode_tga
    | Globals: MAX_W, MAX_H
    | Params: src -- source image path; dst -- destination .TGA path
    | Returns: 0 on success, 1 on refusal
    ----------------------
    """
    src, dst = pathlib.Path(src), pathlib.Path(dst)
    if not src.is_file():
        print(f"{src}: does not exist", file=sys.stderr)
        return 1

    im = Image.open(src)
    w, h = im.size
    if w <= 0 or h <= 0 or w > MAX_W or h > MAX_H:
        print(f"{src}: {w}x{h} is outside what tga_decode accepts "
              f"(1..{MAX_W} by 1..{MAX_H})", file=sys.stderr)
        return 1

    blob, holes = encode_tga(im)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(blob)
    ncolors = blob[5] | (blob[6] << 8)
    print(f"{dst.name}: {w}x{h} 8bpp, {ncolors} colours, "
          f"{holes} transparent pixels, {len(blob)} bytes")
    return 0


def main(argv):
    """
    ----------------------
    | main
    | Description: Parses the two paths and converts.
    | Author: suinevere
    | Dependencies: argparse, convert
    | Globals: N/A
    | Params: argv -- argument list without the program name
    | Returns: 0 on success, 1 on refusal
    ----------------------
    """
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[2].strip(" |"))
    ap.add_argument("source", help="any image PIL can open")
    ap.add_argument("dest", help="the .TGA to write under saturn/cd/data/TGA")
    args = ap.parse_args(argv)
    return convert(args.source, args.dest)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
