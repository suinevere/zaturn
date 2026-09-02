#!/usr/bin/env python3
"""/*----------------------
 | art_frames.py
 | Description: Reads tools/assets/art/frames.json -- where every generated
 |     picture lies inside its archive, what it shows and which scenes it will
 |     do for.
 |
 |     One reader rather than four. Four things need this file --
 |     gen_presentation.py to extend IMAGE_FRAME, gen_pool.py to put the
 |     picture in the supply, image_looks.py to say what it shows and which
 |     scenes may pick it, and the tests that hold those three to each other --
 |     and four copies of the same read would be four places for the shape of
 |     the file to drift.
 |
 |     A checkout that has never generated any art has no such file and gets an
 |     empty list, which is the right answer everywhere: the measured 74 are a
 |     complete supply on their own and always were.
 | Author: suinevere
 | Dependencies: json, pathlib
 | Globals: ROOT, FRAMES
 ----------------------*/"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
FRAMES = ROOT / "tools" / "assets" / "art" / "frames.json"
"""ROOT / FRAMES

Description: The placements tools/gen_art_archive.py writes. Committed, while
    the archives they describe are not -- the runtime reaches a frame by offset
    alone, so the offsets have to survive a checkout that has never built the
    art.
Author: suinevere
"""


def frames():
    """/*----------------------
     | frames
     | Description: Every generated picture, in the order it was packed, which
     |     is the order its IMAGE_FRAME index follows the measured 74 in.
     | Author: suinevere
     | Dependencies: json
     | Globals: FRAMES
     | Params: N/A
     | Returns: a list of frame dicts, empty when no art has been generated
     ----------------------*/"""
    if not FRAMES.is_file():
        return []
    return json.loads(FRAMES.read_text(encoding="utf-8"))["frames"]


def archives():
    """/*----------------------
     | archives
     | Description: The archive stems the generated pictures were packed into,
     |     in first-seen order, which is the order they are appended to
     |     PRES_AREA in.
     | Author: suinevere
     | Dependencies: frames
     | Globals: N/A
     | Params: N/A
     | Returns: a list of stems
     ----------------------*/"""
    out = []
    for f in frames():
        if f["archive"] not in out:
            out.append(f["archive"])
    return out
