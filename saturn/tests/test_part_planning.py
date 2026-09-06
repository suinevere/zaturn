"""Which voice plays which line, and what it costs to get that wrong.

    python -m pytest saturn/tests/test_part_planning.py -q

The owner listened to `lake` and reported three things: it needs rounding, it
seems a harsh square, and one high note is getting squashed. All three are one
fault.

plan_parts follows one source part per voice, and gives up entirely -- returning
None -- the moment any part sounds two notes at once. The fallback then re-picks
which voice plays which note on every row, by pitch order, which is the failure
plan_parts' own docstring describes. Seven of the twelve tunes were on that
fallback, and they are exactly the seven that disagree with a recording of their
own NES original.

In `lake` the fault has a number. Its MIDI channel 0 carries a melody AND a bass
two to three octaves apart on one channel, so the row's lowest note -- which is
what the fallback sends to the triangle -- is the bass when the bass is playing
and the melody when it is not. The melody reached 1228 Hz on the triangle, and
the triangle is a 32-step staircase held in a 256-sample table: at that pitch it
is read 7.13 samples at a time, which leaves 1.1 samples per stair. There is no
staircase left. That is the squashing, and the harshness is the aliasing that
replaces it.

Two things are pinned here. A part carrying two lines a wide interval apart is
separated so plan_parts can follow each, which is checked against `shadow7` --
the same piece sequenced by someone else, whose channels were separate already
and whose bass lands on exactly the same eleven semitones. And every tune whose
parts DO plan is held below the pitch at which the staircase stops being one,
with the tunes that still cannot plan listed by name rather than skipped, so
the list shrinks visibly as they are fixed.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "assets"))
import mid2pat

MANIFEST = ROOT / "tools" / "assets" / "music" / "songs.json"

# The tunes whose parts still cannot be followed one to a voice, and why. Named
# rather than computed: each is a fault of its own and this list is the worklist.
#   court, overworld -- more source parts than the synth has voices
#   halls, shadow8   -- a part sounding a chord, members a third to a tenth apart
UNPLANNED = {"court", "overworld", "halls", "shadow8"}

# Where the NES triangle stops being a staircase. The table is 256 samples
# holding 32 steps, so a step of 2.0 table samples an output sample leaves four
# samples a stair and anything faster smears them together. Index 36 is that
# step. Every tune that plans today tops out at 30 or below; every tune that
# does not reaches 52 to 64.
TRIANGLE_MAX_INDEX = 36


def _planned(record):
    """The parts a tune is reduced from, and the lane plan for them -- run the
    way convert does, since the fold has to happen before the parts are judged.
    A raw sequence doubles its bass at the octave and would look like two lines
    on one channel to anything measuring before that is collapsed."""
    division, tempo, events = mid2pat.read_midi(record["midi"])
    has_drums = (bool(record["drums_tab"])
                 or any(e[3] == mid2pat.DRUM_MIDI_CHANNEL and e[2]
                        for e in events))
    slots = mid2pat.CHANNELS - 1 if has_drums else mid2pat.CHANNELS
    parts, _ = mid2pat.grid_rows(division, events, record["grid"],
                                 has_drums and not record["drums_tab"])
    parts = [{c: mid2pat.fold_octaves(p, record["fold"])
              for c, p in row.items()} for row in parts]
    if record["max_rows"]:
        parts = parts[:record["max_rows"]]
    return mid2pat.plan_lanes(parts, slots)[1]


def test_exactly_the_named_tunes_cannot_be_planned():
    songs, _ = mid2pat.load_manifest(MANIFEST)
    unplanned = {s["id"] for s in songs if _planned(s) is None}
    assert unplanned == UNPLANNED, (
        "the set of tunes reduced by pitch order has moved. A tune that has "
        "left it is fixed and belongs out of UNPLANNED; a tune that has joined "
        "it is now hopping between voices row by row and will sound harsh.")


def test_a_planned_tune_keeps_its_triangle_inside_the_staircase():
    # The measurable form of "one high note getting squashed".
    songs, _ = mid2pat.load_manifest(MANIFEST)
    for record in songs:
        if record["id"] in UNPLANNED:
            continue
        song = mid2pat.convert_song(record)
        for lane, wave in enumerate(song["ch_wave"]):
            if wave != mid2pat.WAVE_TRIANGLE:
                continue
            top = max((row[lane][0] - 2 for row in song["cells"]
                       if row[lane][0] >= 2), default=0)
            assert top <= TRIANGLE_MAX_INDEX, (
                "%s puts its triangle at index %d, which reads the 32-step "
                "table faster than four samples a stair -- the staircase is "
                "gone and what is left is aliasing" % (record["id"], top))


def test_lake_and_shadow7_agree_about_the_bass_they_share():
    # The two are the same piece sequenced twice. shadow7's channels were
    # separate to begin with and it has always planned; lake's melody and bass
    # share a channel. If the separation is right, both put the same bass line
    # on the triangle -- which is a claim nothing about lake alone can make.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    got = {}
    for sid in ("lake", "shadow7"):
        record = [s for s in songs if s["id"] == sid][0]
        song = mid2pat.convert_song(record)
        lane = song["ch_wave"].index(mid2pat.WAVE_TRIANGLE)
        notes = [row[lane][0] - 2 for row in song["cells"] if row[lane][0] >= 2]
        got[sid] = (min(notes), max(notes))
    assert got["lake"] == got["shadow7"], (
        "lake's bass is on %s and shadow7's on %s; they are the same piece, so "
        "either the separation or lake's octave correction is wrong"
        % (got["lake"], got["shadow7"]))


def test_separating_a_part_leaves_a_tune_that_did_not_need_it_alone():
    # The repair runs only where the plan already failed, and castle-halls is
    # the tune every voicing constant was swept against. Run unguarded it splits
    # castle-halls too -- on the octave doubling in its bass, before the fold
    # removes it -- and moves that tune's triangle from eleven semitones to
    # forty-six.
    songs, _ = mid2pat.load_manifest(MANIFEST)
    for sid in ("castle-halls", "title", "corridor", "dragon", "shadow7"):
        record = [s for s in songs if s["id"] == sid][0]
        division, tempo, events = mid2pat.read_midi(record["midi"])
        has_drums = (bool(record["drums_tab"])
                     or any(e[3] == mid2pat.DRUM_MIDI_CHANNEL and e[2]
                            for e in events))
        slots = mid2pat.CHANNELS - 1 if has_drums else mid2pat.CHANNELS
        parts, _ = mid2pat.grid_rows(division, events, record["grid"],
                                     has_drums and not record["drums_tab"])
        parts = [{c: mid2pat.fold_octaves(p, record["fold"])
                  for c, p in row.items()} for row in parts]
        if record["max_rows"]:
            parts = parts[:record["max_rows"]]
        after, lanes = mid2pat.plan_lanes(parts, slots)
        assert lanes is not None, sid
        assert after is parts, "%s was separated and did not need to be" % sid
