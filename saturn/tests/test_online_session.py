#!/usr/bin/env python3
"""Pin the promises an online session makes before its game exists.

Everything here is about the stretch between picking up the line and the first
room id: the boot hangup, the dial the player is told they can call off, the
interface a pad gets handed, and the words the command panel offers while the
only things listening are a login, a lobby and a waiting room. None of it would
fail a compile, and all of it is the kind of thing a later refactor drops
quietly -- a poll callback that stops being passed, a wording that stops being
true, a list that goes back to being sourced from the story.

Runs both ways: `python saturn/tests/test_online_session.py` prints findings and
exits non-zero; `pytest saturn/tests/test_online_session.py` collects the test_*
functions.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
MAIN_NETBIN = SRC / "main_netbin.cxx"
ONLINE = SRC / "net" / "online.cxx"
NET_CONNECT_C = SRC / "net" / "net_connect.c"
NET_CONNECT_H = SRC / "net" / "net_connect.h"
COMMAND_VIEW = SRC / "video" / "command_view.cxx"
COMMAND_PANEL_C = SRC / "input" / "command_panel.c"

# What multizorkd accepts before the Z-machine is reading commands, read off its
# four pre-game input handlers: inpfn_waiting_for_players ("go"/"quit"),
# inpfn_player_waiting ("quit"), inpfn_new_room_privacy ("yes"/"no") and
# inpfn_lobby ("n"/"q"/a row number). Both quits are here because the lobby
# matches "q" alone and the waiting rooms match "quit" alone.
LOBBY_WORDS = ["go", "quit", "yes", "no", "n", "q"]

# The most the lobby can print: LOBBY_MAX_ROWS rows numbered from two, over the
# "<new room>" that is always one.
LOBBY_MAX_ROWS = 24
LOBBY_NUMBERS = [str(d) for d in range(1, LOBBY_MAX_ROWS + 2)]


def _read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def _nocomments(src):
    """Strip C comments, so a test cannot be satisfied by prose that merely names
    the thing it is checking for. Same scan as test_controller_matrix.py's."""
    out = []
    i, n = 0, len(src)
    while i < n:
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif src.startswith("//", i):
            j = src.find("\n", i + 2)
            i = n if j < 0 else j
            out.append(" ")
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def _body(src, fn):
    """The text of function `fn`, from its opening brace to the matching close."""
    m = re.search(r"\b%s\s*\([^)]*\)\s*\{" % re.escape(fn), src)
    assert m, "function not found: %s" % fn
    i = m.end() - 1
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    raise AssertionError("unbalanced braces in %s" % fn)


def test_the_boot_hangup_says_only_that_it_is_waiting():
    """The screen between PlanetWeb letting go and the first dial is a wait, and
    naming the modem operation behind it told the player something they can do
    nothing with. It is also no longer only a hangup -- most of the time is spent
    letting the far end drop its own carrier."""
    src = _nocomments(_read(MAIN_NETBIN))
    assert '"Please wait..."' in src, "the boot wait lost its wording"
    assert "Hanging up" not in src, "the boot wait still names the hangup"


def test_the_line_is_given_fifteen_seconds_to_fall_quiet():
    """Three was measured against a modem that had already let go. A handover
    caught mid-release needs longer, and a dial sent into the browser's own
    carrier trains against it and fails."""
    src = _nocomments(_read(MAIN_NETBIN))
    m = re.search(r"LINE_SETTLE_FRAMES\s*=\s*(\d+)", src)
    assert m, "LINE_SETTLE_FRAMES is gone"
    assert int(m.group(1)) >= 900, (
        "the settle is back under 15s at %s frames" % m.group(1))


def test_the_dial_hands_a_frame_back_often_enough_to_see_a_button():
    """The box has always offered L+R as cancel. It did nothing, because
    modem_dial spins inside the UART read for the whole thirty-five seconds with
    no vsync in it, so the chord was there to be read and nothing was reading."""
    src = _nocomments(_read(ONLINE))
    dial = _body(src, "online_mode")
    assert "net_connect_open_poll" in dial, (
        "the dial no longer takes a poll -- either give the cancel a way to be "
        "seen or stop telling the player it exists")
    assert "net_connect_open(" not in dial, "the blocking dial is back"

    poll = _body(src, "online_dial_poll")
    assert "menu_sync" in poll, "the poll never syncs, so no input is ever fresh"
    assert "online_cancel_requested" in poll, "the poll never asks about cancel"


