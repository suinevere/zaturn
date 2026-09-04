#!/usr/bin/env python3
"""Filling a measured table in with the rooms its scan missed.

A scan places what the OCR could read off the drawing, which across the eighteen
tables that have one was 843 rooms of 1,274. The rest were not missing from the
map -- they were missing from the READING of it, scattered through a drawing
whose neighbours were found. So they can be walked in from the rooms around
them, and the measured coordinates never have to move.

Two things make that safe, and they are what this file holds.

**No page is ever invented.** An added room joins the page of a placed room it
shares a level with -- the same route-floor component, so the same drawn sheet
and the same storey by construction -- and only a room no placed room shares a
level with falls back to its nearest placed neighbour's page. Because nothing
new is created, a game's floor count cannot change, no measured room can change
page, and `MAP_ATLAS_PAGE_MAX` cannot be breached. The rule this replaced --
inherit a sheet and re-derive the floors -- did all three, taking one game to
thirty-five floors against a ceiling of sixteen.

**Nothing measured moves.** `repair` and `nudge` take a frozen set, and the
generator asserts every carried coordinate survives the merge unchanged. That
assert runs on every regeneration, not only the one that introduced this, which
is what makes it worth more than the diff it replaced: stage one's guarantee was
a diff with no deletions in it, and a merged table's cells necessarily change.

Run: pytest saturn/tests/test_atlas_merge.py
"""
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "engine" / "map_atlas_data.inc"
sys.path.insert(0, str(ROOT / "tools"))

import gen_map_atlas as G  # noqa: E402


def line_graph(n, dirn="east", back="west"):
    """n rooms in a row, wired both ways."""
    step = {"east": 1, "west": -1, "north": -1, "south": 1}
    graph = {}
    for i in range(1, n + 1):
        ex = {}
        if i < n:
            ex[dirn] = ("OPEN", i + 1)
        if i > 1:
            ex[back] = ("OPEN", i - 1)
        graph[i] = {"name": "R%d" % i, "exits": ex}
    assert dirn in step
    return graph


# ---- the frozen set --------------------------------------------------------

def test_a_frozen_room_never_moves():
    """The whole guarantee. Both passes take the set; neither may touch it."""
    graph = line_graph(5)
    pos = {1: (0, 0), 2: (1, 0), 3: (9, 9), 4: (3, 0), 5: (4, 0)}
    page = {r: 0 for r in pos}
    frozen = {1, 2, 4, 5}
    before = {r: pos[r] for r in frozen}
    G.repair(pos, page, graph, frozen=frozen)
    G.nudge(pos, page, graph, frozen=frozen)
    for r in frozen:
        assert pos[r] == before[r], f"room {r} moved despite being frozen"


def test_freezing_nothing_is_what_the_scanned_path_already_did():
    """The parameter is defaulted, and the default has to be the old behaviour
    exactly -- eighteen measured tables are regenerated through these passes."""
    graph = line_graph(6)
    seed = {r: (r - 1, (r % 3) - 1) for r in graph}
    page = {r: 0 for r in seed}

    a = dict(seed)
    n_a = G.nudge(a, page, graph)
    b = dict(seed)
    n_b = G.nudge(b, page, graph, frozen=frozenset())
    assert a == b and n_a == n_b

    a = dict(seed)
    r_a = G.repair(a, page, graph)
    b = dict(seed)
    r_b = G.repair(b, page, graph, frozen=frozenset())
    assert a == b and r_a == r_b


def test_a_frozen_room_still_scores_the_rooms_around_it():
    """It is an anchor, not a hole. A pass that skipped its edges as well as its
    position would leave every room next to a measured one unscored."""
    graph = line_graph(3)
    # room 2 is the only free one and belongs east of 1 and west of 3, put
    # within REPAIR_SPAN of that so a local pass can reach it
    pos = {1: (0, 0), 2: (0, 2), 3: (2, 0)}
    page = {r: 0 for r in pos}
    G.repair(pos, page, graph, frozen={1, 3})
    assert pos[2] == (1, 0), "the free room did not settle between its anchors"


# ---- the paging rule -------------------------------------------------------

