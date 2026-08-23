"""Authored scene music: the file, the masks it compiles to, and the drift check."""
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_tracks
import scene_vocab as vocab


@pytest.fixture
def tree(tmp_path):
    """A repo root with two stories that have blessed tags."""
    scenes = tmp_path / "tools" / "assets" / "scenes"
    rooms = tmp_path / "tools" / "assets" / "rooms"
    scenes.mkdir(parents=True)
    rooms.mkdir(parents=True)
    for stem in ("ENCHANTR", "ZORK1"):
        (scenes / f"{stem}.json").write_text(json.dumps({"1": "CAVE", "2": "FOREST"}))
        (rooms / f"{stem}.json").write_text(
            json.dumps({"release": 1, "serial": "000000", "rooms": []}))
    return tmp_path


def test_bit_zero_is_the_first_disc_track_not_track_zero():
    """music_track_from_mask decodes bit i as track i + MUSIC_TRACK_MIN. Under
    the obvious encoding the disc's last track had no bit at all."""
    assert scene_tracks.mask_of([2]) == 1
    assert scene_tracks.tracks_of(1) == [2]


def test_every_disc_track_is_representable():
    """2..32 is the whole run in saturn/cd/music/tracklist -- thirty-one
    tracks, and a mask that could not reach one of them would be silence
    nobody would think to look for."""
    for track in range(scene_tracks.TRACK_MIN, scene_tracks.TRACK_MAX + 1):
        assert scene_tracks.tracks_of(scene_tracks.mask_of([track])) == [track]


def test_a_mask_round_trips_a_whole_playlist():
    tracks = [2, 9, 17, 32]
    assert scene_tracks.tracks_of(scene_tracks.mask_of(tracks)) == tracks


def test_one_track_compiles_to_one_bit_which_is_what_static_music_means(tree):
    """With a single bit the engine's draw has nothing to choose between, so
    that scene sounds the same every time without any engine change."""
    data = {"default": {"CAVE": [23]}, "games": {}}
    masks = scene_tracks.masks_for_game(data, "ZORK1")
    cave = masks[vocab.SCENE_INDEX["CAVE"]]
    assert bin(cave).count("1") == 1
    assert scene_tracks.tracks_of(cave) == [23]


def test_a_game_override_replaces_the_default_rather_than_adding_to_it():
    """Merging would make "fewer tracks here" impossible to express, and
    narrowing is the main reason to override at all."""
    data = {"default": {"CAVE": [18, 19, 20]},
            "games": {"ZORK1": {"CAVE": [23]}}}
    assert scene_tracks.for_game(data, "ZORK1")["CAVE"] == [23]
    assert scene_tracks.for_game(data, "ENCHANTR")["CAVE"] == [18, 19, 20]


def test_a_scene_with_nothing_authored_compiles_to_zero(tree):
    data = scene_tracks.empty()
    assert set(scene_tracks.masks_for_game(data, "ZORK1")) == {0}


def test_column_order_is_the_scene_vocabulary_order():
    """That order is the C enum value; a row built in any other order would
    give every scene someone else's music."""
    data = {"default": {"SPACE": [30]}, "games": {}}
    masks = scene_tracks.masks_for_game(data, "ZORK1")
    assert masks[vocab.SCENE_INDEX["SPACE"]] == scene_tracks.mask_of([30])
    assert sum(1 for m in masks if m) == 1


def test_validate_names_an_unknown_scene_and_an_impossible_track():
    data = {"default": {"NOT_A_SCENE": [4], "CAVE": [99]},
            "games": {"ZORK1": {"FOREST": [1]}}}
    problems = " | ".join(scene_tracks.validate(data))
    assert "NOT_A_SCENE" in problems
    assert "99" in problems
    assert "ZORK1" in problems and "1" in problems


def test_a_sound_document_has_nothing_to_report():
    assert scene_tracks.validate({"default": {"CAVE": [2, 32]},
                                  "games": {"ZORK1": {"FOREST": [17]}}}) == []


def test_save_then_load_round_trips_and_drops_empty_entries(tree):
    scene_tracks.save(tree, {"default": {"CAVE": [23], "FOREST": []},
                             "games": {"ZORK1": {"MAZE": [5]}, "ZORK3": {}}})
    back = scene_tracks.load(tree)
    assert back["default"] == {"CAVE": [23]}
    assert back["games"] == {"ZORK1": {"MAZE": [5]}}


def test_a_missing_file_is_nothing_authored_not_an_error(tree):
    assert scene_tracks.load(tree) == scene_tracks.empty()


def test_unreadable_json_degrades_instead_of_raising(tree):
    path = tree / scene_tracks.TRACKS_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("{ this is not json", encoding="utf-8")
    assert scene_tracks.load(tree) == scene_tracks.empty()


def test_nothing_authored_and_nothing_generated_is_not_stale(tree):
    assert scene_tracks.stale_games(tree) == []


def test_authoring_music_makes_that_game_stale_until_it_is_generated(tree):
    scene_tracks.save(tree, {"default": {}, "games": {"ZORK1": {"CAVE": [23]}}})
    assert scene_tracks.stale_games(tree) == ["ZORK1"]


def test_a_generated_table_that_matches_the_document_is_not_stale(tree):
    data = {"default": {"CAVE": [23]}, "games": {}}
    scene_tracks.save(tree, data)
    out = tree / "saturn" / "src" / "scene"
    out.mkdir(parents=True)
    rows = []
    for stem in scene_tracks.games_in(tree):
        cells = ", ".join(f"0x{m:08X}UL" if m else "0"
                          for m in scene_tracks.masks_for_game(data, stem))
        rows.append(f"    {{ {cells} }},")
    (out / "game_tracks.inc").write_text(
        "static const unsigned long SCENE_TRACKS[GAME_N][SCENE_N] = {\n"
        + "\n".join(rows) + "\n};\n", encoding="utf-8")
    assert scene_tracks.stale_games(tree) == []


def test_the_shipped_document_validates():
    """The one that actually compiles into the disc."""
    assert scene_tracks.validate(scene_tracks.load(REPO)) == []


def test_the_shipped_c_table_matches_the_shipped_document():
    """A commit that edits tracks.json without regenerating would ship music
    the file says is there and the disc does not have."""
    assert scene_tracks.stale_games(REPO) == []
