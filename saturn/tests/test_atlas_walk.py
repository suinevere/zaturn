#!/usr/bin/env python3
"""The layout a story gets when there is no map to measure.

Eighteen of the disc's stories have a table read off Infocom's own printed maps.
The rest had nothing: `map_model_reveal_atlas` loops over the atlas and only the
atlas, so a story with no table places no rooms on Easy and behaves exactly like
Medium -- which is what "Starcross has no map" turned out to mean.

The generator's scoring, its nudge and its floor pass never look at the scan, so
they run perfectly well on a story that has no PDF. What they were missing was a
seed and a repair:

  * `walk_seed` lays the rooms out by walking the story's own exits, one cell per
    compass direction, one floor at a time.
  * `repair` is the counterpart `nudge` cannot be. `nudge` may only ever RAISE
    the count of exits on their axis and is forbidden from losing a half-plane
    agreement, so by construction it cannot repair a half-plane the seed got
    wrong -- and a seed that resolves a taken cell by searching outward gets
    plenty wrong. `repair` is the same greedy shape scored the other way round.

Measured against the eighteen tables that DO have scans, the pair reaches 94% of
the half-plane agreement a scan gets where the seed alone reaches 80%. That is
the number these tests exist to keep.

Run: pytest saturn/tests/test_atlas_walk.py
"""
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "engine" / "map_atlas_data.inc"
sys.path.insert(0, str(ROOT / "tools"))

import gen_map_atlas as G  # noqa: E402


# The stories shipped with a walked table, and the half-plane rate each reached
# when it was accepted. Recorded here for the reason test_atlas_axis.py records
# its own: a derived table that quietly got worse would otherwise ship behind a
# suite that still passes.
WALKED = {
    "ADVENT": 0.94,
    "HITCHHKR": 0.93,
    "INFOSAM5": 0.98,
    "INFOSAM7": 0.85,
    "MZORKI": 0.92,
    "MZORKI2": 0.91,
    "MZORKII": 0.96,
    "PLNTFALL": 0.92,
    "SEASTLKR": 1.00,
    "STARCROS": 0.87,
    "SUSPECT": 0.98,
}

# Stories whose exits contradict a plane badly enough that the walk misses the
# generator's own PASS_RATE. They ship no table and stay on the explored map,
# which is what they do today -- listed so that a change making one of them
# passable is a decision somebody takes rather than a diff nobody reads.
#
# Adventure used to be here at 78%. It left when the layout stopped placing
# rooms from a group the scan would have declined: its own maze was most of what
# it disagreed with, and without those it reaches 94%. Mini-Zork I went 85% to
# 92% and Mini-Zork II 85% to 91% the same way. Refusing to draw a maze does not
# merely avoid a knot, it stops the knot dragging the rest of the map down with
# it.
WALK_REJECTED = {"DEADLINE", "HYPOCOND"}


def grid_graph(w, h, joins=("north", "south", "east", "west")):
    """A w by h lattice of rooms wired with real compass exits, numbered in
    reading order from 1. The layout that satisfies every exit is the lattice
    itself, so anything less than total agreement is the code's fault and not
    the geography's."""
    def num(x, y):
        return 1 + y * w + x

    graph = {}
    for y in range(h):
        for x in range(w):
            ex = {}
            if y > 0 and "north" in joins:
                ex["north"] = ("OPEN", num(x, y - 1))
            if y < h - 1 and "south" in joins:
                ex["south"] = ("OPEN", num(x, y + 1))
            if x < w - 1 and "east" in joins:
                ex["east"] = ("OPEN", num(x + 1, y))
            if x > 0 and "west" in joins:
                ex["west"] = ("OPEN", num(x - 1, y))
            graph[num(x, y)] = {"name": "R%d" % num(x, y), "exits": ex}
    return graph


def rate(pos, graph):
    ag, at, _ = G.agreement(pos, graph)
    return ag / at if at else 0.0


def test_a_lattice_walks_out_as_a_lattice():
    """The seed alone, on a geography that has an exact answer."""
    graph = grid_graph(4, 4)
    floors = {r: 0 for r in graph}
    pos = G.walk_seed(graph, floors)
    assert len(pos) == len(graph), "the walk left rooms unplaced"
    assert len(set(pos.values())) == len(pos), "two rooms landed in one cell"
    assert rate(pos, graph) == 1.0, "a lattice did not lay out as a lattice"