def test_no_page_is_ever_invented():
    graph = line_graph(6)
    apage = {1: 0, 6: 1}
    page, _, _ = G.merge_pages(graph, apage)
    assert set(page) == set(graph)
    assert set(page.values()) <= {0, 1}, "the merge created a floor"


def test_a_level_mate_beats_a_neighbour():
    """Rooms joined by a level exit are on one floor by definition, so a room
    that shares a level with something placed takes that page and not the page
    of whatever happens to be nearest through a staircase."""
    graph = {
        1: {"name": "Hall", "exits": {"east": ("OPEN", 2), "down": ("OPEN", 3)}},
        2: {"name": "Study", "exits": {"west": ("OPEN", 1)}},
        3: {"name": "Cellar", "exits": {"up": ("OPEN", 1), "east": ("OPEN", 4)}},
        4: {"name": "Vault", "exits": {"west": ("OPEN", 3)}},
    }
    # Hall is on page 0 and Cellar on page 1; Study and Vault are unplaced.
    page, by_level, fallback = G.merge_pages(graph, {1: 0, 3: 1})
    assert page[2] == 0, "Study is on the Hall's own level and should share it"
    assert page[4] == 1, "Vault is on the Cellar's own level and should share it"
    assert by_level == 2 and fallback == 0


def test_a_room_sharing_a_level_with_nothing_placed_takes_a_neighbour():
    graph = {
        1: {"name": "Hall", "exits": {"down": ("OPEN", 2)}},
        2: {"name": "Cellar", "exits": {"up": ("OPEN", 1)}},
    }
    page, by_level, fallback = G.merge_pages(graph, {1: 0})
    assert page[2] == 0
    assert by_level == 0 and fallback == 1


# ---- what anchors an added room --------------------------------------------

def cond_graph():
    """Two rooms wired plainly, and a third reachable only behind a door.

    3 is the shape this section is about. Every exit it has, and every exit
    anything has to it, is conditional, so no pass that reads OPEN exits can
    see it: the fill cannot reach it, repair cannot move it and agreement
    cannot score it. Whatever the seed gives it is what it ships with.
    """
    return {
        1: {"name": "A", "exits": {"east": ("OPEN", 2)}},
        2: {"name": "B", "exits": {"west": ("OPEN", 1)}},
        3: {"name": "C", "exits": {"north": ("MAYBE", 2)}},
    }


def test_the_seed_is_left_exactly_as_it_was():
    """The ordering, which is what makes the whole change free. The seed still
    places an unstated room where it always did -- at its page's origin,
    consuming the same cell in the same sweep -- so every coordinate the seed
    hands the rooms around it is unchanged, and so is every agreement built on
    them. settle_unstated moves it afterwards, when it can push nothing but
    itself.

    Both cheaper-looking orders were measured and both cost Moonmist three
    exits and with them its whole filled table: leaving these rooms out of the
    seed changed which cell free_cell gave the stated ones, and placing one on
    a door mid-sweep did the same and then blocked a repair move as well.
    """
    graph = cond_graph()
    assert G.unstated(graph) == {3}
    pos = G.fill_seed(graph, {1: (0, 0), 2: (1, 0)}, {1: 0, 2: 0, 3: 0})
    assert 3 in pos and pos[3] != (1, 1), (
        "the seed already knows where the door goes, which it must not")


def test_a_conditional_exit_places_the_room_it_names():
    """A door is a way to walk from one room to the next, so it is a way to say
    where a room goes. 3 leaves north to 2, so it is the cell south of 2.

    Without this the room is an island and takes its page's origin, which is a
    coordinate the story never suggested: The Lurking Horror's Concrete Box, a
    room whose only exit is a conditional north into a Steam Tunnel, sat ten
    cells from that tunnel with a dashed run leaving it southward.
    """
    graph = cond_graph()
    page = {1: 0, 2: 0, 3: 0}
    pos = G.fill_seed(graph, {1: (0, 0), 2: (1, 0)}, page)
    assert G.settle_unstated(pos, graph, page) == 1
    assert pos[3] == (1, 1), f"room 3 landed at {pos[3]}, not south of 2"


