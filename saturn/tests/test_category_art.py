"""Gate the generated art count table against the files actually on the disc.

The disjointness check this file used to carry is gone: a picture now lives in
exactly one mood folder, so two moods cannot name the same file and there is
nothing left to assert.
"""
import re
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
TGA = REPO / "saturn" / "cd" / "data" / "TGA"
INC = REPO / "saturn" / "src" / "video" / "category_art.inc"
PNG = REPO / "tools" / "assets" / "png"

# make_tga.py lives in tools/, which is not on sys.path by default. Not imported
# here at module scope: it does `from PIL import Image`, and pulling that in for
# every test in this file (most of which never touch PIL) breaks collection of
# the whole directory for a bare `pytest` run with no tools/.venv -- the same
# failure test_lwram_splash_budget.py had for a different import. Only
# test_generator_reproduces_the_splash needs it, so it imports make_tga lazily.
TOOLS = REPO / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

MOODS = ["WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
         "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE"]

# TC_* enum order, saturn/src/sound/music.h:45. None means "carries no art".
ENUM_ORDER = [None, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
              "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
              None, None]


def parse_inc():
    """Parse CATEGORY_BAND's {base, count} rows out of the generated .inc.

    Returns a list of rows, one per TC_* enum slot, each a list of (base,
    count) tuples in ART_BAND_* (neutral first) order.
    """
    text = INC.read_text()
    body = re.search(
        r"CATEGORY_BAND\[TEXT_NUM_CATEGORIES\]\[ART_BAND_N\]\s*=\s*\{(.*)\};",
        text, re.S).group(1)
    rows = re.findall(r"\{\s*((?:\{\s*\d+\s*,\s*\d+\s*\}\s*,?\s*)+)\}", body)
    return [
        [tuple(int(v) for v in cell.split(","))
         for cell in re.findall(r"\{\s*(\d+\s*,\s*\d+)\s*\}", row)]
        for row in rows
    ]


def test_inc_has_one_entry_per_category():
    assert len(parse_inc()) == len(ENUM_ORDER)


def test_counts_match_the_disc():
    rows = parse_inc()
    for slot, mood in enumerate(ENUM_ORDER):
        bases = [base for base, _ in rows[slot]]
        counts = [count for _, count in rows[slot]]

        if mood is None:
            assert sum(counts) == 0, f"row {slot} must carry no art"
        else:
            n = len(list((TGA / mood).glob("*.TGA"))) if (TGA / mood).is_dir() else 0
            assert sum(counts) == n, \
                f"{mood}: bands sum to {sum(counts)}, disc has {n}"

        want_base = 0
        for base, count in zip(bases, counts):
            assert base == want_base, \
                f"row {slot} band base {base} does not follow the bands before it"
            want_base += count


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


def test_generator_reproduces_the_splash(tmp_path):
    # A test suite must not rewrite the repo it is testing: this used to delete
    # the real SUINE.TGA and run convert_tree over the whole disc TGA tree as
    # the destination, clobbering all 38 disc pictures on every pytest run.
    # Reading the real source tree (PNG) is fine -- it is not touched -- but the
    # generated output goes to tmp_path, never to TGA. The property under test
    # is unchanged: convert_tree, run against the real sources, must regenerate
    # the root-level splash.
    pytest.importorskip("PIL")
    import make_tga

    make_tga.convert_tree(PNG, tmp_path)
    assert (tmp_path / "SUINE.TGA").is_file(), \
        "convert_tree must regenerate the root-level splash"


def test_root_prunes_an_orphaned_output(tmp_path):
    # Mood folders already clear their stale *.TGA before writing (see
    # convert_tree). The root pass -- the boot splash's <STEM>.TGA outputs --
    # did not, so a source PNG renamed or deleted left its old TGA behind as an
    # orphan nothing on disc still points at. tmp_path keeps this off the real
    # disc tree, same as test_generator_reproduces_the_splash above.
    pytest.importorskip("PIL")
    from PIL import Image

    import make_tga

    src, dst = tmp_path / "png", tmp_path / "tga"
    src.mkdir()

    def make_png(path):
        Image.new("RGB", (make_tga.WIDTH, make_tga.HEIGHT), (10, 20, 30)).save(path)

    make_png(src / "OLDNAME.png")
    make_tga.convert_tree(src, dst)
    assert (dst / "OLDNAME.TGA").is_file()

    (src / "OLDNAME.png").unlink()
    make_png(src / "NEWNAME.png")
    make_tga.convert_tree(src, dst)

    assert not (dst / "OLDNAME.TGA").exists(), \
        "a renamed/deleted root source must not leave its old TGA behind"
    assert (dst / "NEWNAME.TGA").is_file(), \
        "normal splash generation must still produce the new output"
