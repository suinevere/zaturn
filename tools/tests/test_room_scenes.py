"""Rules decide what they can; humans own everything they have ruled on."""
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import room_scenes as rs


ROOMS = [
    {"obj": 1, "title": "Forest", "description": "Trees.", "source": "static"},
    {"obj": 2, "title": "Dead End", "description": "A dead end.", "source": "static"},
    {"obj": 3, "title": "Cube", "description": None, "source": None},
]


def test_rules_decide_what_they_can():
    decided, refused = rs.decide(ROOMS)
    assert decided == {1: "FOREST"}


def test_refusals_carry_the_description():
    _, refused = rs.decide(ROOMS)
    titles = {r["title"]: r for r in refused}
    assert set(titles) == {"Dead End", "Cube"}
    assert titles["Dead End"]["description"] == "A dead end."


def test_human_verdict_survives_a_rule_rerun():
    blessed, review = rs.merge({2: "MAZE"}, {1: "FOREST"}, [
        {"obj": 2, "title": "Dead End", "description": "A dead end."},
        {"obj": 3, "title": "Cube", "description": None},
    ])
    assert blessed[2] == "MAZE"
    assert [r["obj"] for r in review] == [3]


def test_human_verdict_beats_a_conflicting_rule():
    blessed, _ = rs.merge({1: "MAZE"}, {1: "FOREST"}, [])
    assert blessed[1] == "MAZE"


def test_review_is_sorted_by_object_for_determinism():
    _, review = rs.merge({}, {}, [
        {"obj": 9, "title": "B", "description": None},
        {"obj": 3, "title": "A", "description": None},
    ])
    assert [r["obj"] for r in review] == [3, 9]


def test_blessed_scenes_must_be_in_the_vocabulary():
    import pytest
    with pytest.raises(ValueError):
        rs.merge({1: "NOT_A_SCENE"}, {}, [])


def test_identical_titles_group_into_one_review_entry():
    _, review = rs.merge({}, {}, [
        {"obj": 5, "title": "Maze", "description": "Twisty."},
        {"obj": 6, "title": "Maze", "description": "Twisty."},
    ])
    assert len(review) == 1
    assert review[0]["objs"] == [5, 6]


def test_human_verdict_survives_a_full_regeneration(tmp_path):
    rooms_dir = tmp_path / "rooms"
    scenes_dir = tmp_path / "scenes"
    rooms_dir.mkdir()
    scenes_dir.mkdir()

    rooms = {
        "rooms": [
            {"obj": 1, "title": "Forest", "description": "Trees.",
             "source": "static"},
        ],
    }
    (rooms_dir / "STORY.json").write_text(json.dumps(rooms))

    blessed_path = scenes_dir / "STORY.json"
    blessed_path.write_text(json.dumps({"1": "MAZE"}))

    rs.main(["--rooms-dir", str(rooms_dir), "--scenes-dir", str(scenes_dir)])

    on_disk = json.loads(blessed_path.read_text())
    assert on_disk == {"1": "MAZE"}

    rs.main(["--rooms-dir", str(rooms_dir), "--scenes-dir", str(scenes_dir)])

    on_disk_again = json.loads(blessed_path.read_text())
    assert on_disk_again == {"1": "MAZE"}
