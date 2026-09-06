#!/usr/bin/env python3
"""Hold the committed generated-art archives against frames.json.

The archives under saturn/cd/data/BG are derived but committed: nothing that
could rebuild them exists where the disc is published -- styling grades each
plate against the original frame beside it, so a rebuild needs the staged
originals, Pillow, and ~25 minutes -- and a release workflow has no business
downloading the disc those originals come off. See tools/gen_art_archive.py.

That makes frames.json the only thing standing between a stale or truncated
archive and a disc that decompresses from the wrong offset. It records a size
and SHA-256 per archive alongside the frame offsets measured in it, so the two
have to agree here rather than on the player's Saturn: a wrong archive does not
fail to open, it shows garbage or hangs the LZSS loop with nothing to say why.

Stdlib only, deliberately: this is the check CI can run without installing the
imaging stack that built the archives in the first place.

Run as tests: pytest saturn/tests/test_art_archives.py
"""
import hashlib
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FRAMES = ROOT / "tools" / "assets" / "art" / "frames.json"
BG = ROOT / "saturn" / "cd" / "data" / "BG"

RECORD = json.loads(FRAMES.read_text(encoding="utf-8"))
ARCHIVES = RECORD["archives"]


@pytest.mark.parametrize("entry", ARCHIVES, ids=lambda e: e["stem"])
def test_archive_matches_its_record(entry):
    path = BG / (entry["stem"] + ".CGL")
    assert path.is_file(), (
        f"{path.name} is recorded in frames.json but not committed -- every room "
        f"holding one of its frames would show no picture"
    )
    data = path.read_bytes()
    assert len(data) == entry["bytes"], (
        f"{path.name}: frames.json says {entry['bytes']} bytes, committed {len(data)}"
    )
    assert hashlib.sha256(data).hexdigest() == entry["sha256"], (
        f"{path.name}: digest mismatch -- the frame offsets in frames.json were "
        f"measured in a different build of this archive"
    )


def test_every_frame_lands_in_a_recorded_archive():
    """No frame may point at an archive the record does not describe."""
    stems = {e["stem"] for e in ARCHIVES}
    orphans = sorted({f["archive"] for f in RECORD["frames"] if f["archive"] not in stems})
    assert not orphans, f"frames.json places pictures in undescribed archives: {orphans}"


def test_no_stray_generated_archives():
    """A GEN archive on disc that the record does not name is dead weight on the
    ISO at best, and at worst a leftover from a build the offsets do not match."""
    prefix = RECORD["prefix"]
    stems = {e["stem"] for e in ARCHIVES}
    found = {p.stem for p in BG.glob(prefix + "*.CGL")}
    assert found == stems, f"unrecorded: {sorted(found - stems)}"
