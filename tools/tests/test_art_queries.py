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


SPREAD_NOUNS = {m: [] for m in MOODS}
SPREAD_NOUNS["HOUSE"] = ["hallway", "kitchen", "attic", "cellar"]

SPREAD_VOCAB = {
    "HOUSE": {
        "adjectives": ["cosy", "dusty", "grand"],
        "donors": ["HOUSE"],
        "extra_nouns": [],
        "exclude_nouns": [],
        "target": 99,
    }
}


def test_interleaved_build_is_a_complete_cross_product():
    got = art_queries.build(SPREAD_VOCAB, SPREAD_NOUNS)["HOUSE"]
    pairs = [(q.noun, q.adjective) for q in got]
    expected = {(noun, adj) for noun in SPREAD_NOUNS["HOUSE"]
                for adj in SPREAD_VOCAB["HOUSE"]["adjectives"]}
    assert set(pairs) == expected
    assert len(pairs) == len(set(pairs)) == 12


def test_first_round_spreads_adjectives_and_covers_every_noun():
    got = art_queries.build(SPREAD_VOCAB, SPREAD_NOUNS)["HOUSE"]
    first_round = got[:4]
    assert len(set(q.noun for q in first_round)) == 4
    assert len(set(q.adjective for q in first_round)) == 3


def test_truncating_to_a_budget_does_not_repeat_one_adjective():
    got = art_queries.build(SPREAD_VOCAB, SPREAD_NOUNS)["HOUSE"]
    budget_prefix = got[:3]
    assert len(set(q.adjective for q in budget_prefix)) > 1


def test_build_order_is_deterministic():
    first = [q.phrase for q in art_queries.build(SPREAD_VOCAB, SPREAD_NOUNS)["HOUSE"]]
    second = [q.phrase for q in art_queries.build(SPREAD_VOCAB, SPREAD_NOUNS)["HOUSE"]]
    assert first == second


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


def test_scifi_never_searches_a_noun_another_mood_also_owns():
    """A bare shared noun resolves to the other mood's sense, not this one.

    "cabin" is NAUTICAL's and the woods'; a SCIFI adjective does not reclaim
    it, so SCIFI must reach such nouns only through a qualified extra_noun.
    """
    from collections import defaultdict

    from art_nouns import nouns_by_mood

    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()
    nouns = nouns_by_mood(src)

    owners = defaultdict(set)
    for mood, words in nouns.items():
        for word in words:
            owners[word].add(mood)
    shared = {word for word, moods in owners.items() if len(moods) > 1}

    plan = art_queries.build(vocab, nouns)
    offenders = sorted({q.noun for q in plan["SCIFI"] if q.noun in shared})

    assert offenders == [], (
        f"SCIFI searches bare shared noun(s) {offenders}; another mood owns "
        f"the photographic sense. Add them to SCIFI's exclude_nouns and, if "
        f"the room still needs coverage, add a qualified extra_noun."
    )


PERIOD_ADJECTIVES = {"medieval", "cobbled", "victorian", "ancient"}
MODERN_ONLY_NOUNS = {"office", "submarine", "server hall"}


def test_no_phrase_pairs_a_period_adjective_with_a_modern_only_noun():
    """"medieval office" photographs as nothing and scored 1/22.

    The cross product is total, so a mood cannot hold both a pre-modern
    adjective and a noun that only exists after it. Period belongs in the
    noun, where it can be qualified, not in the adjective slot the whole
    mood shares.
    """
    from art_nouns import nouns_by_mood

    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()

    plan = art_queries.build(vocab, nouns_by_mood(src))
    clashes = sorted({
        q.phrase for queries in plan.values() for q in queries
        if q.adjective in PERIOD_ADJECTIVES and q.noun in MODERN_ONLY_NOUNS
    })

    assert clashes == [], (
        f"period-contradictory phrase(s) {clashes}; drop the adjective from "
        f"that mood or exclude the noun and add a qualified extra_noun"
    )


WORLD_AMBIGUOUS = {"NAUTICAL": {"cabin", "deck", "hull"}}


def test_a_mood_never_searches_a_noun_the_wider_world_reads_differently():
    """A cabin is a log cabin and a deck is a patio before either is a ship's.

    Distinct from the shared-noun guard above: no *mood* owns the rival
    reading, so scanning the classifier's table cannot find these. They are
    hand-listed from measured keep rates -- bare "cabin" scored 3/15 in
    NAUTICAL, bare "deck" 0/10 -- and must be reached qualified or not at all.
    """
    from art_nouns import nouns_by_mood

    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()

    plan = art_queries.build(vocab, nouns_by_mood(src))
    for mood, bare in WORLD_AMBIGUOUS.items():
        offenders = sorted({q.noun for q in plan[mood] if q.noun in bare})
        assert offenders == [], (
            f"{mood} searches bare {offenders}; the everyday sense of those "
            f"words is not this mood. Exclude them and add a qualified "
            f"extra_noun such as \"sailing ship cabin\"."
        )


def test_noun_genre_rejects_an_unknown_genre_name():
    vocab = {"HORROR": {"adjectives": ["dark"], "donors": ["HOUSE"],
                        "extra_nouns": ["morgue"], "exclude_nouns": [],
                        "noun_genre": {"morgue": "GOTHIC"}, "target": 99}}
    with pytest.raises(ValueError, match="GOTHIC"):
        art_queries.validate(vocab)


def test_noun_genre_rejects_a_noun_the_mood_cannot_reach():
    vocab = {"HORROR": {"adjectives": ["dark"], "donors": ["HOUSE"],
                        "extra_nouns": ["morgue"], "exclude_nouns": [],
                        "noun_genre": {"quarterdeck": "MODERN"}, "target": 99}}
    with pytest.raises(ValueError, match="quarterdeck"):
        art_queries.validate(vocab)


def test_noun_genre_is_optional():
    vocab = {"HORROR": {"adjectives": ["dark"], "donors": ["HOUSE"],
                        "extra_nouns": ["morgue"], "exclude_nouns": [],
                        "target": 99}}
    assert art_queries.validate(vocab) is vocab


def test_the_shipped_vocabulary_tags_both_nautical_periods():
    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    tags = vocab["NAUTICAL"]["noun_genre"]
    assert tags["sailing ship cabin"] == "FANTASY"
    assert tags["submarine interior"] == "MODERN"


DEAD_ADJECTIVES = {
    "wooden", "barred", "torchlit", "chrome", "panelled", "bustling",
    "creaking", "smoky", "narrow", "winding", "rain-streaked", "cluttered",
    "open", "dimly lit", "moored", "shimmering", "hushed", "veiled",
    "backlit", "scorched", "storm-tossed",
}


def test_no_mood_ships_an_adjective_measured_dead():
    """Each scored 0 keeps over 6+ fetches; together they burned 219 of 1271.

    A stock photo is tagged with what it looks like, not what it would sound
    or feel like. "creaking" is a sound and "hushed" is a silence; no
    photographer labels an image either. Every noun is crossed with all of a
    mood's adjectives, so one dead word costs that mood an eighth of its
    whole query space.
    """
    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")

    shipping = sorted({(mood, adj) for mood, entry in vocab.items()
                       for adj in entry["adjectives"]
                       if adj in DEAD_ADJECTIVES})

    assert shipping == [], (
        f"measured-dead adjective(s) still shipping: {shipping}. Replace each "
        f"with a word a photo caption would actually carry."
    )
