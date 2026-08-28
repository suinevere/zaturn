"""GAME.INF: the layout the Saturn parses, and the labels it will draw."""
import importlib.util
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

_spec = importlib.util.spec_from_file_location(
    "gen_game_info", REPO / "tools" / "gametitles" / "gen_game_info.py")
g = importlib.util.module_from_spec(_spec)
sys.modules["gen_game_info"] = g
_spec.loader.exec_module(g)

CATALOG = REPO / "saturn" / "src" / "menu" / "game_catalog.cxx"
LAYOUT = REPO / "saturn" / "src" / "menu" / "menu_layout.h"
TITLES = REPO / "saturn" / "src" / "menu" / "game_titles.c"

# A (release, serial) the shipped title table knows, so a record built from it
# must come out carrying the curated name rather than the filename.
ZORK1 = (119, "880429", "Zork I (1980)", 0)


def c_const(path, name):
    """The integer a `static const int NAME = n;` line assigns."""
    for line in path.read_text(encoding="utf-8").splitlines():
        head = line.strip()
        if head.startswith("static const int " + name) and "=" in head:
            return int(head.split("=")[1].strip().rstrip(";"))
    raise AssertionError(f"{name} not found in {path}")


def c_define(path, name):
    """The integer a `#define NAME n` line gives."""
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "#define" and parts[1] == name:
            return int(parts[2])
    raise AssertionError(f"{name} not found in {path}")


def story(release, serial):
    """The first 0x1a bytes of a v3 story: version, release, serial."""
    head = bytearray(0x1A)
    head[0] = 3
    head[2] = (release >> 8) & 0xFF
    head[3] = release & 0xFF
    head[0x12:0x18] = serial.encode("ascii")
    return bytes(head)


def write_story(folder, name, release, serial):
    (folder / name).write_bytes(story(release, serial))


NUL = bytes(1)


def records(blob):
    """Split a generated manifest into (name, label, category) triples."""
    count, size = struct.unpack(">HH", blob[4:8])
    assert size == g.REC_SIZE
    out = []
    for i in range(count):
        rec = blob[g.HDR_SIZE + i * size: g.HDR_SIZE + (i + 1) * size]
        name = rec[:g.NAME_MAX].split(NUL)[0].decode()
        label = rec[g.NAME_MAX:g.NAME_MAX + g.LABEL_MAX].split(NUL)[0].decode()
        out.append((name, label, rec[g.NAME_MAX + g.LABEL_MAX]))
    return out


def generate(tmp_path, stories, titles=TITLES):
    for name, release, serial in stories:
        write_story(tmp_path, name, release, serial)
    table = g.load_titles(titles)
    out = tmp_path / "GAME.INF"
    g.emit(g.collect(str(tmp_path), table), out)
    return out.read_bytes()


def test_layout_matches_what_the_saturn_parses():
    assert g.HDR_SIZE == c_const(CATALOG, "GAME_INFO_HDR")
    assert g.REC_SIZE == c_const(CATALOG, "GAME_INFO_REC")
    assert g.NAME_MAX == c_const(CATALOG, "GAME_INFO_NAME")
    assert g.LABEL_MAX == c_const(CATALOG, "GAME_INFO_LBL")
    assert g.LABEL_MAX - 1 >= c_define(LAYOUT, "MENU_ROW_TEXT_MAX")


def test_header_names_the_format_and_counts_the_records(tmp_path):
    blob = generate(tmp_path, [("ZORK1.Z3", ZORK1[0], ZORK1[1])])
    assert blob[:4] == g.MAGIC
    assert struct.unpack(">HH", blob[4:8]) == (1, g.REC_SIZE)
    assert len(blob) == g.HDR_SIZE + g.REC_SIZE


def test_a_known_story_carries_its_curated_title(tmp_path):
    blob = generate(tmp_path, [("ZORK1.Z3", ZORK1[0], ZORK1[1])])
    assert records(blob) == [("ZORK1.Z3", ZORK1[2], ZORK1[3])]


def test_an_unknown_story_falls_back_to_its_filename(tmp_path):
    blob = generate(tmp_path, [("MYSTERY.Z3", 1, "000000")])
    assert records(blob) == [("MYSTERY.Z3", "MYSTERY.Z3", g.GAME_CAT_OTHER)]


def test_a_file_that_is_not_a_v3_story_still_gets_a_record(tmp_path):
    (tmp_path / "BROKEN.Z3").write_bytes(bytes(4))
    blob = generate(tmp_path, [])
    assert records(blob) == [("BROKEN.Z3", "BROKEN.Z3", g.GAME_CAT_OTHER)]


def test_a_long_title_is_clamped_to_the_menu_width(tmp_path):
    long_title = "A" * 60
    table = tmp_path / "titles.c"
    table.write_text(f'    {{ 9, "770101", "{long_title}", 3 }},\n', encoding="utf-8")
    z3 = tmp_path / "z3"
    z3.mkdir()
    blob = generate(z3, [("LONG.Z3", 9, "770101")], titles=table)
    name, label, cat = records(blob)[0]
    assert (name, cat) == ("LONG.Z3", 3)
    assert label == "A" * (g.LABEL_MAX - 1)


def test_a_full_disc_stays_inside_one_sector():
    assert g.HDR_SIZE + g.MAX_GAMES * g.REC_SIZE <= 2048


def test_the_shipped_manifest_matches_the_shipped_games():
    z3 = REPO / "saturn" / "cd" / "data" / "Z3"
    blob = (z3 / "GAME.INF").read_bytes()
    on_disc = sorted(p.name.upper() for p in z3.glob("*.Z3"))
    assert [name for name, _, _ in records(blob)] == on_disc
