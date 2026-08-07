#!/usr/bin/env python3
"""Convert PNG backgrounds into the 8bpp paletted TGAs the Saturn disc expects.

Usage:
    python tools/make_tga.py <src-dir> <dst-dir>    # batch (what the build runs)
    python tools/make_tga.py <src.png> <dst.tga>    # single file

Two constraints make this script necessary instead of a plain image-editor
export:

1. **8bpp paletted, never truecolor.** SRL's VRAM::AutoAllocateBmp doubles the
   VDP2 bitmap container size for RGB555, so the 512x256 container becomes 256KB
   and spans the A0/A1 VRAM bank boundary. Bank-spanning bitmaps render as
   static: slBitMapNbg0 never reserves the second bank in VDP2_RAMCTL, and SRL's
   allocator tracks banks only in software (see srl_vdp2.hpp:11-15). At 8bpp the
   container is exactly 128KB and fits one bank.

2. **Palette index 0 must be unused.** VDP2 treats index 0 on a scroll screen as
   transparent, which would punch back-color holes through the image. We
   quantize to 255 colors and shift every index up by one.

The TGA is written by hand because PIL re-optimizes the palette on save, which
silently undoes constraint 2.

Sources that are not exactly 320x224 are reported and skipped rather than
aborting the run -- the build calls this on every compile and one bad file
must not stop the rest.

The source tree is one folder per mood (tools/assets/png/WILDER/, /TOWN/,
...) and the disc keeps that shape: each mood gets its own folder under
saturn/cd/data/TGA, holding a gapless 01.TGA..NN.TGA run. The first path
component under the source root names the mood; anything below that is
provenance and is flattened away. Naming disc files by position rather than
by source filename is what lets category_art.inc synthesise a filename from
a mood and an index instead of scanning the disc at boot -- see write_inc.

A source image that sits at the root of the source tree, not inside a mood
folder, is not part of any mood's rotation -- SUINE.PNG (the boot splash) is
the one that exists today. Root-level sources keep their own stem instead of
a position: <STEM>.TGA, upper-cased, same 8.3 length rule as everything else.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
WIDTH, HEIGHT = 320, 224
MAX_STEM = 8  # ISO9660 8.3; the build passes --norock to xorrisofs
SOURCE_EXT = (".png", ".jpg", ".jpeg")

# TC_* enum order, saturn/src/sound/music.h:45. None carries no art.
ENUM_ORDER = [None, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
              "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
              None, None]

# The twelve mood folders write_inc's table has a row for. Any other directory
# under the source root is a typo, not a mood -- converting it anyway would ship
# art onto the ISO that category_art.inc's lookup can never reach (count 0),
# silently.
KNOWN_MOODS = frozenset(m for m in ENUM_ORDER if m)


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


def convert_tree(src_root, dst_root):
    """
    ----------------------
    | convert_tree
    | Description: Convert every source picture under src_root into
    |   dst_root/<MOOD>/NN.TGA, replacing each mood's existing TGAs first, and
    |   every root-level source (the boot splash) into dst_root/<STEM>.TGA. A
    |   source subdirectory not named for one of the twelve known moods is
    |   reported and skipped rather than converted -- see KNOWN_MOODS.
    | Author: suinevere
    | Dependencies: _convert_source
    | Globals: SOURCE_EXT, MAX_STEM, KNOWN_MOODS
    | Params: src_root -- source PNG tree (tools/assets/png); dst_root -- disc
    |   TGA tree (saturn/cd/data/TGA)
    | Returns: dict mapping mood name to the number of pictures written
    ----------------------
    """
    src_root, dst_root = Path(src_root), Path(dst_root)
    if not src_root.is_dir():
        print(f"  skip  {src_root} does not exist -- nothing to convert")
        return {}

    counts = {}
    for mood in sorted({p.name for p in src_root.iterdir() if p.is_dir()}):
        if mood not in KNOWN_MOODS:
            print(f"  skip  {mood}: not one of the twelve known moods "
                  f"(typo? see KNOWN_MOODS in make_tga.py)")
            continue
        out_dir = dst_root / mood
        out_dir.mkdir(parents=True, exist_ok=True)
        for old in out_dir.glob("*.TGA"):
            old.unlink()

        sources = sorted(
            p for p in (src_root / mood).rglob("*")
            if p.suffix.lower() in SOURCE_EXT
        )
        n = 0
        for src in sources:
            if n >= 99:
                print(f"  {mood}: more than 99 pictures, ignoring {src.name}")
                continue
            if _convert_source(src, out_dir / f"{n + 1:02d}.TGA"):
                n += 1
        counts[mood] = n
        print(f"  {mood}: {n}")

    root_sources = sorted(
        p for p in src_root.iterdir()
        if p.is_file() and p.suffix.lower() in SOURCE_EXT
    )
    for src in root_sources:
        stem = src.stem.upper()
        if len(stem) > MAX_STEM:
            print(f"  skip  {src.name}: name over {MAX_STEM} characters "
                  f"(ISO9660 8.3); rename it")
            continue
        if _convert_source(src, dst_root / f"{stem}.TGA"):
            print(f"  {stem}: wrote")

    return counts


def write_inc(counts, path):
    """
    ----------------------
    | write_inc
    | Description: Write the generated per-category art-count table consumed by
    |   display.c.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: ENUM_ORDER
    | Params: counts -- per-mood picture counts from convert_tree; path --
    |   output .inc file path
    | Returns: N/A
    ----------------------
    """
    row = ", ".join(str(0 if m is None else counts.get(m, 0)) for m in ENUM_ORDER)
    path.write_text(
        "/*----------------------\n"
        " | category_art.inc\n"
        " | Description: How many pictures each text category carries on this disc.\n"
        " |   GENERATED by tools/make_tga.py -- do not edit. Row order is the TC_*\n"
        " |   enum order in sound/music.h; the three zero rows are TC_NEUTRAL,\n"
        " |   TC_DANGER and TC_TRIUMPH, which hold whatever is showing.\n"
        " | Author: suinevere\n"
        " ----------------------*/\n"
        "static const unsigned char CATEGORY_ART_N[TEXT_NUM_CATEGORIES] = {\n"
        f"    {row}\n"
        "};\n"
    )


def main(argv):
    """
    ----------------------
    | main
    | Description: CLI entry point. Dispatches to the batch form (convert_tree
    |   plus write_inc) when the source is a directory or the destination is
    |   not a .tga file, otherwise the single-file form (convert_one).
    | Author: suinevere
    | Dependencies: convert_tree, convert_one, write_inc
    | Globals: REPO
    | Params: argv -- sys.argv: [prog, src, dst]
    | Returns: process exit code (0 ok, 2 on bad usage)
    ----------------------
    """
    if len(argv) != 3:
        print(__doc__)
        return 2

    src, dst = Path(argv[1]), Path(argv[2])
    if src.is_dir() or dst.suffix.lower() != ".tga":
        counts = convert_tree(src, dst)
        write_inc(counts, REPO / "saturn" / "src" / "video" / "category_art.inc")
        return 0

    status, message = convert_one(src, dst)
    print(f"  {'wrote' if status == 'wrote' else 'skip '} {message}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
