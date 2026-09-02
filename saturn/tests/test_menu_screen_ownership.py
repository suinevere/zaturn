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
     The first answer was to cut back to the outgoing room in the hook, which
     fixed the black screen and left a pop on the one transition the player
     asked to see fade. The black is kept now and the debt recorded instead.

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


def test_the_hooks_hand_back_the_black_they_leave():
    """(2) The far end of the same hand-off, and the debt that pays for it.

    Both hooks run their pickers inside the interpreter's own turn and end on
    black, so the screen they leave belongs to the prompt on the far side of that
    turn -- a different function, reached by returning through the interpreter,
    with nothing between them but g_screen_owed. Both halves have to hold: the
    hook recording the debt and the prompt seeding reveal_owed from it. Either
    one alone is the black screen this file exists to keep out.
    """
    for fn in ("saturn_save_blob", "saturn_load_blob"):
        lines = code_lines(body("engine/saturn_glue.cxx", fn))
        assert any("g_screen_owed = 1" in l for l in lines), (
            f"{fn} leaves its picker's black behind without recording the debt. "
            "Everything past it -- the story's reply, the prompt -- draws at "
            "normal brightness onto a screen nobody is going to ramp up, so the "
            "game stays black until the player opens a menu.")

    lines = code_lines(body("engine/saturn_glue.cxx", "saturn_readline"))
    assert any("reveal_owed" in l and "g_screen_owed" in l for l in lines), (
        "the prompt never reads g_screen_owed, so the debt the save and restore "
        "hooks record is never spent and the screen they left black stays that "
        "way.")
    assert any(re.fullmatch(r"g_screen_owed = 0;", l) for l in lines), (
        "the prompt reads g_screen_owed without clearing it, so every later "
        "prompt this session ramps the screen up again from a debt that was "
        "paid once.")


def test_the_pause_menu_gets_its_save_and_load_back():
    """Save Game and Load Game picked from the pause menu return to it.

    The pick cannot open its own picker -- that lives inside the interpreter's
    save/restore hook, a whole turn away -- so the way back is a flag the prompt
    on the far side reads. The command panel's own Save/Load rows and the
    F2/F3/F5/F6/F9 quick keys submit the same two commands and deliberately do
    not set it: they land in the room, which is where they were asked from.
    """
    lines = code_lines(body("engine/saturn_glue.cxx", "saturn_readline"))
    assert any("g_menu_reopen = 1" in l and "OM_SAVE" in l for l in lines), (
        "the pause menu's Save/Load rows no longer ask for the menu back, so "
        "they drop the player into the room the way the command panel's do.")
    assert any("g_menu_reopen = 0;" in l for l in lines), (
        "nothing clears g_menu_reopen, so the pause menu re-opens on every "
        "prompt for the rest of the session.")

    body_src = body("engine/saturn_glue.cxx", "saturn_readline")
    assert re.search(r"if\s*\(!menu_back\)\s*menu_ramp_down\(\);", body_src), (
        "the re-open ramps the screen down before opening the menu. It arrives "
        "on the black the hook left, and menu_fade_out starts at full "
        "brightness -- so that ramp flashes the room back on first.")


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


def test_the_prompt_ends_a_menus_chrome_before_claiming_its_layer():
    """(3b) The latch stops NBG2 expiring. It cannot stop it being taken.

    saturn_readline opens with a debounce frame that repaints the input strip.
    Entered straight out of Save or Load, that frame lands while the box's
    letters are still in the text shadow and the image window is still aimed at
    its rectangle -- so repainting NBG2 there produced the hollow box by
    replacement, which is the one route the latch is blind to. Clearing the
    box's text on that frame is what fires the teardown ~MenuBacking deferred,
    so all three halves end together.
    """
    lines = code_lines(body("engine/saturn_glue.cxx", "saturn_readline"))
    try:
        guard = next(i for i, l in enumerate(lines) if "dash_hold_latched()" in l)
        claim = next(i for i, l in enumerate(lines) if re.fullmatch(r"dash_hold\(\);", l))
    except StopIteration:
        raise AssertionError(
            "saturn_readline claims NBG2 for the input strip without first "
            "asking whether a menu box is still owed its teardown "
            "(dash_hold_latched). Entered from Save or Load that repaint takes "
            "the marble out from under the box's own letters.")
    assert guard < claim, (
        "the dash_hold_latched() check comes after the dash_hold() it is meant "
        "to guard, so the strip is painted over the box before anything asks.")
    tail = lines[guard:claim]
    assert any("menu_clear()" in l for l in tail) and any("render_console()" in l for l in tail), (
        "the latched branch does not replace the box's text. Clearing it is "
        "what dirties the shadow, and a dirty shadow is the only thing that "
        "fires the window-off and latch-drop ~MenuBacking deferred -- without "
        "it the box's letters stay lit over a marble that has just become the "
        "input strip.")


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
