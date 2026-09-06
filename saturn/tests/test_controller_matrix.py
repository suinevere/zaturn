#!/usr/bin/env python3
"""Pin saturn/src/input/controller.{h,cxx} against controls.xls, as transcribed
into docs/CONTROLS_MATRIX.md.

The workbook is the spec and it is a binary file nobody diffs, so the risk this
guards is drift: a device column losing its DevKind, an action row losing its
DevAction, a blank cell quietly acquiring a binding, or the light-gun wedge guard
being refactored out of controller_kind. None of that would fail a compile.

Runs both ways: `python saturn/tests/test_controller_matrix.py` prints findings and
exits non-zero; `pytest saturn/tests/test_controller_matrix.py` collects the
test_* functions.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HDR = ROOT / "src" / "input" / "controller.h"
SRC = ROOT / "src" / "input" / "controller.cxx"
DOC = ROOT.parent / "docs" / "CONTROLS_MATRIX.md"

# One per device column of the workbook.
KINDS = [
    "DEV_NONE", "DEV_PAD", "DEV_FLIGHT", "DEV_ANALOG",
    "DEV_MOUSE", "DEV_TWIN", "DEV_GUN", "DEV_KBD",
]

# One per action row of the workbook.
ACTIONS = [
    "DA_MENU", "DA_LETTER", "DA_BACK", "DA_SPACE", "DA_ACCEPT",
    "DA_MAP", "DA_RECALL", "DA_SCROLL", "DA_PAGE", "DA_ENDS",
]

# The workbook's "(2)" rows, which carry a direction rather than firing once.
DIRECTIONAL = ["DA_SCROLL", "DA_PAGE", "DA_ENDS"]


def _read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def _nocomments(src):
    """Strip C comments, so a test cannot be satisfied -- or defeated -- by prose
    that merely names the thing it is checking for. Written as a scan rather than
    a regex because the block form nests no better either way and the line form
    needs an escape this file would rather not carry."""
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


def test_every_device_column_has_a_kind():
    h = _read(HDR)
    missing = [k for k in KINDS if not re.search(r"\b%s\b" % k, h)]
    assert not missing, "DevKind values missing from controller.h: %s" % missing


def test_every_action_row_has_an_action():
    h = _read(HDR)
    missing = [a for a in ACTIONS if not re.search(r"\b%s\b" % a, h)]
    assert not missing, "DevAction values missing from controller.h: %s" % missing


def test_classifier_covers_every_peripheral_id():
    """Each device column must be reachable from controller_kind, or the module
    can never see that device however well the rest of it is written."""
    body = _body(_read(SRC), "controller_kind")
    for k in KINDS:
        if k == "DEV_NONE":
            continue
        assert k in body, "controller_kind never returns %s" % k


def test_light_gun_wedge_is_rejected_by_id():
    """controller_kind must drop id 0x00 before classifying. SRL puts that id in
    the Digital family and reads its all-zero data word as every button held, so a
    classifier that falls through to the family test reports a phantom pad."""
    body = _body(_read(SRC), "controller_kind")
    assert "ID_WEDGED" in body, "controller_kind no longer checks ID_WEDGED"
    wedge = body.index("ID_WEDGED")
    switch = body.index("switch")
    assert wedge < switch, "the ID_WEDGED guard must run before the id switch"


def test_blank_cells_stay_unbound():
    """The twin stick and the light gun have blank Map and Recall cells, and the
    analogue pad has a blank Scroll cell. A binding appearing in one of those
    readers means the matrix drifted."""
    src = _read(SRC)
    for fn, forbidden in (
        ("read_twin", ["DA_MAP", "DA_RECALL", "DA_SCROLL", "DA_PAGE", "DA_ENDS"]),
        ("read_gun",  ["DA_MAP", "DA_RECALL", "DA_SCROLL", "DA_PAGE", "DA_ENDS"]),
    ):
        body = _body(src, fn)
        bound = [a for a in forbidden if a in body]
        assert not bound, "%s binds actions the workbook leaves blank: %s" % (fn, bound)


def test_analogue_pad_does_not_scroll():
    """Only the flight stick column fills in the Scrolling sheet; read_sticks must
    gate its scroll edges on DEV_FLIGHT."""
    body = _body(_read(SRC), "read_sticks")
    assert "DEV_FLIGHT" in body, "read_sticks no longer distinguishes the flight stick"
    gate = body.index("DEV_FLIGHT")
    for a in ("DA_SCROLL", "DA_PAGE"):
        assert a in body, "read_sticks no longer reports %s" % a
        assert body.index(a) > gate, "%s is emitted outside the DEV_FLIGHT gate" % a


def test_directional_rows_are_marked_directional():
    """The three "(2)" rows must be documented as carrying a direction, since
    callers pass dir and every other row is queried with 0."""
    h = _read(HDR)
    for a in DIRECTIONAL:
        m = re.search(r"%s,.*" % a, h)
        assert m, "%s has no enum line to document" % a


def test_keyboard_column_binds_only_menu_and_map():
    """The keyboard's row is blank except Esc and F8, because it types directly."""
    body = _body(_read(SRC), "controller_feed_key")
    assert "SATURN_KEY_ESCAPE" in body and "DA_MENU" in body
    assert "SATURN_KEY_F8" in body and "DA_MAP" in body
    for a in ("DA_LETTER", "DA_BACK", "DA_SPACE", "DA_ACCEPT", "DA_RECALL"):
        assert a not in body, "controller_feed_key binds %s, which is a blank cell" % a


