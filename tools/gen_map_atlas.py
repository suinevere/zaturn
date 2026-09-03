#!/usr/bin/env python3
"""Emit saturn/src/engine/map_atlas_data.inc: authored room positions read off
Infocom's own maps and checked against each compiled story's exit graph.

Usage:
    python3 tools/gen_map_atlas.py --cache DIR [--only STORY ...] [--report]
      > saturn/src/engine/map_atlas_data.inc

Why an authored table exists at all is in saturn/src/engine/map_atlas.h. What
this script contributes is that no coordinate is typed in by hand: box geometry
is measured off the scan, room names are read by OCR, the assignment of a name
to an object number is resolved against the story's own exits, and every
placement is then validated against those exits. A game that does not validate
is dropped rather than shipped, so the failure mode is a missing table -- which
falls back to the graph walk -- rather than a wrong one.

THE MAPS ARE NOT REDISTRIBUTED. They are Activision's, scanned by the Infocom
Documentation Project (infodoc.plover.net) and reproduced there with permission.
This script downloads them into a local cache that is not part of the repository
and emits only coordinates: which grid cell each room sits in. Do not commit the
cache. What ships is derived factual geography, validated against the game's own
data, not the drawings.

Five stages. The first three -- reading what a page draws and matching it to a
story's own room names -- now live in tools/mapscan.py, which this script
imports; what follows here is stages four and five, laying the readings out
and checking them against the exit graph.

1. Boxes (tools/mapscan.py). The scans are low contrast and printed over drawn
   stonework, so nothing thresholds cleanly -- on Zork I's page 3 the box
   interiors are darker than the background (mean 174 against 193). What
   separates them is that the rules framing a room are the only dark things
   running straight for tens of pixels, so opening the ink mask with long 1-D
   kernels keeps the rules and erases the stone. Room interiors are then the
   enclosed holes.

2. Names (tools/mapscan.py). OCR over each box, which on Zork I's page 3 reads
   21 of 21 correctly including the map's own "(1)".."(4)" disambiguators.
   Those parentheticals are the map's numbering and do not exist in the game,
   but they do tell us two boxes are different rooms, which is worth keeping.

3. Assignment (tools/mapscan.py for the name matching; this script for the
   permutation search). Nine short names in Zork I are shared by two or more
   rooms, so a name alone cannot pick an object. Unambiguous names are
   assigned first; each ambiguous group is then resolved by trying every
   permutation and taking the one whose exits best agree with the rooms
   already placed. Groups larger than AMBIG_MAX are dropped rather than
   guessed -- that is what excludes the fifteen Maze rooms, whose drawn layout
   is an arbitrary embedding anyway.

4. Layout. Centres are clustered into ordered lanes per page rather than snapped
   to a fixed pitch, because Infocom's maps are not drawn on a uniform grid --
   aligned centres on Zork I's page 3 sit 147, 148 then 186 apart. Lanes preserve
   every left-of and above-of relation, which is all a compass direction is.
   Separate pages are separate drawings at different scales, so each is given its
   own band of rows below the last: above ground stays above underground. Each
   room also records which page it came off, because a page is a floor -- the
   publisher split the map where the geography did -- and the runtime shows one
   floor at a time rather than the whole stacked strip.

5. Validation. For every open compass exit whose both ends are placed, the drawn
   offset must lie in that direction's half-plane. A game below PASS_RATE is
   reported and dropped.

Needs pymupdf, opencv-python, numpy and rapidocr-onnxruntime. Run by hand when
the maps or the story builds change, not by the Saturn build.
"""
import argparse
import itertools
import json
import os
import re
import subprocess
import sys

import pymupdf

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from mapscan import (DPI, DIRW, FL_DIR, PROP_MAX, Z3DIR, MIN_BOXES, MIN_NAMED,
                     FUZZ_MIN, FUZZ_MARGIN, ocr_engine, direction_props,
                     room_graph, page_image, find_boxes, read_boxes,
                     read_labels, norm, base_and_index, match_name, page_items)

BASE_URL = "https://infodoc.plover.net/maps/"

# Where the emitted table lives, so a walk-only run can carry the measured
# tables forward without rebuilding them -- see carried().
INC_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        os.pardir, "saturn", "src", "engine",
                        "map_atlas_data.inc")

# The sentence a walked table's header carries and a measured one does not.
# Provenance is recorded rather than implied: these rooms were never on a
# printed map, and a reader who assumed otherwise would trust a coordinate that
# is an inference from the exit graph. tests/test_atlas_walk.py holds the set of
# tables carrying it, so neither kind can quietly turn into the other.
DERIVED_MARK = "DERIVED, not measured:"

# What marks one CELL of an otherwise measured table as walked in rather than
# read off the drawing. A whole table's provenance is a sentence in its header;
# a single room's has to be on its own line, because a merged table's two halves
# sit in one list and a reader picking a coordinate out of it is entitled to
# know which kind it is.
ADDED_MARK = "+"

# page_items' memo, at module scope rather than per build_game. The sweep below
# lays one game out dozens of ways and OCR is far and away the slowest thing
# here; keyed by (pdf, page), so re-reading is free and the sweep costs what one
# pass used to.
_PAGE_CACHE = {}

# story file stem -> the map PDF on the Documentation Project
MAPS = {
    "BALLYHOO": "ballyhoo.pdf", "CUTHROAT": "cutthroa.pdf", "DEADLINE": "deadline.pdf",
    "ENCHANTR": "enchante.pdf", "HOLYWOOD": "hollywoo.pdf", "INFIDEL": "infidel.pdf",
    "LEATHERG": "leather.pdf", "LURKING": "lurking.pdf", "MOONMIST": "moonmist.pdf",
    "PLNDHRTS": "plundere.pdf", "PLNTFALL": "planetfa.pdf", "SORCERER": "sorcerer.pdf",
    "SPLBRKR": "spellbre.pdf", "STARCROS": "starcros.pdf", "STATFALL": "stationf.pdf",
    "SUSPECT": "suspect.pdf", "SUSPENDD": "suspende.pdf", "WISHBRNG": "wishbrin.pdf",
    "WITNESS": "witness.pdf", "ZORK1": "zork1.pdf", "ZORK2": "zork2.pdf",
    "ZORK3": "zork3.pdf",
}

# The largest group of identically-named rooms this will try to tell apart. 5!
# is 120 scorings and instant; 15! is the Maze and is not attempted.
AMBIG_MAX = 6

# How many refinement sweeps the assignment gets before it stops regardless.
REFINE_ROUNDS = 4

# The fewest testable exits before a map is allowed to be treated as turned.
# Below this there is not enough evidence to tell a real rotation from a small
# set of exits happening to fit one.
ORIENT_MIN = 6


# A game whose placements disagree with its own exits more often than this is
# reported and dropped rather than shipped.
PASS_RATE = 0.85

# How far apart two drawn box centres may be, in points at DPI, and still be
# read as the same column or the same row.
#
# snap() is single-linkage: a lane spans at most this much, so two rooms Infocom
# drew in one column but printed a few points apart land in different lanes and
# every north exit between them acquires a sideways step.
#
# Swept per game rather than fixed, because the maps are printed at different
# scales and one number cannot serve all of them: LANE_TOLS is tried in order
# and the reading that puts the most cardinal exits on their own axis wins, ties
# going to the tightest tolerance so a game gains nothing from a looser reading
# it did not need. Too loose is not free -- merging two columns Infocom really
# did draw apart destroys the east-west relation between them -- which is why
# the sweep is scored on alignment AND required not to lose half-plane
# agreement.
#
# Worth little, and kept for what little it is worth. Sixteen of the eighteen
# games hold at 60; Spellbreaker takes 75 and Stationfall 160, for two exits and
# one. The reason the tolerance was reached for -- that of the cardinal exits
# missing their axis, 57% missed by exactly one lane and 78% by one or two,
# which reads like a clustering artefact -- turned out not to be the cause of
# most of them. What straightens those is the nudge below, which repairs a room
# the reading placed off true instead of re-reading the whole page around it.
LANE_TOLS = (60, 75, 90, 110, 130, 160)

