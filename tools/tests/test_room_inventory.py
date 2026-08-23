"""Static extraction must recover object numbers, not just titles."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_room_inventory as inv

Z3 = REPO / "tools" / "assets" / "Z3"


def test_zork1_room_count_and_identity():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    assert d["release"] == 88
    assert d["serial"] == "840726"
    assert d["count"] == 110


def test_object_numbers_are_unique_and_in_v3_range():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    objs = [r["obj"] for r in d["rooms"]]
    assert len(set(objs)) == len(objs)
    assert all(1 <= o <= 255 for o in objs)


def test_duplicate_titles_are_separate_rows():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    mazes = [r for r in d["rooms"] if r["title"] == "Maze"]
    assert len(mazes) == 15
    assert len({r["obj"] for r in mazes}) == 15


def test_routine_described_rooms_have_no_static_description():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    by_title = {r["title"]: r for r in d["rooms"] if r["title"] == "West of House"}
    assert by_title["West of House"]["description"] is None


def test_stored_descriptions_are_prose():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    attic = next(r for r in d["rooms"] if r["title"] == "Attic")
    assert attic["description"] and attic["description"][0].isupper()


def test_rows_are_sorted_by_object_number_for_determinism():
    d = inv.inventory_for(Z3 / "ZORK1.Z3")
    objs = [r["obj"] for r in d["rooms"]]
    assert objs == sorted(objs)
