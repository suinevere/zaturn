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
 | Dependencies: argparse, art_frames, csv, hashlib, json, pathlib, sys,
 |     PIL, cgl_archive, room_art_style, scene_vocab
 | Globals: ROOT, ART, MANIFEST, FRAMES, CSV, PNG_DIR, BG_STAGE, BG_LOCAL,
 |     PREFIX
 ----------------------*/"""
import argparse
import csv
import hashlib
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
     |
     |     A plate is buildable from its raw generation OR from its styled
     |     plate, which is the source of record and the only one of the two
     |     that is committed -- so this asks for either, and refuses only a
     |     plate that has neither. Asking for the raw alone made the documented
     |     checkout-with-no-raw-generations case impossible, and refused every
     |     plate of the scene supply on a machine that had not drawn it.
     | Author: suinevere
     | Dependencies: json
     | Globals: MANIFEST, ART, PNG_DIR
     | Params: N/A
     | Returns: a list of plate dicts
     ----------------------*/"""
    if not MANIFEST.is_file():
        return []
    man = json.loads(MANIFEST.read_text(encoding="utf-8"))
    plates = man.get("plates", [])
    for p in plates:
        raw, kept = ART / p["source"], PNG_DIR / preview_name(p)
        if not raw.is_file() and not kept.is_file():
            raise SystemExit(f"gen_art_archive: {p['source']} is in the manifest "
                             f"with neither a raw generation in "
                             f"{ART.relative_to(ROOT)} nor a styled plate at "
                             f"{kept.relative_to(ROOT)}")
        if not isinstance(p.get("reference"), int):
            raise SystemExit(f"gen_art_archive: {p['source']} names no reference "
                             "frame -- a plate is graded against the picture it "
                             "stands beside, and there is no default")
        # A plate that names a room is picked by that room's own record and
        # needs no scene to be reachable. A plate that names neither is
        # unreachable, which is disc space and a maintenance cost and no more --
        # 951 of the 1,931 rooms carry no scene tag, so refusing on scenes alone
        # would have thrown away half of a per-room run after it had drawn it.
        if not p.get("scenes") and not ("game" in p and "obj" in p):
            raise SystemExit(f"gen_art_archive: {p['source']} names neither a "
                             "scene nor a room, so nothing would ever pick it")
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


def area_keys(plates):
    """/*----------------------
     | area_keys
     | Description: One grouping key per plate, so that an archive holds a
     |     region of one game's map rather than whatever happened to be next in
     |     the manifest.
     |
     |     The key is the game, and the ORDER within it is a DEPTH-first walk of
     |     that game's own exit graph, so the cap cuts contiguous regions of the
     |     map and an archive holds rooms the player actually walks between.
     |
     |     Depth-first, and the difference is not small. Measured over the four
     |     games drawn so far, on adjacent-room moves that force a reload:
     |
     |       depth-first walk    28.4%
     |       object number       39.9%
     |       breadth-first walk  40.2%
     |       by CD-DA track      46.0%
     |
     |     Depth-first follows a corridor to its end and so lays down runs of
     |     connected rooms; breadth-first fans across every branch at once and
     |     interleaves places that are nowhere near each other. Grouping by
     |     track was tried first because that is demonstrably how the original
     |     disc is laid out -- all 54 of Zork I's archive crossings are track
     |     changes -- and it came last, because Zork I's authors made place and
     |     music the same decision while ours are guessed from scene tags, so
     |     sorting by track scatters neighbours that merely differ in music.
     |     28.4% is also better than the 34.6% the disc paid before any of this,
     |     which is the number that matters: it is now cheaper to walk around
     |     with a picture for every room than it was with one picture for twenty.
     |
     |     A plate that belongs to no room -- the scene supply, which any game
     |     may draw on -- is keyed apart from every game and so gets its own
     |     archives.
     | Author: suinevere
     | Dependencies: collections, pres_store, zexits
     | Globals: N/A
     | Params: plates -- the manifest entries
     | Returns: (keys, rank) -- both parallel to plates: the key an archive may
     |     not span, and the position within it to lay records down in
     ----------------------*/"""
    import collections
    import pres_store as store
    import zexits

    rank = {}
    for stem in store.games():
        raw = zexits.story(stem)
        rooms = sorted(int(r["obj"]) for r in store.rooms(stem))
        adj = zexits.neighbours(zexits.graph(raw)) if raw else {}
        seen, order = set(), []
        for start in rooms:
            if start in seen:
                continue
            stack = [start]
            seen.add(start)
            while stack:
                o = stack.pop()
                order.append(o)
                for k in sorted(adj.get(o, ()), reverse=True):
                    if k not in seen:
                        seen.add(k)
                        stack.append(k)
        for i, o in enumerate(order):
            rank[(stem, o)] = i

    keys, order = [], []
    for p in plates:
        if "game" in p and "obj" in p:
            keys.append(p["game"])
            order.append(rank.get((p["game"], int(p["obj"])), 1 << 30))
        else:
            keys.append("")
            order.append(0)
    return keys, order


