#!/usr/bin/env python3
"""/*----------------------
 | gen_pool.py
 | Description: GENERATES tools/assets/zork1_pool.json -- the catalogue of the
 |     74 pictures and 13 CD-DA tracks Zork I's Saturn release actually used,
 |     which is the entire art supply for every game on the disc.
 |
 |     Every picture is named by the 1-based index game_presentation.inc's
 |     IMAGE_FRAME table uses, because that index -- not the PNG filename -- is
 |     what a room record stores and what room_art.cxx resolves. The PNG name
 |     is carried alongside purely so the review app has something to show.
 |
 |     Indices are recovered by joining room_backgrounds.csv to IMAGE_FRAME on
 |     (area, frame_offset) rather than by assuming the CSV's row order matches
 |     the generated table's. The two are built by different scripts from
 |     different sort orders, and an off-by-one here would silently hand every
 |     room its neighbour's picture.
 |
 |     Also carries, per scene tag, what Zork I did with rooms bearing that tag
 |     -- the only evidence available for suggesting pictures and tracks to the
 |     other 30 games, whose rooms have a scene tag and nothing else. Scenes
 |     Zork I never exercised get no entry at all rather than a fabricated one;
 |     SCENE_ANALOGUE below is where a human names the stand-in, and it is
 |     marked as such so the app can show it with less confidence.
 | Author: suinevere
 | Dependencies: csv, json, pathlib, re, sys
 | Globals: ROOT, INC, CSV, TRACKS, SCENES, OUT, SCENE_ANALOGUE
 | Run: python tools/gen_pool.py
 ----------------------*/"""
import collections
import csv
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INC = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"
CSV = ROOT / "analysis" / "zork_bg" / "room_backgrounds.csv"
TRACKS = ROOT / "analysis" / "zork_bg" / "cd_tracks.csv"
SCENES = ROOT / "tools" / "assets" / "scenes" / "ZORK1.json"
OUT = ROOT / "tools" / "assets" / "zork1_pool.json"

SCENE_ANALOGUE = {
    "CORRIDOR":  37,   # BCEL_05 East-West Passage -- worked stone corridor
    "GARDEN":     1,   # BWOD_01 Clearing -- open ground ringed by trees
    "DESERT":    61,   # BRIV_06 Sandy Beach -- the only sand in the set
    "VILLAGE":    8,   # BHUS_00 West of House -- a building seen from outside
    "CASTLE":    46,   # BTMP_03 Temple -- the only monumental masonry
    "DOCK":      63,   # BRIV_05 Shore -- water meeting walkable ground
    "BEDROOM":    9,   # BHUS_05 Attic -- the only domestic upper room
    "BATHROOM":  10,   # BHUS_03 Kitchen -- the only tiled service room
    "LIBRARY":   15,   # BCEL_10 Studio -- interior with worked furnishings
    "OFFICE":    11,   # BHUS_04 Living Room -- furnished interior
    "LAB":       51,   # BDAM_00 Control Room -- the only instrumented room
    "STORAGE":   14,   # BCEL_09 Gallery -- interior holding objects
    "CELL":      12,   # BCEL_07 Cellar -- bare stone enclosure
    "CRYPT":     41,   # BDED_00 Entrance to Hades -- the funerary image
    "THEATER":   43,   # BTMP_01 Dome Room -- the only large vaulted space
    "SHIP_EXT":  60,   # BRIV_02 Frigid River -- open water
    "SHIP_INT":  70,   # BMIN_08 Machine Room -- enclosed machinery
    "SPACE":     42,   # BDED_01 Land of the Living Dead -- the void-like frame
}
"""SCENE_ANALOGUE

Description: A stand-in picture for each of the 18 scene tags Zork I never
    used, so a room in one of them opens on something deliberate instead of
    blank. Chosen by visual analogue from the 74 available, one line of
    reasoning each. These are suggestions the review app shows and a human
    overrides -- they are guesses, and the app marks them as guesses, which is
    the difference between this and the learned defaults beside them.
Author: suinevere
"""


def image_index():
    """/*----------------------
     | image_index
     | Description: PNG name -> the 1-based IMAGE_FRAME index that names it,
     |     recovered by joining on (area stem, frame offset). Both sides record
     |     that pair independently, so agreeing on it is a real check rather
     |     than an assumption about row order.
     | Author: suinevere
     | Dependencies: csv, re
     | Globals: INC, CSV
     | Params: N/A
     | Returns: (name -> index, index -> area stem)
     ----------------------*/"""
    inc = INC.read_text(encoding="utf-8")
    areas = re.findall(r'"([A-Z]+)"', re.search(
        r"PRES_AREA\[PRES_AREA_N\] = \{(.*?)\n\};", inc, re.S).group(1))
    frames = re.search(r"IMAGE_FRAME\[PRES_FRAME_N\] = \{(.*?)\n\};", inc, re.S).group(1)
    by_key = {}
    idx_area = {}
    for i, (a, off, _ln) in enumerate(
            re.findall(r"\{\s*(\d+),\s*(\d+)UL,\s*(\d+)UL\s*\}", frames), 1):
        stem = areas[int(a)]
        by_key[(stem, int(off))] = i
        idx_area[i] = stem

    name = {}
    with CSV.open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            key = (r["area_archive"][:4], int(r["frame_offset"]))
            if key not in by_key:
                raise SystemExit(
                    f"gen_pool: {r['image']} at {key} is in the CSV but not in "
                    "IMAGE_FRAME -- the two are out of step, regenerate the .inc")
            name[r["image"]] = by_key[key]
    return name, idx_area


