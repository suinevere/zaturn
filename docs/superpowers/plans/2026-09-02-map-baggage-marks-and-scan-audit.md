# Baggage-Limit Mark and Map Scan Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw Infocom's fifth and last legend mark -- three cross-bars meaning a narrow passageway with a baggage limit -- from data read off the scanned map, and produce a disagreement report comparing the whole drawn network against the story's exit graph.

**Architecture:** Eight page-reading primitives move out of `tools/gen_map_atlas.py` into a shared `tools/mapscan.py`. A new `tools/trace_edges.py` follows drawn strokes and classifies their symbols. `tools/gen_map_marks.py` reconciles the scan against the exit graph under a single rule -- the drawing may resolve a passage only where every exit on it is `RM_EXIT_MAYBE` -- and emits a six-entry table plus a report. At runtime `map_marks.c` binds that table by release and serial, `map_model.c` applies it at the one chokepoint both the live path and the restore rebind pass through, and `map_edges.c` renders the mark.

**Tech Stack:** Python 3.13 with opencv-python, numpy, pymupdf and rapidocr-onnxruntime (all already installed); C89-style C for the Saturn engine, built by `saturn/compile.bat`; host C test suites built with gcc.

**Spec:** `docs/superpowers/specs/2026-09-02-map-baggage-marks-and-scan-audit-design.md`

## Global Constraints

- **The contribution rule.** The scan may resolve a passage only when *every* exit on it is `RM_EXIT_MAYBE`. If any exit on the pair is `RM_EXIT_OPEN` or `RM_EXIT_BLOCKED`, the graph wins outright and the disagreement is reported, not applied.
- **A baggage run draws solid**, never dashed, even where the graph says `MAYBE`.
- **Cross-bars go on straight cells only**, phased on `(x + y) % 3` in map cell coordinates, except on a stub where the single shaft cell always takes one.
- `MAP_EDGE_BAGGAGE` is `0x4000`. `MAP_EXIT_BAGGAGE` is `8`. Neither value may change; `0x8000` stays spare.
- **Zork I only.** Story stem `ZORK1`, release 88, serial `840726`.
- **The maps are not redistributed.** They live in `tools/assets/cache/`, which is gitignored (`.gitignore:95`). Never commit a PDF.
- **House comment style.** Every file, function and constant gets the `/*---- | name | Description: ... | Author: suinevere | ... ----*/` block used throughout `saturn/src`. No comments inside function bodies. Author of record is `suinevere`.
- **Commit messages are one sentence.** No body, no bullets, no trailers. Never mention Claude, AI, or the session.
- **Never run `make`.** Its `find src/ -name '*.c'` returns nothing under git-bash and the link fails. Build with `saturn/compile.bat`, driven from PowerShell with the `cd` and the build in one command.
- **The acceptance property.** Disabling the cross-bar detector must make the calibration test *fail*. A test that passes against its own removal is worthless.

## File Structure

**Created:**
- `tools/mapscan.py` -- page raster, box finding, OCR, name matching, story exit graph. Moved wholesale from `gen_map_atlas.py`; one responsibility: *what is drawn on this page and which story rooms are they*.
- `tools/zil_exits.py` -- extracts conditional and routine exits from Zork I's ZIL source. The independent oracle.
- `tools/trace_edges.py` -- ink mask, stroke following, cross-bar detection, run classification.
- `tools/gen_map_marks.py` -- reconciliation, `map_marks_data.inc` emission, audit report.
- `tools/tests/test_zil_exits.py`, `tools/tests/test_trace_edges.py`, `tools/tests/test_gen_map_marks.py`
- `saturn/src/engine/map_marks.c`, `saturn/src/engine/map_marks.h`, `saturn/src/engine/map_marks_data.inc`
- `saturn/tests/test_map_marks.c`
- `docs/ZORK1_MAP_SCAN_AUDIT.md`

**Modified:**
- `tools/gen_map_atlas.py` -- imports from `mapscan`; behaviour must not change.
- `tools/gen_dash_tiles.py` -- two cross-bar tiles; `N` rises 142 to 144.
- `saturn/src/video/dash_map.h:107-111` -- two enum entries.
- `saturn/src/engine/map_model.h:178-180` -- `MAP_EXIT_BAGGAGE`.
- `saturn/src/engine/map_model.c` -- `g_bag`, marks applied in `record_exits`.
- `saturn/src/video/map_edges.h`, `saturn/src/video/map_edges.c` -- `MAP_EDGE_BAGGAGE`, solid override, stub deco parameter, tile choice.
- `saturn/src/video/map_view.cxx:491` -- the stub call gains its deco argument.
- `saturn/Makefile:96` -- `map_marks.c` in the netbin's explicit source list.
- `saturn/tests/test_map_edges.c`, `saturn/tests/test_map_model.c`, `saturn/tests/test_dash_tiles.c`

---

# Phase A -- the oracle and the scanner

## Task 1: Extract the shared page-reading primitives

**Files:**
- Create: `tools/mapscan.py`
- Modify: `tools/gen_map_atlas.py:1-435` (remove the moved definitions, add the import)

**Interfaces:**
- Consumes: nothing.
- Produces: `mapscan.page_image(pdf, page) -> np.ndarray`, `mapscan.find_boxes(pdf, page) -> [(x,y,w,h)]`, `mapscan.read_boxes(pdf, page) -> [(x,y,w,h,text)]`, `mapscan.read_labels(pdf, page) -> [(x,y,w,h,text)]`, `mapscan.ocr_engine()`, `mapscan.direction_props(raw) -> {dir_index: prop_num}`, `mapscan.room_graph(path) -> (graph, release, serial)`, `mapscan.match_name(text, names) -> str|None`, `mapscan.page_items(pdf, page, names, cache)`, `mapscan.norm(s)`, `mapscan.base_and_index(text)`, and constants `DPI`, `DIRW`, `FL_DIR`, `PROP_MAX`, `Z3DIR`, `MIN_BOXES`, `MIN_NAMED`, `FUZZ_MIN`, `FUZZ_MARGIN`.

- [ ] **Step 1: Capture the current generated file as the baseline**

The whole guarantee of this task is that the output does not change. Capture it first.

```bash
cp saturn/src/engine/map_atlas_data.inc /tmp/atlas_baseline.inc
sha256sum /tmp/atlas_baseline.inc
```

- [ ] **Step 2: Create `tools/mapscan.py` with the moved definitions**

Move these from `gen_map_atlas.py` **verbatim**, in this order, changing nothing but their location: constants `DPI`, `DIRW`, `FL_DIR`, `PROP_MAX`, `Z3DIR`, `MIN_BOXES`, `MIN_NAMED`, `FUZZ_MIN`, `FUZZ_MARGIN`, `_OCR`; then `ocr_engine`, `direction_props`, `room_graph`, `page_image`, `_boxes_at`, `find_boxes`, `read_boxes`, `read_labels`, `norm`, `base_and_index`, `match_name`, `page_items`.

Their existing docstrings move with them and must not be rewritten -- they record measurements (Starcross's inverted polarity, Moonmist's "riveuay", why reading from the flags byte throws the recovery away) that were expensive to obtain.

The module needs this header at the top:

```python
#!/usr/bin/env python3
"""Reading Infocom's scanned maps: what is drawn on a page, and which of a
story's rooms each drawn thing is.

Split out of tools/gen_map_atlas.py, which laid out and validated room
positions and is now one of two consumers; tools/trace_edges.py is the other,
and follows the lines between the boxes this module finds. The seam is between
recognising what is on the page and deciding what it means: nothing here knows
about lanes, floors or exit agreement.

THE MAPS ARE NOT REDISTRIBUTED. They are Activision's, scanned by the Infocom
Documentation Project (infodoc.plover.net) and reproduced there with permission.
Callers download them into a cache that is not part of the repository.

Needs pymupdf, opencv-python, numpy and rapidocr-onnxruntime.
"""
import difflib
import os
import struct
import sys

import cv2
import numpy as np
import pymupdf

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import zstory
```

- [ ] **Step 3: Point `gen_map_atlas.py` at the new module**

