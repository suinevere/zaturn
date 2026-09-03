#!/usr/bin/env python3
"""The shipped tables must not declare more floors than the reader can hold.

MAP_ATLAS_PAGE_MAX sizes map_atlas.c's cached bounding boxes, and map_atlas_bind
clamps a table that declares more -- folding every floor past the ceiling onto
the top one. Nothing fails to compile when that happens and nothing says so at
run time: the map simply pages to the ceiling and stops, with several storeys
piled on the last page.

That is not hypothetical. A floor used to be a whole drawn sheet and no
publisher printed more than four, so eight was ample. A floor is now one
vertical step of the story's own routes inside one sheet, which took Stationfall
to twelve and The Lurking Horror to ten, and the ceiling was still eight -- the
owner tested and found the map stopped paging.

Also checks the trailing count in MAP_ATLAS_STORIES against the floors the cells
actually carry. That number is what map_atlas_bind reads; a table whose cells
span ten floors while its story entry says two would page to two and hide the
rest, and the two are written by different lines of the generator.
"""
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INC = ROOT / "saturn" / "src" / "engine" / "map_atlas_data.inc"
HDR = ROOT / "saturn" / "src" / "engine" / "map_atlas.h"


def ceiling():
    m = re.search(r"^#define\s+MAP_ATLAS_PAGE_MAX\s+(\d+)\s*$",
                  HDR.read_text(encoding="utf-8"), re.M)
    assert m, "map_atlas.h defines no MAP_ATLAS_PAGE_MAX"
    return int(m.group(1))


def tables():
    """{stem: (floors the cells use, floors the story entry declares)}."""
    text = INC.read_text(encoding="utf-8")
    used = {}
    for m in re.finditer(r"MAP_ATLAS_(\w+)\[\] = \{(.*?)\n\};", text, re.S):
        pages = {int(c.group(1))
                 for c in re.finditer(r"\{\s*\d+,\s*(\d+),", m.group(2))}
        if pages:
            used[m.group(1)] = pages

    declared = {}
    for m in re.finditer(
            r"\{\s*\d+u,\s*\"[^\"]+\",\s*MAP_ATLAS_(\w+),\s*"
            r"\(unsigned short\)[^,]*,\s*[^,]*,\s*(\d+)\s*\}", text, re.S):
        declared[m.group(1)] = int(m.group(2))
    if not declared:                     # the count sits on its own line
        for m in re.finditer(
                r"MAP_ATLAS_(\w+),\s*\n\s*\(unsigned short\).*?\n.*?\n\s*(\d+)\s*\},",
                text):
            declared[m.group(1)] = int(m.group(2))
    return used, declared


def test_no_table_declares_more_floors_than_the_reader_holds():
    used, _ = tables()
    cap = ceiling()
    over = {k: len(v) for k, v in used.items() if len(v) > cap}
    assert not over, (
        f"MAP_ATLAS_PAGE_MAX is {cap} and these need more: {over}. Raise it, or "
        "every floor past it is folded onto the top one and the map stops "
        "paging there.")


def test_every_story_entry_declares_the_floors_its_cells_use():
    used, declared = tables()
    assert declared, "no MAP_ATLAS_STORIES entries parsed"
    for stem, pages in sorted(used.items()):
        assert stem in declared, f"{stem} has cells but no story entry"
        assert declared[stem] == len(pages), (
            f"{stem} cells span {len(pages)} floors but its story entry says "
            f"{declared[stem]} -- map_atlas_bind reads the entry")


def test_floors_are_numbered_densely_from_zero():
    """A hole would offer the player a floor with nothing on it."""
    used, _ = tables()
    for stem, pages in sorted(used.items()):
        assert pages == set(range(len(pages))), \
            f"{stem} floors are not dense: {sorted(pages)}"


@pytest.mark.parametrize("stem,least", [("LURKING", 7), ("STATFALL", 7)])
def test_the_deep_games_really_did_gain_floors(stem, least):
    """The owner's report was that The Lurking Horror has seven-odd levels and
    the map offered two. Pinned so a regeneration that quietly went back to
    paging by drawn sheet fails here rather than on a console."""
    used, _ = tables()
    assert stem in used, f"{stem} has no table"
    assert len(used[stem]) >= least, (
        f"{stem} is down to {len(used[stem])} floors; a floor should be one "
        "vertical step of the story's routes, not a drawn sheet")


if __name__ == "__main__":
    used, declared = tables()
    print("MAP_ATLAS_PAGE_MAX =", ceiling())
    for stem in sorted(used):
        print("%-10s %2d floors (entry says %s)"
              % (stem, len(used[stem]), declared.get(stem)))
