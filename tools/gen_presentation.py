#!/usr/bin/env python3
"""/*----------------------
 | gen_presentation.py
 | Description: GENERATES saturn/src/scene/game_presentation.inc -- Zork I's
 |     per-room picture, CD-DA track and sound-effect bank, indexed by
 |     Z-machine object number and keyed by release and serial, plus the frame
 |     offsets that let a late frame be reached without decompressing every
 |     earlier one.
 |
 |     Joins the Saturn room table to the story file's rooms by title, through
 |     a hand-checked alias table for the fourteen rooms that were renamed, and
 |     resolves same-title groups by pairing story object order against Saturn
 |     room-index order. Refuses rather than guessing: any room left
 |     unresolved, any Saturn row claimed twice, or a story whose release and
 |     serial are not 88 / 840726 raises instead of writing a zero, because a
 |     zero would show up only as a background that silently fails to change.
 | Author: suinevere
 | Dependencies: csv, json, pathlib, re, sys
 | Globals: ROOT, CSV, ROOMS, ZIL, ALIASES, OUT, AREAS, SE_BANKS, RELEASE, SERIAL
 ----------------------*/"""
import csv
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CSV = ROOT / "analysis" / "zork_bg" / "room_backgrounds.csv"
ROOMS = ROOT / "tools" / "assets" / "rooms" / "ZORK1.json"
ZIL = (ROOT / "cd" / "Zork I - The Great Underground Empire (Japan)"
            / "zork1" / "1dungeon.zil")
ALIASES = ROOT / "tools" / "assets" / "zork1_room_aliases.json"
OUT = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"

AREAS = ["BBAR", "BCEL", "BDAM", "BDED", "BHUS", "BMAZ",
         "BMIN", "BMIR", "BRIV", "BTMP", "BWOD"]
SE_BANKS = ["SEALL", "SEMINA", "SEMINB", "SEMIR", "SEDAM", "SECEL",
            "SEHDS", "SERIV", "SEWOD", "SEMAZ", "SEBAR"]

RELEASE = 88
SERIAL = "840726"


def zil_rooms():
    """/*----------------------
     | zil_rooms
     | Description: The room names 1dungeon.zil declares, in declaration order.
     |     Read only as a count check: the story file and the Saturn table are
     |     the two sides actually joined, and the ZIL is the third witness that
     |     both describe the same 110 rooms.
     | Author: suinevere
     | Dependencies: re, pathlib
     | Globals: ZIL
     | Params: N/A
     | Returns: a list of room names
     ----------------------*/"""
    text = ZIL.read_text(encoding="utf-8", errors="replace")
    return re.findall(r"^<ROOM\s+([A-Z0-9\-]+)", text, re.MULTILINE)


def load_saturn():
    """/*----------------------
     | load_saturn
     | Description: The Saturn presentation rows, in room-index order.
     | Author: suinevere
     | Dependencies: csv
     | Globals: CSV
     | Params: N/A
     | Returns: a list of dicts, one per Saturn room 0..109
     ----------------------*/"""
    with CSV.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    rows.sort(key=lambda r: int(r["room"]))
    return rows


def build_join():
    """/*----------------------
     | build_join
     | Description: Maps each Z-machine object number to its Saturn row. Groups
     |     both sides by title -- story titles first passed through the alias
     |     table -- checks the group sizes agree, and pairs within a group by
     |     object order against Saturn room-index order.
     | Author: suinevere
     | Dependencies: json
     | Globals: ROOMS, ALIASES, RELEASE, SERIAL
     | Params: N/A
     | Returns: {object number: saturn row dict}
     ----------------------*/"""
    story = json.loads(ROOMS.read_text(encoding="utf-8"))
    if story["release"] != RELEASE or story["serial"] != SERIAL:
        raise SystemExit(
            f"ZORK1.Z3 is release {story['release']} serial {story['serial']}, "
            f"not {RELEASE} / {SERIAL}; the table would bind to the wrong objects")

    alias = {k: v for k, v in json.loads(
        ALIASES.read_text(encoding="utf-8")).items() if not k.startswith("_")}
    saturn = load_saturn()
    story_rooms = sorted(story["rooms"], key=lambda r: r["obj"])

    if len(story_rooms) != len(saturn):
        raise SystemExit(f"{len(story_rooms)} story rooms against "
                         f"{len(saturn)} Saturn rooms")
    if len(zil_rooms()) != len(saturn):
        raise SystemExit(f"{len(zil_rooms())} ZIL rooms against "
                         f"{len(saturn)} Saturn rooms")

    def title_of(room):
        t = room["title"].strip().upper()
        return alias.get(t, t)

    by_title = {}
    for r in story_rooms:
        by_title.setdefault(title_of(r), []).append(r)
    sat_by_title = {}
    for r in saturn:
        sat_by_title.setdefault(r["title"].strip().upper(), []).append(r)

    lopsided = set(by_title) ^ set(sat_by_title)
    if lopsided:
        raise SystemExit(f"titles present on one side only: {sorted(lopsided)}")

    join = {}
    claimed = set()
    for title, group in by_title.items():
        sat_group = sat_by_title[title]
        if len(group) != len(sat_group):
            raise SystemExit(f"{title}: {len(group)} story rooms against "
                             f"{len(sat_group)} Saturn rooms")
        for room, sat in zip(sorted(group, key=lambda r: r["obj"]),
                             sorted(sat_group, key=lambda r: int(r["room"]))):
            if int(sat["room"]) in claimed:
                raise SystemExit(f"Saturn room {sat['room']} claimed twice")
            claimed.add(int(sat["room"]))
            join[room["obj"]] = sat

    if len(join) != len(saturn):
        raise SystemExit(f"{len(join)} rooms resolved of {len(saturn)}")
    return join


