"""Reconciliation, and the property that makes any of this worth trusting.

The expected table is stated as object numbers because that is what ships. They
were resolved once from the story and are asserted rather than recomputed:
Studio 94, Kitchen 203, Altar 212, Cave (TINY-CAVE) 46, Timber Room 206,
Drafty Room 228. mapscan.DIRW order puts UP at 8, DOWN at 9, WEST at 2, EAST at
1 and OUT at 11.

A literal table that only agrees with itself measures nothing, so the last test
scores MARKS against Zork I's ZIL source -- the one oracle on the disc that is
independent of both the scan and the compiled story.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_map_marks
import mapscan
import zil_exits

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PDF = os.path.join(ROOT, "tools", "assets", "cache", "zork1.pdf")
Z3 = os.path.join(mapscan.Z3DIR, "ZORK1.Z3")
ZIL = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)",
                   "zork1", "1dungeon.zil")

pytestmark = pytest.mark.skipif(
    not os.path.exists(PDF),
    reason="the Infocom map is not redistributed; fetch it into tools/assets/cache")

BAGGAGE, RETRACT = 1, 2

MARKS = [
    (94,  8, 203, BAGGAGE),
    (203, 9, 0,   RETRACT),
    (212, 9, 0,   BAGGAGE),
    (206, 2, 0,   BAGGAGE),
    (228, 1, 0,   BAGGAGE),
    (228, 11, 0,  BAGGAGE),
]


def test_the_six_marks_are_exactly_what_the_zil_says():
    assert sorted(gen_map_marks.marks_for("ZORK1", PDF)) == sorted(MARKS)


def test_the_white_cliffs_are_not_marked():
    """DEFLATE's refusal reads "The path is too narrow", which from the source
    alone is a baggage limit. Infocom drew those three lines plain. This is the
    case the scan exists to settle."""
    rooms = {m[0] for m in gen_map_marks.marks_for("ZORK1", PDF)}
    assert 32 not in rooms and 33 not in rooms and 39 not in rooms


def test_disabling_the_detector_makes_this_fail(monkeypatch):
    """The acceptance property. A calibration test that still passes when the
    thing it calibrates is removed measures nothing at all.

    Stated as an inequality rather than as pytest.raises(AssertionError),
    which _one_per_exit's own raise would satisfy just as well and so would
    pass for a reason that has nothing to do with the detector."""
    import trace_edges
    monkeypatch.setattr(trace_edges, "hamburger_seeds", lambda mask: [])
    assert sorted(gen_map_marks.marks_for("ZORK1", PDF)) != sorted(MARKS)


def _objects_named(graph, name):
    """Every object number in the compiled story carrying a given short name.

    A set rather than a number because Zork I gives two rooms the same name in
    several places -- both White Cliffs Beach rooms among them -- and the
    caller decides whether an ambiguous name is fatal or merely widens a set it
    is checking absence against.
    """
    return {o for o, r in graph.items() if mapscan.norm(r["name"]) == mapscan.norm(name)}


def _zil_descs(path):
    """{ZIL room symbol: its DESC string} for every room in a ZIL source.

    The bridge between the two oracles. The ZIL names a room TIMBER-ROOM or
    LOWER-SHAFT; the compiled story carries only what the game prints, "Timber
    Room" and "Drafty Room", and the object numbers this test asserts are keyed
    on those. Reading DESC out of the source is what lets the correspondence be
    derived rather than typed in -- and LOWER-SHAFT, whose symbol resembles no
    part of the name the player ever sees, is exactly why typing it in would be
    the weaker choice.

    Blocks are collapsed the same way zil_exits._blocks collapses them, because
    a DESC can wrap.
    """
    with open(path, "r", encoding="latin-1") as f:
        text = f.read()
    import re
    out = {}
    for chunk in text.split("<ROOM ")[1:]:
        line = " ".join(chunk.split())
        sym = re.match(r"([A-Z0-9-]+)", line)
        desc = re.search(r'\(DESC "([^"]*)"', line)
        if sym and desc:
            out[sym.group(1)] = desc.group(1)
    return out


def test_the_marked_exits_are_the_ones_the_zil_gates():
    """MARKS scored against the source, not against itself.

    Every baggage mark must be an exit Zork I's own ZIL gates on EMPTY-HANDED
    (the Timber-to-Drafty shaft, three exits) or COFFIN-CURE (the Altar's
    descent), plus the chimney, which is a PER routine rather than an IF and so
    is checked against routine_exits instead. Nothing DEFLATE gates may appear
    at all: its refusal reads like a baggage limit in the source and Infocom
    drew those lines plain, so its absence is the sharpest evidence the drawing
    -- not the wording of a refusal -- is what MARKS was derived from.
    """
    graph, _, _ = mapscan.room_graph(Z3)
    descs = _zil_descs(ZIL)
    gated = zil_exits.conditional_exits(ZIL)

    def one(symbol):
        objs = _objects_named(graph, descs[symbol])
        assert len(objs) == 1, f"{symbol} ({descs[symbol]}) is not a unique room name"
        return objs.pop()

    want = {(one(room), mapscan.DIRW.index(direction.lower()))
            for room, direction, _ in gated["EMPTY-HANDED"] + gated["COFFIN-CURE"]}
    assert ("STUDIO", "UP", "UP-CHIMNEY-FUNCTION") in zil_exits.routine_exits(ZIL)
    want.add((one("STUDIO"), mapscan.DIRW.index("up")))

    assert {(m[0], m[1]) for m in MARKS if m[3] & BAGGAGE} == want

    deflated = set()
    for room, _, dest in gated["DEFLATE"]:
        deflated |= _objects_named(graph, descs[room])
        deflated |= _objects_named(graph, descs[dest])
    assert deflated
    assert deflated.isdisjoint({m[0] for m in MARKS})