def test_twin_stick_table_is_flagged_provisional():
    """The twin stick's bits are a guess; the flag is what stops a later reader
    treating them as measured."""
    src = _read(SRC)
    m = re.search(r"TWIN_TRIG_L.*?----------------------\*/", src, re.S)
    assert m, "the TWIN_* table lost its header block"
    assert "PROVISIONAL" in m.group(0), "the TWIN_* table is no longer flagged provisional"


def test_space_default_is_y():
    """controls.xls puts Space on Y, which shipped as the face group's fourth
    button. If this table goes back to X the workbook and the build disagree."""
    body = _body(_read(ROOT / "src" / "input" / "input.cxx"), "face_button")
    assert "Button::Y" in body, "the face group's fourth button is no longer Y"
    assert "Button::X" not in body, "X is back in the face group"


def test_doc_transcription_exists():
    """The workbook is binary; the markdown copy is what review can actually read."""
    assert DOC.exists(), "docs/CONTROLS_MATRIX.md is missing"
    d = _read(DOC)
    for col in ("6 pad", "Flight Stick", "Analogue", "mouse",
                "twin stick", "light gun", "Keyboard"):
        assert col in d, "the transcription lost the %s column" % col


MENU_PAGES = ROOT / "src" / "menu" / "menu_pages.cxx"
CMD_VIEW = ROOT / "src" / "video" / "command_view.cxx"
INPUT_CXX = ROOT / "src" / "input" / "input.cxx"


def _table(src, name):
    """The text of a file-scope array initialiser, from its name to the `};`."""
    i = src.index(name + "[")
    j = src.index("};", i)
    return src[i:j + 2]


def test_controls_root_shows_static_and_submenus():
    """The root Controls page carries the workbook's Static row and one submenu
    per configurable sheet; the sheets are pages of their own."""
    body = _body(_read(MENU_PAGES), "controls_page")
    assert "Menu (fixed)" in body, "the root page no longer prints the Static row"
    assert "controls_sheet_page" in body, "the root page opens no sheet submenu"
    assert "CS_NAME" in body, "the submenu rows are not named from CS_NAME"


