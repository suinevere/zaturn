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

DPI = 200
DIRW = ["north", "east", "west", "south", "ne", "nw", "se", "sw",
        "up", "down", "in", "out"]
FL_DIR, PROP_MAX = 0x10, 31

Z3DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "saturn", "cd", "data", "Z3")
"""Where the story files live, resolved from this file rather than from the
working directory, because every consumer opens it and a relative path made
each of them -- and every test that reaches one -- silently a script that only
runs from the repository root."""

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

# How close an OCR reading must be to a story room name before it is accepted
# as that room, and how far ahead of the next-best candidate it must be. The
# margin is what stops a garbled reading being confidently assigned to whichever
# name it happens to resemble most.
FUZZ_MIN = 0.70
FUZZ_MARGIN = 0.08

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
    anything longer is conditional -- and a conditional four or five bytes long
    carries its destination in byte 0, which is kept when it names a room.

    That last clause matters to the floor pass and to nothing else. Only OPEN
    exits are scored, so what a conditional names cannot move a room; but a
    floor is a set of rooms joined by level exits, and a door or a flag is a
    perfectly good way to walk from one room to the next. Dropping those
    destinations cut floors apart at every locked door.

    Two passes, because "names a room" means "names an object that itself has
    direction properties", which is not known until the first pass is done --
    the same guard room_model.c applies for the same reason: nothing in the
    format distinguishes a destination byte from any other.
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
            elif plen in (4, 5):
                exits[DIRW[d]] = ("MAYBE", raw[addr])
            else:
                exits[DIRW[d]] = ("MAYBE", 0)
        if exits:
            graph[o.num] = {"name": o.name, "exits": exits}
    for r in graph.values():
        for d, (kind, dest) in list(r["exits"].items()):
            if kind == "MAYBE" and dest not in graph:
                r["exits"][d] = (kind, 0)
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


def label_near(pdf, page, x, y, radius, want=None):
    """The text of whichever OCR fragment sits nearest (x, y) within radius
    page pixels, optionally restricted to fragments passing want(text), or
    None if nothing qualifies.

    Crops the page around the point and reads only that crop, the way
    read_boxes crops each box and reads only its interior -- not
    read_labels, whose merge stitches nearby OCR lines into runs long
    enough to cover a floor plan's whole room name. That merge assumes a
    page's text sits in tidy, unrelated islands; Zork I's underground page
    breaks the assumption by running a paragraph of Notes text straight
    through the middle of the drawn geography, so on that page the merge
    chains every line into one blob spanning almost the whole page, whose
    reported position is useless for finding anything near a point. A crop
    is immune to that: nothing outside radius pixels of (x, y) is ever read,
    so there is nothing distant to chain onto. Upscaled 3x with cubic
    interpolation before OCR, matching read_boxes -- an unexplained
    difference in preprocessing between mapscan's two OCR call sites would
    cost recall on fainter labels elsewhere in the corpus for no measured
    reason.

    want, when given, filters candidates by their raw text before distance
    is judged, so a room's own name sitting close to the point never beats
    out a caption a little further away that actually matches what the
    caller is looking for. What counts as a match is the caller's business,
    not this function's -- recognising what is drawn on the page is
    mapscan's job; deciding what a piece of text means belongs to whichever
    module is interpreting the drawing.
    """
    img = page_image(pdf, page)
    ih, iw = img.shape[:2]
    x0, y0 = max(0, x - radius), max(0, y - radius)
    x1, y1 = min(iw, x + radius), min(ih, y + radius)
    crop = img[y0:y1, x0:x1]
    if crop.size == 0:
        return None
    crop = cv2.resize(crop, None, fx=3, fy=3, interpolation=cv2.INTER_CUBIC)
    res, _ = ocr_engine()(crop)
    if not res:
        return None
    best, pick = 1e9, None
    for item in res:
        text = str(item[1]).strip()
        if want is not None and not want(text):
            continue
        pts = item[0]
        cx = x0 + sum(q[0] for q in pts) / len(pts) / 3
        cy = y0 + sum(q[1] for q in pts) / len(pts) / 3
        d = abs(cx - x) + abs(cy - y)
        if d < best and d < radius:
            best, pick = d, text
    return pick


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