def test_every_room_is_placed_even_when_the_graph_is_broken_into_pieces():
    """A story is not one connected component -- a room reachable only by a
    conditional exit is its own island -- and an island nobody walks to is
    exactly the room a player most wants found for them."""
    graph = grid_graph(3, 2)
    graph[99] = {"name": "Island", "exits": {}}
    floors = {r: 0 for r in graph}
    pos = G.walk_seed(graph, floors)
    assert set(pos) == set(graph)
    assert len(set(pos.values())) == len(pos)


def test_rooms_of_different_floors_may_share_a_cell_but_not_one_floor():
    """Floors stand on the same footprint on purpose -- the storeys of a
    building do -- so a cell is only owed to be unique within its own floor."""
    graph = grid_graph(3, 3)
    floors = {r: (0 if r <= 4 else 1) for r in graph}
    pos = G.walk_seed(graph, floors)
    for f in (0, 1):
        cells = [pos[r] for r in pos if floors[r] == f]
        assert len(set(cells)) == len(cells), "two rooms of one floor share a cell"


def test_repair_fixes_what_nudge_is_forbidden_to_touch():
    """The whole reason repair exists. nudge may only raise the axis count and
    may never lose a half-plane, so a room the seed put on the wrong SIDE of its
    neighbour is beyond it; repair is scored the other way round and reaches it."""
    graph = grid_graph(3, 3)

    def lattice():
        p = {r: ((r - 1) % 3, (r - 1) // 3) for r in graph}
        # The middle room on the wrong side of everything around it, by less
        # than REPAIR_SPAN so it is within reach of a local pass.
        p[5] = (-2, -2)
        return p

    page = {r: 0 for r in graph}
    before = rate(lattice(), graph)
    assert before < 1.0, "the fixture is not actually broken"

    nudged = lattice()
    G.nudge(nudged, page, graph)
    assert rate(nudged, graph) <= before + 1e-9, \
        "nudge repaired a half-plane, which it is supposed to be unable to do"

    repaired = lattice()
    G.repair(repaired, page, graph)
    assert rate(repaired, graph) > before, "repair did not repair anything"


def test_repair_is_local_and_says_so():
    """It reaches REPAIR_SPAN cells and no further. A room displaced past that
    stays displaced -- this is a tidy, not a re-layout, and a test that quietly
    depended on it reaching further would be describing a pass that does not
    exist."""
    graph = grid_graph(3, 3)
    pos = {r: ((r - 1) % 3, (r - 1) // 3) for r in graph}
    pos[5] = (-(G.REPAIR_SPAN + 2), -(G.REPAIR_SPAN + 2))
    page = {r: 0 for r in graph}
    before = rate(pos, graph)
    G.repair(pos, page, graph)
    assert rate(pos, graph) == before, "repair reached further than REPAIR_SPAN"


def test_repair_never_loses_a_half_plane_it_already_had():
    """It is greedy and it is allowed to fail; it is not allowed to go
    backwards, or a regeneration could quietly make a shipped table worse."""
    graph = grid_graph(4, 3)
    floors = {r: 0 for r in graph}
    pos = G.walk_seed(graph, floors)
    page = {r: 0 for r in pos}
    before = rate(pos, graph)
    G.repair(pos, page, graph)
    assert rate(pos, graph) >= before - 1e-9


def test_repair_will_not_turn_a_descent_upside_down():
    """The guard nudge already carries, for the reason nudge carries it: this
    pass turned Zork I's canyon over when it was first written without it."""
    graph = {
        1: {"name": "Top", "exits": {"down": ("OPEN", 2)}},
        2: {"name": "Bottom", "exits": {"up": ("OPEN", 1), "east": ("OPEN", 3)}},
        3: {"name": "Side", "exits": {"west": ("OPEN", 2)}},
    }
    pos = {1: (0, 0), 2: (0, 1), 3: (5, 1)}
    page = {r: 0 for r in pos}
    G.repair(pos, page, graph)
    assert pos[2][1] > pos[1][1], "the bottom of the descent ended up above its top"


# ---- the shipped tables ----------------------------------------------------

def tables():
    """{stem: cell count} for every table in the shipped .inc."""
    text = INC.read_text(encoding="utf-8")
    return {m.group(1): len(re.findall(r"^\s*\{\s*\d+,", m.group(2), re.M))
            for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S)}


def derived_stems():
    """The stems the .inc marks as walked rather than measured."""
    text = INC.read_text(encoding="utf-8")
    return {m.group(1) for m in
            re.finditer(r"\| MAP_ATLAS_(\w+)\n(?: \|.*\n)*? \|\s+"
                        + re.escape(G.DERIVED_MARK), text)}


def test_the_shipped_file_marks_exactly_the_walked_tables():
    """Provenance is recorded, not implied. A measured table that came to be
    marked derived -- or a derived one that lost its mark -- would leave the
    file claiming its rooms were read off a printed map when they were not."""
    assert derived_stems() == set(WALKED), (
        "marked derived: %s; expected: %s"
        % (sorted(derived_stems()), sorted(WALKED)))


@pytest.mark.parametrize("stem", sorted(WALKED))
def test_every_walked_story_shipped_a_table(stem):
    assert tables().get(stem), f"{stem} has no table in {INC.name}"


@pytest.mark.parametrize("stem", sorted(WALK_REJECTED))
def test_the_rejected_stories_still_ship_nothing(stem):
    """Below the generator's own PASS_RATE. Shipping one anyway is a decision,
    not a regeneration."""
    assert stem not in tables(), (
        f"{stem} now has a table; it was rejected for missing PASS_RATE, so "
        "either the layout improved and this test should record that, or "
        "something started shipping tables it should not")


def test_nothing_lays_out_a_room_the_scan_could_not_tell_apart():
    """The rule both paths owe to `assign()`, which refuses to place a group of
    more than AMBIG_MAX identically-named rooms because it cannot say which
    drawn box is which.

    A layout read "unplaced" as "the OCR missed it" and walked those rooms in
    anyway. Some were not missed -- they were declined, and the reason has not
    gone away. The Lurking Horror's twelve Wet Tunnels went in as a knot around
    the origin with exits contradicting each other in every direction, five to
    eleven cells from the floor's measured rooms, which is what a maze is: an
    arbitrary embedding whose drawn positions carry no information. Infocom
    printed those schematically or not at all for the same reason.

    Zork I's Maze x15, Zork III's Narrow Room x10 and Land of Shadow x8,
    Infidel's Desert x10 and Cube x8, Spellbreaker's Octagonal Room x9, Zork
    II's Oddly-angled Room x9, Enchanter's Courtyard x7, and the Mini-Zork
    mazes are all the same case.
    """
    import collections
    from mapscan import room_graph

    z3 = ROOT / "saturn" / "cd" / "data" / "Z3"
    text = INC.read_text(encoding="utf-8")
    offenders = []
    for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S):
        story = z3 / (m.group(1) + ".Z3")
        if not story.exists():
            continue
        graph, _, _ = room_graph(str(story))
        names = collections.Counter(
            G.norm(r["name"]) for r in graph.values() if G.norm(r["name"]))
        for line in m.group(2).splitlines():
            c = re.match(r"\s*\{\s*(\d+),", line)
            if not c:
                continue
            room = int(c.group(1))
            if room in graph and names.get(G.norm(graph[room]["name"]), 0) > G.AMBIG_MAX:
                offenders.append((m.group(1), graph[room]["name"]))
    assert not offenders, (
        "%d cell(s) place a room from a group the scan declined, e.g. %s"
        % (len(offenders), offenders[:6]))


def test_the_unresolvable_rule_is_about_the_group_not_the_room():
    """One room called Maze is a room; fifteen are a maze. The predicate has to
    count the group, or it would refuse a story that happens to name two rooms
    alike and accept a fifteen-room labyrinth one name at a time."""
    small = {i: {"name": "Twin", "exits": {}} for i in range(1, 3)}
    small[3] = {"name": "Hall", "exits": {}}
    assert G.unresolvable(small) == set()

    big = {i: {"name": "Maze", "exits": {}} for i in range(1, G.AMBIG_MAX + 2)}
    big[99] = {"name": "Hall", "exits": {}}
    assert G.unresolvable(big) == set(range(1, G.AMBIG_MAX + 2))
    assert 99 not in G.unresolvable(big)


def test_no_walked_table_reaches_past_the_cell_room_field():
    """MapAtlasCell.room is an unsigned char, so a story whose rooms are
    numbered past 255 cannot be tabled at all -- and would truncate silently.
    Seastalker and Suspect both reach 254."""
    text = INC.read_text(encoding="utf-8")
    for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S):
        for room in re.findall(r"^\s*\{\s*(\d+),", m.group(2), re.M):
            assert int(room) <= 255, f"{m.group(1)} room {room} will not fit"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
