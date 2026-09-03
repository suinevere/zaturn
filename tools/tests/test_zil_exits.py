"""The ZIL oracle: Zork I's conditional and routine exits read from source.

This is the only ground truth on the disc -- Zork I is the one story whose ZIL
is in this repository -- and it is what the scanned cross-bar detector is
scored against. The expected sets below are transcribed from
1dungeon.zil and are asserted here so that a change to the source invalidates
them loudly rather than leaving the calibration silently stale.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import zil_exits

ZIL = os.path.join("cd", "Zork I - The Great Underground Empire (Japan)",
                   "zork1", "1dungeon.zil")


def test_empty_handed_gates_exactly_the_timber_shaft_passage():
    got = zil_exits.conditional_exits(ZIL)["EMPTY-HANDED"]
    assert got == [
        ("LOWER-SHAFT", "EAST", "TIMBER-ROOM"),
        ("LOWER-SHAFT", "OUT", "TIMBER-ROOM"),
        ("TIMBER-ROOM", "WEST", "LOWER-SHAFT"),
    ]


def test_coffin_cure_gates_exactly_the_altar_descent():
    got = zil_exits.conditional_exits(ZIL)["COFFIN-CURE"]
    assert got == [("SOUTH-TEMPLE", "DOWN", "TINY-CAVE")]


def test_deflate_gates_the_white_cliffs_and_is_not_a_baggage_limit():
    got = zil_exits.conditional_exits(ZIL)["DEFLATE"]
    assert got == [
        ("WHITE-CLIFFS-NORTH", "SOUTH", "WHITE-CLIFFS-SOUTH"),
        ("WHITE-CLIFFS-NORTH", "WEST", "DAMP-CAVE"),
        ("WHITE-CLIFFS-SOUTH", "NORTH", "WHITE-CLIFFS-NORTH"),
    ]


def test_the_chimney_is_a_routine_exit_with_no_destination_in_source():
    assert ("STUDIO", "UP", "UP-CHIMNEY-FUNCTION") in zil_exits.routine_exits(ZIL)


def test_a_multi_line_exit_clause_is_read_whole():
    """WHITE-CLIFFS-SOUTH's NORTH exit wraps before its IF, and SOUTH-TEMPLE's
    DOWN wraps twice. A line-at-a-time reader finds neither."""
    conds = zil_exits.conditional_exits(ZIL)
    assert ("WHITE-CLIFFS-SOUTH", "NORTH", "WHITE-CLIFFS-NORTH") in conds["DEFLATE"]
    assert ("SOUTH-TEMPLE", "DOWN", "TINY-CAVE") in conds["COFFIN-CURE"]
