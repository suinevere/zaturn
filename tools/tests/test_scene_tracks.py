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
    (tmp_path / "tools" / "assets").mkdir(parents=True)
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
    for track in scene_tracks.tracks():
        assert scene_tracks.tracks_of(scene_tracks.mask_of([track])) == [track]


def test_the_disc_offers_thirty_one_tracks():
    assert len(scene_tracks.tracks()) == 31


def test_a_mask_round_trips_a_whole_playlist():
    playlist = [2, 9, 17, 32]
    assert scene_tracks.tracks_of(scene_tracks.mask_of(playlist)) == playlist


def test_one_track_naming_a_scene_compiles_to_one_bit():
    """Which is what static music means: with a single bit the engine's draw
    has nothing to choose between, so that scene sounds the same every time
    without any engine change."""
    masks = scene_tracks.masks({23: ["CAVE"]})
    cave = masks[vocab.SCENE_INDEX["CAVE"]]
    assert bin(cave).count("1") == 1
    assert scene_tracks.tracks_of(cave) == [23]


def test_several_tracks_naming_one_scene_all_reach_it():
    inverted = scene_tracks.by_scene({18: ["CAVE"], 19: ["CAVE"], 20: ["MINE"]})
    assert inverted["CAVE"] == [18, 19]
    assert inverted["MINE"] == [20]


def test_one_track_can_serve_several_scenes():
    """The disc has thirty-one tracks and the vocabulary has more scenes than
    that, so sharing is not an edge case, it is the normal state."""
    inverted = scene_tracks.by_scene({17: ["FOREST", "ROCKY", "GARDEN"]})
    assert inverted == {"FOREST": [17], "ROCKY": [17], "GARDEN": [17]}


def test_a_scene_nobody_named_compiles_to_zero():
    assert set(scene_tracks.masks({})) == {0}


def test_column_order_is_the_scene_vocabulary_order():
    """That order is the C enum value; a row built in any other order would
    give every scene someone else's music."""
    masks = scene_tracks.masks({30: ["SPACE"]})
    assert masks[vocab.SCENE_INDEX["SPACE"]] == scene_tracks.mask_of([30])
    assert sum(1 for m in masks if m) == 1


def test_there_is_one_row_of_masks_not_one_per_game():
    """The thirty-one CD-DA tracks are already most of the disc and every
    story shares them, so a scene sounds the same whichever game is loaded."""
    assert len(scene_tracks.masks({})) == len(vocab.SCENES)


def test_validate_names_an_unknown_scene_and_an_impossible_track():
    problems = " | ".join(scene_tracks.validate({4: ["NOT_A_SCENE"], 99: ["CAVE"]}))
    assert "NOT_A_SCENE" in problems
    assert "99" in problems


def test_a_sound_document_has_nothing_to_report():
    assert scene_tracks.validate({2: ["CAVE"], 32: ["FOREST", "ROCKY"]}) == []


def test_save_then_load_round_trips_and_drops_empty_entries(tree):
    scene_tracks.save(tree, {23: ["CAVE"], 24: [], 17: ["ROCKY", "FOREST"]})
    assert scene_tracks.load(tree) == {17: ["FOREST", "ROCKY"], 23: ["CAVE"]}


def test_the_file_is_keyed_by_track(tree):
    """The shape the page edits and the shape on disk are the same one."""
    scene_tracks.save(tree, {23: ["CAVE"]})
    raw = json.loads((tree / scene_tracks.TRACKS_PATH).read_text())
    assert raw == {"23": ["CAVE"]}


def test_a_missing_file_is_nothing_authored_not_an_error(tree):
    assert scene_tracks.load(tree) == {}


def test_unreadable_json_degrades_instead_of_raising(tree):
    path = tree / scene_tracks.TRACKS_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("{ this is not json", encoding="utf-8")
    assert scene_tracks.load(tree) == {}


def test_nothing_authored_and_nothing_generated_is_not_stale(tree):
    assert not scene_tracks.is_stale(tree)


def test_authoring_music_is_stale_until_it_is_generated(tree):
    scene_tracks.save(tree, {23: ["CAVE"]})
    assert scene_tracks.is_stale(tree)


def test_a_generated_table_that_matches_the_document_is_not_stale(tree):
    data = {23: ["CAVE"]}
    scene_tracks.save(tree, data)
    out = tree / "saturn" / "src" / "scene"
    out.mkdir(parents=True)
    cells = "\n".join(f"    {m if m == 0 else hex(m) + 'UL'},"
                      for m in scene_tracks.masks(data))
    (out / "game_tracks.inc").write_text(
        "static const unsigned long SCENE_TRACKS[SCENE_N] = {\n"
        + cells + "\n};\n", encoding="utf-8")
    assert not scene_tracks.is_stale(tree)


def test_the_shipped_document_validates():
    assert scene_tracks.validate(scene_tracks.load(REPO)) == []


def test_the_shipped_c_table_matches_the_shipped_document():
    """A commit that edits tracks.json without regenerating would ship music
    the file says is there and the disc does not have."""
    assert not scene_tracks.is_stale(REPO)
