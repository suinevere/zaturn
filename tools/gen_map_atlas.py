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

Five stages.

1. Boxes. The scans are low contrast and printed over drawn stonework, so
   nothing thresholds cleanly -- on Zork I's page 3 the box interiors are darker
   than the background (mean 174 against 193). What separates them is that the
   rules framing a room are the only dark things running straight for tens of
   pixels, so opening the ink mask with long 1-D kernels keeps the rules and
   erases the stone. Room interiors are then the enclosed holes.

2. Names. OCR over each box, which on Zork I's page 3 reads 21 of 21 correctly
   including the map's own "(1)".."(4)" disambiguators. Those parentheticals are
   the map's numbering and do not exist in the game, but they do tell us two
   boxes are different rooms, which is worth keeping.

3. Assignment. Nine short names in Zork I are shared by two or more rooms, so a
   name alone cannot pick an object. Unambiguous names are assigned first; each
   ambiguous group is then resolved by trying every permutation and taking the
   one whose exits best agree with the rooms already placed. Groups larger than
   AMBIG_MAX are dropped rather than guessed -- that is what excludes the fifteen
   Maze rooms, whose drawn layout is an arbitrary embedding anyway.

4. Layout. Centres are clustered into ordered lanes per page rather than snapped
   to a fixed pitch, because Infocom's maps are not drawn on a uniform grid --
   aligned centres on Zork I's page 3 sit 147, 148 then 186 apart. Lanes preserve
   every left-of and above-of relation, which is all a compass direction is.
   Separate pages are separate drawings at different scales, so each is given its
   own band of rows below the last: above ground stays above underground.

5. Validation. For every open compass exit whose both ends are placed, the drawn
   offset must lie in that direction's half-plane. A game below PASS_RATE is
   reported and dropped.

