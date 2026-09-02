#!/usr/bin/env python3
"""Hold every TGA on the disc to what tga_decode will actually accept.

saturn/src/video/title.cxx's tga_decode validates the header and returns false
on anything it does not like, and every caller treats false as "show nothing".
So a picture in the wrong format is not a build failure or a crash -- it is a
blank screen, silently, on hardware. That is worth a gate here, because an image
editor's default TGA export is RLE truecolor and passes none of these: MAP.TGA
arrived that way (32bpp, imgtype 10) and would have shown nothing at all.

Six gates, each one a line in tga_decode:

  1. colour-mapped, uncompressed, 8bpp        (title.cxx: cmaptype/imgtype/bpp)
  2. colormap of 1..256 entries               (cmaplen)
  3. colormap entries 24 or 32 bits           (cmapbits)
  4. within 1024x512                          (w/h)
  5. pixels start inside the first sector      (pixoff > ss)
  6. the file holds the pixels it declares     (pixoff + npix > Size.Bytes)

And one the decoder cannot check, because it is about the layer rather than the
file: the picture is uploaded into a VDP2 bitmap container, and SRL's
AutoAllocateBmp sizes that container off thresholds. Up to 512x256 at 8bpp it is
128KB and fits one VRAM bank; past that it doubles, spans the A0/A1 boundary,
and renders as static because slBitMapNbg0 never reserves the second bank. So
the real ceiling is 512x256, not the decoder's 1024x512.

Regenerate any file this rejects with tools/gen_tga.py.

Run as a report: python saturn/tests/test_tga_assets.py
Run as tests:    pytest saturn/tests/test_tga_assets.py
"""
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
TGA_DIR = ROOT / "saturn" / "cd" / "data" / "TGA"

# tga_decode reads the header and colormap out of one sector and refuses a file
# whose pixels start past it (title.cxx, `if (pixoff > ss)`). 2048 is the value
# it falls back to when the CD reports no sector size.
SECTOR = 2048

# The VDP2 bitmap container's one-bank ceiling at 8bpp.
BMP_MAX_W = 512
BMP_MAX_H = 256


def gates(path):
    """[] when the file passes every gate, else a list of complaints."""
    d = path.read_bytes()
    if len(d) < 18:
        return ["shorter than a TGA header"]

    idlen, cmaptype, imgtype = d[0], d[1], d[2]
    cmaplen = d[5] | (d[6] << 8)
    cmapbits = d[7]
    w = d[12] | (d[13] << 8)
    h = d[14] | (d[15] << 8)
    bpp = d[16]

    bad = []
    if cmaptype != 1 or imgtype != 1 or bpp != 8:
        bad.append(f"cmaptype={cmaptype} imgtype={imgtype} bpp={bpp}, "
                   f"tga_decode wants 1/1/8 (uncompressed colour-mapped 8bpp)")
    if not 0 < cmaplen <= 256:
        bad.append(f"colormap has {cmaplen} entries, tga_decode wants 1..256")
    if cmapbits not in (24, 32):
        bad.append(f"colormap entries are {cmapbits}-bit, tga_decode wants 24 or 32")
    if not (0 < w <= 1024 and 0 < h <= 512):
        bad.append(f"{w}x{h} is outside tga_decode's 1024x512")
    if not (w <= BMP_MAX_W and h <= BMP_MAX_H):
        bad.append(f"{w}x{h} is past the one-bank VDP2 container ceiling of "
                   f"{BMP_MAX_W}x{BMP_MAX_H} -- it would render as static")

    pixoff = 18 + idlen + cmaplen * (cmapbits // 8 if cmapbits else 0)
    if pixoff > SECTOR:
        bad.append(f"pixels start at {pixoff}, past the first {SECTOR}-byte sector")
    if pixoff + w * h > len(d):
        bad.append(f"declares {w}x{h} from offset {pixoff} but the file is "
                   f"{len(d)} bytes")
    return bad


def shipped():
    return sorted(TGA_DIR.glob("*.TGA")) if TGA_DIR.is_dir() else []


def main():
    files = shipped()
    if not files:
        print(f"no TGAs under {TGA_DIR}", file=sys.stderr)
        return 1
    fails = 0
    for f in files:
        bad = gates(f)
        d = f.read_bytes()
        w, h = d[12] | (d[13] << 8), d[14] | (d[15] << 8)
        if bad:
            print(f"  FAIL {f.name}", file=sys.stderr)
            for why in bad:
                print(f"       {why}", file=sys.stderr)
            fails += 1
        else:
            print(f"  ok   {f.name:12s} {w}x{h} 8bpp, "
                  f"{d[5] | (d[6] << 8)} colours, {len(d)} bytes")
    if fails:
        print(f"test_tga_assets: {fails} FAILED -- regenerate with "
              f"tools/gen_tga.py", file=sys.stderr)
        return 1
    print(f"test_tga_assets: OK ({len(files)} files)")
    return 0


@pytest.mark.parametrize("path", shipped(), ids=lambda p: p.name)
def test_tga_asset(path):
    """One case per shipped picture, so a failure names the file."""
    assert gates(path) == []


def test_map_parchment_present():
    """The map's own background, which map_view.cxx opens by name. Absent, the
    map still draws -- it falls back to the flat back colour -- so nothing else
    would report this."""
    assert (TGA_DIR / "MAP.TGA").is_file()


def test_title_plate_present():
    """The title screen's ZATURN plate. Absent, title_logo_show returns false and
    the title screen is a wallpaper with two lines of text on it and no name."""
    assert (TGA_DIR / "LOGO.TGA").is_file()


def _title_const(name):
    """One #define out of title.cxx, so the layout below cannot drift from it."""
    src = (ROOT / "saturn" / "src" / "video" / "title.cxx").read_text()
    m = re.search(r"^#define\s+%s\s+(\d+)" % name, src, re.M)
    assert m, f"title.cxx no longer defines {name}"
    return int(m.group(1))


def test_title_plate_fits_its_window():
    """Two ceilings the plate has to clear, neither of which says anything when
    it does not.

    title_logo_show writes the picture into NBG1's container by hand at
    TITLE_LOGO_Y, and refuses outright -- silently, leaving no plate -- if it
    would run off the container's 512-byte row or past its 256th. And the credit
    line is printed on the text grid at TITLE_CREDIT_ROW, which nothing stops the
    plate from growing down into: a taller logo would simply be drawn over it.

    Both are things a redrawn logo would trip, so they are held against the
    file's own header rather than against a remembered size."""
    d = (TGA_DIR / "LOGO.TGA").read_bytes()
    w, h = d[12] | (d[13] << 8), d[14] | (d[15] << 8)
    top = _title_const("TITLE_LOGO_Y")
    credit = _title_const("TITLE_CREDIT_ROW") * 8   # 8-pixel rows on the 40x30 grid

    assert w <= 512, (
        f"the plate is {w} wide and NBG1's container row is 512 -- "
        "title_logo_show would refuse it and the title screen would have no name")
    assert top + h <= 256, (
        f"the plate runs from {top} to {top + h} and the container is 256 rows -- "
        "title_logo_show would refuse it and the title screen would have no name")
    assert top + h <= credit, (
        f"the plate runs from {top} to {top + h} and the credit line starts at "
        f"{credit} -- the plate would be drawn over it. Move TITLE_LOGO_Y up, or "
        "TITLE_CREDIT_ROW and TITLE_PROMPT_ROW down.")


if __name__ == "__main__":
    sys.exit(main())
