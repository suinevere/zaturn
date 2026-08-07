"""Pin the noun derivation against a fixed fragment of the keyword table."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from art_nouns import MOODS, TC_TO_FOLDER, nouns_by_mood

SAMPLE = """
static const TextKeyword ROOM_KEYWORDS[] = {
    { "hallway",  TC_HOUSE,       KT_STRUCTURE, GN_ANY },
    { "ballroom", TC_HOUSE,       KT_STRUCTURE, GN_ANY },
    { "fireplace",TC_HOUSE,       KT_FEATURE,   GN_ANY },
    { "cave",     TC_UNDERGROUND, KT_STRUCTURE, GN_ANY },
    { "forest",   TC_WILDERNESS,  KT_BIOME,     GN_ANY },
    { "boulder",  TC_WILDERNESS,  KT_FEATURE,   GN_ANY },
    { "shaft",    TC_UNDERGROUND, KT_STRUCTURE, GN_FANTASY },
    { "shaft",    TC_SCIFI,       KT_STRUCTURE, GN_SCIFI },
    { "decay",    TC_HORROR,      KT_FEATURE,   GN_ANY },
};
"""


def test_structure_and_biome_rows_become_nouns():
    got = nouns_by_mood(SAMPLE)
    assert got["HOUSE"] == ["ballroom", "hallway"]
    assert got["WILDER"] == ["forest"]


def test_feature_rows_are_excluded():
    got = nouns_by_mood(SAMPLE)
    assert "fireplace" not in got["HOUSE"]
    assert "boulder" not in got["WILDER"]
    assert got["HORROR"] == []


def test_a_word_in_two_genres_lands_in_both_moods():
    got = nouns_by_mood(SAMPLE)
    assert "shaft" in got["UNDRGRND"]
    assert "shaft" in got["SCIFI"]


def test_every_mood_has_an_entry_even_when_empty():
    got = nouns_by_mood(SAMPLE)
    assert sorted(got) == sorted(MOODS)


def test_folder_names_fit_iso9660():
    for mood in MOODS:
        assert len(mood) <= 8


def test_the_real_table_parses():
    repo = Path(__file__).resolve().parents[2]
    src = (repo / "saturn" / "src" / "classify" / "room_class_data.c").read_text()
    got = nouns_by_mood(src)
    assert "hallway" in got["HOUSE"]
    assert "cave" in got["UNDRGRND"]
    assert got["HORROR"] == [], "HORROR's keywords are qualities, not places"
