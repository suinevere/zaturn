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
MENU = (REPO / "saturn" / "src" / "menu" / "menu.cxx").read_text()


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


def strip_netbin_blocks(source):
    """`source` with every `#ifdef NETBIN` arm removed and every `#else` arm of
    an `#ifndef NETBIN` removed -- i.e. what the CD build actually compiles.
    Crude but sufficient: these files nest no conditionals inside the NETBIN
    ones, and a nested `#if` would show up as an unbalanced-depth failure here
    rather than passing silently."""
    out, skipping, depth = [], False, 0
    for line in source.splitlines():
        s = line.strip()
        if re.match(r"#\s*if(n?)def\s+NETBIN\b", s) or \
           re.match(r"#\s*if\s+!?\s*defined\s*\(\s*NETBIN\s*\)", s):
            depth += 1
            skipping = bool(re.match(r"#\s*ifdef\s+NETBIN\b", s))
            continue
        if depth and re.match(r"#\s*else\b", s):
            skipping = not skipping
            continue
        if depth and re.match(r"#\s*endif\b", s):
            depth -= 1
            skipping = False
            continue
        if not skipping:
            out.append(line)
    assert depth == 0, "unbalanced NETBIN conditionals"
    return "\n".join(out)


def test_screen_fades_leave_the_picture_on_the_dim_channel():
    """The wallpaper dim rides colour offset channel B on NBG0, and a scroll
    sits on one channel or the other, never both. Every screen-wide fade used to
    claim NBG0 for channel A for its duration, which dropped the dim -- and left
    it dropped, because the release wrote NoOffset without handing the layer
    back. The visible symptom was a saved dim ignored from boot and a Dimming
    row that appeared dead until it was stepped to Normal and back.

    So: nothing outside title_bg_apply may point NBG0 at a channel. The fades
    drive it through title_fade_set instead. menu.cxx still does it the old way
    under NETBIN, which links no title.cxx and shows no wallpaper at all."""
    for name, source in (("title.cxx", TITLE), ("menu.cxx", MENU)):
        body = strip_netbin_blocks(source)
        for m in re.finditer(r"NBG0::UseColorOffset\s*\([^)]*\)", body):
            enclosing = body.rfind("static void title_bg_apply", 0, m.start())
            assert name == "title.cxx" and enclosing >= 0 and \
                body.find("\n}", enclosing) > m.start(), (
                    f"{name}: NBG0::UseColorOffset outside title_bg_apply -- "
                    "that takes the picture off the wallpaper dim's channel")


def test_the_fade_step_drives_both_layers():
    """One step of a screen-wide fade has to write channel A (the text) AND the
    picture's composed level, or the two halves of the screen drift apart: a
    bare SetColorOffsetA leaves the picture wherever the last ramp left it."""
    body = extract_function_body(TITLE, "static void title_fade_set")
    assert "SetColorOffsetA" in body, "title_fade_set must still write channel A"
    assert re.search(r"\btitle_bg_apply\s*\(\s*255\s*\+\s*v\s*\)", body), (
        "title_fade_set must drive the picture to the matching level, so the "
        "held wallpaper dim is composed into every fade frame")


def test_dyn_fade_is_inert_under_a_screen_fade():
    """A room transition or a music callback must not re-light a screen a
    screen-wide fade is holding black -- the case the old code covered by having
    the fade steal NBG0 outright. With the layer no longer moving, the guard has
    to be explicit."""
    body = extract_function_body(TITLE, "void title_bg_dyn_fade")
    assert "g_screen_fade" in body, (
        "title_bg_dyn_fade must check g_screen_fade before writing")
    assert re.search(r"bg_dim_note_level", body), (
        "a swallowed call must still record its level, or title_bg_dim_set "
        "replays at the wrong place afterwards")