Needs pymupdf, opencv-python, numpy and rapidocr-onnxruntime. Run by hand when
the maps or the story builds change, not by the Saturn build.
"""
import argparse
import difflib
import itertools
import json
import os
import struct
import subprocess
import sys

import cv2
import numpy as np
import pymupdf

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import zstory

DPI = 200
DIRW = ["north", "east", "west", "south", "ne", "nw", "se", "sw",
        "up", "down", "in", "out"]
FL_DIR, PROP_MAX = 0x10, 31

Z3DIR = "saturn/cd/data/Z3"
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

# A page must yield at least this many boxes that name a room the story actually
# has, and that share of all its boxes, before it is treated as a map page.
#
# MIN_NAMED is deliberately loose. It exists only to reject a cover or a legend
# where a stray word happens to match, and the real gate on a page is the
# PASS_RATE check at the end, which asks whether the placements agree with the
# story's own exits. Holding it at 0.55 rejected Moonmist, whose scan reads
# "riveuay" for driveway and "itchen" for kitchen: nineteen of its forty-one
# boxes resolve, which is ample geography, but under a half-share rule the page
# was thrown away whole.
MIN_BOXES = 8
MIN_NAMED = 0.25

# The largest group of identically-named rooms this will try to tell apart. 5!
# is 120 scorings and instant; 15! is the Maze and is not attempted.
AMBIG_MAX = 6

# How many refinement sweeps the assignment gets before it stops regardless.
REFINE_ROUNDS = 4

# The fewest testable exits before a map is allowed to be treated as turned.
# Below this there is not enough evidence to tell a real rotation from a small
# set of exits happening to fit one.
ORIENT_MIN = 6


# How close an OCR reading must be to a story room name before it is accepted
# as that room, and how far ahead of the next-best candidate it must be. The
# margin is what stops a garbled reading being confidently assigned to whichever
# name it happens to resemble most.
FUZZ_MIN = 0.70
FUZZ_MARGIN = 0.08

# A game whose placements disagree with its own exits more often than this is
# reported and dropped rather than shipped.
PASS_RATE = 0.85

HALF = {
    "north": lambda dx, dy: dy < 0, "south": lambda dx, dy: dy > 0,
    "east": lambda dx, dy: dx > 0, "west": lambda dx, dy: dx < 0,
    "ne": lambda dx, dy: dx > 0 and dy < 0, "nw": lambda dx, dy: dx < 0 and dy < 0,
    "se": lambda dx, dy: dx > 0 and dy > 0, "sw": lambda dx, dy: dx < 0 and dy > 0,
}

_OCR = None


def ocr_engine():
    """The OCR reader, built once -- construction loads models and is slow."""
    global _OCR
    if _OCR is None:
        from rapidocr_onnxruntime import RapidOCR
        _OCR = RapidOCR()
    return _OCR


def direction_props(raw):
    """{direction index: property number}, recovered the way room_model.c does.

    The dictionary's direction flag marks the entry; of the data bytes past the
    flags byte, the unique one in 1..31 is the property. Reading from the flags
    byte instead of past it counts the flags as a candidate and throws the whole
    recovery away, which is worth stating because it is exactly what went wrong
    the first time this was written.
    """
    da = struct.unpack(">H", raw[0x08:0x0A])[0]
    p = da + 1 + raw[da]
    elen = raw[p]
    n = struct.unpack(">H", raw[p + 1:p + 3])[0]
    p += 3
    props = {}
    for i in range(n):
        off = p + i * elen
        if not raw[off + 4] & FL_DIR:
            continue
        cand = [b for b in raw[off + 5:off + elen] if 1 <= b <= PROP_MAX]
        if len(cand) != 1:
            continue
        text = zstory.decode(raw, off).strip()
        if text in DIRW:
            props[DIRW.index(text)] = cand[0]
    return props


def room_graph(path):
    """({object: {direction: (kind, dest)}}, release, serial) for one story.

    Exits are decoded exactly as room_model_refresh_room does: a one-byte
    direction property is a plain destination, two bytes is a blocked message,
    anything longer is conditional.
    """
    st = zstory.Story(path)
    raw = st.raw
    p2d = {v: k for k, v in direction_props(raw).items()}
    graph = {}
    for o in st.objects:
        exits = {}
        for pnum, (addr, plen) in o.properties.items():
            d = p2d.get(pnum)
            if d is None:
                continue
            if plen == 1:
                exits[DIRW[d]] = ("OPEN", raw[addr])
            elif plen == 2:
                exits[DIRW[d]] = ("BLOCKED", 0)
            else:
                exits[DIRW[d]] = ("MAYBE", 0)
        if exits:
            graph[o.num] = {"name": o.name, "exits": exits}
    return (graph,
            struct.unpack(">H", raw[2:4])[0],
            raw[0x12:0x18].decode("ascii", "replace"))


def page_image(pdf, page):
    pix = pymupdf.open(pdf)[page - 1].get_pixmap(dpi=DPI)
    return np.frombuffer(pix.samples, np.uint8).reshape(pix.height, pix.width, 3)


def _boxes_at(img, invert):
    """Room boxes in one polarity: enclosed holes in the long straight rules."""
    gray = cv2.cvtColor(img, cv2.COLOR_RGB2GRAY)
    ink = ((gray > 135) if invert else (gray < 120)).astype(np.uint8) * 255
    line = cv2.getStructuringElement
    hor = cv2.morphologyEx(ink, cv2.MORPH_OPEN, line(cv2.MORPH_RECT, (22, 1)))
    ver = cv2.morphologyEx(ink, cv2.MORPH_OPEN, line(cv2.MORPH_RECT, (1, 22)))
    grid = cv2.dilate(cv2.bitwise_or(hor, ver), np.ones((3, 3), np.uint8))
    cnts, hier = cv2.findContours(grid, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
    out = []
    for i, c in enumerate(cnts):
        if hier[0][i][3] < 0:
            continue
        x, y, w, h = cv2.boundingRect(c)
        if not (45 <= w <= 460 and 28 <= h <= 460):
            continue
        if cv2.contourArea(c) < 0.72 * w * h:
            continue
        out.append((x, y, w, h))
    out.sort(key=lambda b: (b[1] // 40, b[0]))
    return out


def find_boxes(pdf, page):
    """Bounding rectangles of the room boxes drawn on one map page.

    Both polarities are tried and the one yielding more boxes is taken. Starcross
    is set aboard a starship and its map is drawn in negative -- white rules and
    white boxes on a black field -- so a detector that only looks for dark ink
    finds nothing at all on any of its three pages, and finds 139, 20 and 60
    boxes when inverted. Deciding by which reading yields more boxes rather than
    by how dark the page is avoids a threshold that would have to be right about
    photographs and cover art too.
    """
    img = page_image(pdf, page)
    a = _boxes_at(img, False)
    b = _boxes_at(img, True)
    return b if len(b) > len(a) else a


def read_boxes(pdf, page):
    """[(x, y, w, h, text)] for one page, text read by OCR from the box interior."""
    boxes = find_boxes(pdf, page)
    if not boxes:
        return []
    img = page_image(pdf, page)
    ocr = ocr_engine()
    out = []
    for (x, y, w, h) in boxes:
        crop = img[y + 2:y + h - 2, x + 2:x + w - 2]
        if crop.size == 0:
            continue
        crop = cv2.resize(crop, None, fx=3, fy=3, interpolation=cv2.INTER_CUBIC)
        res, _ = ocr(crop)
        txt = " ".join(r[1] for r in res) if res else ""
        out.append((x, y, w, h, " ".join(txt.split())))
    return out


def read_labels(pdf, page):
    """[(x, y, w, h, text)] for every text label on a page, boxes or not.

    Suspended, The Witness and The Lurking Horror are drawn as architectural
    floor plans: rooms are areas enclosed by walls, and the name is set as bare
    text inside the area rather than in a box of its own. There is nothing for
    the box finder to find -- Suspended's single page yields one box against
    forty-odd rooms -- so the label itself has to stand in for the room.

    Fragments are merged before they are returned, because a plan sets a long
    name on two or three lines inside a cramped space and OCR reports each line
    separately. Two join when they overlap horizontally and either sit inside
    six tenths of a line of each other or the first ends in a hyphen, which is
    what turns "Sterili-", "zation" and "Chamber" back into one room.

    The tolerance was a line and a half and had to come down: in a dense plan
    that reached past the wall into the next room and swallowed it, so The
    Witness returned one label reading "TUB Room BatHROOM ToIL" where the game
    has three separate rooms. Merging too eagerly does not just misname a room,
    it deletes the ones it absorbs.

    Note what this does not need: the walls. Adjacency is never read off the
    drawing -- the exits come from the story file, which is the authority and
    already knows which rooms connect. The plan is only being asked where each
    room sits, so a wall between two neighbours changes nothing here.
    """
    img = page_image(pdf, page)
    res, _ = ocr_engine()(img)
    if not res:
        return []
    frags = []
    for item in res:
        pts = item[0]
        xs = [q[0] for q in pts]
        ys = [q[1] for q in pts]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        frags.append([x0, y0, x1 - x0, y1 - y0, str(item[1]).strip()])
    frags.sort(key=lambda f: (f[1], f[0]))

    merged = []
    for f in frags:
        placed = False
        for m in merged:
            gap = f[1] - (m[1] + m[3])
            overlap = min(m[0] + m[2], f[0] + f[2]) - max(m[0], f[0])
            joins = m[4].endswith("-")
            close = -m[3] <= gap <= 0.6 * max(m[3], f[3])
            if (joins or close) and overlap > 0.6 * min(m[2], f[2]):
                nx0 = min(m[0], f[0])
                ny0 = min(m[1], f[1])
                nx1 = max(m[0] + m[2], f[0] + f[2])
                ny1 = max(m[1] + m[3], f[1] + f[3])
                m[0], m[1], m[2], m[3] = nx0, ny0, nx1 - nx0, ny1 - ny0
                m[4] = (m[4].rstrip("-") + f[4]) if m[4].endswith("-") else (m[4] + " " + f[4])
                placed = True
                break
        if not placed:
            merged.append(list(f))
    return [(int(m[0]), int(m[1]), int(m[2]), int(m[3]), " ".join(m[4].split()))
            for m in merged]


def page_items(pdf, page, names, cache):
    """The named rooms drawn on one page, read once and remembered.

    Boxes first, falling back to bare labels when the page has rooms but no
    boxes to put them in. Cached because build_game lays a game out more than
    one way and OCR is far and away the slowest thing here.
    """
    key = (pdf, page)
    if key in cache:
        return cache[key]

    def resolve(items):
        out = []
        for b in items:
            hit = match_name(base_and_index(b[4])[0], names)
            if hit:
                out.append((b[0], b[1], b[2], b[3], hit))
        return out

    boxes = read_boxes(pdf, page)
    named = resolve(boxes)
    total, mode = len(boxes), "box"
    if len(named) < MIN_BOXES:
        labels = read_labels(pdf, page)
        cand = resolve(labels)
        if len(cand) > len(named):
            named, total, mode = cand, len(labels), "label"
    cache[key] = (named, total, mode)
    return cache[key]


def norm(s):
    """A room name reduced to what two spellings of it have in common."""
    s = s.lower()
    s = "".join(ch if ch.isalnum() or ch.isspace() else " " for ch in s)
    return " ".join(s.split())


def base_and_index(text):
    """Split "Forest (2)" into ("forest", 2); plain names get index None.

    The parenthetical is the map's own disambiguation of rooms the game gives
    the same short name. It never identifies which object is which -- only that
    two boxes are not the same room.
    """
    t = text.strip()
    idx = None
    if t.endswith(")") and "(" in t:
        head, _, tail = t.rpartition("(")
        tail = tail[:-1].strip()
        if tail.isdigit():
            idx = int(tail)
        # Stripped either way. A map annotates a room as "Garage (2-car)" where
        # the game simply calls it Garage, and keeping the aside in the string
        # drops the match below the threshold.
        t = head
    return norm(t), idx


def match_name(text, names):
    """The story room name a box's OCR reading means, or None.

    Exact matching is not enough. Zork I's page 3 reads perfectly, but the
    scans vary and several games do not: Zork II gives "Insldo the tarrow" for
    Inside the Barrow and "Wost Ytowing Room" for West Viewing Room, Ballyhoo
    gives "Bupupls Rooml Ony" for Standing Room Only. Requiring an exact string
    dropped four games whose maps are perfectly good.

    What makes this tractable is that the answer is always one of a closed set
    of about a hundred names the story itself supplies, so this is a nearest
    match over a known vocabulary rather than open-ended reading. A match is
    taken only when it is both close enough in absolute terms and clearly ahead
    of the runner-up -- a confident wrong answer would place a room somewhere
    permanently, which is worse than not placing it at all.
    """
    if not text:
        return None
    best = second = 0.0
    pick = None
    for cand in names:
        r = difflib.SequenceMatcher(None, text, cand).ratio()
        if r > best:
            best, second, pick = r, best, cand
        elif r > second:
            second = r
    if best < FUZZ_MIN or best - second < FUZZ_MARGIN:
        return None
    return pick


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
    stats = {"rooms": len(pos), "agreed": agreed, "tested": tested,
             "rate": rate, "bad": bad, "pages": used_pages, "dropped": dropped,
             "orient": orient,
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
        w(f" |   {st['rooms']} rooms, ascending by object number so map_atlas_pos can\n")
        w(f" |   bisect. {st['agreed']} of {st['tested']} compass exits between two placed\n")
        w(f" |   rooms leave in the direction drawn ({st['rate']:.0%}).\n")
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
            w(f"    {{ {room:3d}, {x:4d}, {y:4d} }},   /* {nm} */\n")
        w("};\n")
    w("\n/*----------------------\n")
    w(" | MAP_ATLAS_STORIES / MAP_ATLAS_STORY_N\n")
    w(" | Description: Every story with an authored table, keyed by the release and\n")
    w(" |   serial in its Z-machine header. Object numbers are assigned by the\n")
    w(" |   compiler, so a table is only valid for the exact build it was derived\n")
    w(" |   from and both fields must match before it is used.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    w("static const MapAtlasStory MAP_ATLAS_STORIES[] = {\n")
    for t in tables:
        w(f'    {{ {t["release"]}u, "{t["serial"]}", MAP_ATLAS_{t["story"]},\n')
        w(f'      (unsigned short) (sizeof MAP_ATLAS_{t["story"]} /\n')
        w(f'                        sizeof MAP_ATLAS_{t["story"]}[0]) }},\n')
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
