#!/usr/bin/env python3
"""Hold BG_MANIFEST against the tracked reverse-engineering copies.

The manifest's size and SHA-256 are load-bearing rather than defensive: the
runtime holds measured byte offsets into these archives, so a wrong entry does
not fail to open -- it decompresses from the wrong offset and shows garbage, or
hangs the LZSS loop, with nothing upstream to say why. analysis/zork_bg/raw/
holds the bytes those offsets were measured against, so the two must agree.

Run as tests: pytest saturn/tests/test_bg_manifest.py
"""
import hashlib
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
RAW = ROOT / "analysis" / "zork_bg" / "raw"

sys.path.insert(0, str(ROOT / "tools"))
from extract_bg import BG_MANIFEST  # noqa: E402


@pytest.mark.parametrize("name", sorted(BG_MANIFEST))
def test_manifest_matches_tracked_bytes(name):
    size, digest = BG_MANIFEST[name]
    data = (RAW / name).read_bytes()
    assert len(data) == size, f"{name}: manifest size {size}, tracked {len(data)}"
    assert hashlib.sha256(data).hexdigest() == digest, f"{name}: digest mismatch"


def test_item_archive_is_in_the_manifest():
    assert "OITEM.CZ" in BG_MANIFEST, (
        "OITEM.CZ must ship for the inventory pane to have any pictures"
    )