def test_controls_pages_only_connected_devices():
    """A page for a controller nobody has plugged in can only mislead."""
    body = _body(_read(MENU_PAGES), "ctl_dev_list")
    assert "controller_present" in body, "the device list is not gated on presence"


def test_static_row_matches_the_workbook():
    """Each device's Static cell, straight off the Static sheet."""
    tbl = _table(_read(MENU_PAGES), "CTL_DEV")
    for want in ('"Start"', '"Blue button"', '"Button"', '"ESC"'):
        assert want in tbl, "CTL_DEV lost the Static value %s" % want


def test_sheets_applicable_per_device():
    """"if applicable": neither the light gun nor the twin stick has a Scrolling
    column, and the mouse's Mouse Mode sheet carries a speed but no on/off, which
    is that sheet's "N/A (no mouse on/off)"."""
    src = _read(MENU_PAGES)
    tbl = _table(src, "CTL_DEV")
    rows = {}
    for line in tbl.splitlines():
        if "DEV_" in line and "{" in line:
            key = line[line.index("DEV_"):].split()[0].strip("*/ ")
            rows[key] = line
    assert "CSB_SCR" not in rows["DEV_GUN"], "the light gun gained a Scrolling sheet"
    assert "CSB_SCR" not in rows["DEV_TWIN"], "the twin stick gained a Scrolling sheet"
    assert "CSB_MOUSE" in rows["DEV_PAD"], "the pad lost its Mouse Mode sheet"
    body = _body(src, "ctl_sheet_rows")
    mouse = body[body.index("k == DEV_MOUSE && sheet == CS_MOUSE"):]
    assert "CK_MSPEED" in mouse, "the mouse lost its speed row"
    assert "CK_MMODE" not in mouse, "the mouse gained a Mouse Mode on/off it cannot have"


def test_cursor_source_is_named_per_device():
    """The naming is the point: a 3D Control Pad calls it the Analogue Stick and a
    Mission Stick calls the same reading its Left Stick."""
    tbl = _table(_read(SRC), "CSRC_NAME")
    for want in ('"D-Pad"', '"Left Stick"', '"Analogue Stick"'):
        assert want in tbl, "CSRC_NAME lost %s" % want


def test_dpad_is_gated_while_it_steers_the_cursor():
    """One job at a time: with Mouse Mode on and the D-pad chosen, the four
    directions must stop stepping selections."""
    body = _body(_read(INPUT_CXX), "pad_fired")
    assert "controller_dpad_is_cursor" in body, "pad_fired no longer gates directions"
    assert "is_direction" in body, "the gate no longer distinguishes directions"
    raw = _body(_read(INPUT_CXX), "pad_fired_raw")
    assert "controller_dpad_is_cursor" not in raw, "the ungated read gained the gate"


def test_mouse_y_is_not_inverted():
    """Measured on hardware: the reported sign already runs the way the screen
    does, and negating it moved the cursor up when the hand went down."""
    body = _body(_read(SRC), "read_mouse")
    assert "g_ptr.y + mouse_travel(dy," in body, "mouse Y is negated again"
    assert "g_ptr.y - " not in body, "mouse Y is negated again"


def test_pointer_reaches_menus():
    """A menu runs its own loop and never reaches the game loop's tick, so without
    this the cursor freezes the moment a menu opens."""
    body = _body(_read(ROOT / "src" / "menu" / "menu.cxx"), "menu_sync")
    assert "controller_tick" in body, "menus no longer tick the controller"
    assert "render_pointer" in body, "menus no longer draw the cursor"


def test_sheets_are_separate_configuration_groups():
    """A slot swap must stay inside one controls.xls sheet, or remapping a
    Scrolling row silently moves an Actions row the player cannot see."""
    body = _body(_read(INPUT_CXX), "chord_assign")
    assert "chord_group" in body, "chord_assign swaps across sheets again"