Delete the moved definitions from `gen_map_atlas.py`. Replace its now-unused imports and add:

```python
from mapscan import (DPI, DIRW, FL_DIR, PROP_MAX, Z3DIR, MIN_BOXES, MIN_NAMED,
                     FUZZ_MIN, FUZZ_MARGIN, ocr_engine, direction_props,
                     room_graph, page_image, find_boxes, read_boxes,
                     read_labels, norm, base_and_index, match_name, page_items)
```

Keep in `gen_map_atlas.py`: `MAPS`, `BASE_URL`, `AMBIG_MAX`, `REFINE_ROUNDS`, `ORIENT_MIN`, `PASS_RATE`, `HALF`, `snap`, `agreement`, `assign`, `build_game`, `emit`, `main`. Amend its module docstring so the five-stage description says stages one to three now live in `mapscan.py`.

- [ ] **Step 4: Regenerate and require the output byte-identical**

This downloads twenty-two PDFs and runs OCR over every page. It takes many minutes; run it in the background and wait for it once rather than polling.

```bash
python tools/gen_map_atlas.py --cache tools/assets/cache > /tmp/atlas_new.inc
diff /tmp/atlas_baseline.inc /tmp/atlas_new.inc && echo "IDENTICAL"
```

Expected: `IDENTICAL`, and no output from `diff`.

If it differs, the move was not verbatim. Do not "fix" the baseline -- find what changed.

- [ ] **Step 5: Run the atlas suite**

```bash
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tma \
    saturn/tests/test_map_atlas.c saturn/src/engine/map_atlas.c && /tmp/tma
```

Expected: exits 0.

- [ ] **Step 6: Commit**

```bash
git add tools/mapscan.py tools/gen_map_atlas.py
git commit -m "Split the eight primitives that read a scanned map page -- the raster, the box finder, the OCR reader, the name matcher and the story exit graph -- out of gen_map_atlas.py into mapscan.py, so the line tracer about to be written consumes them rather than duplicating them, with the seam drawn between recognising what is drawn on a page and deciding what it means and the move held to being verbatim by regenerating map_atlas_data.inc and requiring it byte-identical."
```

---

## Task 2: The ZIL oracle

**Files:**
- Create: `tools/zil_exits.py`
- Test: `tools/tests/test_zil_exits.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `zil_exits.conditional_exits(path) -> {flag: [(room, direction, dest)]}` and `zil_exits.routine_exits(path) -> [(room, direction, routine)]`, both sorted, all names the ZIL's uppercase symbols.

This task builds the ground truth **before** anything reads a pixel, so the scanner is measured against something that did not come from the scanner.

- [ ] **Step 1: Write the failing test**

`tools/tests/test_zil_exits.py`:

```python
"""The ZIL oracle: Zork I's conditional and routine exits read from source.

This is the only ground truth on the disc -- Zork I is the one story whose ZIL
is in this repository -- and it is what the scanned cross-bar detector is
scored against. The expected sets below are transcribed from
1dungeon.zil and are asserted here so that a change to the source invalidates
them loudly rather than leaving the calibration silently stale.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import zil_exits

ZIL = os.path.join("cd", "Zork I - The Great Underground Empire (Japan)",
                   "zork1", "1dungeon.zil")


def test_empty_handed_gates_exactly_the_timber_shaft_passage():
    got = zil_exits.conditional_exits(ZIL)["EMPTY-HANDED"]
    assert got == [
        ("LOWER-SHAFT", "EAST", "TIMBER-ROOM"),
        ("LOWER-SHAFT", "OUT", "TIMBER-ROOM"),
        ("TIMBER-ROOM", "WEST", "LOWER-SHAFT"),
    ]


def test_coffin_cure_gates_exactly_the_altar_descent():
    got = zil_exits.conditional_exits(ZIL)["COFFIN-CURE"]
    assert got == [("SOUTH-TEMPLE", "DOWN", "TINY-CAVE")]


def test_deflate_gates_the_white_cliffs_and_is_not_a_baggage_limit():
    got = zil_exits.conditional_exits(ZIL)["DEFLATE"]
    assert got == [
        ("WHITE-CLIFFS-NORTH", "SOUTH", "WHITE-CLIFFS-SOUTH"),
        ("WHITE-CLIFFS-NORTH", "WEST", "DAMP-CAVE"),
        ("WHITE-CLIFFS-SOUTH", "NORTH", "WHITE-CLIFFS-NORTH"),
    ]


def test_the_chimney_is_a_routine_exit_with_no_destination_in_source():
    assert ("STUDIO", "UP", "UP-CHIMNEY-FUNCTION") in zil_exits.routine_exits(ZIL)


def test_a_multi_line_exit_clause_is_read_whole():
    """WHITE-CLIFFS-SOUTH's NORTH exit wraps before its IF, and SOUTH-TEMPLE's
    DOWN wraps twice. A line-at-a-time reader finds neither."""
    conds = zil_exits.conditional_exits(ZIL)
    assert ("WHITE-CLIFFS-SOUTH", "NORTH", "WHITE-CLIFFS-NORTH") in conds["DEFLATE"]
    assert ("SOUTH-TEMPLE", "DOWN", "TINY-CAVE") in conds["COFFIN-CURE"]
```

- [ ] **Step 2: Run it and watch it fail**

```bash
python -m pytest tools/tests/test_zil_exits.py -v
```

Expected: `ModuleNotFoundError: No module named 'zil_exits'`.

- [ ] **Step 3: Write `tools/zil_exits.py`**

```python
#!/usr/bin/env python3
"""Zork I's conditional and routine exits, read from its ZIL source.

This repository carries the ZIL for exactly one story, and that is what makes
Zork I the calibration game: a passage traced off the scanned map can be
scored against the source rather than against the exit graph the scan is meant
to audit. Nothing here is shipped -- it exists to measure the scanner.

Exit clauses wrap. SOUTH-TEMPLE's DOWN spans three lines and WHITE-CLIFFS-
SOUTH's NORTH spans two, so a room's whole block is collapsed to one line
before it is matched; a line-at-a-time reader silently misses both.
"""
import re

DIRS = ("NORTH", "SOUTH", "EAST", "WEST", "NE", "NW", "SE", "SW",
        "UP", "DOWN", "IN", "OUT")

_ROOM = re.compile(r"<ROOM\s+([A-Z0-9-]+)")
_COND = re.compile(r"\(\s*(" + "|".join(DIRS) +
                   r")\s+TO\s+([A-Z0-9-]+)\s+IF\s+([A-Z0-9-]+)")
_PER = re.compile(r"\(\s*(" + "|".join(DIRS) + r")\s+PER\s+([A-Z0-9-]+)")


def _blocks(path):
    """(room symbol, its whole definition collapsed to one line) for each room."""
    with open(path, "r", encoding="latin-1") as f:
        text = f.read()
    out = []
    for chunk in text.split("<ROOM ")[1:]:
        m = _ROOM.match("<ROOM " + chunk)
        if m:
            out.append((m.group(1), " ".join(chunk.split())))
    return out


def conditional_exits(path):
    """{flag: sorted [(room, direction, destination)]} for every IF-gated exit.

    The flag is the interesting key: what a passage is conditional *on* is the
    thing the compiled story never records, and the thing the map draws.
    """
    found = {}
    for room, body in _blocks(path):
        for direction, dest, flag in _COND.findall(body):
            found.setdefault(flag, []).append((room, direction, dest))
    return {k: sorted(v) for k, v in found.items()}


def routine_exits(path):
    """Sorted [(room, direction, routine)] for every PER exit.

    A routine exit's destination is computed, so it exists nowhere in the
    compiled data and the map's own drawing is the only record of where it
    goes.
    """
    out = []
    for room, body in _blocks(path):
        for direction, routine in _PER.findall(body):
            out.append((room, direction, routine))
    return sorted(out)
```

- [ ] **Step 4: Run the tests**

```bash
python -m pytest tools/tests/test_zil_exits.py -v
```

Expected: 5 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/zil_exits.py tools/tests/test_zil_exits.py
git commit -m "Read Zork I's conditional and routine exits straight from the ZIL this repository carries, collapsing each room's block to one line because three of the clauses that matter wrap before their IF and a line-at-a-time reader finds none of them, so that the cross-bar detector about to be written is scored against the source rather than against the exit graph it is meant to audit."
```

---

## Task 3: The ink mask with box rules removed

**Files:**
- Create: `tools/trace_edges.py`
- Test: `tools/tests/test_trace_edges.py`

**Interfaces:**
- Consumes: `mapscan.page_image`, `mapscan.find_boxes`.
- Produces: `trace_edges.ink_mask(pdf, page) -> np.ndarray` -- a `uint8` array, 255 where passage ink is and 0 elsewhere, with every room box's rules erased so a following walk cannot run around a box instead of into it.

- [ ] **Step 1: Write the failing test**

Add to `tools/tests/test_trace_edges.py`:

```python
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
import mapscan
import trace_edges

PDF = os.path.join("tools", "assets", "cache", "zork1.pdf")
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
```

- [ ] **Step 2: Run it and watch it fail**

```bash
python -m pytest tools/tests/test_trace_edges.py -v
```

Expected: `ModuleNotFoundError: No module named 'trace_edges'`.

- [ ] **Step 3: Write `ink_mask`**

Create `tools/trace_edges.py`:

```python
#!/usr/bin/env python3
"""Following the passages Infocom drew between the room boxes, and reading the
symbol each one carries.

