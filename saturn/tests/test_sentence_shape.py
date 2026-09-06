#!/usr/bin/env python3
"""Host oracle for the v3 verb grammar table, and the cross-check against
sentence_shape.c's decode of the same bytes.

A v3 story's verb grammar lives at a word table indexed from the static-memory
base by (255 - verb dictionary id). The first byte at that address is the row
count; each row is 8 bytes, of which the first five carry the sentence shape:

    byte 0  number of objects the line takes (0, 1 or 2)
    byte 1  dictionary id of the preposition before object 1, 0 for none
    byte 2  dictionary id of the preposition before object 2, 0 for none
    byte 3  object 1 search/attribute byte
    byte 4  object 2 search/attribute byte

Bytes 1-4 are what typeahead_extract.c already reads, into two flat bags.
Byte 0 -- and the pairing of a preposition with the object it precedes -- is
what this table adds and what the panel's sentence shape is made of.

Reads the shipped stories directly, not a fixture.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"
INPUT_DIR = ROOT / "saturn" / "src" / "input"
DRIVER_SRC = ROOT / "saturn" / "tests" / "dump_shape.c"

FL_VERB = 0x40
FL_PREP = 0x08

# Three v3 stories that do not carry Infocom-format grammar tables:
# ADVENT.Z3 is a 2015 port; MZORKI2.Z3 and MZORKII.Z3 are multizork rebuilds.
# The bytes at their grammar-table address hold something else, so nobj values
# are noise. shape_build will detect and reject these on sight.
NO_INFOCOM_GRAMMAR = {"ADVENT.Z3", "MZORKI2.Z3", "MZORKII.Z3"}


def rd16(raw, addr):
    return (raw[addr] << 8) | raw[addr + 1]


def dict_entries(raw):
    """Every dictionary entry as (text, flags, dict_id).

    Text is decoded through the A0 alphabet only. Shift z-chars (A1/A2) are
    not decoded; the oracle only needs the spelling for counting, not perfect
    character accuracy.
    """
    a0 = "      abcdefghijklmnopqrstuvwxyz"
    p = rd16(raw, 0x08)
    nsep = raw[p]
    p += 1 + nsep
    elen = raw[p]
    p += 1
    count = rd16(raw, p)
    p += 2
    out = []
    for k in range(count):
        off = p + k * elen
        chars = []
        for half in (0, 2):
            x = rd16(raw, off + half)
            chars.append(a0[(x >> 10) & 31])
            chars.append(a0[(x >> 5) & 31])
            chars.append(a0[x & 31])
        out.append(("".join(chars).strip(), raw[off + 4], raw[off + 5]))
    return out


def verb_rows(raw):
    """{verb dict id: [(nobj, prep1, prep2, attr1, attr2), ...]}.

    Applies the same two guards typeahead_extract.c does -- the entry address
    must land inside static memory, and the row count must be 1..12 -- so a
    verb this refuses is a verb the C refuses too.
    """
    base = rd16(raw, 0x0e)
    out = {}
    for _text, flags, wid in dict_entries(raw):
        if not (flags & FL_VERB) or wid in out:
            continue
        a = rd16(raw, base + 2 * (255 - wid))
        if not (base <= a < len(raw)):
            continue
        n = raw[a]
        if not (1 <= n <= 12):
            continue
        rows = []
        for e in range(n):
            r = raw[a + 1 + e * 8: a + 1 + e * 8 + 8]
            if len(r) < 5:
                break
            rows.append((r[0], r[1], r[2], r[3], r[4]))
        out[wid] = rows
    return out


def all_stories():
    return sorted(Z3_DIR.glob("*.Z3"))


def load(path):
    return path.read_bytes()


def test_zork1_shapes_are_the_six_the_spec_names():
    """The design rests on Zork I having exactly six distinct shapes and 76
    rows with a preposition before the first object. If either number moves,
    the story file moved and the design's evidence needs re-reading."""
    raw = load(Z3_DIR / "ZORK1.Z3")
    rows = verb_rows(raw)
    shapes = {(n, 1 if p1 else 0, 1 if p2 else 0) for rs in rows.values() for (n, p1, p2, _a1, _a2) in rs}
    assert shapes == {(0, 0, 0), (1, 0, 0), (1, 1, 0), (2, 0, 0), (2, 0, 1), (2, 1, 1)}
    assert sum(len(rs) for rs in rows.values()) == 246
    assert sum(1 for rs in rows.values() for (n, p1, _p2, _a1, _a2) in rs if n >= 1 and p1) == 76


