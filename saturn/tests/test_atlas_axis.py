#!/usr/bin/env python3
"""How often a cardinal exit is drawn on its own axis, measured off the shipped
atlas and the shipped story files.

The generator scores a layout with a half-plane test -- is the destination
somewhere below me, for a south exit -- which is the right test for what it is
used for, deciding which object a drawn box is and whether a whole map has been
turned. It is not the test a player applies. Told an exit is south, they expect
the line to go down; a room drawn down and two columns across reads as
south-east, and the map is then answering a question they did not ask. That was
the owner's report on The Lurking Horror: Terminal Room's south exit reaches
Second Floor, which is drawn at (+2,+1).

The emitted header used to report the half-plane result as "leave in the
direction drawn", so The Lurking Horror's table claimed 34 of 34 (100%) while 19
of its 32 plain cardinal exits were on axis. gen_map_atlas.py now reports both
numbers and its nudge pass has taken that game to 32 of 32; across the atlas the
figure went from 664 of 779 to 702 of 779. This measures the shipped file, which
needs neither the map scans nor the cache they live in and so runs anywhere the
repo does.

Nothing here enforces alignment. A plan drawn square to a building rather than
to the compass is Infocom's and not an error, and refusing those would drop
Zork I, The Lurking Horror and The Witness from the atlas entirely and fall all
three back to the explored map.

Ten of the tables measured here were never drawn by anybody: the stories with no
scannable map are laid out from their own exit graphs, and this file measures
them alongside the rest because the regression question is the same one. It is
not the same NUMBER, though, and the two blocks of BASELINE should not be
compared or totalled. What this holds is that no regeneration quietly
makes a table worse -- which is the whole value of a number nobody can see on
screen.
"""
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "engine" / "map_atlas_data.inc"
sys.path.insert(0, str(ROOT / "tools"))

import zexits  # noqa: E402

W = zexits.DIR_WORDS
CARD = {W.index("north"): (0, -1), W.index("south"): (0, 1),
        W.index("east"): (1, 0), W.index("west"): (-1, 0)}

# Aligned and tested per story, as the shipped table stands. Assertions are
# ">= this rate", so a regeneration that improves a table passes and one that
# loses ground fails. Raise a number here only alongside the table that earned
# it.
#
# The second block is the stories with no map to scan, laid out from their own
# exit graphs instead (see test_atlas_walk.py). They are held to the same rule
# for the same reason -- a table nobody measures can get worse without anybody
# hearing -- but they are NOT comparable to the block above them and were never
# meant to be: one is how well a reading of Infocom's drawing agrees with the
# story, the other is how well an inference from the story agrees with itself.
# Which kind a table is, is recorded in the .inc, not here.
BASELINE = {
    "BALLYHOO": (15, 19),
    "CUTHROAT": (33, 35),
    "ENCHANTR": (58, 61),
    "HOLYWOOD": (42, 42),
    "INFIDEL": (80, 80),
    "LEATHERG": (15, 15),
    "LURKING": (32, 32),
    "MOONMIST": (4, 4),
    "PLNDHRTS": (48, 50),
    "SORCERER": (80, 95),
    "SPLBRKR": (23, 24),
    "STATFALL": (63, 66),
    "SUSPENDD": (48, 48),
    "WISHBRNG": (19, 23),
    "WITNESS": (8, 8),
    "ZORK1": (75, 113),
    "ZORK2": (17, 20),
    "ZORK3": (40, 44),

    # Walked, not measured.
    "HITCHHKR": (18, 20),
    "INFOSAM5": (109, 147),
    "INFOSAM7": (35, 69),
    "MZORKI": (46, 84),
    "MZORKI2": (56, 94),
    "MZORKII": (40, 53),
    "PLNTFALL": (115, 138),
    "SEASTLKR": (25, 38),
    "STARCROS": (131, 170),
    "SUSPECT": (70, 98),
}


