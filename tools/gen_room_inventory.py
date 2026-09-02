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
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import gen_room_corpus as corpus
import zstory

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3 = ROOT / "tools" / "assets" / "Z3"
OUT = ROOT / "tools" / "assets" / "rooms"
SOLUTIONS = ROOT / "tools" / "typeahead" / "solutions"
WANDER = ROOT / "tools" / "wander.txt"


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
    direction_props = corpus.derive_direction_props(children, desc_prop,
                                                    story=story)
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


def capture_runtime(exe, story_path):
    """{title: body} captured by driving host mojozork through this story.

    Description: Runs the story's walkthrough (tools/typeahead/solutions/
        <STEM>.WIN) when one exists, always followed by the fixed wander
        script, exactly as gen_room_corpus.main does it. A title seen twice
        keeps its first capture, so the walkthrough (a real, deliberate path
        through the game) wins over the wander pass's incidental visits.
    Author: suinevere
    Dependencies: gen_room_corpus
    Globals: SOLUTIONS, WANDER
    Params: exe -- built host mojozork binary; story_path -- a .Z3 file
    Returns: dict mapping room title to its captured body text
    """
    seen = {}
    stem = story_path.stem.upper()
    win = SOLUTIONS / f"{stem}.WIN"
    if win.is_file() and win.stat().st_size > 0:
        cmds = [ln.strip() for ln in win.read_text(errors="replace").splitlines()
                if ln.strip() and not ln.strip().startswith("#")]
        for title, body in corpus.rooms_from(corpus.run(exe, story_path, cmds)):
            seen.setdefault(title, body)
    wander = [ln.strip() for ln in WANDER.read_text().splitlines() if ln.strip()]
    for title, body in corpus.rooms_from(corpus.run(exe, story_path, wander)):
        seen.setdefault(title, body)
    return seen


def merge_runtime(inv, captured):
    """Fills the descriptions the static pass could not decode, and stamps
    every row's source.

    Description: Fills descriptions the static pass could not decode from a
        {title: text} map captured by driving the interpreter, and stamps
        every row's source. Static wins on a collision: it is the string the
        story actually stores, while a capture is one particular visit under
        one particular game state.

        A capture attaches to EVERY row sharing that title. Runtime output
        carries no object number, so a duplicated title cannot be resolved
        to one object -- and does not need to be, because rooms are
        duplicated precisely when they are the same place repeated.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: inv -- an inventory dict; captured -- {title: description}
    Returns: the same dict, rows mutated in place
    """
    for row in inv["rooms"]:
        if row["description"]:
            row["source"] = "static"
            continue
        text = captured.get(row["title"])
        if text:
            row["description"] = text
            row["source"] = "runtime"
        else:
            row["source"] = None
    return inv


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
        load a single game's rooms without parsing the whole library. Static
        decoding fills what it can; a runtime capture pass (host mojozork,
        built once for the whole run) fills the rest, then merge_runtime
        stamps every row's source.
    Author: suinevere
    Dependencies: json, tempfile, gen_room_corpus
    Globals: Z3, OUT
    Params: argv -- unused
    Returns: 0
    """
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        exe = corpus.build_mojozork(pathlib.Path(td))
        for path in sorted(Z3.glob("*.Z3")):
            d = inventory_for(path)
            captured = capture_runtime(exe, path)
            merge_runtime(d, captured)
            dst = OUT / (path.stem + ".json")
            dst.write_text(json.dumps(d, indent=1, sort_keys=True) + "\n",
                           encoding="utf-8")
            n_null = sum(1 for r in d["rooms"] if r["source"] is None)
            print(f"{path.name:14} {d['count']:4} rooms  null={n_null:3d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