mapscan.py finds the boxes and names them. This finds the lines. The two are
separate because a box is an enclosed region and a passage is an open stroke,
and the morphology that isolates one destroys the other: the box finder opens
the ink with long one-dimensional kernels precisely to erase everything that is
not a rule, which is to say precisely to erase the passages.

Coordinates are page pixels at mapscan.DPI throughout.
"""
import cv2
import numpy as np

import mapscan

BOX_PAD = 3
SHAFT_MIN = 120


def ink_mask(pdf, page):
    """Passage ink on one page: 255 where a drawn line is, 0 elsewhere.

    The room boxes are painted out rather than merely ignored. A box rule is
    darker and straighter than any passage, so a walk that reached one would
    follow it around the box perimeter and emerge on the wrong side; erasing
    the rules makes a box a wall the walk stops at, which is what it is.

    The stonework the maps are printed over is mid-grey and the passages are
    black, so a plain threshold separates them -- unlike the boxes, whose
    interiors are darker than the background on some pages and lighter on
    others.
    """
    img = mapscan.page_image(pdf, page)
    gray = cv2.cvtColor(img, cv2.COLOR_RGB2GRAY)
    mask = (gray < SHAFT_MIN).astype(np.uint8) * 255
    for (x, y, w, h) in mapscan.find_boxes(pdf, page):
        x0 = max(0, x - BOX_PAD)
        y0 = max(0, y - BOX_PAD)
        mask[y0:y + h + BOX_PAD, x0:x + w + BOX_PAD] = 0
    return mask
```

- [ ] **Step 4: Run the tests**

```bash
python -m pytest tools/tests/test_trace_edges.py -v
```

Expected: 2 passed.

If `test_ink_mask_is_not_empty` fails, `SHAFT_MIN` is too low for this scan. Measure the real separation rather than guessing:

```bash
python -c "
import sys; sys.path.insert(0,'tools'); import mapscan, cv2, numpy as np
g = cv2.cvtColor(mapscan.page_image('tools/assets/cache/zork1.pdf', 4), cv2.COLOR_RGB2GRAY)
print(np.percentile(g, [1, 5, 25, 50, 75]))
"
```

Set `SHAFT_MIN` between the first and fifth percentile and record the measured numbers in the `ink_mask` docstring.

- [ ] **Step 5: Commit**

```bash
git add tools/trace_edges.py tools/tests/test_trace_edges.py
git commit -m "Build the passage ink mask by painting the room boxes out rather than ignoring them, because a box rule is darker and straighter than any passage and a walk that reached one would follow it around the perimeter and emerge on the wrong side, and assert both that no box border survives into the mask and that the mask is not empty, since an erasure that took everything would satisfy the first check and be useless."
```

---

## Task 4: Following a stroke

**Files:**
- Modify: `tools/trace_edges.py`
- Test: `tools/tests/test_trace_edges.py`

**Interfaces:**
- Consumes: `trace_edges.ink_mask`.
- Produces: `trace_edges.follow(mask, boxes, seed, heading) -> {"points": [(x,y)], "ends": [terminus, terminus]}` where a terminus is `("box", index)` or `("open", (x, y))`. `seed` is `(x, y)` on the stroke; `heading` is one of `(1,0)`, `(-1,0)`, `(0,1)`, `(0,-1)`.

- [ ] **Step 1: Write the failing test**

Timber Room and Drafty Room are adjacent on the drawn page and joined by a short straight horizontal line, which is the simplest case there is. Append to `tools/tests/test_trace_edges.py`:

```python
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
```

- [ ] **Step 2: Run it and watch it fail**

```bash
python -m pytest tools/tests/test_trace_edges.py::test_follow_joins_timber_room_to_drafty_room -v
```

Expected: `AttributeError: module 'trace_edges' has no attribute 'follow'`.

- [ ] **Step 3: Implement `follow`**

Append to `tools/trace_edges.py`:

```python
STEP = 2
LOOK = 7


def _lit(mask, x, y):
    """Whether a small neighbourhood of a point holds ink, which tolerates the
    one-pixel raggedness a scan leaves on a printed rule."""
    h, w = mask.shape
    x0, y0 = max(0, x - 1), max(0, y - 1)
    return mask[y0:min(h, y + 2), x0:min(w, x + 2)].any()


def _box_at(boxes, x, y):
    """The index of the box whose padded rectangle contains a point, or None."""
    for i, (bx, by, bw, bh) in enumerate(boxes):
        if (bx - BOX_PAD - STEP <= x <= bx + bw + BOX_PAD + STEP and
                by - BOX_PAD - STEP <= y <= by + bh + BOX_PAD + STEP):
            return i
    return None


def _walk(mask, boxes, x, y, dx, dy):
    """One direction of a stroke, from a seed to wherever it ends.

    At each step the current heading is preferred and the two perpendiculars
    are tried only when it dies, which is what turns a corner without turning
    round: a stroke crossing another would otherwise be free to leave along the
    crossing line.
    """
    h, w = mask.shape
    pts = [(x, y)]
    while True:
        nxt = None
        for cdx, cdy in ((dx, dy), (-dy, dx), (dy, -dx)):
            px, py = x + cdx * STEP, y + cdy * STEP
            if not (0 <= px < w and 0 <= py < h):
                continue
            if _lit(mask, px, py):
                nxt = (px, py, cdx, cdy)
                break
        if nxt is None:
            for k in range(STEP, LOOK):
                px, py = x + dx * k, y + dy * k
                if not (0 <= px < w and 0 <= py < h):
                    break
                hit = _box_at(boxes, px, py)
                if hit is not None:
                    return pts, ("box", hit)
            return pts, ("open", (x, y))
        x, y, dx, dy = nxt
        pts.append((x, y))
        hit = _box_at(boxes, x, y)
        if hit is not None:
            return pts, ("box", hit)
        if len(pts) > 4000:
            return pts, ("open", (x, y))


def follow(mask, boxes, seed, heading):
    """The whole stroke a seed sits on, walked out in both directions.

    Returns its points in order and the two things it ends at, each either a
    room box or an open point -- an open end is a labelled stub, which is how
    Infocom draws a passage whose far end is on another page.
    """
    dx, dy = heading
    fwd, end_a = _walk(mask, boxes, seed[0], seed[1], dx, dy)
    back, end_b = _walk(mask, boxes, seed[0], seed[1], -dx, -dy)
    pts = list(reversed(back)) + fwd[1:]
    return {"points": pts, "ends": [end_b, end_a]}
```

- [ ] **Step 4: Run the test**

```bash
python -m pytest tools/tests/test_trace_edges.py -v
```

Expected: 3 passed.

If the walk ends `("open", ...)` at the Drafty end, raise `LOOK` -- the cross-bars sit on this very line and the walk must be able to step over the gap they leave. Record whatever value works and why in the constant's docstring.

- [ ] **Step 5: Commit**

```bash
git add tools/trace_edges.py tools/tests/test_trace_edges.py
git commit -m "Walk a drawn passage out from a seed in both directions, preferring the current heading and trying the perpendiculars only when it dies so a stroke that crosses another turns corners without being free to leave along the crossing line, ending each arm either at a room box or at the open point that is how Infocom draws a stub whose far end is on another page, and prove it on the Timber Room to Drafty Room line that the cross-bars themselves interrupt."
```

---

## Task 5: Detecting the cross-bar cluster

**Files:**
- Modify: `tools/trace_edges.py`
- Test: `tools/tests/test_trace_edges.py`

**Interfaces:**
- Consumes: `trace_edges.ink_mask`.
- Produces: `trace_edges.hamburger_seeds(mask) -> [(x, y, axis)]` where `axis` is `"h"` for a cluster crossing a horizontal run and `"v"` for a vertical one.

- [ ] **Step 1: Write the failing test**

The named adversary gets its own assertion. Append:

```python
def test_hamburger_seeds_find_the_three_baggage_passages():
    """Zork I's underground page carries cross-bars on exactly two passages --
    Altar to Cave and Timber to Drafty -- plus the chimney stub out of Studio.
    Clusters repeat along a run, so this counts distinct passages, not glyphs.
    """
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    boxes = [b[:4] for b in mapscan.read_boxes(PDF, UNDERGROUND)]
    seeds = trace_edges.hamburger_seeds(mask)
    assert seeds, "no cross-bar clusters found at all"

    named = set()
    for (x, y, axis) in seeds:
        heading = (1, 0) if axis == "h" else (0, 1)
        run = trace_edges.follow(mask, boxes, (x, y), heading)
        for end in run["ends"]:
            if end[0] == "box":
                named.add(mapscan.norm(mapscan.read_boxes(PDF, UNDERGROUND)[end[1]][4]))
    assert {"timber room", "drafty room"} <= named
    assert "cave" in named


def test_hamburger_seeds_reject_the_hatched_wall_and_mirror():
    """North Temple's west wall is solid granite and Infocom draws it as a
    filled hatched bar beside the Temple box; the Mirror Rooms carry the same
    mark. Both are short strokes beside a line and are the thing most likely to
    be mistaken for a cluster. A cluster is three one-pixel bars over about five
    pixels; a wall is a filled rectangle."""
    mask = trace_edges.ink_mask(PDF, UNDERGROUND)
    _, temple = _box_named(PDF, UNDERGROUND, "Temple")
    tx, ty, tw, th = temple
    for (x, y, _axis) in trace_edges.hamburger_seeds(mask):
        beside_temple = (tx - 40 <= x <= tx and ty <= y <= ty + th)
        assert not beside_temple, "the hatched granite wall was read as a cluster"
```

- [ ] **Step 2: Run it and watch it fail**

```bash
python -m pytest tools/tests/test_trace_edges.py -k hamburger -v
```

Expected: `AttributeError: module 'trace_edges' has no attribute 'hamburger_seeds'`.

- [ ] **Step 3: Implement `hamburger_seeds`**

Append to `tools/trace_edges.py`:

```python
BAR_MIN, BAR_MAX = 5, 22
CLUSTER_SPAN = 26
FILL_MAX = 0.55


def _bars(mask, axis):
    """Short strokes perpendicular to one axis: (centre x, centre y, length).

    A bar crossing a horizontal run is itself vertical, so the horizontal case
    opens the ink with a vertical kernel and vice versa.
    """
    k = (1, BAR_MIN) if axis == "h" else (BAR_MIN, 1)
    opened = cv2.morphologyEx(mask, cv2.MORPH_OPEN,
                              cv2.getStructuringElement(cv2.MORPH_RECT, k))
    n, _lbl, stats, cent = cv2.connectedComponentsWithStats(opened, 8)
    out = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        long_side, short_side = (h, w) if axis == "h" else (w, h)
        if not (BAR_MIN <= long_side <= BAR_MAX):
            continue
        if short_side > 3:
            continue
        if area > FILL_MAX * w * h and short_side > 1:
            continue
        out.append((int(cent[i][0]), int(cent[i][1]), long_side))
    return out


def hamburger_seeds(mask):
    """Where Infocom's narrow-passageway mark sits: [(x, y, axis)].

    The mark is three short bars struck through a passage, and the legend on
    every page draws it twice on one sample line -- it decorates a whole run
    rather than marking a point, so a run carries several and each of them
    lands here. Callers follow the stroke a seed sits on and treat the run,
    not the seed, as the passage.

    Three collinear bars within CLUSTER_SPAN of each other make a cluster.
    Requiring three is what rejects the pair of ticks Infocom uses elsewhere,
    and requiring them thin is what rejects the filled hatched bars that stand
    for the Temple's granite wall and for a mirror.
    """
    seeds = []
    for axis in ("h", "v"):
        bars = sorted(_bars(mask, axis),
                      key=lambda b: (b[1], b[0]) if axis == "h" else (b[0], b[1]))
        for i in range(len(bars) - 2):
            trio = bars[i:i + 3]
            if axis == "h":
                if max(abs(b[1] - trio[0][1]) for b in trio) > 3:
                    continue
                span = trio[2][0] - trio[0][0]
            else:
                if max(abs(b[0] - trio[0][0]) for b in trio) > 3:
                    continue
                span = trio[2][1] - trio[0][1]
            if not (0 < span <= CLUSTER_SPAN):
                continue
            seeds.append((trio[1][0], trio[1][1], axis))
    return seeds
```

- [ ] **Step 4: Run the tests**

```bash
python -m pytest tools/tests/test_trace_edges.py -v
```

Expected: 5 passed.

These four constants are the whole detector and are the one thing here that must be *measured* rather than reasoned about. If a test fails, print what was actually found and tune against it, then record the measured figures in the constants' docstring:

```bash
python -c "
import sys; sys.path.insert(0,'tools'); import trace_edges as t
m = t.ink_mask('tools/assets/cache/zork1.pdf', 4)
for axis in ('h','v'):
    b = t._bars(m, axis)
    print(axis, len(b), sorted(x[2] for x in b)[:20])
print('seeds', t.hamburger_seeds(m))
"
```

- [ ] **Step 5: Commit**

```bash
git add tools/trace_edges.py tools/tests/test_trace_edges.py
git commit -m "Detect Infocom's narrow-passageway mark as three thin collinear bars struck through a run, opening the ink with a kernel perpendicular to the axis being tested, and reject on thinness the filled hatched rectangles that stand for the Temple's granite west wall and for the mirrors -- the one thing on the page most likely to be read as a cluster -- while treating the several clusters a single passage carries as seeds onto one run rather than as several passages."
```

---

## Task 6: Reading a run's direction and its far label

**Files:**
- Modify: `tools/trace_edges.py`
- Test: `tools/tests/test_trace_edges.py`

**Interfaces:**
- Consumes: `trace_edges.follow`, `mapscan.read_labels`, `mapscan.match_name`.
- Produces: `trace_edges.arrow_end(mask, run) -> 0 | 1 | 2` (0 no head, 1 head at `ends[1]`, 2 head at `ends[0]`), and `trace_edges.open_end_room(pdf, page, run, names) -> str | None`, which reads the parenthetical label beside an open end and returns the story room name it points at.

- [ ] **Step 1: Write the failing test**

```python
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
    assert trace_edges.arrow_end(mask, run) != 0
```

- [ ] **Step 2: Run it and watch it fail**

```bash
python -m pytest tools/tests/test_trace_edges.py -k chimney -v
```

Expected: `AttributeError: module 'trace_edges' has no attribute 'open_end_room'`.

- [ ] **Step 3: Implement both**

Append to `tools/trace_edges.py`:

```python
HEAD_WIDEN = 3
LABEL_NEAR = 60


def arrow_end(mask, run):
    """Which end of a run carries an arrowhead: 0 none, 1 ends[1], 2 ends[0].

    A head is simply ink wider than the shaft. Measuring width perpendicular to
    the run at each end and comparing against its middle avoids having to know
    how wide the shaft is on a given scan, which varies between pages.
    """
    pts = run["points"]
    if len(pts) < 6:
        return 0
    def width(i):
        x, y = pts[i]
        dx = pts[min(len(pts) - 1, i + 1)][0] - pts[max(0, i - 1)][0]
        h, w = mask.shape
        n = 0
        for k in range(-6, 7):
            px, py = (x + k, y) if dx == 0 else (x, y + k)
            if 0 <= px < w and 0 <= py < h and mask[py, px]:
                n += 1
        return n
    mid = width(len(pts) // 2)
    a, b = width(2), width(len(pts) - 3)
    if b - mid >= HEAD_WIDEN and b >= a:
        return 1
    if a - mid >= HEAD_WIDEN:
        return 2
    return 0


def open_end_room(pdf, page, run, names):
    """The story room a stub's parenthetical label names, or None.

    Infocom draws a passage whose far end is on another page as a stub with a
    label -- "(to Kitchen)" beside Studio, "(from Studio)" beside the Kitchen.
    The label is read with the same OCR and the same nearest-match over the
    story's own closed set of room names that mapscan uses for the boxes, so a
    garbled reading fails to match rather than matching confidently and wrongly.
    """
    opens = [e[1] for e in run["ends"] if e[0] == "open"]
    if not opens:
        return None
    ox, oy = opens[0]
    best, pick = 1e9, None
    for (x, y, w, h, text) in mapscan.read_labels(pdf, page):
        if "(" not in text:
            continue
        d = abs(x + w // 2 - ox) + abs(y + h // 2 - oy)
        if d < best and d < LABEL_NEAR * 4:
            best, pick = d, text
    if pick is None:
        return None
    inner = pick[pick.find("(") + 1:]
    inner = inner[:inner.find(")")] if ")" in inner else inner
    inner = mapscan.norm(inner)
    for lead in ("to ", "from "):
        if inner.startswith(lead):
            inner = inner[len(lead):]
    by_norm = {mapscan.norm(n): n for n in names}
    hit = mapscan.match_name(inner, list(by_norm))
    return by_norm.get(hit) if hit else None
```

- [ ] **Step 4: Run the tests**

```bash
python -m pytest tools/tests/test_trace_edges.py -v
```

Expected: 6 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/trace_edges.py tools/tests/test_trace_edges.py
git commit -m "Read a run's arrowhead as ink wider than its own shaft measured against its middle rather than against a fixed width that varies by page, and read a stub's parenthetical label through the same nearest-match over the story's closed set of room names the box reader uses, since \"(to Kitchen)\" beside the Studio is the only record anywhere of where the chimney goes once the compiled story has decoded its destination as zero."
```

---

## Task 7: Reconcile, emit, and prove the detector is load-bearing

**Files:**
- Create: `tools/gen_map_marks.py`, `tools/tests/test_gen_map_marks.py`
- Create: `saturn/src/engine/map_marks_data.inc` (generated)

**Interfaces:**
- Consumes: everything above, plus `mapscan.room_graph`.
- Produces: `gen_map_marks.marks_for(story, pdf) -> [(room, dir_index, dest, flags)]` with `dir_index` following `mapscan.DIRW` order, and the generated `.inc`.

- [ ] **Step 1: Write the failing test**

`tools/tests/test_gen_map_marks.py`:

```python
"""Reconciliation, and the property that makes any of this worth trusting.

