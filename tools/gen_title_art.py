#!/usr/bin/env python3
"""/*----------------------
 | gen_title_art.py
 | Description: Converts tools/assets/png/TITLE/*.png into the 8bpp paletted
 |     TGAs the title screen reads (saturn/cd/data/TGA/TITLE/01.TGA..NN.TGA)
 |     and writes saturn/src/scene/title_art.inc with the count.
 |
 |     This is what survives of tools/make_tga.py. That script converted a
 |     per-game, per-scene tree of downloaded pictures for the retired
 |     category art system, and generated title_art.inc on the side -- so
 |     deleting it wholesale would have taken the title screen's generator with
 |     it. Room backgrounds no longer come through here at all: they are Zork
 |     I's own CGL frames, injected as archives by tools/assets/bg.bat and
 |     decoded on the Saturn, never converted to TGA.
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
 |
 |     The TGA is written by hand because PIL re-optimizes the palette on save,
 |     which silently undoes constraint 2.
 | Author: suinevere
 | Dependencies: PIL.Image, pathlib, sys
 | Globals: ROOT, PNG_DIR, TGA_DIR, INC, WIDTH, HEIGHT, SOURCE_EXT
 | Run: python tools/gen_title_art.py
 ----------------------*/"""
import pathlib
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
PNG_DIR = ROOT / "tools" / "assets" / "png"
TGA_DIR = ROOT / "saturn" / "cd" / "data" / "TGA"
INC = ROOT / "saturn" / "src" / "scene" / "title_art.inc"




def encode_tga(im):
    """
    ----------------------
    | encode_tga
    | Description: Pack a 320x224 RGB image into a complete 8bpp paletted TGA
    |   file image, index 0 reserved (VDP2 reads it as transparent on a scroll
    |   screen) and colormap entries stored BGR, per the TGA spec.
    | Author: suinevere
    | Dependencies: PIL.Image, struct
    | Globals: N/A
    | Params: im -- a 320x224 PIL Image, any mode
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


def convert_one(src, dst):
    """
    ----------------------
    | convert_one
    | Description: Convert a single PNG at src to a TGA at dst, for the
    |   single-file CLI form. Reports a size mismatch as a skip rather than
    |   raising, matching the batch form's one-bad-file tolerance.
    | Author: suinevere
    | Dependencies: PIL.Image, encode_tga
    | Globals: WIDTH, HEIGHT
    | Params: src -- source picture path; dst -- destination .TGA path
    | Returns: (status, message), status 'wrote' or 'skip'
    ----------------------
    """
    im = Image.open(src).convert("RGB")
    w, h = im.size
    if (w, h) != (WIDTH, HEIGHT):
        return ("skip", f"{src.name}: expected {WIDTH}x{HEIGHT}, got {w}x{h}")

    blob = encode_tga(im)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(blob)
    return ("wrote", f"{dst.name}: {w}x{h} 8bpp, index 0 reserved, {len(blob)} bytes")


def _convert_source(src, dst):
    """
    ----------------------
    | _convert_source
    | Description: Convert one source picture straight to dst, open, encode and
    |   write as a single guarded unit so one bad file costs one picture, not
    |   the run. AssertionError (a genuine encoder bug) is left to propagate.
    | Author: suinevere
    | Dependencies: PIL.Image, encode_tga
    | Globals: WIDTH, HEIGHT
    | Params: src -- source picture path; dst -- destination .TGA path
    | Returns: True if dst was written, False if src was skipped
    ----------------------
    """
    try:
        im = Image.open(src).convert("RGB")
        if im.size != (WIDTH, HEIGHT):
            print(f"  skipped {src}: {im.size} is not {WIDTH}x{HEIGHT}")
            return False
        dst.write_bytes(encode_tga(im))
        return True
    except AssertionError:
        raise
    except Exception as exc:
        print(f"  skipped {src}: {exc}")
        return False


def convert_title(src_root, dst_root):
    """/*----------------------
     | convert_title
     | Description: Converts tools/assets/png/TITLE/*.png into
     |     dst_root/TITLE/NN.TGA. The title screen needs a wallpaper at boot,
     |     before any game is chosen, so it is a flat gapless run addressed by
     |     literal filename in C rather than routed through any per-game table.
     |     Clears the folder's existing TGAs first, so a renamed or deleted
     |     source leaves no orphan behind. A missing source folder converts
     |     nothing and returns 0 rather than raising, since the pictures may
     |     not exist yet -- which is the current state.
     | Author: suinevere
     | Dependencies: _convert_source
     | Globals: SOURCE_EXT
     | Params: src_root -- source PNG tree; dst_root -- disc TGA tree
     | Returns: the number of pictures converted
     ----------------------*/"""
    title_src = pathlib.Path(src_root) / "TITLE"
    out_dir = pathlib.Path(dst_root) / "TITLE"
    if not title_src.is_dir():
        print(f"  skip  TITLE: {title_src} does not exist -- nothing to convert")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.TGA"):
        old.unlink()

    sources = sorted(p for p in title_src.iterdir()
                     if p.is_file() and p.suffix.lower() in SOURCE_EXT)
    n = 0
    for s in sources:
        if n >= 99:
            print(f"  TITLE: more than 99 pictures, ignoring {s.name}")
            continue
        if _convert_source(s, out_dir / f"{n + 1:02d}.TGA"):
            n += 1
    print(f"  TITLE: {n}")
    return n


def write_title_inc(n, path):
    """/*----------------------
     | write_title_inc
     | Description: Writes TITLE_ART_N, the picture count the title screen
     |     iterates by. C addresses the pictures by literal filename
     |     (TITLE/01.TGA..NN.TGA) and uses this constant only to bound that
     |     walk, so a count larger than the folder would name a file the disc
     |     lacks -- which is why it is generated from what actually converted
     |     rather than from what was offered.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: N/A
     | Params: n -- picture count from convert_title; path -- output .inc path
     | Returns: N/A
     ----------------------*/"""
    text = (
        "/*----------------------\n"
        " | title_art.inc\n"
        " | Description: GENERATED FILE -- do not edit by hand; produced by\n"
        " |   tools/gen_title_art.py. The picture count for the shared TITLE/\n"
        " |   folder (saturn/cd/data/TGA/TITLE/01.TGA..NN.TGA), addressed by\n"
        " |   literal filename since the title screen has no game to route it\n"
        " |   through.\n"
        " | Author: suinevere\n"
        " ----------------------*/\n"
        f"#define TITLE_ART_N {n}\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main():
    """/*----------------------
     | main
     | Description: Converts the title pictures and rewrites the count.
     | Author: suinevere
     | Dependencies: convert_title, write_title_inc
     | Globals: PNG_DIR, TGA_DIR, INC
     | Params: N/A
     | Returns: 0
     ----------------------*/"""
    n = convert_title(PNG_DIR, TGA_DIR)
    write_title_inc(n, INC)
    print(f"Wrote {INC.relative_to(ROOT)}: TITLE_ART_N {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