def test_a_conditional_exit_places_the_room_that_names_it():
    """The other direction of the same fact. The fill walks forward from what
    is placed, so a room nothing points at was unreachable however much it said
    about itself; here it is 2 that states the passage."""
    graph = cond_graph()
    graph[2]["exits"]["south"] = ("MAYBE", 3)
    del graph[3]["exits"]["north"]
    page = {1: 0, 2: 0, 3: 0}
    pos = G.fill_seed(graph, {1: (0, 0), 2: (1, 0)}, page)
    assert G.settle_unstated(pos, graph, page) == 1
    assert pos[3] == (1, 1), f"room 3 landed at {pos[3]}, not south of 2"


def test_a_room_a_plain_exit_touches_is_never_moved_by_a_door():
    """The narrowness is the safety. Letting a door place a room the stated
    graph names drags its whole component off its island seed and in among the
    measured cells, where free_cell resolves collisions the metric can see for
    the sake of a door the metric cannot; Moonmist lost three exits and with
    them its filled table."""
    graph = cond_graph()
    graph[3]["exits"]["west"] = ("MAYBE", 2)
    graph[1]["exits"]["south"] = ("OPEN", 3)
    page = {1: 0, 2: 0, 3: 0}
    assert G.unstated(graph) == set()
    pos = G.fill_seed(graph, {1: (0, 0), 2: (1, 0)}, page)
    before = dict(pos)
    assert G.settle_unstated(pos, graph, page) == 0
    assert pos == before


def test_a_measured_room_is_never_moved_by_a_door():
    """The frozen set reaches this pass too. A scan may read a room no plain
    exit touches, and a measured coordinate does not move to tidy an inferred
    one."""
    graph = cond_graph()
    page = {1: 0, 2: 0, 3: 0}
    pos = {1: (0, 0), 2: (1, 0), 3: (5, 5)}
    assert G.settle_unstated(pos, graph, page, frozen={1, 2, 3}) == 0
    assert pos[3] == (5, 5)


def test_a_room_no_exit_names_keeps_what_the_seed_gave_it():
    """Declining is a real outcome. A room behind an exit the story decides by
    running code names no destination for anything to step from, and there is
    nothing better available than where it already sits."""
    graph = cond_graph()
    graph[3]["exits"] = {"up": ("MAYBE", 0)}
    page = {1: 0, 2: 0, 3: 0}
    pos = {1: (0, 0), 2: (1, 0), 3: (4, 4)}
    assert G.settle_unstated(pos, graph, page) == 0
    assert pos[3] == (4, 4)


# ---- sliding the floors over each other ------------------------------------

def floor(base, page_of, y):
    """One floor of two rooms wired east-west, so that both are STATED -- a
    room no plain compass exit touches is left out of the vote, having only
    whatever the seed gave it."""
    graph = {
        base: {"name": "A%d" % base, "exits": {"east": ("OPEN", base + 1)}},
        base + 1: {"name": "B%d" % base, "exits": {"west": ("OPEN", base)}},
    }
    pos = {base: (0, y), base + 1: (1, y)}
    page = {base: page_of, base + 1: page_of}
    return graph, pos, page


def stack(dy=3):
    """Two such floors, joined by one staircase, the upper one offset."""
    g1, p1, pg1 = floor(1, 0, 0)
    g2, p2, pg2 = floor(11, 1, dy)
    g1[1]["exits"]["up"] = ("OPEN", 11)
    g2[11]["exits"]["down"] = ("OPEN", 1)
    g1.update(g2); p1.update(p2); pg1.update(pg2)
    return g1, p1, pg1


def test_a_staircase_comes_out_where_it_went_in():
    """The whole point. Only one floor is drawn at a time, so a stair has no
    line to follow across a page change and the coordinate has to carry it."""
    graph, pos, page = stack()
    off, landed, total = G.align_pages(pos, page, graph)
    # Counted at each end: the story states this staircase both ways.
    assert (landed, total) == (2, 2)
    assert pos[1] == pos[11], f"{pos[1]} and {pos[11]} are not the same cell"


