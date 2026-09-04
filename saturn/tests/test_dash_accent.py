#!/usr/bin/env python3
"""The map draws itself in four colours nothing else on the layer uses: the
accent, which dash_view leaves untinted so it stays legible on a tan sheet,
and three borrowed slots the other seats' figures are drawn in. Several things
have to agree for that to hold and nothing in the compiler can catch any of
them drifting.

The accent index lives in three places: PAL_ACCENT in the generator,
DASH_PAL_ACCENT in dash_tiles.h, and the `if (i == DASH_PAL_ACCENT)` arm of
write_palette. If the generator and the header disagree the build still links
-- it just tints the cursor back into the paper, or leaves one stone tile
glowing red.

That slot is only free because nothing else can reach it: marble() caps its
veins two steps below and every frame, rule and mark names an entry on either
side. That is a property of the generator's numbers, not a reservation, so it
is checked against the emitted tiles rather than trusted -- a new mark or a
raised vein cap would take the slot silently.

The four party slots are a different bargain and need a different check. They
ARE stone the rest of the time; what makes borrowing them safe is that no other
tile the map paints reaches them, so they are unreachable for as long as the
screen is up, and that dash_map_ink is the only writer while the next dash_tint
calls the loan in. Both halves are checked here: only the figures and the shield
may name those entries, and write_palette must NOT exempt them, or the stone
would keep a player's colour after the map closed.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "tools" / "gen_dash_tiles.py"
HDR = ROOT / "saturn" / "src" / "video" / "dash_tiles.h"
SRC = ROOT / "saturn" / "src" / "video" / "dash_tiles.c"
VIEW = ROOT / "saturn" / "src" / "video" / "dash_view.cxx"

# The only tiles allowed to name the accent: the four reticle corners. Read off
# the enum in dash_map.h rather than recomputed here, so a tile inserted before
# them moves this with it.
XHAIR_NAMES = ("DT_XHAIR_TL", "DT_XHAIR_TR", "DT_XHAIR_BL", "DT_XHAIR_BR")

# One figure, two cells by three. Written down rather than read off
# DT_KNIGHT_CELLS because that define is arithmetic on two others and this file
# parses the header rather than compiling it.
KNIGHT_CELLS = 6


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


def enum_values():
    """dash_map.h's tile enum as a name -> index dict."""
    text = (ROOT / "saturn" / "src" / "video" / "dash_map.h").read_text(encoding="utf-8")
    body = text[text.index("enum {"):]
    body = body[:body.index("};")]
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
    return dict(order)


def accent_tiles():
    """The tiles allowed to name the accent: the reticle, and nothing else. The
    cursor is red on every sheet and in every party, which is only true while it
    is the sole occupant of the one entry write_palette leaves alone."""
    e = enum_values()
    return [e[n] for n in XHAIR_NAMES]


def map_paints(e, t):
    """Whether the map screen can put this tile on the layer. Everything else is
    chrome the map never draws -- it clears the layer to DT_BLANK and paints no
    box -- which is exactly what makes five of the chrome's own palette entries
    borrowable while it is up. The two ranges are the ones is_map_ink names in
    test_dash_tiles.c; DT_GROUND is left out of both because nothing paints it
    any more."""
    return ((e["DT_ROOM"] <= t <= e["DT_LINK_STAIR"]) or
            (e["DT_ROOM_SEL"] <= t < e["DT_N"]))


def party_indices():
    """PAL_PARTY and DASH_PAL_PARTY0..3, which must be the same four numbers."""
    gen = re.search(r"^PAL_PARTY\s*=\s*\(([^)]*)\)\s*$",
                    GEN.read_text(encoding="utf-8"), re.M)
    assert gen is not None, "tools/gen_dash_tiles.py defines no PAL_PARTY"
    want = [int(v) for v in re.findall(r"\d+", gen.group(1))]
    hdr = HDR.read_text(encoding="utf-8")
    got = []
    for i in range(len(want)):
        m = re.search(r"^#define\s+DASH_PAL_PARTY%d\s+(\d+)\s*$" % i, hdr, re.M)
        assert m is not None, "dash_tiles.h defines no DASH_PAL_PARTY%d" % i
        got.append(int(m.group(1)))
    assert want == got, "PAL_PARTY %s and DASH_PAL_PARTY* %s disagree" % (want, got)
    assert len(set(want)) == len(want), "two seats share a palette slot"
    assert accent_index() not in want, (
        "a seat holds the accent, so the crosshair is not red on every sheet")
    return want


def test_generator_and_header_name_the_same_slot():
    accent_index()


def test_write_palette_passes_the_accent_through():
    """The untinted arm exists and is keyed on the define, not on a literal."""
    text = VIEW.read_text(encoding="utf-8")
    assert "if (i == DASH_PAL_ACCENT)" in text, \
        "write_palette no longer exempts the accent from the tint"


def test_only_the_cursor_uses_the_accent():
    """Nothing on the stone reaches the slot, so the marble and the frames are
    untouched by giving it a colour -- and nothing on the MAP reaches it either
    but the reticle, which is what makes the cursor red on every sheet and in
    every party rather than one more thing the ink decides."""
    a = accent_index()
    allowed = set(accent_tiles())
    for i, px in enumerate(tiles()):
        used = a in px
        if i in allowed:
            assert used, "tile %d is not drawn in the accent" % i
        else:
            assert not used, "tile %d uses the accent slot %d" % (i, a)