The expected table is stated as object numbers because that is what ships. They
were resolved once from the story and are asserted rather than recomputed:
Studio 94, Kitchen 203, Altar 212, Cave (TINY-CAVE) 46, Timber Room 206,
Drafty Room 228. mapscan.DIRW order puts UP at 8, DOWN at 9, WEST at 2, EAST at
1 and OUT at 11.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_map_marks

PDF = os.path.join("tools", "assets", "cache", "zork1.pdf")

pytestmark = pytest.mark.skipif(
    not os.path.exists(PDF),
    reason="the Infocom map is not redistributed; fetch it into tools/assets/cache")

BAGGAGE, RETRACT = 1, 2

MARKS = [
    (94,  8, 203, BAGGAGE),
    (203, 9, 0,   RETRACT),
    (212, 9, 0,   BAGGAGE),
    (206, 2, 0,   BAGGAGE),
    (228, 1, 0,   BAGGAGE),
    (228, 11, 0,  BAGGAGE),
]


def test_the_six_marks_are_exactly_what_the_zil_says():
    assert sorted(gen_map_marks.marks_for("ZORK1", PDF)) == sorted(MARKS)


def test_the_white_cliffs_are_not_marked():
    """DEFLATE's refusal reads "The path is too narrow", which from the source
    alone is a baggage limit. Infocom drew those three lines plain. This is the
    case the scan exists to settle."""
    rooms = {m[0] for m in gen_map_marks.marks_for("ZORK1", PDF)}
    assert 32 not in rooms and 33 not in rooms and 39 not in rooms


