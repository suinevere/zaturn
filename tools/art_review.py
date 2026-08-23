"""Dedup near-duplicate candidates and apply review verdicts to the manifest.

Description: The metric gate removes what is provably unusable; everything left
    is a judgement call, which is now made in tools/art_server.py. This module
    keeps the state machine that judgement writes through: cross-scene dedup so
    the same picture is not offered for two scenes, promote() as the only mover
    of files between tools/assets/candidates and tools/assets/png, and
    refetch_missing to restore a picture a fresh clone never had.

    HAMMING_MAX = 6 is out of the 64 bits a phash carries: under 10% of bits
    differing is the conventional near-duplicate threshold for perceptual
    hashes, tight enough that unrelated photos essentially never collide.
Author: suinevere
Dependencies: collections, json, pathlib, io, PIL, art_status
Globals: HAMMING_MAX
"""
import json
from collections import namedtuple
from io import BytesIO
from pathlib import Path

from PIL import Image

import art_status

HAMMING_MAX = 6


SHOWN = (art_status.ACCEPTED, art_status.REJECTED, art_status.CANDIDATE)


def _hamming(a, b):
    """Hamming distance between two hex perceptual hashes.

    Description: Unequal lengths mean the hashes came from different phash
        settings and are not comparable, so they are treated as maximally far.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: a, b -- hex strings of equal length
    Returns: the bit distance, or a large number when they are not comparable
    """
    if not a or not b or len(a) != len(b):
        return 1 << 30
    return bin(int(a, 16) ^ int(b, 16)).count("1")


def dedup(records, already_accepted=None):
    """Drop records whose perceptual hash is too close to an earlier one.

    Description: Runs across every scene of one game, not within one scene. A
        cave that appears in both CAVE and CRYPT reads as the rotation being
        broken, which is the same reason the pools are kept disjoint on the
        disc. It deliberately does not run across games: two stories ship
        their own copy of a picture, so the same photograph in ZORK1 and in
        ENCHANTR is not a duplicate, it is the design.

        `already_accepted` seeds `seen` before any candidate is judged, so a
        picture promoted in an earlier review pass still blocks its duplicate
        today -- without it, dedup only sees the current run's candidates and
        a later fetch can re-promote a picture already on the disc.

        A record with no hash is kept: imagehash is optional, and dropping
        everything when it is absent would be worse than keeping a duplicate.
    Author: suinevere
    Dependencies: N/A
    Globals: HAMMING_MAX
    Params: records -- a list of manifest records under review;
        already_accepted -- optional iterable of phashes already promoted
    Returns: the kept records, in input order
    """
    kept = []
    seen = [h for h in (already_accepted or []) if h]
    for r in records:
        h = r.get("phash", "")
        if h and any(_hamming(h, s) <= HAMMING_MAX for s in seen):
            continue
        if h:
            seen.append(h)
        kept.append(r)
    return kept


Counts = namedtuple("Counts", "gained lost")


def scene_of(rec):
    """The scene a record belongs to, whichever vocabulary wrote it.

    Description: Every record now carries "scene" directly. The legacy
        "mood" fallback is kept for one reason only: a stale manifest from
        before the game-first rewrite should render as an unknown scene the
        server quietly drops, not as a KeyError that takes the page down.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: rec -- a manifest record
    Returns: the record's scene name, or its legacy mood
    """
    return rec.get("scene", rec.get("mood"))


def _rel(rec):
    """Build a record's path relative to either image tree.

    Description: The candidates tree and the source tree share one layout, so
        one relative path locates a picture in both. That layout is
        <game>/<scene>/<id>.png, which is what make_tga.convert_game_tree
        walks -- it globs a scene directory for files rather than recursing,
        so the noun cannot be a path segment. The noun survives as a record
        field, where the review server still groups by it.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: rec -- a manifest record
    Returns: Path of <game>/<scene>/<id>.png
    """
    return Path(rec.get("game") or "UNKNOWN",
                scene_of(rec) or "UNKNOWN") / "{}.png".format(rec["id"])


def _move_if_absent(src, dst):
    """Move a picture between trees, but never over an existing destination.

    Description: A destination that already exists means the move has happened
        before and the source is a leftover -- moving it again would re-promote
        a picture the status guard has already settled. See the reconciliation
        rule in this feature's plan.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: src -- where the picture is; dst -- where it belongs
    Returns: True if the picture now sits at dst, False if neither tree has it
    """
    src, dst = Path(src), Path(dst)
    if dst.exists():
        return True
    if not src.exists():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(src.read_bytes())
    src.unlink()
    return True