# Not corrected here: the oblique drawings.
#
# Several of these maps are drawn in projection rather than in plan. The Lurking
# Horror's compass rose is a parallelogram -- north straight up, the ground's
# north-south axis leaning right -- and every room box on its sheets is sheared
# to match, so a room due south of another is printed down AND across. No
# rotation fixes that; the four the layout tries are all right angles, and a
# sheared drawing is not a turned one.
#
# Un-shearing the centres before the columns are read was tried, swept over nine
# leans from -0.6 to +0.6. Every one of the eighteen games chose zero: the nudge
# below reaches the same placements directly and reaches them further, taking
# The Lurking Horror to 32 of 32 cardinal exits on axis where the best shear
# managed no better than leaving it alone. The sweep is gone rather than kept at
# a setting nothing selects.

# What a coordinate must fit in. map_atlas.c stores x and y as signed chars, and
# the layout's own clamp to this range runs before the nudge below, so the nudge
# has to keep to it itself; nothing else would catch a room pushed past 127 but
# the silent wrap on the console. No shipped table comes anywhere near -- the
# widest is 24 rooms from the centre -- which is exactly why this is asserted
# rather than trusted to stay true.
CELL_MIN, CELL_MAX = -128, 127

# How far a single room may be moved off its measured lane to put an exit on its
# axis, and how many passes the search gets. Two lanes covers the 78% of misses
# that are within two of true; beyond that a miss is more likely to be real
# geography, and moving a room further starts redrawing the map rather than
# tidying it.
#
# This is what actually straightened the maps: 24 moves across eight games took
# the atlas from 664 of 779 cardinal exits on axis to 702, The Lurking Horror
# from 19 of 32 to all of them and The Witness from 4 of 8 to all of them, with
# no game losing a single half-plane agreement. It moves a room off where the
# scan measured it, which is the trade: at most two lanes, only where the
# story's own compass says the drawing disagrees with it, and never at the cost
# of a left-of or above-of relation.
NUDGE_SPAN = 2
NUDGE_ROUNDS = 6

# The walk's own three numbers. REPAIR_SPAN is wider than NUDGE_SPAN because the
# fault it fixes is bigger: a nudge tidies a room a lane off true, a repair
# retrieves one the free-cell search pushed to the wrong side entirely. Three
# cells covers what an outward ring search can do before it gives up, and the
# square is 48 trials a room rather than the nudge's 8 -- affordable offline and
# nowhere near the runtime.
REPAIR_SPAN = 3
REPAIR_ROUNDS = 8

# How far free_cell will look for somewhere to put a room before it gives up and
# stacks. Reached by nothing on the disc; it is a bound on a search, not a
# tuning knob.
FILL_SPAN = 64

# One cell per compass direction. The step a walked layout takes when it follows
# an exit -- up, down, in and out have no planar step and are left out, which is
# the same set LEVEL_DIRS splits floors on.
STEP = {"north": (0, -1), "south": (0, 1), "east": (1, 0), "west": (-1, 0),
        "ne": (1, -1), "nw": (-1, -1), "se": (1, 1), "sw": (-1, 1)}

HALF = {
    "north": lambda dx, dy: dy < 0, "south": lambda dx, dy: dy > 0,
    "east": lambda dx, dy: dx > 0, "west": lambda dx, dy: dx < 0,
    "ne": lambda dx, dy: dx > 0 and dy < 0, "nw": lambda dx, dy: dx < 0 and dy < 0,
    "se": lambda dx, dy: dx > 0 and dy > 0, "sw": lambda dx, dy: dx < 0 and dy > 0,
}
"""HALF

Description: Whether a drawn delta falls in the half-plane a direction names.
    This is deliberately loose and is the right test for what it is used for --
    scoring which object a drawn box is, and whether a whole map has been turned
    -- because Infocom's maps are not drawn on a lattice and a room reached by
    going north may well be drawn up and a little to one side.

    It is NOT a test that a north exit is drawn due north, and the emitted
    header used to report its result as "leave in the direction drawn", which
    reads as though it were. AXIS below is that test, reported beside it.
Author: suinevere
"""

LEVEL_DIRS = ("north", "east", "west", "south", "ne", "nw", "se", "sw",
              "in", "out")
"""LEVEL_DIRS

Description: The exits that do not change floor. Everything except up and down,
    including in and out -- walking into a building puts you on its ground
    floor, not above or below it.
Author: suinevere
"""

VERT = {
    "up": lambda dx, dy: dy < 0,
    "down": lambda dx, dy: dy > 0,
}
"""VERT

Description: Which way up and down are normally drawn. Not an axis test -- a
    staircase is often drawn off to one side -- only a sense, so that a nudge
    cannot put the bottom of a canyon above its top while straightening
    something else. Held, never improved: a map that draws a descent upward was
    drawn that way and is not this pass's business to argue with.
Author: suinevere
"""

AXIS = {
    "north": lambda dx, dy: dx == 0 and dy < 0,
    "south": lambda dx, dy: dx == 0 and dy > 0,
    "east": lambda dx, dy: dy == 0 and dx > 0,
    "west": lambda dx, dy: dy == 0 and dx < 0,
}
"""AXIS

Description: Whether a cardinal exit is drawn on its own axis, with no sideways
    component at all. Only the four compass points have an axis to be on; a
    diagonal is a diagonal at any ratio, so the eight-way HALF test is already
    exact for those and they are left out rather than given a rule that would
    always pass.

    This is what a player reads off the screen: told the exit is south, they
    expect the line to go down, not down and across. It is reported and not
    enforced -- see the note on the drop rule in main() -- because a map drawn
    square to a building rather than to the compass is Infocom's, not an error,
    and refusing those would drop Zork I, The Lurking Horror and The Witness
    entirely and fall all three back to the graph walk.
Author: suinevere
"""


def snap(values, tol):
    """Cluster centre coordinates into ordered lane indices.

    Infocom's maps are not drawn on a uniform pitch, so a fixed step would
    distort them. Ordered lanes keep every left-of and above-of relation, which
    is all a compass direction is, without pretending to a regularity the
    drawing does not have.
    """
    order = sorted(range(len(values)), key=lambda i: values[i])
    lane, cur, out = 0, values[order[0]], {}
    for i in order:
        if values[i] - cur > tol:
            lane += 1
            cur = values[i]
        out[i] = lane
    return out


def agreement(pos, graph, among=None):
    """(agreed, tested, [failures]) over compass exits with both ends placed."""
    agreed = tested = 0
    bad = []
    keys = pos.keys() if among is None else among
    for a in keys:
        ax, ay = pos[a]
        for dirn, (kind, dest) in graph[a]["exits"].items():
            if kind != "OPEN" or dest not in pos or dirn not in HALF:
                continue
            tested += 1
            bx, by = pos[dest]
            if HALF[dirn](bx - ax, by - ay):
                agreed += 1
            else:
                bad.append((graph[a]["name"], dirn, graph[dest]["name"]))
    return agreed, tested, bad


def alignment(pos, graph):
    """(aligned, tested, [failures]) over cardinal exits with both ends placed
    on the same page. The strict counterpart of agreement()."""
    aligned = tested = 0
    bad = []
    for a in pos:
        ax, ay = pos[a]
        for dirn, (kind, dest) in graph[a]["exits"].items():
            if kind != "OPEN" or dest not in pos or dirn not in AXIS:
                continue
            tested += 1
            bx, by = pos[dest]
            if AXIS[dirn](bx - ax, by - ay):
                aligned += 1
            else:
                bad.append((graph[a]["name"], dirn, graph[dest]["name"],
                            bx - ax, by - ay))
    return aligned, tested, bad


