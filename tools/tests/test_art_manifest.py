"""/*----------------------
 | test_art_manifest.py
 | Description: The generated-art manifest against the frame table it produced.
 |     manifest.json is the source of record and frames.json is derived, both
 |     are tracked, and shipping the derived one without the other leaves a
 |     tracked file describing plates the manifest no longer lists -- which the
 |     next checkout reverts to silently, taking the record of every plate
 |     drawn since with it. That is how 1,868 plates came to be described by a
 |     one-plate manifest.
 | Author: suinevere
 ----------------------*/"""
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import art_frames
import gen_art_archive as g

MANIFEST = json.loads(g.MANIFEST.read_text(encoding="utf-8"))["plates"]
FRAMES = sorted(art_frames.frames(), key=lambda f: f["index"])


def test_the_frame_table_is_explained_by_the_manifest():
    assert len(MANIFEST) >= len(FRAMES)
    assert [p["source"] for p in MANIFEST[:len(FRAMES)]] == \
           [f["source"] for f in FRAMES]


def test_frame_indices_run_unbroken_from_the_measured_supply():
    base = g.measured()
    assert [f["index"] for f in FRAMES] == \
           list(range(base + 1, base + 1 + len(FRAMES)))


def test_every_plate_can_be_built_from_a_committed_checkout():
    """The raw generations are gitignored scratch, so a fresh checkout builds
    the archives from the styled plates alone or not at all."""
    missing = [p["source"] for p in MANIFEST
               if not (g.PNG_DIR / g.preview_name(p)).is_file()]
    assert not missing, missing[:10]


def test_no_plate_is_listed_twice():
    sources = [p["source"] for p in MANIFEST]
    assert len(set(sources)) == len(sources)