def promote(verdicts, manifest, candidates_dir, png_dir):
    """Apply human verdicts, moving each picture to match its new status.

    Description: The manifest is the decision, and the file location follows it
        -- so a verdict is recorded even when no picture is on disk, which is
        the state of every rejected image in a fresh clone. A metric rejection
        is not a human decision and is never overwritten here; art_status
        explains why the two must not blur together.
    Author: suinevere
    Dependencies: pathlib, art_status
    Globals: N/A
    Params: verdicts -- id -> "accept"/"reject"/"unmark"; manifest -- mutated
        in place; candidates_dir -- the git-ignored tree; png_dir --
        tools/assets/png
    Returns: dict mapping scene to a Counts of gained and lost pictures
    """
    counts = {}
    for key, call in verdicts.items():
        rec = manifest.get(key)
        if rec is None or rec["status"] == art_status.METRIC_REJECTED:
            continue

        if call == "accept":
            want = art_status.ACCEPTED
        elif call == "unmark":
            want = art_status.CANDIDATE
        else:
            want = art_status.REJECTED
        was = rec["status"]
        rel = _rel(rec)
        cand, png = Path(candidates_dir) / rel, Path(png_dir) / rel

        if want == art_status.ACCEPTED:
            placed = _move_if_absent(cand, png)
        else:
            placed = _move_if_absent(png, cand)
        if not placed:
            print("  {}: no local copy; recording the verdict only".format(rel))

        rec["status"] = want
        if was == want:
            continue
        gained = 1 if want == art_status.ACCEPTED else 0
        lost = 1 if was == art_status.ACCEPTED else 0
        scene = scene_of(rec)
        prev = counts.get(scene, Counts(0, 0))
        counts[scene] = Counts(prev.gained + gained, prev.lost + lost)
    return counts


def refetch_missing(records, candidates_dir, png_dir, fetcher):
    """Re-download pictures the manifest knows about but the disk has lost.

    Description: A fresh clone has no candidates tree, so every rejected
        picture's pixels are gone while its verdict survives in the manifest.
        Restores them into the candidates tree from the recorded image_url.
        A failed download reports and is skipped -- the sheet falls back to a
        placeholder tile and the run continues.
    Author: suinevere
    Dependencies: PIL, pathlib, art_status
    Globals: N/A
    Params: records -- the manifest dict; candidates_dir -- the git-ignored
        tree; png_dir -- tools/assets/png; fetcher -- anything with download()
    Returns: how many pictures were restored
    """
    restored = 0
    for rec in records.values():
        if rec["status"] not in SHOWN:
            continue
        rel = _rel(rec)
        if (Path(png_dir) / rel).exists() or (Path(candidates_dir)
                                              / rel).exists():
            continue
        url = rec.get("image_url", "")
        if not url:
            print("  {}: no image_url recorded, cannot restore".format(rel))
            continue
        try:
            im = Image.open(BytesIO(fetcher.download(url)))
            im.load()
        except Exception as exc:
            print("  {}: refetch failed ({})".format(url, exc))
            continue
        dst = Path(candidates_dir) / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        im.convert("RGB").save(dst, "PNG")
        restored += 1
    return restored


def main(argv, repo=None):
    """Apply a downloaded verdicts file through promote(), or sweep undecided ones.

    Description: `repo` defaults to the real repository root; tests pass a
        tmp_path so a run never writes into the working tree. Review itself
        now happens in tools/art_server.py, which applies a verdict the
        moment it is clicked, so this entry point has two jobs left: apply a
        verdicts file someone hands it explicitly, and reject whatever the
        operator never clicked before asking for a sweep.
    Author: suinevere
    Dependencies: fetch_art, art_status
    Globals: N/A
    Params: argv -- ["--promote", "<verdicts.json>"] or
        ["--reject-unmarked", "[SCENE]"]; repo -- optional repository root
        override, for tests
    Returns: 0 always
    """
    import fetch_art

    repo = repo or Path(__file__).resolve().parents[1]
    assets = repo / "tools" / "assets"
    manifest_path = assets / "art_manifest.json"
    manifest = fetch_art.load_manifest(manifest_path)

    if argv and argv[0] == "--promote" and len(argv) >= 2:
        with open(argv[1], "r", encoding="utf-8") as fh:
            verdicts = json.load(fh)
        counts = promote(verdicts, manifest, assets / "candidates",
                         assets / "png")
        fetch_art.save_manifest(manifest_path, manifest)
        for scene in sorted(counts):
            print(f"  {scene}: +{counts[scene].gained} -{counts[scene].lost}")
        return 0

    if argv and argv[0] == "--reject-unmarked":
        scene_filter = argv[1] if len(argv) >= 2 else None
        verdicts = {}
        for key, rec in manifest.items():
            if rec["status"] != art_status.CANDIDATE:
                continue
            if scene_filter is not None and scene_of(rec) != scene_filter:
                continue
            verdicts[key] = "reject"
        swept = {}
        for key in verdicts:
            scene = scene_of(manifest[key])
            swept[scene] = swept.get(scene, 0) + 1
        promote(verdicts, manifest, assets / "candidates", assets / "png")
        fetch_art.save_manifest(manifest_path, manifest)
        for scene in sorted(swept):
            print(f"  {scene}: rejected {swept[scene]} unmarked candidate(s)")
        return 0

    print("  usage: art_review.py --promote <verdicts.json>")
    print("  usage: art_review.py --reject-unmarked [SCENE]")
    print("  review runs at http://127.0.0.1:8080 -- python tools/art_server.py")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