def assign(cells, graph):
    """Map each drawn box to an object number.

    cells is {box key: (lane_x, lane_y, text)}. Unambiguous names go first, then
    each ambiguous group is resolved by trying every permutation and keeping the
    one whose exits best agree with what is already placed. Groups larger than
    AMBIG_MAX are dropped rather than guessed.
    """
    by_name = {}
    for num, r in graph.items():
        key = norm(r["name"])
        if not key:
            continue
        by_name.setdefault(key, []).append(num)
    for v in by_name.values():
        v.sort()

    groups = {}
    for key, (lx, ly, text) in cells.items():
        base, _ = base_and_index(text)
        if base:
            groups.setdefault(base, []).append(key)

    pos, chosen, dropped = {}, {}, []
    for base, keys in sorted(groups.items()):
        objs = by_name.get(base, [])
        if len(objs) == 1 and len(keys) == 1:
            k = keys[0]
            pos[objs[0]] = (cells[k][0], cells[k][1])
            chosen[k] = objs[0]

    for base, keys in sorted(groups.items(), key=lambda kv: (len(kv[1]), kv[0])):
        objs = by_name.get(base, [])
        if not objs or (len(objs) == 1 and len(keys) == 1):
            continue
        keys = sorted(keys, key=lambda k: (cells[k][1], cells[k][0]))
        if len(keys) > AMBIG_MAX or len(objs) > AMBIG_MAX:
            dropped.append((base, len(keys), len(objs)))
            continue
        best, best_score = None, -1
        for perm in itertools.permutations(objs, min(len(keys), len(objs))):
            trial = dict(pos)
            for k, o in zip(keys, perm):
                trial[o] = (cells[k][0], cells[k][1])
            a, t, _ = agreement(trial, graph, among=list(perm))
            score = a - (t - a)
            if score > best_score:
                best_score, best = score, perm
        if best is None:
            continue
        for k, o in zip(keys, best):
            pos[o] = (cells[k][0], cells[k][1])
            chosen[k] = o

    # Refine. The pass above resolves each ambiguous group against whatever was
    # already placed, which for a game like Starcross is almost nothing: five
    # Red Halls, five Green Halls, six Outskirts of Village and only a handful
    # of uniquely-named rooms to anchor them, so the first group decided is
    # decided against an empty board. Scoring every group again now that the
    # whole layout exists is what catches those, and it shows up in the failures
    # as exits like "Thin Forest --west--> Thin Forest" that a swap would fix.
    #
    # Hill-climbing on the same score the game is finally judged by, so this can
    # only move a table toward validating. It stops when a full sweep changes
    # nothing, and at REFINE_ROUNDS regardless, since a cycle between two equal
    # arrangements would otherwise run forever.
    groups_keys = {}
    for k, o in chosen.items():
        base, _ = base_and_index(cells[k][2])
        groups_keys.setdefault(base, []).append(k)
    for _ in range(REFINE_ROUNDS):
        improved = False
        for base, keys in sorted(groups_keys.items()):
            if len(keys) < 2 or len(keys) > AMBIG_MAX:
                continue
            objs = [chosen[k] for k in keys]
            base_score, _, _ = agreement(pos, graph)
            best_perm, best_val = None, base_score
            for perm in itertools.permutations(objs):
                if perm == tuple(objs):
                    continue
                trial = dict(pos)
                for k, o in zip(keys, perm):
                    trial[o] = (cells[k][0], cells[k][1])
                val, _, _ = agreement(trial, graph)
                if val > best_val:
                    best_val, best_perm = val, perm
            if best_perm:
                for k, o in zip(keys, best_perm):
                    pos[o] = (cells[k][0], cells[k][1])
                    chosen[k] = o
                improved = True
        if not improved:
            break
    return pos, chosen, dropped


def incident(graph, pos):
    """{room: [(a, dirn, b)]} -- every exit either end of which is this room, so
    a move can be scored against exactly the edges it changes.

    Up and down are in the list as well as the compass. They have no axis to be
    on, but they do have a sense on the drawing -- a down exit is normally drawn
    downward -- and a move that reverses one is a move that redraws the map.
    Leaving them out is what let the nudge turn Zork I's canyon upside down:
    Canyon View, Rocky Ledge and Canyon Bottom descend by three down exits and
    nothing in the cardinal score knew it."""
    idx = {r: [] for r in pos}
    for a in pos:
        for dirn, (kind, dest) in graph[a]["exits"].items():
            if kind != "OPEN" or dest not in pos:
                continue
            if dirn not in HALF and dirn not in VERT:
                continue
            idx[a].append((a, dirn, dest))
            if dest != a:
                idx[dest].append((a, dirn, dest))
    return idx


def score_edges(edges, pos, sheet=None):
    """(on axis, in half-plane, vertical sense kept) at these positions.

    The third term counts up and down exits drawn the way they read, and only
    between two rooms on one sheet -- across sheets the two coordinates share no
    frame and the sign means nothing."""
    axis = half = vert = 0
    for a, dirn, b in edges:
        ax, ay = pos[a]
        bx, by = pos[b]
        dx, dy = bx - ax, by - ay
        if dirn in VERT:
            if sheet is not None and sheet[a] != sheet[b]:
                continue
            if VERT[dirn](dx, dy):
                vert += 1
            continue
        if HALF[dirn](dx, dy):
            half += 1
        if dirn in AXIS and AXIS[dirn](dx, dy):
            axis += 1
    return axis, half, vert


def nudge(pos, page, graph, frozen=frozenset()):
    """Move rooms onto their exits' axes, one lane at a time.

    The lanes come off a drawing, and a drawing is not a lattice: a room printed
    a few points out of true acquires a sideways step on every straight corridor
    through it. This walks the rooms in object order and tries each one at up to
    NUDGE_SPAN lanes either side along each axis, keeping a move only when it
    puts MORE cardinal exits on their own axis, takes NONE out of the half-plane
    their direction names, and reverses NO up or down exit drawn between two
    rooms of one sheet. Those last two are what stop it trading a real relation
    for a cosmetic straight line: the half-plane term is why the drop rule
    downstream cannot be made worse by running this, and the vertical one was
    added after it turned Zork I's canyon upside down, putting Canyon Bottom
    above Rocky Ledge in order to straighten something else.

    Only edges touching the moved room change, so each trial is scored on those
    rather than on the whole graph. Cells are kept unique per page: two rooms in
    one cell would draw one mark and lose the other, which is a worse fault than
    a bent corridor.

    Greedy and deterministic. It is not looking for the best layout -- that is a
    placement problem with the drawing as its evidence, and the drawing has
    already been read -- only for the moves that are obviously right.

    `frozen` holds rooms that may not move. It is empty for a table built in one
    go and holds the measured rooms when a scan is being filled in, where the
    whole point is that a coordinate read off Infocom's drawing survives the
    addition of the rooms around it. A frozen room is still an ANCHOR: its edges
    are scored as they always were, so the rooms beside it settle against it.
    Skipping its edges as well as its position would leave every room next to a
    measured one unscored, which is most of them.
    """
    idx = incident(graph, pos)
    taken = {(page[r], x, y) for r, (x, y) in pos.items()}
    moved = 0
    for _ in range(NUDGE_ROUNDS):
        gained = 0
        for room in sorted(pos):
            if room in frozen:
                continue
            edges = idx[room]
            if not edges:
                continue
            base_axis, base_half, base_vert = score_edges(edges, pos, page)
            ox, oy = pos[room]
            best = None
            for axis_i in (0, 1):
                for step in range(-NUDGE_SPAN, NUDGE_SPAN + 1):
                    if step == 0:
                        continue
                    nx = ox + (step if axis_i == 0 else 0)
                    ny = oy + (step if axis_i == 1 else 0)
                    if not (CELL_MIN <= nx <= CELL_MAX and
                            CELL_MIN <= ny <= CELL_MAX):
                        continue
                    if (page[room], nx, ny) in taken:
                        continue
                    pos[room] = (nx, ny)
                    a2, h2, v2 = score_edges(edges, pos, page)
                    pos[room] = (ox, oy)
                    if h2 < base_half or v2 < base_vert or a2 <= base_axis:
                        continue
                    key = (a2 - base_axis, h2 - base_half, -abs(step))
                    if best is None or key > best[0]:
                        best = (key, nx, ny)
            if best is None:
                continue
            _, nx, ny = best
            taken.discard((page[room], ox, oy))
            taken.add((page[room], nx, ny))
            pos[room] = (nx, ny)
            gained += 1
            moved += 1
        if gained == 0:
            break
    return moved


def free_cell(taken, x, y):
    """The nearest unclaimed cell to (x, y), searched outward in rings.

    The same bargain map_model.c's place() makes at runtime and for the same
    reason: two rooms in one cell draw one mark and lose the other, which is a
    worse fault than a room a step off where its exit points.
    """
    if (x, y) not in taken:
        return x, y
    r = 1
    while r <= FILL_SPAN:
        for dx in range(-r, r + 1):
            for dy in (-r, r):
                if (x + dx, y + dy) not in taken:
                    return x + dx, y + dy
        for dy in range(-r + 1, r):
            for dx in (-r, r):
                if (x + dx, y + dy) not in taken:
                    return x + dx, y + dy
        r += 1
    return x, y


