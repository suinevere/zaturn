#!/usr/bin/env python3
"""Assert the CD-DA backend can still come back from a muted level.

The reported bug: dropping Music to 0 in the Sound options page silenced the
game for the rest of the session, and raising the slider again did nothing.

Level 0 does not merely attenuate -- music_set_volume calls StopPause(), so the
drive is stopped and no amount of SetVolume brings it back. Something has to
reissue the track. That restart used to live in music_set_level, keyed off
`was_muted = (g_level == 0)` sampled on entry. It could never fire from the
Sound page, because the page drives the slider through music_set_volume live
(menu_pages.cxx, the SR_MUSIC row): by the time Ok called music_set_level,
music_set_volume had already moved g_level off 0 and there was no edge left to
see. g_level is the level the player asked for and says nothing about whether
the head is running, so the two states have to be tracked separately.

This is a source-shape guard because music_cdda.cxx only builds against SRL on
the SH-2 and cannot be exercised on the host. It pins the invariant, not the
wording: the restart is keyed off the stopped flag, and it lives in the entry
point the slider actually calls.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
CDDA = ROOT / "src" / "sound" / "music_cdda.cxx"
PAGES = ROOT / "src" / "menu" / "menu_pages.cxx"

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")


def strip_comments(text):
    """Prose in this file names both the bug and the fix; a naive scan would
    match the explanation instead of the code."""
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def brace_block(code, start):
    """Source from the brace at/after `start` to its match, by brace depth."""
    open_at = code.find("{", start)
    if open_at < 0:
        return None
    i, depth = open_at + 1, 1
    while i < len(code) and depth:
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
        i += 1
    return code[open_at + 1:i]


def body_of(code, func):
    """Source of `func` from its signature to the closing brace, by brace depth."""
    m = re.search(r"\b" + re.escape(func) + r"\s*\([^)]*\)\s*\{", code)
    if not m:
        return None
    i, depth = m.end(), 1
    while i < len(code) and depth:
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
        i += 1
    return code[m.end():i]


def main():
    fails = 0
    for path in (CDDA, PAGES):
        if not path.exists():
            print(f"{path.name}: not found", file=sys.stderr)
            return 1

    cdda = strip_comments(CDDA.read_text(encoding="utf-8", errors="replace"))
    pages = strip_comments(PAGES.read_text(encoding="utf-8", errors="replace"))

    setvol = body_of(cdda, "music_set_volume")
    if setvol is None:
        print("music_set_volume: not found", file=sys.stderr)
        return 1

    # 1. The slider's live path is the one that has to recover. Whatever the
    #    Sound page calls per keypress must itself be able to restart the drive.
    if "PlaySingle" not in setvol:
        print("music_set_volume: no PlaySingle -- raising the level off 0 "
              "cannot restart a stopped drive", file=sys.stderr)
        fails += 1

    # 2. Keyed off the stopped flag, not off the requested level. g_level has
    #    already moved by then on the live path, which is the original bug.
    if "g_vol_stopped" not in setvol:
        print("music_set_volume: restart is not keyed off g_vol_stopped",
              file=sys.stderr)
        fails += 1

    # 3. Muting must record that the drive was stopped for volume, or nothing
    #    downstream can tell it apart from "nothing was requested".
    if not re.search(r"StopPause\s*\(\s*\)\s*;\s*g_vol_stopped\s*=\s*1", setvol):
        print("music_set_volume: level 0 stops the drive without recording it",
              file=sys.stderr)
        fails += 1

    # 4. Every volume write except the one that lifts a duck must go through
    #    out_level(). The duck is not a one-off: the engine keeps cycling tracks
    #    under an open menu and music_cdda_play_mode sets the volume as it issues
    #    each one, so a bare g_level there hands the next track full volume and
    #    silently cancels the duck. music_cdda_unduck is the sole exception --
    #    clearing the flag is the whole point of it.
    for func in ("music_cdda_play_mode", "music_set_volume",
                 "music_cdda_duck", "music_cdda_resume"):
        body = body_of(cdda, func)
        if body is None:
            print(f"{func}: not found", file=sys.stderr)
            fails += 1
            continue
        if "SetVolume" in body and "out_level" not in body:
            print(f"{func}: writes SetVolume without out_level() -- a duck in "
                  "force will be cancelled by this write", file=sys.stderr)
            fails += 1

    # 5. The premise of all of the above: the Sound page really does drive the
    #    Music row through music_set_volume. If it ever switches to
    #    music_set_level this guard is testing the wrong entry point.
    at = pages.find("SR_MUSIC)")
    music_row = brace_block(pages, at) if at >= 0 else None
    if music_row is None or "music_set_volume" not in music_row:
        print("menu_pages.cxx: SR_MUSIC row no longer calls music_set_volume; "
              "re-point this guard at whatever it calls now", file=sys.stderr)
        fails += 1

    print("test_music_unmute:", "FAIL" if fails else "OK")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
