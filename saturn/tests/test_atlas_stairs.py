#!/usr/bin/env python3
"""Can the reader climb?

The map draws one floor at a time and L/R change floor. This file measures the
shipped table against every up and down exit the stories carry, which is the
question nobody was asking while four separate reports came back about it:
"below second floor is steam tunnels and above it is third floor".

Two different rules answer a floor change and both are held here.

**The one the port uses.** `map_model_climb` reads the staircase out of the room
under the crosshair and goes to ITS far end, so the only thing that can stop it
is a staircase whose far end the table does not place. That is what
`test_a_staircase_can_be_followed` counts, and 98% of the disc's 526 is the
floor it must not fall below.

**The fallback, for a room with no staircase that way**, which steps the page
index. Page order cannot be made to answer this and no numbering of the pages
would fix it: a page index is one line and a story's floors are a tree, so a
level with three floors above it -- The Lurking Horror's ground floor -- gets at
most one of them as its neighbour. What CAN be held is that regenerating the
atlas does not make the fallback worse, which is what `BACKWARDS` and `OK` are.
An ordering by height rather than by drawn sheet was built and measured against
these numbers and traded Lurking's three backwards steps for ten that skip; it
is not obviously better and it is not what shipped.

Run: pytest saturn/tests/test_atlas_stairs.py
"""
import os
import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "engine" / "map_atlas_data.inc"
Z3 = ROOT / "saturn" / "cd" / "data" / "Z3"
sys.path.insert(0, str(ROOT / "tools"))

import mapscan  # noqa: E402

VERT = ("up", "down")

# Staircases out of a placed room whose far end is placed too, against the
# total. A floor: filling more of a table in can only raise it.
FOLLOWABLE = (519, 526)

# How the page-index fallback scores per story: steps that land on the next
# page in the right direction, and steps that go the WRONG WAY. ok is a floor
# and backwards is a ceiling. Neither is a target -- see the header.
OK = {
    "ADVENT": 12, "CUTHROAT": 0, "ENCHANTR": 4, "HITCHHKR": 2, "HOLYWOOD": 5,
    "INFIDEL": 2, "INFOSAM5": 6, "INFOSAM7": 5, "LEATHERG": 7, "LURKING": 10,
    "MZORKI": 9, "MZORKI2": 8, "PLNDHRTS": 4, "PLNTFALL": 7, "SORCERER": 6,
    "SPLBRKR": 11, "STARCROS": 15, "STATFALL": 15, "WISHBRNG": 1, "ZORK1": 8,
    "ZORK2": 1, "ZORK3": 3,
}
BACKWARDS = {
    "ADVENT": 1, "LURKING": 3, "MZORKI": 1, "MZORKI2": 2, "SPLBRKR": 1,
    "STATFALL": 2, "ZORK1": 4, "ZORK3": 1,
}


def tables():
    """{stem: {room: page}} out of the shipped .inc, for stories on the disc."""
    text = INC.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S):
        if not (Z3 / (m.group(1) + ".Z3")).is_file():
            continue
        out[m.group(1)] = {int(c.group(1)): int(c.group(2)) for c in
                           re.finditer(r"\{\s*(\d+),\s*(\d+),", m.group(2))}
    return out


def graph_of(stem):
    return mapscan.room_graph(str(Z3 / (stem + ".Z3")))[0]


def stairs(stem, page, graph):
    """Every up/down exit out of a placed room that names a room."""
    for a in sorted(graph):
        if a not in page:
            continue
        for d, (kind, b) in sorted(graph[a]["exits"].items()):
            if d not in VERT or kind == "BLOCKED" or not b or b not in graph:
                continue
            yield a, d, b


TABLES = tables()


def test_a_staircase_can_be_followed():
    """The rule the port actually runs. A staircase whose far end the table does
    not place drops the reader back on the page index, which is the behaviour
    every one of the owner's reports was about."""
    both = total = 0
    for stem, page in TABLES.items():
        graph = graph_of(stem)
        for a, d, b in stairs(stem, page, graph):
            total += 1
            if b in page:
                both += 1
    assert (both, total) >= FOLLOWABLE, (
        f"{both}/{total} staircases have both ends placed, against "
        f"{FOLLOWABLE[0]}/{FOLLOWABLE[1]} recorded")


@pytest.mark.parametrize("stem", sorted(TABLES))
def test_the_page_order_fallback_does_not_get_worse(stem):
    """A ceiling on steps that run the wrong way and a floor on steps that
    land. Not a target: one page index cannot serve a tree of floors."""
    page = TABLES[stem]
    graph = graph_of(stem)
    ok = back = 0
    seen = set()
    for a, d, b in stairs(stem, page, graph):
        if b not in page or page[a] == page[b] or (a, b) in seen:
            continue
        seen.add((b, a))
        step = page[b] - page[a]
        want = 1 if d == "up" else -1
        if step == want:
            ok += 1
        elif (step > 0) != (want > 0):
            back += 1
    assert back <= BACKWARDS.get(stem, 0), (
        f"{stem} now has {back} staircases running backwards in page order, "
        f"against {BACKWARDS.get(stem, 0)} recorded")
    assert ok >= OK.get(stem, 0), (
        f"{stem} now lands {ok} staircases on the next page, against "
        f"{OK.get(stem, 0)} recorded")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
