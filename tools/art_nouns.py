"""Derive photographable place-nouns from the room classifier's keyword table.

Description: Parses saturn/src/classify/room_class_data.c and collects every
    KT_STRUCTURE and KT_BIOME keyword, grouped by the mood folder its TC_*
    category maps to. KT_FEATURE rows are excluded: a rug is not a room, and a
    search for one returns a rug.

    Deriving rather than duplicating is the point. A keyword added to the
    classifier gains art coverage with no edit here, so the pictures a mood can
    show cannot drift from the words that mood can recognise.
Author: suinevere
Dependencies: re, pathlib
Globals: MOODS, TC_TO_FOLDER
"""
import re

# TC_* identifier -> disc folder, in enum order. The three categories that carry
# no art (TC_NEUTRAL, TC_DANGER, TC_TRIUMPH) are absent by construction.
TC_TO_FOLDER = {
    "TC_WILDERNESS":  "WILDER",
    "TC_UNDERGROUND": "UNDRGRND",
    "TC_WATER":       "WATER",
    "TC_NAUTICAL":    "NAUTICAL",
    "TC_TOWN":        "TOWN",
    "TC_DUNGEON":     "DUNGN",
    "TC_DESERT":      "DESERT",
    "TC_MAGIC":       "MAGIC",
    "TC_SCIFI":       "SCIFI",
    "TC_HORROR":      "HORROR",
    "TC_MYSTERY":     "MYSTERY",
    "TC_HOUSE":       "HOUSE",
}

MOODS = list(TC_TO_FOLDER.values())

_ROW = re.compile(
    r'\{\s*"([a-z ]+)"\s*,\s*(TC_[A-Z_]+)\s*,\s*(KT_[A-Z]+)\s*,\s*GN_[A-Z]+'
)

_PLACE_TIERS = ("KT_STRUCTURE", "KT_BIOME")


def nouns_by_mood(source):
    """Group the table's place-nouns by mood folder.

    Description: Returns one entry per mood, sorted and deduplicated. A mood whose
        keywords are all KT_FEATURE gets an empty list rather than being absent,
        so a caller can tell "no nouns of its own" from "not a mood".
    Author: suinevere
    Dependencies: N/A
    Globals: TC_TO_FOLDER, MOODS, _ROW, _PLACE_TIERS
    Params: source -- the text of room_class_data.c
    Returns: dict mapping mood folder name to a sorted list of nouns
    """
    out = {mood: set() for mood in MOODS}
    for word, cat, tier in _ROW.findall(source):
        if tier not in _PLACE_TIERS:
            continue
        folder = TC_TO_FOLDER.get(cat)
        if folder is None:
            continue
        out[folder].add(word)
    return {mood: sorted(words) for mood, words in out.items()}