def test_disabling_the_detector_makes_this_fail(monkeypatch):
    """The acceptance property. A calibration test that still passes when the
    thing it calibrates is removed measures nothing at all."""
    import trace_edges
    monkeypatch.setattr(trace_edges, "hamburger_seeds", lambda mask: [])
    with pytest.raises(AssertionError):
        assert sorted(gen_map_marks.marks_for("ZORK1", PDF)) == sorted(MARKS)
```

- [ ] **Step 2: Run it and watch it fail**

```bash
python -m pytest tools/tests/test_gen_map_marks.py -v
```

Expected: `ModuleNotFoundError: No module named 'gen_map_marks'`.

- [ ] **Step 3: Write `tools/gen_map_marks.py`**

The module must:

1. Build the story graph with `mapscan.room_graph(os.path.join(mapscan.Z3DIR, story + ".Z3"))`.
2. For each map page, take `trace_edges.hamburger_seeds`, follow each seed's run, and resolve both ends to object numbers -- a box end through `mapscan.page_items`, an open end through `trace_edges.open_end_room`.
3. Deduplicate runs: several clusters land on one passage, so key by the resolved endpoint pair.
4. For each resolved pair, find every exit in the graph joining them. **Apply the contribution rule:** if any such exit is `OPEN` or `BLOCKED`, emit no mark for that pair and record a disagreement instead. If all are `MAYBE`:
   - Mark each graph exit that runs in the drawn direction `BAGGAGE`.
   - Where the drawing shows an arrowhead and the graph holds an exit running the *other* way, emit `RETRACT` for that exit.
   - Where the drawing gives a destination the graph left at 0, emit that destination.
5. Emit `saturn/src/engine/map_marks_data.inc` in the shape of `emit()` in `gen_map_atlas.py:665` -- a `MapMark` array per story, then a `MAP_MARKS_STORIES` table keyed on release and serial, then `MAP_MARKS_STORY_N`. Copy that function's structure; do not invent a different one.

- [ ] **Step 4: Run the tests**

```bash
python -m pytest tools/tests/test_gen_map_marks.py -v
```

Expected: 3 passed, including the monkeypatched acceptance test.

- [ ] **Step 5: Generate the shipped table**

```bash
python tools/gen_map_marks.py --cache tools/assets/cache --only ZORK1 \
    > saturn/src/engine/map_marks_data.inc
grep -c "" saturn/src/engine/map_marks_data.inc
```

Expected: a file containing six `MapMark` entries.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_map_marks.py tools/tests/test_gen_map_marks.py \
        saturn/src/engine/map_marks_data.inc
git commit -m "Reconcile the traced cross-bar passages against Zork I's own exit graph under the one rule that the drawing may resolve a passage only where every exit on it is conditional and the graph has therefore asserted nothing, emitting six marks -- five baggage annotations and the single retraction that lets the chimney's arrow point the way the game actually permits -- and hold the whole thing against an acceptance test that fails when the detector is stubbed out, since a calibration that survives the removal of the thing it calibrates measures nothing."
```

---

# Phase B -- the mark on screen

## Task 8: `map_marks` binding

**Files:**
- Create: `saturn/src/engine/map_marks.h`, `saturn/src/engine/map_marks.c`
- Test: `saturn/tests/test_map_marks.c`

