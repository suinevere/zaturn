#!/usr/bin/env python3
"""Hold the three hand-offs an in-game menu makes, none of which is visible
anywhere but on a screen.

A menu fade has two halves that live in different files. One side takes the
screen to black and holds it; the other has to know that, ramp up from it, and
give it back. Nothing checks the pairing at runtime -- a mismatched half does
not fail, it just draws wrong -- so all three of these shipped:

  1. The prompt released the hold before submitting "save"/"restore", and the
     device picker that opened next ramped IN from black. With the layers
     already off the offset channel that ramp reached the picture and the
     backdrop but not the box, so the menu's own text sat at full brightness on
     black until the ramp caught up.

  2. choose_dest ends held black on every exit -- correct for the title-menu
     Load flow, where the game load runs under the same black -- and in game
     nothing released it. The prompt came back to a black screen and stayed
     there until the player opened a menu, which ramps up on the way out.

  3. ~MenuBacking latches the marble and owes the window off to the next text
     flush. The callback dropped the latch without saying who owns NBG2 next,
     so in game the layer went unclaimed for a frame between the box going and
     the input strip being repainted, and the wallpaper and console text showed
     through with no strip under them.

These are source-shape checks. There is no way to observe any of it from a host
test -- the evidence is a VDP2 colour offset channel and one frame of a tilemap
-- so what is pinned is the call each side owes the other.

Run as tests: pytest saturn/tests/test_menu_screen_ownership.py
"""
import re
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"


def body(path, name):
    """The brace-balanced body of a C/C++ function definition.

    Walked rather than regexed: these bodies are full of braces of their own and
    a lazy match stops at the first inner one, which reads as an empty function
    and passes every check below while proving nothing.
    """
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
                return text[i:j + 1]
    raise AssertionError(f"{name} in {path} never closes")


def code_lines(src):
    """Statement lines only -- comments here discuss the very calls being
    checked for, so a plain substring search matches the prose that explains why
    the call is absent."""
    out, in_block = [], False
    for line in src.splitlines():
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


def test_save_and_load_do_not_release_the_hold_at_the_prompt():
    """(1) The picker fades in from black, so the prompt has to leave it black.

    choose_device arms g_menu_intro_fade unconditionally, and g_menu_page_fade
    stays set once a game is running, so that ramp is live in game. Releasing
    here hands it a screen nobody is holding.
    """
    lines = code_lines(body("engine/saturn_glue.cxx", "saturn_readline"))
    bad = [l for l in lines if "menu_ramp_cut" in l and "OM_SAVE" in l]
    assert not bad, (
        "the prompt releases the screen on the Save/Load exit:\n  "
        + "\n  ".join(bad)
        + "\nThose two hand the screen to a picker that ramps up from black. "
          "Let them leave it black and release at the far end, in "
          "saturn_save_blob / saturn_load_blob, where music_resume is already "
          "owned for the same reason.")


def test_load_hook_releases_what_its_picker_left_black():
    """(2) The far end of the same hand-off."""
    lines = code_lines(body("engine/saturn_glue.cxx", "saturn_load_blob"))
    assert any("menu_ramp_cut" in l for l in lines), (
        "saturn_load_blob never releases the screen. choose_dest ends held "
        "black on every exit, and everything past it -- the empty-slot box, the "
        "story's reply, the prompt -- draws at normal brightness, so the prompt "
        "comes back to a screen that stays black until a menu ramps it up.")


def test_marble_latch_drop_hands_the_layer_over():
    """(3) Dropping the latch says who no longer owns NBG2, not who does."""
    lines = code_lines(body("menu/menu.cxx", "menu_backing_window_off"))
    assert any("dash_hold_latch(0)" in l for l in lines), (
        "menu_backing_window_off no longer drops the marble latch, so the "
        "marble outlives the window it is meant to leave with.")
    assert any(re.fullmatch(r"dash_hold\(\);", l) for l in lines), (
        "menu_backing_window_off drops the marble latch without painting the "
        "in-game strip, so NBG2 goes unclaimed for the frame between the box "
        "ending and the next prompt render -- the wallpaper and the console "
        "text with no strip under them. dash_hold() paints it, and is a no-op "
        "off the game path.")


def test_the_two_comments_that_hid_this_still_tell_the_truth():
    """g_menu_page_fade is live in game. Both of the bugs above came from source
    that said otherwise, so the claim is worth pinning where it is made."""
    main = (SRC / "main.cxx").read_text(encoding="utf-8", errors="replace")
    assert re.search(r"g_menu_page_fade\s*=\s*QUICK_FADE_FRAMES", main), (
        "main.cxx no longer sets g_menu_page_fade before the mode menu -- "
        "re-check every reader that assumes it is live in game.")
    lines = code_lines(main)
    assert not any(re.search(r"g_menu_page_fade\s*=\s*0", l) for l in lines), (
        "main.cxx clears g_menu_page_fade again. If in-game menu fades are "
        "meant to be off, choose_device must stop arming g_menu_intro_fade "
        "from it and the save/load hand-off above has to be re-derived.")