def test_caps_has_no_pad_binding():
    """Caps is an Options row now; L+R carries the dashboard swap instead."""
    src = _read(INPUT_CXX)
    assert "mode_combo_fired" in src, "the L+R combo is gone"
    assert "caps_combo_fired" not in src, "Caps is back on the pad"


def test_command_module_rows():
    """The far-right dashboard module, in the order it is drawn."""
    tbl = _table(_read(CMD_VIEW), "CV_CMD_ROW")
    got = [p.split('"')[0] for p in tbl.split('"')[1::2]]
    assert got == ["menu", "invent", "look", "map", "swap"], got


def test_scroll_markers_are_symmetric():
    """Both markers are shoot targets, so both are the same width at the same
    column; a one-cell caret against a six-cell "more v" is neither."""
    body = _body(_read(ROOT / "src" / "video" / "console_view.cxx"), "render_console")
    assert body.count("CV_MORE_X") == 2, "the two markers no longer share a column"
    assert '"more ^"' in body and '"more v"' in body, "a marker lost its wording"


def test_scroll_markers_are_shootable():
    """The mouse's "player clicks up/down arrows" row, and the gun aimed at the
    same cells, with a held button repeating."""
    body = _body(_read(ROOT / "src" / "video" / "console_view.cxx"),
                 "console_pointer_scroll")
    assert "controller_hold_fired" in body, "a held button no longer repeats"
    assert "DEV_HOLD_SCROLL_UP" in body and "DEV_HOLD_SCROLL_DOWN" in body


def test_picture_edge_moves():
    """A shot at the picture's edge is a move, and only where the room has that
    exit."""
    body = _body(_read(CMD_VIEW), "cv_pointer_travel")
    assert "RM_EXIT_OPEN" in body, "the edge shot no longer checks the exit"
    assert "controller_pointer_consume" in body, "the shot is not consumed"
    assert "room_model_dir_word" in body, "the edge shot submits no direction"


def test_a_cursor_is_actually_drawn():
    """The pointer is useless if nothing paints it: render_pointer must place a
    cell through the text layer's cursor overlay."""
    body = _body(_read(ROOT / "src" / "video" / "console_view.cxx"), "render_pointer")
    assert "text_cursor_set" in body, "render_pointer paints nothing"
    assert "text_cursor_off" in body, "the cursor is never taken away"
    assert "controller_pointer" in body, "render_pointer reads no pointer"


def test_cursor_overlay_paints_after_the_block_copy():
    """The copy is what restores the character under the cursor, so the cursor has
    to go down after it or it erases itself."""
    body = _body(_read(ROOT / "src" / "video" / "text_map.cxx"), "text_flush")
    copy = body.index("for (int i = 0; i < longs; i++)")
    paint = body.index("g_cur_word")
    assert paint > copy, "the cursor is painted before the block copy erases it"


def test_mouse_mode_is_reachable():
    """A Mouse Mode nothing can switch on is a cursor that never moves."""
    src = _read(ROOT / "src" / "menu" / "menu_pages.cxx")
    assert "controller_mouse_mode_set" in src, "no Mouse Mode toggle on the Controls page"
    assert "controller_twin_set" in src, "no Twin Stick toggle on the Controls page"


def test_every_pointing_device_updates_its_cell():
    """col/row come from clamp_cursor, so any reader that moves the cursor has to
    call it -- the gun sets absolute coordinates and once did not."""
    src = _read(SRC)
    for fn in ("read_gun", "read_mouse", "read_sticks", "read_dpad_cursor"):
        body = _body(src, fn)
        assert "clamp_cursor" in body, "%s moves the cursor without recomputing its cell" % fn


