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
 |     a hand-checked alias table for the thirteen rooms that were renamed, and
 |     resolves same-title groups by pairing story object order against Saturn
 |     room-index order. An alias can merge two distinct story rooms into one
 |     Saturn title group -- STRANGE PASSAGE and NARROW PASSAGE both land in
 |     the Saturn NARROW PASSAGE group once the alias applies -- and
 |     within-group order cannot then tell them apart, so a pin table names
 |     specific story rooms directly by Saturn room index and is honoured
 |     before the order-based pairing runs. Refuses rather than guessing: any
 |     room left unresolved, any Saturn row claimed twice, a pin naming a
 |     Saturn room that does not exist or a story title it cannot match
 |     exactly once, or a story whose release and serial are not 88 / 840726,
 |     raises instead of writing a zero, because a zero would show up only as
 |     a background that silently fails to change.
 |     Zork I is the only game whose rows are measured. Every other game on
 |     the disc draws from that same 74-picture, 31-track supply, but its rooms
 |     are ASSIGNED by hand through the review app and read here out of
 |     tools/assets/presentation/<STEM>.json. A game with no such file, or with
 |     an empty one, emits no row at all rather than a table of zeros -- a
 |     missing row means "this story has no authored presentation", which the
 |     runtime already handles by holding whatever picture is showing and
 |     drawing music from the neutral pool. A row of zeros would mean the same
 |     thing far less legibly, and would cost 768 bytes each to say it.
 | Author: suinevere
 | Dependencies: csv, json, pathlib, re, sys
 | Globals: ROOT, CSV, ROOMS, ZIL, ALIASES, OUT, STORE, AREAS, SE_BANKS,
 |     RELEASE, SERIAL
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
STORE = ROOT / "tools" / "assets" / "presentation"
INVENTORY = ROOT / "tools" / "assets" / "rooms"

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


def apply_pins(pins, story_rooms, by_title, sat_by_title, sat_by_index, title_of):
    """/*----------------------
     | apply_pins
     | Description: Resolves the pinned rooms before the order-based pairing
     |     loop runs. A pin names a story room by its raw (pre-alias) title and
     |     a Saturn room index directly, because an alias can merge two
     |     distinct story rooms into one Saturn title group and within-group
     |     order cannot then tell them apart -- STRANGE PASSAGE and NARROW
     |     PASSAGE both land in the Saturn NARROW PASSAGE group once the alias
     |     is applied, and object-order-vs-index-order pairs them backwards.
     |     Removes each pinned room from the group the ordering loop then
     |     processes, so it is not paired a second time.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: pins -- {raw story title: saturn room index}
     |     story_rooms -- the story's rooms, obj order
     |     by_title -- {post-alias title: [story room, ...]}, mutated in place
     |     sat_by_title -- {saturn title: [saturn row, ...]}, mutated in place
     |     sat_by_index -- {int saturn room index: saturn row}
     |     title_of -- story room -> post-alias title
     | Returns: (join, claimed) -- join is {object number: saturn row dict}
     |     for pinned rooms only; claimed is the set of int Saturn room
     |     indices those rooms resolved to
     ----------------------*/"""
    join = {}
    claimed = set()
    for pin_title, room_index in pins.items():
        matches = [r for r in story_rooms
                   if r["title"].strip().upper() == pin_title]
        if len(matches) != 1:
            raise SystemExit(f"pin {pin_title!r} matches {len(matches)} "
                             f"story rooms, need exactly 1")
        room = matches[0]
        if room_index not in sat_by_index:
            raise SystemExit(f"pin {pin_title!r} names Saturn room "
                             f"{room_index}, which does not exist")
        sat = sat_by_index[room_index]
        group_title = title_of(room)
        if sat["title"].strip().upper() != group_title:
            raise SystemExit(
                f"pin {pin_title!r} names Saturn room {room_index} "
                f"({sat['title'].strip()!r}), which is not in the "
                f"{group_title!r} group {pin_title!r} resolves into")
        if int(sat["room"]) in claimed:
            raise SystemExit(f"Saturn room {sat['room']} claimed twice")
        claimed.add(int(sat["room"]))
        join[room["obj"]] = sat
        by_title[group_title].remove(room)
        sat_by_title[group_title].remove(sat)
    return join, claimed


