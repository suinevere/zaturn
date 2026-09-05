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
    """"if applicable": the mouse is always a cursor (that sheet's "N/A (no mouse
    on/off)"), and neither the light gun nor the twin stick has a Scrolling
    column."""
    tbl = _table(_read(MENU_PAGES), "CTL_DEV")
    rows = {}
    for line in tbl.splitlines():
        if "DEV_" in line and "{" in line:
            key = line[line.index("DEV_"):].split()[0].strip("*/ ")
            rows[key] = line
    assert "CSB_MOUSE" not in rows["DEV_MOUSE"], "the mouse gained a Mouse Mode sheet"
    assert "CSB_SCR" not in rows["DEV_GUN"], "the light gun gained a Scrolling sheet"
    assert "CSB_SCR" not in rows["DEV_TWIN"], "the twin stick gained a Scrolling sheet"
    assert "CSB_MOUSE" in rows["DEV_PAD"], "the pad lost its Mouse Mode sheet"


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
