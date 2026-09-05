"""The drum tablature reader.

    python -m pytest saturn/tests/test_drum_tab.py -q

The reader returns one kind-or-None per thirty-second row, which is the grid
the tracker runs on, so a beat is eight rows however it was written.

Pinned because the parsing has three traps that all bit during the session that
wrote it: a triplet is three of the same letter, so a beat ending "hh" beside
one starting "h" reads as a triplet unless the beat boundaries are honoured;
the accumulator and the per-beat list are easy to give the same name, which
silently returns only the last bar; and the tune is in 3/4, so a bar is three
beats and not four.
"""
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "assets"))
import mid2pat


def _tab(tmp_path, text, beats=4):
    p = tmp_path / "drums.tab"
    p.write_text(text, encoding="utf-8")
    return mid2pat.read_drum_tab(p, beats)


def test_a_beat_is_eight_rows_however_it_is_written(tmp_path):
    coarse = _tab(tmp_path, "h.h./..../..../....\n")
    fine = _tab(tmp_path, "h......./......../......../........\n")
    assert len(coarse) == 32
    assert len(fine) == 32
    assert coarse[0] == "hat" and coarse[4] == "hat"
    assert fine[0] == "hat" and fine[4] is None


def test_a_three_four_bar_is_three_beats(tmp_path):
    slots = _tab(tmp_path, "h.h./h.h./h.h.\n", beats=3)
    assert len(slots) == 24


def test_a_bar_with_the_wrong_number_of_beats_is_refused(tmp_path):
    with pytest.raises(SystemExit):
        _tab(tmp_path, "h.h./h.h./h.h./h.h.\n", beats=3)


def test_a_repeat_multiplies_the_bar(tmp_path):
    one = _tab(tmp_path, "h.h./h.h./h.h.\n", beats=3)
    three = _tab(tmp_path, "h.h./h.h./h.h. x3\n", beats=3)
    assert three == one * 3


def test_every_bar_is_kept_not_only_the_last(tmp_path):
    """The accumulator and the per-beat list must not share a name."""
    slots = _tab(tmp_path, "h.h./h.h./h.h.\ns.s./s.s./s.s.\n", beats=3)
    assert len(slots) == 48
    assert slots[0] == "hat"
    assert slots[24] == "snare"


def test_three_of_a_letter_is_a_triplet_on_consecutive_rows(tmp_path):
    slots = _tab(tmp_path, "sss.../..../....\n", beats=3)
    assert [i for i, k in enumerate(slots) if k] == [0, 1, 2]


def test_a_beat_ending_in_two_is_not_a_triplet_with_the_next(tmp_path):
    """A beat ending "hh" beside one starting "h" is not a triplet."""
    slots = _tab(tmp_path, "h.hh/h.../....\n", beats=3)
    assert [i for i, k in enumerate(slots) if k] == [0, 4, 6, 8]


def test_a_beat_written_out_at_thirty_seconds_is_literal(tmp_path):
    slots = _tab(tmp_path, "hss.h.../......../........\n", beats=3)
    assert [i for i, k in enumerate(slots) if k] == [0, 1, 2, 4]
    assert slots[1] == "snare"


def test_a_short_beat_is_refused(tmp_path):
    with pytest.raises(SystemExit):
        _tab(tmp_path, "h.h/h.h./h.h.\n", beats=3)


def test_comments_and_blank_lines_are_ignored(tmp_path):
    slots = _tab(tmp_path, "# a comment\n\nh.h./h.h./h.h.   # trailing\n", beats=3)
    assert len(slots) == 24


def test_the_hits_a_tab_produces_land_on_their_own_rows(tmp_path):
    slots = _tab(tmp_path, "h.h./..../....\n", beats=3)
    hits = mid2pat.tab_hits(slots, 24, 32)
    assert [i for i, h in enumerate(hits) if h] == [0, 4]