def walk_seed(graph, floors):
    """Lay every room out by walking the story's own exits.

    The seed a story with no printed map gets. Breadth-first from the
    lowest-numbered room of each floor, one cell per compass direction, with a
    taken cell resolved by free_cell. One floor at a time, because floors stand
    on each other's footprint and a cell is only owed to be unique within one.

    Every room is placed, including one nothing walks to -- a room reachable
    only through a conditional exit is its own island, and an island is exactly
    what a player most wants found for them. Islands are seeded at the origin of
    their own floor and grown from there.

    This is a seed and not a layout. On its own it reaches 80% of the half-plane
    agreement a scan gets, against 94% once repair() has been over it: an
    outward search for a free cell is free to put a room on the wrong side of
    the one it was reached from, and nothing here notices.
    """
    by_floor = {}
    for r, f in floors.items():
        by_floor.setdefault(f, []).append(r)

    pos = {}
    for f, rooms in sorted(by_floor.items()):
        members = set(rooms)
        taken, seen = set(), set()
        for start in sorted(rooms):
            if start in seen:
                continue
            pos[start] = free_cell(taken, 0, 0)
            taken.add(pos[start])
            seen.add(start)
            queue = [start]
            while queue:
                a = queue.pop(0)
                ax, ay = pos[a]
                for d, (kind, dest) in sorted(graph[a]["exits"].items()):
                    if kind != "OPEN" or d not in STEP:
                        continue
                    if dest not in members or dest in seen or dest not in graph:
                        continue
                    dx, dy = STEP[d]
                    pos[dest] = free_cell(taken, ax + dx, ay + dy)
                    taken.add(pos[dest])
                    seen.add(dest)
                    queue.append(dest)
    return pos


def fill_seed(graph, placed, page):
    """Place every room the scan missed, without moving one it found.

    A missing room hangs off a placed neighbour, one cell along the exit that
    reaches it, with a taken cell on its own page resolved outward. Sweeps until
    nothing more can be reached, because a room two hops from anything placed
    becomes reachable once the room between them is down.

    What is left after that is an island the placed set cannot reach at all --
    a room behind a conditional exit, usually -- and it is seeded at the origin
    of its own page for the next sweep to grow from. Those are the rooms whose
    coordinates mean least, and there is nothing better available: nothing
    placed is near enough to say where they go.
    """
    pos = dict(placed)
    taken = {}
    for r, (x, y) in pos.items():
        taken.setdefault(page[r], set()).add((x, y))

    while True:
        grew = False
        for a in sorted(pos):
            for d, (kind, dest) in sorted(graph[a]["exits"].items()):
                if kind != "OPEN" or d not in STEP or dest in pos:
                    continue
                if dest not in graph:
                    continue
                dx, dy = STEP[d]
                p = page[dest]
                cell = free_cell(taken.setdefault(p, set()),
                                 pos[a][0] + dx, pos[a][1] + dy)
                pos[dest] = cell
                taken[p].add(cell)
                grew = True
        if grew:
            continue
        rest = [r for r in sorted(graph) if r not in pos]
        if not rest:
            return pos
        r = rest[0]
        p = page[r]
        cell = free_cell(taken.setdefault(p, set()), 0, 0)
        pos[r] = cell
        taken[p].add(cell)


def repair(pos, page, graph, frozen=frozenset()):
    """Move rooms onto the correct SIDE of their neighbours.

    The counterpart nudge() cannot be. nudge may only ever raise the number of
    exits on their own axis and is forbidden from losing a half-plane
    agreement -- that guard is what makes it safe to run on a measured table --
    so by construction it cannot repair a half-plane the seed got wrong. A seed
    that resolves a taken cell by searching outward gets plenty wrong: a room
    displaced two cells the wrong way is still placed, still unique, and now
    contradicts the exit that reached it.

    Same greedy shape as nudge, scored the other way round: half-plane first,
    axis as the tie-break. It carries nudge's vertical guard for the reason
    nudge carries it -- without it this pass turned a canyon over, putting the
    bottom above the top to straighten something else.

    Ordering is object order and the search is a square rather than two axes,
    since a room on the wrong side is usually wrong in both.

    `frozen` is nudge's, for nudge's reason: a measured coordinate does not move
    to tidy an inferred one.
    """
    idx = incident(graph, pos)
    taken = {(page[r], x, y) for r, (x, y) in pos.items()}
    moved = 0
    for _ in range(REPAIR_ROUNDS):
        gained = 0
        for room in sorted(pos):
            if room in frozen:
                continue
            edges = idx[room]
            if not edges:
                continue
            base_axis, base_half, base_vert = score_edges(edges, pos, page)
            ox, oy = pos[room]
            best = None
            for dx in range(-REPAIR_SPAN, REPAIR_SPAN + 1):
                for dy in range(-REPAIR_SPAN, REPAIR_SPAN + 1):
                    if dx == 0 and dy == 0:
                        continue
                    nx, ny = ox + dx, oy + dy
                    if not (CELL_MIN <= nx <= CELL_MAX and
                            CELL_MIN <= ny <= CELL_MAX):
                        continue
                    if (page[room], nx, ny) in taken:
                        continue
                    pos[room] = (nx, ny)
                    a2, h2, v2 = score_edges(edges, pos, page)
                    pos[room] = (ox, oy)
                    if v2 < base_vert or (h2, a2) <= (base_half, base_axis):
                        continue
                    key = (h2 - base_half, a2 - base_axis, -abs(dx) - abs(dy))
                    if best is None or key > best[0]:
                        best = (key, nx, ny)
            if best is None:
                continue
            _, nx, ny = best
            taken.discard((page[room], ox, oy))
            taken.add((page[room], nx, ny))
            pos[room] = (nx, ny)
            gained += 1
            moved += 1
        if gained == 0:
            break
    return moved


def merge_pages(graph, placed):
    """A page for every room, inventing none.

    The paging rule for filling a measured table in, and the reason the fill is
    safe at all. A story's floors are (sheet, level) pairs: the sheet is which
    drawing the room was read off, which only a scan knows, and the level comes
    from the routes. A room the scan missed has no sheet -- and the shipped
    table does not record sheets, only the densified pair -- so the sheet cannot
    be inherited and the floors cannot be re-derived. Trying anyway took one
    game to thirty-five floors against a ceiling of sixteen.

    What CAN be recovered is the level, from the story alone. A room in the same
    level-exit component as a placed room is on that room's floor by definition:
    same storey, and since it is joined to it without going up or down, the same
    drawing. So it takes that page. A room sharing a level with nothing placed
    -- a whole storey the scan missed -- falls back to the page of the nearest
    placed room through any exit, which is a guess, and is counted separately so
    the header can say how many it made.

    Because no page is created, the floor count cannot change, no placed room
    can change page, and MAP_ATLAS_PAGE_MAX cannot be breached. Those are the
    three things the sheet-inheritance rule could not promise.
    """
    parent = {r: r for r in graph}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for a in graph:
        for d, (kind, dest) in graph[a]["exits"].items():
            if d in LEVEL_DIRS and kind != "BLOCKED" and dest in parent:
                ra, rb = find(a), find(dest)
                if ra != rb:
                    parent[rb] = ra

    # Which page each level holds, by majority: a component can contain rooms
    # the scan put on two pages, and the one with more of them is the drawing
    # the level is mostly on.
    votes = {}
    for r, p in placed.items():
        if r in parent:
            votes.setdefault(find(r), {}).setdefault(p, 0)
            votes[find(r)][p] += 1
    level_page = {c: max(sorted(v), key=lambda p: (v[p], -p))
                  for c, v in votes.items()}

    page = dict(placed)
    by_level = 0
    for r in sorted(graph):
        if r in page:
            continue
        c = find(r)
        if c in level_page:
            page[r] = level_page[c]
            by_level += 1

    fallback = 0
    frontier = sorted(page)
    while frontier:
        nxt = []
        for a in frontier:
            for d, (kind, dest) in sorted(graph[a]["exits"].items()):
                if kind == "BLOCKED" or dest not in graph or dest in page:
                    continue
                page[dest] = page[a]
                fallback += 1
                nxt.append(dest)
        frontier = nxt

    # An island nothing placed can reach at all. It has to go somewhere and no
    # page is better than another, so it goes on the first.
    first = min(placed.values()) if placed else 0
    for r in graph:
        if r not in page:
            page[r] = first
            fallback += 1
    return page, by_level, fallback