def test_a_cancelled_dial_is_a_silence_and_not_a_failure():
    """The player asked for it, so there is nothing to report and nothing to
    retry -- an 'Connection failed.' box after a deliberate cancel is the client
    arguing with them."""
    hdr = _nocomments(_read(NET_CONNECT_H))
    assert "NET_CANCELLED" in hdr, "the cancel has no result of its own"
    dial = _nocomments(_body(_read(ONLINE), "online_mode"))
    m = re.search(r"NET_CANCELLED\s*\)\s*\{([^}]*)\}", dial)
    assert m, "online_mode never handles NET_CANCELLED"
    assert "return" in m.group(1), "a cancelled dial falls through to the retry"


def test_the_polled_dial_offers_the_poll_and_obeys_it():
    """A slice long enough to matter is a button the player has to hold; a poll
    whose answer is ignored is worse than none at all."""
    src = _nocomments(_read(NET_CONNECT_C))
    body = _body(src, "dial_polled")
    assert "poll(ctx)" in body, "dial_polled never calls the poll"
    assert "*cancelled = 1" in body, "dial_polled cannot report a cancel"
    m = re.search(r"DIAL_POLL_SLICE\s+(\d+)u?", src)
    assert m, "DIAL_POLL_SLICE is gone"
    # MODEM_DIAL_TIMEOUT counts ~35s in these units, so a frame is about 50,000.
    assert int(m.group(1)) <= 100000, (
        "the slice is over two frames at %s -- the cancel gets sluggish" % m.group(1))


def test_the_dial_ignores_its_first_frames_of_input():
    """Powering the modem on goes through the SMPC, which reboots the controllers.
    All held at once is exactly what a rebooting peripheral reports, and that is
    the cancel chord, so a dial without a grace period cancels itself."""
    src = _nocomments(_read(ONLINE))
    assert re.search(r"DIAL_CANCEL_GRACE\s+\d+", src), "the dial has no input grace"
    poll = _body(src, "online_dial_poll")
    assert "DIAL_CANCEL_GRACE" in poll, "the grace is declared but never applied"


def test_a_pad_starts_a_session_on_the_keyboard():
    """The first thing multizorkd asks for is a username, and the command panel
    offers words -- a name is not a word any dictionary holds."""
    src = _nocomments(_read(ONLINE))
    dial = _body(src, "online_mode")
    # The seeding, not the restore: both assign g_cmd_mode, and only the one at
    # the top of the session is being pinned here.
    m = re.search(r"cp_init\(&cpanel\);(.*?)mode_toggle_reset", dial, re.S)
    assert m, "the panel is no longer set up at the top of the session"
    seed = m.group(1)
    assert "g_cmd_mode = IFACE_KEYBOARD" in seed, (
        "a session no longer opens on the keyboard")
    assert "g_cmd_iface" not in seed, (
        "the saved interface is seeded at connect again, which lands a panel "
        "user on the login prompt with no way to spell their name")
    assert "cv_set_lobby(1)" in seed, "a session no longer opens in lobby mode"


def test_the_saved_interface_comes_back_when_the_game_does():
    """Forcing the keyboard for the lobby is a stand-in for the seconds it is
    needed, not a repeal of the Options row. A swap the player made themselves
    outranks it, and the half-built line crosses with them either way."""
    dial = _nocomments(_body(_read(ONLINE), "online_mode"))
    m = re.search(r"cv_set_lobby\(0\);(.{0,400})", dial, re.S)
    assert m, "lobby mode is never turned off"
    after = m.group(1)
    assert "g_cmd_mode = g_cmd_iface" in after, "the preference never returns"
    assert "iface_by_hand" in after, "a manual swap does not outrank the preference"
    assert "cp_load_line" in after and "keyboard_load_line" in after, (
        "the half-built line is dropped when the interface changes under it")


def _lobby_list():
    src = _nocomments(_read(COMMAND_VIEW))
    m = re.search(r"CV_LOBBY_WORDS\[\]\s*=\s*\{(.*?)\}", src, re.S)
    assert m, "the lobby word list is gone"
    return re.findall(r'"([^"]*)"', m.group(1))


def test_the_slim_list_is_exactly_what_the_lobby_accepts():
    """Offering the story's several hundred words to a login, a room list and a
    waiting room is offering words that cannot work -- and a word this list
    offers that the server does not take is worse, because it spends a pick and
    answers 'Wrong choice or room name'."""
    got = _lobby_list()
    assert got == LOBBY_WORDS + LOBBY_NUMBERS, (
        "lobby list is %r, wanted %r" % (got, LOBBY_WORDS + LOBBY_NUMBERS))


def test_every_row_the_lobby_can_print_is_pickable():
    """A pick is the whole command, so picking "1" then "2" sends "1 2" and not
    "12" -- every two-digit row has to be on the list in one piece or it cannot
    be chosen from the panel at all."""
    got = _lobby_list()
    missing = [n for n in LOBBY_NUMBERS if n not in got]
    assert not missing, "lobby rows unreachable from the panel: %s" % missing


