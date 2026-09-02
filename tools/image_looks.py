#!/usr/bin/env python3
"""/*----------------------
 | image_looks.py
 | Description: What each of the 74 measured pictures actually SHOWS, written
 |     down by looking at all of them, and which of them will do for each
 |     scene. Generated pictures answer the same question out of
 |     tools/assets/art/frames.json, where the person who made one said what it
 |     shows -- one function answers for every picture, because nothing
 |     downstream knows or should know which supply an index came from.
 |
 |     Everything before this file matched pictures by the name of the Zork I
 |     room they were drawn for, and that name is a caption, not a description.
 |     Picture 37 is called East-West Passage, so CORRIDOR was pointed at it
 |     and 441 rooms -- 22% of the disc -- opened on it. Picture 37 is a rough
 |     ochre-lit rock face. It is not a passage of any kind and never was; the
 |     room it was drawn for happens to be underground and happens to run east
 |     to west. The corridors on this disc are 30, 33 and 39, and nothing had
 |     ever looked.
 |
 |     So each scene names every picture that will genuinely do for it, in
 |     preference order, and rooms are spread across that list rather than all
 |     taking its head. Twenty-nine of the 74 pictures were in use before this
 |     and 45 had never been drawn once, which is its own answer to whether one
 |     picture per scene was enough.
 |
 |     Where Zork I's own rooms agree strongly on a picture for a scene, that
 |     measurement still wins outright and no spreading happens -- four of four
 |     FOREST rooms took picture 3 and that is not a thing to improve on. The
 |     spreading is for the scenes where the evidence was thin or absent, which
 |     is exactly where a single repeated picture was least defensible.
 | Author: suinevere
 | Dependencies: art_frames
 | Globals: IMAGE_LOOKS, SCENE_IMAGES
 ----------------------*/"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import art_frames

IMAGE_LOOKS = {
    1:  "dark wood, close trunks, no sky",
    2:  "the side of a stone cottage, window and eaves",
    3:  "straight tree trunks, dense green",
    4:  "a boarded window in a stone wall, close",
    5:  "a bright track running away through trees",
    6:  "a stone beehive hut set in a mound, dark doorway",
    7:  "foliage and branches filling the frame",
    8:  "a white house across open lawn, sky above",
    9:  "bare roof timbers and rafters",
    10: "a dim room with a small window and a table",
    11: "a furnished room, cabinet and doorway",
    12: "a bare stone room with one doorway",
    13: "a broken rock face",
    14: "a panelled room, two doors, framed pictures",
    15: "a vaulted brick cellar with a bench",
    16: "a rough gold-lit rock chamber",
    17: "a dark brick dead end",
    18: "a bare grey chamber with a grating overhead",
    19: "a dark colonnade receding",
    20: "a dark vaulted hall between columns",
    21: "mossy green stonework and a stair",
    22: "a reddish rock passage with an oval mouth",
    23: "a gilded hall, columns and ornament",
    24: "stones standing in shallow water",
    25: "still water in a cave mouth",
    26: "a low flooded stone chamber",
    27: "a vaulted stone chamber, wet floor",
    28: "a pale shore under a dark sky",
    29: "a fallen column in the dark",
    30: "a deep blue brick tunnel receding",
    31: "a plain rectangular room, doors both sides",
    32: "a timbered shaft head, beams overhead",
    33: "a brick barrel-vault tunnel receding",
    34: "a red rock chasm",
    35: "a dripping teal cave",
    36: "sheer dark rock walls, a slot of sky",
    37: "a rough ochre rock face, no worked stone",
    38: "a blue stone room with two doorways",
    39: "an arched stone passage, lit floor",
    40: "a domed brown rock chamber",
    41: "a magenta gateway in broken rock",
    42: "a green skeletal ruin in the dark",
    43: "a circular gallery above a black drop",
    44: "a sandstone ramp rising, gold light",
    45: "a carved stone relief wall",
    46: "green temple ruins, standing stonework",
    47: "columns around an altar",
    48: "a domed brown chamber with a plinth",
    49: "an interior with fittings and a counter",
    50: "a dam wall across water",
    51: "banks of instruments and panels",
    52: "a waterfall down a rock face",
    53: "canyon walls rising from water",
    54: "mountains over a wide dark valley",
    55: "a green mossy ledge",
    56: "stone steps climbing",
    57: "an arch of light over water",
    58: "vertical streaks of light",
    59: "dark water between rock",
    60: "dark water, far bank",
    61: "a stony beach at the waterline",
    62: "rippled sand, gold light",
    63: "a rocky shore",
    64: "blue rock cliffs at the water",
    65: "a timber-framed tunnel",
    66: "a gold rock tunnel, dead end",
    67: "a ladder against broken rock",
    68: "a ladder head in a rough chamber",
    69: "a cold blue stone room",
    70: "a white machine cabinet in a dark room",
    71: "heavy timber framing at a mine mouth",
    72: "a round grated shaft mouth, green",
    73: "stacked timbers in a rough chamber",
    74: "a timber-framed chamber, boards underfoot",
}
"""IMAGE_LOOKS

Description: One line per picture saying what is in it. Written by looking at
    all 74, not by reading the name of the Zork I room each was drawn for --
    that name is a caption and captions lie: 37 is called East-West Passage and
    is a rock face, 62 is called Sandy Cave and is a field of dunes, 18 is
    called Grating Room and is the only prison cell on the disc.