def test_a_floor_moves_whole_or_not_at_all():
    """Every room on a page takes the same offset. A pass that moved one room
    of a floor to line a stair up would be rewriting the layout, not sliding
    it."""
    graph, pos, page = stack()
    before = dict(pos)
    off, landed, _ = G.align_pages(pos, page, graph)
    assert landed == 2
    for r in pos:
        assert pos[r] == (before[r][0] + off[page[r]][0],
                          before[r][1] + off[page[r]][1])
    assert pos[11][0] - pos[12][0] == before[11][0] - before[12][0]


def test_two_floors_with_no_stair_between_them_are_left_alone():
    """A forest and not a tree. Floors that say nothing to each other each keep
    their own origin rather than being stacked on a guess."""
    g1, p1, pg1 = floor(1, 0, 0)
    g2, p2, pg2 = floor(11, 1, 3)
    g1.update(g2); p1.update(p2); pg1.update(pg2)
    before = dict(p1)
    off, landed, total = G.align_pages(p1, pg1, g1)
    assert (landed, total) == (0, 0)
    assert off == {0: (0, 0), 1: (0, 0)} and p1 == before


def test_a_room_the_stated_graph_never_names_does_not_vote():
    """Its coordinate is whatever the seed gave it and nothing more --
    settle_unstated runs after this pass and cannot run before, since it reads
    the positions this pass produces. Letting one vote would let an island's
    origin decide where a whole floor goes."""
    graph, pos, page = stack()
    graph[99] = {"name": "Island", "exits": {"down": ("OPEN", 12)}}
    pos[99] = (40, 40)
    page[99] = 1
    assert 99 in G.unstated(graph)
    _, landed, total = G.align_pages(pos, page, graph)
    assert total == 2, "the island's staircase was counted"
    assert pos[1] == pos[11]


def test_contradictory_stairs_satisfy_the_better_attested_one():
    """Two floors joined by two staircases that disagree can satisfy at most
    one, so this is a plurality and not a solution."""
    graph, pos, page = stack()
    # A second stair between the same two floors, wanting a different offset.
    graph[2]["exits"]["up"] = ("OPEN", 12)
    graph[12]["exits"]["down"] = ("OPEN", 2)
    pos[12] = (9, 9)
    _, landed, total = G.align_pages(pos, page, graph)
    assert total == 4, "each stair is one exit at each end"
    assert landed == 2, "both stairs were satisfied, which is impossible"
    assert pos[1] == pos[11]


def test_a_slide_may_not_lose_a_stated_exit():
    """nudge's guard, for nudge's reason. agreement and alignment score every
    compass exit between two placed rooms and do not care what page either is
    on, so sliding floors apart really can break one -- it cost Hollywood four
    half-plane exits and four axis before this was here."""
    graph, pos, page = stack()
    # 2 is east of 1 on floor 0 and 11 sits on floor 1; wire a stated compass
    # exit ACROSS the floors that the slide would have to break.
    graph[2]["exits"]["east"] = ("OPEN", 11)
    graph[11]["exits"]["west"] = ("OPEN", 2)
    pos[11] = (2, 0)
    pos[12] = (3, 0)
    half_before = G.agreement(pos, graph)[0]
    before = dict(pos)
    _, landed, _ = G.align_pages(pos, page, graph)
    assert landed == 0, "the stair was straightened by bending a stated exit"
    assert pos == before
    assert G.agreement(pos, graph)[0] == half_before


# ---- a room reached only by stairs -----------------------------------------

def test_a_staircase_places_a_room_no_compass_exit_touches():
    """The Lurking Horror's Third Floor: `up` to the Roof, `down` to Second
    Floor, and an `out` the story decides by running code. No planar exit at
    either end, so the fill and conditional_cell both have nothing to say and
    it took its page's origin ten rows from every other room on its floor."""
    graph = {
        1: {"name": "Roof", "exits": {"east": ("OPEN", 2), "down": ("OPEN", 3)}},
        2: {"name": "Deck", "exits": {"west": ("OPEN", 1)}},
        3: {"name": "Third Floor", "exits": {"up": ("OPEN", 1)}},
    }
    page = {1: 0, 2: 0, 3: 0}
    # fill_seed places every room; an island lands on its page's origin, which
    # is what this pass exists to correct.
    pos = {1: (5, 5), 2: (6, 5), 3: (0, 0)}
    assert 3 in G.unstated(graph)
    assert G.settle_unstated(pos, graph, page) == 1
    # Its own floor's neighbour, resolved off the taken cell by free_cell.
    assert max(abs(pos[3][0] - 5), abs(pos[3][1] - 5)) == 1, pos[3]