def test_the_number_list_matches_the_lobby_the_server_prints():
    """Both ends have to agree on how long the list can get: rows past the cap
    would be offered and refused, and rows short of it would exist on screen with
    no way to pick them."""
    src = (ROOT.parent / "saturn" / "multizorkd.c").read_text(
        encoding="utf-8", errors="replace")
    m = re.search(r"#define\s+LOBBY_MAX_ROWS\s+(\d+)", src)
    assert m, "LOBBY_MAX_ROWS is gone from multizorkd.c"
    assert int(m.group(1)) == LOBBY_MAX_ROWS, (
        "the server now prints %s rows, so CV_LOBBY_WORDS wants numbers to %d"
        % (m.group(1), int(m.group(1)) + 1))


def test_both_quits_are_offered_because_they_are_different_words():
    """inpfn_lobby matches "q" alone; inpfn_waiting_for_players and
    inpfn_player_waiting match "quit" alone. Either one at the other screen does
    nothing, so dropping one strands the player on that screen."""
    got = _lobby_list()
    assert "q" in got and "quit" in got, (
        "one of the two quits is missing -- %r" % got)


def test_the_slim_list_is_sourced_instead_of_the_story_not_beside_it():
    """Ranking the story's words below the lobby's would still show them, one
    page down, on a prompt that refuses every one."""
    src = _nocomments(_read(COMMAND_VIEW))
    refill = _body(src, "cv_refill_words")
    m = re.search(r"if\s*\(g_cv_lobby\)\s*\{(.*?)\}\s*else", refill, re.S)
    assert m, "the lobby branch does not come first in the sourcing"
    assert "CV_LOBBY_WORDS" in m.group(1), "the lobby branch sources something else"
    assert "core_n = g_cv_ncand" in refill, (
        "the lobby list is not fully protected, so cv_reorder will sort it -- on "
        "Hard that alphabetises the digits above the words")


def test_the_mode_change_is_part_of_the_candidate_cache_key():
    """The list is rebuilt only when the key changes, and the frame the game
    begins on changes nothing else the key watches."""
    stale = _nocomments(_body(_read(COMMAND_VIEW), "cv_cache_stale"))
    assert "g_cv_lobby == last_lobby" in stale, (
        "the cache cannot notice the game starting, so the lobby list survives "
        "into the game")
    assert "last_lobby = g_cv_lobby" in stale, "the key is compared but never recorded"


def test_a_lobby_word_is_the_whole_answer_and_leaves_the_cursor_alone():
    """These prompts take one token, so a pick submits rather than opening a
    sentence -- and it must not walk the slot chain on the way, because that
    restores the noun slot's remembered row and drags the cursor off the word
    just picked, which a yes/no prompt asks for again straight away."""
    accept = _nocomments(_body(_read(COMMAND_VIEW), "cv_word_accept"))
    m = re.search(r"if\s*\(g_cv_lobby\)\s*\{(.*?)\}", accept, re.S)
    assert m, "picking a word does not special-case the lobby"
    branch = m.group(1)
    assert "cp_pick_whole" in branch, "the lobby pick still walks the slot chain"
    assert "cp_submit" not in branch, (
        "cp_pick + cp_submit is the cursor-moving pair cp_pick_whole exists to "
        "replace")

    whole = _nocomments(_body(_read(COMMAND_PANEL_C), "cp_pick_whole"))
    assert "CP_SLOT_DONE" in whole and "submitted = 1" in whole, (
        "cp_pick_whole does not submit")
    assert "slot_remember" not in whole and "slot_restore" not in whole, (
        "cp_pick_whole touches the slot memory it exists to skip")


def test_the_rose_is_shut_while_there_is_no_room_to_leave():
    """Twelve directions in the lobby are twelve more words the prompt refuses,
    which is the thing lobby mode takes out of the module beside it."""
    room = _nocomments(_body(_read(ONLINE), "netbin_room"))
    assert "cv_lobby()" in room, "the stand-in room does not know about the lobby"
    assert "RM_EXIT_NONE" in room, "the lobby rose still offers directions"
    assert "RM_EXIT_OPEN" in room, (
        "the all-open stand-in is gone -- a named-but-undecoded room still wants it")


def main():
    fails = 0
    for name, fn in sorted(globals().items()):
        if not name.startswith("test_") or not callable(fn):
            continue
        try:
            fn()
        except AssertionError as e:
            print("%s: %s" % (name, e), file=sys.stderr)
            fails += 1
    if fails:
        print("test_online_session: %d FAILED" % fails, file=sys.stderr)
        sys.exit(1)
    print("test_online_session: OK")


if __name__ == "__main__":
    main()
