#!/usr/bin/env python3
"""Hold the header guard main.cxx applies to a story it has just read.

main.cxx reads a story off the CD into High Work RAM and, before handing the
buffer to the interpreter, checks two fields of it: the version byte at 0 and
the length word at 0x1A, which a v3 image counts in words. The guard exists
because the size the CD layer reports can come back garbage on a first access --
saturn_read_story_prefix says so, and the read is inside a three-hundred-attempt
retry loop for exactly that reason -- and a garbage size that happens to land
inside the plausible range buys a complete, successful read of something that is
not the file that was asked for. That buffer used to reach initStory, which
halted the machine on "only version 3 is supported right now, this is 32" from a
place with no way back.

A guard is only worth having if it passes everything real, so this asserts the
two fields for every story actually shipped. Anything that fails here would be
refused on the hardware and would spend all three hundred attempts before saying
so.

Run as tests: pytest saturn/tests/test_story_header.py
"""
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"


def stories():
    if not Z3_DIR.is_dir():
        return []
    return sorted(Z3_DIR.glob("*.Z3"))


@pytest.mark.parametrize("path", stories(), ids=lambda p: p.name)
def test_every_shipped_story_passes_the_load_guard(path):
    data = path.read_bytes()
    assert len(data) > 0x40, "%s is too short to hold a header" % path.name
    assert data[0] == 3, "%s reports version %d" % (path.name, data[0])
    hdr = ((data[0x1a] << 8) | data[0x1b]) * 2
    assert hdr > 0, "%s declares a zero length" % path.name
    assert hdr <= len(data), (
        "%s declares %d bytes of story in a %d byte file"
        % (path.name, hdr, len(data)))


def test_there_is_a_catalogue_to_guard():
    """The parametrisation above vanishes silently on a tree with no Z3 folder,
    which would leave this file passing while proving nothing."""
    assert stories(), "no .Z3 stories found in %s" % Z3_DIR
