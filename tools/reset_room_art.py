#!/usr/bin/env python3
"""/*----------------------
 | reset_room_art.py
 | Description: Clears every generated ROOM plate so the whole set can be drawn
 |     again, and leaves everything else alone.
 |
 |     Wanted when the prompts or the sampler settings have moved far enough
 |     that redrawing the changed ones is not a saving. That is where this ends
 |     up after a long argument with the pictures: the checkpoint, the guidance
 |     and the step count all changed, and so did every prompt in the sheet, so
 |     there is no plate on disk that was drawn the way the next one will be.
 |
 |     Three things are deliberately NOT touched. The 109 hand-authored scene
 |     plates at the head of the manifest, which no room names and which were
 |     not drawn from the sheet -- they occupy a clean prefix, so the room
 |     plates can be cut off the end without renumbering them. The 75 B*_NN.png
 |     frames measured off the original disc, which sit in the same directory
 |     as the styled plates and are the thing every plate is graded against.
 |     And frames.json, which gen_art_archive rewrites from the manifest.
 |
 |     Renumbering the room plates is safe here only because nothing downstream
 |     keeps an index by hand: assign_room_art rewrites the presentation
 |     records, gen_presentation rewrites the table, and neither has run
 |     against hardware yet. It would not be safe on a disc that had shipped.
 | Author: suinevere
 | Dependencies: argparse, json, pathlib, shutil, sys, time
 | Globals: ROOT, ART, MANIFEST, PNG_DIR
 ----------------------*/"""
import argparse
import json
import pathlib
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
ART = ROOT / "tools" / "assets" / "art"
MANIFEST = ART / "manifest.json"
SHEET = ART / "room_prompts.json"
PNG_DIR = ROOT / "analysis" / "zork_bg" / "png"
"""ROOT / ART / MANIFEST / PNG_DIR

Description: The manifest, the raw plates beside it, and the styled plates that
    gen_art_archive encodes from. A raw plate left behind would be skipped as
    already drawn; a styled one left behind would be packed in place of the
    redraw, which is the more dangerous of the two because it is silent.
Author: suinevere
"""


def main(argv=None):
    """/*----------------------
     | main
     | Description: Reports what would go, and removes it when told to.
     | Author: suinevere
     | Dependencies: argparse, json, shutil, time
     | Globals: ART, MANIFEST, PNG_DIR
     | Params: argv -- command line
     | Returns: 0
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--apply", action="store_true",
                    help="actually remove them; without this nothing is "
                         "touched and the counts are printed")
    args = ap.parse_args(argv)

    if not MANIFEST.is_file():
        raise SystemExit(f"reset_room_art: no {MANIFEST}")
    man = json.loads(MANIFEST.read_text(encoding="utf-8"))
    plates = man.get("plates", [])
    keep = [p for p in plates if "game" not in p]
    rooms = [p for p in plates if "game" in p]

    # The hand-authored plates have to be a prefix, or cutting the room plates
    # off the end would renumber one of them.
    if plates[:len(keep)] != keep:
        raise SystemExit(
            "reset_room_art: the hand-authored plates are not a prefix of the "
            "manifest, so the room plates cannot be cut off the end without "
            "renumbering one of them. Nothing done.")

    # By the SHEET and not only by the manifest. gen_art_source checkpoints
    # the manifest every 25 plates, so at any moment up to 24 drawn plates are
    # on disk and not yet listed in it -- invisible to a reset that trusts the
    # manifest, and left behind to be reviewed as though they were current.
    # 23 survived one this way, including the plate that prompted this.
    names = {p["source"] for p in rooms}
    if SHEET.is_file():
        import json as _json
        names |= {e["name"] + ".png" for e in
                  _json.loads(SHEET.read_text(encoding="utf-8"))["batch"]}
    kept_names = {p["source"] for p in keep}
    names -= kept_names

    have_raw = [ART / n for n in sorted(names) if (ART / n).is_file()]
    have_styled = [PNG_DIR / n for n in sorted(names) if (PNG_DIR / n).is_file()]
    unlisted = len([n for n in names
                    if n not in {p["source"] for p in rooms}
                    and (ART / n).is_file()])

    print(f"manifest      : {len(plates)} plates -> {len(keep)} kept, "
          f"{len(rooms)} room plates cut")
    print(f"raw plates    : {len(have_raw)} to delete from "
          f"{ART.relative_to(ROOT)}")
    if unlisted:
        print(f"                ({unlisted} of them are on disk but not in "
              "the manifest -- drawn since its last checkpoint)")
    print(f"styled plates : {len(have_styled)} to delete from "
          f"{PNG_DIR.relative_to(ROOT)}")
    print(f"                (the {len(list(PNG_DIR.glob('B*.png')))} B*.png "
          "measured off the disc are not touched)")

    if not args.apply:
        print("\nnothing done. Re-run with --apply to do it.")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = MANIFEST.with_suffix(f".json.before-reset-{stamp}")
    shutil.copy2(MANIFEST, backup)
    print(f"\nbacked the manifest up to {backup.name}")

    man["plates"] = keep
    MANIFEST.write_text(json.dumps(man, indent=1, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    for path in have_raw + have_styled:
        path.unlink()
    print(f"cut {len(rooms)} room plates, deleted "
          f"{len(have_raw) + len(have_styled)} files")
    print("\nNow: gen_art_source.py --sheet tools/assets/art/room_prompts.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
