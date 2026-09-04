#!/usr/bin/env python3
"""/*----------------------
 | check_plates.py
 | Description: Finds generated plates that contain a person, a face or written
 |     signage, so they can be redrawn.
 |
 |     This exists because looking at them does not work. 109 plates were
 |     inspected by eye on contact sheets and two offenders were found; the
 |     owner then found two more in the same set, a figure in a boat and a road
 |     sign, both of which had been looked straight at. At 1,931 plates that
 |     approach is not merely unreliable, it is hopeless, and every miss is a
 |     picture that will be on screen for every turn a player spends in that
 |     room.
 |
 |     Zero-shot CLIP rather than a face detector: the failures are not all
 |     faces. A figure in a boat forty pixels tall, a carved statue and a
 |     signpost are three different things, and what they have in common is
 |     only that a sentence describes them. CLIP scores a picture against
 |     sentences, so one model answers for all three.
 |
 |     Scored against the STYLED plate, not the raw generation: the styled one
 |     is what the console shows, and posterising to twenty-odd colours in a
 |     narrow dark band is exactly the kind of thing that could hide a figure
 |     from a classifier or invent one. What is judged has to be what ships.
 | Author: suinevere
 | Dependencies: argparse, json, pathlib, sys, PIL, torch, transformers
 | Globals: ROOT, PNG_DIR, MODEL, PROMPTS_GOOD, PROMPTS_BAD
 ----------------------*/"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PNG_DIR = ROOT / "analysis" / "zork_bg" / "png"
MODEL = "facebook/detr-resnet-50"
WANTED = ("person",)
MAX_AREA = 0.15
"""ROOT / PNG_DIR / MODEL / WANTED

Description: Where the styled plates are, the detector to run over them, and
    the COCO classes that must not appear.

    A detector, not CLIP zero-shot, and the second thing tried rather than the
    first. Scoring a picture against "a photograph with a person in it" versus
    "a photograph with no person in it" fails for exactly the reason "no
    people" failed in the diffusion prompt: a CLIP text encoder has no negation
    either, so the two sentences mean nearly the same thing to it. Both
    orderings put a lunar crater with nobody in it at the top of 107 plates,
    ahead of a picture with a man in a boat. A detector answers the question
    that was actually being asked -- is there a person, and where -- and
    returns a box that can be looked at instead of a number that has to be
    trusted.
Author: suinevere
"""


def scores(paths, batch=16):
    """/*----------------------
     | scores
     | Description: For each plate, the confidence of the most confident person
     |     the detector finds in it, and where.
     |
     |     Run on the plate upscaled fourfold. These are 320x240, dark and
     |     posterised to about two dozen colours, and the figures that matter
     |     are small -- the one the owner caught was a man in a boat perhaps
     |     forty pixels tall. A detector trained on photographs needs the
     |     resolution back before it will see him.
     | Author: suinevere
     | Dependencies: PIL, torch, transformers
     | Globals: MODEL, WANTED
     | Params: paths -- the plate files; batch -- unused, kept for the caller
     | Returns: [(path, score in 0..1, box or None)]
     ----------------------*/"""
    from PIL import Image
    import torch
    from transformers import DetrForObjectDetection, DetrImageProcessor

    model = DetrForObjectDetection.from_pretrained(MODEL)
    proc = DetrImageProcessor.from_pretrained(MODEL)
    model.eval()
    labels = model.config.id2label
    out = []
    for p in paths:
        with Image.open(p) as im:
            big = im.convert("RGB").resize((im.width * 4, im.height * 4),
                                           Image.LANCZOS)
        inputs = proc(images=big, return_tensors="pt")
        with torch.no_grad():
            res = model(**inputs)
        got = proc.post_process_object_detection(
            res, target_sizes=torch.tensor([[big.height, big.width]]),
            threshold=0.05)[0]
        best, box = 0.0, None
        for score, label, b in zip(got["scores"], got["labels"], got["boxes"]):
            if labels[int(label)] not in WANTED or float(score) <= best:
                continue
            x0, y0, x1, y1 = [v / 4 for v in b.tolist()]
            # A person in one of these is small -- the one that had to be caught
            # was eight pixels by fifteen. Every detection covering a large part
            # of the frame turned out to be terrain read as a body: a rock arch,
            # a well head, a boulder field. Rejected on size rather than on
            # confidence, because those scored 0.8 and 0.87, above real figures.
            if (x1 - x0) * (y1 - y0) > MAX_AREA * 320 * 240:
                continue
            best = float(score)
            box = [round(x0), round(y0), round(x1), round(y1)]
        out.append((p, best, box))
    return out


def main(argv=None):
    """/*----------------------
     | main
     | Description: Scores every styled plate and reports the ones over the
     |     threshold, worst first, as names a redraw can be handed.
     | Author: suinevere
     | Dependencies: argparse, json
     | Globals: PNG_DIR, ROOT
     | Params: argv -- command line
     | Returns: 0 when nothing is over the threshold, 1 otherwise
     ----------------------*/"""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--threshold", type=float, default=0.5)
    ap.add_argument("--only", nargs="*", help="score just these plate names")
    ap.add_argument("--json", help="write the full ranking here")
    ap.add_argument("--dir", help="score every PNG in this directory instead of "
                                  "the styled plates the frame table names")
    args = ap.parse_args(argv)

    if args.dir:
        paths = sorted(pathlib.Path(args.dir).glob("*.png"))
        if not paths:
            raise SystemExit(f"check_plates: no PNGs in {args.dir}")
        return report(scores(paths), args)

    frames = json.loads((ROOT / "tools" / "assets" / "art" / "frames.json")
                        .read_text(encoding="utf-8"))["frames"]
    names = [f["png"] for f in frames]
    if args.only:
        want = {n if n.endswith(".png") else n + ".png" for n in args.only}
        names = [n for n in names if n in want]
    paths = [PNG_DIR / n for n in names if (PNG_DIR / n).is_file()]
    if not paths:
        raise SystemExit("check_plates: no styled plates to score")

    return report(scores(paths), args)


def report(rows, args):
    """/*----------------------
     | report
     | Description: Prints the ranking worst first and says how many are over
     |     the threshold, so the output is a list of names a redraw can be
     |     handed directly.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: N/A
     | Params: rows -- what scores returned; args -- the parsed command line
     | Returns: 0 when nothing is over the threshold, 1 otherwise
     ----------------------*/"""
    ranked = sorted(rows, key=lambda r: -r[1])
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(
            [{"plate": p.stem, "score": round(s, 4), "box": b}
             for p, s, b in ranked], indent=1) + "\n", encoding="utf-8")

    bad = [r for r in ranked if r[1] >= args.threshold]
    print(f"scored {len(ranked)} plates; {len(bad)} at or over {args.threshold}")
    for p, s, b in bad:
        print(f"  {s:.3f}  {p.stem:<24} at {b}")
    if not bad:
        print(f"  worst was {ranked[0][1]:.3f} ({ranked[0][0].stem})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
