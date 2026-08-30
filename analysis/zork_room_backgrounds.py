#!/usr/bin/env python3
"""/*----------------------
 | zork_room_backgrounds.py
 | Description: Extract every Zork I (Saturn, JP) room background and emit the room->image table.
 | Author: suinevere
 | Dependencies: zork_cgl, zork_data.messages, saturn_translate.iso, PIL
 | Globals: AREAS, ROOM_TBL, ROOM_STRIDE, ROOM_COUNT, SE_BANKS, TITLE_OVERRIDES
 ----------------------*/

The presentation layer keeps one 16-byte record per room at file offset 0x75060
(address 0x06079060) of ``0ZORK.BIN``:

    +0  u16  CD-DA track number (0 = silence)
    +2  u16  SE bank index into SE_BANKS
    +4  u16  area index into AREAS (picks the B*.CGL archive)
    +6  u16  unused
    +8  u32  byte length of the frame record inside that archive
    +12 u32  byte offset of the frame record inside that archive

Verified against the loader at 0x0600b180, which does
``src = cgl_buffer + record[12] + 512`` (the +512 skips the frame's own CLUT).
Room titles come from the game's msg777 table, where entry ``3 * room`` is the
header printed on entry.
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zork_cgl
from zork_data.messages import MSGS

# /*----------------------
#  | Table constants
#  | Description: Where the room table lives and how its index fields decode.
#  | Author: suinevere
#  ----------------------*/
AREAS = ["BBAR", "BCEL", "BDAM", "BDED", "BHUS", "BMAZ",
         "BMIN", "BMIR", "BRIV", "BTMP", "BWOD"]
SE_BANKS = ["SEALL", "SEMINA", "SEMINB", "SEMIR", "SEDAM", "SECEL",
            "SEHDS", "SERIV", "SEWOD", "SEMAZ", "SEBAR"]
ROOM_TBL = 0x075060
ROOM_STRIDE = 16
ROOM_COUNT = 110

# Four rooms carry their title inside a multi-part <1c> message bank rather than
# as a plain msg777[3n] string; these are resolved from the bank text + area.
TITLE_OVERRIDES = {
    0:  "CLEARING",
    5:  "CLEARING",
    41: "CYCLOPS ROOM",
    92: "WHITE CLIFFS BEACH",
}


def read_rooms(zork_bin):
    """/*----------------------
     | read_rooms
     | Description: Decode all 110 room presentation records from 0ZORK.BIN.
     | Author: suinevere
     | Dependencies: struct
     | Globals: ROOM_TBL, ROOM_STRIDE, ROOM_COUNT, AREAS, SE_BANKS
     | Params: zork_bin -- bytes of 0ZORK.BIN
     | Returns: list of dicts, one per room index
     ----------------------*/"""
    rooms = []
    for i in range(ROOM_COUNT):
        o = ROOM_TBL + i * ROOM_STRIDE
        track, se, area, _pad = struct.unpack_from(">4H", zork_bin, o)
        length, offset = struct.unpack_from(">2I", zork_bin, o + 8)
        rooms.append({
            "room": i,
            "title": TITLE_OVERRIDES.get(i) or _title(i),
            "cd_track": track,
            "se_bank": SE_BANKS[se],
            "area": AREAS[area],
            "frame_offset": offset,
            "frame_length": length,
        })
    return rooms


def _title(i):
    """/*----------------------
     | _title
     | Description: Fetch the room-title header string for room *i* from msg777.
     | Author: suinevere
     | Dependencies: zork_data.messages
     | Globals: MSGS
     | Params: i -- room index
     | Returns: the title str, or "?" when the entry is not a plain string
     ----------------------*/"""
    v = MSGS.get(3 * i)
    return v if isinstance(v, str) else "?"


def extract_images(raw_dir, png_dir):
    """/*----------------------
     | extract_images
     | Description: Decode every B*.CGL archive to per-frame PNGs.
     | Author: suinevere
     | Dependencies: zork_cgl, PIL
     | Globals: AREAS
     | Params: raw_dir -- directory holding the .CGL files; png_dir -- output dir
     | Returns: dict of area -> {file offset: frame index}
     ----------------------*/"""
    os.makedirs(png_dir, exist_ok=True)
    index = {}
    for area in AREAS:
        buf = open(os.path.join(raw_dir, area + ".CGL"), "rb").read()
        index[area] = {}
        for n, off, pal, px in zork_cgl.records(buf):
            zork_cgl.save_png(os.path.join(png_dir, f"{area}_{n:02d}.png"), pal, px)
            index[area][off] = n
    return index


def main():
    """/*----------------------
     | main
     | Description: Extract the backgrounds and write room_backgrounds.csv.
     | Author: suinevere
     | Dependencies: read_rooms, extract_images, csv
     | Globals: N/A
     | Params: N/A
     | Returns: N/A
     ----------------------*/"""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    zork_bin = open(os.path.join(
        root, "cd",
        "Zork I - The Great Underground Empire (Japan)", "0ZORK.BIN"), "rb").read()
    raw_dir = os.path.join(here, "zork_bg", "raw")
    png_dir = os.path.join(here, "zork_bg", "png")
    index = extract_images(raw_dir, png_dir)
    rooms = read_rooms(zork_bin)

    # The 19 item pictures live in their own container; extract them in the
    # same pass so one run yields every image the disc actually stores.
    import zork_ui_rip
    from saturn_translate.iso import SaturnImage
    disc = os.path.join(root, "cd",
                        "Zork I - The Great Underground Empire (Japan)",
                        "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
    item_dir = os.path.join(here, "zork_ui", "items")
    n_items = zork_ui_rip.rip_items(
        SaturnImage.from_file(disc).extract("/OITEM.CZ"), item_dir)
    out = os.path.join(here, "zork_bg", "room_backgrounds.csv")
    with open(out, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["room", "title", "area_archive", "frame", "image",
                    "frame_offset", "frame_length", "cd_track", "se_bank"])
        for r in rooms:
            frame = index[r["area"]][r["frame_offset"]]
            w.writerow([r["room"], r["title"], r["area"] + ".CGL", frame,
                        f"{r['area']}_{frame:02d}.png", r["frame_offset"],
                        r["frame_length"], r["cd_track"], r["se_bank"]])
    total = sum(len(v) for v in index.values())
    print(f"{total} backgrounds -> {png_dir}")
    print(f"{n_items} item pictures -> {item_dir}")
    print(f"{len(rooms)} rooms -> {out}")


if __name__ == "__main__":
    main()
