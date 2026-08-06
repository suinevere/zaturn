"""Gate the generated art count table against the files actually on the disc.

The disjointness check this file used to carry is gone: a picture now lives in
exactly one mood folder, so two moods cannot name the same file and there is
nothing left to assert.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TGA = REPO / "saturn" / "cd" / "data" / "TGA"
INC = REPO / "saturn" / "src" / "video" / "category_art.inc"
PNG = REPO / "tools" / "assets" / "png"

# make_tga.py lives in tools/, which is not on sys.path by default.
TOOLS = REPO / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import make_tga  # noqa: E402

MOODS = ["WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
         "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE"]

# TC_* enum order, saturn/src/sound/music.h:45. None means "carries no art".
ENUM_ORDER = [None, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
              "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
              None, None]


def parse_inc():
    text = INC.read_text()
    body = re.search(r"CATEGORY_ART_N\[TEXT_NUM_CATEGORIES\]\s*=\s*\{(.*?)\}",
                     text, re.S).group(1)
    return [int(v) for v in re.findall(r"\d+", body)]


def test_inc_has_one_entry_per_category():
    assert len(parse_inc()) == len(ENUM_ORDER)


def test_counts_match_the_disc():
    counts = parse_inc()
    for slot, mood in enumerate(ENUM_ORDER):
        if mood is None:
            assert counts[slot] == 0, f"row {slot} must carry no art"
            continue
        n = len(list((TGA / mood).glob("*.TGA"))) if (TGA / mood).is_dir() else 0
        assert counts[slot] == n, f"{mood}: table says {counts[slot]}, disc has {n}"


def test_filenames_are_two_digits_from_one():
    for mood in MOODS:
        d = TGA / mood
        if not d.is_dir():
            continue
        names = sorted(p.name for p in d.glob("*.TGA"))
        assert names == [f"{i:02d}.TGA" for i in range(1, len(names) + 1)], \
            f"{mood}: expected a gapless 01..NN run, got {names}"


def test_folder_and_file_names_fit_iso9660():
    for mood in MOODS:
        assert len(mood) <= 8, f"{mood} exceeds the 8-character directory limit"
    for mood in MOODS:
        d = TGA / mood
        if not d.is_dir():
            continue
        for p in d.glob("*"):
            stem, _, ext = p.name.partition(".")
            assert len(stem) <= 8 and len(ext) <= 3, f"{p.name} is not 8.3"


def test_splash_stays_at_the_root():
    assert (TGA / "SUINE.TGA").is_file()
    for mood in MOODS:
        assert not (TGA / mood / "SUINE.TGA").exists()


def test_generator_reproduces_the_splash():
    splash = TGA / "SUINE.TGA"
    if splash.is_file():
        splash.unlink()
    make_tga.convert_tree(PNG, TGA)
    assert splash.is_file(), "convert_tree must regenerate the root-level splash"
