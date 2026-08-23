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


def test_skip_defers_the_group_to_the_back_without_blessing_anything(app, tmp_path):
    """Skip must never cost a room. It rotates; only room_scenes.py used to be
    able to bring a dropped group back, and only by regenerating the queue."""
    c = app.test_client()
    r = c.post("/skip", json={"story": "ZORK1", "obj": 7})
    assert r.status_code == 200
    review = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [9, 7]
    blessed = json.loads(
        (tmp_path / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed == {"1": "FOREST"}


def test_undo_restores_the_group_and_erases_its_verdict(app, tmp_path):
    scenes = tmp_path / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 7, "scene": "MAZE"})
    r = c.post("/undo", json={"story": "ZORK1"})
    assert r.status_code == 200
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert "7" not in blessed and "8" not in blessed
    review = json.loads((scenes / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [7, 9]


def test_undo_puts_a_skipped_group_back_where_it_was(app, tmp_path):
    scenes = tmp_path / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/skip", json={"story": "ZORK1", "obj": 7})
    c.post("/undo", json={"story": "ZORK1"})
    review = json.loads((scenes / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [7, 9]


def test_undo_walks_back_more_than_one_verdict(app, tmp_path):
    scenes = tmp_path / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/verdict", json={"story": "ZORK1", "obj": 7, "scene": "MAZE"})
    c.post("/verdict", json={"story": "ZORK1", "obj": 9, "scene": "CAVE"})
    c.post("/undo", json={"story": "ZORK1"})
    c.post("/undo", json={"story": "ZORK1"})
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert blessed == {"1": "FOREST"}
    review = json.loads((scenes / "ZORK1.review.json").read_text())
    assert [g["obj"] for g in review] == [7, 9]


def test_undo_on_an_empty_stack_is_refused_not_silently_ignored(app):
    r = app.test_client().post("/undo", json={"story": "ZORK1"})
    assert r.status_code == 409


def test_retag_overwrites_a_verdict_and_is_itself_undoable(app, tmp_path):
    scenes = tmp_path / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/retag", json={"story": "ZORK1", "objs": [1], "scene": "CAVE"})
    assert json.loads((scenes / "ZORK1.json").read_text())["1"] == "CAVE"
    c.post("/undo", json={"story": "ZORK1"})
    assert json.loads((scenes / "ZORK1.json").read_text())["1"] == "FOREST"


def test_retag_rejects_a_scene_outside_the_vocabulary(app):
    r = app.test_client().post("/retag",
                               json={"story": "ZORK1", "objs": [1], "scene": "NOPE"})
    assert r.status_code == 400


def test_tagged_page_names_the_room_and_marks_who_decided_it(app, tmp_path):
    """Object 1 is blessed FOREST but the rules refuse "Cube", so the page must
    call it a human verdict, not a rule one."""
    rooms = tmp_path / "tools" / "assets" / "rooms"
    rooms.mkdir(parents=True)
    (rooms / "ZORK1.json").write_text(json.dumps(
        {"serial": "AAAAAA", "rooms": [{"obj": 1, "title": "Cube",
                                        "description": None}]}))
    r = app.test_client().get("/game/ZORK1/tagged")
    assert r.status_code == 200
    assert b"Cube" in r.data
    assert b"you" in r.data


def test_a_group_verdict_shows_its_scope_before_the_click(app):
    """Fifteen Mazes behind one button is the thing a reviewer must be told."""
    r = app.test_client().get("/game/ZORK1")
    assert b"all 2 rooms" in r.data


@pytest.fixture
def furnished(tmp_path):
    """A repo with a room inventory, which the room pages and UNSET need.

    UNSET re-derives the queue from the inventory rather than hand-inserting,
    so without one there is nothing to derive and the room cannot come back.
    """
    scenes = tmp_path / "tools" / "assets" / "scenes"
    scenes.mkdir(parents=True)
    rooms = tmp_path / "tools" / "assets" / "rooms"
    rooms.mkdir(parents=True)
    (rooms / "ZORK1.json").write_text(json.dumps({
        "serial": "AAAAAA", "release": 88,
        "rooms": [
            {"obj": 7, "title": "Maze", "description": "Twisty."},
            {"obj": 8, "title": "Maze", "description": "Twisty."},
            {"obj": 9, "title": "Cube", "description": "A featureless cube."},
            {"obj": 10, "title": "Forest", "description": "Trees."},
        ]}))
    (scenes / "ZORK1.json").write_text(json.dumps({"7": "MAZE", "8": "MAZE"}))
    (scenes / "ZORK1.review.json").write_text(json.dumps([
        {"obj": 9, "objs": [9], "title": "Cube", "description": "A featureless cube."},
    ]))
    a = scene_server.create_app(tmp_path)
    a.config["TESTING"] = True
    return a, tmp_path


def test_room_page_shows_the_captured_description_and_the_current_tag(furnished):
    app, _ = furnished
    r = app.test_client().get("/game/ZORK1/room/7")
    assert r.status_code == 200
    assert b"Twisty." in r.data
    assert b"MAZE" in r.data


def test_a_rule_tagged_room_is_reachable_and_retaggable(furnished):
    """The queue can never return you to a room it never showed you. Object 10
    is decided by rule, so only its own page can change it."""
    app, tmp = furnished
    c = app.test_client()
    assert c.get("/game/ZORK1/room/10").status_code == 200
    c.post("/retag", json={"story": "ZORK1", "objs": [10], "scene": "CAVE"})
    blessed = json.loads(
        (tmp / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed["10"] == "CAVE"


def test_room_page_404s_for_an_object_the_inventory_does_not_have(furnished):
    app, _ = furnished
    assert app.test_client().get("/game/ZORK1/room/999").status_code == 404


def test_room_page_names_the_rooms_a_tag_would_travel_to(furnished):
    """Two rooms share the title Maze, and a click tags both unless told
    otherwise -- the reviewer must be able to see that before clicking."""
    app, _ = furnished
    page = app.test_client().get("/game/ZORK1/room/7").get_data(as_text=True)
    assert "this room only" in page
    assert "all 2 rooms" in page


def test_unset_returns_a_refused_room_to_the_queue(furnished):
    """Cube names a shape, so the rules refuse it and only a human can rule.
    Unsetting must put it back in front of one, not leave it untagged and
    unreachable."""
    app, tmp = furnished
    scenes = tmp / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/retag", json={"story": "ZORK1", "objs": [9], "scene": "CAVE"})
    r = c.post("/retag", json={"story": "ZORK1", "objs": [9], "scene": None})
    assert r.status_code == 200
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert "9" not in blessed
    review = json.loads((scenes / "ZORK1.review.json").read_text())
    assert any(g["title"] == "Cube" for g in review)


def test_unset_on_a_rule_decided_room_reverts_to_the_rule(furnished):
    """"Maze" matches a title rule, so there is a standing answer underneath
    the human verdict. Unset drops the verdict; it does not overrule the
    rules, and the room does not become untagged."""
    app, tmp = furnished
    scenes = tmp / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/retag", json={"story": "ZORK1", "objs": [7, 8], "scene": "CAVE"})
    c.post("/retag", json={"story": "ZORK1", "objs": [7, 8], "scene": None})
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert blessed["7"] == "MAZE" and blessed["8"] == "MAZE"


def test_unset_is_undoable(furnished):
    app, tmp = furnished
    scenes = tmp / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/retag", json={"story": "ZORK1", "objs": [9], "scene": "CAVE"})
    c.post("/retag", json={"story": "ZORK1", "objs": [9], "scene": None})
    c.post("/undo", json={"story": "ZORK1"})
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert blessed["9"] == "CAVE"
    review = json.loads((scenes / "ZORK1.review.json").read_text())
    assert not any(9 in g["objs"] for g in review), \
        "restoring the tag must also take the room back out of the queue"


def test_no_object_is_ever_both_tagged_and_queued(furnished):
    """The one invariant the two files share, checked across a set and unset."""
    app, tmp = furnished
    scenes = tmp / "tools" / "assets" / "scenes"
    c = app.test_client()
    c.post("/retag", json={"story": "ZORK1", "objs": [9], "scene": "CAVE"})
    c.post("/retag", json={"story": "ZORK1", "objs": [9], "scene": None})
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    review = json.loads((scenes / "ZORK1.review.json").read_text())
    queued = {o for g in review for o in g["objs"]}
    assert not (queued & {int(k) for k in blessed})


@pytest.fixture
def growable(furnished):
    """A furnished repo carrying its own copy of scene_vocab.py to edit.

    append_scene writes to <root>/tools/scene_vocab.py, so every test that
    grows the vocabulary must own the file it grows -- pointing one at the
    real repo would append a scene to the shipping vocabulary. It also
    appends to the live module, which is process-wide, so the fixture puts
    that back afterwards or the next test inherits the new scene.
    """
    app, tmp = furnished
    (tmp / "tools" / "scene_vocab.py").write_text(
        (REPO / "tools" / "scene_vocab.py").read_text(encoding="utf-8"),
        encoding="utf-8")
    vocab = scene_server.vocab
    saved = (vocab.SCENES, dict(vocab.SCENE_INDEX), dict(vocab.FETCH_NOUNS))
    yield app, tmp
    vocab.SCENES = saved[0]
    vocab.SCENE_INDEX.clear()
    vocab.SCENE_INDEX.update(saved[1])
    vocab.FETCH_NOUNS.clear()
    vocab.FETCH_NOUNS.update(saved[2])


def _vocab_of(tmp):
    """SCENES and FETCH_NOUNS as the written file declares them."""
    ns = {}
    exec(compile((tmp / "tools" / "scene_vocab.py").read_text(encoding="utf-8"),
                 "scene_vocab", "exec"), ns)
    return ns["SCENES"], ns["FETCH_NOUNS"]


def test_a_new_scene_is_appended_never_inserted(growable):
    """SCENES' order is the C enum value and a column index in three generated
    tables. Appending is safe; moving anything silently repoints every row."""
    _, tmp = growable
    before, _ = _vocab_of(tmp)
    ok, why = scene_server.append_scene(tmp, "MISTROOM", ["misty room"])
    assert ok, why
    after, nouns = _vocab_of(tmp)
    assert after[:len(before)] == before, "existing scenes must not move"
    assert after[-1] == "MISTROOM"
    assert nouns["MISTROOM"] == ("misty room",)


def test_a_new_scene_always_gets_search_phrases(growable):
    """art_queries.validate refuses a scene it cannot search, so a scene in
    SCENES but not FETCH_NOUNS would take the next fetch run down."""
    _, tmp = growable
    assert scene_server.append_scene(tmp, "MIST_ROOM", [])[0]
    _, nouns = _vocab_of(tmp)
    assert nouns["MIST_ROOM"] == ("mist room",)


def test_a_malformed_or_duplicate_scene_name_is_refused(growable):
    _, tmp = growable
    for bad in ("", "x", "lower", "HAS SPACE", "9START", "A" * 20):
        ok, why = scene_server.append_scene(tmp, bad, ["x"])
        assert not ok and why, bad
    assert not scene_server.append_scene(tmp, "CAVE", ["x"])[0]


def test_adding_a_scene_from_a_room_page_tags_that_room_with_it(growable):
    app, tmp = growable
    c = app.test_client()
    r = c.post("/scene/new", json={"story": "ZORK1", "objs": [9],
                                   "name": "MISTROOM", "phrases": "misty room"})
    assert r.status_code == 200, r.get_json()
    assert r.get_json()["name"] == "MISTROOM"
    blessed = json.loads(
        (tmp / "tools" / "assets" / "scenes" / "ZORK1.json").read_text())
    assert blessed["9"] == "MISTROOM"


def test_a_refused_new_scene_changes_nothing(growable):
    app, tmp = growable
    before = (tmp / "tools" / "scene_vocab.py").read_text(encoding="utf-8")
    r = app.test_client().post("/scene/new",
                               json={"story": "ZORK1", "objs": [9],
                                     "name": "cave", "phrases": ""})
    assert r.status_code == 400
    assert (tmp / "tools" / "scene_vocab.py").read_text(encoding="utf-8") == before


def test_the_pages_warn_while_the_generated_header_is_a_scene_behind(furnished):
    """Adding a scene leaves three generated tables a column short, and the
    build failure that follows never mentions the vocabulary."""
    app, tmp = furnished
    header = tmp / "saturn" / "src" / "scene"
    header.mkdir(parents=True)
    (header / "scene_map.h").write_text("#define SCENE_N 1\n", encoding="utf-8")
    for url in ("/game/ZORK1", "/game/ZORK1/tagged", "/game/ZORK1/room/9"):
        page = app.test_client().get(url).get_data(as_text=True)
        assert "gen_scene_tables.py" in page, url


def test_no_warning_when_the_header_agrees_with_the_vocabulary(furnished):
    app, tmp = furnished
    header = tmp / "saturn" / "src" / "scene"
    header.mkdir(parents=True)
    (header / "scene_map.h").write_text(
        f"#define SCENE_N {len(scene_server.vocab.SCENES)}\n", encoding="utf-8")
    page = app.test_client().get("/game/ZORK1").get_data(as_text=True)
    assert "gen_scene_tables.py" not in page


def test_the_music_page_lists_every_disc_track(furnished):
    """One row per track, because that is the decision being made: you listen
    to a track and say where it belongs."""
    app, _ = furnished
    page = app.test_client().get("/game/ZORK1/tracks").get_data(as_text=True)
    for track in (2, 17, 32):
        assert f'data-track="{track}"' in page


def test_the_music_page_also_says_what_each_scene_will_play(furnished):
    app, _ = furnished
    page = app.test_client().get("/game/ZORK1/tracks").get_data(as_text=True)
    assert 'id="plays-MAZE"' in page
    assert "neutral pool" in page


def test_naming_one_scene_on_one_track_is_written_and_reflected(furnished):
    app, tmp = furnished
    c = app.test_client()
    r = c.post("/tracks", json={"track": 23, "scenes": "MAZE"})
    assert r.status_code == 200, r.get_json()
    assert r.get_json()["plays"]["MAZE"] == "23"
    doc = json.loads((tmp / "tools" / "assets" / "tracks.json").read_text())
    assert doc == {"23": ["MAZE"]}


def test_one_track_can_be_given_several_scenes(furnished):
    app, tmp = furnished
    r = app.test_client().post("/tracks",
                               json={"track": 17, "scenes": "FOREST, ROCKY"})
    assert r.get_json()["plays"]["FOREST"] == "17"
    assert r.get_json()["plays"]["ROCKY"] == "17"
    doc = json.loads((tmp / "tools" / "assets" / "tracks.json").read_text())
    assert doc == {"17": ["FOREST", "ROCKY"]}


def test_a_scene_named_by_two_tracks_reports_both(furnished):
    app, _ = furnished
    c = app.test_client()
    c.post("/tracks", json={"track": 18, "scenes": "CAVE"})
    r = c.post("/tracks", json={"track": 19, "scenes": "CAVE"})
    assert r.get_json()["plays"]["CAVE"] == "18, 19"


def test_clearing_a_track_retires_it(furnished):
    app, tmp = furnished
    c = app.test_client()
    c.post("/tracks", json={"track": 23, "scenes": "MAZE"})
    r = c.post("/tracks", json={"track": 23, "scenes": ""})
    assert r.get_json()["plays"]["MAZE"] == ""
    doc = json.loads((tmp / "tools" / "assets" / "tracks.json").read_text())
    assert doc == {}


def test_a_scene_name_is_accepted_in_any_case(furnished):
    app, _ = furnished
    r = app.test_client().post("/tracks", json={"track": 5, "scenes": "maze"})
    assert r.status_code == 200
    assert r.get_json()["written"] == "MAZE"


def test_a_name_that_is_not_a_scene_is_refused_not_dropped(furnished):
    """A name the page accepted and the vocabulary does not have would look
    authored here and be silence on the disc."""
    app, tmp = furnished
    r = app.test_client().post("/tracks", json={"track": 5, "scenes": "SWAMP"})
    assert r.status_code == 400
    assert not (tmp / "tools" / "assets" / "tracks.json").exists()


def test_a_track_the_disc_does_not_have_is_refused_not_dropped(furnished):
    """A number the page accepted and the disc cannot play is silence nobody
    would think to look for."""
    app, tmp = furnished
    r = app.test_client().post("/tracks", json={"track": 99, "scenes": "MAZE"})
    assert r.status_code == 400
    assert not (tmp / "tools" / "assets" / "tracks.json").exists()


def test_gibberish_in_the_track_field_is_refused(furnished):
    app, _ = furnished
    r = app.test_client().post("/tracks", json={"track": "seven", "scenes": "MAZE"})
    assert r.status_code == 400


def test_authoring_music_raises_the_regenerate_banner(furnished):
    """Editing tracks.json leaves the compiled C table behind, and the failure
    that follows is silence rather than an error."""
    app, _ = furnished
    c = app.test_client()
    c.post("/tracks", json={"track": 23, "scenes": "MAZE"})
    page = c.get("/game/ZORK1/tracks").get_data(as_text=True)
    assert "gen_scene_tables.py" in page


@pytest.fixture
def renamable(growable):
    """A growable tree that also carries art_nouns.py, a picture folder, music
    and a term override -- every place a tag name is written down."""
    app, tmp = growable
    (tmp / "tools" / "art_nouns.py").write_text(
        (REPO / "tools" / "art_nouns.py").read_text(encoding="utf-8"),
        encoding="utf-8")
    png = tmp / "tools" / "assets" / "png" / "ZORK1" / "MAZE"
    png.mkdir(parents=True)
    (png / "1.png").write_bytes(b"not really a png")
    (tmp / "tools" / "assets" / "tracks.json").write_text(
        json.dumps({"23": ["MAZE"]}), encoding="utf-8")
    (tmp / "tools" / "assets" / "search_terms.json").write_text(
        json.dumps({"scenes": {"MAZE": ["hedge maze"]}, "games": {}}),
        encoding="utf-8")
    saved = dict(scene_server.art_nouns.GENRE_NOUNS["FANTASY"])
    yield app, tmp
    scene_server.art_nouns.GENRE_NOUNS["FANTASY"].clear()
    scene_server.art_nouns.GENRE_NOUNS["FANTASY"].update(saved)


def test_editing_a_tags_phrases_is_written_and_shown_as_edited(furnished):
    app, tmp = furnished
    r = app.test_client().post("/search/scene",
                               json={"story": "ZORK1", "scene": "MAZE",
                                     "terms": "hedge maze, stone labyrinth"})
    assert r.status_code == 200
    assert r.get_json()["edited"] is True
    assert r.get_json()["written"] == "hedge maze, stone labyrinth"
    doc = json.loads((tmp / "tools" / "assets" / "search_terms.json").read_text())
    assert doc["scenes"]["MAZE"] == ["hedge maze", "stone labyrinth"]


def test_a_phrase_may_contain_spaces(furnished):
    """"rocky shore" is one search, not two, so only commas separate."""
    app, tmp = furnished
    app.test_client().post("/search/scene",
                           json={"story": "ZORK1", "scene": "MAZE",
                                 "terms": "twisty stone passages"})
    doc = json.loads((tmp / "tools" / "assets" / "search_terms.json").read_text())
    assert doc["scenes"]["MAZE"] == ["twisty stone passages"]


def test_clearing_a_tags_phrases_restores_the_shipped_ones(furnished):
    app, _ = furnished
    c = app.test_client()
    c.post("/search/scene", json={"story": "ZORK1", "scene": "MAZE",
                                  "terms": "hedge maze"})
    r = c.post("/search/scene", json={"story": "ZORK1", "scene": "MAZE",
                                      "terms": ""})
    assert r.get_json()["edited"] is False
    assert r.get_json()["queries"]["MAZE"], "a cleared tag is not left unsearchable"


def test_editing_a_tag_that_is_not_in_the_vocabulary_is_refused(furnished):
    app, _ = furnished
    r = app.test_client().post("/search/scene",
                               json={"story": "ZORK1", "scene": "NOPE",
                                     "terms": "x"})
    assert r.status_code == 400


def test_global_keywords_are_written_and_reach_every_query(furnished):
    app, tmp = furnished
    r = app.test_client().post("/search/game",
                               json={"story": "ZORK1", "terms": "medieval, stone"})
    assert r.status_code == 200
    assert r.get_json()["written"] == "medieval stone"
    doc = json.loads((tmp / "tools" / "assets" / "search_terms.json").read_text())
    assert doc["games"]["ZORK1"] == ["medieval", "stone"]
    assert "medieval stone" in r.get_json()["queries"]["MAZE"]


def test_clearing_the_global_keywords_removes_them_from_the_queries(furnished):
    app, _ = furnished
    c = app.test_client()
    c.post("/search/game", json={"story": "ZORK1", "terms": "medieval"})
    r = c.post("/search/game", json={"story": "ZORK1", "terms": ""})
    assert r.get_json()["written"] == ""
    assert "medieval" not in r.get_json()["queries"]["MAZE"]


def test_the_search_page_shows_what_is_actually_searched(furnished):
    app, _ = furnished
    c = app.test_client()
    c.post("/search/game", json={"story": "ZORK1", "terms": "medieval"})
    page = c.get("/game/ZORK1/search").get_data(as_text=True)
    assert "medieval" in page
    assert 'id="q-MAZE"' in page


def test_the_search_page_shows_only_this_story_tags_unless_asked(furnished):
    app, _ = furnished
    c = app.test_client()
    mine = c.get("/game/ZORK1/search").get_data(as_text=True)
    assert 'data-scene="MAZE"' in mine
    assert 'data-scene="SHIP_INT"' not in mine
    every = c.get("/game/ZORK1/search?all=1").get_data(as_text=True)
    assert 'data-scene="SHIP_INT"' in every


def test_renaming_a_tag_moves_the_vocabulary_entry_without_moving_its_position(
        renamable):
    """The position is the C enum value and a column index in the generated
    tables. Renaming is safe precisely because it does not move."""
    app, tmp = renamable
    before = scene_server.vocab.SCENE_INDEX["MAZE"]
    ok, why = scene_server.rename_scene(tmp, "MAZE", "LABYRINTH")
    assert ok, why
    assert "MAZE" not in scene_server.vocab.SCENE_INDEX
    assert scene_server.vocab.SCENE_INDEX["LABYRINTH"] == before
    assert scene_server.vocab.SCENES[before] == "LABYRINTH"
    text = (tmp / "tools" / "scene_vocab.py").read_text(encoding="utf-8")
    assert '"LABYRINTH"' in text and '"MAZE"' not in text


def test_renaming_a_tag_moves_every_rooms_verdict(renamable):
    """A tag name is what the blessed files store, so a rename that skipped
    them would untag every room that used it."""
    app, tmp = renamable
    scenes = tmp / "tools" / "assets" / "scenes"
    (scenes / "ZORK1.json").write_text(json.dumps({"7": "MAZE", "8": "MAZE",
                                                   "9": "CAVE"}))
    assert scene_server.rename_scene(tmp, "MAZE", "LABYRINTH")[0]
    blessed = json.loads((scenes / "ZORK1.json").read_text())
    assert blessed == {"7": "LABYRINTH", "8": "LABYRINTH", "9": "CAVE"}


def test_renaming_a_tag_moves_its_music_its_terms_and_its_pictures(renamable):
    app, tmp = renamable
    assert scene_server.rename_scene(tmp, "MAZE", "LABYRINTH")[0]
    music = json.loads((tmp / "tools" / "assets" / "tracks.json").read_text())
    assert music == {"23": ["LABYRINTH"]}
    terms = json.loads((tmp / "tools" / "assets" / "search_terms.json").read_text())
    assert terms["scenes"] == {"LABYRINTH": ["hedge maze"]}
    png = tmp / "tools" / "assets" / "png" / "ZORK1"
    assert (png / "LABYRINTH" / "1.png").exists()
    assert not (png / "MAZE").exists()


def test_renaming_a_tag_moves_its_genre_rewordings(renamable):
    app, tmp = renamable
    assert scene_server.rename_scene(tmp, "MAZE", "LABYRINTH")[0]
    assert "LABYRINTH" in scene_server.art_nouns.GENRE_NOUNS["SCIFI"]
    assert "MAZE" not in scene_server.art_nouns.GENRE_NOUNS["SCIFI"]
    text = (tmp / "tools" / "art_nouns.py").read_text(encoding="utf-8")
    assert '"LABYRINTH"' in text and '"MAZE"' not in text


def test_a_rename_to_a_name_already_taken_is_refused(renamable):
    app, tmp = renamable
    ok, why = scene_server.rename_scene(tmp, "MAZE", "CAVE")
    assert not ok and "already" in why
    assert "MAZE" in scene_server.vocab.SCENE_INDEX


def test_a_rename_to_a_malformed_name_is_refused(renamable):
    app, tmp = renamable
    for bad in ("", "x", "lower", "HAS SPACE", "9START"):
        ok, _ = scene_server.rename_scene(tmp, "MAZE", bad)
        assert not ok, bad
    assert "MAZE" in scene_server.vocab.SCENE_INDEX


def test_renaming_a_tag_that_does_not_exist_is_refused(renamable):
    app, tmp = renamable
    assert not scene_server.rename_scene(tmp, "NOPE", "LABYRINTH")[0]


def test_the_rename_route_reports_its_refusal(renamable):
    app, _ = renamable
    r = app.test_client().post("/search/rename",
                               json={"old": "MAZE", "new": "CAVE"})
    assert r.status_code == 400
    assert "already" in r.get_json()["error"]


def test_the_rename_route_renames(renamable):
    app, tmp = renamable
    r = app.test_client().post("/search/rename",
                               json={"old": "MAZE", "new": "LABYRINTH"})
    assert r.status_code == 200
    assert "LABYRINTH" in scene_server.vocab.SCENE_INDEX