**Interfaces:**
- Consumes: `map_marks_data.inc` from Task 7.
- Produces: `int map_marks_bind(const unsigned char *story, unsigned int len)` returning the number of marks bound, and `int map_marks_for(unsigned short room, int dir, unsigned char *dest, unsigned char *flags)` returning 1 when a mark exists.

- [ ] **Step 1: Write the failing test**

`saturn/tests/test_map_marks.c`, modelled on `test_map_atlas.c:1-26` -- a fabricated v3 header is all `map_marks_bind` reads:

```c
/* Build:
     gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm \
         saturn/tests/test_map_marks.c saturn/src/engine/map_marks.c && /tmp/tmm */
#include "../src/engine/map_marks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ZORK1_MARKS 6

static void header(unsigned char *h, unsigned int release, const char *serial) {
    memset(h, 0, 0x18);
    h[0] = 3;
    h[0x02] = (unsigned char) (release >> 8);
    h[0x03] = (unsigned char) (release & 0xFF);
    memcpy(h + 0x12, serial, 6);
}

int main(void) {
    unsigned char h[0x18];
    unsigned char dest = 0xFF, flags = 0xFF;

    assert(!map_marks_for(94, 8, &dest, &flags));

    header(h, 88, "840726");
    assert(map_marks_bind(h, sizeof h) == ZORK1_MARKS);

    /* The chimney: the drawing supplies the destination the routine exit hid,
       and retracts the descent the game never permits. */
    assert(map_marks_for(94, 8, &dest, &flags));
    assert(dest == 203 && (flags & MARK_BAGGAGE));
    assert(map_marks_for(203, 9, &dest, &flags));
    assert(flags & MARK_RETRACT);

    /* Both ways through the Timber Room shaft, including the OUT synonym. */
    assert(map_marks_for(206, 2, &dest, &flags) && (flags & MARK_BAGGAGE));
    assert(map_marks_for(228, 1, &dest, &flags) && (flags & MARK_BAGGAGE));
    assert(map_marks_for(228, 11, &dest, &flags) && (flags & MARK_BAGGAGE));

    /* The White Cliffs read as a baggage limit in source and are not one. */
    assert(!map_marks_for(32, 3, &dest, &flags));
    assert(!map_marks_for(33, 0, &dest, &flags));

    /* A story with no table binds nothing rather than reading off the end. */
    header(h, 1, "000000");
    assert(map_marks_bind(h, sizeof h) == 0);
    assert(!map_marks_for(94, 8, &dest, &flags));

    printf("ok\n");
    return 0;
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm \
    saturn/tests/test_map_marks.c saturn/src/engine/map_marks.c && /tmp/tmm
```

Expected: `map_marks.h: No such file or directory`.

- [ ] **Step 3: Write the header and implementation**

`map_marks.h` declares `MARK_BAGGAGE 0x01`, `MARK_RETRACT 0x02`, `map_marks_bind` and `map_marks_for`, each with a house header block.

`map_marks.c` mirrors `map_atlas.c:1-146`: the `MapMark` and `MapMarkStory` structs, `#include "map_marks_data.inc"`, the same `HDR_RELEASE 0x02` / `HDR_SERIAL 0x12` / `HDR_MIN 0x18` constants and the same `serial_is` helper, and a `map_marks_bind` that walks `MAP_MARKS_STORIES` for a matching release and serial. `map_marks_for` scans linearly -- six entries do not justify a bisection, and unlike the atlas this is not consulted per drawn cell.

- [ ] **Step 4: Run the test**

```bash
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm \
    saturn/tests/test_map_marks.c saturn/src/engine/map_marks.c && /tmp/tmm
```

Expected: `ok`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/engine/map_marks.h saturn/src/engine/map_marks.c \
        saturn/tests/test_map_marks.c
git commit -m "Bind the scanned passage marks to a story by the release word and six serial bytes in its header exactly as map_atlas does, since object numbers are assigned by the compiler and a table is only ever valid for the build it was derived from, and scan the six entries linearly rather than bisecting because unlike the atlas this is consulted once per exit at record time and not once per drawn cell."
```

---

## Task 9: Apply the marks in the model

**Files:**
- Modify: `saturn/src/engine/map_model.h:167-180`, `saturn/src/engine/map_model.c:106`, `:130`, `:334-345`, `:549-563`
- Test: `saturn/tests/test_map_model.c`

**Interfaces:**
- Consumes: `map_marks_for`.
- Produces: `MAP_EXIT_BAGGAGE 8` in `MapExit.flags`; a corrected `g_dest` / `g_kind`.

- [ ] **Step 1: Add the flag and the storage**

In `map_model.h`, beside `MAP_EXIT_COND 1`, `MAP_EXIT_ONEWAY 2`, `MAP_EXIT_SELF 4`:

```c
#define MAP_EXIT_BAGGAGE 8
```

Extend that constant's existing header block to say what it means -- an exit Infocom's map marks with cross-bars, meaning it can be taken only under a limit on what is carried, which is a fact no compiled story records.

In `map_model.c`, beside `g_cond`, add `static unsigned short g_bag[MAP_ROOM_MAX];` and clear it in `map_model_reset` (`:130`) alongside `g_cond[i] = 0`.

- [ ] **Step 2: Write the failing test**

Append to `saturn/tests/test_map_model.c` a case that walks Studio then Kitchen and asserts the corrected graph. Build it the way that file already documents, adding `map_marks.c` to the link line.

```c
    /* The chimney. The story decodes Studio's up exit with no destination and
       the Kitchen's descent as conditional on a flag that is never set, so the
       map drew a one-way arrow at the single direction the game refuses. The
       scanned marks supply the one and retract the other, and the two edits are
       one edit: supplying the destination alone would make has_reverse succeed
       and delete the arrow entirely. */
    {
        MapExit ex[RM_DIR_N];
        int n, k, saw_up = 0;
        map_model_reset();
        enter_room(94);           /* Studio  */
        enter_room(203);          /* Kitchen */

        n = map_model_exits(94, ex, RM_DIR_N);
        for (k = 0; k < n; k++) {
            if (ex[k].dir != 8) continue;
            saw_up = 1;
            assert(ex[k].dest == 203);
            assert(ex[k].flags & MAP_EXIT_BAGGAGE);
            assert(ex[k].flags & MAP_EXIT_ONEWAY);
        }
        assert(saw_up);

        n = map_model_exits(203, ex, RM_DIR_N);
        for (k = 0; k < n; k++)
            assert(ex[k].dir != 9);
    }
```

- [ ] **Step 3: Run it and watch it fail**

Expected: the assertion on `saw_up` fails -- Studio's up exit is still dropped for `dest == 0`.

- [ ] **Step 4: Apply the marks in `record_exits`**

`record_exits` (`map_model.c:334`) is the single chokepoint the live path and the restore rebind both pass through, which is why the marks go here and nowhere else:

```c
static void record_exits(unsigned short room, const RoomModel *m) {
    int d;
    g_cond[room] = 0;
    g_bag[room] = 0;
    for (d = 0; d < RM_DIR_N; d++) {
        unsigned char mdest = 0, mflags = 0;
        int marked = map_marks_for(room, d, &mdest, &mflags);
        g_dest[room][d] = m->dest[d];
        g_kind[room][d] = (unsigned char)
            (m->exits[d] == RM_EXIT_NONE ? MAP_LINK_NONE
             : (d >= RM_UP ? MAP_LINK_VERT : MAP_LINK_FLAT));
        if (m->exits[d] == RM_EXIT_MAYBE)
            g_cond[room] |= (unsigned short) (1u << d);
        if (!marked) continue;
        if (mflags & MARK_RETRACT) {
            g_kind[room][d] = MAP_LINK_NONE;
            g_dest[room][d] = 0;
            g_cond[room] &= (unsigned short) ~(1u << d);
            continue;
        }
        if (mdest != 0) g_dest[room][d] = mdest;
        if (mflags & MARK_BAGGAGE)
            g_bag[room] |= (unsigned short) (1u << d);
    }
}
```

In `map_model_exits` (`:556`), beside the `MAP_EXIT_COND` line:

```c
        if (g_bag[room] & (unsigned short) (1u << d))
            out[n].flags |= MAP_EXIT_BAGGAGE;