def build_join():
    """/*----------------------
     | build_join
     | Description: Maps each Z-machine object number to its Saturn row. Groups
     |     both sides by title -- story titles first passed through the alias
     |     table -- checks the group sizes agree, honours the pin table for
     |     rooms an alias merges into a group order cannot separate, and pairs
     |     what is left in a group by object order against Saturn room-index
     |     order.
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

    raw_aliases = json.loads(ALIASES.read_text(encoding="utf-8"))
    alias = {k: v for k, v in raw_aliases.items() if not k.startswith("_")}
    pins = raw_aliases.get("_pins", {})
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

    sat_by_index = {int(r["room"]): r for r in saturn}
    join, claimed = apply_pins(pins, story_rooms, by_title, sat_by_title,
                                sat_by_index, title_of)

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


def assigned_games():
    """/*----------------------
     | assigned_games
     | Description: The hand-assigned per-room tables for every game other than
     |     Zork I, as (stem, release, serial, rows) with rows a 256-entry list
     |     of (image, track, se_bank).
     |
     |     Release and serial come from the room inventory rather than from a
     |     table here, because the inventory is generated from the story file
     |     itself and so cannot disagree with the bytes the runtime will read
     |     out of the header. se_bank is always 0: the sound-effect banks are
     |     Zork I's own, keyed to its areas, and nothing plays them for another
     |     story.
     |
     |     Refuses a picture index outside the frame table or a track outside
     |     the disc, rather than writing it -- both would fail silently at run
     |     time, one as a wild read into an archive and the other as a seek past
     |     the last track.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: STORE, INVENTORY
     | Params: n_frames -- how many pictures the frame table holds
     | Returns: a list of (stem, release, serial, rows)
     ----------------------*/"""


def load_assigned(n_frames):
    if not STORE.is_dir():
        return []
    out = []
    for path in sorted(STORE.glob("*.json")):
        stem = path.stem
        if stem == "ZORK1":
            raise SystemExit(
                "gen_presentation: tools/assets/presentation/ZORK1.json exists, but "
                "Zork I's table is measured from the original disc and must not be "
                "hand-assigned -- delete it")
        data = json.loads(path.read_text(encoding="utf-8"))
        rooms = data.get("rooms", {})
        if not rooms:
            continue

        inv_path = INVENTORY / f"{stem}.json"
        if not inv_path.is_file():
            raise SystemExit(f"gen_presentation: {stem} has assignments but no "
                             f"room inventory at {inv_path}")
        inv = json.loads(inv_path.read_text(encoding="utf-8"))
        release, serial = int(inv["release"]), str(inv["serial"])

        rows = [(0, 0, 0)] * 256
        n = 0
        for obj_s, rec in rooms.items():
            obj = int(obj_s)
            if obj >= 256:
                raise SystemExit(f"gen_presentation: {stem} object {obj} is outside "
                                 "the 256-entry table")
            image = int(rec.get("image", 0))
            track = int(rec.get("track", 0))
            if image and not (1 <= image <= n_frames):
                raise SystemExit(f"gen_presentation: {stem} object {obj} names picture "
                                 f"{image}, outside the {n_frames} the archives hold")
            if track != 0 and not (2 <= track <= 32):
                raise SystemExit(f"gen_presentation: {stem} object {obj} names track "
                                 f"{track}, which is neither silence nor a disc track")
            if image == 0 and track == 0:
                continue          # an explicit "leave this room alone"
            rows[obj] = (image, track, 0)
            n += 1
        if n:
            out.append((stem, release, serial, rows))
    return out


def main(argv):
    """/*----------------------
     | main
     | Description: Writes game_presentation.inc -- Zork I's measured rows plus
     |     one row per game that has hand-assigned ones.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: OUT, AREAS, SE_BANKS, RELEASE, SERIAL, STORE
     | Params: argv -- command-line arguments (unused; accepted for test calls)
     | Returns: 0
     ----------------------*/"""
    join = build_join()
    frames, index_of = frame_table()
    assigned = load_assigned(len(frames))

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
             " |   tools/gen_presentation.py. Every game's per-room picture,",
             " |   CD-DA track and sound-effect bank indexed by object number,",
             " |   the frame offsets inside each archive, and the table that",
             " |   keys them by release and serial. image is 1-based so 0 means",
             " |   unauthored; track 0 means silence, which ten rooms want.",
             " |   Zork I's rows are measured off the original disc; every",
             " |   other game's are assigned by hand and drawn from that same",
             " |   supply of pictures and tracks.",
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
             f"#define PRES_GAME_N {1 + len(assigned)}",
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
    for stem, _release, _serial, rows in assigned:
        lines.append(f"static const Presentation GAME_PRES_{stem}[256] = {{")
        for i in range(0, 256, 4):
            chunk = ", ".join(f"{{ {a}, {b}, {c} }}" for a, b, c in rows[i:i + 4])
            lines.append(f"    {chunk},")
        lines.append("};")
    lines.append("static const GamePresMap GAME_PRES_MAP[PRES_GAME_N] = {")
    lines.append(f'    {{ {RELEASE}, "{SERIAL}", GAME_PRES_ZORK1 }},')
    for stem, release, serial, _rows in assigned:
        lines.append(f'    {{ {release}, "{serial}", GAME_PRES_{stem} }},')
    lines.append("};")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Wrote {OUT.relative_to(ROOT)}: {1 + len(assigned)} games, "
          f"{len(frames)} pictures")
    for stem, _r, _s, rows in assigned:
        print(f"  {stem}: {sum(1 for r in rows if r != (0, 0, 0))} rooms")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
