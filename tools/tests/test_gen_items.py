#!/usr/bin/env python3
"""gen_items.py's refusals and its byte-identical regeneration.

The refusals are the point of the module. A zero or a silently dropped row
would show up only as a pane that never changes -- indistinguishable from an
item that legitimately has no picture -- so the generator has to raise instead.
"""
import json
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_items  # noqa: E402
import gen_room_corpus  # noqa: E402

# The Z-machine property that separates Zork I's treasures from everything else
# it can carry. Verified against ZORK1.Z3 rather than taken on trust: all
# nineteen bound objects carry it, and the only other objects that do are the
# four scoring rooms (East-West Passage, Cellar, Treasure Room, Kitchen), which
# is why the check below excuses rooms rather than naming an exception list.
TREASURE_PROP = 13

# The three near-misses the binding deliberately leaves out -- SWORD carries a
# zero TVALUE, and the two damaged variants are what a treasure becomes once it
# is worthless. Naming them here is the point of the test: they are the objects
# a careless widening of the binding would pick up.
NOT_TREASURES = ("sword", "broken jewel-encrusted egg", "broken clockwork canary")


def test_shipped_binding_resolves():
    rows = gen_items.resolve(gen_items.load_binding(), gen_items.story())
    assert len(rows) == 19
    assert sorted(p for _, p in rows) == list(range(19))
    assert len(set(o for o, _ in rows)) == 19


def test_every_bound_object_is_a_treasure():
    """The spec's central claim: the nineteen pictures are bound to the
    nineteen treasures, not to nineteen arbitrary objects. Zork I marks a
    treasure by carrying property 13, so that is what is asserted -- every
    bound object carries it, no non-room object outside the binding carries
    it, and the three objects the binding deliberately omits do not."""
    st = gen_items.story()
    rows = gen_items.resolve(gen_items.load_binding(), st)
    bound = {obj for obj, _ in rows}
    objs = {o.num: o for o in st.objects}
    rooms = {c.num for c in gen_room_corpus.find_rooms_hub(st)[1]}

    for obj in sorted(bound):
        assert TREASURE_PROP in objs[obj].properties, (
            f"object {obj} (\"{objs[obj].name}\") is bound to a picture but "
            f"carries no property {TREASURE_PROP} -- it is not a treasure")

    stray = sorted(n for n, o in objs.items()
                   if TREASURE_PROP in o.properties and n not in bound and n not in rooms)
    assert not stray, (
        f"objects {[(n, objs[n].name) for n in stray]} are treasures with no "
        "picture -- the binding is no longer the whole treasure set")

    for name in NOT_TREASURES:
        hits = [o for o in st.objects if (o.name or "").strip() == name]
        assert len(hits) == 1, f"\"{name}\" matches {len(hits)} objects, expected 1"
        assert hits[0].num not in bound, f"\"{name}\" is bound to a picture"
        assert TREASURE_PROP not in hits[0].properties, (
            f"\"{name}\" carries property {TREASURE_PROP} -- it would no longer "
            "be excluded by the treasure test above")


def test_refuses_unknown_name():
    with pytest.raises(SystemExit):
        gen_items.resolve({0: "no such object anywhere"}, gen_items.story())


def test_refuses_ambiguous_name():
    with pytest.raises(SystemExit):
        gen_items.resolve({0: "Coal Mine"}, gen_items.story())


def test_refuses_duplicate_object():
    with pytest.raises(SystemExit):
        gen_items.resolve({0: "chalice", 1: "chalice"}, gen_items.story())


def test_refuses_index_out_of_range():
    with pytest.raises(SystemExit):
        gen_items.resolve({19: "chalice"}, gen_items.story())
    with pytest.raises(SystemExit):
        gen_items.resolve({-1: "chalice"}, gen_items.story())


def test_refuses_wrong_story():
    with pytest.raises(SystemExit):
        gen_items.check_identity(87, "999999")


def test_regenerates_byte_identically():
    out = ROOT / "saturn" / "src" / "scene" / "game_items.inc"
    before = out.read_bytes()
    gen_items.main([])
    assert out.read_bytes() == before
