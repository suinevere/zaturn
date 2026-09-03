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
import subprocess
import sys

import pymupdf

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from mapscan import (DPI, DIRW, FL_DIR, PROP_MAX, Z3DIR, MIN_BOXES, MIN_NAMED,
                     FUZZ_MIN, FUZZ_MARGIN, ocr_engine, direction_props,
                     room_graph, page_image, find_boxes, read_boxes,
                     read_labels, norm, base_and_index, match_name, page_items)

BASE_URL = "https://infodoc.plover.net/maps/"

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


def build_game(story, pdf, verbose=False):
    """(pos, release, serial, stats) for one game, or (None, ...) if it fails."""
    graph, release, serial = room_graph(os.path.join(Z3DIR, story + ".Z3"))

    # An object with no short name is not a room -- Zork I's object 82 is one,
    # with a single exit leading to itself. Leaving it in the name set made every
    # box whose OCR read nothing match it, which passed two of Zork I's
    # non-map pages through the named-box gate and then placed their contentless
    # boxes as if they were rooms.
    names = {norm(r["name"]) for r in graph.values()} - {""}

    doc = pymupdf.open(pdf)
    cache = {}
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
        col, row = snap(cx, 60), snap(cy, 60)
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
           if -128 <= v[0] <= 127 and -128 <= v[1] <= 127}
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
    rate = (agreed / tested) if tested else 0.0
    aligned, atested, abad = alignment(pos, graph)
    stats = {"rooms": len(pos), "agreed": agreed, "tested": tested,
             "rate": rate, "bad": bad, "pages": used_pages, "dropped": dropped,
             "orient": orient, "page": page, "npages": len(order),
             "aligned": aligned, "atested": atested, "abad": abad,
             "arate": (aligned / atested) if atested else 0.0,
             "names": {k: graph[k]["name"] for k in pos}}
    if tested == 0 or rate < PASS_RATE:
        stats["reason"] = f"only {agreed}/{tested} exits agree ({rate:.0%})"
        return None, release, serial, stats
    return pos, release, serial, stats


def emit(tables, out):
    w = out.write
    w("/*----------------------\n")
    w(" | map_atlas_data.inc\n")
    w(" | Description: Generated by tools/gen_map_atlas.py -- do not edit. Authored\n")
    w(" |   room positions measured off Infocom's own maps and validated against each\n")
    w(" |   story's exit graph. See that script for the derivation and map_atlas.h\n")
    w(" |   for why an authored table exists.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    for t in tables:
        st = t["stats"]
        w("\n/*----------------------\n")
        w(f" | MAP_ATLAS_{t['story']}\n")
        w(f" | Description: {t['story']}, release {t['release']} serial {t['serial']}.\n")
        w(f" |   {st['rooms']} rooms on {st['npages']} floor(s), ascending by object\n")
        w(f" |   number so map_atlas_pos can bisect. {st['agreed']} of {st['tested']} compass exits\n")
        w(f" |   between two placed rooms land in the half-plane their direction names\n")
        w(f" |   ({st['rate']:.0%}), which is the test the layout was scored and accepted on.\n")
        w(f" |   Of the cardinal ones, {st['aligned']} of {st['atested']} are drawn on their own axis\n")
        w(f" |   with no sideways component ({st['arate']:.0%}) -- the stricter thing a player\n")
        w(f" |   reads off the screen, reported rather than enforced because a plan drawn\n")
        w(f" |   square to a building is the publisher's and not an error.\n")
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
        w(f"static const MapAtlasCell MAP_ATLAS_{t['story']}[] = {{\n")
        for room in sorted(t["pos"]):
            x, y = t["pos"][room]
            nm = st["names"].get(room, "")
            w(f"    {{ {room:3d}, {st['page'][room]:2d}, {x:4d}, {y:4d} }},"
              f"   /* {nm} */\n")
        w("};\n")
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
        w(f'    {{ {t["release"]}u, "{t["serial"]}", MAP_ATLAS_{t["story"]},\n')
        w(f'      (unsigned short) (sizeof MAP_ATLAS_{t["story"]} /\n')
        w(f'                        sizeof MAP_ATLAS_{t["story"]}[0]),\n')
        w(f'      {t["stats"]["npages"]} }},\n')
    w("};\n\n")
    w("#define MAP_ATLAS_STORY_N \\\n")
    w("    ((int) (sizeof MAP_ATLAS_STORIES / sizeof MAP_ATLAS_STORIES[0]))\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", required=True,
                    help="directory for downloaded map PDFs; not part of the repo")
    ap.add_argument("--only", nargs="*", help="limit to these story stems")
    ap.add_argument("--report", action="store_true",
                    help="write the per-game report to stderr and emit nothing")
    args = ap.parse_args()

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
        pos, release, serial, stats = build_game(story, pdf)
        if pos is None:
            failed.append((story, stats.get("reason", "?"), stats))
            print(f"  DROP {story:9s} {stats.get('reason','?')}", file=sys.stderr)
            continue
        tables.append({"story": story, "pos": pos, "release": release,
                       "serial": serial, "stats": stats})
        pages = ",".join(str(p) for p, _, _ in stats["pages"])
        turn = "" if stats.get("orient") == "as drawn" else             f"  turned {stats.get('orient')}"
        print(f"  OK   {story:9s} {stats['rooms']:4d} rooms  "
              f"{stats['agreed']:4d}/{stats['tested']:<4d} exits "
              f"({stats['rate']:.0%})  pages {pages}{turn}", file=sys.stderr)

    print(f"\n{len(tables)} games pass, {len(failed)} dropped", file=sys.stderr)
    if args.report:
        return
    if not tables:
        print("nothing to emit", file=sys.stderr)
        sys.exit(1)
    emit(tables, sys.stdout)


if __name__ == "__main__":
    main()