def fingerprint(plate):
    """/*----------------------
     | fingerprint
     | Description: Everything that decides what a plate's styled picture looks
     |     like: the bytes it was generated as, the frame it is graded against,
     |     and the three numbers the styler takes. If all of those are what they
     |     were when the styled plate was written, the styled plate is still
     |     right and does not have to be made again.
     |
     |     A raw generation that is not on this machine fingerprints as its own
     |     name. That is not a weakness: without it there is nothing to restyle
     |     from either, so the styled plate is the only thing there is.
     | Author: suinevere
     | Dependencies: hashlib
     | Globals: ART
     | Params: plate -- one manifest entry
     | Returns: a hex digest
     ----------------------*/"""
    h = hashlib.sha256()
    src = ART / plate["source"]
    h.update(src.read_bytes() if src.is_file() else plate["source"].encode())
    for k in ("reference", "grain"):
        h.update(f"|{k}={plate.get(k, 0)}".encode())
    h.update(f"|margin={room_art_style.MARGIN}".encode())
    h.update(f"|target={room_art_style.TARGET_MEAN}".encode())
    return h.hexdigest()


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
    src = ART / plate["source"]
    if src.is_file():
        with Image.open(src) as im:
            q, pal = room_art_style.stylise(im, int(plate["reference"]),
                                            grain=float(plate.get("grain", 0.0)))
        return q, pal, q.tobytes()

    # No raw generation on this machine: the styled plate is the source of
    # record and encodes to the same record byte for byte, having been written
    # out of exactly the pixels and palette the record is built from.
    kept = PNG_DIR / preview_name(plate)
    if not kept.is_file():
        raise SystemExit(f"gen_art_archive: {plate['source']} has neither a raw "
                         f"generation in {ART.relative_to(ROOT)} nor a styled "
                         f"plate at {kept.relative_to(ROOT)}")
    return from_preview(kept)


def from_preview(kept):
    """/*----------------------
     | from_preview
     | Description: One already-styled plate read straight back as the
     |     (palette, pixels) a record is built from. It was written out of
     |     exactly those, so this encodes to the same record byte for byte --
     |     which is what lets the styling be skipped for a plate nothing has
     |     changed about, and what lets a checkout with no raw generations build
     |     every archive.
     | Author: suinevere
     | Dependencies: PIL
     | Globals: N/A
     | Params: kept -- the styled plate
     | Returns: (image, palette, pixel bytes)
     ----------------------*/"""
    q = Image.open(kept)
    if q.mode != "P":
        raise SystemExit(f"gen_art_archive: {kept.name} is {q.mode}, not the "
                         "paletted image a record is built from")
    pal = list(q.getpalette() or [])
    pal += [0] * (256 * 3 - len(pal))
    return q, [(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]) for i in range(256)], q.tobytes()


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


def preview_name(plate):
    """/*----------------------
     | preview_name
     | Description: The filename one plate's styled picture is kept under.
     |     Named for the plate, never for where it landed in an archive: at
     |     nearly two thousand plates the packing shuffles whenever anything is
     |     added, and a preview whose name moved would break every reference to
     |     it and, worse, would stop the styled plate being findable as the
     |     source of record for the plate it belongs to.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: plate -- one manifest entry
     | Returns: a bare PNG filename
     ----------------------*/"""
    return pathlib.Path(plate["source"]).stem + ".png"