def frame_table():
    """/*----------------------
     | frame_table
     | Description: One row per distinct frame a room references: its area, byte
     |     offset and byte length inside that area's archive, in first-seen
     |     order. There are 74 of these, not 75 -- BBAR_01 belongs to the barrow
     |     ending sequence and no room names it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: AREAS
     | Params: N/A
     | Returns: (list of (area index, offset, length), {(archive, frame): index})
     ----------------------*/"""
    seen = {}
    rows = []
    for r in load_saturn():
        key = (r["area_archive"], int(r["frame"]))
        if key in seen:
            continue
        area = AREAS.index(r["area_archive"].replace(".CGL", ""))
        seen[key] = len(rows)
        rows.append((area, int(r["frame_offset"]), int(r["frame_length"])))
    return rows, seen


def main(argv):
    """/*----------------------
     | main
     | Description: Writes game_presentation.inc.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: OUT, AREAS, SE_BANKS, RELEASE, SERIAL
     | Params: argv -- command-line arguments (unused; accepted for test calls)
     | Returns: 0
     ----------------------*/"""
    join = build_join()
    frames, index_of = frame_table()

    pres = [(0, 0, 0)] * 256
    for obj, sat in join.items():
        if obj >= 256:
            raise SystemExit(f"object {obj} is outside the 256-entry table")
        image = index_of[(sat["area_archive"], int(sat["frame"]))] + 1
        track = int(sat["cd_track"])
        if track != 0 and not (2 <= track <= 32):
            raise SystemExit(f"object {obj} names track {track}, "
                             f"which is neither silence nor a disc track")
        pres[obj] = (image, track, SE_BANKS.index(sat["se_bank"]))

    lines = ["/*----------------------",
             " | game_presentation.inc",
             " | Description: GENERATED FILE -- do not edit by hand; produced by",
             " |   tools/gen_presentation.py. Zork I's per-room picture, CD-DA",
             " |   track and sound-effect bank indexed by object number, the",
             " |   frame offsets inside each archive, and the table that keys",
             " |   them by release and serial. image is 1-based so 0 means",
             " |   unauthored; track 0 means silence, which ten rooms want.",
             " | Author: suinevere",
             " ----------------------*/",
             "typedef struct {",
             "    unsigned char image;",
             "    unsigned char track;",
             "    unsigned char se_bank;",
             "} Presentation;",
             "typedef struct {",
             "    unsigned char area;",
             "    unsigned long offset;",
             "    unsigned long length;",
             "} PresFrame;",
             "typedef struct {",
             "    unsigned short release;",
             "    const char *serial;",
             "    const Presentation *rooms;",
             "} GamePresMap;",
             "#define PRES_GAME_N 1",
             f"#define PRES_FRAME_N {len(frames)}",
             f"#define PRES_AREA_N {len(AREAS)}",
             "static const char *const PRES_AREA[PRES_AREA_N] = {"]
    for a in AREAS:
        lines.append(f'    "{a}",')
    lines.append("};")
    lines.append("static const PresFrame IMAGE_FRAME[PRES_FRAME_N] = {")
    for area, off, ln in frames:
        lines.append(f"    {{ {area}, {off}UL, {ln}UL }},")
    lines.append("};")
    lines.append("static const Presentation GAME_PRES_ZORK1[256] = {")
    for i in range(0, 256, 4):
        chunk = ", ".join(f"{{ {a}, {b}, {c} }}" for a, b, c in pres[i:i + 4])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("static const GamePresMap GAME_PRES_MAP[PRES_GAME_N] = {")
    lines.append(f'    {{ {RELEASE}, "{SERIAL}", GAME_PRES_ZORK1 }},')
    lines.append("};")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