def tables():
    """{story stem: {object: (page, x, y)}} out of the shipped .inc."""
    text = INC.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S):
        cells = {}
        for line in m.group(2).splitlines():
            c = re.match(r"\s*\{\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", line)
            if c:
                cells[int(c.group(1))] = (int(c.group(2)), int(c.group(3)),
                                          int(c.group(4)))
        if cells:
            out[m.group(1)] = cells
    return out


def plain_exits(stem):
    """{room: {direction index: destination}} over one-byte direction
    properties only -- the exits that name a room outright. A door or flag
    property carries a destination too, but the generator scores only these,
    and counting a different set here would make the two numbers unanswerable
    against each other."""
    raw = zexits.story(stem)
    dirs = zexits.direction_properties(raw)
    objs = zexits.object_tables(raw)
    props = {o: zexits.properties(raw, a) for o, a in objs.items()}
    rooms = {o for o, p in props.items() if any(n in dirs for n in p)}
    out = {}
    for o in rooms:
        ex = {}
        for num, data in props[o].items():
            if num in dirs and len(data) == 1 and data[0] in rooms:
                ex[dirs[num]] = data[0]
        out[o] = ex
    return out


def measure(stem, cells):
    """(aligned, tested, [(room, dir, dest, dx, dy)]) over cardinal exits with
    both ends placed on the same page."""
    aligned = tested = 0
    bad = []
    for room, ex in plain_exits(stem).items():
        if room not in cells:
            continue
        pa, ax, ay = cells[room]
        for d, dest in ex.items():
            if d not in CARD or dest not in cells:
                continue
            pb, bx, by = cells[dest]
            if pb != pa:
                continue
            tested += 1
            wx, wy = CARD[d]
            dx, dy = bx - ax, by - ay
            on = ((wx == 0 and dx == 0 and (dy > 0) == (wy > 0)) or
                  (wy == 0 and dy == 0 and (dx > 0) == (wx > 0)))
            if on:
                aligned += 1
            else:
                bad.append((room, W[d], dest, dx, dy))
    return aligned, tested, bad


@pytest.mark.parametrize("stem", sorted(BASELINE))
def test_no_table_loses_alignment(stem):
    cells = tables().get(stem)
    assert cells, f"{stem} has no table in {INC.name}"
    aligned, tested, _ = measure(stem, cells)
    assert tested > 0
    base_a, base_t = BASELINE[stem]
    assert aligned / tested >= base_a / base_t - 1e-9, (
        f"{stem} fell to {aligned}/{tested} from {base_a}/{base_t}")


def test_every_shipped_table_is_measured():
    """A new game in the atlas must arrive with its number recorded, or it
    would ship unmeasured behind a suite that still passes."""
    missing = sorted(set(tables()) - set(BASELINE))
    assert not missing, f"no recorded alignment for {missing}"


def test_the_generators_own_half_plane_test_is_not_this_one():
    """The two predicates must stay distinct. Collapsing AXIS into HALF would
    make the header's stricter line a copy of its looser one, and the file would
    once again claim more than it checked."""
    src = (ROOT / "tools" / "gen_map_atlas.py").read_text(encoding="utf-8")
    assert "AXIS = {" in src, "gen_map_atlas.py no longer defines AXIS"
    assert "def alignment(" in src, "gen_map_atlas.py no longer measures axis alignment"
    assert "dx == 0 and dy < 0" in src, "AXIS north is no longer an axis test"


if __name__ == "__main__":
    tabs = tables()
    ta = tt = 0
    for stem in sorted(tabs):
        a, t, bad = measure(stem, tabs[stem])
        ta += a
        tt += t
        print("%-10s %3d/%3d on axis (%3.0f%%)"
              % (stem, a, t, 100.0 * a / max(1, t)))
    print("TOTAL %d/%d (%.0f%%)" % (ta, tt, 100.0 * ta / max(1, tt)))
