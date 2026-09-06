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
    got = mid2pat.convert_song(dragon)
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
        got = mid2pat.convert_song(s)
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


def test_an_octave_correction_moves_only_its_own_lane():
    # The whole point of the field: sglake.mid writes its bass an octave below
    # the NES original -- 42 per cent of our energy sat in an octave the
    # original puts 0.1 per cent in -- and the correction has to lift that one
    # voice without touching the others. Compared cell by cell against the same
    # tune converted without it, because "the bass moved" and "everything moved"
    # sound identical in a band measurement.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    lake = [s for s in songs if s["id"] == "lake"][0]
    assert lake["octaves"][0] == 1, "lake's measured correction has been lost"
    assert any(lake["octaves"]), "lake carries no correction at all"
    with_it = mid2pat.convert_song(lake)
    without = mid2pat.convert_song(dict(lake, octaves=[0, 0, 0, 0]))
    assert len(with_it["cells"]) == len(without["cells"])
    moved = [0] * mid2pat.CHANNELS
    for a, b in zip(with_it["cells"], without["cells"]):
        for lane, ((na, wa), (nb, wb)) in enumerate(zip(a, b)):
            assert wa == wb, "an octave changed a waveform or a level"
            want = 12 * lake["octaves"][lane]
            if na >= 2 and nb >= 2:
                assert na - nb == want, (
                    "lane %d moved %d semitones and its correction is %d"
                    % (lane, na - nb, want))
                moved[lane] += 1
            else:
                assert na == nb, "lane %d gained or lost a note" % lane
    for lane, octaves in enumerate(lake["octaves"]):
        if octaves:
            assert moved[lane] > 0, "lane %d has a correction and no note "                                    "moved" % lane


def test_an_octave_correction_is_refused_when_it_is_not_one():
    # A float, a string or an absurd number in this field would otherwise reach
    # the conversion as a transposition nobody meant.
    src = pathlib.Path(mid2pat.load_manifest(MANIFEST)[0][0]["midi"])
    for bad in (1.5, "1", 9, [0, 0, 0, 0, 0]):
        doc = {"default": "a",
               "songs": [{"id": "a", "name": "A", "midi": src.name, "cd": True,
                          "octaves": bad if isinstance(bad, list) else [bad]}]}
        p = src.parent / "songs-test-octaves.json"
        p.write_text(json.dumps(doc), encoding="utf-8")
        try:
            with pytest.raises(SystemExit):
                mid2pat.load_manifest(p)
        finally:
            p.unlink()


def test_a_level_override_reaches_the_cells_and_stops_there():
    # "flute too loud compared to other notes", measured: lake's lead carried
    # 0.260 of the tune's energy in the 440-880 Hz octave where the NES
    # original carries 0.178, and one DISDL step down puts it on 0.131. The
    # global CH_VOL stays as it is -- taking that step for every tune moves
    # castle-halls from 0.16 to 0.54 -- so what is pinned here is that the
    # override reaches lake and reaches nothing else.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    lake = [s for s in songs if s["id"] == "lake"][0]
    assert lake["levels"] is not None, "lake's measured level override is gone"
    assert lake["levels"][1] == mid2pat.CH_VOL[1] - 1, (
        "lake's lead is no longer one step under the global level")

    got = mid2pat.convert_song(lake)
    seen = [set() for _ in range(mid2pat.CHANNELS)]
    for row in got["cells"]:
        for lane, (note, wv) in enumerate(row):
            if note >= 2:
                seen[lane].add(wv & 0x0F)
    for lane, levels in enumerate(seen):
        if levels:
            assert levels == {lake["levels"][lane]}, (
                "lane %d emits levels %s and its override says %d"
                % (lane, sorted(levels), lake["levels"][lane]))

    for other in songs:
        if other["levels"] is not None:
            continue
        song = mid2pat.convert_song(other)
        for row in song["cells"]:
            for lane, (note, wv) in enumerate(row):
                if note >= 2 and song["ch_wave"][lane] != mid2pat.WAVE_NOISE:
                    assert (wv & 0x0F) == mid2pat.CH_VOL[lane], (
                        "%s has no override and lane %d is not at CH_VOL"
                        % (other["id"], lane))


def test_a_level_that_is_not_a_disdl_value_is_refused():
    src = pathlib.Path(mid2pat.load_manifest(MANIFEST)[0][0]["midi"])
    for bad in (8, -1, 2.5, "6", [0, 0, 0, 0, 0]):
        doc = {"default": "a",
               "songs": [{"id": "a", "name": "A", "midi": src.name, "cd": True,
                          "levels": bad if isinstance(bad, list) else [bad]}]}
        p = src.parent / "songs-test-levels.json"
        p.write_text(json.dumps(doc), encoding="utf-8")
        try:
            with pytest.raises(SystemExit):
                mid2pat.load_manifest(p)
        finally:
            p.unlink()
