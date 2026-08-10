"""Turn the hand-edited art vocabulary into a per-mood list of search phrases.

Description: A mood contributes adjectives; the moods it names as donors
    contribute nouns. Their cross product is what gets searched.

    Donors exist because several moods name no place at all. TC_HORROR's keywords
    are corpse, decay, eerie, rotting, shadow, skeleton, stench -- qualities, and
    nothing photographs ninety-nine distinct stenches. Horror is a mood applied to
    a place, so it borrows nouns from the moods whose places it haunts. TC_MAGIC
    and TC_MYSTERY have the same shape; TC_HOUSE donates to itself.
Author: suinevere
Dependencies: json, collections, art_nouns
Globals: N/A
"""
import json
from collections import namedtuple

from art_nouns import MOODS

Query = namedtuple("Query", "mood donor noun adjective phrase")

"""
----------------------
| GENRES
| Description: The genre names noun_genre may use, matching room_class.h's
|   GN_FANTASY / GN_SCIFI / GN_MODERN. An untagged noun is neutral and serves
|   every game, which is what most art is.
| Author: suinevere
----------------------
"""
GENRES = ("FANTASY", "SCIFI", "MODERN")


def validate(vocab):
    """Reject a vocabulary that names a mood or donor that does not exist.

    Description: Raises rather than warning. A typo'd mood key silently fetches
        nothing for a real mood and 99 pictures for a folder no disc reads, which
        is not worth discovering after an afternoon of review.
    Author: suinevere
    Dependencies: N/A
    Globals: MOODS, GENRES
    Params: vocab -- the parsed art_queries.json
    Returns: the vocabulary, unchanged
    """
    for mood, entry in vocab.items():
        if mood not in MOODS:
            raise ValueError(f"{mood} is not a mood folder; expected one of {MOODS}")
        if not entry.get("adjectives"):
            raise ValueError(f"{mood} has no adjectives")
        for donor in entry.get("donors", []):
            if donor not in MOODS:
                raise ValueError(f"{mood}: donor {donor} is not a mood folder")
        target = entry.get("target")
        if not isinstance(target, int) or isinstance(target, bool) or target <= 0:
            raise ValueError(f"{mood} has no positive integer target")
        tags = entry.get("noun_genre", {})
        reachable = set(entry.get("extra_nouns", []))
        for noun, genre in tags.items():
            if genre not in GENRES:
                raise ValueError(
                    f"{mood}: noun_genre {noun!r} has genre {genre!r}; "
                    f"expected one of {GENRES}")
            if noun not in reachable:
                raise ValueError(
                    f"{mood}: noun_genre names {noun!r}, which is not one of "
                    f"this mood's extra_nouns; only a qualified noun carries "
                    f"a period")
    return vocab


def load(path):
    """Read and validate art_queries.json.

    Description: The only entry point production code uses to reach the
        vocabulary, so nothing downstream ever sees an unvalidated one.
    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: path -- the JSON file
    Returns: the validated vocabulary dict
    """
    with open(path, "r", encoding="utf-8") as fh:
        return validate(json.load(fh))


def build(vocab, nouns):
    """Cross each mood's adjectives with the nouns its donors contribute.

    Description: Raises when a mood reaches no noun at all, because that mood
        would silently fetch nothing. Emits the cross product noun-major,
        rotating the adjective per noun, so a budget that stops early still
        draws from across the whole vocabulary instead of exhausting a
        single adjective first. validate() already guarantees adjectives is
        non-empty, so no zero-adjective guard is needed here.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: vocab -- the validated vocabulary; nouns -- nouns_by_mood output
    Returns: dict mapping mood to a list of Query
    """
    plan = {}
    for mood, entry in vocab.items():
        excluded = set(entry.get("exclude_nouns", []))
        pairs = []
        for donor in entry.get("donors", []):
            for noun in nouns.get(donor, []):
                if noun not in excluded:
                    pairs.append((donor, noun))
        for noun in entry.get("extra_nouns", []):
            if noun not in excluded:
                pairs.append(("EXTRA", noun))
        if not pairs:
            raise ValueError(
                f"{mood} reaches no noun: its donors contribute none and it "
                f"lists no extra_nouns"
            )
        adjs = entry["adjectives"]
        plan[mood] = [
            Query(mood, donor, noun, adjs[(i + j) % len(adjs)],
                  "{} {}".format(adjs[(i + j) % len(adjs)], noun))
            for j in range(len(adjs))
            for i, (donor, noun) in enumerate(pairs)
        ]
    return plan
