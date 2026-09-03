"""The --audit mode's bucketing, against hand-built graphs rather than a scan.

Everything here runs without the map PDFs, which are not redistributed: the
tracer supplies the drawn side of the audit and these tests supply it by hand,
so what is under test is the comparison against the exit graph and nothing
else.

The graphs are written in story_graph's shape -- {object: {"name", "exits":
{direction index: (kind, dest)}}} -- with mapscan.DIRW putting NORTH at 0,
SOUTH at 3, UP at 8 and DOWN at 9.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_map_marks

NORTH, SOUTH, UP, DOWN = 0, 3, 8, 9


def _room(name, exits):
    return {"name": name, "exits": exits}


def _labels(rows):
    return [r[0] for r in rows]


def test_a_one_way_exit_drawn_from_the_higher_numbered_room_is_still_listed():
    """The dedup bug this test exists for.

    Zork I's Altar (212) descends into Cave (46) and the Cave has no exit
    back, so the only exit joining the pair belongs to the higher-numbered
    room. A dedup that kept an exit only where its destination outranked its
    source dropped the pair from both loop iterations at once and reported
    nothing at all -- not a wrong row, no row -- which is the one failure a
    disagreement report cannot afford.
    """
    graph = {
        46: _room("Cave", {NORTH: ("OPEN", 0)}),
        212: _room("Altar", {DOWN: ("MAYBE", 46)}),
    }
    rows = gen_map_marks._graph_only(graph, {}, {"cave", "altar"})
    assert _labels(rows) == ["Cave (46) -- Altar (212)"]
    assert rows[0][2] == "yes"


def test_the_same_pair_drawn_the_other_way_round_is_listed_once():
    """A two-way passage is one row, not two. The unordered collection has to
    survive both rooms naming each other."""
    graph = {
        46: _room("Cave", {UP: ("OPEN", 212)}),
        212: _room("Altar", {DOWN: ("OPEN", 46)}),
    }
    rows = gen_map_marks._graph_only(graph, {}, {"cave", "altar"})
    assert _labels(rows) == ["Cave (46) -- Altar (212)"]


def test_a_pair_the_drawing_traced_is_not_graph_only():
    graph = {
        46: _room("Cave", {NORTH: ("OPEN", 0)}),
        212: _room("Altar", {DOWN: ("MAYBE", 46)}),
    }
    drawn = {frozenset(("cave", "altar")): [("cave", "altar"), [], {4}]}
    assert gen_map_marks._graph_only(graph, drawn, {"cave", "altar"}) == []


def test_a_room_no_page_drew_is_not_graph_only():
    """The bucket measures the tracer's line coverage, so a room it was never
    shown cannot count against it."""
    graph = {
        46: _room("Cave", {NORTH: ("OPEN", 0)}),
        212: _room("Altar", {DOWN: ("MAYBE", 46)}),
    }
    assert gen_map_marks._graph_only(graph, {}, {"altar"}) == []


def test_an_exit_with_no_destination_asserts_no_pair():
    """A blocked or conditional exit the story records no destination for
    joins nothing, so it must not manufacture a graph-only row."""
    graph = {
        94: _room("Studio", {UP: ("MAYBE", 0)}),
        203: _room("Kitchen", {DOWN: ("MAYBE", 0)}),
    }
    assert gen_map_marks._graph_only(graph, {}, {"studio", "kitchen"}) == []


def test_eligibility_is_the_shipped_all_maybe_rule():
    """_eligible has to agree with marks_for's veto, not approximate it: one
    OPEN or BLOCKED exit anywhere on the pair takes the pair out of reach."""
    maybe = {
        46: _room("Cave", {}),
        212: _room("Altar", {DOWN: ("MAYBE", 46)}),
    }
    assert gen_map_marks._eligible(maybe, 212, 46)

    stated = {
        46: _room("Cave", {UP: ("OPEN", 212)}),
        212: _room("Altar", {DOWN: ("MAYBE", 46)}),
    }
    assert not gen_map_marks._eligible(stated, 212, 46)

    refused = {
        46: _room("Cave", {UP: ("BLOCKED", 0)}),
        212: _room("Altar", {DOWN: ("MAYBE", 46)}),
    }
    assert not gen_map_marks._eligible(refused, 212, 46)

    joined_by_nothing = {
        46: _room("Cave", {}),
        212: _room("Altar", {}),
    }
    assert not gen_map_marks._eligible(joined_by_nothing, 212, 46)


def test_a_pair_the_graph_does_not_assert_is_drawn_only():
    graph = {
        72: _room("Cellar", {SOUTH: ("BLOCKED", 0)}),
        80: _room("South of House", {NORTH: ("BLOCKED", 0)}),
    }
    bucket, _why = gen_map_marks._bucket(graph, (80, 72), None)
    assert bucket == "drawn only"


def test_an_arrowhead_against_the_graph_s_one_way_is_a_direction_difference():
    graph = {
        46: _room("Cave", {}),
        212: _room("Altar", {DOWN: ("OPEN", 46)}),
    }
    assert gen_map_marks._bucket(graph, (212, 46), 46)[0] == "agree"
    assert gen_map_marks._bucket(graph, (212, 46), 212)[0] == "direction differs"


def test_a_head_is_believed_only_when_every_trace_agrees():
    assert gen_map_marks._settled_head([46, 46]) == 46
    assert gen_map_marks._settled_head([46, None]) is None
    assert gen_map_marks._settled_head([46, 212]) is None
    assert gen_map_marks._settled_head([]) is None
