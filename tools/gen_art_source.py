#!/usr/bin/env python3
"""/*----------------------
 | gen_art_source.py
 | Description: Draws the source plates named in tools/assets/art/prompts.json
 |     off a local Forge server and appends them to the manifest
 |     tools/gen_art_archive.py builds from.
 |
 |     Appends, and only appends. A plate's IMAGE_FRAME index is its position
 |     in the manifest after the measured 74, and a room record stores that
 |     index -- so a plate that moved would silently become a different picture
 |     everywhere it had been chosen. An entry whose name is already in the
 |     manifest is therefore skipped rather than redrawn, which also makes a
 |     second run after a failed one cost nothing.
 |
 |     Run by hand, never by a build: the server is a machine this project
 |     happens to have, the plates it draws are committed, and a checkout
 |     without it builds every archive from what is already there.
 | Author: suinevere
 | Dependencies: argparse, json, pathlib, sys, forge_client, scene_vocab
 | Globals: ROOT, ART, PROMPTS, MANIFEST
 ----------------------*/"""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import forge_client
import scene_vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
ART = ROOT / "tools" / "assets" / "art"
PROMPTS = ART / "prompts.json"
MANIFEST = ART / "manifest.json"
PNG_DIR = ROOT / "analysis" / "zork_bg" / "png"
"""ROOT / ART / PROMPTS / MANIFEST / PNG_DIR

Description: The batch to draw and the manifest it is appended to. Both live
    beside the plates themselves so a picture, the words that produced it and
    the frame it is graded against are one thing to read. PNG_DIR is where the
    styled plates live, which a redraw has to clear: it is the source of record
    gen_art_archive falls back to, so leaving it would rebuild the archive from
    the picture being rejected.
Author: suinevere
"""


def batch(sheet=None):
    """/*----------------------
     | batch
     | Description: The plates to draw, checked before a single one is
     |     generated: a bad scene tag or a missing seed found after twenty
     |     minutes of drawing is twenty minutes lost.
     | Author: suinevere
     | Dependencies: json, scene_vocab
     | Globals: PROMPTS
     | Params: N/A
     | Returns: a list of batch entries
     ----------------------*/"""
    sheet = sheet or PROMPTS
    if not sheet.is_file():
        raise SystemExit(f"gen_art_source: no {sheet}")
    out = json.loads(sheet.read_text(encoding="utf-8"))["batch"]
    for e in out:
        for field in ("name", "prompt", "seed", "reference", "scenes", "shows"):
            if field not in e:
                raise SystemExit(f"gen_art_source: {e.get('name', '?')} has no "
                                 f"{field}")
        for s in e["scenes"]:
            if s not in scene_vocab.SCENES:
                raise SystemExit(f"gen_art_source: {e['name']} names scene {s!r}, "
                                 "which is not a tag rooms are classified into")
    return out


def manifest():
    """/*----------------------
     | manifest
     | Description: The manifest as it stands, or an empty one.
     | Author: suinevere
     | Dependencies: json
     | Globals: MANIFEST
     | Params: N/A
     | Returns: the manifest dict
     ----------------------*/"""
    if MANIFEST.is_file():
        return json.loads(MANIFEST.read_text(encoding="utf-8"))
    return {"prefix": "GEN", "plates": []}


def save_manifest(man):
    """/*----------------------
     | save_manifest
     | Description: Writes the manifest out. Separate so the drawing loop can
     |     checkpoint every so often rather than after every plate.
     | Author: suinevere
     | Dependencies: json
     | Globals: MANIFEST
     | Params: man -- the manifest dict
     | Returns: N/A
     ----------------------*/"""
    MANIFEST.write_text(json.dumps(man, indent=1, ensure_ascii=False) + "\n",
                        encoding="utf-8")


