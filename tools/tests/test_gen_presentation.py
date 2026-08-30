"""Zork I's generated presentation table: complete, bounded, byte-identical."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_presentation as g

INC = REPO / "saturn" / "src" / "scene" / "game_presentation.inc"


def test_regeneration_is_byte_identical():
    before = INC.read_bytes()
    g.main([])
    assert INC.read_bytes() == before


def test_every_saturn_room_is_claimed_exactly_once():
    join = g.build_join()
    assert len(join) == 110
    assert sorted(int(r["room"]) for r in join.values()) == list(range(110))


def test_the_table_is_keyed_by_release_and_serial():
    text = INC.read_text(encoding="utf-8")
    assert '{ 88, "840726", GAME_PRES_ZORK1 }' in text


def test_rooms_reference_seventy_four_of_the_seventy_five_frames():
    frames, index_of = g.frame_table()
    assert len(frames) == 74
    assert len(index_of) == 74
    assert "#define PRES_FRAME_N 74" in INC.read_text(encoding="utf-8")


def test_every_room_has_a_picture():
    join = g.build_join()
    _frames, index_of = g.frame_table()
    for sat in join.values():
        assert (sat["area_archive"], int(sat["frame"])) in index_of


def test_ten_rooms_are_silent():
    join = g.build_join()
    assert len([s for s in join.values() if int(s["cd_track"]) == 0]) == 10


def test_west_of_house_lands_on_the_house_exterior():
    join = g.build_join()
    obj = next(o for o, s in join.items()
               if s["title"].strip().upper() == "WEST OF HOUSE")
    assert join[obj]["image"] == "BHUS_00.png"
    assert int(join[obj]["cd_track"]) == 10


def test_a_wrong_story_identity_is_refused(monkeypatch):
    monkeypatch.setattr(g, "RELEASE", 89)
    try:
        g.build_join()
    except SystemExit:
        return
    raise AssertionError("a mismatched release was accepted")
