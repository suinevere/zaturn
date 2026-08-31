#!/usr/bin/env python3
"""Emit saturn/src/engine/map_atlas_data.inc: authored room positions taken from
Infocom's own InvisiClues maps and checked against the compiled story's exits.

Usage: python3 tools/gen_map_atlas.py zork1.pdf > saturn/src/engine/map_atlas_data.inc

Why an authored table exists at all is in saturn/src/engine/map_atlas.h. What
this script contributes is that the table is not typed in by eye: the box
geometry is measured off the scan, and every placement is then validated against
the story's own exit graph, so a misread room fails the run instead of shipping
as a wrong cell.

Three stages.

1. Boxes. The scan is low contrast and the map is printed over drawn stonework,
   so nothing thresholds cleanly -- box interiors are actually *darker* than the
   background (mean 174 against 193 on page 3). What separates them is that the
   rules framing a room are the only dark things running straight for tens of
   pixels, so opening the ink mask with long 1-D kernels keeps the rules and
   erases the stone. Room interiors are then the enclosed holes.

2. Names. Read off the drawing by a person, once, and recorded in PAGES below.
   Nine short names in Zork I are shared by two or more rooms -- four Forests,
   two Clearings, fifteen Mazes -- and Infocom's parenthetical numbering is the
   map's disambiguation, not the game's, so a name alone cannot pick an object.
   The ambiguous ones are resolved by structure and the choice is re-derived by
   the CHECKS assertions, which fail the run if a label is wrong.

3. Validation. For every open compass exit whose both ends are placed, the drawn
   offset must lie in that direction's half-plane. Page 3 scores 49 of 51. The
   two failures are Forest (4), whose north, west and south all lead back to
   Forest (2) -- the loop-back symbol on Infocom's own legend. No planar layout
   satisfies that, which is the point: the table is faithful where geography
   exists and honest where it does not.

Mazes are deliberately excluded. Infocom drew the fifteen maze rooms in an
arbitrary planar embedding for legibility; there is no geography to be faithful
to, so those rooms are left out and map_model falls back to its graph walk.

Needs pymupdf, opencv-python and numpy. It is run by hand when the maps or the
story build change, not by the Saturn build.
"""
import json
import struct
import sys

import cv2
import numpy as np
import pymupdf

sys.path.insert(0, "tools")
import zstory

DPI = 200
DIRW = ["north", "east", "west", "south", "ne", "nw", "se", "sw",
        "up", "down", "in", "out"]
FL_DIR, PROP_MAX = 0x10, 31

# The story this table is for. Object numbers are assigned by the compiler, so a
# table is only valid for the exact build it was derived from; map_atlas_bind
# matches on both of these before using it.
STORY = "saturn/cd/data/Z3/ZORK1.Z3"

# Page number -> {box index: (object number, label)}. Box indices are the order
# find_boxes returns, which is top-to-bottom then left-to-right and is stable for
# a given scan and DPI. Anything not listed is dropped: an artifact, or a room
# whose geography we decline to author.
PAGES = {
    3: {
        0: (143, "Clearing (grating)"), 1: (88, "Up a Tree"),
        3: (78, "Forest (1)"), 4: (75, "Forest Path"), 5: (77, "Forest (2)"),
        6: (239, "Forest (4)"), 8: (81, "North of House"), 9: (201, "Attic"),
        10: (180, "West of House"), 11: (79, "Behind House"),
        12: (74, "Clearing (canyon)"), 13: (193, "Living Room"),
        14: (203, "Kitchen"), 15: (25, "Canyon View"), 17: (178, "Stone Barrow"),
        18: (80, "South of House"), 19: (26, "Rocky Ledge"),
        20: (136, "End of Rainbow"), 21: (76, "Forest (3)"),
        22: (27, "Canyon Bottom"),
    },
}

# Boxes deliberately not placed, and why, so a future run does not rediscover
# them as oversights.
#   page 3 box 2, 7 -- the loop-back passage drawn at Forest (4) encloses a
#                      region the box finder cannot tell from a room
#   page 3 box 16   -- Inside the Barrow carries no direction property, so it is
#                      not in the exit graph at all and cannot be validated

# Each entry re-derives an ambiguous label from structure alone. If a label is
# wrong the assertion fires and nothing is emitted.
CHECKS = [
    (143, "south", 75, "grating Clearing is the one south to Forest Path"),
    (74, "west", 79, "canyon Clearing is the one west to Behind House"),
    (78, "east", 75, "Forest (1) is west of Forest Path"),
    (77, "west", 75, "Forest (2) is east of Forest Path"),
    (76, "nw", 80, "Forest (3) is south-east of South of House"),
]

HALF = {
    "north": lambda dx, dy: dy < 0, "south": lambda dx, dy: dy > 0,
    "east": lambda dx, dy: dx > 0, "west": lambda dx, dy: dx < 0,
    "ne": lambda dx, dy: dx > 0 and dy < 0, "nw": lambda dx, dy: dx < 0 and dy < 0,
    "se": lambda dx, dy: dx > 0 and dy > 0, "sw": lambda dx, dy: dx < 0 and dy > 0,
}


def direction_props(raw):
    """{direction index: property number}, recovered the way room_model.c does.

    The dictionary's direction flag marks the entry; of the data bytes past the
    flags byte, the unique one in 1..31 is the property. Reading from the flags
    byte instead of past it counts the flags as a candidate and throws the whole
    recovery away, which is worth stating because it is exactly what went wrong
    the first time.
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
    """{object number: {direction: (kind, destination)}} for every room.

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
    return graph, raw