def storeys(graph, sheet):
    """{room: floor index} -- one floor per vertical step of the story's own
    routes, within one drawn sheet.

    A page of the published map is not a floor. Infocom drew The Lurking
    Horror's seven-odd levels on two sheets, in oblique projection so the
    stacking reads on paper, and marked the underground ones by shading them; a
    port that pages by sheet offers two floors for a building you climb. What
    says which level a room is on is the story: rooms joined by a LEVEL_DIRS
    exit are on one floor and up and down are the only exits that leave it.

    So the floors are the connected components of the level-exit graph -- taken
    over the whole story, since two placed rooms joined through an unplaced one
    are still on one floor -- ordered by the vertical exits between them.

    Paired with the sheet rather than replacing it, and that is the whole
    subtlety. Coordinates were measured per sheet and each sheet was then
    dropped into its own band of rows, so two rooms off different sheets share
    no frame; re-paging by level alone put eleven floors across two or three
    sheets at once, Zork I's largest spanning all three with forty-five rows of
    nothing in the middle. (sheet, level) keeps every floor inside the one
    drawing it was read from.

    Ordering is by sheet then by height, so paging left and right walks the book
    the way it was bound and then climbs. Levels are numbered from the largest
    component outward by breadth, not by longest path: a vertical cycle -- Zork
    I's coal mine has two the ordering cannot honour -- inflates a longest path
    without bound, and put Zork I on a level twenty-five.
    """
    parent = {r: r for r in graph}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for a in graph:
        for d, (kind, dest) in graph[a]["exits"].items():
            if d in LEVEL_DIRS and kind != "BLOCKED" and dest in parent:
                ra, rb = find(a), find(dest)
                if ra != rb:
                    parent[rb] = ra
    comp = {r: find(r) for r in graph}

    above, below = {}, {}
    for a in graph:
        for d, (kind, dest) in graph[a]["exits"].items():
            if kind == "BLOCKED" or dest not in comp:
                continue
            ca, cb = comp[a], comp[dest]
            if ca == cb:
                continue
            hi, lo = (cb, ca) if d == "up" else (ca, cb) if d == "down" else (None, None)
            if hi is None:
                continue
            above.setdefault(lo, set()).add(hi)
            below.setdefault(hi, set()).add(lo)

    size = {}
    for r in comp:
        size[comp[r]] = size.get(comp[r], 0) + 1
    height = {}
    for seed in sorted(size, key=lambda c: (-size[c], c)):
        if seed in height:
            continue
        height[seed] = 0
        queue = [seed]
        while queue:
            c = queue.pop(0)
            for hi in sorted(above.get(c, ())):
                if hi not in height:
                    height[hi] = height[c] + 1
                    queue.append(hi)
            for lo in sorted(below.get(c, ())):
                if lo not in height:
                    height[lo] = height[c] - 1
                    queue.append(lo)

    floor = {r: (sheet[r], height[comp[r]]) for r in sheet if r in comp}
    for r in sheet:
        if r not in floor:
            floor[r] = (sheet[r], 0)
    dense = {k: i for i, k in enumerate(sorted(set(floor.values())))}
    unhonoured = sum(1 for lo in above for hi in above[lo]
                     if height[hi] - height[lo] != 1)
    return {r: dense[v] for r, v in floor.items()}, len(dense), unhonoured


def build_game(story, pdf, verbose=False, tol=LANE_TOLS[0],
               nudge_on=True):
    """(pos, release, serial, stats) for one game, or (None, ...) if it fails."""
    graph, release, serial = room_graph(os.path.join(Z3DIR, story + ".Z3"))

    # An object with no short name is not a room -- Zork I's object 82 is one,
    # with a single exit leading to itself. Leaving it in the name set made every
    # box whose OCR read nothing match it, which passed two of Zork I's
    # non-map pages through the named-box gate and then placed their contentless
    # boxes as if they were rooms.
    names = {norm(r["name"]) for r in graph.values()} - {""}

    doc = pymupdf.open(pdf)
    cache = _PAGE_CACHE
    pages = []
    for page in range(1, doc.page_count + 1):
        named, total, mode = page_items(pdf, page, names, cache)
        if len(named) < MIN_BOXES:
            continue
        if mode == "box" and len(named) < MIN_NAMED * total:
            continue
        pages.append((page, named, total, mode))

    cells, used_pages, y_base = {}, [], 0
    for seq, (page, named, total, mode) in enumerate(pages):
        cx = [b[0] + b[2] / 2.0 for b in named]
        cy = [b[1] + b[3] / 2.0 for b in named]
        col, row = snap(cx, tol), snap(cy, tol)
        for i, b in enumerate(named):
            cells[(seq, i)] = (col[i], row[i] + y_base, b[4])
        y_base += max(row.values()) + 3
        used_pages.append((page, total, len(named)))

    if not cells:
        return None, release, serial, {"reason": "no page yielded named boxes"}

    pos, chosen, dropped = assign(cells, graph)
    if not pos:
        return None, release, serial, {"reason": "no box matched a room"}

    # Which drawn page each room came off. A page is a floor: the publisher
    # split the map because the geography did, so above ground, the dungeon and
    # the coal mine are separate drawings and separate coordinate spaces stacked
    # into one only because the table has nowhere else to put them. The runtime
    # shows one at a time, so it needs to know which.
    page = {o: k[0] for k, o in chosen.items()}

    cs = [c for c, _ in pos.values()]
    rs = [r for _, r in pos.values()]
    ox, oy = (min(cs) + max(cs)) // 2, (min(rs) + max(rs)) // 2
    pos = {k: (c - ox, r - oy) for k, (c, r) in pos.items()}

    seen, uniq = set(), {}
    for k, v in sorted(pos.items()):
        if v in seen:
            continue
        seen.add(v)
        uniq[k] = v
    pos = uniq
    pos = {k: v for k, v in pos.items()
           if CELL_MIN <= v[0] <= CELL_MAX and CELL_MIN <= v[1] <= CELL_MAX}
    page = {k: v for k, v in page.items() if k in pos}

    # Renumber densely from zero. A page whose every room lost the coordinate
    # clamp or a duplicate-cell tie leaves a hole otherwise, and the runtime
    # counts pages by the largest index it sees -- a hole would offer the player
    # an empty floor to page onto.
    order = {p: i for i, p in enumerate(sorted(set(page.values())))}
    page = {k: order[v] for k, v in page.items()}

    # A plan is often drawn square to the building rather than to the compass.
    # The Witness is: as printed, all eight of its testable exits disagree, and
    # every one of them is half of a reciprocal pair, which is the signature of
    # a whole map turned rather than of rooms misplaced. Turned a quarter, all
    # eight agree.
    #
    # Only the four rotations are tried. Reflections are not, because a mirrored
    # map is not a thing a publisher prints, and offering eight candidates
    # instead of four doubles the chance of a small exit set fitting one by
    # accident. For the same reason a turn is only accepted on ORIENT_MIN exits
    # or more, and ties go to the map as drawn.
    ROTATIONS = (("as drawn", lambda x, y: (x, y)),
                 ("quarter",  lambda x, y: (-y, x)),
                 ("half",     lambda x, y: (-x, -y)),
                 ("three-quarter", lambda x, y: (y, -x)))
    agreed, tested, bad = agreement(pos, graph)
    orient = "as drawn"
    if tested >= ORIENT_MIN:
        for label, fn in ROTATIONS[1:]:
            turned = {k: fn(*v) for k, v in pos.items()}
            a2, t2, b2 = agreement(turned, graph)
            if a2 > agreed:
                pos, agreed, tested, bad, orient = turned, a2, t2, b2, label
    nudged = nudge(pos, page, graph) if nudge_on else 0
    page, nfloors, unhonoured = storeys(graph, page)
    if nudged:
        agreed, tested, bad = agreement(pos, graph)
    rate = (agreed / tested) if tested else 0.0
    aligned, atested, abad = alignment(pos, graph)
    stats = {"rooms": len(pos), "agreed": agreed, "tested": tested,
             "rate": rate, "bad": bad, "pages": used_pages, "dropped": dropped,
             "orient": orient, "page": page, "npages": nfloors,
             "sheets": len(order), "unhonoured": unhonoured,
             "aligned": aligned, "atested": atested, "abad": abad,
             "arate": (aligned / atested) if atested else 0.0,
             "tol": tol, "nudged": nudged,
             "names": {k: graph[k]["name"] for k in pos}}
    if tested == 0 or rate < PASS_RATE:
        stats["reason"] = f"only {agreed}/{tested} exits agree ({rate:.0%})"
        return None, release, serial, stats
    return pos, release, serial, stats


