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
    "ADVENT": (67, 127),
    "HITCHHKR": (18, 20),
    "INFOSAM5": (82, 117),
    "INFOSAM7": (35, 69),
    "MZORKI": (43, 64),
    "MZORKI2": (60, 78),
    "MZORKII": (40, 53),
    "PLNTFALL": (115, 138),
    "SEASTLKR": (25, 38),
    "STARCROS": (136, 170),
    "SUSPECT": (70, 98),
}

# The whole table as a player sees it: the rooms read off the drawing and the
# ones a fill walked in beside them. Recorded apart from BASELINE because the
# two answer different questions -- how good the reading was, and how good the
# map is -- and one number cannot be both. This one is a floor to hold: a
# better fill is a better map and may raise it.
WHOLE = {
    "ADVENT": (65, 127),
    "BALLYHOO": (18, 24),
    "CUTHROAT": (66, 82),
    "ENCHANTR": (58, 62),
    "HITCHHKR": (18, 20),
    "HOLYWOOD": (57, 57),
    "INFIDEL": (88, 88),
    "INFOSAM5": (82, 117),
    "INFOSAM7": (35, 69),
    "LEATHERG": (52, 59),
    "LURKING": (41, 42),
    "MOONMIST": (30, 52),
    "MZORKI": (43, 64),
    "MZORKI2": (60, 78),
    "MZORKII": (40, 53),
    "PLNDHRTS": (50, 54),
    "PLNTFALL": (115, 138),
    "SEASTLKR": (25, 38),
    "SORCERER": (93, 111),
    "SPLBRKR": (29, 30),
    "STARCROS": (131, 170),
    "STATFALL": (71, 76),
    "SUSPECT": (70, 98),
    "SUSPENDD": (102, 111),
    "WISHBRNG": (32, 39),
    "WITNESS": (22, 27),
    "ZORK1": (89, 144),
    "ZORK2": (68, 89),
    "ZORK3": (50, 73),
}


def tables(measured_only=False):
    """{story stem: {object: (page, x, y)}} out of the shipped .inc.

    With measured_only, the rooms a merge walked in are left out -- they carry
    a marker in their trailing comment. That is what lets BASELINE keep meaning
    the thing it was recorded to mean: how well the rooms read off Infocom's
    drawing agree with the story. Filling a table in adds rooms whose placement
    was inferred and which are harder to satisfy, so the whole table's rate
    drops; the measured half's must not move at all, because those coordinates
    did not.
    """
    text = INC.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S):
        cells = {}
        for line in m.group(2).splitlines():
            if measured_only and re.search(r"/\* \+ ", line):
                continue
            c = re.match(r"\s*\{\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", line)
            if c:
                cells[int(c.group(1))] = (int(c.group(2)), int(c.group(3)),
                                          int(c.group(4)))
        if cells:
            out[m.group(1)] = cells
    return out


def derived():
    """The stems whose header says the table was never measured.

    BASELINE holds a measured table to EXACT equality, on the grounds that a
    coordinate read off a drawing is not supposed to improve either -- a reading
    that changed is a reading that was wrong once. Nothing in that argument
    reaches a table nobody read. A walked layout is an inference, and a better
    inference is a better map, so those are held to a floor like WHOLE.

    Applying exact equality to both was over-reach, and it showed the first time
    the floor pass got better: splitting a level exit that was really a
    staircase took Adventure from 65 of 127 on axis to 67 and Starcross from 131
    of 170 to 136, and the suite called both a regression.
    """
    text = INC.read_text(encoding="utf-8")
    out = set()
    for m in re.finditer(
            r"\| MAP_ATLAS_(\w+)\n(.*?)\n -{10,}\*/", text, re.S):
        if "DERIVED, not measured" in m.group(2):
            out.add(m.group(1))
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
    """Measured rooms only, and the number must be EXACTLY the recorded one.

    Not ">=", which is what this asserted while a table could only be
    regenerated whole. A measured coordinate is not supposed to improve either:
    it is a reading of a drawing, and a reading that changed is a reading that
    was wrong once. Filling a table in is asserted not to move one, and this is
    that assertion surviving into the repo, where it holds against any future
    regeneration rather than only against the run that made it.
    """
    cells = tables(measured_only=True).get(stem)
    assert cells, f"{stem} has no table in {INC.name}"
    aligned, tested = measure(stem, cells)[:2]
    base_a, base_t = BASELINE[stem]
    if stem in derived():
        assert tested > 0
        assert aligned / tested >= base_a / base_t - 1e-9, (
            f"{stem} now lays {aligned}/{tested} on axis, against "
            f"{base_a}/{base_t} recorded -- a walked table got worse")
        return
    assert (aligned, tested) == (base_a, base_t), (
        f"{stem} measured rooms now score {aligned}/{tested}, recorded as "
        f"{base_a}/{base_t} -- a coordinate read off the drawing moved")


@pytest.mark.parametrize("stem", sorted(WHOLE))
def test_no_filled_table_loses_alignment(stem):
    """The whole table, measured rooms and walked-in ones together, which is
    what a player actually sees. ">=" here, because this half CAN legitimately
    improve: a better fill is a better map."""
    cells = tables().get(stem)
    assert cells, f"{stem} has no table in {INC.name}"
    aligned, tested, _ = measure(stem, cells)
    assert tested > 0
    base_a, base_t = WHOLE[stem]
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
