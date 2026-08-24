"""The guided walkthrough: what it measures, and what it refuses to run."""
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import art_status
import walkthrough


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A repo root with one story tagged and one picture accepted for it."""
    assets = tmp_path / "tools" / "assets"
    (assets / "scenes").mkdir(parents=True)
    (assets / "rooms").mkdir(parents=True)
    (assets / "scenes" / "ZORK1.json").write_text(
        json.dumps({"7": "MAZE", "8": "MAZE", "9": "CAVE"}))
    (assets / "scenes" / "ZORK1.review.json").write_text(
        json.dumps([{"obj": 11, "objs": [11], "title": "Cube",
                     "description": None}]))
    (assets / "rooms" / "ZORK1.json").write_text(
        json.dumps({"release": 88, "serial": "840726", "rooms": []}))
    (assets / "art_manifest.json").write_text(json.dumps({
        "ZORK1:1": {"id": 1, "game": "ZORK1", "scene": "MAZE", "noun": "maze",
                    "status": art_status.ACCEPTED},
        "ZORK1:2": {"id": 2, "game": "ZORK1", "scene": "MAZE", "noun": "maze",
                    "status": art_status.CANDIDATE},
        "ZORK3:3": {"id": 3, "game": "ZORK3", "scene": "MAZE", "noun": "maze",
                    "status": art_status.ACCEPTED},
    }))
    monkeypatch.setattr(walkthrough, "ROOT", tmp_path)
    return tmp_path


def test_games_are_the_stories_with_both_files(tree):
    assert walkthrough.games() == ["ZORK1"]


def test_a_story_with_no_room_inventory_is_not_offered(tree):
    (tree / "tools" / "assets" / "scenes" / "GHOST.json").write_text("{}")
    assert walkthrough.games() == ["ZORK1"]


def test_progress_counts_what_is_tagged_and_what_is_left(tree):
    p = walkthrough.progress("ZORK1")
    assert p["tagged"] == 3
    assert p["left"] == 1
    assert p["scenes"] == ["CAVE", "MAZE"]


def test_progress_counts_only_this_story_pictures(tree):
    """Two stories curate independently, so ZORK3's accepted picture must not
    make ZORK1 look further along than it is."""
    p = walkthrough.progress("ZORK1")
    assert p["accepted"] == {"MAZE": 1}
    assert p["undecided"] == 1


def test_progress_names_the_scenes_with_no_art(tree):
    """The count that matters is scenes left empty, not pictures accepted: one
    empty scene is a room that draws nothing, however deep the rest is."""
    assert walkthrough.progress("ZORK1")["empty"] == ["CAVE"]


def test_progress_counts_the_tgas_actually_on_the_disc_tree(tree):
    assert walkthrough.progress("ZORK1")["tga"] == 0
    tga = tree / "saturn" / "cd" / "data" / "TGA" / "ZORK1"
    tga.mkdir(parents=True)
    (tga / "01.TGA").write_bytes(b"")
    (tga / "02.TGA").write_bytes(b"")
    assert walkthrough.progress("ZORK1")["tga"] == 2


def test_progress_on_an_untouched_story_is_zeroes_not_an_error(tree):
    p = walkthrough.progress("NOTAGAME")
    assert p["tagged"] == 0 and p["left"] == 0 and p["scenes"] == []


def test_a_stem_on_the_command_line_skips_the_menu(tree, monkeypatch):
    def no_input(*a, **k):
        raise AssertionError("naming a game must not prompt")
    monkeypatch.setattr("builtins.input", no_input)
    assert walkthrough.choose(["zork1"]) == "ZORK1"


def test_an_unknown_stem_falls_through_to_the_menu(tree, monkeypatch):
    monkeypatch.setattr("builtins.input", lambda *a, **k: "1")
    assert walkthrough.choose(["NOPE"]) == "ZORK1"


def test_the_menu_takes_a_number_or_a_stem(tree, monkeypatch):
    monkeypatch.setattr("builtins.input", lambda *a, **k: "1")
    assert walkthrough.choose([]) == "ZORK1"
    monkeypatch.setattr("builtins.input", lambda *a, **k: "zork1")
    assert walkthrough.choose([]) == "ZORK1"


def test_a_blank_answer_gives_up_rather_than_guessing(tree, monkeypatch):
    monkeypatch.setattr("builtins.input", lambda *a, **k: "")
    assert walkthrough.choose([]) is None


def test_q_at_a_prompt_stops_the_walkthrough():
    with pytest.raises(SystemExit):
        walkthrough.stop_if_quit("q")
    walkthrough.stop_if_quit("x")


def test_the_walkthrough_never_builds_the_disc():
    """Every build in this project is the owner's to start and to watch. The
    last step prints the command; it must not call it."""
    source = (REPO / "tools" / "walkthrough.py").read_text(encoding="utf-8")
    body = source[source.index("def step_build"):]
    assert "subprocess" not in body
    assert "run(" not in body.split("def main")[0]


def test_the_fetch_step_is_opt_in(monkeypatch, tree, capsys):
    """A fetch costs API quota and minutes, so it asks rather than assumes."""
    called = []
    monkeypatch.setattr(walkthrough, "anykey", lambda *a, **k: "x")
    monkeypatch.setattr(walkthrough, "run", lambda *a: called.append(a))
    walkthrough.step_fetch("ZORK1")
    assert not called
    assert "Skipped" in capsys.readouterr().out


def test_pressing_f_runs_the_fetcher_for_that_game(monkeypatch, tree):
    called = []
    monkeypatch.setattr(walkthrough, "anykey", lambda *a, **k: "f")
    monkeypatch.setattr(walkthrough, "run", lambda argv, why: called.append(argv))
    walkthrough.step_fetch("ZORK1")
    assert called and "--game" in [str(a) for a in called[0]]
    assert "ZORK1" in [str(a) for a in called[0]]


def test_paths_are_derived_from_the_checkout_not_hardcoded():
    source = (REPO / "tools" / "walkthrough.py").read_text(encoding="utf-8")
    assert ":\\" not in source and "/Users/" not in source