def build_walked(story):
    """(pos, release, serial, stats) for a story with no printed map.

    The same pipeline the scanned games take, with the seed swapped: the exit
    graph in place of the drawing, then repair, then the same nudge and the same
    scoring. Floors come from storeys() over a single sheet, since there is no
    book to have been bound into sheets -- which is why a walked table's floors
    are the story's own levels and nothing else.

    Returns pos None when the layout misses PASS_RATE, so the caller drops it
    exactly as it drops a scan that cannot be read. A story dropped here keeps
    the behaviour it has today: the map shows what the player has walked.
    """
    graph, release, serial = room_graph(os.path.join(Z3DIR, story + ".Z3"))
    floors, npages, unhonoured = storeys(graph, {r: 0 for r in graph})
    pos = walk_seed(graph, floors)
    pos = {k: v for k, v in pos.items()
           if CELL_MIN <= v[0] <= CELL_MAX and CELL_MIN <= v[1] <= CELL_MAX}
    page = {k: floors[k] for k in pos}
    repaired = repair(pos, page, graph)
    nudged = nudge(pos, page, graph)

    agreed, tested, bad = agreement(pos, graph)
    aligned, atested, abad = alignment(pos, graph)
    rate = agreed / tested if tested else 0.0
    stats = {
        "rooms": len(pos), "npages": len(set(page.values())),
        "agreed": agreed, "tested": tested, "rate": rate,
        "aligned": aligned, "atested": atested,
        "arate": aligned / atested if atested else 0.0,
        "unhonoured": unhonoured, "page": page,
        "names": {r: graph[r]["name"] for r in pos},
        "bad": bad, "abad": abad,
        "repaired": repaired, "nudged": nudged, "derived": True,
    }
    if tested and rate < PASS_RATE:
        stats["reason"] = f"walked layout agrees with only {rate:.0%} of its exits"
        return None, release, serial, stats
    if not tested:
        stats["reason"] = "no compass exit between two placed rooms to score"
        return None, release, serial, stats
    return pos, release, serial, stats


def build_merged(story, entry):
    """(pos, page, stats) for a measured table with its missing rooms walked in.

    The measured cells anchor it: they keep their coordinate and their page, and
    both refinement passes are told not to touch them. What the scan missed is
    filled in around them and then repaired against them.

    Asserts the anchors survived rather than trusting the frozen set, because
    this is the one promise the whole stage rests on and it costs a comparison.
    That assert runs on every regeneration, not only the first.

    Returns pos None when the filled table would miss PASS_RATE, in which case
    the caller keeps the measured table exactly as it stands: a merge that makes
    a map worse than the one already shipping is not worth its extra rooms.
    """
    graph, release, serial = room_graph(os.path.join(Z3DIR, story + ".Z3"))
    anchors = {r: (c[1], c[2]) for r, c in entry["cells"].items() if r in graph}
    apage = {r: entry["cells"][r][0] for r in anchors}
    if not anchors:
        return None, {"reason": "no carried cell names a room in this story"}

    page, by_level, fallback = merge_pages(graph, apage)
    pos = fill_seed(graph, anchors, page)
    frozen = set(anchors)
    repaired = repair(pos, page, graph, frozen=frozen)
    nudged = nudge(pos, page, graph, frozen=frozen)

    for r, cell in anchors.items():
        assert pos[r] == cell, (
            f"{story} room {r} moved from {cell} to {pos[r]}: a measured "
            "coordinate was not held")
        assert page[r] == apage[r], (
            f"{story} room {r} changed page from {apage[r]} to {page[r]}")
    assert len(set(page.values())) == len(set(apage.values())), (
        f"{story} gained a floor, which merge_pages must never do")

    pos = {r: v for r, v in pos.items()
           if CELL_MIN <= v[0] <= CELL_MAX and CELL_MIN <= v[1] <= CELL_MAX}
    page = {r: page[r] for r in pos}
    agreed, tested, bad = agreement(pos, graph)
    aligned, atested, abad = alignment(pos, graph)
    rate = agreed / tested if tested else 0.0
    stats = {
        "rooms": len(pos), "npages": len(set(page.values())),
        "agreed": agreed, "tested": tested, "rate": rate,
        "aligned": aligned, "atested": atested,
        "arate": aligned / atested if atested else 0.0,
        "page": page, "names": {r: graph[r]["name"] for r in pos},
        "added": sorted(set(pos) - set(anchors)), "measured": len(anchors),
        "by_level": by_level, "fallback": fallback,
        "repaired": repaired, "nudged": nudged,
        "bad": bad, "abad": abad, "release": release, "serial": serial,
    }
    if rate < PASS_RATE:
        stats["reason"] = f"filled layout agrees with only {rate:.0%} of its exits"
        return None, stats
    return pos, stats


def carried(path):
    """Every table already in an emitted .inc, as the verbatim text of its block.

    A run that adds walked tables must not rebuild the measured ones: doing so
    needs the map scans, which are not in the repo and must not be, and it would
    put eighteen tables measured off paper at the mercy of a code change aimed
    at the ten that were not. Carrying the block as TEXT rather than as
    coordinates is what makes that guarantee cheap -- the emitted bytes are
    reproduced, headers and stats and all, so a diff of a walk run shows
    insertions and nothing else.

    Returns {} when the file does not exist yet, which is what a first
    generation from scratch sees.
    """
    if not os.path.exists(path):
        return {}
    text = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(
            r"(/\*-{10,}\n \| MAP_ATLAS_(\w+)\n.*?\n -{10,}\*/\n)"
            r"static const MapAtlasCell MAP_ATLAS_\2\[\] = \{\n(.*?)\n\};\n",
            text, re.S):
        cells = {}
        for line in m.group(3).splitlines():
            c = re.match(r"\s*\{\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}",
                         line)
            if c:
                cells[int(c.group(1))] = (int(c.group(2)), int(c.group(3)),
                                          int(c.group(4)))
        out[m.group(2)] = {"story": m.group(2), "header": m.group(1),
                           "cells": cells,
                           "block": m.group(1) + "static const MapAtlasCell "
                                    f"MAP_ATLAS_{m.group(2)}[] = {{\n"
                                    + m.group(3) + "\n};\n"}
    for m in re.finditer(
            r"\{ (\d+)u, \"([^\"]+)\", MAP_ATLAS_(\w+),.*?\n\s+(\d+) \},",
            text, re.S):
        t = out.get(m.group(3))
        if t is not None:
            t["release"] = int(m.group(1))
            t["serial"] = m.group(2)
            t["npages"] = int(m.group(4))
    return {k: v for k, v in out.items() if "release" in v}


def emit_cells(w, t, st):
    """One table's cells. Shared by both kinds, so a walked table cannot come
    out in a different shape from a measured one."""
    w(f"static const MapAtlasCell MAP_ATLAS_{t['story']}[] = {{\n")
    for room in sorted(t["pos"]):
        x, y = t["pos"][room]
        assert CELL_MIN <= x <= CELL_MAX and CELL_MIN <= y <= CELL_MAX, (
            f"{t['story']} room {room} at ({x},{y}) will not fit a signed char")
        # MapAtlasCell.room is an unsigned char. A story numbering its rooms
        # past 255 cannot be tabled at all, and would truncate in silence --
        # Seastalker and Suspect both reach 254, which is why this is asserted
        # rather than assumed.
        assert 0 <= room <= 255, (
            f"{t['story']} room {room} will not fit MapAtlasCell.room")
        nm = st["names"].get(room, "")
        # A cell walked into an otherwise measured table is marked, so a reader
        # picking a coordinate out of the list can see which kind it is without
        # counting back to the header.
        mark = ADDED_MARK + " " if room in st.get("added", ()) else ""
        w(f"    {{ {room:3d}, {st['page'][room]:2d}, {x:4d}, {y:4d} }},"
          f"   /* {mark}{nm} */\n")
    w("};\n")


