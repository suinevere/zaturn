"""Pin the query cross-product and the vocabulary's validation rules."""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_queries
from art_nouns import MOODS

NOUNS = {m: [] for m in MOODS}
NOUNS["HOUSE"] = ["hallway", "kitchen"]
NOUNS["UNDRGRND"] = ["cave"]

VOCAB = {
    "HORROR": {
        "adjectives": ["dark", "eerie"],
        "donors": ["HOUSE", "UNDRGRND"],
        "extra_nouns": ["morgue"],
        "exclude_nouns": ["kitchen"],
        "target": 99,
    }
}


def test_cross_product_covers_adjectives_by_nouns():
    got = art_queries.build(VOCAB, NOUNS)["HORROR"]
    phrases = sorted(q.phrase for q in got)
    assert phrases == ["dark cave", "dark hallway", "dark morgue",
                       "eerie cave", "eerie hallway", "eerie morgue"]


def test_excluded_nouns_never_appear():
    got = art_queries.build(VOCAB, NOUNS)["HORROR"]
    assert not any("kitchen" in q.phrase for q in got)


def test_donor_is_recorded_for_the_folder_path():
    got = {q.phrase: q for q in art_queries.build(VOCAB, NOUNS)["HORROR"]}
    assert got["dark hallway"].donor == "HOUSE"
    assert got["dark cave"].donor == "UNDRGRND"
    assert got["dark morgue"].donor == "EXTRA"


def test_unknown_mood_key_is_rejected():
    with pytest.raises(ValueError, match="NOTAMOOD"):
        art_queries.validate({"NOTAMOOD": {"adjectives": ["x"], "donors": []}})


def test_unknown_donor_is_rejected():
    with pytest.raises(ValueError, match="NOPE"):
        art_queries.validate({"HORROR": {"adjectives": ["x"], "donors": ["NOPE"]}})


def test_a_mood_with_no_adjectives_is_rejected():
    with pytest.raises(ValueError, match="HORROR"):
        art_queries.validate({"HORROR": {"adjectives": [], "donors": [],
                                         "target": 99}})


def test_a_mood_with_no_target_is_rejected():
    with pytest.raises(ValueError, match="HORROR"):
        art_queries.validate({"HORROR": {"adjectives": ["dark"], "donors": []}})


def test_a_mood_with_no_reachable_noun_is_rejected():
    """HORROR donating only to itself would produce zero queries -- catch it here."""
    with pytest.raises(ValueError, match="HORROR"):
        art_queries.build({"HORROR": {"adjectives": ["dark"], "donors": ["HORROR"],
                                      "extra_nouns": [], "exclude_nouns": [],
                                      "target": 99}}, NOUNS)


def test_the_shipped_vocabulary_is_valid_and_covers_every_mood():
    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    assert sorted(vocab) == sorted(MOODS)

    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()
    from art_nouns import nouns_by_mood
    plan = art_queries.build(vocab, nouns_by_mood(src))
    for mood in MOODS:
        assert len(plan[mood]) >= vocab[mood]["target"], (
            f"{mood}: {len(plan[mood])} queries for a target of "
            f"{vocab[mood]['target']} -- add adjectives, donors or extra_nouns"
        )
