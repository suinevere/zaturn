"""Runtime capture fills descriptions static decoding cannot reach."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_room_inventory as inv


def _inv(rows):
    return {"story": "T.Z3", "release": 1, "serial": "000000",
            "desc_prop": 11, "count": len(rows), "rooms": rows}


def test_runtime_text_fills_a_missing_description():
    d = _inv([{"obj": 5, "title": "Cellar", "description": None}])
    out = inv.merge_runtime(d, {"Cellar": "You are in a dark and damp cellar."})
    assert out["rooms"][0]["description"] == "You are in a dark and damp cellar."
    assert out["rooms"][0]["source"] == "runtime"


def test_static_text_wins_over_runtime():
    d = _inv([{"obj": 5, "title": "Attic", "description": "A dusty attic."}])
    out = inv.merge_runtime(d, {"Attic": "Something else entirely."})
    assert out["rooms"][0]["description"] == "A dusty attic."
    assert out["rooms"][0]["source"] == "static"


def test_duplicate_titles_all_receive_the_capture():
    d = _inv([{"obj": 5, "title": "Maze", "description": None},
              {"obj": 6, "title": "Maze", "description": None}])
    out = inv.merge_runtime(d, {"Maze": "This is a maze of twisty passages."})
    assert all(r["description"].startswith("This is a maze") for r in out["rooms"])


def test_unreached_room_keeps_none_and_null_source():
    d = _inv([{"obj": 5, "title": "Cube", "description": None}])
    out = inv.merge_runtime(d, {})
    assert out["rooms"][0]["description"] is None
    assert out["rooms"][0]["source"] is None