def test_cursor_is_an_arrow_with_a_top_left_tip():
    """An arrow, not a reticle, and its tip is the cell's own origin so what the
    player aims at and what the program selects are the same pixel."""
    src = _read(ROOT / "src" / "video" / "text_map.cxx")
    tbl = _table(src, "CURSOR_ARROW_FILL")
    assert "0x80" in tbl, "the arrow lost its top-left tip pixel"
    assert "install_cursor_glyph" in src, "the arrow glyph is never installed"
    body = _body(_read(ROOT / "src" / "video" / "console_view.cxx"), "render_pointer")
    assert "TEXT_CURSOR_CH" in body, "render_pointer does not draw the arrow"


def test_any_button_dismisses_a_prompt():
    """"press any key" has to mean any key on anything, including a gun fired off
    screen -- which now reports as a right click, so both halves still count."""
    body = _body(_read(ROOT / "src" / "menu" / "menu.cxx"), "menu_wait")
    assert "AnyPressed" in body, "menu_wait no longer takes every pad button"
    assert "controller_any_fired" in body, "menu_wait no longer takes other devices"
    any_body = _body(_read(SRC), "controller_any_fired")
    assert "g_ptr.hot" in any_body, "a pointing device's click no longer counts"
    assert "g_fired" in any_body, "an off-screen gun shot no longer counts"


def test_the_cursor_accelerates():
    """Constant speed is unusable for pointing and too slow for crossing: every
    source ramps or scales."""
    src = _read(SRC)
    dpad = _body(src, "read_dpad_cursor")
    assert "g_dpad_held" in dpad, "a held direction no longer accelerates"
    travel = _body(src, "axis_travel")
    assert "CURSOR_STEP_MAX" in travel, "the stick no longer scales with deflection"
    mouse = _body(src, "mouse_travel")
    assert "MOUSE_ACCEL_KNEE" in mouse, "the mouse no longer accelerates"


def test_mouse_reading_is_a_position_not_a_delta():
    """Measured: the Saturn mouse reports a running total, so the movement is the
    difference against last frame. Read raw, the cursor slid on until the mouse was
    carried back to where it started."""
    body = _body(_read(SRC), "read_mouse")
    assert "g_mouse_last" in body, "the mouse reading is being taken as a delta again"
    assert "g_mouse_seen" in body, "the first reading is not seeded, so it jumps"
    assert "mouse_delta" in body, "the difference is being taken without minding the wrap"


def test_the_mouse_wrap_cannot_fling_the_cursor():
    """SGL accumulates the mouse into a sixteen-bit field, so the difference has to
    be read back at that width. Narrowed to a byte -- which it was -- every frame
    that moved more than 127 counts came back with the wrong sign, and the cursor
    pinned itself against whichever edge it reached."""
    body = _nocomments(_body(_read(SRC), "mouse_delta"))
    assert "int16_t" in body, "the difference is no longer read back as sixteen-bit signed"
    assert "0xFF" not in body, "the difference is masked into a byte again"
    assert "MOUSE_JUMP_MAX" in body, "an implausible jump is no longer dropped"


def test_the_device_row_names_the_hardware():
    """A page that says "Analogue" cannot tell a 3D Control Pad from a wheel, and a
    name read once cannot follow a hot-swap. Both pages read the label live."""
    src = _nocomments(_read(SRC))
    assert "controller_port_name" in src, "the model name is no longer read off the id"
    label = _nocomments(_body(_read(SRC), "controller_kind_label"))
    assert "controller_port_name" in label, "the label no longer names what is attached"
    assert "controller_kind_name" in label, "an unattached kind has nothing left to fall back to"
    for page in ("menu/menu_pages.cxx", "net/netbin_pages.cxx"):
        body = _nocomments(_read(ROOT / "src" / page))
        assert "controller_kind_label" in body, page + " no longer names the attached device"


def test_slow_mouse_movement_is_not_thrown_away():
    """Integer division at any gain below 1 discards every movement too small to
    make a whole pixel, which reads as the cursor having no resolution."""
    body = _body(_read(SRC), "mouse_travel")
    assert "g_mouse_rem" in body, "the sub-pixel remainder is no longer carried"
    assert "MOUSE_TRAVEL_MAX" in body, "one frame's travel is no longer bounded"


