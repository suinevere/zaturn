"""The review loop: show a refusal, take a verdict, never lose one."""
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_server


@pytest.fixture
def app(tmp_path):
    scenes = tmp_path / "tools" / "assets" / "scenes"
    scenes.mkdir(parents=True)
    (scenes / "ZORK1.json").write_text(json.dumps({"1": "FOREST"}))
    (scenes / "ZORK1.review.json").write_text(json.dumps([
        {"obj": 7, "objs": [7, 8], "title": "Maze", "description": "Twisty."},
        {"obj": 9, "objs": [9], "title": "Cube", "description": None},
    ]))
    a = scene_server.create_app(tmp_path)
    a.config["TESTING"] = True
    return a


def test_index_lists_games_with_outstanding_reviews(app):
    r = app.test_client().get("/")
    assert r.status_code == 200
    assert b"ZORK1" in r.data


def test_game_page_shows_title_and_description(app):
    r = app.test_client().get("/game/ZORK1")
    assert b"Maze" in r.data
    assert b"Twisty." in r.data


def test_game_page_offers_every_scene(app):
    r = app.test_client().get("/game/ZORK1")
    for name in ("FOREST", "MAZE", "SHIP_INT", "SPACE"):
        assert name.encode() in r.data


def test_verdict_writes_every_object_in_the_group(app, tmp_path):
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 7, "scene": "MAZE"})
    blessed = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed["7"] == "MAZE"
    assert blessed["8"] == "MAZE"


def test_verdict_removes_the_group_from_the_queue(app, tmp_path):
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 7, "scene": "MAZE"})
    review = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [9]


def test_verdict_rejects_a_scene_outside_the_vocabulary(app):
    r = app.test_client().post("/verdict",
                               json={"story": "ZORK1", "obj": 7, "scene": "NOPE"})
    assert r.status_code == 400


def test_existing_verdicts_are_never_dropped(app, tmp_path):
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 9, "scene": "CAVE"})
    blessed = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed["1"] == "FOREST"


def test_a_verdict_on_a_group_writes_every_object_not_just_the_handle(app, tmp_path):
    """A group of 3 objects must produce 3 blessed entries from one POST."""
    scenes = tmp_path / "tools" / "assets" / "scenes"
    (scenes / "ZORK1.review.json").write_text(json.dumps([
        {"obj": 5, "objs": [5, 6, 7], "title": "Twisty Passages",
         "description": None},
    ]))
    c = app.test_client()
    r = c.post("/verdict", json={"story": "ZORK1", "obj": 5, "scene": "MAZE"})
    assert r.status_code == 200
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert blessed["5"] == "MAZE"
    assert blessed["6"] == "MAZE"
    assert blessed["7"] == "MAZE"


def test_every_hinted_scene_is_a_real_scene():
    """MOOD_TO_SCENES is hand-picked, not derived; guard it against vocab drift."""
    for mood, scenes in scene_server.MOOD_TO_SCENES.items():
        for scene in scenes:
            assert scene in scene_server.vocab.SCENE_INDEX, (mood, scene)


def test_hints_are_scoped_to_the_same_story_no_cross_game_bleed(tmp_path):
    """"Kitchen" recurs across two different games with two different blessed
    moods. Each game must show only its own hint, never the other's -- the
    whole reason load_hints keys by (serial, title) instead of title alone."""
    scenes = tmp_path / "tools" / "assets" / "scenes"
    scenes.mkdir(parents=True)
    (scenes / "ZORK1.json").write_text(json.dumps({}))
    (scenes / "ZORK1.review.json").write_text(json.dumps([
        {"obj": 1, "objs": [1], "title": "Kitchen", "description": None},
    ]))
    (scenes / "ZORK2.json").write_text(json.dumps({}))
    (scenes / "ZORK2.review.json").write_text(json.dumps([
        {"obj": 1, "objs": [1], "title": "Kitchen", "description": None},
    ]))

    rooms = tmp_path / "tools" / "assets" / "rooms"
    rooms.mkdir(parents=True)
    (rooms / "ZORK1.json").write_text(json.dumps({"serial": "AAAAAA"}))
    (rooms / "ZORK2.json").write_text(json.dumps({"serial": "BBBBBB"}))

    (tmp_path / "tools" / "assets" / "blessed_moods.json").write_text(json.dumps({
        "AAAAAA": {"kitchen": "HOUSE"},
        "BBBBBB": {"kitchen": "UNDRGRND"},
    }))

    a = scene_server.create_app(tmp_path)
    a.config["TESTING"] = True
    c = a.test_client()

    r1 = c.get("/game/ZORK1")
    assert b"(was HOUSE)" in r1.data
    assert b"(was UNDRGRND)" not in r1.data

    r2 = c.get("/game/ZORK2")
    assert b"(was UNDRGRND)" in r2.data
    assert b"(was HOUSE)" not in r2.data


def test_skip_removes_the_group_from_the_queue_without_blessing_anything(app, tmp_path):
    c = app.test_client()
    r = c.post("/skip", json={"story": "ZORK1", "obj": 7})
    assert r.status_code == 200
    review = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [9]
    blessed = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed == {"1": "FOREST"}