def emit_merged(w, t, st):
    """A measured table with the rooms its scan missed walked in.

    The scan's own header is reproduced and appended to rather than replaced:
    the lane tolerance, the drawn pages and the exits it could not satisfy are
    the only record of how the measured half was obtained, and a merge has no
    business throwing them away to describe itself. The paragraph added at the
    end says what the merge did and how each added room got its page.
    """
    head, tail = t["header"].split(" | Author: suinevere\n", 1)
    w("\n" + head)
    w(" |\n")
    w(f" |   FILLED IN: {len(st['added'])} walked in beside {st['measured']} measured. The\n")
    w(f" |   scan read what the OCR could find on the drawing; the rest of the\n")
    w(f" |   story's rooms are placed by stepping out from the ones it did find,\n")
    w(f" |   then repaired and nudged against them. Every measured coordinate and\n")
    w(f" |   page above is unchanged and asserted so on each regeneration; the added\n")
    w(f" |   cells are marked '{ADDED_MARK}' in the list below.\n")
    w(f" |   No floor was created: {st['by_level']} added room(s) took the page of a placed\n")
    w(f" |   room on their own level, which is the same sheet and storey by\n")
    w(f" |   definition, and {st['fallback']} took the page of the nearest placed room\n")
    w(f" |   through any exit, which is a guess and the only one here.\n")
    w(f" |   With them the table agrees with {st['agreed']} of {st['tested']} compass exits ({st['rate']:.0%})\n")
    w(f" |   and draws {st['aligned']} of {st['atested']} cardinal ones on axis ({st['arate']:.0%}).\n")
    w(f" |   {st['repaired']} added room(s) moved onto the right side of a neighbour and\n")
    w(f" |   {st['nudged']} onto an exit's own axis.\n")
    w(" | Author: suinevere\n")
    w(tail)
    emit_cells(w, t, st)


