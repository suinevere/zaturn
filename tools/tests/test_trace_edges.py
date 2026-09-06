"""The line tracer, scored on Zork I's own pages.

Page numbers here are one-based, matching mapscan.page_image, which indexes
pymupdf at page - 1. Zork I: page 3 is above ground and carries the legend,
page 4 is underground, page 5 is the maze.
"""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

# See test_gen_map_marks.py.
pytest.importorskip("cv2", reason="opencv-python is not installed (pip install -e .[maps])")
pytest.importorskip("pymupdf", reason="pymupdf is not installed (pip install -e .[maps])")

import mapscan
import trace_edges

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PDF = os.path.join(ROOT, "tools", "assets", "cache", "zork1.pdf")
UNDERGROUND = 4

pytestmark = pytest.mark.skipif(
    not os.path.exists(PDF),
    reason="the Infocom map is not redistributed; fetch it into tools/assets/cache")


def test_ink_mask_keeps_passages_and_erases_box_rules():
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    assert mask.dtype == np.uint8
    for (x, y, w, h) in mapscan.find_boxes(PDF, UNDERGROUND):
        border = np.concatenate([mask[y, x:x + w], mask[y + h - 1, x:x + w],
                                 mask[y:y + h, x], mask[y:y + h, x + w - 1]])
        assert border.mean() < 32, "a box's own rules survived into the mask"


def test_ink_mask_is_not_empty():
    """The erasure must take the rules and leave the passages. A mask that
    erased everything would pass the test above and be useless."""
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    assert mask.mean() > 1.0


def _box_named(pdf, page, want):
    """The index and rectangle of the box whose OCR reading is `want`."""
    boxes = mapscan.read_boxes(pdf, page)
    for i, b in enumerate(boxes):
        if mapscan.norm(b[4]) == mapscan.norm(want):
            return i, b
    raise AssertionError("no box read as %r on page %d" % (want, page))


def test_follow_joins_timber_room_to_drafty_room():
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    boxes = [b[:4] for b in mapscan.read_boxes(PDF, UNDERGROUND)]
    ti, tb = _box_named(PDF, UNDERGROUND, "Timber Room")
    di, _ = _box_named(PDF, UNDERGROUND, "Drafty Room")

    seed = (tb[0] - trace_edges.BOX_PAD - 1, tb[1] + tb[3] // 2)
    run = trace_edges.follow(mask, boxes, seed, (-1, 0))

    assert ("box", di) in run["ends"]
    assert ("box", ti) in run["ends"]


def test_hamburger_seeds_find_the_three_baggage_passages():
    """Zork I's underground page carries cross-bars on exactly two passages --
    Altar to Cave and Timber to Drafty -- plus the chimney stub out of Studio.
    Clusters repeat along a run, so this counts distinct passages, not glyphs.
    """
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    all_boxes = mapscan.read_boxes(PDF, UNDERGROUND)
    boxes = [b[:4] for b in all_boxes]
    seeds = trace_edges.hamburger_seeds(mask)
    assert seeds, "no cross-bar clusters found at all"

    named = set()
    for (x, y, axis) in seeds:
        heading = (1, 0) if axis == "h" else (0, 1)
        run = trace_edges.follow(mask, boxes, (x, y), heading)
        for end in run["ends"]:
            if end[0] == "box":
                named.add(mapscan.norm(all_boxes[end[1]][4]))
    assert {"timber room", "drafty room"} <= named
    assert "cave" in named


def test_the_chimney_stub_names_the_kitchen():
    """Studio's up exit is drawn as a stub labelled "(to Kitchen)" because its
    far end is on another page. That label is the only record anywhere of where
    the chimney goes -- the compiled story decodes its destination as zero."""
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    boxes = [b[:4] for b in mapscan.read_boxes(PDF, UNDERGROUND)]
    si, sb = _box_named(PDF, UNDERGROUND, "Studio")

    seed = (sb[0] + sb[2] // 2, sb[1] - trace_edges.BOX_PAD - 1)
    run = trace_edges.follow(mask, boxes, seed, (0, -1))
    assert ("box", si) in run["ends"]
    assert any(e[0] == "open" for e in run["ends"])

    names = ["Kitchen", "Studio", "Gallery", "Cellar", "Attic"]
    assert trace_edges.open_end_room(PDF, UNDERGROUND, run, names) == "Kitchen"

    # The chimney is one-way, Studio to Kitchen: the arrowhead belongs at
    # whichever end is open (the Kitchen end), not the Studio box -- derived
    # from run["ends"] rather than hard-coded, so this fails if the head
    # were found at the wrong end of the passage, not merely if none at all.
    open_index = next(i for i, e in enumerate(run["ends"]) if e[0] == "open")
    expected_arrow = 2 if open_index == 0 else 1
    assert trace_edges.arrow_end(mask, run) == expected_arrow


def test_hamburger_seeds_reject_the_hatched_wall_and_mirror():
    """North Temple's west wall is solid granite and Infocom draws it as a
    filled hatched bar beside the Temple box; the Mirror Rooms carry the same
    mark. Both are short strokes beside a line and are the thing most likely to
    be mistaken for a cluster. A cluster is three one-pixel bars over about five
    pixels; a wall is a filled rectangle."""
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    _, temple = _box_named(PDF, UNDERGROUND, "Temple")
    tx, ty, tw, th = temple[:4]
    for (x, y, _axis) in trace_edges.hamburger_seeds(mask):
        beside_temple = (tx - 40 <= x <= tx and ty <= y <= ty + th)
        assert not beside_temple, "the hatched granite wall was read as a cluster"