def test_a_stair_on_the_rooms_own_floor_wins_a_stair_off_it():
    """Measured the other way round first, and it was worse: sending Third
    Floor to Second Floor's cell one floor down did make paging land on it and
    left it eleven rows from the Roof it opens onto -- a stair drawn as a line
    across the whole sheet."""
    graph = {
        1: {"name": "Roof", "exits": {"east": ("OPEN", 2), "down": ("OPEN", 3)}},
        2: {"name": "Deck", "exits": {"west": ("OPEN", 1)}},
        3: {"name": "Third Floor", "exits": {"up": ("OPEN", 1),
                                             "down": ("OPEN", 4)}},
        4: {"name": "Below", "exits": {"up": ("OPEN", 3), "east": ("OPEN", 5)}},
        5: {"name": "Hall", "exits": {"west": ("OPEN", 4)}},
    }
    page = {1: 0, 2: 0, 3: 0, 4: 1, 5: 1}
    pos = {1: (5, 5), 2: (6, 5), 3: (0, 0), 4: (-9, -9), 5: (-8, -9)}
    assert G.settle_unstated(pos, graph, page) == 1
    assert max(abs(pos[3][0] - 5), abs(pos[3][1] - 5)) == 1, pos[3]


# ---- the shipped file ------------------------------------------------------

def blocks():
    """{stem: (header text, [cell lines])} for every table in the .inc."""
    text = INC.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(
            r"/\*-{10,}\n \| MAP_ATLAS_(\w+)\n(.*?)\n -{10,}\*/\n"
            r"static const MapAtlasCell MAP_ATLAS_\1\[\] = \{\n(.*?)\n\};",
            text, re.S):
        out[m.group(1)] = (m.group(2), m.group(3).splitlines())
    return out


def test_every_table_says_how_many_of_its_rooms_were_measured():
    """A merged table's header states the split, and the cells have to agree
    with it -- the marker on an added cell is what lets a reader see which
    coordinates came off a drawing and which were inferred."""
    for stem, (header, cells) in blocks().items():
        added = [c for c in cells if G.ADDED_MARK in c]
        m = re.search(r"(\d+) walked in beside (\d+) measured", header)
        if m is None:
            assert not added, f"{stem} has added cells but its header is silent"
            continue
        assert len(added) == int(m.group(1)), (
            f"{stem} header claims {m.group(1)} added, file has {len(added)}")
        assert len(cells) - len(added) == int(m.group(2)), (
            f"{stem} header claims {m.group(2)} measured, file has "
            f"{len(cells) - len(added)}")


def test_a_merged_table_keeps_its_measured_provenance():
    """The merge appends to the scan's own header rather than replacing it. A
    table that lost the lane tolerance and page numbers it was read at would
    have thrown away the only record of how its measured half was obtained."""
    for stem, (header, cells) in blocks().items():
        if not any(G.ADDED_MARK in c for c in cells):
            continue
        assert "lane tolerance" in header, (
            f"{stem} was merged and lost the scan's own header")


def test_no_merged_table_gained_a_floor():
    """The paging rule invents nothing, so the floors a story declares are the
    floors its scan gave it. This is the invariant the whole rule exists for."""
    text = INC.read_text(encoding="utf-8")
    declared = {m.group(1): int(m.group(2)) for m in re.finditer(
        r"MAP_ATLAS_(\w+),\n\s+\(unsigned short\).*?\n.*?\n\s+(\d+) \},",
        text, re.S)}
    for stem, (_, cells) in blocks().items():
        pages = {int(re.match(r"\s*\{\s*\d+,\s*(\d+),", c).group(1))
                 for c in cells}
        assert declared.get(stem) == len(pages), (
            f"{stem} declares {declared.get(stem)} floors but its cells use "
            f"{len(pages)}")
        assert pages == set(range(len(pages))), \
            f"{stem} floors are not dense from zero"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
