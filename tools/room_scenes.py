#!/usr/bin/env python3
"""Decide room scenes by rule, refuse the rest into a review queue.

Description: Stage two and three of the tagging pipeline. decide() runs the
    ordered title rules over one story's inventory and splits it into what the
    rules settle and what they refuse. merge() folds a fresh rule pass into the
    verdicts a human has already given.

    The precedence rule: a human verdict is authoritative and is never
    overwritten by a rule re-run. Editing the vocabulary re-decides only rooms
    nobody has ruled on, so the review work survives every change to the rules.
    The alternative -- regenerating wholesale -- is how 365 curated images came
    to be orphaned from their manifest.

    Refusal is the designed output of a rule that does not match, not a failure.
    A title naming a shape ("Dead End", "Cube", "Oddly-angled Room") cannot be
    resolved from the title, and guessing is worse than asking.
Author: suinevere
Dependencies: json, pathlib, sys, argparse, scene_vocab
Globals: ROOT, ROOMS, SCENES_DIR
"""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_vocab as vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROOMS = ROOT / "tools" / "assets" / "rooms"
SCENES_DIR = ROOT / "tools" / "assets" / "scenes"


def decide(rooms):
    """Splits one story's rooms into rule-decided and refused.

    Description: Splits one story's rooms into rule-decided and refused.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: rooms -- inventory rows
    Returns: (decided {obj: scene}, refused [row])
    """
    decided, refused = {}, []
    for row in rooms:
        scene = vocab.scene_for_title(row["title"])
        if scene:
            decided[row["obj"]] = scene
        else:
            refused.append(row)
    return decided, refused


def merge(existing_blessed, decided, refused):
    """Folds a rule pass into existing human verdicts.

    Description: Human wins everywhere. Refusals a human has already ruled on
        drop out of the queue; what remains is grouped by title so repeated
        rooms cost one decision.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: existing_blessed -- {obj: scene} already ruled; decided -- {obj:
        scene} from the rules; refused -- rows the rules would not decide
    Returns: (blessed {obj: scene}, review [{title, description, objs}])
    """
    for obj, scene in existing_blessed.items():
        if scene not in vocab.SCENE_INDEX:
            raise ValueError(f"object {obj}: {scene} is not in the vocabulary")

    blessed = dict(decided)
    blessed.update(existing_blessed)

    groups = {}
    for row in refused:
        if row["obj"] in existing_blessed:
            continue
        key = row["title"]
        g = groups.setdefault(key, {"title": key,
                                    "description": row.get("description"),
                                    "objs": []})
        g["objs"].append(row["obj"])
        if not g["description"] and row.get("description"):
            g["description"] = row["description"]

    review = sorted(groups.values(), key=lambda g: min(g["objs"]))
    for g in review:
        g["objs"].sort()
        g["obj"] = g["objs"][0]
    return blessed, review


def _process_story(stem, rooms_dir, scenes_dir):
    """Runs the pipeline for one story and writes its blessed/review files.

    Description: Reads the story's room inventory and any existing blessed
        scenes, runs decide() then merge(), and writes both output files.
        Converts JSON's string keys to int on read and back to string on
        write, so a rule-decided object number and a human-blessed one never
        collide as separate entries.
    Author: suinevere
    Dependencies: json
    Globals: N/A
    Params: stem -- story identifier; rooms_dir -- inventory directory;
        scenes_dir -- output directory
    Returns: (decided_count, refused_count, review_group_count)
    """
    rooms_path = rooms_dir / f"{stem}.json"
    data = json.loads(rooms_path.read_text(encoding="utf-8"))
    rooms = data["rooms"]

    blessed_path = scenes_dir / f"{stem}.json"
    if blessed_path.exists():
        existing = {int(k): v
                    for k, v in json.loads(blessed_path.read_text(encoding="utf-8")).items()}
    else:
        existing = {}

    decided, refused = decide(rooms)
    blessed, review = merge(existing, decided, refused)

    out_blessed = {str(k): v for k, v in sorted(blessed.items())}
    review_path = scenes_dir / f"{stem}.review.json"

    scenes_dir.mkdir(parents=True, exist_ok=True)
    blessed_path.write_text(
        json.dumps(out_blessed, indent=1, sort_keys=True) + "\n",
        encoding="utf-8")
    review_path.write_text(
        json.dumps(review, indent=1, sort_keys=True) + "\n",
        encoding="utf-8")

    return len(decided), len(refused), len(review)


def main(argv=None):
    """Runs the rule pass over every story's inventory and reports totals.

    Description: Reads each tools/assets/rooms/<STEM>.json, folds a fresh
        rule pass into any existing tools/assets/scenes/<STEM>.json, and
        writes the blessed table and review queue back out. Prints per-story
        and total counts.
    Author: suinevere
    Dependencies: json, pathlib, argparse
    Globals: ROOMS, SCENES_DIR
    Params: argv -- CLI arguments, or None to use sys.argv
    Returns: N/A
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rooms-dir", default=str(ROOMS))
    parser.add_argument("--scenes-dir", default=str(SCENES_DIR))
    args = parser.parse_args(argv)

    rooms_dir = pathlib.Path(args.rooms_dir)
    scenes_dir = pathlib.Path(args.scenes_dir)

    total_decided = total_refused = 0
    for rooms_path in sorted(rooms_dir.glob("*.json")):
        stem = rooms_path.stem
        decided_n, refused_n, review_n = _process_story(stem, rooms_dir, scenes_dir)
        total_decided += decided_n
        total_refused += refused_n
        print(f"{stem}: {decided_n} decided, {refused_n} refused, "
              f"{review_n} review groups")

    print(f"TOTAL: {total_decided} decided, {total_refused} refused")


if __name__ == "__main__":
    main()