def named_index(gen_name, hdr_name):
    """One palette index the generator and the header both name, checked to be
    the same number in both."""
    gen = re.search(r"^%s\s*=\s*(\d+|MARK_[A-Z_]+)\s*$" % gen_name,
                    GEN.read_text(encoding="utf-8"), re.M)
    assert gen is not None, "tools/gen_dash_tiles.py defines no %s" % gen_name
    want = gen.group(1)
    if not want.isdigit():
        alias = re.search(r"^%s\s*=\s*(\d+)\s*$" % want,
                          GEN.read_text(encoding="utf-8"), re.M)
        assert alias is not None, "%s is %s, which is not a number" % (gen_name, want)
        want = alias.group(1)
    hdr = re.search(r"^#define\s+%s\s+(\d+)\s*$" % hdr_name,
                    HDR.read_text(encoding="utf-8"), re.M)
    assert hdr is not None, "dash_tiles.h defines no %s" % hdr_name
    assert int(want) == int(hdr.group(1)), \
        "%s %s and %s %s disagree" % (gen_name, want, hdr_name, hdr.group(1))
    return int(want)


def test_generator_and_header_name_the_same_line_and_fill():
    """The map sets these two per sheet. A drift would build and link and paint
    the passages in whatever the header happened to point at."""
    line = named_index("PAL_LINE", "DASH_PAL_LINE")
    fill = named_index("PAL_FILL", "DASH_PAL_FILL")
    assert line != fill, (
        "the passages and the locations' fill share an entry, so a sheet cannot "
        "colour them differently -- which is the whole reason there are two")
    assert fill not in party_indices() + [accent_index()], \
        "the fill holds a slot something else on the map is drawn in"


def test_only_the_room_mark_is_filled():
    """DASH_PAL_FILL is borrowed from the marble the same way the party slots
    are, so the same thing has to be true of it: nothing else the map paints may
    reach it, or that mark would silently take the locations' fill colour.

    And the ordinary room mark must not reach the LINE entry, which is the
    separation the two entries exist for -- it is what stops "the passages are
    brown" from also meaning "the rooms are brown"."""
    e = enum_values()
    line = named_index("PAL_LINE", "DASH_PAL_LINE")
    fill = named_index("PAL_FILL", "DASH_PAL_FILL")
    px = tiles()
    room = e["DT_ROOM"]
    assert fill in px[room], "the room mark is not drawn in the fill entry"
    assert line not in px[room], "the room mark still reaches the line entry"
    for i, t in enumerate(px):
        if i == room or not map_paints(e, i):
            continue
        assert fill not in t, "map tile %d reaches the fill slot %d" % (i, fill)


def test_generator_and_header_name_the_same_party_slots():
    party_indices()


def test_only_the_party_tiles_use_the_borrowed_slots():
    """The borrow is only safe while nothing else the map paints reaches those
    entries. That is a property of the emitted tiles, not a reservation: a mark
    that took one would be drawn in a player's colour on the map screen and in
    stone everywhere else, and would build."""
    e = enum_values()
    party = party_indices()
    hi, lo = e["DT_SHIELD_HI0"], e["DT_SHIELD_LO0"]
    px = tiles()
    for slot, ink in enumerate(party):
        drawn = (e["DT_KNIGHT0"] if slot == 0
                 else e["DT_KNIGHT_PEER0"] + (slot - 1) * KNIGHT_CELLS)
        allowed = set(range(drawn, drawn + KNIGHT_CELLS))
        for i, t in enumerate(px):
            # The shield sets carry every seat's colour by construction; which
            # quadrant is whose is test_the_shield_quarters_the_four_party_slots.
            if hi <= i < hi + 16 or lo <= i < lo + 16:
                continue
            if i in allowed:
                assert ink in t, "figure tile %d is not drawn in slot %d" % (i, ink)
            elif map_paints(e, i):
                assert ink not in t, "map tile %d reaches borrowed slot %d" % (i, ink)


def test_the_shield_quarters_the_four_party_slots():
    """One quadrant per seat, in the same order the tile set numbers them, so a
    room two people share names the two. The quadrants are the blank interiors
    of the grid drawn on the figure's shield, given in the 16x24 drawing's own
    coordinates, and the lower pair straddles the boundary between the two cells
    the shield falls across."""
    e = enum_values()
    inks = party_indices()
    px = tiles()
    quad = [(1, 12), (4, 12), (1, 15), (4, 15)]
    cells = {e["DT_SHIELD_HI0"]: 8, e["DT_SHIELD_LO0"]: 16}
    for base, y0 in cells.items():
        for mask in range(16):
            t = px[base + mask]
            for bit, (qx, qy) in enumerate(quad):
                for y in range(qy, qy + 2):
                    if not y0 <= y < y0 + 8:
                        continue
                    v = t[(y - y0) * 8 + qx]
                    if mask & (1 << bit):
                        assert v == inks[bit],                             "shield %d quadrant %d is entry %d, not %d" % (
                                mask, bit, v, inks[bit])
                    else:
                        assert v not in inks,                             "shield %d claims quadrant %d nobody is in" % (mask, bit)


def test_write_palette_does_not_exempt_the_borrowed_slots():
    """They are stone the rest of the time. An exemption would leave a player's
    colour on the marble for the rest of the session."""
    text = VIEW.read_text(encoding="utf-8")
    for i in range(4):
        assert "i == DASH_PAL_PARTY%d" % i not in text, (
            "write_palette exempts DASH_PAL_PARTY%d, so dash_tint cannot call "
            "the loan in" % i)


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
