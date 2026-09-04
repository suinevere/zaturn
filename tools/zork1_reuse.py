#!/usr/bin/env python3
"""/*----------------------
 | zork1_reuse.py
 | Description: Says which rooms of the Zork I derivatives are Zork I rooms,
 |     and which of the disc's own measured pictures each one should show.
 |
 |     Four of the thirty games are Zork I in another wrapper: the two Mini-Zork
 |     cut-downs and the two Infocom Samplers, whose opening act is Zork I's.
 |     Drawing a new West of House for them is drawing a worse one -- the disc
 |     already carries the picture that room was authored with, and 172 rooms
 |     across the four are the same rooms by name. They take the measured frame
 |     and no plate is generated for them at all.
 |
 |     The match is by title and deliberately conservative: casefolded, leading
 |     article dropped, punctuation flattened, and nothing else. That is enough
 |     for Mini-Zork's "Troll Room" against Zork I's "The Troll Room" and stops
 |     short of guessing, which matters because the unmatched rooms are not
 |     failures -- a Sampler holds Planetfall's dorms and Trinity's Kensington
 |     Gardens beside its Zork, and those must go on being drawn.
 | Author: suinevere
 | Dependencies: json, pathlib, re, sys, gen_presentation
 | Globals: ROOT, ROOMS, DERIVED
 ----------------------*/"""
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROOMS = ROOT / "tools" / "assets" / "rooms"
DERIVED = ("INFOSAM5", "INFOSAM7", "MZORKI", "MZORKI2")
"""ROOT / ROOMS / DERIVED

Description: The four games whose rooms are partly Zork I's. MZORKII is not
    among them -- Mini-Zork II is a cut-down of Zork II and shares one title
    with Zork I by coincidence, not by being the same room.
Author: suinevere
"""

_ARTICLE = re.compile(r"^(?:the|a|an)\s+")
_PUNCT = re.compile(r"[^a-z0-9 ]+")
_SPACE = re.compile(r"\s+")


def norm(title):
    """/*----------------------
     | norm
     | Description: A room title reduced to the form two games can be compared
     |     on: casefolded, punctuation flattened to spaces, a leading article
     |     dropped. No stemming and no synonyms -- a wrong match hands a room
     |     another room's picture forever and looks deliberate.
     | Author: suinevere
     | Dependencies: re
     | Globals: _ARTICLE, _PUNCT, _SPACE
     | Params: title -- a room title, possibly empty
     | Returns: the normalised title, possibly empty
     ----------------------*/"""
    if not title:
        return ""
    t = _PUNCT.sub(" ", title.lower())
    t = _SPACE.sub(" ", t).strip()
    return _ARTICLE.sub("", t).strip()


def zork1_pictures():
    """/*----------------------
     | zork1_pictures
     | Description: Every Zork I room title that has a measured picture, against
     |     the IMAGE_FRAME index that picture holds. Read through
     |     gen_presentation rather than recomputed here, because that module
     |     owns the join between the story file and the Saturn disc and a second
     |     copy of it would be a second thing to keep true.
     | Author: suinevere
     | Dependencies: json, gen_presentation
     | Globals: ROOMS
     | Params: N/A
     | Returns: {normalised title: picture index}
     ----------------------*/"""
    import gen_presentation

    join = gen_presentation.build_join()
    _frames, index_of, _areas = gen_presentation.frame_table()
    data = json.loads((ROOMS / "ZORK1.json").read_text(encoding="utf-8"))
    titles = {int(r["obj"]): (r.get("title") or "") for r in data["rooms"]}
    out = {}
    for obj, sat in join.items():
        key = norm(titles.get(obj, ""))
        if key:
            out[key] = index_of[(sat["area_archive"], int(sat["frame"]))] + 1
    return out


def matches(stem, pictures=None):
    """/*----------------------
     | matches
     | Description: The rooms of one derivative that are Zork I rooms, against
     |     the picture each takes. Empty for every game that is not a
     |     derivative, so a caller can ask about all thirty without a special
     |     case.
     | Author: suinevere
     | Dependencies: json
     | Globals: DERIVED, ROOMS
     | Params: stem -- the story stem; pictures -- a cached zork1_pictures()
     | Returns: {object number: picture index}
     ----------------------*/"""
    if stem not in DERIVED:
        return {}
    pictures = zork1_pictures() if pictures is None else pictures
    data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
    out = {}
    for r in data["rooms"]:
        idx = pictures.get(norm(r.get("title") or ""))
        if idx:
            out[int(r["obj"])] = idx
    return out


def all_matches():
    """/*----------------------
     | all_matches
     | Description: matches() for every derivative, off one read of the Zork I
     |     join.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: DERIVED
     | Params: N/A
     | Returns: {(stem, object number): picture index}
     ----------------------*/"""
    pictures = zork1_pictures()
    out = {}
    for stem in DERIVED:
        for obj, idx in matches(stem, pictures).items():
            out[(stem, obj)] = idx
    return out


def main():
    """/*----------------------
     | main
     | Description: Reports what would be reused, for a person checking it.
     | Author: suinevere
     | Dependencies: collections
     | Globals: DERIVED
     | Params: N/A
     | Returns: 0
     ----------------------*/"""
    import collections

    per = collections.Counter()
    for (stem, _obj) in all_matches():
        per[stem] += 1
    for stem in DERIVED:
        data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
        print(f"{stem:9} {per[stem]:3} of {len(data['rooms']):3} rooms take "
              "Zork I's own picture")
    print(f"{sum(per.values())} rooms need no plate drawn for them")
    return 0


if __name__ == "__main__":
    sys.exit(main())