def previews(plates, images):
    """/*----------------------
     | previews
     | Description: Writes each styled plate out beside the disc's own extracted
     |     pictures, under the same <STEM>_<NN>.png name, because that directory
     |     is what the review app serves a picture from and a generated frame
     |     has to be lookable-at to be choosable.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: PNG_DIR
     | Params: plates -- the manifest entries; images -- the paletted images in
     |     the same order
     | Returns: the list of PNG names, in frame order
     ----------------------*/"""
    names = []
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    for plate, im in zip(plates, images):
        name = preview_name(plate)
        # Written every time, not only when absent. The styled plate is the
        # source of record, so one that is left behind after the styler changes
        # -- a new margin, a new lift -- is not merely a stale preview: the next
        # rebuild encodes the archive from it, and the disc quietly keeps the
        # picture the change was made to get rid of.
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
    ap.add_argument("--jobs", type=int, default=cgl_archive.default_jobs(),
                    help="how many encoders to run at once; 1 stays in this "
                         "process and leaves the machine alone")
    args = ap.parse_args(argv)

    plates = load_manifest()
    if not plates:
        FRAMES.parent.mkdir(parents=True, exist_ok=True)
        FRAMES.write_text(json.dumps({"archives": [], "frames": []}, indent=1)
                          + "\n", encoding="utf-8")
        print("gen_art_archive: no plates in the manifest; wrote an empty "
              f"{FRAMES.relative_to(ROOT)}")
        return 0

    # What each plate looked like last time, so a plate whose inputs have not
    # moved is not styled again. One redraw used to cost a full restyle of
    # every plate on the disc, which at 109 was five minutes and at 1,931 would
    # be an hour and a half to change one picture.
    was = {}
    if art_frames.FRAMES.is_file():
        for f in art_frames.frames():
            if "fingerprint" in f:
                was[f["source"]] = f["fingerprint"]

    prints, images, frames, restyled = [], [], [], 0
    for p in plates:
        fp = fingerprint(p)
        prints.append(fp)
        kept = PNG_DIR / preview_name(p)
        if was.get(p["source"]) == fp and kept.is_file():
            im, pal, pix = from_preview(kept)
        else:
            im, pal, pix = styled(p)
            restyled += 1
        images.append(im)
        frames.append((pal, pix))
    print(f"styled {restyled} of {len(plates)} plates "
          f"({len(plates) - restyled} unchanged)")

    base = measured()

    # Packed in area order, indexed in manifest order. A frame's index is its
    # position in the manifest and a room record stores that index, so the order
    # here must not move -- but nothing requires the BYTES to be laid down in
    # that order, because every frame records its own archive and offset. So the
    # records are packed grouped by area and the placements are scattered back.
    keys, walk = area_keys(plates)
    order = sorted(range(len(plates)),
                   key=lambda i: (keys[i], walk[i], plates[i]["source"]))
    # The encoder is a byte-at-a-time LZSS in Python and a picture per room is
    # half an hour of it, which read as a hang the first time it was waited on.
    def said(done, total):
        if done == total or done % 50 == 0:
            print(f"  encoded {done}/{total} records", flush=True)

    blobs, packed, sums = cgl_archive.build(
        [frames[i] for i in order], PREFIX, cap=args.cap,
        keys=[keys[i] for i in order], progress=said, jobs=args.jobs)
    rows = [None] * len(plates)
    for slot, i in enumerate(order):
        rows[i] = packed[slot]
    names = previews(plates, images)
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
                    "scenes": list(p.get("scenes", [])), "png": n,
                    "fingerprint": fp,
                    **({"game": p["game"], "obj": p["obj"]}
                       if "game" in p and "obj" in p else {}), **r}
                   for i, (p, r, n, fp) in enumerate(
                       zip(plates, rows, names, prints))],
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