def test_every_story_decodes_and_stays_within_the_c_limits():
    """No shipped story exceeds the fixed limits sentence_shape.c is built to.

    Two measurements for different purposes:
    - Per-id count (worst_verbs, worst_rows): what the design measured, tied to
      verb dictionary ids where synonyms (e.g., 'take'/'get') share one id.
    - Per-spelling count (worst_spellings, worst_spelling_rows): what the C table
      is actually bounded by, counted per unique verb spelling since the table
      keys entries by spelling, and multiple spellings can map to one id. The
      per-spelling walk deliberately omits the wid dedup that verb_rows applies
      to count each spelling once, matching the C table's entry-per-spelling design.

    Excludes ADVENT.Z3, MZORKI2.Z3, and MZORKII.Z3, which do not carry Infocom-
    format grammar tables and have malformed nobj values. These stories will be
    rejected by shape_build during decode.

    Every object count must be 0, 1 or 2 -- the three the panel knows how to walk.
    """
    worst_verbs = worst_rows = 0
    worst_spellings = worst_spelling_rows = 0

    for path in all_stories():
        raw = load(path)
        if raw[0] != 3:
            continue
        if path.name in NO_INFOCOM_GRAMMAR:
            continue
        rows = verb_rows(raw)

        nrows = sum(len(rs) for rs in rows.values())
        worst_verbs = max(worst_verbs, len(rows))
        worst_rows = max(worst_rows, nrows)

        base = rd16(raw, 0x0e)
        spelling_count = 0
        spelling_row_count = 0
        for text, flags, wid in dict_entries(raw):
            if not (flags & FL_VERB):
                continue
            a = rd16(raw, base + 2 * (255 - wid))
            if not (base <= a < len(raw)):
                continue
            n = raw[a]
            if not (1 <= n <= 12):
                continue
            spelling_count += 1
            spelling_row_count += n

        worst_spellings = max(worst_spellings, spelling_count)
        worst_spelling_rows = max(worst_spelling_rows, spelling_row_count)

        for rs in rows.values():
            for (n, _p1, _p2, _a1, _a2) in rs:
                assert n in (0, 1, 2), f"{path.name}: object count {n}"

    assert worst_verbs <= 184, worst_verbs
    assert worst_rows <= 364, worst_rows

    assert worst_spellings <= 384, worst_spellings
    assert worst_spelling_rows <= 1408, worst_spelling_rows


@pytest.mark.parametrize("name", sorted(NO_INFOCOM_GRAMMAR))
def test_the_non_infocom_stories_are_detectably_malformed(name):
    """The three non-Infocom stories (ADVENT.Z3, MZORKI2.Z3, MZORKII.Z3) have
    grammar-table addresses that point to garbage, not verb grammar rows. Each
    one must exhibit at least one row with nobj > 2 so shape_build can detect
    and reject them. If any file is ever replaced or one of these tests fails,
    the exclusion list has drifted and must be revisited."""
    path = Z3_DIR / name
    raw = load(path)
    if raw[0] != 3:
        pytest.skip(f"{name} is not a v3 story")
    rows = verb_rows(raw)
    has_malformed = any(
        nobj > 2
        for rs in rows.values()
        for (nobj, _p1, _p2, _a1, _a2) in rs
    )
    assert has_malformed, f"{name} did not have any nobj > 2; exclusion list may be stale"


_BINS = {}


def _compile(driver_src, bin_name):
    """Compiles `driver_src` against sentence_shape.c once per session per
    `bin_name`, or skips if there is no compiler.

    Skipping rather than failing is deliberate and matches test_exit_dests.py:
    a machine with no host compiler is not a machine where this decode
    regressed.
    """
    if bin_name in _BINS:
        return _BINS[bin_name]
    cc = shutil.which("gcc") or shutil.which("cc")
    if cc is None:
        pytest.skip("no host compiler")
    out_dir = pathlib.Path(tempfile.gettempdir()) / "zaturn_test_sentence_shape"
    out_dir.mkdir(exist_ok=True)
    exe = out_dir / (bin_name + (".exe" if os.name == "nt" else ""))
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-o", str(exe),
           str(driver_src),
           str(INPUT_DIR / "sentence_shape.c"),
           str(INPUT_DIR / "typeahead.c"),
           str(INPUT_DIR / "typeahead_extract.c")]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    _BINS[bin_name] = exe
    return exe


