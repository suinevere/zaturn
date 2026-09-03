#!/usr/bin/env python3
"""Keep the map's parchment off the drive once a game is running.

tga_decode is the only thing on the map's path that touches the CD, and a data
seek does not merely interrupt CD-DA. item_art.cxx wrote the finding down first:
a track that was never held reads to the music engine as one that ended, so the
next tick starts it again from the top. The player hears the music cut out and
come back, not a gap.

So the read belongs in the game load, under the ramp, before the music starts --
the same slot item_art_set_game uses for OITEM.CZ. Two things have to hold for
that to be true, and neither is visible from anywhere but a pair of speakers:

  1. map_view_show must ASK whether a picture is held, never read one. Reading
     there was worse than a single stutter: tga_decode reads the file header
     before it checks the heap, so on a story too large to hold the picture the
     seek happened on every open and still put no paper up.

  2. main.cxx must do the read BEFORE music_start(). After it, the preload is
     just the same seek moved earlier in the same session, with the track
     already running.

Run as tests: pytest saturn/tests/test_map_parchment.py
"""
import re
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"


def code_lines(path):
    """Statement lines only. The comments in these files discuss the very calls
    being searched for, so a substring match would find the prose."""
    out, in_block = [], False
    for line in (SRC / path).read_text(encoding="utf-8", errors="replace").splitlines():
        s = line.strip()
        if in_block:
            if "*/" in s:
                in_block = False
                s = s.split("*/", 1)[1].strip()
            else:
                continue
        while "/*" in s:
            head, rest = s.split("/*", 1)
            if "*/" in rest:
                s = head + rest.split("*/", 1)[1]
            else:
                s, in_block = head, True
                break
        s = s.split("//", 1)[0].strip()
        if s:
            out.append(s)
    return out


def function_body_lines(path, name):
    """Statement lines of one brace-balanced function, walked rather than
    regexed: these bodies are full of braces and a lazy match reads as empty."""
    text = (SRC / path).read_text(encoding="utf-8", errors="replace")
    m = re.search(r"(?m)^[A-Za-z_][\w :*&\"]*\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", text)
    assert m is not None, f"{name} not found in {path} -- if it moved, move this check with it"
    depth, i = 0, m.end() - 1
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                body = text[i:j + 1]
                break
    else:
        raise AssertionError(f"{name} in {path} never closes")
    out, in_block = [], False
    for line in body.splitlines():
        s = line.strip()
        if in_block:
            if "*/" in s:
                in_block = False
                s = s.split("*/", 1)[1].strip()
            else:
                continue
        while "/*" in s:
            head, rest = s.split("/*", 1)
            if "*/" in rest:
                s = head + rest.split("*/", 1)[1]
            else:
                s, in_block = head, True
                break
        s = s.split("//", 1)[0].strip()
        if s:
            out.append(s)
    return out


def test_map_view_never_reads_the_disc():
    """(1) Opening the map must cost no drive access at all."""
    lines = function_body_lines("video/map_view.cxx", "map_view_show")
    reads = [l for l in lines if "title_bg_hold(" in l]
    assert not reads, (
        "map_view_show reads the parchment off the disc:\n  " + "\n  ".join(reads)
        + "\nThat seek silences the CD-DA track, and an unheld track is restarted "
          "from the top rather than resumed -- the music cuts out and comes back. "
          "Ask title_bg_held() instead and let map_view_preload own the read.")
    assert any("title_bg_held()" in l for l in lines), (
        "map_view_show no longer asks whether a parchment is held, so it can "
        "never draw one.")


def test_the_preload_is_the_one_reader():
    """The read has to exist somewhere, or the map silently loses its paper."""
    lines = function_body_lines("video/map_view.cxx", "map_view_preload")
    assert any("title_bg_hold(" in l for l in lines), (
        "map_view_preload does not read the parchment, so nothing does and the "
        "map draws on its back colour forever.")


def test_the_map_ground_survives_its_own_fade():
    """The fallback ground has to outlive the ramp that reveals it.

    Every fade recomputes the backdrop from the player's background setting, so
    a page that sets its own keeps it only until the fade's first frame. The map
    sets tan, faded in, and arrived on the player's colour -- black by default.
    Nothing saw it while a parchment covered the ground; on a story too large to
    hold one it was the whole screen.
    """
    lines = function_body_lines("video/map_view.cxx", "map_view_show")
    sets = [i for i, l in enumerate(lines) if "MAP_BACK_555" in l and "SetBackColor" in l]
    assert sets, "map_view_show no longer sets its own ground colour at all"
    for i in sets:
        near = lines[max(0, i - 3):i + 4]
        assert any("menu_back_override(MAP_BACK_555)" in l for l in near), (
            "map_view_show sets the back colour to MAP_BACK_555 without telling "
            "the fade to drive it too. The next ramp recomputes the backdrop "
            "from g_display.bg and the tan is gone.")
    assert any("menu_back_override(0)" in l for l in lines), (
        "map_view_show never clears the fade's backdrop override, so every fade "
        "after the map closes drives the map's tan.")


def test_the_override_is_cleared_on_the_way_to_the_title():
    """The map polls for the reset chord, so its own exit path can be skipped."""
    lines = code_lines("main.cxx")
    assert any("menu_back_override(0)" in l for l in lines), (
        "main.cxx never clears the backdrop override on the title recovery. A "
        "soft reset taken inside the map longjmps past the clear it does on its "
        "own way out, and every fade from the title on drives the map's tan.")


def test_the_read_happens_before_the_music_starts():
    """(2) Ordering is the whole point. A preload after music_start() is the
    same seek with the track already running."""
    lines = code_lines("main.cxx")
    try:
        pre = next(i for i, l in enumerate(lines) if "map_view_preload(" in l)
    except StopIteration:
        raise AssertionError(
            "main.cxx never calls map_view_preload(), so the first time the "
            "player opens the map the read lands mid-game with CD-DA playing.")
    starts = [i for i, l in enumerate(lines) if re.search(r"\bmusic_start\(\)", l)]
    assert starts, "main.cxx no longer starts the music -- re-derive this check"
    assert pre < min(starts), (
        "map_view_preload() runs after music_start(). The read is meant to land "
        "while the drive is still the loader's and no track is playing; moved "
        "after, it is the same stutter one moment earlier.")
