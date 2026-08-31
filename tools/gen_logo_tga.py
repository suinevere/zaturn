#!/usr/bin/env python3
"""/*----------------------
 | gen_logo_tga.py
 | Description: Converts tools/assets/png/SUINE.PNG into the 8bpp paletted TGA
 |     the boot splash reads (saturn/cd/data/TGA/SUINE.TGA).
 |
 |     One picture, and there will not be a second. This is what survives of
 |     tools/make_tga.py and then of gen_title_art.py: the first converted a
 |     per-game, per-scene tree of downloaded pictures for the retired category
 |     art system, the second a folder of title-screen wallpapers that never
 |     shipped. Room backgrounds are Zork I's own CGL frames, injected as
 |     archives by tools/assets/bg.bat and decoded on the Saturn, and the title
 |     screen shows one of those too -- so the SUINEVERE logo is the only TGA
 |     left on the disc.
 |
 |     Two constraints keep the hand-rolled encoder necessary rather than an
 |     image-editor export, both inherited unchanged:
 |
 |     1. 8bpp paletted, never truecolor. SRL's VRAM::AutoAllocateBmp doubles
 |        the VDP2 bitmap container for RGB555, so a 512x256 container becomes
 |        256KB and spans the A0/A1 bank boundary; a bank-spanning bitmap
 |        renders as static because slBitMapNbg0 never reserves the second bank.
 |        At 8bpp the container is exactly 128KB and fits one bank.
 |     2. Palette index 0 must be unused. VDP2 treats index 0 on a scroll screen
 |        as transparent, which would punch back-color holes through the image,
 |        so this quantizes to 255 colors and shifts every index up by one.
 |        The splash's fade depends on it as well: the hardware colour offset
 |        cannot darken what shows through a transparent hole, so a logo whose
 |        black fill landed on index 0 would fade its glyphs and leave the
 |        surround fixed (see splash.cxx).
 |
 |     The TGA is written by hand because PIL re-optimizes the palette on save,
 |     which silently undoes constraint 2.
 | Author: suinevere
 | Dependencies: PIL.Image, pathlib, struct, sys
 | Globals: ROOT, PNG_DIR, TGA_DIR, LOGO_SRC, LOGO_DST, WIDTH, HEIGHT
 | Run: python tools/gen_logo_tga.py [png_dir] [tga_dir]
 ----------------------*/"""
import pathlib
import struct
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
PNG_DIR = ROOT / "tools" / "assets" / "png"
TGA_DIR = ROOT / "saturn" / "cd" / "data" / "TGA"
LOGO_SRC = "SUINE.PNG"
LOGO_DST = "SUINE.TGA"

# The VDP2 bitmap the splash uploads into. A source of any other size is
# refused rather than scaled: a logo is not a picture to resample blind.
WIDTH = 320
HEIGHT = 224


def encode_tga(im):
    """
    ----------------------
    | encode_tga
    | Description: Pack a WIDTHxHEIGHT RGB image into a complete 8bpp paletted
    |   TGA file image, index 0 reserved (VDP2 reads it as transparent on a
    |   scroll screen) and colormap entries stored BGR, per the TGA spec.
    | Author: suinevere
    | Dependencies: PIL.Image, struct
    | Globals: N/A
    | Params: im -- a PIL Image in any mode, already the right size
    | Returns: the complete TGA file, as bytes
    ----------------------
    """
    w, h = im.size
    q = im.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    idx = q.tobytes()
    ncolors = max(idx) + 1

    flat = q.getpalette()[: ncolors * 3]
    rgb = [tuple(flat[i * 3 : i * 3 + 3]) for i in range(ncolors)]

    # Reserve index 0: shift colors up one slot, pixels follow.
    palette = [(0, 0, 0)] + rgb
    pixels = bytes(b + 1 for b in idx)
    if 0 in pixels:
        raise AssertionError("index 0 must stay unused (VDP2 reads it as transparent)")
    if not (max(pixels) < len(palette) <= 256):
        raise AssertionError("palette overflow: indices must fit the colormap")

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
    cmap = b"".join(bytes((b, g, r)) for (r, g, b) in palette)  # TGA colormaps are BGR
    rows = [pixels[y * w : (y + 1) * w] for y in range(h)]
    body = b"".join(reversed(rows))  # bottom-left origin: rows bottom-to-top
    return header + cmap + body


def convert_logo(src_root, dst_root):
    """/*----------------------
     | convert_logo
     | Description: Converts src_root/SUINE.PNG into dst_root/SUINE.TGA. A
     |     missing source converts nothing and returns False rather than
     |     raising: the committed TGA is what the build then uses, which is the
     |     same tolerance every other asset step here has. A source of the wrong
     |     size is refused the same way, because the splash uploads it into a
     |     fixed VDP2 bitmap.
     | Author: suinevere
     | Dependencies: PIL.Image, encode_tga
     | Globals: LOGO_SRC, LOGO_DST, WIDTH, HEIGHT
     | Params: src_root -- source PNG tree; dst_root -- disc TGA tree
     | Returns: True if the TGA was written, False if the source was skipped
     ----------------------*/"""
    src = pathlib.Path(src_root) / LOGO_SRC
    dst = pathlib.Path(dst_root) / LOGO_DST
    if not src.is_file():
        print(f"  skip  {LOGO_SRC}: {src} does not exist -- nothing to convert")
        return False
    try:
        im = Image.open(src).convert("RGB")
        if im.size != (WIDTH, HEIGHT):
            print(f"  skipped {src}: {im.size} is not {WIDTH}x{HEIGHT}")
            return False
        blob = encode_tga(im)
    except AssertionError:
        raise
    except Exception as exc:
        print(f"  skipped {src}: {exc}")
        return False

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(blob)
    print(f"  {LOGO_DST}: {WIDTH}x{HEIGHT} 8bpp, index 0 reserved, {len(blob)} bytes")
    return True


def main(argv):
    """/*----------------------
     | main
     | Description: Converts the boot logo. Takes the source PNG tree and the
     |     disc TGA tree as optional positional arguments, defaulting to this
     |     repo's own, because the build wrapper passes both and a hand run
     |     wants neither. Exits 0 on a skipped source, same as the wrapper does:
     |     the committed TGA covers it.
     | Author: suinevere
     | Dependencies: convert_logo
     | Globals: PNG_DIR, TGA_DIR
     | Params: argv -- argument list without the program name
     | Returns: 0
     ----------------------*/"""
    src = pathlib.Path(argv[0]) if len(argv) > 0 else PNG_DIR
    dst = pathlib.Path(argv[1]) if len(argv) > 1 else TGA_DIR
    convert_logo(src, dst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
