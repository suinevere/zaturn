"""Hand-edited search terms: per-tag overrides and per-story filter keywords."""
import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import art_nouns
import art_terms
import scene_vocab as vocab


@pytest.fixture
def tree(tmp_path):
    (tmp_path / "tools" / "assets").mkdir(parents=True)
    return tmp_path


def test_a_missing_file_overrides_nothing(tree):
    assert art_terms.load(tree) == art_terms.empty()


def test_unreadable_json_degrades_instead_of_raising(tree):
    path = tree / art_terms.TERMS_PATH
    path.write_text("{ not json", encoding="utf-8")
    assert art_terms.load(tree) == art_terms.empty()


def test_save_then_load_round_trips_and_drops_empties(tree):
    art_terms.save(tree, {"scenes": {"CAVE": ["lava tube"], "FOREST": []},
                          "games": {"ZORK1": ["medieval"], "ZORK3": []}})
    back = art_terms.load(tree)
    assert back["scenes"] == {"CAVE": ["lava tube"]}
    assert back["games"] == {"ZORK1": ["medieval"]}


def test_an_override_replaces_the_shipped_phrases_and_the_genre_wording():
    """The shipped phrases are a guess; the override is the correction someone
    made after seeing what they returned, so it has to win over both."""
    terms = {"scenes": {"CORRIDOR": ["hotel hallway"]}, "games": {}}
    assert art_nouns.nouns_for_scene("CORRIDOR", "SCIFI", terms) == ("hotel hallway",)
    assert art_nouns.nouns_for_scene("CORRIDOR", "SCIFI") != ("hotel hallway",)


def test_clearing_an_override_falls_back_to_the_shipped_phrases():
    terms = {"scenes": {}, "games": {}}
    assert (art_nouns.nouns_for_scene("CORRIDOR", "SCIFI", terms)
            == art_nouns.GENRE_NOUNS["SCIFI"]["CORRIDOR"])


def test_game_keywords_narrow_every_phrase_that_story_searches():
    """A period is not somewhere a camera can point, so it can only ever be a
    filter on the places -- appended to each phrase, not searched alone."""
    terms = {"scenes": {}, "games": {"DEADLINE": ["1930s"]}}
    got = art_nouns.nouns_for_game("DEADLINE", ["GARDEN"], terms)
    assert all(p.endswith(" 1930s") for p in got["GARDEN"])
    assert len(got["GARDEN"]) == len(art_nouns.nouns_for_scene("GARDEN", "DETECTIVE"))


def test_a_keyword_already_in_a_phrase_is_not_repeated():
    """"spaceship corridor" filtered by "spaceship" would ask for it twice and
    match less, not more."""
    assert art_terms.apply_terms(("spaceship corridor",), ["spaceship"]) \
        == ("spaceship corridor",)
    assert art_terms.apply_terms(("Spaceship corridor",), ["spaceship"]) \
        == ("Spaceship corridor",)


def test_no_keywords_leaves_the_phrases_exactly_as_they_were():
    assert art_terms.apply_terms(("cave", "grotto"), []) == ("cave", "grotto")


def test_keywords_are_scoped_to_their_own_story():
    terms = {"scenes": {}, "games": {"DEADLINE": ["1930s"]}}
    other = art_nouns.nouns_for_game("ZORK1", ["FOREST"], terms)
    assert not any("1930s" in p for p in other["FOREST"])


def test_overrides_are_shared_by_every_story():
    """Only the keywords belong to one game; a corrected phrase is a
    correction for the whole library."""
    terms = {"scenes": {"FOREST": ["birch stand"]}, "games": {}}
    for game in ("ZORK1", "DEADLINE", "PLNTFALL"):
        got = art_nouns.nouns_for_game(game, ["FOREST"], terms)
        assert got["FOREST"] == ("birch stand",)


def test_validate_names_an_unknown_scene_and_an_empty_term():
    problems = " | ".join(art_terms.validate(
        {"scenes": {"NOT_A_SCENE": ["x"], "CAVE": ["  "]},
         "games": {"ZORK1": [""]}}))
    assert "NOT_A_SCENE" in problems
    assert "CAVE" in problems
    assert "ZORK1" in problems


def test_a_sound_document_has_nothing_to_report():
    assert art_terms.validate({"scenes": {"CAVE": ["lava tube"]},
                               "games": {"ZORK1": ["medieval"]}}) == []


def test_the_shipped_document_validates():
    assert art_terms.validate(art_terms.load(REPO)) == []
