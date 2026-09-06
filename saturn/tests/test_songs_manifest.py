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

    # The accent lifts individual ornament cells above the voice's own level on
    # purpose, so a lane may emit its level and that level plus the accent, and
    # nothing else.
    got = mid2pat.convert_song(lake)
    steps = (lake["accent"] or {}).get("steps", 0)
    seen = [set() for _ in range(mid2pat.CHANNELS)]
    for row in got["cells"]:
        for lane, (note, wv) in enumerate(row):
            if note >= 2:
                seen[lane].add(wv & 0x0F)
    for lane, levels in enumerate(seen):
        if not levels:
            continue
        base = lake["levels"][lane]
        allowed = {base, min(7, base + steps)} if steps else {base}
        assert levels <= allowed, (
            "lane %d emits levels %s and its override says %d with an accent "
            "of %d" % (lane, sorted(levels), base, steps))

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


def test_legato_closes_only_the_gaps_too_short_to_be_rests():
    # sglake.mid lifts every bass note a row or two before the next, which is
    # how the part was played in and not a rest anybody wrote: 143 gaps on that
    # lane, all of them one or two rows. shadow7.mid is the same piece sequenced
    # by someone else, its bass has none, and that is what says the gaps belong
    # to the sequence. With a release that fades over about 90 ms each one is a
    # dip and a re-attack, eight times a second under the melody.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    lake = [s for s in songs if s["id"] == "lake"][0]
    assert lake["legato"] > 0, "lake's legato has been lost"

    plain = mid2pat.convert_song(dict(lake, legato=0))
    got = mid2pat.convert_song(lake)
    assert len(plain["cells"]) == len(got["cells"])

    # Nothing but a key-off may have changed, and only into "no change".
    for r, (a, b) in enumerate(zip(plain["cells"], got["cells"])):
        for lane in range(mid2pat.CHANNELS):
            if a[lane] == b[lane]:
                continue
            assert a[lane][0] == 1 and b[lane] == (0, 0), (
                "row %d lane %d went from %s to %s and legato may only drop a "
                "key-off" % (r, lane, a[lane], b[lane]))

    # No silence of legato rows or fewer survives on a tonal lane.
    for lane in range(mid2pat.CHANNELS - 1):
        off = None
        for r, row in enumerate(got["cells"]):
            note = row[lane][0]
            if note == 1:
                off = r
            elif note >= 2:
                if off is not None:
                    assert r - off > lake["legato"], (
                        "lane %d is silent for %d rows from row %d, which is "
                        "short enough to be an artefact of the sequence"
                        % (lane, r - off, off))
                off = None

    # A rest long enough to be one still is.
    kept = sum(1 for row in got["cells"] for lane in range(mid2pat.CHANNELS)
               if row[lane][0] == 1)
    assert kept > 0, "legato removed every key-off, including the real rests"


def test_a_tune_with_no_legato_is_untouched():
    songs, _ = mid2pat.load_manifest(MANIFEST)
    for record in songs:
        if record["legato"]:
            continue
        assert (mid2pat.convert_song(record)["cells"]
                == mid2pat.convert_song(dict(record, legato=0))["cells"]), \
            record["id"]


def test_accent_lifts_the_ornaments_and_holds_the_notes_they_decorate():
    # "what your doing at 9s and three times in next bar, 5 times in last bar
    # ... Make them more pronounced or louder", then "let off not early so
    # three in between".
    #
    # Those are the sequence's own ornaments -- eight of them in lake, each a
    # note a couple of rows after the last and a semitone or two above it -- and
    # every one is on the harmony voice, which the level table puts at the
    # bottom of the mix. A DISDL step is carried per cell, so one note can be
    # lifted without moving the voice it belongs to, and a note can be pushed a
    # row later without moving anything else.
    #
    # What the pass may NOT do is change which notes are played. Widening the
    # narrow figures to match the wide ones was tried and "sounded offkey": six
    # of lake's eight are a semitone and two are a tone, and a semitone step is
    # a scale degree rather than a magnitude.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    lake = [s for s in songs if s["id"] == "lake"][0]
    spec = lake["accent"]
    assert spec, "lake's accent has been lost"

    plain = mid2pat.convert_song(dict(lake, accent=None))
    got = mid2pat.convert_song(lake)
    assert len(plain["cells"]) == len(got["cells"])

    def played(song, lane):
        return [row[lane][0] for row in song["cells"] if row[lane][0] >= 2]

    for lane in range(mid2pat.CHANNELS):
        assert played(plain, lane) == played(got, lane), (
            "lane %d plays different notes with the accent on; it may change "
            "when and how loudly, never what" % lane)

    # skip leaves the first few at the voice's own level: nothing in the cells
    # separates lake's early ornaments from its late ones -- same bass, same
    # drum, the same level on every voice at all eight -- so the dynamic is by
    # ear, "too loud for first 5, last 4 right volume", and what is pinned is
    # that it lands on the right ones and not that it was derived.
    skip = spec.get("skip", 0)
    figures = 0
    for lane in range(mid2pat.CHANNELS - 1):
        was = [r for r, row in enumerate(plain["cells"]) if row[lane][0] >= 2]
        now = [r for r, row in enumerate(got["cells"]) if row[lane][0] >= 2]
        assert len(was) == len(now)
        for k, (a, b) in enumerate(zip(was, now)):
            fast = k and was[k] - was[k - 1] <= spec["within"]
            if not fast:
                assert a == b, (
                    "lane %d moved a note at row %d that is not a fast figure"
                    % (lane, a))
                assert plain["cells"][a][lane] == got["cells"][b][lane], (
                    "lane %d changed a note at row %d that is not a fast "
                    "figure" % (lane, a))
                continue
            figures += 1
            want = spec.get("steps", 0) if figures > skip else 0
            if spec.get("hold"):
                assert b - now[k - 1] == spec["hold"], (
                    "lane %d has a fast figure %d rows after its note and the "
                    "hold is %d" % (lane, b - now[k - 1], spec["hold"]))
                assert b >= a, "a fast figure was pulled earlier, not later"
            lifted = ((got["cells"][b][lane][1] & 0x0F)
                      - (plain["cells"][a][lane][1] & 0x0F))
            assert lifted == want, (
                "figure %d on lane %d lifted %d steps and should have lifted "
                "%d" % (figures, lane, lifted, want))
    assert figures, "no fast figure in lake for the accent to reach"
    assert figures > skip, (
        "skip is %d and there are only %d figures, so nothing is ever accented"
        % (skip, figures))


def test_a_tune_with_no_accent_is_untouched():
    songs, _ = mid2pat.load_manifest(MANIFEST)
    for record in songs:
        if record["accent"]:
            continue
        assert (mid2pat.convert_song(record)["cells"]
                == mid2pat.convert_song(dict(record, accent=None))["cells"]), \
            record["id"]
