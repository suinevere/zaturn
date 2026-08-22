"""Generated scene ranges may only name pictures the disc actually carries."""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import scene_vocab as vocab

TGA = REPO / "saturn" / "cd" / "data" / "TGA"
INC = REPO / "saturn" / "src" / "scene" / "game_scenes.inc"


def _rows():
    text = INC.read_text(encoding="utf-8")
    body = text.split("GAME_SCENE", 1)[1]
    return [[tuple(int(x) for x in pair)
             for pair in re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", line)]
            for line in body.splitlines() if line.strip().startswith("{ {")]


def test_every_row_has_one_entry_per_scene():
    for row in _rows():
        assert len(row) == len(vocab.SCENES)


def test_ranges_never_exceed_the_ninety_nine_budget():
    for row in _rows():
        for base, count in row:
            assert base + count <= 99


def test_ranges_within_a_game_do_not_overlap():
    for row in _rows():
        spans = sorted((b, b + c) for b, c in row if c)
        for (_, end), (nxt, _) in zip(spans, spans[1:]):
            assert end <= nxt


def test_every_counted_picture_exists_on_disc():
    text = INC.read_text(encoding="utf-8")
    dirs = re.findall(
        r'"([A-Z0-9]{1,8})"',
        text.split("GAME_DIR[GAME_N] = {", 1)[1].split("};", 1)[0])
    for game, row in zip(dirs, _rows()):
        for base, count in row:
            for i in range(1, count + 1):
                assert (TGA / game / f"{base + i:02d}.TGA").exists(), f"{game} {base+i}"
