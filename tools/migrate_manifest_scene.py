"""Additively tag legacy manifest records with a `scene`, derived from FETCH_NOUNS.

Description: tools/assets/art_manifest.json holds 412 records fetched under
    the retired mood vocabulary; every one carries `mood`/`donor` and no
    `scene`, so art_server.py's scene-keyed routes cannot reach them even
    though art_review.scene_of() falls back to `mood` for display. This
    migration adds the missing `scene` key wherever a record's `noun` can be
    placed, so those 382 records of human curation become reachable through
    the current scene UI. It never routes legacy mood names into the
    vocabulary and never removes anything -- it only adds a key.

    The noun->scene lookup is built from scene_vocab.FETCH_NOUNS itself: every
    word in every fetch phrase maps to the scene that phrase belongs to, so a
    manifest noun such as "cottage" matches HOUSE_EXT because "cottage" is a
    fetch phrase for it, and a noun such as "hall" matches PARLOR because
    "hall" is a word inside the "entrance hall" phrase. Scanning scenes in
    scene_vocab.SCENES order and keeping the first hit gives collisions the
    same "position is priority" resolution scene_for_title already uses, and
    the words that do collide (see test_migrate_manifest_scene.py) never
    appear as a manifest noun.

    Idempotent by construction: a record that already carries `scene` -- from
    a prior run of this script, or because it was fetched fresh under the
    current vocabulary -- is left untouched, so a second run changes nothing.
Author: suinevere
Dependencies: json, pathlib, fetch_art, scene_vocab
Globals: N/A
"""
import sys
from pathlib import Path

import fetch_art
import scene_vocab as vocab


def build_noun_scene_map():
    """The noun-word -> scene lookup this migration reads.

    Description: Every fetch phrase in FETCH_NOUNS is split on whitespace and
        each word is pointed at that phrase's scene. Scenes are walked in
        scene_vocab.SCENES order, and setdefault keeps only the first scene
        a word is seen under, so an earlier scene wins any collision.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: N/A
    Returns: dict mapping a lowercase noun word to a scene name
    """
    mapping = {}
    for scene in vocab.SCENES:
        for phrase in vocab.FETCH_NOUNS[scene]:
            for word in phrase.split():
                mapping.setdefault(word, scene)
    return mapping


def migrate(manifest, mapping=None):
    """Add `scene` to every record whose noun maps; touch nothing else.

    Description: A record that already has a `scene` key is left exactly as
        it is -- not re-derived, not overwritten -- which is what makes a
        second run of this script a no-op. `mood`, `donor`, `status`,
        `verdict`, and every other key a record carries are never read or
        written here.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: manifest -- the loaded manifest dict, mutated in place;
        mapping -- optional noun->scene override, for tests
    Returns: (gained, already_tagged, unmapped) record counts
    """
    mapping = mapping if mapping is not None else build_noun_scene_map()
    gained = already_tagged = unmapped = 0
    for rec in manifest.values():
        if "scene" in rec:
            already_tagged += 1
            continue
        scene = mapping.get(rec.get("noun"))
        if scene is None:
            unmapped += 1
            continue
        rec["scene"] = scene
        gained += 1
    return gained, already_tagged, unmapped


def main(argv, repo=None):
    """Run the migration against the real manifest and report what happened.

    Description: `repo` defaults to the real repository root; tests pass a
        tmp_path so a run never writes into the working tree.
    Author: suinevere
    Dependencies: fetch_art
    Globals: N/A
    Params: argv -- unused, accepted so the entry point matches its siblings;
        repo -- optional repository root override, for tests
    Returns: 0 always
    """
    repo = repo or Path(__file__).resolve().parents[1]
    manifest_path = repo / "tools" / "assets" / "art_manifest.json"
    manifest = fetch_art.load_manifest(manifest_path)

    gained, already_tagged, unmapped = migrate(manifest)
    fetch_art.save_manifest(manifest_path, manifest)

    print(f"  scene added: {gained}")
    print(f"  already tagged: {already_tagged}")
    print(f"  left untouched (noun does not map): {unmapped}")
    if unmapped:
        left = sorted({r["noun"] for r in manifest.values()
                       if "scene" not in r})
        print("  unmapped nouns: " + ", ".join(left))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