def shape_binary():
    return _compile(DRIVER_SRC, "dump_shape")


def c_rows(exe, story_path):
    """{verb text: [(nobj, prep1, prep2), ...]} as sentence_shape.c sees it.

    Tab-separated, not whitespace-split: the naive A0-only decode both this
    driver and the oracle use renders some dictionary entries (contractions
    like "'ve") with embedded literal spaces, so a plain .split() would tear
    one field into several. Tab never appears in decoded text."""
    result = subprocess.run([str(exe), str(story_path)], capture_output=True,
                            text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    out = {}
    for line in result.stdout.splitlines():
        text, nobj, p1, p2 = line.split("\t")
        out.setdefault(text, []).append((int(nobj), int(p1), int(p2)))
    return out


@pytest.mark.parametrize("path", [p for p in all_stories() if p.name not in NO_INFOCOM_GRAMMAR],
                         ids=lambda p: p.name)
def test_c_decode_matches_the_oracle(path):
    """Every verb the oracle finds rows for, the C finds the same rows for, in
    the same order. Verbs the oracle skipped are not asserted against: the C
    applies the same guards and skipping is what both are meant to do.

    Excludes NO_INFOCOM_GRAMMAR: those stories' grammar-table bytes are not
    Infocom-format, so the oracle's per-row guard (nobj in 0..2) does not hold
    and shape_build rejects them outright by design. See
    test_the_c_finds_no_rows_at_all_for_the_non_infocom_stories."""
    raw = load(path)
    if raw[0] != 3:
        pytest.skip("not v3")
    exe = shape_binary()
    got = c_rows(exe, path)
    by_id = verb_rows(raw)
    id_to_text = {}
    for text, flags, wid in dict_entries(raw):
        if (flags & FL_VERB) and wid not in id_to_text:
            id_to_text[wid] = text
    for wid, rows in by_id.items():
        text = id_to_text.get(wid)
        if text is None:
            continue
        expect = [(n, p1, p2) for (n, p1, p2, _a1, _a2) in rows]
        assert got.get(text) == expect, f"{path.name}: {text}"


@pytest.mark.parametrize("name", sorted(NO_INFOCOM_GRAMMAR))
def test_the_c_finds_no_rows_at_all_for_the_non_infocom_stories(name):
    """The other half of the malformed-grammar proof: not just that the
    oracle can detect these three stories as malformed (the prior test), but
    that shape_build actually refuses the whole story -- no verb, no rows --
    which is the documented fallback shape_verb_rows callers see.

    Rejection is story-level, not per-verb: a per-verb-only check (skip just
    the offending entry) leaves some entries intact purely by chance -- 37 of
    MZORKI2.Z3's 206 verb entries and 30 of MZORKII.Z3's 180 individually
    satisfy the nobj-in-0..2 guard despite the file not being Infocom grammar
    at all, every one of them an nobj==0 row. shape_build instead scans every
    verb's entry before committing any of them, and abandons the whole build
    if any row anywhere is impossible."""
    path = Z3_DIR / name
    raw = load(path)
    if raw[0] != 3:
        pytest.skip(f"{name} is not a v3 story")
    exe = shape_binary()
    got = c_rows(exe, path)
    assert got == {}, f"{name}: expected no rows, got {sorted(got)}"


def test_shape_next_against_zork1():
    """Builds and runs test_shape_next.c so the C matching rule -- the
    five-state walk, the noun-beats-preposition-beats-end priority, and the
    unresolved-preposition-id fallback -- is exercised by test.bat rather
    than only by hand. Deadline's ZORK1.Z3 sibling supplies the second
    story: `peek` there has one row whose preposition id no dictionary
    entry resolves."""
    exe = _compile(ROOT / "saturn" / "tests" / "test_shape_next.c", "shape_next")
    run = subprocess.run([str(exe), str(Z3_DIR / "ZORK1.Z3"), str(Z3_DIR / "DEADLINE.Z3")],
                         capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "ok" in run.stdout
