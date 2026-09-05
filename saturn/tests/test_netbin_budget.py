"""The netbin's image size against PlanetWeb 4.0's loader ceiling.

    python -m pytest saturn/tests/test_netbin_budget.py -q

The ceiling is not ours: PlanetWeb refuses an oversized .netbin, and the symptom
is a browser that will not load the client at all rather than a build error. The
first attempt at this target came in 54 KB over it.

300 KB is the owner's working figure and it is the one to trust.
docs/superpowers/specs/2026-07-25-netbin-minimal-design.md says 400 KB and also
says the loader "does not state the value" -- so that number was inferred, and
this one comes from the person who has loaded the thing.

Nothing had measured it since the spec. It is measured now because the synth's
tune catalogue is the first thing to put tens of kilobytes into this image at
once -- see the per-tune costs in tools/assets/music/songs.json -- and the next
tune added there spends the same budget silently.

Skipped rather than failed when there is no build: BuildDrop is not committed.
"""
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
NETBIN = ROOT / "BuildDrop" / "zaturn.netbin"

# The owner's working figure for what PlanetWeb 4.0 will load. Not a target we
# chose and not one we can raise. Read as 300 KiB rather than 300,000 bytes; the
# headroom below is far wider than the 7,424-byte difference, so which one it
# means never decides anything.
CEILING = 300 * 1024

# What a build has to leave behind it. Not a measurement of anything -- it is
# the room the next feature gets before this becomes an emergency, and the
# netbin has historically grown in tens of kilobytes at a time. A tenth of the
# image is the least that is worth calling headroom.
HEADROOM = 32 * 1024


def _size():
    if not NETBIN.is_file():
        pytest.skip("no zaturn.netbin in BuildDrop -- build once to measure it")
    return NETBIN.stat().st_size


def test_the_image_is_under_the_loader_ceiling():
    size = _size()
    assert size <= CEILING, (
        f"zaturn.netbin is {size} bytes, past PlanetWeb's {CEILING}-byte "
        "ceiling. The loader will refuse it and say nothing useful. The "
        "cheapest thing to give back is tunes: drop entries from "
        "tools/assets/music/songs.json and re-run tools/assets/drums-emit.bat.")


def test_the_image_leaves_room_to_grow():
    size = _size()
    assert size + HEADROOM <= CEILING, (
        f"zaturn.netbin is {size} bytes, leaving {CEILING - size} under the "
        f"{CEILING}-byte ceiling and less than the {HEADROOM}-byte floor. "
        "Nothing is broken yet; the next feature is what breaks.")
