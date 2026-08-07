"""Pin the fix for a room-transition wallpaper flash: title_bg_dim_set must
replay the dim at the level the ramp actually recorded, not hardcode 255.

This is a source-level check, not a hardware one, for the same reason
test_cd_mood_dirs.py gives for its own tga_decode check: title.cxx includes
SRL and cannot build (or run) on the host, so nothing here proves the SH-2
actually re-lights the wallpaper correctly at runtime -- only that the fix is
still textually in place. saturn/tests/test_bg_dim.c pins the arithmetic this
depends on (bg_dim_effective, bg_dim_last_level) on the host; this file is
what ties title.cxx's call site to that arithmetic, since nothing else does.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TITLE = (REPO / "saturn" / "src" / "video" / "title.cxx").read_text()


def extract_function_body(source, name):
    """The braced body of the C/C++ function `name`, found by brace matching
    from its first `{`. Starts at the opening brace, not the signature or the
    header comment above it -- mirrors test_cd_mood_dirs.py's helper of the
    same name, so a header comment mentioning "255" or "bg_dim_last_level" in
    prose can never be mistaken for code."""
    sig = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    assert sig, f"{name}(...) not found in title.cxx"
    start = sig.end() - 1
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError(f"unbalanced braces reading {name}'s body")


def test_dim_set_replays_at_the_recorded_level():
    """title_bg_dim_set used to hardcode title_bg_dyn_fade(255), which re-lit
    the wallpaper to full brightness whenever a dim change landed mid room-
    transition -- exactly the CD read the blackout at that moment is meant to
    cover (see title_fade_engage's comment in title.cxx). The fix reads the
    level the ramp actually recorded via bg_dim_last_level() instead of
    assuming 255. Pins two things: the call is there, and the old hardcoded
    call is not."""
    body = extract_function_body(TITLE, "title_bg_dim_set")

    assert re.search(r"\bbg_dim_last_level\s*\(\s*\)", body), (
        "title_bg_dim_set must read bg_dim_last_level() before calling "
        "title_bg_dyn_fade")

    assert not re.search(r"\btitle_bg_dyn_fade\s*\(\s*255\s*\)", body), (
        "title_bg_dim_set must not pass a bare 255 to title_bg_dyn_fade -- "
        "that re-lights the wallpaper to full brightness mid room-transition")
