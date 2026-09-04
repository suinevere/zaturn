#!/usr/bin/env python3
"""/*----------------------
 | assign_room_art.py
 | Description: Gives every room the picture drawn for that room and no other,
 |     except in the four games that are Zork I in another wrapper, where a
 |     room that IS a Zork I room takes the disc's own measured picture --
 |     drawing a new West of House for Mini-Zork is drawing a worse one. See
 |     zork1_reuse.
 |
 |     A plate generated from tools/assets/art/room_prompts.json carries the
 |     game and object number it was drawn for all the way through the manifest
 |     into tools/assets/art/frames.json, so the assignment is a lookup rather
 |     than a guess: there is exactly one picture per room and it is the one
 |     made out of that room's own title and prose.
 |
 |     The track is left exactly as it stands. Picking a room's music is a
 |     different question from picking its picture, it was answered by
 |     room_guess against the scene tags and the exit graph, and nothing about
 |     drawing a picture for a room says anything new about what should be
 |     playing in it. A room with no record at all takes silence, which the
 |     runtime reads as a real choice rather than an absent one.
 |
 |     Refuses to write a partial assignment. A room whose plate is missing is
 |     reported and nothing is written, because a half-assigned disc is one
 |     where some rooms show their own picture and the rest show whatever the
 |     previous run left -- which looks deliberate and is not.
 | Author: suinevere
 | Dependencies: argparse, collections, json, pathlib, sys, art_frames,
 |     pres_store, zork1_reuse
 | Globals: ROOT
 ----------------------*/"""
import argparse
import collections
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import art_frames
import pres_store as store
import zork1_reuse

ROOT = pathlib.Path(__file__).resolve().parent.parent
"""ROOT

Description: The repository root, for reporting paths a person can open.
Author: suinevere
"""


def by_room():
    """/*----------------------
     | by_room
     | Description: (game, object) -> picture index, for every plate that names
     |     a room. Refuses a room named by two plates: two pictures for one room
     |     means one of them is unreachable and the manifest has been appended
     |     to twice.
     | Author: suinevere
     | Dependencies: art_frames
     | Globals: N/A
     | Params: N/A
     | Returns: {(stem, obj): index}
     ----------------------*/"""
    out = {}
    for f in art_frames.frames():
        if "game" not in f or "obj" not in f:
            continue
        key = (f["game"], int(f["obj"]))
        if key in out:
            raise SystemExit(f"assign_room_art: {key[0]} room {key[1]} is named "
                             f"by two plates, {out[key]} and {f['index']} -- the "
                             "manifest has been appended to twice")
        out[key] = int(f["index"])
    return out


def main(argv=None):
    """/*----------------------
     | main
     | Description: Writes one picture per room, keeping each room's track.
     | Author: suinevere
     | Dependencies: argparse, collections, json, pres_store
     | Globals: ROOT
     | Params: argv -- command line
     | Returns: 0
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be written and write nothing")
    args = ap.parse_args(argv)

    have = by_room()
    reuse = zork1_reuse.all_matches()
    # Reuse wins over a drawn plate, and the plate is left where it is rather
    # than removed. A plate's index is its position in the manifest and a room
    # record stores that index, so dropping one renumbers every plate after it
    # and silently rehomes hundreds of rooms to fix a picture nothing shows.
    # The stranded plate costs its bytes in an archive until something is drawn
    # over its slot, which is the cheaper of the two wrongs.
    stranded = sorted(set(have) & set(reuse))
    have.update(reuse)
    if not have:
        raise SystemExit("assign_room_art: no plate in frames.json names a room "
                         "-- run tools/gen_room_prompts.py, then "
                         "tools/gen_art_source.py --sheet "
                         "tools/assets/art/room_prompts.json")

    missing = collections.Counter()
    for stem in store.games():
        for r in store.rooms(stem):
            if (stem, int(r["obj"])) not in have:
                missing[stem] += 1
    if missing:
        total = sum(missing.values())
        worst = ", ".join(f"{s}:{n}" for s, n in missing.most_common(5))
        raise SystemExit(
            f"assign_room_art: {total} rooms have no plate of their own "
            f"({worst}). Nothing written: a half-assigned disc shows its own "
            "picture in some rooms and the last run's in the rest, which looks "
            "deliberate and is not. Finish the generation first.")

    written = 0
    for stem in store.games():
        rec = store.load(stem)
        rooms = rec["rooms"]
        for r in store.rooms(stem):
            obj = int(r["obj"])
            key = str(obj)
            track = rooms.get(key, {}).get("track", 0)
            rooms[key] = {"image": have[(stem, obj)], "track": int(track)}
            written += 1
        if not args.dry_run:
            rec["undo"] = []
            path = ROOT / "tools" / "assets" / "presentation" / f"{stem}.json"
            path.write_text(json.dumps(rec, indent=1) + "\n", encoding="utf-8")

    verb = "would give" if args.dry_run else "gave"
    print(f"{verb} {written} rooms a picture across {len(store.games())} "
          f"games, {len(reuse)} of them Zork I's own rather than a drawn one")
    if stranded:
        worst = ", ".join(f"{g}:{o}" for g, o in stranded[:5])
        print(f"{len(stranded)} plates were drawn for rooms that now take Zork "
              f"I's picture ({worst}...). They keep their manifest slot -- "
              "removing one renumbers every plate after it -- and are packed "
              "but never shown. Redraw over those slots to reclaim them.")
    if not args.dry_run:
        print("Now run tools/gen_presentation.py to put it in the table.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
