#!/usr/bin/env python3
"""/*----------------------
 | zork_audio.py
 | Description: Map Zork I (Saturn, JP) rooms to their CD-DA tracks and sound-effect banks.
 | Author: suinevere
 | Dependencies: zork_room_backgrounds, csv, re, os
 | Globals: VOLUME_TBL, SECTOR_BYTES, FRAMES_PER_SEC, SCREEN_DISPATCH, ONCE_CALLS
 ----------------------*/

Field +0 of each 16-byte room presentation record (see zork_room_backgrounds.py)
is a raw disc track number, not an index. The room-change handler at 0x06048c1c
reads it and hands it straight to the looping player at 0x0602a4d8, which fills a
CDC play spec with start track = value / index 1, end track = value / index 99,
mode 0x0F (repeat forever). A second player at 0x0602a578 is byte-identical apart
from mode 0x00 (play once). Both scale playback with a per-track volume byte
table at address 0x0608ef74 (file offset 0x8af74) indexed by track number.
"""
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zork_room_backgrounds import read_rooms

# /*----------------------
#  | Audio constants
#  | Description: Where the volume table lives and how CD sectors convert to time.
#  | Author: suinevere
#  ----------------------*/
VOLUME_TBL = 0x8AF74
SECTOR_BYTES = 2352
FRAMES_PER_SEC = 75

# Script music-command opcodes: the jump table at 0x06010ac8 maps command id -> track.
SCRIPT_CMD = {0: 4, 1: 5, 2: 9, 3: 10, 4: 11, 5: 12, 6: 2, 7: 22}
# Endgame rank screen: rank-name pointers at 0x060143a4, parallel track array at 0x060143c8.
RANK_TRACKS = {21: 0, 22: 1, 23: 2, 24: 3, 26: 4, 27: 5, 28: 6, 29: 7}
RANK_NAMES = ["(placeholder '????')", "Amateur Adventurer", "Novice Adventurer",
              "Junior Adventurer", "Adventurer", "Master", "Wizard",
              "Master Adventurer", "Master Adventurer"]
# The Japanese rank ladder at 0x0602acd0 writes the same tracks by score band.
RANK_SCORES = {21: "score <= 25", 22: "score > 25", 23: "score > 50",
               24: "score > 100", 26: "score > 200", 27: "score > 300",
               28: "score > 330", 29: "score = 350"}
# Tracks passed as a literal to the play-once entry point.
ONCE_CALLS = {30: "ending screen (END.CGD/END.COL), played once from 0x0600b9f2"}

# Situational music. The pending-track byte at 0x060afb10 is the only argument
# either byte-pointer player (0x06048a58 loop, 0x06048b74 once) ever reads, so
# every literal stored to it is a complete list of the event cues. Conditions
# are what the code tests immediately before the store; IN?(a, b) is the
# containment test at 0x0603b2d4 over the 22-byte object records at
# [0x0608ef98]+332, and object ids come from GAME.DAT's initial parents:
# 145 player, 180 elvish sword, 188 troll, 195 cyclops, 204 thief, 205 spirits,
# 241 vampire bat.
EVENT_TRACKS = {
    9: "the rainbow solidifies at End Of Rainbow (room 81), set at 0x06038c6c",
    13: "danger cue: sword glow drops to level 1 (0x060347a4), sword examined "
        "at level 1 (0x06038814), or a troll/cyclops fight ends with the sword "
        "still carried (0x06048dc8)",
    14: "troll in the Troll Room: room 20 and IN?(188, 20), set at 0x06048c9a",
    15: "thief present outside the Treasure Room, set at 0x06048e48",
    16: "thief present in the Treasure Room (room 43), set at 0x06048e48",
    17: "cyclops in the Cyclops Room: room 41 and IN?(195, 41), set at 0x06048d3a",
    19: "death sequence, set at 0x0603a5dc",
}
# Tracks the sound handler singles out with an explicit compare.
SPECIAL_CASED = {25: "one-shot cue armed by a successful TAKE (0x060331d8); the "
                     "loop player refuses it at 0x06048a7c so only the play-once "
                     "path at 0x06048b92 sounds it"}
# Tracks no code path can reach.
UNUSED = {
    7: "UNUSED - no code path selects it",
    32: "UNUSED - the same recording as track 30 (292 differing bytes in 46 MB) "
        "with volume-table entry 0",
}


