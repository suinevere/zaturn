"""The song manifest, the tolerant MIDI reader, and the catalogue they emit.

    python -m pytest saturn/tests/test_songs_manifest.py -q

Three things are pinned here and each cost time to find.

The reader has to survive a file that lies about its own size. sgdragon.mid
declares eight tracks, carries five, and gives the last of those a length that
ends 290 bytes past the end of the file; before the clamp it raised IndexError
and the tune was simply unconvertible.

The manifest has to fail loudly. A typo in a filename or a duplicate id would
otherwise reach the generated C as a missing tune or a silently overwritten
one, and the build would succeed.

The catalogue has to be addressable. Every tune shares one cell array and one
order array and points at its own offset inside them, and both counts are
unsigned char in TrackerSong -- so a tune of 256 patterns would wrap into
playing the wrong bars rather than failing to build.
"""
import json
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "assets"))
import mid2pat

MANIFEST = ROOT / "tools" / "assets" / "music" / "songs.json"


def _bare_midi(tmp_path, track_len, body):
    """A one-track file whose header may claim more than the body holds."""
    head = b"MThd" + (6).to_bytes(4, "big") + \
           (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + (96).to_bytes(2, "big")
    p = tmp_path / "t.mid"
    p.write_bytes(head + b"MTrk" + track_len.to_bytes(4, "big") + body)
    return str(p)


def test_a_track_longer_than_the_file_reads_what_is_there(tmp_path):
    # Two notes, then a declared length 200 bytes past the end.
    body = bytes([0x00, 0x90, 60, 100, 0x30, 0x80, 60, 0x00,
                  0x00, 0x90, 64, 100])
    div, tempo, events = mid2pat.read_midi(_bare_midi(tmp_path, len(body) + 200, body))
    pitches = [p for _, p, on, _ in events if on]
    assert pitches == [60, 64]


def test_an_event_straddling_the_end_does_not_raise(tmp_path):
    # A note-on whose velocity byte is missing entirely.
    body = bytes([0x00, 0x90, 60, 100, 0x00, 0x90, 64])
    div, tempo, events = mid2pat.read_midi(_bare_midi(tmp_path, len(body) + 50, body))
    assert [p for _, p, on, _ in events if on][0] == 60


def test_the_damaged_tune_in_the_manifest_still_converts():
    # The reason the clamp exists. Named rather than generated, because what is
    # being asserted is that this particular file ships.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    dragon = [s for s in songs if s["id"] == "dragon"][0]
    got = mid2pat.convert(dragon["midi"], grid=dragon["grid"],
                          fold=dragon["fold"])
    assert len(got["cells"]) > 0


def test_a_missing_midi_is_refused(tmp_path):
    doc = {"default": "a",
           "songs": [{"id": "a", "name": "A", "midi": "nothing-here.mid"}]}
    p = tmp_path / "songs.json"
    p.write_text(json.dumps(doc), encoding="utf-8")
    with pytest.raises(SystemExit):
        mid2pat.load_manifest(p)


def test_two_songs_may_not_share_an_id(tmp_path):
    src = pathlib.Path(mid2pat.load_manifest(MANIFEST)[0][0]["midi"])
    doc = {"default": "a",
           "songs": [{"id": "a", "name": "A", "midi": src.name},
                     {"id": "a", "name": "B", "midi": src.name}]}
    p = src.parent / "songs-test-dupe.json"
    p.write_text(json.dumps(doc), encoding="utf-8")
    try:
        with pytest.raises(SystemExit):
            mid2pat.load_manifest(p)
    finally:
        p.unlink()


def test_the_default_must_be_one_of_the_songs(tmp_path):
    src = pathlib.Path(mid2pat.load_manifest(MANIFEST)[0][0]["midi"])
    doc = {"default": "nobody",
           "songs": [{"id": "a", "name": "A", "midi": src.name}]}
    p = src.parent / "songs-test-default.json"
    p.write_text(json.dumps(doc), encoding="utf-8")
    try:
        with pytest.raises(SystemExit):
            mid2pat.load_manifest(p)
    finally:
        p.unlink()


def test_every_shipped_tune_fits_the_tracker():
    # The 255 ceiling on both counts, checked against the manifest as it ships
    # rather than against a fixture -- the point is that what is in the build
    # today is playable, and a tune added later without a max_rows is exactly
    # how this stops being true.
    songs, default_index = mid2pat.load_manifest(MANIFEST)
    assert 0 <= default_index < len(songs)
    for s in songs:
        got = mid2pat.convert(s["midi"], grid=s["grid"], speed_arg=0,
                              max_rows=s["max_rows"], no_drums=False,
                              bpm_override=s["bpm"], fold=s["fold"],
                              drum_tab=s["drums_tab"], tab_beats=s["tab_beats"])
        patterns, order = mid2pat.pack_patterns(got["cells"])
        assert 0 < len(patterns) <= 255, s["id"]
        assert 0 < len(order) <= 255, s["id"]
        assert max(order) < len(patterns), s["id"]
        assert got["speed"] > 0, s["id"]


def test_the_cd_build_gets_a_prefix_and_not_a_selection():
    # The CD build carries fewer tunes and reads the same two arrays as the
    # netbin, cut short -- so which tunes it has must be the FIRST n and never a
    # scattered subset, or a song index means one thing in one build and
    # something else in the other. load_manifest sorts for this; the assertion
    # is that the sort happened and that nothing later re-ordered it.
    songs, default_index = mid2pat.load_manifest(MANIFEST)
    flags = [s["cd"] for s in songs]
    assert flags == sorted(flags, reverse=True), \
        "a CD tune sits after a netbin-only one: %s" % [s["id"] for s in songs]
    assert any(flags), "no tune is marked cd, so the CD build has no music"
    assert songs[default_index]["cd"], \
        "the default is not in the CD build's prefix"


def test_a_default_the_cd_build_would_not_carry_is_refused(tmp_path):
    src = pathlib.Path(mid2pat.load_manifest(MANIFEST)[0][0]["midi"])
    doc = {"default": "b",
           "songs": [{"id": "a", "name": "A", "midi": src.name, "cd": True},
                     {"id": "b", "name": "B", "midi": src.name}]}
    p = src.parent / "songs-test-cd.json"
    p.write_text(json.dumps(doc), encoding="utf-8")
    try:
        with pytest.raises(SystemExit):
            mid2pat.load_manifest(p)
    finally:
        p.unlink()


def test_the_track_map_names_only_real_songs():
    # A hand-edit of track_songs.json is expected -- the file says so -- and the
    # way that goes wrong is a typo in a tune id, which would otherwise surface
    # as a build error in generated C.
    songs, default_index = mid2pat.load_manifest(MANIFEST)
    ids = [s["id"] for s in songs]
    lo, hi, table = mid2pat.load_track_map(MANIFEST, ids, default_index)
    assert lo < hi
    assert len(table) == hi - lo + 1
    for index in table:
        assert 0 <= index < len(songs)
