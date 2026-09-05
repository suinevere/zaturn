#!/usr/bin/env python3
"""The percussion table's numbers, which live in two files that cannot see each
other.

`tools/assets/genwaves.py` generates the table and `saturn/src/sound/scsp.h`
addresses it, and each holds its own copy of the length, the run and the stride.
Drift between them is silent in the worst way: the C would read past the end of
the generated array into whatever the linker put next, or the start address
would stop advancing and every drum hit would go back to being identical, and
neither shows up as a failure anywhere -- the tune still plays.

Also pins the shift register's warm-up, because that fault was inaudible as a
fault and only showed up as "the hi-hat sounds pitchy".

Run (from the repo root):
    python -m pytest saturn/tests/test_noise_table.py -q
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "assets"))
import genwaves


def _scsp_h_defines():
    text = (pathlib.Path(__file__).resolve().parents[1]
            / "src" / "sound" / "scsp.h").read_text(encoding="utf-8")
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^#define\s+(SCSP_\w+)\s+(\d+)\s*$", text, re.M)}


def test_the_engine_and_the_generator_agree_on_the_table():
    d = _scsp_h_defines()
    assert d["SCSP_NOISE_LEN"] == genwaves.NOISE_LEN
    assert d["SCSP_NOISE_RUN"] == genwaves.NOISE_RUN
    assert d["SCSP_NOISE_STRIDE"] == genwaves.NOISE_STRIDE


def test_the_start_can_always_move_and_still_fit_a_hit():
    d = _scsp_h_defines()
    assert d["SCSP_NOISE_RUN"] < d["SCSP_NOISE_LEN"], "no room to move the start at all"
    positions = 1 + (d["SCSP_NOISE_LEN"] - d["SCSP_NOISE_RUN"]) // d["SCSP_NOISE_STRIDE"]
    assert positions >= 8, "only %d distinct hits before it repeats" % positions
    assert positions % 2 == 1, (
        "%d positions divides the sixteen-row drum pattern, so a slice would "
        "land on the same beat every bar" % positions)


def test_the_shift_register_is_run_past_its_biased_opening():
    """The fault this pins, which was heard before it was measured.

    The register is seeded with 1, a corner of its state space, and its first
    few hundred outputs are lopsided. Every hit read that same opening, so every
    hit carried the same DC step -- a thump with a pitch, not a noise burst. One
    hit's worth of the old table averaged +39.6 of a possible 100.
    """
    table = genwaves.build_noise()
    hit = genwaves.NOISE_RUN // 2          # what a hit actually reaches
    opening = sum(table[:hit]) / float(hit)
    assert abs(opening) < 12, "the table opens on a DC step of %+.1f" % opening

    worst = max((abs(sum(table[i:i + hit]) / float(hit))
                 for i in range(0, len(table) - hit, 64)))
    assert worst < 15, "some hit starts on a DC step of %+.1f" % worst


def test_the_table_is_two_samples_per_shift_register_bit():
    """Runs of odd length mean the oversampling has slipped, which moves every
    rate the drum can be keyed at without changing anything visible."""
    table = genwaves.build_noise()
    assert len(table) == genwaves.NOISE_LEN
    assert set(table) == {100, -100}
    runs, current = [], 1
    for a, b in zip(table, table[1:]):
        if a == b:
            current += 1
        else:
            runs.append(current)
            current = 1
    runs.append(current)
    assert all(r % genwaves.NOISE_OVERSAMPLE == 0 for r in runs)