def emit_walked(w, t, st):
    """One table laid out from the story's exit graph, and its header.

    A separate header rather than the measured one with fields left blank: a
    walked table has no lane tolerance, no drawn sheet and no page of a book to
    cite, and printing those as zero would read as a measurement of zero.
    """
    w("\n/*----------------------\n")
    w(f" | MAP_ATLAS_{t['story']}\n")
    w(f" | Description: {t['story']}, release {t['release']} serial {t['serial']}.\n")
    w(f" |   {DERIVED_MARK} Infocom printed no map for this story that could be\n")
    w(f" |   scanned, so these {st['rooms']} rooms are laid out by walking the story's own\n")
    w(f" |   exits -- one cell per compass direction, breadth first, a taken cell\n")
    w(f" |   resolved by the nearest free one -- and then repaired and nudged by the\n")
    w(f" |   same passes a measured table gets. The coordinates are an inference from\n")
    w(f" |   the exit graph and evidence of nothing else: where two exits cannot both\n")
    w(f" |   be satisfied on a plane, which one gives way was decided here and not by\n")
    w(f" |   a draughtsman who had seen the game.\n")
    w(f" |   {st['agreed']} of {st['tested']} compass exits between two placed rooms land in the\n")
    w(f" |   half-plane their direction names ({st['rate']:.0%}), which is the test this was\n")
    w(f" |   accepted on, at the same PASS_RATE a scan has to clear. Of the cardinal\n")
    w(f" |   ones, {st['aligned']} of {st['atested']} are on their own axis ({st['arate']:.0%}).\n")
    w(f" |   {st['npages']} floor(s), one per vertical step of the story's own routes -- there\n")
    w(f" |   is no drawn sheet to pair them with, so a floor here is a level and\n")
    w(f" |   nothing else.\n")
    if st["unhonoured"]:
        w(f" |   {st['unhonoured']} vertical exit(s) do not fit one ordering of the levels,\n")
        w(f" |   which is a real loop in the geography rather than a fault.\n")
    w(f" |   {st['repaired']} room(s) moved onto the right side of a neighbour, then\n")
    w(f" |   {st['nudged']} moved onto an exit's own axis.\n")
    if st["bad"]:
        w(" |\n |   Exits the layout contradicts. On a measured table these are the\n")
        w(" |   drawing's exceptions; here they are rooms whose exits no plane can\n")
        w(" |   satisfy at once, which is the same fact with nobody to blame:\n")
        for a, d, b in st["bad"][:8]:
            w(f" |     {a} --{d}--> {b}\n")
        if len(st["bad"]) > 8:
            w(f" |     ... and {len(st['bad']) - 8} more\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    emit_cells(w, t, st)


def emit(tables, out):
    w = out.write
    w("/*----------------------\n")
    w(" | map_atlas_data.inc\n")
    w(" | Description: Generated by tools/gen_map_atlas.py -- do not edit. Authored\n")
    w(" |   room positions measured off Infocom's own maps and validated against each\n")
    w(" |   story's exit graph. See that script for the derivation and map_atlas.h\n")
    w(" |   for why an authored table exists.\n")
    w(" |     Not every table here was measured. A story Infocom printed no map for --\n")
    w(" |   or none that survives to be scanned -- is laid out from its own exit\n")
    w(" |   graph instead, and its header says so. Both kinds are the same bytes to\n")
    w(" |   the runtime and are held to the same PASS_RATE; what differs is what the\n")
    w(" |   coordinates are evidence of, which is why the distinction is written down\n")
    w(" |   rather than left to whoever reads the file next.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    for t in tables:
        # A table carried forward from the previous emit, reproduced byte for
        # byte. See carried(): a walk run must not put the measured tables at
        # the mercy of a change aimed at the walked ones.
        if "block" in t:
            w("\n" + t["block"])
            continue
        st = t["stats"]
        if st.get("added") is not None:
            emit_merged(w, t, st)
            continue
        if st.get("derived"):
            emit_walked(w, t, st)
            continue
        w("\n/*----------------------\n")
        w(f" | MAP_ATLAS_{t['story']}\n")
        w(f" | Description: {t['story']}, release {t['release']} serial {t['serial']}.\n")
        w(f" |   {st['rooms']} rooms on {st['npages']} floor(s) off {st['sheets']} drawn\n")
        w(f" |   sheet(s), ascending by object\n")
        w(f" |   number so map_atlas_pos can bisect. {st['agreed']} of {st['tested']} compass exits\n")
        w(f" |   between two placed rooms land in the half-plane their direction names\n")
        w(f" |   ({st['rate']:.0%}), which is the test the layout was scored and accepted on.\n")
        w(f" |   Of the cardinal ones, {st['aligned']} of {st['atested']} are drawn on their own axis\n")
        w(f" |   with no sideways component ({st['arate']:.0%}) -- the stricter thing a player\n")
        w(f" |   reads off the screen, reported rather than enforced because a plan drawn\n")
        w(f" |   square to a building is the publisher's and not an error.\n")
        w(f" |   A floor is one vertical step of the story's own routes inside one\n")
        w(f" |   drawn sheet, not a sheet -- see storeys().\n")
        if st["unhonoured"]:
            w(f" |   {st['unhonoured']} vertical exit(s) do not fit one ordering of the levels,\n")
            w(f" |   which is a real loop in the geography rather than a fault.\n")
        w(f" |   Read at a lane tolerance of {st['tol']}, chosen by sweep; {st['nudged']} room(s)\n")
        w(f" |   then moved a lane or two onto an exit's axis without losing any exit\n")
        w(f" |   from the half-plane its direction names.\n")
        if st["abad"]:
            w(" |\n |   Cardinal exits drawn off their axis:\n")
            for a, d, b, dx, dy in st["abad"][:8]:
                w(f" |     {a} --{d}--> {b} at ({dx:+d},{dy:+d})\n")
            if len(st["abad"]) > 8:
                w(f" |     ... and {len(st['abad']) - 8} more\n")
        if st["bad"]:
            w(" |\n |   The exceptions, which are rooms whose exits contradict any planar\n")
            w(" |   layout rather than misplacements:\n")
            for a, d, b in st["bad"][:8]:
                w(f" |     {a} --{d}--> {b}\n")
            if len(st["bad"]) > 8:
                w(f" |     ... and {len(st['bad']) - 8} more\n")
        w(" | Author: suinevere\n")
        w(" ----------------------*/\n")
        emit_cells(w, t, st)
    w("\n/*----------------------\n")
    w(" | MAP_ATLAS_STORIES / MAP_ATLAS_STORY_N\n")
    w(" | Description: Every story with an authored table, keyed by the release and\n")
    w(" |   serial in its Z-machine header. Object numbers are assigned by the\n")
    w(" |   compiler, so a table is only valid for the exact build it was derived\n")
    w(" |   from and both fields must match before it is used. The trailing count\n")
    w(" |   is how many floors the table spans, so a caller can offer the last\n")
    w(" |   floor without walking the cells.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    w("static const MapAtlasStory MAP_ATLAS_STORIES[] = {\n")
    for t in tables:
        npages = t["npages"] if "block" in t else t["stats"]["npages"]
        w(f'    {{ {t["release"]}u, "{t["serial"]}", MAP_ATLAS_{t["story"]},\n')
        w(f'      (unsigned short) (sizeof MAP_ATLAS_{t["story"]} /\n')
        w(f'                        sizeof MAP_ATLAS_{t["story"]}[0]),\n')
        w(f'      {npages} }},\n')
    w("};\n\n")
    w("#define MAP_ATLAS_STORY_N \\\n")
    w("    ((int) (sizeof MAP_ATLAS_STORIES / sizeof MAP_ATLAS_STORIES[0]))\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache",
                    help="directory for downloaded map PDFs; not part of the "
                         "repo. Required unless --walk is the whole run")
    ap.add_argument("--merge", action="store_true",
                    help="fill each measured table in with the rooms its scan "
                         "missed, anchoring every coordinate it did read. "
                         "Composes with --walk and needs no --cache either")
    ap.add_argument("--walk", action="store_true",
                    help="lay out the stories that have no map to scan from "
                         "their own exit graphs, carrying every table already "
                         "emitted forward untouched. Needs no --cache: the "
                         "measured tables are reproduced from the shipped .inc "
                         "rather than rebuilt, which is what lets this run "
                         "anywhere the repo does")
    ap.add_argument("--only", nargs="*", help="limit to these story stems")
    ap.add_argument("--report", action="store_true",
                    help="write the per-game report to stderr and emit nothing")
    ap.add_argument("--no-nudge", action="store_true",
                    help="place rooms exactly as the lanes read them, without "
                         "moving any onto its exits' axes; for measuring what "
                         "the nudge pass is worth")
    args = ap.parse_args()

    if args.walk or args.merge:
        return main_walk(args)
    if not args.cache:
        ap.error("--cache is required unless --walk is given")

    os.makedirs(args.cache, exist_ok=True)
    wanted = args.only or sorted(MAPS)
    tables, failed = [], []
    for story in wanted:
        if story not in MAPS:
            print(f"{story}: no map known", file=sys.stderr)
            continue
        if not os.path.exists(os.path.join(Z3DIR, story + ".Z3")):
            print(f"{story}: not on the disc", file=sys.stderr)
            continue
        pdf = os.path.join(args.cache, story + ".pdf")
        if not os.path.exists(pdf):
            subprocess.run(["curl", "-sSL", "-o", pdf, BASE_URL + MAPS[story]],
                           check=True)
        # Sweep the lane tolerance and keep the reading that puts the most
        # cardinal exits on their own axis. One number cannot serve every map --
        # they are printed at different scales -- and the cost of guessing it
        # wrong is a corridor drawn with a step in it. Ties go to the tightest
        # tolerance, so a game gains nothing from a looser reading it did not
        # need, and a tolerance that loses half-plane agreement is not taken
        # however straight it draws: that would be merging two columns Infocom
        # really did draw apart.
        pos = release = serial = stats = None
        for tol in LANE_TOLS:
            t_pos, t_rel, t_ser, t_st = build_game(story, pdf, tol=tol,
                                                   nudge_on=not args.no_nudge)
            if t_pos is None:
                if stats is None:
                    release, serial, stats = t_rel, t_ser, t_st
                continue
            if pos is None:
                pos, release, serial, stats = t_pos, t_rel, t_ser, t_st
                continue
            if (t_st["aligned"] > stats["aligned"] and
                    t_st["agreed"] >= stats["agreed"]):
                pos, release, serial, stats = t_pos, t_rel, t_ser, t_st
        if pos is None:
            failed.append((story, stats.get("reason", "?"), stats))
            print(f"  DROP {story:9s} {stats.get('reason','?')}", file=sys.stderr)
            continue
        tables.append({"story": story, "pos": pos, "release": release,
                       "serial": serial, "stats": stats})
        pages = ",".join(str(p) for p, _, _ in stats["pages"])
        turn = "" if stats.get("orient") == "as drawn" else             f"  turned {stats.get('orient')}"
        print(f"  OK   {story:9s} {stats['rooms']:4d} rooms  "
              f"{stats['agreed']:4d}/{stats['tested']:<4d} half "
              f"({stats['rate']:.0%})  "
              f"{stats['aligned']:4d}/{stats['atested']:<4d} on axis "
              f"({stats['arate']:.0%})  tol {stats['tol']:3d}  "
              f"nudged {stats['nudged']:3d}  "
              f"{stats['npages']:2d} floors off {stats['sheets']} sheet(s)  "
              f"pages {pages}{turn}",
              file=sys.stderr)

    print(f"\n{len(tables)} games pass, {len(failed)} dropped", file=sys.stderr)
    if args.report:
        return
    if not tables:
        print("nothing to emit", file=sys.stderr)
        sys.exit(1)
    emit(tables, sys.stdout)


def main_walk(args):
    """Add a table for every story on the disc that has no map to scan.

    The measured tables are carried forward as text, so this run touches
    nothing that was read off paper and needs neither the scans nor the cache
    they live in. Stories are emitted in stem order, which -- because the
    eighteen are already in that order -- makes the diff of a walk run a set of
    insertions and nothing else.

    Usage: python tools/gen_map_atlas.py --walk > saturn/src/engine/map_atlas_data.inc
    """
    keep = carried(INC_PATH)
    on_disc = sorted(f[:-3] for f in os.listdir(Z3DIR) if f.endswith(".Z3"))

    tables = []
    for stem in sorted(keep):
        entry = keep[stem]
        if not args.merge or not os.path.exists(
                os.path.join(Z3DIR, stem + ".Z3")):
            tables.append(dict(entry))
            continue
        pos, st = build_merged(stem, entry)
        if pos is not None and not st["added"]:
            # Nothing to add: the scan already reached every room, or the table
            # was walked out whole and is complete by construction. Rewriting it
            # to say so would append "beside N measured" to a header that opens
            # by saying it was never measured.
            tables.append(dict(entry))
            continue
        if pos is None:
            # The measured table stands. A fill that makes the map worse than
            # the one already shipping is not worth its extra rooms.
            print(f"  KEEP {stem:9s} {st.get('reason','?')}", file=sys.stderr)
            tables.append(dict(entry))
            continue
        tables.append({"story": stem, "pos": pos, "header": entry["header"],
                       "release": st["release"], "serial": st["serial"],
                       "stats": st})
        print(f"  FILL {stem:9s} {st['measured']:4d} measured + "
              f"{len(st['added']):3d} walked  "
              f"{st['agreed']:4d}/{st['tested']:<4d} half ({st['rate']:.0%})  "
              f"{st['aligned']:4d}/{st['atested']:<4d} on axis ({st['arate']:.0%})  "
              f"{st['npages']:2d} floors  "
              f"{st['by_level']} by level, {st['fallback']} by neighbour",
              file=sys.stderr)
    # Every story with no TABLE, which is not the same as every story with no
    # map. Four of the disc's stories have a scan that was read and then
    # rejected for disagreeing with their own exits -- Starcross among them, and
    # it is the one this was asked for. Keying off MAPS would skip exactly the
    # games whose drawing has already been tried and found wanting.
    wanted = (args.only or [s for s in on_disc if s not in keep]) if args.walk else []

    dropped = []
    for story in wanted:
        if story in keep:
            print(f"  keep {story:9s} already tabled", file=sys.stderr)
            continue
        if not os.path.exists(os.path.join(Z3DIR, story + ".Z3")):
            print(f"{story}: not on the disc", file=sys.stderr)
            continue
        pos, release, serial, st = build_walked(story)
        if pos is None:
            dropped.append(story)
            print(f"  DROP {story:9s} {st.get('reason','?')}", file=sys.stderr)
            continue
        tables.append({"story": story, "pos": pos, "release": release,
                       "serial": serial, "stats": st})
        print(f"  WALK {story:9s} {st['rooms']:4d} rooms  "
              f"{st['agreed']:4d}/{st['tested']:<4d} half ({st['rate']:.0%})  "
              f"{st['aligned']:4d}/{st['atested']:<4d} on axis ({st['arate']:.0%})  "
              f"repaired {st['repaired']:3d}  nudged {st['nudged']:3d}  "
              f"{st['npages']:2d} floors", file=sys.stderr)

    tables.sort(key=lambda t: t["story"])
    filled = sum(1 for t in tables if "stats" in t
                 and t["stats"].get("added") is not None)
    print(f"\n{len(tables)} tables ({len(keep)} carried, {filled} of them "
          f"filled in, {len(tables) - len(keep)} walked), "
          f"{len(dropped)} dropped", file=sys.stderr)
    if args.report:
        return
    emit(tables, sys.stdout)


if __name__ == "__main__":
    main()
