#!/usr/bin/env python3
"""Hold the shared room-presentation pool that game_presentation.inc emits.

Every game's per-room table is a byte per object naming a slot in one pool of
distinct records, rather than a record per room. Thirty-one games' worth of
rooms draw their pictures, tracks and effect banks from one supply, so 7,936
per-room entries say fewer than two hundred distinct things; spelling each one
out cost thirty-one kilobytes of .rodata against eight.

That is not tidiness. __heap_start follows the program image, so every byte of
table is a byte the largest story cannot have, and this table growing past what
the heap could spare is what stopped The Lurking Horror loading --
saturn/tests/test_hwram_budget.py holds the other end of that.

Three things have to stay true or the indirection is wrong rather than merely
large:

  - slot 0 is the unauthored record, because pres_of_room still decides
    "authored" by testing image against zero;
  - no index reaches past the pool, since an index is a byte and a wrap would
    quietly hand a room another room's picture and track;
  - the pool stays inside a byte, which is the generator's own check and is
    asserted here too so a regenerated table cannot arrive broken.

Run as tests: pytest saturn/tests/test_presentation_pool.py
"""
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"


def inc_text():
    if not INC.is_file():
        pytest.skip("no game_presentation.inc -- run tools/gen_presentation.py")
    return INC.read_text(encoding="utf-8", errors="replace")


def pool(text):
    m = re.search(r"static const Presentation PRES_POOL\[PRES_POOL_N\]\s*=\s*\{(.*?)\n\};",
                  text, re.S)
    assert m, "PRES_POOL is not in the table -- its shape changed"
    return [tuple(int(v) for v in rec)
            for rec in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", m.group(1))]


def games(text):
    found = re.findall(
        r"static const unsigned char (GAME_PRES_\w+)\[256\]\s*=\s*\{(.*?)\n\};",
        text, re.S)
    assert found, "no per-game index tables -- their shape changed"
    return {name: [int(v) for v in re.findall(r"\d+", body)] for name, body in found}


def test_the_pool_count_matches_the_define():
    text = inc_text()
    m = re.search(r"#define PRES_POOL_N (\d+)", text)
    assert m, "PRES_POOL_N is not defined"
    assert len(pool(text)) == int(m.group(1))


def test_slot_zero_is_the_unauthored_record():
    """pres_of_room reads a room as unauthored by testing image against zero, so
    an index of zero has to land on a record that answers that way."""
    assert pool(inc_text())[0] == (0, 0, 0)


def test_the_pool_fits_a_byte():
    """An index is one byte per room. Past 256 the generator refuses; this says
    so from the far side, against the table that actually shipped."""
    assert len(pool(inc_text())) <= 256


def test_every_index_names_a_record():
    text = inc_text()
    n = len(pool(text))
    for name, idx in games(text).items():
        assert len(idx) == 256, f"{name} has {len(idx)} entries, not 256"
        worst = max(idx)
        assert worst < n, (
            f"{name} names slot {worst} of a {n}-record pool -- a room would "
            "be handed another room's picture and track")


def test_the_pool_holds_no_duplicates():
    """Two slots with the same record means the pooling missed one, which is
    dead .rodata in the one table that cannot afford any."""
    p = pool(inc_text())
    assert len(set(p)) == len(p), "PRES_POOL repeats a record"
