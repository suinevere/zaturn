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

The three peer slots are a different bargain and need a different check. They
ARE stone the rest of the time; what makes borrowing them safe is that no tile
the map paints reaches them, so they are unreachable for as long as the screen
is up, and that dash_map_ink is the only writer while the next dash_tint calls
the loan in. Both halves are checked here: only the peer figures may name those
entries, and write_palette must NOT exempt them, or the stone would keep a
player's colour after the map closed.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "tools" / "gen_dash_tiles.py"
HDR = ROOT / "saturn" / "src" / "video" / "dash_tiles.h"
SRC = ROOT / "saturn" / "src" / "video" / "dash_tiles.c"
VIEW = ROOT / "saturn" / "src" / "video" / "dash_view.cxx"

# The tiles allowed to name the accent: the four reticle corners and the local
# player's own figure. Read off the enum in dash_map.h rather than recomputed
# here, so a tile inserted before them moves this with it.
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
    """The tiles allowed to name the accent: the reticle and the local player's
    own figure, which is drawn in the colour the map gives that player."""
    e = enum_values()
    out = [e[n] for n in XHAIR_NAMES]
    out += [e["DT_KNIGHT0"] + i for i in range(KNIGHT_CELLS)]
    return out


def map_paints(e, t):
    """Whether the map screen can put this tile on the layer. Everything else is
    chrome the map never draws -- it clears the layer to DT_BLANK and paints no
    box -- which is exactly what makes three of the chrome's own palette entries
    borrowable while it is up. The two ranges are the ones is_map_ink names in
    test_dash_tiles.c; DT_GROUND is left out of both because nothing paints it
    any more."""
    return (e["DT_ROOM"] <= t <= e["DT_LINK_STAIR"]) or            (e["DT_ROOM_SEL"] <= t < e["DT_N"])


def peer_indices():
    """PAL_PEER and DASH_PAL_PEER0..2, which must be the same three numbers."""
    gen = re.search(r"^PAL_PEER\s*=\s*\(([^)]*)\)\s*$",
                    GEN.read_text(encoding="utf-8"), re.M)
    assert gen is not None, "tools/gen_dash_tiles.py defines no PAL_PEER"
    want = [int(v) for v in re.findall(r"\d+", gen.group(1))]
    hdr = HDR.read_text(encoding="utf-8")
    got = []
    for i in range(len(want)):
        m = re.search(r"^#define\s+DASH_PAL_PEER%d\s+(\d+)\s*$" % i, hdr, re.M)
        assert m is not None, "dash_tiles.h defines no DASH_PAL_PEER%d" % i
        got.append(int(m.group(1)))
    assert want == got, "PAL_PEER %s and DASH_PAL_PEER* %s disagree" % (want, got)
    assert len(set(want)) == len(want), "two seats share a palette slot"
    return want


def test_generator_and_header_name_the_same_slot():
    accent_index()


def test_write_palette_passes_the_accent_through():
    """The untinted arm exists and is keyed on the define, not on a literal."""
    text = VIEW.read_text(encoding="utf-8")
    assert "if (i == DASH_PAL_ACCENT)" in text, \
        "write_palette no longer exempts the accent from the tint"


def test_only_the_cursor_and_the_player_use_the_accent():
    """Nothing on the stone reaches the slot, so the marble and the frames are
    untouched by giving it a colour. The reticle and the local player's figure
    are what may reach it; the shared-room shield is checked separately, since
    its own upper-left quadrant is that player's colour too."""
    a = accent_index()
    allowed = set(accent_tiles())
    shield = enum_values()["DT_SHIELD0"]
    for i, px in enumerate(tiles()):
        if shield <= i < shield + 16:
            continue
        used = a in px
        if i in allowed:
            assert used, "tile %d is not drawn in the accent" % i
        else:
            assert not used, "tile %d uses the accent slot %d" % (i, a)


def test_generator_and_header_name_the_same_peer_slots():
    peer_indices()


def test_only_the_peer_figures_use_the_borrowed_slots():
    """The borrow is only safe while nothing the map paints reaches those
    entries. That is a property of the emitted tiles, not a reservation: a mark
    that took one would be drawn in another player's colour on the map screen
    and in stone everywhere else, and would build."""
    e = enum_values()
    peers = peer_indices()
    shield = e["DT_SHIELD0"]
    px = tiles()
    for slot, ink in enumerate(peers):
        drawn = e["DT_KNIGHT_PEER0"] + slot * KNIGHT_CELLS
        allowed = set(range(drawn, drawn + KNIGHT_CELLS))
        for i, t in enumerate(px):
            if shield <= i < shield + 16:
                continue
            if i in allowed:
                assert ink in t, "peer figure tile %d is not drawn in slot %d" % (i, ink)
            elif map_paints(e, i):
                assert ink not in t, "map tile %d reaches borrowed slot %d" % (i, ink)


def test_the_shield_quarters_the_four_party_slots():
    """One quadrant per seat, in the same order the tile set numbers them, so a
    room two people share names the two."""
    e = enum_values()
    inks = [accent_index()] + peer_indices()
    px = tiles()
    quad = [(2, 2), (4, 2), (2, 4), (4, 4)]
    for mask in range(16):
        t = px[e["DT_SHIELD0"] + mask]
        for bit, (qx, qy) in enumerate(quad):
            v = t[qy * 8 + qx]
            if mask & (1 << bit):
                assert v == inks[bit],                     "shield %d quadrant %d is entry %d, not %d" % (mask, bit, v, inks[bit])
            else:
                assert v not in inks,                     "shield %d claims quadrant %d nobody is in" % (mask, bit)


def test_write_palette_does_not_exempt_the_borrowed_slots():
    """They are stone the rest of the time. An exemption would leave a player's
    colour on the marble for the rest of the session."""
    text = VIEW.read_text(encoding="utf-8")
    for i in range(3):
        assert "i == DASH_PAL_PEER%d" % i not in text,             "write_palette exempts DASH_PAL_PEER%d, so dash_tint cannot call the loan in" % i


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
