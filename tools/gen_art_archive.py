#!/usr/bin/env python3
"""/*----------------------
 | gen_art_archive.py
 | Description: GENERATES the generated-art archives -- new B*.CGL files
 |     carrying pictures that were never on the original disc -- and
 |     tools/assets/art/frames.json, the record of where each one landed that
 |     gen_presentation.py extends IMAGE_FRAME from.
 |
 |     The three things this reconciles are that the source plates are ours and
 |     committed, the archives are derived and are not, and the frame offsets
 |     have to be in a committed table because the runtime reaches a frame by
 |     offset alone. So the offsets are written down here, next to a SHA-256 of
 |     the archive they were measured in, and a rebuild that produces different
 |     bytes says so instead of leaving the table pointing into the middle of a
 |     record. That rebuild has to be reproducible for this to hold: the
 |     encoder has no randomness, and stylise's grain is drawn from a seeded
 |     generator for the same reason.
 |
 |     Styling needs the original archives, because a plate is graded against
 |     the FRAME it stands beside rather than against a description -- so this
 |     runs after tools/assets/bg.bat has staged them, and refuses rather than
 |     grading against nothing.
 |
 |     Nothing here assigns a picture to a room. A generated frame enters the
 |     supply and is chosen the same way any other picture is, through the
 |     review app.
 | Author: suinevere
 | Dependencies: argparse, art_frames, csv, json, pathlib, sys, PIL,
 |     cgl_archive, room_art_style, scene_vocab
 | Globals: ROOT, ART, MANIFEST, FRAMES, CSV, PNG_DIR, BG_STAGE, BG_LOCAL,
 |     PREFIX
 ----------------------*/"""
import argparse
import csv
import json
import pathlib
import sys

from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import art_frames
import cgl_archive
import room_art_style
import scene_vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
ART = ROOT / "tools" / "assets" / "art"
MANIFEST = ART / "manifest.json"
FRAMES = art_frames.FRAMES
CSV = ROOT / "analysis" / "zork_bg" / "room_backgrounds.csv"
PNG_DIR = ROOT / "analysis" / "zork_bg" / "png"
BG_STAGE = ROOT / "tools" / "assets" / "BG"
BG_LOCAL = ROOT / "saturn" / "cd" / "data" / "BG"
PREFIX = "GEN"
"""ROOT / ART / MANIFEST / FRAMES / CSV / PNG_DIR / BG_STAGE / BG_LOCAL / PREFIX

Description: Where the source plates and their manifest live, where the
    placements are written, the extraction table the measured frame count is
    counted out of, where the review app looks for a picture to show,
    the two directories a B*.CGL has to reach (bg.bat's staging directory and
    the SDK's own data tree, exactly the pair bg.bat mirrors between), and the
    stem prefix the generated archives are named with. The prefix is letters
    only because two other generators read PRES_AREA back with a letters-only
    regex.
Author: suinevere
"""


def load_manifest():
    """/*----------------------
     | load_manifest
     | Description: The plates to build, in the order they will be packed --
     |     which is the order their frame indices are assigned in, so a plate
     |     may be appended but never reordered or removed without every room
     |     record that names a later one meaning a different picture.
     | Author: suinevere
     | Dependencies: json
     | Globals: MANIFEST, ART
     | Params: N/A
     | Returns: a list of plate dicts
     ----------------------*/"""
    if not MANIFEST.is_file():
        return []
    man = json.loads(MANIFEST.read_text(encoding="utf-8"))
    plates = man.get("plates", [])
    for p in plates:
        src = ART / p["source"]
        if not src.is_file():
            raise SystemExit(f"gen_art_archive: {p['source']} is in the manifest "
                             f"but not in {ART.relative_to(ROOT)}")
        if not isinstance(p.get("reference"), int):
            raise SystemExit(f"gen_art_archive: {p['source']} names no reference "
                             "frame -- a plate is graded against the picture it "
                             "stands beside, and there is no default")
        if not p.get("scenes"):
            raise SystemExit(f"gen_art_archive: {p['source']} names no scenes, so "
                             "nothing would ever pick it -- an unpickable picture "
                             "is disc space and a maintenance cost and no more")
        for s in p["scenes"]:
            if s not in scene_vocab.SCENES:
                raise SystemExit(f"gen_art_archive: {p['source']} names scene "
                                 f"{s!r}, which is not one of the tags rooms are "
                                 "actually classified into")
    return plates


def measured():
    """/*----------------------
     | measured
     | Description: How many frames the original disc supplies -- the count of
     |     distinct (archive, frame) pairs Zork I's rooms reference, which is
     |     exactly what gen_presentation.py lays down before the generated ones.
     |     Counted out of the extraction table rather than read back out of the
     |     .inc, so a generated frame's index does not depend on a file this run
     |     is about to make stale.
     | Author: suinevere
     | Dependencies: csv
     | Globals: CSV
     | Params: N/A
     | Returns: the measured frame count
     ----------------------*/"""
    with CSV.open(newline="", encoding="utf-8") as f:
        return len({(r["area_archive"], r["frame"]) for r in csv.DictReader(f)})