def cue_tracks(game_dir, cue_name):
    """/*----------------------
     | cue_tracks
     | Description: Read every track from the .cue and time it from its .bin size.
     | Author: suinevere
     | Dependencies: re, os
     | Globals: SECTOR_BYTES, FRAMES_PER_SEC
     | Params: game_dir -- folder holding the cue and bins; cue_name -- the .cue filename
     | Returns: dict of track number -> {type, sectors, pregap, seconds}
     ----------------------*/"""
    cue = open(os.path.join(game_dir, cue_name), encoding="utf-8").read()
    blocks = re.findall(
        r'FILE "([^"]+)" BINARY\s+TRACK (\d+) (\S+)((?:\s+INDEX \d+ [\d:]+)+)', cue)
    out = {}
    for fname, tno, ttype, idx in blocks:
        marks = dict(re.findall(r"INDEX (\d+) ([\d:]+)", idx))
        pregap = _frames(marks.get("01", "00:00:00"))
        sectors = os.path.getsize(os.path.join(game_dir, fname)) // SECTOR_BYTES
        out[int(tno)] = {
            "type": ttype,
            "sectors": sectors,
            "pregap": pregap,
            "seconds": (sectors - pregap) / FRAMES_PER_SEC,
        }
    return out


def _frames(stamp):
    """/*----------------------
     | _frames
     | Description: Convert an mm:ss:ff cue timestamp to a frame count.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: FRAMES_PER_SEC
     | Params: stamp -- "mm:ss:ff"
     | Returns: total frames as int
     ----------------------*/"""
    m, s, f = (int(x) for x in stamp.split(":"))
    return (m * 60 + s) * FRAMES_PER_SEC + f


def mmss(seconds):
    """/*----------------------
     | mmss
     | Description: Format a duration as m:ss.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: seconds -- float duration
     | Returns: formatted str
     ----------------------*/"""
    return f"{int(seconds) // 60}:{int(seconds) % 60:02d}"


def role(track, room_count):
    """/*----------------------
     | role
     | Description: Say what a track is used for, as far as the code proves it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: SCRIPT_CMD, RANK_TRACKS, RANK_NAMES, RANK_SCORES, ONCE_CALLS,
     |          SPECIAL_CASED, EVENT_TRACKS, UNUSED
     | Params: track -- disc track number; room_count -- how many rooms loop it
     | Returns: a short description str
     ----------------------*/"""
    parts = []
    if room_count:
        parts.append(f"room BGM, looped by {room_count} "
                     f"room{'s' if room_count > 1 else ''}")
    cmds = [c for c, t in SCRIPT_CMD.items() if t == track]
    if cmds:
        parts.append(f"script music command {cmds[0]}")
    if track in RANK_TRACKS:
        parts.append(f"rank screen: {RANK_NAMES[RANK_TRACKS[track]]} "
                     f"({RANK_SCORES[track]})")
    if track in EVENT_TRACKS:
        parts.append(EVENT_TRACKS[track])
    if track in ONCE_CALLS:
        parts.append(ONCE_CALLS[track])
    if track in SPECIAL_CASED:
        parts.append(SPECIAL_CASED[track])
    if track in UNUSED:
        parts.append(UNUSED[track])
    return "; ".join(parts) or "UNATTRIBUTED - needs a runtime capture"


def main():
    """/*----------------------
     | main
     | Description: Write room_audio.csv and cd_tracks.csv.
     | Author: suinevere
     | Dependencies: cue_tracks, read_rooms, csv
     | Globals: VOLUME_TBL
     | Params: N/A
     | Returns: N/A
     ----------------------*/"""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    game_dir = os.path.join(root, "cd",
                            "Zork I - The Great Underground Empire (Japan)")
    zork_bin = open(os.path.join(game_dir, "0ZORK.BIN"), "rb").read()
    volumes = zork_bin[VOLUME_TBL:VOLUME_TBL + 40]
    tracks = cue_tracks(game_dir,
                        "Zork I - The Great Underground Empire (Japan).cue")
    rooms = read_rooms(zork_bin)

    users = {}
    for r in rooms:
        users.setdefault(r["cd_track"], []).append(r)

    out_dir = os.path.join(here, "zork_bg")
    with open(os.path.join(out_dir, "room_audio.csv"), "w", newline="",
              encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["room", "title", "area", "cd_track", "track_length",
                    "track_volume", "se_bank"])
        for r in rooms:
            t = r["cd_track"]
            w.writerow([r["room"], r["title"], r["area"],
                        t if t else "",
                        mmss(tracks[t]["seconds"]) if t else "silent",
                        volumes[t] if t else "",
                        r["se_bank"]])

    with open(os.path.join(out_dir, "cd_tracks.csv"), "w", newline="",
              encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["track", "length", "volume", "rooms", "role", "room_list"])
        for t in sorted(k for k, v in tracks.items() if v["type"] == "AUDIO"):
            us = users.get(t, [])
            w.writerow([t, mmss(tracks[t]["seconds"]), volumes[t], len(us),
                        role(t, len(us)),
                        "; ".join(f'{u["room"]} {u["title"].title()}' for u in us)])

    silent = users.get(0, [])
    print(f"{len(rooms)} rooms, {len(silent)} silent")
    print(f"{sum(1 for k, v in tracks.items() if v['type'] == 'AUDIO')} audio tracks, "
          f"{len([t for t in users if t])} used as room BGM")
    print(f"-> {out_dir}\\room_audio.csv, {out_dir}\\cd_tracks.csv")


if __name__ == "__main__":
    main()
