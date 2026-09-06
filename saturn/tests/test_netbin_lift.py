#!/usr/bin/env python3
"""Assert netbin_pages.cxx's lifted bodies match their menu_pages.cxx originals.

The netbin links a three-screen slice of menu_pages.cxx rather than the whole
51.7 KB file. That slice is a verbatim move, so any divergence is either a
transcription error or an undocumented edit -- both worth failing on.

Two bodies are deliberately NOT compared, and get inline checks in main()
instead. network_page is renamed to netbin_dial_page and its row set changes
(Cancel out, Controls in). display_options_page loses everything that serves
the Dynamic palette -- the Dimming row and the wallpaper image-slot pinning --
which is what keeps title.cxx out of the netbin's link.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

def body(path, sig):
    r"""Return the brace-balanced body of the DEFINITION whose signature is sig.

    [^;{]* rather than \s* between the signature and the brace, for two
    reasons. It spans menu_digit_row's multi-line parameter list, and it
    refuses to cross a `;`, which is what makes it skip forward declarations --
    netbin_pages.cxx forward-declares controls_dispatch above
    netbin_dial_page, and a naive find() would latch onto that `;` and then
    walk into the wrong function's braces.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(re.escape(sig) + r"[^;{]*\{", text)
    assert m, f"no definition of {sig!r} in {path.name}"
    i = text.index("{", m.start())
    depth, j = 0, i
    while True:
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0: return text[i:j+1]
        j += 1

def normalize(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", " ", s)
    return re.sub(r"\s+", "", s)

def main():
    old = SRC / "menu" / "menu_pages.cxx"
    new = SRC / "net" / "netbin_pages.cxx"
    pairs = [
        ("static bool controls_page(void)",           "static bool controls_page(void)"),
        ("bool keyboard_controls_page(void)",         "bool keyboard_controls_page(void)"),
        ("static void controls_dispatch(void)",       "static void controls_dispatch(void)"),
        ("static bool menu_digit_row(",               "static bool menu_digit_row("),
        ("static void gameplay_page(void)",           "static void gameplay_page(void)"),
    ]
    fails = 0
    for a, b in pairs:
        if normalize(body(old, a)) != normalize(body(new, b)):
            print(f"MISMATCH: {a}", file=sys.stderr); fails += 1

    # The Controls table moves verbatim too. CTL_DEV used to be checked beside it
    # and is gone: the sheets mask it carried described a submenu list the root
    # page no longer builds, and its Menu column a row that no longer prints.
    # FACE_LABEL/CHORD_LABEL used to be
    # the pair checked here; they went when the page stopped being two flat views
    # and became a device pager over one submenu per controls.xls sheet, and their
    # strings now live inside ctl_sheet_rows, which the controls_page pair covers.
    for tbl in ("CS_NAME",):
        oa = normalize(re.search(rf"{tbl}\[[A-Z_]+\]\s*=\s*\{{.*?\}};",
                                 old.read_text(encoding='utf-8'), re.S).group(0))
        nb = normalize(re.search(rf"{tbl}\[[A-Z_]+\]\s*=\s*\{{.*?\}};",
                                 new.read_text(encoding='utf-8'), re.S).group(0))
        if oa != nb:
            print(f"MISMATCH: {tbl}", file=sys.stderr); fails += 1

    # The dialer keeps its validation contract but not its Cancel row.
    dial = body(new, "void netbin_dial_page(void)")
    for must in ("valid_dialnum", "options_save", "controls_dispatch"):
        if must not in dial:
            print(f"MISSING in netbin_dial_page: {must}", file=sys.stderr); fails += 1
    if "Cancel" in dial:
        print("netbin_dial_page still offers a Cancel row", file=sys.stderr); fails += 1

    # The Display page keeps the three rows it can honour and none of the
    # Dynamic-palette machinery. display_pin_dynamic_slot is the one that
    # matters most: title_bg_loaded_file lives in title.cxx, which this build
    # does not link.
    disp = body(new, "static void display_options_page(void)")
    for must in ("DR_PALETTE", "DR_BG", "DR_TEXT", "display_cycle_row", "options_save"):
        if must not in disp:
            print(f"MISSING in netbin display_options_page: {must}", file=sys.stderr); fails += 1
    for banned in ("DR_DIM", "display_pin_dynamic_slot", "display_dynamic_slot",
                   "title_bg_loaded_file", "DISP_PAL_DYNAMIC"):
        if banned in disp:
            print(f"netbin display_options_page still carries {banned}", file=sys.stderr); fails += 1

    # The pause menu offers exactly the six rows it is specified to offer, and
    # reaches Restart through the same confirm the soft-reset chord uses.
    # Sound joined the list when the netbin gained generated music: it has no
    # CD-DA and no Blorb, but the synth plays on every netbin, so there is a
    # level to set. The rows still banned below are the ones that would name
    # something this build genuinely does not have.
    pause = body(new, "void netbin_pause_menu(void)")
    for must in ("PI_RESUME", "PI_DISPLAY", "PI_GAMEPLAY", "PI_SOUND", "PI_CONTROLS",
                 "PI_RESTART", "display_options_page", "gameplay_page",
                 "sound_options_page", "controls_dispatch",
                 "confirm_return_to_title"):
        if must not in pause:
            print(f"MISSING in netbin_pause_menu: {must}", file=sys.stderr); fails += 1
    for banned in ("Save Game", "Load Game", "Network", "Title Screen"):
        if banned in pause:
            print(f"netbin_pause_menu offers a {banned} row", file=sys.stderr); fails += 1

    # Every modal the netbin opens over a live session runs its own poll loop,
    # so online_mode has to hand menu_sync an RX pump for the duration or the
    # UART FIFO overruns behind it -- transport_uart.c has no software ring.
    online = (SRC / "net" / "online.cxx").read_text(encoding="utf-8")
    if "menu_set_service(pause_service" not in online:
        print("online_mode opens the pause menu without registering an RX pump",
              file=sys.stderr); fails += 1
    if "menu_set_service(nullptr, nullptr)" not in online:
        print("online_mode never clears the RX pump", file=sys.stderr); fails += 1
    # A longjmp out of the pause menu leaves that pointer aimed at a dead frame.
    main_nb = (SRC / "main_netbin.cxx").read_text(encoding="utf-8")
    if "menu_set_service(nullptr, nullptr)" not in main_nb:
        print("main_netbin's soft-reset landing never clears the RX pump",
              file=sys.stderr); fails += 1

    # No page may drop a frame with a bare Synchronize. dash_frame_end takes
    # NBG2 down on any frame nobody claims it, and a page's entry and exit
    # frame-drops happen with a menu box on screen -- a bare Synchronize there
    # blanks the border for exactly one frame, which is what the flicker was.
    # menu_sync claims and then Synchronizes. Matched at page scope (four-space
    # indent) so the fade loops and nested waits, which are indented further and
    # hold the layer their own way, are not swept up.
    for f in (SRC / "menu" / "menu_pages.cxx", SRC / "net" / "netbin_pages.cxx"):
        bare = [n for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1)
                if line == "    SRL::Core::Synchronize();"]
        if bare:
            print(f"{f.name}: bare Synchronize at page scope, line(s) "
                  f"{', '.join(map(str, bare))} -- use menu_sync", file=sys.stderr)
            fails += 1

    if fails:
        print(f"test_netbin_lift: {fails} FAILED", file=sys.stderr); sys.exit(1)
    print("test_netbin_lift: OK")

main()