Author: suinevere
"""

SCENE_IMAGES = {
    "FOREST":    (3, 1, 7, 5),
    "GARDEN":    (5, 55, 21, 1),
    "DESERT":    (62, 44, 61),
    "ROCKY":     (53, 54, 36, 34, 13, 64),
    "SHORE":     (61, 63, 28, 64),
    "RIVER":     (59, 60, 52, 24, 57, 26, 50, 25),
    "ROAD":      (5, 56, 28),
    "CAVE":      (37, 16, 35, 40, 22, 66, 13, 26),
    "MAZE":      (20, 19, 17, 30, 33),
    "MINE":      (65, 67, 68, 71, 73, 32, 66),
    "PIT":       (34, 72, 32, 13, 36),
    "CRYPT":     (45, 48, 6, 41, 44),
    "HOUSE_EXT": (4, 2, 8),
    "VILLAGE":   (8, 2, 6),
    "CASTLE":    (46, 23, 47, 29, 21),
    "DOCK":      (63, 56, 50, 61),
    "PARLOR":    (11, 14, 10),
    "KITCHEN":   (10, 11, 49),
    "BEDROOM":   (9, 11, 31),
    "BATHROOM":  (10, 69, 38, 31),
    "LIBRARY":   (14, 15, 11),
    "DARKROOM":  (9, 12, 18, 38, 31, 69),
    "CORRIDOR":  (33, 30, 39, 27, 22, 74),
    "OFFICE":    (11, 49, 14, 51),
    "LAB":       (51, 70, 49),
    "STORAGE":   (15, 74, 73, 14),
    "CELL":      (18, 12, 69, 38, 31, 27),
    "THEATER":   (43, 23, 47),
    "TEMPLE":    (47, 46, 45, 48, 29, 23),
    "SHIP_EXT":  (60, 59, 64, 54),
    "SHIP_INT":  (70, 51, 49, 69, 27, 31),
    "SPACE":     (42, 58, 43, 30),
}
"""SCENE_IMAGES

Description: Every picture that will genuinely do for each scene, best first.
    The head of each list is what a single-picture answer would have been, so
    nothing gets worse by consulting this; the tail is what stops one picture
    carrying a fifth of the disc.

    Some corrections this encodes over the old title-derived guesses, all from
    looking: CORRIDOR was 37, a rock face, and is now 33, a brick barrel vault.
    CELL was 12, a bare stone room, and is now 18, which has a grating in the
    ceiling. DESERT was 61, a stony beach, and is now 62, which is dunes.
    THEATER keeps 43 and gains 23, the gilded hall. CRYPT was 41, a magenta
    gateway, and leads on 45, a carved relief wall, which is what a tomb
    actually looks like.
Author: suinevere
"""


def generated_looks():
    """/*----------------------
     | generated_looks
     | Description: Index -> description for every generated picture, cached
     |     after the first read.
     | Author: suinevere
     | Dependencies: art_frames
     | Globals: _GENERATED
     | Params: N/A
     | Returns: {index: description}
     ----------------------*/"""
    global _GENERATED
    if _GENERATED is None:
        _GENERATED = {int(f["index"]): f.get("shows", "")
                      for f in art_frames.frames()}
    return _GENERATED


_GENERATED = None
"""_GENERATED

Description: The cache generated_looks fills on its first call. A module-level
    None rather than a read at import time so a checkout with no generated art,
    and a run that generates some and asks afterwards, both behave.
Author: suinevere
"""


def looks(index):
    """/*----------------------
     | looks
     | Description: What one picture shows, measured or generated.
     | Author: suinevere
     | Dependencies: generated_looks
     | Globals: IMAGE_LOOKS
     | Params: index -- the 1-based IMAGE_FRAME index
     | Returns: a one-line description, or an empty string
     ----------------------*/"""
    if index in IMAGE_LOOKS:
        return IMAGE_LOOKS[index]
    return generated_looks().get(index, "")


def scene_images():
    """/*----------------------
     | scene_images
     | Description: Every picture each scene may use, measured and generated
     |     together. A generated picture goes on the TAIL of the lists it names:
     |     the measured order is a judgement made by looking at all 74 at once,
     |     and a new plate has no claim to displace it -- what it does is give
     |     the spreading somewhere further to go.
     | Author: suinevere
     | Dependencies: art_frames
     | Globals: SCENE_IMAGES, _SCENES
     | Params: N/A
     | Returns: {scene: tuple of picture indices}
     ----------------------*/"""
    global _SCENES
    if _SCENES is None:
        out = {k: tuple(v) for k, v in SCENE_IMAGES.items()}
        for f in art_frames.frames():
            for s in f.get("scenes", ()):
                out[s] = tuple(out.get(s, ())) + (int(f["index"]),)
        _SCENES = out
    return _SCENES


_SCENES = None
"""_SCENES

Description: The cache scene_images fills on its first call, for the reason
    _GENERATED is one.
Author: suinevere
"""


def images_for(scene, measured=None):
    """/*----------------------
     | images_for
     | Description: The pictures one scene may use, best first, with a measured
     |     favourite promoted to the head if it is in the list and prepended if
     |     it is not. Zork I's own agreement outranks anything written here --
     |     this file is a judgement about pictures and that is a count of what
     |     the original actually did.
     | Author: suinevere
     | Dependencies: scene_images
     | Globals: N/A
     | Params: scene -- the scene name; measured -- the picture Zork I's rooms
     |     of this scene mostly took, or None
     | Returns: a tuple of picture indices, possibly empty
     ----------------------*/"""
    have = scene_images().get(scene, ())
    if not measured:
        return have
    return (measured,) + tuple(i for i in have if i != measured)