```

Add `#include "map_marks.h"` to `map_model.c`, and call `map_marks_bind` wherever `map_atlas_bind` is already called so the two tables bind together.

- [ ] **Step 5: Run the model and rose suites**

`test_command_rose` is the one that proves exit *classification* is unchanged. Build both fresh -- a stale binary in `/tmp` will print `ok` after a failed compile.

```bash
rm -f /tmp/tmm2 /tmp/tcr
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tmm2 \
    saturn/tests/test_map_model.c saturn/src/engine/map_model.c \
    saturn/src/engine/map_atlas.c saturn/src/engine/map_marks.c \
    saturn/src/engine/room_model.c && /tmp/tmm2
gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/tcr \
    saturn/tests/test_command_rose.c saturn/src/engine/room_model.c && /tmp/tcr
```

Expected: both print `ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/map_model.h saturn/src/engine/map_model.c \
        saturn/tests/test_map_model.c
git commit -m "Apply the scanned marks inside record_exits, the one chokepoint both the live path and the restore rebind pass through, so has_reverse and map_model_link and map_model_exits all read one corrected graph and cannot disagree about it, giving the Studio's routine exit the destination the drawing names and retracting the Kitchen's descent in the same pass because supplying the first without the second would let has_reverse succeed and silently delete the very arrow the correction exists to point the right way."
```

---

## Task 10: The cross-bar tiles

**Files:**
- Modify: `tools/gen_dash_tiles.py:16`, `:198-233`, `:431-435`; `saturn/src/video/dash_map.h:107-111`
- Test: `saturn/tests/test_dash_tiles.c`

**Interfaces:**
- Produces: `DT_BAGGAGE_H`, `DT_BAGGAGE_V` in the `dash_map.h` enum, immediately after `DT_LOOP`; `N` rises from 142 to 144.

- [ ] **Step 1: Add the enum entries**

In `dash_map.h`, after `DT_LOOP,`:

```c
    DT_BAGGAGE_H, DT_BAGGAGE_V,
```

Extend the enum's existing header block: `DT_BAGGAGE_H` and `DT_BAGGAGE_V` carry Infocom's narrow-passageway mark -- three bars struck through the groove -- on an east-west and a north-south run respectively.

- [ ] **Step 2: Write the failing test**

Append to `saturn/tests/test_dash_tiles.c` a check that the two tiles exist, are distinct, keep the shaft lit, and are each other's quarter turn:

```c
    {
        const unsigned char *h = dash_tile(DT_BAGGAGE_H);
        const unsigned char *v = dash_tile(DT_BAGGAGE_V);
        int x, y, bars = 0;
        for (x = 0; x < 8; x++) assert(h[3 * 8 + x] && h[4 * 8 + x]);
        for (y = 0; y < 8; y++) assert(v[y * 8 + 3] && v[y * 8 + 4]);
        for (x = 0; x < 8; x++) if (h[1 * 8 + x]) bars++;
        assert(bars == 3);
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                assert(v[y * 8 + x] == h[(7 - x) * 8 + y]);
    }
```

Match the accessor this file already uses for tile bytes rather than inventing `dash_tile` if it is named something else.

- [ ] **Step 3: Run it and watch it fail**

Build `test_dash_tiles.c` the way its own header comment documents. Expected: `DT_BAGGAGE_H` undeclared.

- [ ] **Step 4: Draw the tiles**

In `gen_dash_tiles.py`, beside `LOOP`:

```python
BAGGAGE_H = ["........",
             "..#.#.#.",
             "..#.#.#.",
             "########",
             "########",
             "..#.#.#.",
             "..#.#.#.",
             "........"]
```

After `tiles.append(bitmap(LOOP, MARK_DARK))`:

```python
    baggage_h = bitmap(BAGGAGE_H, MARK_DARK)
    tiles.append(baggage_h)                                     # DT_BAGGAGE_H
    tiles.append(rot_cw(baggage_h))                             # DT_BAGGAGE_V
```

Raise `N = 142` to `N = 144`.

- [ ] **Step 5: Regenerate and test**

```bash
python tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
```

Then rebuild and run `test_dash_tiles.c`. Expected: `ok`.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_dash_tiles.py saturn/src/video/dash_map.h \
        saturn/src/video/dash_tiles.c saturn/tests/test_dash_tiles.c
git commit -m "Draw Infocom's narrow-passageway mark as three one-pixel bars struck through the two-pixel groove, one tile drawn east-west and the other taken as its quarter turn through the rotation the arrowheads already use so the pair cannot drift apart, and assert in the suite that the shaft stays lit under the bars and that the vertical tile really is the horizontal one turned."
```

---

## Task 11: Render the mark

**Files:**
- Modify: `saturn/src/video/map_edges.h:29-40`, `saturn/src/video/map_edges.c:24-30`, `:213-218`, `:260-270`, `:311-334`; `saturn/src/video/map_view.cxx:491`
- Test: `saturn/tests/test_map_edges.c`

**Interfaces:**
- Produces: `MAP_EDGE_BAGGAGE 0x4000`; `map_edges_stub(int mx, int my, int dx, int dy, unsigned int flags)` -- the signature gains a parameter.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_map_edges.c`:

```c
    /* A baggage run draws solid rather than dashed even though the exit is
       conditional -- Infocom makes the baggage limit its own legend entry, not
       a flavour of "requires problem solving", and draws Timber to Drafty solid
       despite its being a conditional exit. Exactly one cross-bar cell falls in
       the three between adjacent rooms. */
    {
        int x, bars = 0, solid = 0;
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT,
                       MAP_EXIT_COND | MAP_EXIT_BAGGAGE, 0);
        for (x = 5; x <= 7; x++) {
            unsigned char t = map_edges_tile(x, 4);
            if (t == DT_BAGGAGE_H) bars++;
            else if (t == DT_LINK_H) solid++;
            assert(t != DT_DASH0 + DT_LINK_H);
        }
        assert(bars == 1);
        assert(solid == 2);
    }

    /* A conditional run with no baggage mark still dashes, so the override is
       the mark's doing and not a change to conditional passages at large. */
    {
        map_edges_reset();
        map_edges_mark(4, 4);
        map_edges_mark(8, 4);
        map_edges_link(4, 4, 8, 4, MAP_LINK_FLAT, MAP_EXIT_COND, 0);
        assert(map_edges_tile(6, 4) == DT_DASH0 + DT_LINK_H);
    }

    /* The chimney is drawn as a stub, because Studio and the Kitchen are on
       different floors of the atlas, so the mark has to reach the stub path as
       well as the link path. A stub's single shaft cell always takes one. */
    {
        map_edges_reset();
        map_edges_mark(6, 6);
        map_edges_stub(6, 6, 0, -1, MAP_EXIT_BAGGAGE);
        assert(map_edges_tile(6, 5) == DT_BAGGAGE_V);
    }
```

- [ ] **Step 2: Run it and watch it fail**

```bash
rm -f /tmp/tme
gcc -O2 -Wall -Wextra -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c \
    saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme
```

Expected: compile error -- `MAP_EDGE_BAGGAGE` undeclared and `map_edges_stub` takes four arguments.

- [ ] **Step 3: Implement**

In `map_edges.c`, beside the internal bit plan:

```c
#define MAP_EDGE_BAGGAGE  0x4000
```

Declare it in `map_edges.h` alongside `MAP_EDGE_UP`/`DOWN`/`LOOP`, since `map_view.cxx` names it, and extend that block's prose to say `0x8000` remains spare.

In `map_edges_link` (`:216`), the deco becomes:

```c
    unsigned short deco = (unsigned short)
        ((flags & MAP_EXIT_BAGGAGE) ? (MAP_EDGE_SOLID | MAP_EDGE_BAGGAGE)
         : (flags & MAP_EXIT_COND) ? MAP_EDGE_DASH : MAP_EDGE_SOLID);
```