def test_every_press_any_prompt_takes_every_device():
    """One narrow wait strands whoever is holding the wrong thing, so all four are
    checked together rather than each being remembered separately."""
    for path, fn in (
        (ROOT / "src" / "menu" / "menu.cxx", "menu_wait"),
        (ROOT / "src" / "video" / "splash.cxx", "splash_skip_pressed"),
        (ROOT / "src" / "net" / "online.cxx", "online_wait_any"),
    ):
        body = _body(_read(path), fn)
        assert "AnyPressed" in body, "%s takes only some pad buttons" % fn
        assert "controller_any_fired" in body, "%s takes no other device" % fn
    title = _read(ROOT / "src" / "video" / "title.cxx")
    assert "controller_any_fired" in title, "the title takes no other device"
    assert "WasPressed(Button::C) || g_pad->WasPressed(Button::START)" not in title,         "the title still has its own four-button test"


def test_accept_spans_the_two_buttons_the_layouts_disagree_about():
    """SRL calls the digital A, C and B bits Left, Right and Middle -- flags bits
    2, 1, 0 -- while the hardware's own order for that byte is Left, Right, Middle
    at bits 0, 1, 2. The two disagree about the outer buttons and agree about the
    right one, so accept takes both of the first pair and back takes only Right."""
    act = _nocomments(_body(_read(ROOT / "src" / "menu" / "menu.cxx"), "menu_pointer_act"))
    assert "DEV_BTN_LEFT" in act and "DEV_BTN_MIDDLE" in act,         "accept no longer spans both buttons the two layouts disagree about"
    assert "DEV_BTN_RIGHT" not in act, "accept claims the button back needs"
    back = _nocomments(_body(_read(ROOT / "src" / "menu" / "menu.cxx"), "menu_pointer_back"))
    assert "DEV_BTN_RIGHT" in back, "back no longer reads the right button"


def test_menus_can_be_backed_out_of_with_a_click():
    """Every list and page that takes a pad B takes a right click too."""
    for path, fn in (
        (ROOT / "src" / "menu" / "menu.cxx", "select_at"),
        (ROOT / "src" / "menu" / "menu_pages.cxx", "controls_page"),
        (ROOT / "src" / "menu" / "menu_pages.cxx", "controls_sheet_page"),
        (ROOT / "src" / "menu" / "menu_pages.cxx", "options_menu"),
    ):
        body = _nocomments(_body(_read(path), fn))
        assert "menu_pointer_back" in body, "%s cannot be backed out of with a click" % fn


def test_pad_actions_do_not_read_stale_repeat_state():
    """controller_tick runs from menu_sync, and no menu or the title advances the
    repeat timers. Reading pad_fired there returns whatever the last screen that
    did advance them left behind -- one flag stuck true makes controller_any_fired
    true on every frame, which is a title screen that cannot be waited on."""
    body = _nocomments(_body(_read(SRC), "read_pad_family"))
    assert "pad_fired" not in body, "the pad's actions read repeat state again"
    assert "WasPressed" in body, "the pad's actions no longer read an edge"
    assert "chord_ticked" in body, "the chords are not gated on having been ticked"


def test_a_click_does_not_leak_into_the_next_screen():
    """The pad's stale edge passes with a Synchronize; the pointer's is module
    state that only clears on the next tick, so it has to be discarded by hand."""
    body = _body(_read(ROOT / "src" / "menu" / "menu.cxx"), "select_at")
    assert "controller_pointer_flush" in body, "a list can inherit the click that opened it"
    for path in (ROOT / "src" / "engine" / "saturn_glue.cxx",
                 ROOT / "src" / "net" / "online.cxx"):
        src = _nocomments(_read(path))
        assert src.count("mode_toggle_reset()") == src.count("controller_pointer_flush()"),             "a modal settles the combo latch without settling the pointer's"


