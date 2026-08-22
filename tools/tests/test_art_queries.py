"""Pin the scene->query lift and its validation rules."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_queries
import scene_vocab as vocab

NOUNS = {"FOREST": ["forest", "pine grove"], "CAVE": ["cave", "grotto"]}


def test_every_noun_becomes_one_query():
    got = art_queries.build(NOUNS)
    assert sorted(q.phrase for q in got["FOREST"]) == ["forest", "pine grove"]
    assert sorted(q.phrase for q in got["CAVE"]) == ["cave", "grotto"]


def test_the_phrase_is_the_noun_itself():
    got = art_queries.build(NOUNS)
    for scene, queries in got.items():
        for q in queries:
            assert q.phrase == q.noun


def test_the_scene_is_recorded_on_every_query():
    got = art_queries.build(NOUNS)
    assert all(q.scene == "FOREST" for q in got["FOREST"])
    assert all(q.scene == "CAVE" for q in got["CAVE"])


def test_build_order_follows_the_nouns_mapping():
    got = art_queries.build(NOUNS)
    assert [q.noun for q in got["FOREST"]] == ["forest", "pine grove"]


def test_build_order_is_deterministic():
    first = [q.phrase for q in art_queries.build(NOUNS)["FOREST"]]
    second = [q.phrase for q in art_queries.build(NOUNS)["FOREST"]]
    assert first == second


def test_build_defaults_to_the_real_scene_vocabulary():
    got = art_queries.build()
    assert sorted(got) == sorted(vocab.SCENES)
    for scene in vocab.SCENES:
        assert len(got[scene]) == len(vocab.FETCH_NOUNS[scene])


def test_unknown_scene_key_is_rejected():
    with pytest.raises(ValueError, match="NOTASCENE"):
        art_queries.build({"NOTASCENE": ["x"]})


def test_a_scene_with_no_nouns_is_rejected():
    with pytest.raises(ValueError, match="FOREST"):
        art_queries.build({"FOREST": []})


def test_validate_returns_its_argument_unchanged():
    assert art_queries.validate(NOUNS) is NOUNS


def test_query_carries_no_donor_field():
    assert art_queries.Query._fields == ("scene", "noun", "phrase")


def test_donor_removal_is_explained_in_a_comment():
    src = Path(__file__).resolve().parents[1] / "art_queries.py"
    text = src.read_text(encoding="utf-8")
    assert "donor" in text.lower(), \
        "the donor concept's removal should be explained where it used to live"
