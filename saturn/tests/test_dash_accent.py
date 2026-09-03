#!/usr/bin/env python3
"""The map's crosshair is drawn in one palette entry that dash_view leaves
untinted, so it stays red on a tan sheet while every other tile bends toward
the background's hue. Three things have to agree for that to hold and nothing
in the compiler can catch any of them drifting.

The index lives in three places: PAL_ACCENT in the generator, DASH_PAL_ACCENT
in dash_tiles.h, and the `if (i == DASH_PAL_ACCENT)` arm of write_palette. If
the generator and the header disagree the build still links -- it just tints
the cursor back into the paper, or leaves one stone tile glowing red.

The slot is only free because nothing else can reach it: marble() caps its
veins two steps below and every frame, rule and mark names an entry on either
side. That is a property of the generator's numbers, not a reservation, so it
is checked against the emitted tiles rather than trusted -- a new mark or a
raised vein cap would take the slot silently.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "tools" / "gen_dash_tiles.py"
HDR = ROOT / "saturn" / "src" / "video" / "dash_tiles.h"
SRC = ROOT / "saturn" / "src" / "video" / "dash_tiles.c"
VIEW = ROOT / "saturn" / "src" / "video" / "dash_view.cxx"

# DT_XHAIR_TL..BR, as offsets from DT_ROOM_SEL, which opens the run of map
# marks that ends the tile set. Read off the enum in dash_map.h rather than
# recomputed here, so a tile inserted before them moves this with it.
XHAIR_NAMES = ("DT_XHAIR_TL", "DT_XHAIR_TR", "DT_XHAIR_BL", "DT_XHAIR_BR")


def accent_index():
    """PAL_ACCENT and DASH_PAL_ACCENT, which must be the same number."""
    gen = re.search(r"^PAL_ACCENT\s*=\s*(\d+)\s*$", GEN.read_text(encoding="utf-8"), re.M)
    hdr = re.search(r"^#define\s+DASH_PAL_ACCENT\s+(\d+)\s*$", HDR.read_text(encoding="utf-8"), re.M)
    assert gen is not None, "tools/gen_dash_tiles.py defines no PAL_ACCENT"
    assert hdr is not None, "dash_tiles.h defines no DASH_PAL_ACCENT"
    assert int(gen.group(1)) == int(hdr.group(1)), (
        "PAL_ACCENT %s and DASH_PAL_ACCENT %s disagree" % (gen.group(1), hdr.group(1)))
    return int(gen.group(1))


def tiles():
    """dash_tile_data as a list of 64-nibble lists, one per tile."""
    text = SRC.read_text(encoding="utf-8")
    body = text[text.index("dash_tile_data"):]
    out = []
    for row in re.findall(r"\{([^{}]*)\}", body):
        by = re.findall(r"0x([0-9A-Fa-f]{2})", row)
        if len(by) != 32:
            continue
        px = []
        for b in by:
            v = int(b, 16)
            px.append(v >> 4)
            px.append(v & 15)
        out.append(px)
    assert out, "no tiles parsed out of dash_tiles.c"
    return out


def xhair_tiles():
    """The four crosshair tiles' indices into dash_tile_data."""
    text = (ROOT / "saturn" / "src" / "video" / "dash_map.h").read_text(encoding="utf-8")
    body = text[text.index("enum {"):]
    order, value = [], 0
    for tok in re.findall(r"(DT_[A-Z0-9_]+)\s*(?:=\s*([A-Za-z0-9_ +]+))?", body):
        name, init = tok
        if init.strip():
            init = init.strip()
            if init.isdigit():
                value = int(init)
            else:
                m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*\+\s*(\d+)$", init)
                assert m, "unparsed enum initialiser %r" % init
                value = dict(order)[m.group(1)] + int(m.group(2))
        order.append((name, value))
        value += 1
    seen = dict(order)
    return [seen[n] for n in XHAIR_NAMES]


def test_generator_and_header_name_the_same_slot():
    accent_index()


def test_write_palette_passes_the_accent_through():
    """The untinted arm exists and is keyed on the define, not on a literal."""
    text = VIEW.read_text(encoding="utf-8")
    assert "if (i == DASH_PAL_ACCENT)" in text, \
        "write_palette no longer exempts the accent from the tint"


def test_only_the_crosshair_uses_the_accent():
    """Nothing on the stone reaches the slot, so the marble and the frames are
    untouched by giving it a colour."""
    a = accent_index()
    xh = set(xhair_tiles())
    for i, px in enumerate(tiles()):
        used = a in px
        if i in xh:
            assert used, "crosshair tile %d is not drawn in the accent" % i
        else:
            assert not used, "tile %d uses the accent slot %d" % (i, a)


def test_the_accent_is_a_colour_not_a_grey():
    """A grey here would tint like the ramp does and defeat the exemption."""
    a = accent_index()
    text = SRC.read_text(encoding="utf-8")
    row = re.search(r"dash_palette\[16\]\s*=\s*\{([^}]*)\}", text).group(1)
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", row)]
    assert len(vals) == 16
    v = vals[a]
    r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
    assert r > g + 8 and r > b + 8, \
        "accent 0x%04X is not clearly red (r=%d g=%d b=%d)" % (v, r, g, b)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
    print("test_dash_accent: ok")
