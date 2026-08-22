"""The classifier is gone, and nothing still reaches for it."""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SELF = Path(__file__).resolve()

GONE = [
    "saturn/src/classify/room_class.c",
    "saturn/src/classify/room_class.h",
    "saturn/src/classify/room_class_data.c",
    "saturn/src/video/category_art.inc",
    "test/room_class_test.c",
    "test/music_category_test.c",
    "test/corpus/blessed.inc",
    "saturn/tests/test_room_genre.c",
    "saturn/tests/test_cd_mood_dirs.py",
    "saturn/tests/test_category_art.py",
]

DEAD_SYMBOLS = (
    "text_classify_room", "room_class_genre", "room_class_genre_locked",
    "display_set_art_band", "art_band_of_genre", "music_note_room_title",
    "text_game_room_category", "music_category_pool", "MUSIC_FALLBACK_ROOMS",
    "TC_NEUTRAL", "TEXT_NUM_CATEGORIES", "display_category_image",
)

# "test" included deliberately: test/music_mix_test.c, test/music_test.c and
# test/music_category_test.c all name these symbols, and leaving that directory
# out of the sweep is how a dead reference survives a deletion.
SEARCH_ROOTS = ["saturn/src", "saturn/mojozork.c", "saturn/tests", "tools", "test"]


def test_classifier_files_are_deleted():
    for rel in GONE:
        assert not (REPO / rel).exists(), rel


def test_no_source_still_names_a_dead_symbol():
    offenders = []
    for root in SEARCH_ROOTS:
        p = REPO / root
        files = [p] if p.is_file() else [
            f for f in p.rglob("*")
            if f.suffix in {".c", ".h", ".cxx", ".inc", ".py"}
            and ".venv" not in f.parts and "__pycache__" not in f.parts
            and f.resolve() != SELF   # this file names every DEAD_SYMBOLS entry itself
        ]
        for f in files:
            text = f.read_text(encoding="utf-8", errors="replace")
            for sym in DEAD_SYMBOLS:
                if re.search(rf"\b{sym}\b", text):
                    offenders.append(f"{f.relative_to(REPO)}: {sym}")
    assert not offenders, offenders


def test_corpus_room_text_is_kept():
    assert (REPO / "test" / "corpus" / "rooms.inc").exists()
