#!/usr/bin/env python3
"""The octave fold, which is silent when it is backwards.

--fold-octaves collapses a note doubled at the octave onto one of the synth's
three tonal voices. Which member it keeps is the whole point and is not
observable from the shape of the result: a song folded the wrong way still has
one note per pair, still fits the voices, and still passes every other test --
it just plays an octave away from what the original did. That is exactly the
fault this pass was written to remove, so it is pinned here.

The direction was decided by measurement, not taste. The Shadowgate entryway
theme's fan sequence doubles every bass note an octave below what the NES plays;
a recording of the machine has its lowest tonal voice at G3 (196 Hz) and almost
nothing under 125 Hz, while our render of the unfolded sequence put 60 per cent
of its energy there. Keeping the upper member is what puts the bass back where
the original had it, which is why the shipped tune uses --fold-octaves up.

Run (from the repo root):
    python -m pytest saturn/tests/test_mid2pat_fold.py -q
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "assets"))
import mid2pat


def test_up_keeps_the_upper_member_of_a_pair():
    assert mid2pat.fold_octaves({48, 60}, "up") == {60}


def test_down_keeps_the_lower_member_of_a_pair():
    assert mid2pat.fold_octaves({48, 60}, "down") == {48}


def test_a_whole_stack_collapses_to_one_note_not_two():
    """Three octaves of the same pitch is still one line, however it is folded.

    Discarding pairwise against a set being mutated leaves the middle member
    of a three-high stack behind in one direction and not the other, which is
    a fold that silently does nothing on the tunes that need it most.
    """
    assert mid2pat.fold_octaves({48, 60, 72}, "up") == {72}
    assert mid2pat.fold_octaves({48, 60, 72}, "down") == {48}


def test_a_non_octave_interval_is_left_alone():
    assert mid2pat.fold_octaves({60, 67}, "up") == {60, 67}
    assert mid2pat.fold_octaves({60, 64, 67}, "down") == {60, 64, 67}


def test_off_is_the_identity():
    assert mid2pat.fold_octaves({48, 60, 72}, "off") == {48, 60, 72}


def test_a_bass_under_a_melody_an_octave_up_is_not_a_doubling():
    """The fault this pass had when it was applied to a whole row at once.

    fold_octaves takes one part's notes. Handed a merged row it cannot tell a
    doubled line from two parts that happen to be an octave apart, and the
    Shadowgate theme is exactly that case: a bass pedalling on G3 under a melody
    that sits on G4. Folding the merge deleted the bass wherever they lined up,
    which left the melody as the lowest note in the row and so put it on the bass
    voice -- the tune moving onto the triangle and back several times a bar.
    """
    bass, melody = 55, 67                      # G3 and G4
    assert mid2pat.fold_octaves({bass}, "up") == {bass}
    assert mid2pat.fold_octaves({melody}, "up") == {melody}
    assert mid2pat.fold_octaves({bass, melody}, "up") == {melody},         "merged, the fold does delete the bass -- which is why it is never merged"


def test_the_shipped_tune_loses_its_low_octave_and_keeps_its_line():
    """The measured claim, run against the real sequence rather than a fixture.

    Every bass note in the first statement is doubled exactly an octave down;
    folding up must remove the lower copy of each without dropping any pitch
    class, because the line itself is unchanged -- only its register is.
    """
    rows = _shipped_rows()
    before = {p for r in rows for ps in r.values() for p in ps}
    after = {p for r in rows for c, ps in r.items()
             for p in mid2pat.fold_octaves(ps, "up")}

    assert min(before) == 33, "A1: the sequence's own lowest note has moved"
    assert min(after) == 45, "A2: the fold did not lift the bass an octave"
    assert {p % 12 for p in after} == {p % 12 for p in before}, "a part was lost, not folded"


def test_each_voice_follows_one_part_for_the_whole_tune():
    """Why the reduction is by part and not by pitch order.

    The sequence's second pulse line is the first delayed by three sixteenths,
    so the two cross constantly. Taking the higher of them at each row -- which
    is what the pitch-order rule does -- swapped the lead between the melody and
    its own echo on 45 of the 192 rows, heard as the lead jumping about. Pinned
    as the mapping itself: bass on the lowest part, lead on the melody, harmony
    on the echo.
    """
    rows = [{c: mid2pat.fold_octaves(ps, "up") for c, ps in r.items()}
            for r in _shipped_rows()]
    assert mid2pat.plan_parts(rows, 3) == [2, 0, 1]


def test_parts_that_are_not_single_lines_keep_the_pitch_order_rule():
    """One voice per part only works where a part is one note at a time.

    A part sounding a chord is a reduction problem in itself, and a piece with
    more parts than voices has to drop one. Both must fall back, or a piano
    score loses every note but the top of each hand.
    """
    chordy = [{0: {60, 64, 67}, 1: {48}}]
    assert mid2pat.plan_parts(chordy, 3) is None

    crowded = [{0: {72}, 1: {67}, 2: {60}, 3: {55}}]
    assert mid2pat.plan_parts(crowded, 3) is None

    lines = [{0: {72}, 1: {67}, 2: {55}}]
    assert mid2pat.plan_parts(lines, 3) == [2, 0, 1]


def test_a_delayed_repeat_of_a_part_is_recognised_as_the_same_instrument():
    """The lead's own echo must not arrive on a different waveform.

    The sequence answers its pulse line with the same line three sixteenths
    later. Giving the answer a duty of its own made it a second instrument
    shadowing the lead, heard as a chorus on it -- and a recording of the NES
    original measures its answering voice at h2 0.14 / h3 0.31, which is the
    50% square the melody is on, not the 25% pulse (h2 0.71) we had it on.
    """
    rows = _shipped_rows()
    assert mid2pat.echo_delay(rows, 0, 1) == 3
    assert mid2pat.echo_delay(rows, 1, 0) is None, "the echo does not lead"

    song = mid2pat.convert(_music() / "castle-halls.mid", 16, 0, 192,
                           False, 122.3, "up")
    assert song["echoes"] == [(1, 0, 3)]
    lead, answer = song["ch_wave"][1], song["ch_wave"][2]
    assert lead == answer == 3, "the echo is not on the lead's waveform"


def test_two_independent_parts_keep_their_own_instruments():
    """Only a repeat shares a waveform, or every countermelody loses its colour."""
    rows = [{0: {60 + (i % 5)}, 1: {72 - (i % 7)}} for i in range(40)]
    assert mid2pat.echo_delay(rows, 0, 1) is None

    for name in ("ases-death.mid", "revenant-capoeira.mid"):
        song = mid2pat.convert(_music() / name, 16, 0, 0, False, 0.0, "off")
        assert song["echoes"] == [], "%s should have no echo part" % name
        assert song["ch_wave"][1] != song["ch_wave"][2]


def _music():
    return pathlib.Path(__file__).resolve().parents[2] / "tools" / "assets" / "music"


def _shipped_rows():
    """The first statement of the shipped tune, per part, ungrided by nothing."""
    division, _, events = mid2pat.read_midi(_music() / "castle-halls.mid")
    rows, _ = mid2pat.grid_rows(division, [e for e in events if e[0] < 5760], 16, True)
    return rows