def test_generic_list_takes_a_click():
    """select_at is the mode menu, the game picker and the save pickers at once."""
    body = _body(_read(ROOT / "src" / "menu" / "menu.cxx"), "select_at")
    assert "menu_pointer_row" in body, "the generic list has no hover"
    assert "menu_pointer_act" in body, "the generic list takes no click"


def main():
    fails = 0
    for name, fn in sorted(globals().items()):
        if not name.startswith("test_") or not callable(fn):
            continue
        try:
            fn()
        except AssertionError as e:
            sys.stderr.write("FAIL %s: %s\n" % (name, e))
            fails += 1
    sys.stderr.write("test_controller_matrix: %s\n" % ("%d FAILED" % fails if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())


def test_a_gun_shot_off_the_screen_goes_back():
    """A gun has one trigger and cannot point at a Cancel row it is not aiming at,
    so the shot that misses the raster is its Back -- the same right click a mouse
    gives, so every page that honours one honours the other."""
    body = _nocomments(_body(_read(SRC), "read_gun"))
    assert "DEV_BTN_RIGHT" in body, "an off-screen shot no longer fires as a right click"
    assert "DA_ACCEPT" not in body, "an off-screen shot still accepts"
    fire = _nocomments(_body(_read(SRC), "pointer_fire"))
    assert "DA_LETTER, DA_ACCEPT, DA_BACK" in fire,         "the fallback table no longer matches menu_pointer_act/back"


def test_every_yes_no_box_is_answerable_by_pointer():
    """A box that prints "B = no" is unanswerable to a mouse and a gun, which have
    no B to read it with. Every yes/no question is the same two-cell widget."""
    menu = _read(ROOT / "src" / "menu" / "menu.cxx")
    for fn in ("menu_yesno_input", "menu_yesno_draw", "menu_yesno_hit"):
        assert fn + "(" in menu, "menu.cxx lost " + fn
    yn = _nocomments(_body(menu, "int menu_yesno_input"))
    assert "menu_pointer_back" in yn, "Back no longer answers No"
    assert "menu_pointer_act" in yn, "a click no longer picks a cell"
    for path, fn in ((ROOT / "src" / "menu" / "menu.cxx", "bool menu_confirm"),
                     (ROOT / "src" / "engine" / "soft_reset.cxx", "bool confirm_return_to_title"),
                     (ROOT / "src" / "menu" / "save_ui.cxx", "void save_space_warn")):
        body = _nocomments(_body(_read(path), fn))
        assert "menu_yesno_input" in body, fn + " does not use the yes/no widget"
        assert "hint(" not in body, fn + " still prints a button legend"


def test_every_menu_page_answers_a_pointer():
    """The audit this file exists for, applied to the pages rather than to the
    device table: a page a mouse or a gun cannot work is a page that strands
    whoever is holding one, and they are reachable from every menu."""
    pages = {
        "menu/menu_pages.cxx": ["network_page", "controls_sheet_page", "controls_page",
                                "keyboard_controls_page", "sound_options_page",
                                "display_options_page", "credits_page",
                                "gameplay_page", "options_menu"],
        "net/netbin_pages.cxx": ["netbin_dial_page", "controls_page",
                                 "keyboard_controls_page", "display_options_page",
                                 "gameplay_page", "sound_options_page",
                                 "netbin_pause_menu"],
        "menu/save_ui.cxx":     ["pick_slot_and_name", "save_space_warn"],
        "video/map_view.cxx":   ["map_view_show"],
        "menu/menu.cxx":        ["select_at"],
    }
    for path, fns in pages.items():
        src = _read(ROOT / "src" / path)
        for fn in fns:
            body = _nocomments(_body(src, fn))
            hit = ("menu_pointer_" in body) or ("menu_yesno_input" in body)
            assert hit, path + ": " + fn + " answers no pointing device"