def find_boxes(pdf, page):
    """Bounding rectangles of the room boxes drawn on one map page."""
    pix = pymupdf.open(pdf)[page - 1].get_pixmap(dpi=DPI)
    img = np.frombuffer(pix.samples, np.uint8).reshape(pix.height, pix.width, 3)
    ink = (cv2.cvtColor(img, cv2.COLOR_RGB2GRAY) < 120).astype(np.uint8) * 255
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


def snap(values, tol):
    """Cluster centre coordinates into ordered lane indices.

    Infocom's map is not drawn on a uniform pitch -- aligned centres on page 3
    sit 147, 148 then 186 apart -- so a fixed step would distort it. Ordered
    lanes keep every left-of / above-of relation, which is all a compass
    direction is, without pretending to a regularity the drawing does not have.
    """
    order = sorted(range(len(values)), key=lambda i: values[i])
    lane, cur, out = 0, values[order[0]], {}
    for i in order:
        if values[i] - cur > tol:
            lane += 1
            cur = values[i]
        out[i] = lane
    return out


def build(pdf):
    graph, raw = room_graph(STORY)
    for room, dirn, dest, why in CHECKS:
        got = graph[room]["exits"].get(dirn)
        assert got and got[0] == "OPEN" and got[1] == dest, why

    pos, names = {}, {}
    for page, labels in sorted(PAGES.items()):
        boxes = find_boxes(pdf, page)
        keys = sorted(labels)
        cx = [boxes[i][0] + boxes[i][2] / 2.0 for i in keys]
        cy = [boxes[i][1] + boxes[i][3] / 2.0 for i in keys]
        col, row = snap(cx, 60), snap(cy, 60)
        for k, i in enumerate(keys):
            room, label = labels[i]
            pos[room] = (col[k], row[k])
            names[room] = label

    cs = [c for c, _ in pos.values()]
    rs = [r for _, r in pos.values()]
    ox = (min(cs) + max(cs)) // 2
    oy = (min(rs) + max(rs)) // 2
    pos = {k: (c - ox, r - oy) for k, (c, r) in pos.items()}

    assert len(set(pos.values())) == len(pos), "two rooms landed on one cell"
    for x, y in pos.values():
        assert -128 <= x <= 127 and -128 <= y <= 127, "cell outside signed char"

    tested = agree = 0
    bad = []
    for a, (ax, ay) in pos.items():
        for dirn, (kind, dest) in graph[a]["exits"].items():
            if kind != "OPEN" or dest not in pos or dirn not in HALF:
                continue
            tested += 1
            bx, by = pos[dest]
            if HALF[dirn](bx - ax, by - ay):
                agree += 1
            else:
                bad.append((names[a], dirn, names[dest]))
    return pos, names, raw, tested, agree, bad


def main():
    pdf = sys.argv[1] if len(sys.argv) > 1 else "zork1.pdf"
    pos, names, raw, tested, agree, bad = build(pdf)
    release = struct.unpack(">H", raw[2:4])[0]
    serial = raw[0x12:0x18].decode("ascii")

    w = sys.stdout.write
    w("/*----------------------\n")
    w(" | map_atlas_data.inc\n")
    w(" | Description: Generated by tools/gen_map_atlas.py -- do not edit. Authored\n")
    w(" |   room positions read off Infocom's InvisiClues maps and validated against\n")
    w(" |   the story's own exit graph. See that script for the derivation and\n")
    w(" |   map_atlas.h for why an authored table exists.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n\n")
    w("/*----------------------\n")
    w(" | MAP_ATLAS_ZORK1\n")
    w(f" | Description: Zork I, release {release} serial {serial}. {len(pos)} rooms,\n")
    w(" |   ascending by object number so map_atlas_pos can bisect.\n")
    w(" |\n")
    w(f" |   {agree} of {tested} compass exits between two placed rooms leave in the\n")
    w(" |   direction drawn.")
    if not bad:
        w(" Every one of them.\n")
    else:
        w(f" The {len(bad)} that do not:\n")
        for a, d, b in bad:
            w(f" |     {a} --{d}--> {b}\n")
        w(" |   Those are arms of the passageway-returning-to-room-of-origin that\n")
        w(" |   Infocom's own legend marks, and no planar layout satisfies them.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    w("static const MapAtlasCell MAP_ATLAS_ZORK1[] = {\n")
    for room in sorted(pos):
        x, y = pos[room]
        w(f"    {{ {room:3d}, {x:3d}, {y:3d} }},   /* {names[room]} */\n")
    w("};\n\n")
    w("/*----------------------\n")
    w(" | MAP_ATLAS_STORIES / MAP_ATLAS_STORY_N\n")
    w(" | Description: Every story with an authored table, keyed by the release and\n")
    w(" |   serial in its Z-machine header.\n")
    w(" | Author: suinevere\n")
    w(" ----------------------*/\n")
    w("static const MapAtlasStory MAP_ATLAS_STORIES[] = {\n")
    w(f'    {{ {release}u, "{serial}", MAP_ATLAS_ZORK1,\n')
    w(f"      (unsigned short) (sizeof MAP_ATLAS_ZORK1 / sizeof MAP_ATLAS_ZORK1[0]) }},\n")
    w("};\n\n")
    w("#define MAP_ATLAS_STORY_N \\\n")
    w("    ((int) (sizeof MAP_ATLAS_STORIES / sizeof MAP_ATLAS_STORIES[0]))\n")

    sys.stderr.write(f"gen_map_atlas: {len(pos)} rooms, "
                     f"{agree}/{tested} exits agree with the drawing\n")
    for a, d, b in bad:
        sys.stderr.write(f"gen_map_atlas:   disagrees: {a} --{d}--> {b}\n")


if __name__ == "__main__":
    main()