def styled(plate):
    """/*----------------------
     | styled
     | Description: One source plate put into the house style, as the
     |     (palette, pixels) pair a record is built from plus the paletted image
     |     for the preview.
     | Author: suinevere
     | Dependencies: PIL, room_art_style
     | Globals: ART
     | Params: plate -- one manifest entry
     | Returns: (image, palette, pixel bytes)
     ----------------------*/"""
    with Image.open(ART / plate["source"]) as im:
        q, pal = room_art_style.stylise(im, int(plate["reference"]),
                                        grain=float(plate.get("grain", 0.0)))
    return q, pal, q.tobytes()


def write_dirs(blobs):
    """/*----------------------
     | write_dirs
     | Description: Puts each archive in both places a B*.CGL has to be: the
     |     staging directory games.bat maps to /BG, and the SDK's data tree that
     |     a plain compile-cd.bat bakes into the image. Either being absent is
     |     not an error -- the release kit has no saturn tree and a checkout
     |     that has never run bg.bat has no staging directory -- but writing
     |     nowhere is, because that is a run that looks like it worked.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: BG_STAGE, BG_LOCAL
     | Params: blobs -- {stem: bytes}
     | Returns: the list of directories written to
     ----------------------*/"""
    written = []
    for d in (BG_STAGE, BG_LOCAL):
        if not d.parent.is_dir():
            continue
        d.mkdir(parents=True, exist_ok=True)
        for stem, blob in blobs.items():
            (d / f"{stem}.CGL").write_bytes(blob)
        written.append(d)
    if blobs and not written:
        raise SystemExit("gen_art_archive: neither tools/assets nor "
                         "saturn/cd/data is present, so the archives would go "
                         "nowhere the disc build can find them")
    return written


def previews(blobs, rows, images):
    """/*----------------------
     | previews
     | Description: Writes each styled plate out beside the disc's own extracted
     |     pictures, under the same <STEM>_<NN>.png name, because that directory
     |     is what the review app serves a picture from and a generated frame
     |     has to be lookable-at to be choosable.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: PNG_DIR
     | Params: blobs -- {stem: bytes}; rows -- the placements; images -- the
     |     paletted images in the same order
     | Returns: the list of PNG names, in frame order
     ----------------------*/"""
    seen = {stem: 0 for stem in blobs}
    names = []
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    for row, im in zip(rows, images):
        stem = row["archive"]
        name = f"{stem}_{seen[stem]:02d}.png"
        seen[stem] += 1
        im.save(PNG_DIR / name)
        names.append(name)
    return names


def main(argv=None):
    """/*----------------------
     | main
     | Description: Builds every plate in the manifest, writes the archives and
     |     the placements, and prints what a person needs to see: how many
     |     frames, how big each archive came out and how much of the cap it
     |     spent.
     | Author: suinevere
     | Dependencies: argparse, json, cgl_archive
     | Globals: FRAMES, ROOT, PREFIX
     | Params: argv -- command line
     | Returns: 0
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cap", type=int, default=cgl_archive.CAP,
                    help="byte ceiling for one archive")
    args = ap.parse_args(argv)

    plates = load_manifest()
    if not plates:
        FRAMES.parent.mkdir(parents=True, exist_ok=True)
        FRAMES.write_text(json.dumps({"archives": [], "frames": []}, indent=1)
                          + "\n", encoding="utf-8")
        print("gen_art_archive: no plates in the manifest; wrote an empty "
              f"{FRAMES.relative_to(ROOT)}")
        return 0

    images, frames = [], []
    for p in plates:
        im, pal, pix = styled(p)
        images.append(im)
        frames.append((pal, pix))

    base = measured()
    blobs, rows, sums = cgl_archive.build(frames, PREFIX, cap=args.cap)
    names = previews(blobs, rows, images)
    dirs = write_dirs(blobs)

    out = {
        "_comment": "GENERATED by tools/gen_art_archive.py -- where each "
                    "generated picture lies inside its archive. The archives "
                    "themselves are not committed; this is what lets "
                    "gen_presentation.py extend IMAGE_FRAME without them.",
        "prefix": PREFIX,
        "cap": args.cap,
        "archives": [{"stem": s, "bytes": len(b), "sha256": sums[s]}
                     for s, b in blobs.items()],
        "frames": [{"index": base + i + 1, "source": p["source"],
                    "reference": int(p["reference"]),
                    "shows": p.get("shows", ""),
                    "scenes": list(p["scenes"]), "png": n, **r}
                   for i, (p, r, n) in enumerate(zip(plates, rows, names))],
    }
    FRAMES.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")

    print(f"Wrote {FRAMES.relative_to(ROOT)}: {len(rows)} frames in "
          f"{len(blobs)} archive(s), numbered {base + 1}..{base + len(rows)}")
    for s, b in blobs.items():
        print(f"  {s}.CGL {len(b):>7} bytes  "
              f"{100 * len(b) // args.cap}% of the cap")
    for d in dirs:
        print(f"  archives written to {d.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