def rooms_by_image(name_to_idx):
    """/*----------------------
     | rooms_by_image
     | Description: Which Zork I rooms each picture served, and which tracks
     |     played over it. The room titles are what makes a picture choosable in
     |     the review app -- an index and a filename say nothing about what is
     |     depicted, and "the picture Zork I showed for Loud Room" does.
     | Author: suinevere
     | Dependencies: csv, collections
     | Globals: CSV
     | Params: name_to_idx -- PNG name -> image index
     | Returns: index -> {"rooms": [...], "tracks": [...]}
     ----------------------*/"""
    out = collections.defaultdict(lambda: {"rooms": [], "tracks": set()})
    with CSV.open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            i = name_to_idx[r["image"]]
            if r["title"] not in out[i]["rooms"]:
                out[i]["rooms"].append(r["title"])
            out[i]["tracks"].add(int(r["cd_track"]))
    return out


def track_table():
    """/*----------------------
     | track_table
     | Description: The CD-DA tracks, with the length and role measured off the
     |     original disc. Track 0 is not a track -- it is the table's way of
     |     saying silence -- and is carried here explicitly so the review app can
     |     offer it as a choice rather than treating its absence as a gap.
     | Author: suinevere
     | Dependencies: csv
     | Globals: TRACKS
     | Params: N/A
     | Returns: a list of track records
     ----------------------*/"""
    out = [{"track": 0, "length": "", "role": "silence", "rooms": 0}]
    with TRACKS.open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            out.append({
                "track": int(r["track"]),
                "length": r["length"],
                "role": r["role"],
                "rooms": int(r["rooms"]),
            })
    return out


def scene_evidence(name_to_idx):
    """/*----------------------
     | scene_evidence
     | Description: What Zork I did with the rooms carrying each scene tag --
     |     the picture and track counts, so the review app can both suggest a
     |     default and show how well supported it is. A tag whose rooms took ten
     |     different pictures is a weak suggestion and has to look like one.
     |
     |     Reads the object -> picture join out of game_presentation.inc rather
     |     than redoing it here. The join is the hard part of this whole feature
     |     -- thirteen renamed rooms, two hand-pinned rows, same-title groups
     |     resolved by order -- and gen_presentation.py already did it and
     |     refuses rather than guessing. Rejoining by title here would be a
     |     second, worse implementation of it: repeated titles like the fifteen
     |     MAZE rooms would cross-join fifteen objects against fifteen rows and
     |     report 225 pieces of evidence where there are 15.
     | Author: suinevere
     | Dependencies: json, re, collections
     | Globals: SCENES, INC
     | Params: name_to_idx -- PNG name -> image index, unused but kept so the
     |     caller need not know which join this side uses
     | Returns: scene -> evidence record
     ----------------------*/"""
    inc = INC.read_text(encoding="utf-8")
    tbl = re.search(r"GAME_PRES_ZORK1\[256\] = \{(.*?)\n\};", inc, re.S)
    if tbl is None:
        raise SystemExit("gen_pool: GAME_PRES_ZORK1 not found in the .inc")
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", tbl.group(1))
    pres = {i: (int(img), int(trk)) for i, (img, trk, _se) in enumerate(rows)}

    scenes = json.loads(SCENES.read_text(encoding="utf-8"))
    ev = collections.defaultdict(lambda: {"images": collections.Counter(),
                                          "tracks": collections.Counter()})
    for obj, scene in scenes.items():
        img, trk = pres.get(int(obj), (0, 0))
        if img == 0:
            continue
        ev[scene]["images"][img] += 1
        ev[scene]["tracks"][trk] += 1

    out = {}
    for scene, d in ev.items():
        if not d["images"]:
            continue
        n = sum(d["images"].values())
        img, ic = d["images"].most_common(1)[0]
        trk, tc = d["tracks"].most_common(1)[0]
        out[scene] = {
            "image": img, "track": trk, "n": n,
            "image_support": ic, "track_support": tc,
            "images": dict(sorted(d["images"].items())),
            "tracks": dict(sorted(d["tracks"].items())),
            "source": "measured",
        }
    for scene, img in SCENE_ANALOGUE.items():
        if scene in out:
            continue
        out[scene] = {"image": img, "track": 0, "n": 0,
                      "image_support": 0, "track_support": 0,
                      "images": {}, "tracks": {}, "source": "analogue"}
    return out


def main():
    """/*----------------------
     | main
     | Description: Writes the catalogue.
     | Author: suinevere
     | Dependencies: json
     | Globals: OUT
     | Params: N/A
     | Returns: 0
     ----------------------*/"""
    name_to_idx, idx_area = image_index()
    used = rooms_by_image(name_to_idx)
    idx_to_name = {v: k for k, v in name_to_idx.items()}

    images = []
    for i in sorted(idx_to_name):
        images.append({
            "index": i,
            "png": idx_to_name[i],
            "area": idx_area[i],
            "rooms": used[i]["rooms"],
            "tracks": sorted(used[i]["tracks"]),
        })

    pool = {
        "_comment": "GENERATED by tools/gen_pool.py -- the Zork I picture and "
                    "track supply every game on the disc draws from. index is "
                    "the IMAGE_FRAME index a room record stores.",
        "images": images,
        "tracks": track_table(),
        "scene_defaults": scene_evidence(name_to_idx),
    }
    OUT.write_text(json.dumps(pool, indent=1, sort_keys=False) + "\n", encoding="utf-8")
    print(f"Wrote {OUT.relative_to(ROOT)}: {len(images)} images, "
          f"{len(pool['tracks'])} tracks, {len(pool['scene_defaults'])} scene defaults")
    return 0


if __name__ == "__main__":
    sys.exit(main())
