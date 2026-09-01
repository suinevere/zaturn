#!/usr/bin/env python3
"""/*----------------------
 | gen_items.py
 | Description: GENERATES saturn/src/scene/game_items.inc -- which of the
 |     Japanese Zork I disc's nineteen item pictures each of Zork I's objects
 |     gets, keyed by release and serial.
 |
 |     The binding is authored rather than measured, because there is nothing
 |     to measure: the Saturn build is a native reimplementation, not a
 |     Z-machine interpreter, so its GAME.DAT carries no object numbers to
 |     recover. What IS measured is the set -- 1dungeon.zil gives 22 objects
 |     carrying a TVALUE, and dropping SWORD (TVALUE 0) and the two damaged
 |     variants leaves exactly nineteen treasures against exactly nineteen
 |     pictures.
 |
 |     tools/assets/zork1_items.json carries object NAMES rather than numbers,
 |     because a name is checkable by a human reading the diff and a number is
 |     not. This module resolves them and refuses rather than writing a zero
 |     on: a name matching zero objects or more than one, a duplicate picture
 |     index or object, an index outside 0..18, or a story whose release and
 |     serial are not 88 / 840726. A zero would show up only as a pane that
 |     silently fails to change.
 | Author: suinevere
 | Dependencies: json, pathlib, sys, zstory
 | Globals: ROOT, Z3, BINDING, OUT, RELEASE, SERIAL, PIC_N
 ----------------------*/"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import zstory  # noqa: E402

Z3 = ROOT / "tools" / "assets" / "Z3" / "ZORK1.Z3"
BINDING = ROOT / "tools" / "assets" / "zork1_items.json"
OUT = ROOT / "saturn" / "src" / "scene" / "game_items.inc"

RELEASE = 88
SERIAL = "840726"
PIC_N = 19


def story():
    """/*----------------------
     | story
     | Description: ZORK1.Z3 decoded, after checking its identity.
     | Author: suinevere
     | Dependencies: zstory
     | Globals: Z3, RELEASE, SERIAL
     | Params: N/A
     | Returns: a zstory.Story
     ----------------------*/"""
    raw = Z3.read_bytes()
    check_identity((raw[2] << 8) | raw[3], raw[0x12:0x18].decode("ascii", "replace"))
    return zstory.Story(Z3)


def check_identity(release, serial):
    """/*----------------------
     | check_identity
     | Description: Refuses any story but Zork I release 88 / serial 840726.
     |     The picture set is portraits of THAT story's objects; another
     |     release's object numbers would bind pictures to the wrong things
     |     silently.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RELEASE, SERIAL
     | Params: release -- Z-machine release; serial -- 6-char serial
     | Returns: N/A
     ----------------------*/"""
    if release != RELEASE or serial != SERIAL:
        raise SystemExit(f"expected release {RELEASE} serial {SERIAL}, "
                         f"got release {release} serial {serial}")


def load_binding():
    """/*----------------------
     | load_binding
     | Description: zork1_items.json as {picture index: object name}.
     | Author: suinevere
     | Dependencies: json
     | Globals: BINDING
     | Params: N/A
     | Returns: dict of int -> str
     ----------------------*/"""
    return {int(k): v for k, v in json.loads(BINDING.read_text()).items()}


def resolve(binding, st):
    """/*----------------------
     | resolve
     | Description: Every (object number, picture index) pair the binding names,
     |     sorted by object number so the emitted table can be searched in
     |     order. Raises SystemExit on any of the five refusals.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: PIC_N
     | Params: binding -- {picture index: object name}; st -- a zstory.Story
     | Returns: list of (obj, picture), sorted by obj
     ----------------------*/"""
    rows, seen_obj, seen_pic = [], set(), set()
    for pic, name in sorted(binding.items()):
        if pic < 0 or pic >= PIC_N:
            raise SystemExit(f"picture index {pic} is outside 0..{PIC_N - 1}")
        if pic in seen_pic:
            raise SystemExit(f"picture index {pic} bound twice")
        seen_pic.add(pic)
        hits = [o for o in st.objects if (o.name or "").strip() == name]
        if len(hits) != 1:
            raise SystemExit(f"\"{name}\" matches {len(hits)} objects, expected exactly 1")
        obj = hits[0].num
        if obj in seen_obj:
            raise SystemExit(f"object {obj} (\"{name}\") bound twice")
        seen_obj.add(obj)
        rows.append((obj, pic))
    return sorted(rows)


def emit(rows):
    """/*----------------------
     | emit
     | Description: The generated .inc text for one game's rows.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RELEASE, SERIAL
     | Params: rows -- (obj, picture) pairs, sorted by obj
     | Returns: the file text
     ----------------------*/"""
    lines = [
        "/*----------------------",
        " | game_items.inc",
        " | Description: GENERATED FILE -- do not edit by hand; produced by",
        " |   tools/gen_items.py. Which of OITEM.CZ's nineteen item pictures",
        " |   each object gets, keyed by release and serial. picture is a",
        " |   0-based index into the container; an object with no row has no",
        " |   picture and takes the blank plate. Zork I is the only game with a",
        " |   table: these are portraits of its own treasures, not a pool other",
        " |   stories could draw from.",
        " | Author: suinevere",
        " ----------------------*/",
        "typedef struct {",
        "    unsigned short obj;",
        "    unsigned char  picture;",
        "} ItemPicture;",
        "typedef struct {",
        "    unsigned short     release;",
        "    const char        *serial;",
        "    const ItemPicture *items;",
        "    unsigned short     count;",
        "} GameItemMap;",
        "#define ITEM_GAME_N 1",
        f"#define ITEM_PIC_N {PIC_N}",
        f"static const ItemPicture ITEMS_ZORK1[{len(rows)}] = {{",
    ]
    for obj, pic in rows:
        lines.append(f"    {{ {obj}, {pic} }},")
    lines += [
        "};",
        "static const GameItemMap GAME_ITEM_MAP[ITEM_GAME_N] = {",
        f"    {{ {RELEASE}, \"{SERIAL}\", ITEMS_ZORK1, {len(rows)} }},",
        "};",
    ]
    return "\n".join(lines) + "\n"


def main(argv):
    """/*----------------------
     | main
     | Description: Resolves the binding and writes game_items.inc.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: OUT
     | Params: argv -- unused
     | Returns: 0
     ----------------------*/"""
    rows = resolve(load_binding(), story())
    OUT.write_text(emit(rows), newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {len(rows)} objects bound")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