Give `map_edges_stub` a `flags` parameter and pass `(flags & MAP_EXIT_BAGGAGE) ? (MAP_EDGE_SOLID | MAP_EDGE_BAGGAGE) : MAP_EDGE_DASH` as its deco to both `mark_step` calls. Update its header block.

In `map_edges_tile`, after the arrow branch and before `mask = e & 15;`:

```c
    mask = e & 15;
    if (mask == 0) return 0;
    if (e & MAP_EDGE_BAGGAGE) {
        int straight = (mask == (DT_EDGE_E | DT_EDGE_W)) ||
                       (mask == (DT_EDGE_N | DT_EDGE_S));
        int stub = (mask == DT_EDGE_N) || (mask == DT_EDGE_S) ||
                   (mask == DT_EDGE_E) || (mask == DT_EDGE_W);
        if (straight && ((x + y) % 3) == 0)
            return (mask == (DT_EDGE_E | DT_EDGE_W)) ? DT_BAGGAGE_H : DT_BAGGAGE_V;
        if (stub)
            return (mask == DT_EDGE_E || mask == DT_EDGE_W)
                   ? DT_BAGGAGE_H : DT_BAGGAGE_V;
    }
```

Remove the now-duplicated `mask` lines further down. Give the branch a sentence in the function's header block: a baggage run takes the mark on straight cells at a fixed cell-space period, and on the single shaft cell of a stub unconditionally, since a stub is too short for a period to be certain of catching.

In `map_view.cxx:491`, pass the exit's flags: `map_edges_stub(cx, cy, 0, dy, ex[k].flags);`

- [ ] **Step 4: Run the suite**

```bash
rm -f /tmp/tme
gcc -O2 -Wall -Wextra -I saturn/src -o /tmp/tme saturn/tests/test_map_edges.c \
    saturn/src/video/map_edges.c saturn/src/video/dash_tiles.c && /tmp/tme
```

Expected: `ok`.

If `bars == 1` fails, the `(x + y) % 3` phase does not land in cells 5..7 at row 4. Adjust the phase offset -- not the period -- and state the chosen offset in the header block.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/map_edges.h saturn/src/video/map_edges.c \
        saturn/src/video/map_view.cxx saturn/tests/test_map_edges.c
git commit -m "Render the baggage mark on straight cells at a fixed period in cell space so a run keeps its rhythm across cell edges the way the dash does, and unconditionally on the single shaft cell of a stub because a stub is too short for a period to be sure of catching it and the chimney is drawn as one, forcing a marked run solid rather than dashed since Infocom makes the baggage limit its own legend entry rather than a flavour of requiring problem solving and dashing it as well would report one fact twice."
```

---

## Task 12: Build both Saturn targets

**Files:**
- Modify: `saturn/Makefile:96`

- [ ] **Step 1: Add `map_marks.c` to the netbin's explicit list**

The CD build discovers sources with `find src/ -name '*.c'` (`Makefile:40`) and picks the new file up by itself. The netbin build (`Makefile:64-101`) lists every source by hand and will not. After `src/engine/map_atlas.c \`:

```
          src/engine/map_marks.c \
```

- [ ] **Step 2: Build both targets**

From PowerShell, with the `cd` and the build in one command. Do not run `make`.

```powershell
cd C:\Users\saggl\CLionProjects\zaturn-intro\saturn; .\compile.bat; echo "EXIT=$LASTEXITCODE"
```

Expected: `EXIT=0`. Then the netbin the same way with `compile-netbin.bat`.

- [ ] **Step 3: Run every host suite**

```bash
python -m pytest saturn/tests tools/tests -q
```

Plus each C suite named in this plan, built fresh.

Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add saturn/Makefile
git commit -m "Name map_marks.c in the netbin's explicit source list, which unlike the CD build's find does not discover a new engine file by itself, and confirm both targets still link."
```

---

# Phase C -- the audit

## Task 13: The whole-network disagreement report

**Files:**
- Modify: `tools/gen_map_marks.py`
- Create: `docs/ZORK1_MAP_SCAN_AUDIT.md`

- [ ] **Step 1: Add box-edge seeding**

Add `--audit` to `gen_map_marks.py`. Instead of seeding at cross-bar clusters, seed `trace_edges.follow` from every box's four edge midpoints, on each page, deduplicating runs by their resolved endpoint pair.

- [ ] **Step 2: Compare against the graph and classify**

For each resolved pair, compare the drawn run against the graph's exits and bucket the result:

- **agree** -- the graph holds an exit between them and the drawn direction matches.
- **drawn only** -- the drawing joins them, the graph does not.
- **graph only** -- the graph joins them, the drawing does not.
- **direction differs** -- both join them, the arrowheads disagree.

For each disagreement record the pair, the graph's exit states, and whether the all-`MAYBE` rule would permit resolving it.

- [ ] **Step 3: Write the report**

```bash
python tools/gen_map_marks.py --cache tools/assets/cache --only ZORK1 --audit \
    > docs/ZORK1_MAP_SCAN_AUDIT.md
```

The report must open with a paragraph stating that it changes nothing, that a disagreement is a question for the owner rather than a defect to be auto-corrected, and that only the all-`MAYBE` cases are even eligible for resolution. Every table row cites the graph's exit states so a reader can apply the rule themselves.

- [ ] **Step 4: Read it and sanity-check the totals**

The six marks from Task 7 must appear as resolved or agreeing rows, and the count of traced rooms should be in the region of the atlas's 84 for Zork I. A wildly lower number means the box-edge seeding is missing lines, not that Infocom drew fewer.

- [ ] **Step 5: Commit**

```bash
git add tools/gen_map_marks.py docs/ZORK1_MAP_SCAN_AUDIT.md
git commit -m "Seed the same tracer from every box edge rather than from the cross-bar clusters to read Zork I's whole drawn network, and write the comparison against the story's exit graph to a report that changes nothing by itself, bucketing each pair as agreeing or drawn-only or graph-only or differing in direction and citing the graph's own exit states on every row so a reader can apply the all-conditional resolution rule themselves rather than taking the tool's word for which disagreements are even eligible."
```

---

# Self-Review

**Spec coverage.** Legend as a closed set -- context, no task needed. Three baggage passages -- Tasks 2, 7. DEFLATE excluded -- Tasks 2, 7. Why the graph cannot supply it -- Tasks 6, 9. Contribution rule -- Task 7 step 3, Task 9. `mapscan.py` -- Task 1. `trace_edges.py` -- Tasks 3-6. Hatched-bar adversary -- Task 5. Cross-page labels -- Task 6. `MapMark` and `record_exits` -- Tasks 8, 9. `MAP_EXIT_BAGGAGE 8` -- Task 9. Tiles and `0x4000` -- Tasks 10, 11. Straight-only, phase, solid override -- Task 11. Audit report -- Task 13. Byte-identical atlas -- Task 1 step 4. Acceptance property -- Task 7 step 1. Netbin source list -- Task 12. **No gaps.**

**Placeholder scan.** Clean. Every code step carries runnable code; the four CV constants that genuinely cannot be reasoned about in advance (`SHAFT_MIN`, `LOOK`, and the four bar thresholds) each come with a named measurement command and an instruction to record the measured figure in the docstring, which is an action rather than a TBD. Task 7 step 3 is prose rather than code -- it specifies five numbered obligations and points at `gen_map_atlas.py:665` as the emitter to copy -- because the reconciliation is the one place a literal would be guessing at the tracer's real output shape.

**Type consistency.** `MARK_BAGGAGE`/`MARK_RETRACT` are used identically in Tasks 7, 8, 9. `map_marks_for(room, dir, *dest, *flags)` matches between Tasks 8 and 9. `map_edges_stub` gains its fifth parameter in Task 11 and every call site is updated in the same task. `DT_BAGGAGE_H`/`DT_BAGGAGE_V` are defined in Task 10 and consumed in Task 11. `mapscan.DIRW` indices are stated once in Task 7 and used in Task 8's test.

**One spec refinement.** The spec says the truth set is "parsed from `1dungeon.zil`". Task 2 both parses it *and* asserts the parsed result against a transcribed literal, so that a change to the source invalidates the calibration loudly instead of leaving it silently stale. That is stronger than the spec's wording, not weaker.