def redraw(args, plates, man):
    """/*----------------------
     | redraw
     | Description: Draws one plate again over the top of itself, keeping its
     |     place in the manifest.
     |
     |     In place is the whole point. A plate's picture index is its position
     |     in the manifest, and a room record stores that index, so removing a
     |     rejected plate and drawing a replacement on the end would renumber
     |     every plate after it -- silently handing hundreds of rooms a
     |     different picture to fix one. The entry stays exactly where it is and
     |     only the pixels change.
     |
     |     The styled plate is deleted too, not just the raw generation: it is
     |     the source of record and gen_art_archive will encode from it if it is
     |     still sitting there, which would rebuild the archive from the very
     |     picture being rejected.
     | Author: suinevere
     | Dependencies: forge_client, json
     | Globals: ART, PNG_DIR
     | Params: args -- the parsed command line; plates -- the sheet;
     |     man -- the manifest
     | Returns: 0
     ----------------------*/"""
    by_name = {e["name"]: e for e in plates}
    slots = {p["source"]: p for p in man["plates"]}
    for name in args.redraw:
        e = by_name.get(name)
        if e is None:
            raise SystemExit(f"gen_art_source: {name} is not in {args.sheet}")
        if f"{name}.png" not in slots:
            raise SystemExit(f"gen_art_source: {name} has no manifest entry to "
                             "redraw over -- draw it normally instead")
        seed = args.seed if args.seed is not None else int(e["seed"]) + 1
        print(f"  redrawing {name} at seed {seed} (was {e['seed']})", flush=True)
        (ART / f"{name}.png").write_bytes(
            forge_client.txt2img(e["prompt"], seed, host=args.host))
        kept = PNG_DIR / f"{name}.png"
        if kept.is_file():
            kept.unlink()
        slot = slots[f"{name}.png"]
        slot["reference"] = int(e["reference"])
        slot["lift"] = float(e.get("lift", 0.0))
        slot["shows"] = e["shows"]
        slot["seed"] = seed
    save_manifest(man)
    print(f"Redrew {len(args.redraw)} plate(s) in place. Run "
          "tools/gen_art_archive.py to re-encode them.")
    return 0


def main(argv=None):
    """/*----------------------
     | main
     | Description: Draws every batch entry not already in the manifest, writes
     |     each plate beside it and appends its entry.
     | Author: suinevere
     | Dependencies: argparse, json, forge_client
     | Globals: ART, MANIFEST, ROOT
     | Params: argv -- command line
     | Returns: 0
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default=forge_client.HOST)
    ap.add_argument("--sheet", default=str(PROMPTS),
                    help="the prompt sheet to draw (default the scene sheet)")
    ap.add_argument("--only", help="draw just this one batch entry, by name")
    ap.add_argument("--redraw", action="append", default=[],
                    help="redraw this plate IN PLACE, keeping its manifest "
                         "position and so its picture index; repeatable")
    ap.add_argument("--seed", type=int,
                    help="with --redraw, the seed to draw it at instead of the "
                         "sheet's; omit to step one past the sheet's")
    args = ap.parse_args(argv)

    ckpt = forge_client.alive(args.host)
    if ckpt is None:
        raise SystemExit(f"gen_art_source: no Forge API at {args.host}. Start it "
                         "with --api; without that flag the web UI works and "
                         "every endpoint here is a 404.")
    print(f"Forge at {args.host}, checkpoint {ckpt}")

    plates = batch(pathlib.Path(args.sheet))
    man = manifest()

    if args.redraw:
        return redraw(args, plates, man)

    have = {p["source"] for p in man["plates"]}
    todo = [e for e in plates
            if f"{e['name']}.png" not in have
            and (args.only is None or e["name"] == args.only)]
    if not todo:
        print("nothing to draw -- every batch entry is already in the manifest")
        return 0

    for n, e in enumerate(todo, 1):
        png = ART / f"{e['name']}.png"
        print(f"  [{n}/{len(todo)}] {e['name']} seed {e['seed']} ...", flush=True)
        png.write_bytes(forge_client.txt2img(e["prompt"], e["seed"], host=args.host))
        man["plates"].append({
            "source": png.name,
            "reference": int(e["reference"]),
            "grain": float(e.get("grain", 0.0)),
            "lift": float(e.get("lift", 0.0)),
            "shows": e["shows"],
            "scenes": list(e["scenes"]),
            **({"game": e["game"], "obj": int(e["obj"])}
               if "game" in e and "obj" in e else {}),
        })
        # Every 25 rather than every plate: the manifest is rewritten whole, and
        # at nearly two thousand entries doing that per plate is a gigabyte of
        # writes, to save at most 24 plates of a run that is already resumable.
        if n % 25 == 0:
            save_manifest(man)
    save_manifest(man)

    print(f"Drew {len(todo)} plate(s); {len(man['plates'])} in the manifest. "
          "Run tools/gen_art_archive.py, then gen_presentation.py and "
          "gen_pool.py.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
