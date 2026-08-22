#!/usr/bin/env python3
"""Decode every story's rooms, keyed by object number.

Description: The primary source for scene tagging. gen_room_corpus.py already
    decodes titles and descriptions, but it keys by title and dedupes on it,
    which cannot distinguish Zork I's fifteen "Maze" rooms from one another.
    This module keeps the object number, which is what the runtime looks a room
    up by (mojozork.c reads it from global 0x10).

    Room-shaped means "a child of the rooms hub that carries at least one
    direction property" -- find_rooms_hub's judgement, reused rather than
    re-derived. A room whose description property holds a routine address
    decodes to garbage and is recorded with description None rather than
    dropped; those rooms are exactly the ones the runtime pass exists to reach.

    Deterministic by construction: it reads bytes and sorts by object number.
Author: suinevere
Dependencies: json, pathlib, sys, zstory, gen_room_corpus
Globals: ROOT, Z3, OUT
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import gen_room_corpus as corpus
import zstory

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3 = ROOT / "tools" / "assets" / "Z3"
OUT = ROOT / "tools" / "assets" / "rooms"


def static_rooms(story):
    """Every room-shaped object of `story`, sorted by object number.

    Description: Each row is {obj, title, description}; description is None
        when the property holds a routine rather than a stored string, so
        that room stays visible rather than silently dropped.
    Author: suinevere
    Dependencies: gen_room_corpus
    Globals: N/A
    Params: story -- a zstory.Story
    Returns: (rows, desc_prop)
    """
    hub, children = corpus.find_rooms_hub(story)
    if hub is None:
        return [], 0
    desc_prop, _ = corpus.detect_description_property(story, children)
    direction_props = corpus.derive_direction_props(children, desc_prop)
    rows = []
    for obj in children:
        if not obj.properties.keys() & direction_props:
            continue
        title = obj.name.strip()
        if not title or len(title) > corpus.TITLE_MAX:
            continue
        rows.append({
            "obj": obj.num,
            "title": title,
            "description": corpus.decode_description(story, obj, desc_prop),
        })
    rows.sort(key=lambda r: r["obj"])
    return rows, desc_prop


def inventory_for(path):
    """One story's full inventory record, ready to serialise.

    Description: Wraps static_rooms with the header fields a per-story JSON
        file needs to stand alone -- release and serial identify the exact
        build, desc_prop records which property this story's rooms describe.
    Author: suinevere
    Dependencies: zstory
    Globals: N/A
    Params: path -- a .Z3 story file
    Returns: dict with story, release, serial, desc_prop, count, rooms
    """
    story = zstory.Story(path)
    rows, desc_prop = static_rooms(story)
    return {
        "story": pathlib.Path(path).name,
        "release": story.release,
        "serial": story.serial,
        "desc_prop": desc_prop,
        "count": len(rows),
        "rooms": rows,
    }


def main(argv):
    """Writes one JSON inventory per story into tools/assets/rooms.

    Description: One file per story, named after its stem, so later tasks can
        load a single game's rooms without parsing the whole library.
    Author: suinevere
    Dependencies: json
    Globals: Z3, OUT
    Params: argv -- unused
    Returns: 0
    """
    OUT.mkdir(parents=True, exist_ok=True)
    for path in sorted(Z3.glob("*.Z3")):
        d = inventory_for(path)
        dst = OUT / (path.stem + ".json")
        dst.write_text(json.dumps(d, indent=1, sort_keys=True) + "\n",
                       encoding="utf-8")
        print(f"{path.name:14} {d['count']:4} rooms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
