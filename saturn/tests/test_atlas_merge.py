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
