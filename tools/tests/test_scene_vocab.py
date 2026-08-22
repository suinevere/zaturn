# tools/tests/test_scene_vocab.py
"""The vocabulary is a table index, so order and membership are load-bearing."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_vocab as v


def test_thirty_two_scenes():
    assert len(v.SCENES) == 32


def test_names_are_unique():
    assert len(set(v.SCENES)) == len(v.SCENES)


def test_index_matches_position():
    for i, name in enumerate(v.SCENES):
        assert v.SCENE_INDEX[name] == i


def test_every_rule_names_a_real_scene():
    for pattern, scene in v.RULES:
        if scene is None:
            continue
        assert scene in v.SCENE_INDEX, f"{pattern} -> unknown scene {scene}"


def test_refusal_rules_return_none():
    assert v.scene_for_title("Dead End") is None
    assert v.scene_for_title("A Dead End Corridor") is None


def test_rule_patterns_are_lowercase():
    for pattern, _ in v.RULES:
        assert pattern == pattern.lower()


def test_plain_titles_resolve():
    assert v.scene_for_title("Forest") == "FOREST"
    assert v.scene_for_title("Sandy Beach") == "SHORE"
    assert v.scene_for_title("Kitchen") == "KITCHEN"
    assert v.scene_for_title("Maze") == "MAZE"


def test_priority_cases_resolve_to_the_more_specific_noun():
    assert v.scene_for_title("Shore Road") == "ROAD"
    assert v.scene_for_title("Wharf Road") == "ROAD"
    assert v.scene_for_title("Ocean Road") == "ROAD"
    assert v.scene_for_title("Upstairs Hallway") == "CORRIDOR"
    assert v.scene_for_title("Upstairs Closet") == "DARKROOM"
    assert v.scene_for_title("Hall of the Mountain King") == "CORRIDOR"


def test_shape_titles_refuse():
    for t in ("Dead End", "Cube", "Oddly-angled Room", "Land of Shadow",
              "The Troll Room", "Mirror Room", "Studio"):
        assert v.scene_for_title(t) is None, t


def test_every_scene_has_fetch_nouns():
    for name in v.SCENES:
        assert v.FETCH_NOUNS.get(name), name


def test_fetch_nouns_carry_no_dead_adjectives():
    dead = {"torchlit", "bustling", "creaking", "moored", "wooden",
            "dimly lit", "eerie", "foreboding", "ominous"}
    for name, nouns in v.FETCH_NOUNS.items():
        for n in nouns:
            assert n not in dead, f"{name}: {n} scored 0 in 589d6c5"
