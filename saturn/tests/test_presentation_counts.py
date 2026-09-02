#!/usr/bin/env python3
"""presentation.h carries a hand-copied set of the generated table's counts, so
a caller can size an array against them without pulling the whole 24 KB table
into its translation unit. Nothing in the compiler can catch them drifting: the
two files only ever meet inside presentation.c, where the .inc lands first and
the header's block is skipped entirely.

What drift looks like is a caller that loops to the header's PRES_FRAME_N while
the table holds more -- generated pictures that are on the disc and are never
reached -- or one that loops past the end of PRES_AREA and reads a stem out of
whatever follows it. Both counts grow every time a picture is added to the
supply, so this is not a one-off check.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"
HDR = ROOT / "saturn" / "src" / "scene" / "presentation.h"
NAMES = ("PRES_FRAME_N", "PRES_AREA_N", "PRES_MAP_BG_N")


def counts(path):
    """Every count the file defines, as {name: value}."""
    text = path.read_text(encoding="utf-8")
    out = {}
    for name in NAMES:
        m = re.search(rf"^#define\s+{name}\s+(\d+)\s*$", text, re.M)
        if m is None:
            raise AssertionError(f"{path.name} defines no {name}")
        out[name] = int(m.group(1))
    return out


def test_the_header_counts_match_the_table():
    inc, hdr = counts(INC), counts(HDR)
    assert inc == hdr, (
        "presentation.h's copied counts have drifted from "
        f"game_presentation.inc's: table {inc}, header {hdr}. Copy the .inc's "
        "values across -- gen_presentation.py writes the table and does not "
        "touch the header.")


def test_the_table_declares_what_it_holds():
    """A count that disagrees with the array it sizes is worse than a wrong
    count: the array is what the runtime indexes and the count is what bounds
    it, so the gap is a read past the end."""
    text = INC.read_text(encoding="utf-8")
    inc = counts(INC)
    for name, array in (("PRES_FRAME_N", "IMAGE_FRAME"),
                        ("PRES_AREA_N", "PRES_AREA"),
                        ("PRES_MAP_BG_N", "PRES_MAP_BG")):
        body = re.search(rf"{array}\s*\[[^\]]*\]\s*=\s*\{{(.*?)\n\}}\s*;",
                         text, re.S)
        assert body is not None, f"{array} is not in the table"
        n = len(re.findall(r"\{[^{}]*\}" if array == "IMAGE_FRAME" else r'"[^"]*"',
                           body.group(1)))
        assert n == inc[name], (
            f"{name} is {inc[name]} but {array} holds {n} entries")
