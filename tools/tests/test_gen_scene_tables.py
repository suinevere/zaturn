"""Generated C tables: correct, bounded, and byte-identical on regeneration."""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_scene_tables as g
import scene_vocab as vocab

INC = REPO / "saturn" / "src" / "scene" / "game_rooms.inc"


def test_regeneration_is_byte_identical():
    before = INC.read_bytes()
    g.main([])
    assert INC.read_bytes() == before


def test_every_game_has_a_row():
    text = INC.read_text(encoding="utf-8")
    assert text.count("static const unsigned char GAME_ROOM_") == len(g.GAMES)


def test_zork1_row_is_keyed_by_release_and_serial():
    text = INC.read_text(encoding="utf-8")
    assert '{ 88, "840726"' in text


def test_scene_values_are_stored_plus_one():
    rows = g.room_bytes("ZORK1")
    assert all(0 <= b <= len(vocab.SCENES) for b in rows)
    assert any(b > 0 for b in rows)


def test_unblessed_object_is_zero():
    rows = g.room_bytes("ZORK1")
    assert rows[0] == 0


def test_row_length_is_the_v3_object_ceiling():
    assert len(g.room_bytes("ZORK1")) == 256


def test_c_enum_matches_the_python_vocabulary_in_order():
    header = (REPO / "saturn" / "src" / "scene" / "scene_map.h").read_text(encoding="utf-8")
    found = re.findall(r"SC_([A-Z_]+)\s*=\s*(\d+)", header)
    assert [n for n, _ in found] == list(vocab.SCENES)
