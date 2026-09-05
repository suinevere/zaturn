/*----------------------
 | menu_pages.cxx
 | Description: Implements the Options menu and its sub-pages: the Network
 |   dial-number editor with its Ok/Cancel rows, the Controls pages (a live
 |   gamepad remap editor plus Keyboard Caps toggle, or a physical-keyboard
 |   settings page, chosen and switched between by controls_dispatch as the
 |   active input device changes), Sound Options, Display Options, and the
 |   read-only, paginated Credits page. Every page constructs a
 |   MenuBacking on entry (menu.h) so an image background stays suppressed
 |   for the page's lifetime, and drops the input edge that opened it with an
 |   initial SRL::Core::Synchronize() before entering its poll loop, so the
 |   same button press that opened the page cannot also act inside it. Pages
 |   that offer Ok/Cancel snapshot the state they edit on entry and restore
 |   it verbatim on Cancel. menu_digit_row is the shared C++ binding from a
 |   polled key-char to a page's own (row, direction) locals, backing
 |   menu_layout.c's unit-tested digit-to-row mapping -- the layout unit
 |   itself cannot reference a page's local bool state.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.c, input.h (g_pad/g_face_btn/g_chord_slot/
 |   face_assign/chord_assign/face_btn_name/slot_name/pad_repeat_update/
 |   mapping_reset_defaults), console_view.h (note_input_device/hint/
 |   g_kbd_visible), options.h (options_save/display_apply/
 |   display_cycle_row/valid_dialnum), app_state.h (g_difficulty/g_dialnum/
 |   g_display/g_music_level/g_pcm_level/g_in_game/
 |   g_cmd_iface), keyboard.h, saturn_keyboard.h, soft_reset.h,
 |   display.h, sound.h, music.h, SRL
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "menu_pages.h"
#include "menu.h"
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "controller.h"
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
#include "title.h"
#include "map_view.h"

extern "C" {
#include "keyboard.h"
#include "numpad.h"
#include "menu_layout.h"
#include "display.h"
#include "sound.h"
#include "music.h"
}

/*----------------------
 | page_fade_out / page_fade_in
 | Description: Guarded wrappers over menu_fade_out/menu_fade_in for the Options
 |   pages. They fade only when g_menu_page_fade > 0, which main() sets while it
 |   drives the title -> Options transition and leaves 0 otherwise. This keeps
 |   the in-game F10/F11/F12 openings of these same pages (from saturn_glue.cxx,
 |   over live gameplay rather than a faded menu) an instant open/close as
 |   before -- and, critically, stops an unconditional exit fade from leaving the
 |   game screen stuck black. Passing 0 to menu_fade_* would divide by zero, so
 |   the >0 guard lives here rather than at each of the ~20 call sites.
 | Author: suinevere
 | Dependencies: menu.c (menu_fade_out/menu_fade_in, g_menu_page_fade)
 | Globals: g_menu_page_fade
 | Params: frames -- g_menu_page_fade snapshot (0 = no fade)
 | Returns: N/A
 ----------------------*/
static void page_fade_out(int frames) { if (frames > 0) menu_fade_out(frames); }
static void page_fade_in(int frames)  { if (frames > 0) menu_fade_in(frames); }

/*----------------------
 | menu_digit_row
 | Description: Reads one polled key-char event and, via menu_row_digit
 |   (menu_layout.c, unit-tested standalone), maps the character to a row
 |   index in [0, nrows) plus a direction -- a plain digit selects forward,
 |   the shifted symbol above it selects backward. On a match, writes `sel`
 |   and sets `left` or `right` per the direction, leaving the page's own
 |   activation flag for the caller to set afterward, since call sites
 |   disagree on whether that local is named `ok` or `act`.
 | Author: suinevere
 | Dependencies: menu_layout.c
 | Globals: N/A
 | Params: ke -- the polled key event; nrows -- number of selectable rows on
 |   the page; sel -- (out) row selected on a digit-row match; left, right --
 |   (out) direction flags set on a match
 | Returns: true if `ke` selected a row (sel/left/right updated); false
 |   otherwise
 ----------------------*/
static bool menu_digit_row(const SaturnKeyEvent &ke, int nrows,
                           int &sel, bool &left, bool &right) {
    if (ke.kind != SATURN_KEY_CHAR) return false;
    int ddir = 0;
    int drow = menu_row_digit(ke.ch, nrows, &ddir);
    if (drow < 0) return false;
    sel = drow;
    if (ddir > 0) right = true; else left = true;
    return true;
}

/*----------------------
 | network_page
 | Description: Network dial-number editor, sharing its numpad with the netbin
 |   build's netbin_dial_page (numpad.h) plus explicit Ok and Cancel rows. Seeds
 |   the edit buffer from g_dialnum, then loops: a real-keyboard char/backspace/
 |   enter/escape/clear edits or accepts/cancels directly. The on-screen numpad is
 |   shown only while a gamepad is the active device (g_kbd_visible) -- it is how a
 |   controller enters digits; a real keyboard hides it and the box shrinks to the
 |   input line and the two rows. np_dpad walks the pad and the rows: `arow` is -1
 |   on the pad, 0 on Ok, 1 on Cancel; C types the highlighted digit or activates
 |   the row. B backspaces; A and Start both accept regardless of cursor position,
 |   matching the shortcuts every other Options page hint advertises. B and
 |   Backspace cancel instead of backspacing when the buffer is already empty, so
 |   clearing the field and backing out is one continuous run of the same button.
 |   Accept validates the buffer with valid_dialnum before committing it into
 |   g_dialnum and calling options_save().
 |
 |   The two accepts part company on an invalid buffer, because only one of them
 |   is also the way out. A/Enter keeps the page open with an inline error, the
 |   editor's own retry. Start/Esc says the player is leaving, so it warns that
 |   the number was not saved and then goes -- refusing to close would strand
 |   them on the one Options page whose exit can fail. Cancel returns without
 |   saving and without a warning, since discarding is what was asked.
 | Author: suinevere
 | Dependencies: keyboard.c, saturn_keyboard.h, soft_reset.h, options.c
 |   (valid_dialnum, options_save), menu.c, console_view.c
 | Globals: g_dialnum
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void network_page(void) {
    MenuBacking backing;
    const int NET_ROW_W = 6;   // "Cancel", the wider of the page's two rows
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    int arow = -1;   // -1 = on the numpad; 0 = Ok; 1 = Cancel
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        // A real keyboard hides the numpad, so the cursor cannot sit on it.
        if (!g_kbd_visible && arow < 0) arow = 0;
        bool accept = false, cancel = false, leave = false;
        if      (ke.kind == SATURN_KEY_CHAR)      { if (k.input_len < DIALNUM_MAX) keyboard_type_char(&k, ke.ch); }
        else if (ke.kind == SATURN_KEY_BACKSPACE) { if (k.input_len == 0) cancel = true; else keyboard_backspace(&k); }
        else if (ke.kind == SATURN_KEY_ENTER)     accept = true;
        else if (ke.kind == SATURN_KEY_ESCAPE)    leave = true;
        else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
        else {
            bool up   = g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP;
            bool down = g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN;
            np_dpad(up, down, g_pad->WasPressed(Button::Left), g_pad->WasPressed(Button::Right),
                    g_kbd_visible, &arow, &k.cursor_row, &k.cursor_col);
            if (g_pad->WasPressed(Button::C)) {
                if      (arow == 0) accept = true;
                else if (arow == 1) cancel = true;
                else if (np_valid(k.cursor_row, k.cursor_col) && k.input_len < DIALNUM_MAX)
                    keyboard_type_char(&k, np_char(k.cursor_row, k.cursor_col));
            }
            if (g_pad->WasPressed(Button::B))     { if (k.input_len == 0) cancel = true; else keyboard_backspace(&k); }
            if (g_pad->WasPressed(Button::A))     accept = true;
            if (g_pad->WasPressed(Button::START)) leave = true;
        }
        if (cancel) { page_fade_out(g_menu_page_fade); return; }
        if (accept || leave) {
            bool valid = valid_dialnum(k.input);
            if (valid) {
                int j;
                for (j = 0; k.input[j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = k.input[j];
                g_dialnum[j] = '\0';
                options_save();
            } else if (leave) {
                menu_clear();
                menu_message("NETWORK", "Invalid number - not saved.", "(press any button)");
                menu_wait();
            }
            if (valid || leave) { page_fade_out(g_menu_page_fade); return; }
            err = "Invalid number (digits only).";
        }
        menu_clear();
        int fx, fy, fw, fh;
        /* Taller with the numpad up (gamepad), shrinking to the line and the two
           rows once a real keyboard hides it. */
        menu_box_fit("NETWORK", 24, g_kbd_visible ? 13 : 8, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "NETWORK");
        int y = fy + 4;
        menu_text(fx, fw, y++, 0, "Server dial number:");
        // The ROW is padded to the field's full length, not the text: the row
        // width has to hold still or the centred line creeps sideways by half a
        // column with every digit typed, while the caret has to stay against
        // the last digit rather than out at the end of the field.
        menu_textf(fx, fw, y++, DIALNUM_MAX + 1, "%s_", k.input);
        y++;
        if (g_kbd_visible) {
            int npx = fx + 2 + ((fw - 4) - (NP_COLS * 2 - 1)) / 2;   // centre the pad
            for (int r = 0; r < NP_ROWS; r++) {
                char rowbuf[NP_COLS * 2]; int p = 0;
                for (int c = 0; c < NP_COLS; c++) { rowbuf[p++] = np_char(r, c); if (c < NP_COLS - 1) rowbuf[p++] = ' '; }
                rowbuf[p] = '\0';
                text_print_dim(npx, y, rowbuf);
                if (arow < 0 && r == k.cursor_row && np_valid(r, k.cursor_col)) {
                    char one[2] = { np_char(r, k.cursor_col), '\0' };
                    text_print(npx + k.cursor_col * 2, y, one);
                }
                y++;
            }
            y++;
        }
        menu_row(fx, fw, y++, arow == 0, NET_ROW_W, "Ok");
        menu_row(fx, fw, y++, arow == 1, NET_ROW_W, "Cancel");
        if (err[0]) menu_text(fx, fw, y, 0, err);
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
}

/*----------------------
 | CK_FACE / CK_CHORD / CK_FIXED / CtlRow
 | Description: One row of a configuration sheet: an editable face-button or
 |   shift-chord binding, or a fixed one the device itself decides. `idx` is the
 |   FA_ or CA_ action for the first two and unused for the third, whose value is
 |   the literal in `fixed`.
 | Author: suinevere
 ----------------------*/
enum { CK_FACE, CK_CHORD, CK_FIXED };
struct CtlRow {
    unsigned char kind;
    unsigned char idx;
    const char *label;
    const char *fixed;
};

/*----------------------
 | CS_ACTIONS / CS_SCROLL / CS_MOUSE / CS_N / CS_NAME
 | Description: The three configurable sheets of controls.xls, and their names as
 |   the submenu rows spell them. The workbook's fourth sheet, Static, has no
 |   entry here: nothing on it can change, so it is printed on the root page
 |   rather than given a submenu that could only be read.
 | Author: suinevere
 ----------------------*/
enum { CS_ACTIONS = 0, CS_SCROLL, CS_MOUSE, CS_N };
static const char *const CS_NAME[CS_N] = { "Actions", "Scrolling", "Mouse Mode" };

/*----------------------
 | CSB_ACT / CSB_SCR / CSB_MOUSE / DevCfg / CTL_DEV
 | Description: Which sheets each device configures, and what its row on the
 |   Static sheet says, read off controls.xls. A device with nothing on a sheet
 |   does not get that submenu: the light gun has no Scrolling column and no
 |   selection to step, so it lists Actions alone, and the mouse has no Mouse Mode
 |   because it is always a cursor -- that sheet's "N/A (no mouse on/off)".
 | Author: suinevere
 ----------------------*/
#define CSB_ACT   1
#define CSB_SCR   2
#define CSB_MOUSE 4
struct DevCfg { unsigned char sheets; const char *menu; };
static const DevCfg CTL_DEV[DEV_KIND_N] = {
    { 0,                         ""            },   /* DEV_NONE   */
    { CSB_ACT|CSB_SCR|CSB_MOUSE, "Start"       },   /* DEV_PAD    */
    { CSB_ACT|CSB_SCR|CSB_MOUSE, "Start"       },   /* DEV_FLIGHT */
    { CSB_ACT|CSB_SCR|CSB_MOUSE, "Start"       },   /* DEV_ANALOG */
    { CSB_ACT|CSB_SCR,           "Blue button" },   /* DEV_MOUSE  */
    { CSB_ACT|CSB_MOUSE,         "Start"       },   /* DEV_TWIN   */
    { CSB_ACT,                   "Button"      },   /* DEV_GUN    */
    { CSB_ACT,                   "ESC"         },   /* DEV_KBD    */
};

/*----------------------
 | CTL_SHEET_MAX
 | Description: The most rows any one sheet lists, which is the pad's Actions
 |   sheet in the Keyboard interface: four face buttons, three chords and the
 |   fixed Map row.
 | Author: suinevere
 ----------------------*/
#define CTL_SHEET_MAX 8

/*----------------------
 | INAMES / IDESC
 | Description: The Interface row's value names and description lines, indexed by
 |   IFACE_KEYBOARD/IFACE_PANEL.
 | Author: suinevere
 ----------------------*/
static const char *const INAMES[] = { "Keyboard", "Command Panel" };
static const char *const IDESC[]  = { "Type words, autocomplete",
                                      "Pick words with the pad" };

/*----------------------
 | ctl_dev_editable
 | Description: Whether a device's bindings are the remappable face/chord mapping
 |   rather than the device's own fixed buttons. Only the three pad-family kinds
 |   are: a mouse click is a mouse click and a trigger is a trigger, and there is
 |   no second mapping table for those to be pointed at.
 | Author: suinevere
 | Dependencies: controller.h
 | Globals: N/A
 | Params: k -- the device kind
 | Returns: true when the sheets for `k` are editable
 ----------------------*/
static bool ctl_dev_editable(DevKind k) {
    return k == DEV_PAD || k == DEV_FLIGHT || k == DEV_ANALOG;
}

/*----------------------
 | ctl_sheet_rows
 | Description: Fills `out` with sheet `sheet`'s rows for device `k`, in display
 |   order, and reports how many. The pad family's Actions and Scrolling sheets
 |   are the live face/chord mapping, filtered by interface exactly as the old
 |   two-view page filtered it -- the Command Panel has no Space, no completion
 |   list and no shifted cursor, so those three rows are absent from it. Every
 |   other device's rows come straight off controls.xls and are fixed.
 | Author: suinevere
 | Dependencies: input.h, controller.h, app_state.h (g_cmd_iface)
 | Globals: g_cmd_iface
 | Params: k -- the device; sheet -- one of CS_*; out -- at least CTL_SHEET_MAX rows
 | Returns: how many rows were written
 ----------------------*/
static int ctl_sheet_rows(DevKind k, int sheet, CtlRow *out) {
    int n = 0;
    if (ctl_dev_editable(k)) {
        bool panel = (g_cmd_iface == IFACE_PANEL);
        if (sheet == CS_ACTIONS) {
            out[n++] = CtlRow{ CK_FACE, FA_ACCEPT, "Accept", 0 };
            out[n++] = CtlRow{ CK_FACE, FA_BACK, "Backspace/Cancel", 0 };
            out[n++] = CtlRow{ CK_FACE, FA_TYPE, panel ? "Type Word" : "Type Letter", 0 };
            if (!panel) out[n++] = CtlRow{ CK_FACE, FA_SPACE, "Space", 0 };
            if (!panel) out[n++] = CtlRow{ CK_CHORD, CA_AUTO, "Autocomplete", 0 };
            out[n++] = CtlRow{ CK_CHORD, CA_RECALL, "Recall", 0 };
            if (!panel) out[n++] = CtlRow{ CK_CHORD, CA_CURSOR, "Cursor Move", 0 };
            out[n++] = CtlRow{ CK_FIXED, 0, "Map", "Command module" };
        } else if (sheet == CS_SCROLL) {
            out[n++] = CtlRow{ CK_CHORD, CA_LINE, "Line Up/Down", 0 };
            out[n++] = CtlRow{ CK_CHORD, CA_PAGE, "Page Up/Down", 0 };
            out[n++] = CtlRow{ CK_CHORD, CA_HOMEEND, "Home/End", 0 };
        } else {
            const char *sel = (k == DEV_FLIGHT) ? "Right Stick" : "D-pad";
            const char *cur = (k == DEV_FLIGHT) ? "Right Stick"
                            : (k == DEV_ANALOG) ? "Analogue Stick" : "D-pad";
            out[n++] = CtlRow{ CK_FIXED, 0, "Move Selection", sel };
            out[n++] = CtlRow{ CK_FIXED, 0, "Cursor Move", cur };
        }
        return n;
    }
    if (k == DEV_MOUSE) {
        if (sheet == CS_ACTIONS) {
            out[n++] = CtlRow{ CK_FIXED, 0, "Letter", "Click" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Backspace/Cancel", "Middle Click" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Space", "Click" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Accept", "Right Click" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Map", "Click Map" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Recall", "Click prompt" };
        } else if (sheet == CS_SCROLL) {
            out[n++] = CtlRow{ CK_FIXED, 0, "Line Up/Down", "Click arrows" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Page Up/Down", "R-Click arrows" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Top/Bottom", "M-Click arrows" };
        }
    } else if (k == DEV_TWIN) {
        if (sheet == CS_ACTIONS) {
            out[n++] = CtlRow{ CK_FIXED, 0, "Letter", "Gun Trigger R" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Backspace/Cancel", "Gun Trigger L" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Space", "Top Trigger R" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Accept", "Top Trigger L" };
        } else if (sheet == CS_MOUSE) {
            out[n++] = CtlRow{ CK_FIXED, 0, "Move Selection", "Left Stick" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Cursor Move", "Left Stick" };
        }
    } else if (k == DEV_GUN) {
        if (sheet == CS_ACTIONS) {
            out[n++] = CtlRow{ CK_FIXED, 0, "Letter", "Shoot" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Backspace/Cancel", "Shoot" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Space", "Shoot" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Accept", "Shoot off screen" };
            out[n++] = CtlRow{ CK_FIXED, 0, "Move", "Shoot edge" };
        }
    } else if (k == DEV_KBD) {
        if (sheet == CS_ACTIONS) out[n++] = CtlRow{ CK_FIXED, 0, "Map", "F8" };
    }
    return n;
}

/*----------------------
 | ctl_row_value
 | Description: The value column for one sheet row: the live button or slot name
 |   for an editable row, the row's own literal for a fixed one.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_chord_slot
 | Params: r -- the row
 | Returns: the string to print
 ----------------------*/
static const char *ctl_row_value(const CtlRow &r) {
    if (r.kind == CK_FACE)  return face_btn_name(r.idx);
    if (r.kind == CK_CHORD) return slot_name(g_chord_slot[r.idx]);
    return r.fixed;
}

/*----------------------
 | ctl_dev_list
 | Description: Fills `out` with every device kind currently attached, which is
 |   what the Device row pages over -- a page for a controller nobody has plugged
 |   in is a page that can only mislead. Falls back to the control pad when
 |   nothing at all reports, so the row always has something to name.
 | Author: suinevere
 | Dependencies: controller.h
 | Globals: N/A
 | Params: out -- at least DEV_KIND_N entries
 | Returns: how many were written, always at least 1
 ----------------------*/
static int ctl_dev_list(DevKind *out) {
    int n = 0;
    for (int k = DEV_PAD; k < DEV_KIND_N; k++)
        if (controller_present((DevKind) k)) out[n++] = (DevKind) k;
    if (n == 0) out[n++] = DEV_PAD;
    return n;
}

/*----------------------
 | controls_sheet_page
 | Description: One configuration sheet for one device, opened from the root
 |   Controls page. Editable rows cycle with Left/Right through face_assign or
 |   chord_assign, which swap ties only inside the same sheet -- that is what
 |   makes a sheet a configuration group. Fixed rows are listed and skipped over,
 |   because a device that decides its own bindings still has to say what they
 |   are. Leaves on B/Backspace, Start/Esc, or the Back row.
 | Author: suinevere
 | Dependencies: input.c (face_assign/chord_assign/face_btn_name/slot_name),
 |   menu.c, menu_layout.c (MENU_DIGIT_COLS), saturn_keyboard.h
 | Globals: g_face_btn, g_chord_slot, g_kbd_visible
 | Params: dev -- the device being configured; sheet -- one of CS_*
 | Returns: N/A
 ----------------------*/
static void controls_sheet_page(DevKind dev, int sheet) {
    MenuBacking backing;
    const int CTL_ROW_W   = 36;
    const int CTL_LABEL_W = 18;
    menu_sync();
    bool need_fade_in = true;
    int sel = 0;
    for (;;) {
        CtlRow rows[CTL_SHEET_MAX];
        int nrows = ctl_sheet_rows(dev, sheet, rows);
        int r_back = nrows;
        if (sel < 0 || sel > r_back) sel = 0;
        SaturnKeyEvent ke = saturn_keyboard_poll();
        pad_repeat_update();
        bool up    = g_pad->WasPressed(Button::Up)    || ke.kind == SATURN_KEY_UP;
        bool down  = g_pad->WasPressed(Button::Down)  || ke.kind == SATURN_KEY_DOWN;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                   || ke.kind == SATURN_KEY_ENTER;
        bool back  = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool done  = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (back || done) break;
        if (up)   sel = (sel - 1 + r_back + 1) % (r_back + 1);
        if (down) sel = (sel + 1) % (r_back + 1);
        if (sel == r_back) { if (act) break; }
        else if (left || right) {
            const CtlRow &r = rows[sel];
            if (r.kind == CK_FACE) {
                int n = right ? (g_face_btn[r.idx] + 1) % FA_BTN_N
                              : (g_face_btn[r.idx] + FA_BTN_N - 1) % FA_BTN_N;
                face_assign(r.idx, n);
            } else if (r.kind == CK_CHORD) {
                int n = right ? (g_chord_slot[r.idx] + 1) % SL_N
                              : (g_chord_slot[r.idx] + SL_N - 1) % SL_N;
                chord_assign(r.idx, n);
            }
        }

        nrows = ctl_sheet_rows(dev, sheet, rows);
        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit(CS_NAME[sheet], CTL_ROW_W, CTL_SHEET_MAX + 5, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, CS_NAME[sheet]);
        int y = fy + 3;
        menu_text(fx, fw, y++, 0, controller_kind_name(dev));
        y++;
        for (int i = 0; i < nrows; i++)
            menu_rowf(fx, fw, y++, sel == i, CTL_ROW_W, "   %s%s",
                      menu_pad(rows[i].label, CTL_LABEL_W), ctl_row_value(rows[i]));
        y = fy + 4 + CTL_SHEET_MAX + 1;
        menu_row(fx, fw, y++, sel == r_back, CTL_ROW_W, "   Back");
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    menu_sync();
    mode_toggle_reset();
}

/*----------------------
 | controls_page
 | Description: The Controls root. The Device row pages over every peripheral
 |   currently attached and nothing else, so the page always describes something
 |   the player can actually pick up; the Interface row under it is the persisted
 |   preference a game starts in, which L+R and the command module's Swap row then
 |   change for the session. Under those sits the workbook's Static sheet, printed
 |   rather than offered, and then one submenu row per sheet the current device
 |   configures. Keyboard Caps is here because it stopped being a pad binding: a
 |   modifier the player cannot see the state of is not worth a combo.
 |
 |   Snapshots g_face_btn/g_chord_slot/g_cmd_iface on entry so Cancel (or
 |   B/Backspace) restores them verbatim, including edits made two levels down in
 |   a sheet; Start/Esc save what is on screen exactly as the Ok row does.
 |   Keyboard Caps takes effect immediately and is not part of that snapshot,
 |   matching the toggles on every other page.
 |
 |   Returns true, without saving or restoring, if the active input device's
 |   family (pad vs. real keyboard) changed while this page was open, so
 |   controls_dispatch can hand off to keyboard_controls_page instead of leaving
 |   this page on screen showing the wrong device's controls with the music still
 |   paused; false on a genuine Ok/Cancel/B/Backspace/Start/Esc exit.
 | Author: suinevere
 | Dependencies: input.c (g_face_btn/g_chord_slot/mapping_reset_defaults),
 |   controller.h (controller_present/controller_kind_name), keyboard.c
 |   (keyboard_get_caps/keyboard_set_caps), console_view.c
 |   (note_input_device/g_kbd_visible), menu.c, menu_layout.c, options.c
 |   (options_save), app_state.h (g_cmd_iface, IFACE_PANEL/IFACE_KEYBOARD),
 |   saturn_keyboard.h
 | Globals: g_face_btn, g_chord_slot, g_cmd_iface, g_kbd_visible
 | Params: N/A
 | Returns: true if it exited because the input device family changed
 ----------------------*/
static bool controls_page(void) {
    MenuBacking backing;
    const int CTL_ROW_W   = 36;
    const int CTL_LABEL_W = 18;
    const int CTL_IFACE_W = 13;
    const int CTL_DEV_W   = 13;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    bool started_kbd = g_kbd_visible;
    int s_face[FA_N], s_chord[CA_N];
    int s_iface = g_cmd_iface;
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
    static int last_dev = 0;
    int sel = 0;
    bool switched = false;
    for (;;) {
        DevKind devs[DEV_KIND_N];
        int ndev = ctl_dev_list(devs);
        if (last_dev < 0 || last_dev >= ndev) last_dev = 0;
        DevKind dev = devs[last_dev];
        int sheets[CS_N], nsheet = 0;
        for (int s = 0; s < CS_N; s++)
            if (CTL_DEV[dev].sheets & (1 << s)) sheets[nsheet++] = s;
        const int R_DEV = 0, R_IFACE = 1;
        int r_sheet0 = 2;
        int r_caps   = r_sheet0 + nsheet;
        int r_reset  = r_caps + 1;
        int r_done   = r_caps + 2;
        int r_cancel = r_caps + 3;
        int nrows    = r_caps + 4;
        if (sel < 0 || sel >= nrows) sel = 0;

        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_kbd_visible != started_kbd) { switched = true; break; }
        pad_repeat_update();
        bool up    = g_pad->WasPressed(Button::Up)    || ke.kind == SATURN_KEY_UP;
        bool down  = g_pad->WasPressed(Button::Down)  || ke.kind == SATURN_KEY_DOWN;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                   || ke.kind == SATURN_KEY_ENTER;
        bool back  = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool done  = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (back) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            g_cmd_iface = s_iface;
            break;
        }
        if (done) { options_save(); break; }
        if (up)   sel = (sel - 1 + nrows) % nrows;
        if (down) sel = (sel + 1) % nrows;
        if (sel == r_done) { if (act) { options_save(); break; } }
        else if (sel == r_cancel) { if (act) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            g_cmd_iface = s_iface;
            break; } }
        else if (sel == r_reset) { if (act) mapping_reset_defaults(); }
        else if (sel == r_caps) { if (left || right || act) keyboard_set_caps(!keyboard_get_caps()); }
        else if (sel == R_DEV) {
            if (left)  last_dev = (last_dev - 1 + ndev) % ndev;
            if (right) last_dev = (last_dev + 1) % ndev;
        }
        else if (sel == R_IFACE) {
            if (left  && g_cmd_iface > IFACE_KEYBOARD) g_cmd_iface--;
            if (right && g_cmd_iface < IFACE_PANEL)    g_cmd_iface++;
        }
        else if (act && sel >= r_sheet0 && sel < r_caps) {
            controls_sheet_page(dev, sheets[sel - r_sheet0]);
            need_fade_in = true;
            continue;
        }

        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("CONTROLS", CTL_ROW_W, CS_N + 12, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int y = fy + 3;
        menu_rowf(fx, fw, y++, sel == R_DEV, CTL_ROW_W, "   Device:     %s %s %s",
                  ndev > 1 ? "<" : " ",
                  menu_pad(controller_kind_name(dev), CTL_DEV_W),
                  ndev > 1 ? ">" : " ");
        menu_rowf(fx, fw, y++, sel == R_IFACE, CTL_ROW_W, "   Interface:  %s %s %s",
                  g_cmd_iface > IFACE_KEYBOARD ? "<" : " ",
                  menu_pad(INAMES[g_cmd_iface], CTL_IFACE_W),
                  g_cmd_iface < IFACE_PANEL ? ">" : " ");
        menu_text(fx, fw, y++, 0, IDESC[g_cmd_iface]);
        y++;
        menu_rowf(fx, fw, y++, 0, CTL_ROW_W, "   %s%s",
                  menu_pad("Menu (fixed)", CTL_LABEL_W), CTL_DEV[dev].menu);
        y++;
        for (int i = 0; i < nsheet; i++)
            menu_rowf(fx, fw, y++, sel == r_sheet0 + i, CTL_ROW_W, "   %s...",
                      CS_NAME[sheets[i]]);
        y = fy + 9 + CS_N;
        menu_rowf(fx, fw, y++, sel == r_caps, CTL_ROW_W, "   Keyboard Caps: %s",
                  keyboard_get_caps() ? "On" : "Off");
        y++;
        menu_row(fx, fw, y++, sel == r_reset,  CTL_ROW_W, "   Reset to Defaults");
        menu_row(fx, fw, y++, sel == r_done,   CTL_ROW_W, "   Ok");
        menu_row(fx, fw, y++, sel == r_cancel, CTL_ROW_W, "   Cancel");
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    return switched;
}

/*----------------------
 | keyboard_controls_page
 | Description: Physical-keyboard settings page: 6 rows -- Insert mode, Caps
 |   Lock, Num Lock, Scroll Lock (all two-state toggles, so direction and
 |   activation are treated alike), then Ok and Cancel. Snapshots the
 |   keyboard.c getters on entry so Cancel (B/Backspace, or the Cancel row)
 |   can restore them. Ok commits and calls options_save(), and so do Start/Esc
 |   -- leaving an options page keeps what it shows. Rows are composed whole
 |   and drawn through menu_row, so the label field (KB_LABEL_W) is what holds
 |   the value column in place rather than an absolute screen column, and it
 |   holds in BOTH digit and no-digit modes because menu_num reserves its three
 |   columns either way. Returns true, without saving or
 |   restoring, if the active input device's family changed while this page
 |   was open, so controls_dispatch can hand off to controls_page instead of
 |   leaving this page on screen showing the wrong device's controls with the
 |   music still paused; false on a genuine Ok/Cancel/B/Backspace/Start/Esc exit.
 | Author: suinevere
 | Dependencies: keyboard.c (keyboard_get_insert/keyboard_set_insert/
 |   keyboard_get_caps/keyboard_set_caps/keyboard_get_num/keyboard_set_num/
 |   keyboard_get_scrolllock/keyboard_set_scrolllock),
 |   console_view.c (note_input_device/hint/g_kbd_visible),
 |   input.c (pad_repeat_update), menu.c, options.c (options_save)
 | Globals: N/A
 | Params: N/A
 | Returns: true if it exited because the input device family changed
 |   (caller should redispatch to controls_page); false on a normal exit
 ----------------------*/
bool keyboard_controls_page(void) {
    MenuBacking backing;
    // One bar width and one value column for the page, as on controls_page.
    const int KB_ROW_W   = 21;
    const int KB_LABEL_W = 15;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    bool started_kbd = g_kbd_visible;
    int s_ins = keyboard_get_insert(), s_caps = keyboard_get_caps(),
        s_num = keyboard_get_num(), s_scrl = keyboard_get_scrolllock();
    const int N = 6;
    static int last_sel = 0;   // held across visits; the six rows never change
    int sel = last_sel;
    bool switched = false;
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_kbd_visible != started_kbd) { switched = true; break; }
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + N) % N;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % N;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool done = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (back) {
            keyboard_set_insert(s_ins); keyboard_set_caps(s_caps);
            keyboard_set_num(s_num); keyboard_set_scrolllock(s_scrl);
            break;
        }
        if (done) { options_save(); break; }
        if (menu_digit_row(ke, N, sel, left, right)) act = true;
        last_sel = sel;   // after every move, so no exit path has to remember to
        bool toggle = left || right || act;
        if      (sel == 0 && toggle) keyboard_set_insert(!keyboard_get_insert());
        else if (sel == 1 && toggle) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 2 && toggle) keyboard_set_num(!keyboard_get_num());
        else if (sel == 3 && toggle) keyboard_set_scrolllock(!keyboard_get_scrolllock());
        else if (sel == 4 && act) { options_save(); break; }
        else if (sel == 5 && act) {
            keyboard_set_insert(s_ins); keyboard_set_caps(s_caps);
            keyboard_set_num(s_num); keyboard_set_scrolllock(s_scrl); break; }

        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("CONTROLS", 34, 12, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int y = fy + 4;
        menu_text(fx, fw, y++, 0, "Insert: type-insert, caret arrows.");
        menu_text(fx, fw, y++, 0, "ScrLk: Up/Dn scroll, Ctrl=history.");
        y++;
        bool nums = !g_kbd_visible;
        menu_rowf(fx, fw, y++, sel == 0, KB_ROW_W, "%s%s%s", menu_num(nums, 0),
                  menu_pad("Insert mode", KB_LABEL_W), keyboard_get_insert() ? "On" : "Off");
        menu_rowf(fx, fw, y++, sel == 1, KB_ROW_W, "%s%s%s", menu_num(nums, 1),
                  menu_pad("Caps Lock", KB_LABEL_W), keyboard_get_caps() ? "On" : "Off");
        menu_rowf(fx, fw, y++, sel == 2, KB_ROW_W, "%s%s%s", menu_num(nums, 2),
                  menu_pad("Num Lock", KB_LABEL_W), keyboard_get_num() ? "On" : "Off");
        menu_rowf(fx, fw, y++, sel == 3, KB_ROW_W, "%s%s%s", menu_num(nums, 3),
                  menu_pad("Scroll Lock", KB_LABEL_W), keyboard_get_scrolllock() ? "On" : "Off");
        y++;
        menu_rowf(fx, fw, y++, sel == 4, KB_ROW_W, "%sOk", menu_num(nums, 4));
        menu_rowf(fx, fw, y++, sel == 5, KB_ROW_W, "%sCancel", menu_num(nums, 5));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    return switched;
}

/*----------------------
 | sound_options_page
 | Description: Sound Options (full-screen, Ok/Cancel). A master switch and two
 |   levels: the Audio Mix row and the Track row it fed are gone with the mix
 |   modes themselves, so the music is the dynamic engine or, at CD Music 0,
 |   silence. Music On/Off is both levels at once -- Off puts each to 0 and On
 |   restores the shipped pair -- and its value is READ BACK from them rather
 |   than stored, so raising either level below turns it On without the two
 |   having to be kept in step, and nothing new goes into the save blob. The two
 |   level rows underneath it stay usable while it is Off; moving one is how a
 |   player turns just that half back on.
 |   Which rows appear depends on what is available: CD Music needs CD-DA on
 |   the disc (has_cd, from music_cdda_audio_tracks() > 0); PCM needs the loaded
 |   game's .BLB (has_blb, from sound_has_audio()); Music On/Off needs either,
 |   since with neither it would switch two levels nothing reads; Ok/Cancel
 |   always show.
 |   `sel` indexes the resulting visible-row list, not a fixed row number.
 |   Snapshots g_music_level/g_pcm_level for Cancel, which is what restores a
 |   mistaken Off.
 |   Opening this page resumes the music if the in-game Options menu paused it:
 |   every row here is judged by ear, so it is the one page that cannot work in
 |   silence. Leaving it puts that pause back (`was_paused`, restored once after
 |   the loop rather than on each of the two exits), because the root Options menu
 |   it returns to is silent for as long as it is up -- saturn_glue.cxx:266 owns
 |   the resume that ends it. Without the restore, visiting Sound and backing out
 |   left the rest of the in-game Options menu playing. The flag is read rather
 |   than assumed: from the main menu nothing paused the music and nothing may.
 |   Neither row can interrupt what is streaming -- a level is a volume write, not
 |   a new track -- so no exit path re-asserts playback any more; opening and
 |   closing this page in-game leaves the current track running. Cancel (or
 |   B/Backspace) restores the snapshot, live audio included, via
 |   music_set_level/sound_set_level; Start/Esc take the Ok path instead,
 |   committing and saving what the rows currently show. The value column is
 |   fixed at x + 14 + MENU_DIGIT_COLS in both digit and no-digit modes, reserved
 |   unconditionally so it does not move when the player switches device. The box
 |   keeps the 34-column width it was given for "< Sequential >": the widest value
 |   left is a single digit, but the other Options pages are 34 too and a SOUND
 |   box that shrank away from them would be the more conspicuous change.
 | Author: suinevere
 | Dependencies: music.c (music_cdda_audio_tracks/music_resume/music_duck/
 |   music_set_volume/music_set_level/music_is_paused), sound.c (sound_has_audio/
 |   sound_set_level), console_view.c
 |   (note_input_device/hint/g_kbd_visible), input.c (pad_repeat_update),
 |   menu.c, menu_layout.c (MENU_DIGIT_COLS), options.c (options_save),
 |   soft_reset.h (check_soft_reset)
 | Globals: g_music_level, g_pcm_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_options_page(void) {
    MenuBacking backing;
    const int SND_ROW_W   = 31;
    const int SND_LABEL_W = 14;
    enum { SR_SOUND, SR_MUSIC, SR_PCM, SR_OK, SR_CANCEL };
    bool has_cd  = (music_cdda_audio_tracks(0) > 0);
    bool has_blb = (sound_has_audio() != 0);

    int rows[5], nrows = 0;
    // The master switch is listed whenever there is anything for it to switch.
    // On a disc with neither CD audio nor a sound blob it would sit there
    // toggling two levels nothing reads.
    if (has_cd || has_blb) rows[nrows++] = SR_SOUND;
    if (has_cd)  rows[nrows++] = SR_MUSIC;
    if (has_blb) rows[nrows++] = SR_PCM;
    rows[nrows++] = SR_OK;
    rows[nrows++] = SR_CANCEL;

    // Remembered as a row ID, not an index: Music is only listed when there is
    // CD audio and PCM only when there is a sound blob, so the same index names
    // a different row on a different disc.
    static int last_row = SR_SOUND;
    int sel = 0;
    for (int i = 0; i < nrows; i++) if (rows[i] == last_row) { sel = i; break; }
    int s_mus = g_music_level, s_pcm = g_pcm_level;
    // Reached from the in-game Options menu, the music is ducked; this page is the
    // one place that cannot work under a duck, since every row on it is judged by
    // ear and has to be heard at the level being set. A no-op everywhere else.
    // Remembered rather than assumed, because the way back out has to put the drive
    // in the state this page found it: the in-game Options menu underneath is ducked
    // and stays that way until saturn_glue's read loop resumes it, while the main
    // menu's music was never held and must not be. See the restore after the loop.
    const int was_paused = music_is_paused();
    music_resume();

    int fx, fy, fw, fh;
    menu_box_fit("SOUND", 34, nrows + 3, &fx, &fy, &fw, &fh);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool commit = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;
        int row = rows[sel];
        last_row = row;   // every frame, so no exit path has to remember to

        if (cancel || (ok && row == SR_CANCEL)) {
            g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            break;
        }
        if (commit || (ok && row == SR_OK)) {
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            options_save();
            break;
        }
        if (row == SR_SOUND && (left || right || ok)) {
            // One switch over both levels, and derived from them rather than
            // held beside them: a level row moved off 0 turns this back On by
            // itself, and there is no second piece of state to keep in step or
            // to find room for in the save blob. Off is 0, which is what each
            // level's own bottom stop already means; On is the shipped pair,
            // since the levels the player had are exactly what Off overwrote.
            bool on = (g_music_level > 0 || g_pcm_level > 0);
            g_music_level = on ? 0 : MUSIC_LEVEL_DEFAULT;
            g_pcm_level   = on ? 0 : PCM_LEVEL_DEFAULT;
            music_set_volume(g_music_level);
            sound_set_level(g_pcm_level);
        }
        else if (row == SR_MUSIC) { if (left && g_music_level > 0) g_music_level--; if (right && g_music_level < 7) g_music_level++;
                               if (left || right) music_set_volume(g_music_level); }
        else if (row == SR_PCM) { if (left && g_pcm_level > 0) g_pcm_level--; if (right && g_pcm_level < 7) g_pcm_level++;
                                  if (left || right) sound_set_level(g_pcm_level); }

        menu_clear();
        menu_frame(fx, fy, fw, fh, "SOUND");
        int y = fy + 4;
        bool nums = !g_kbd_visible;
        for (int i = 0; i < nrows; i++) {
            const char *n = menu_num(nums, i);
            switch (rows[i]) {
                case SR_SOUND:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%s%s%s", n,
                              menu_pad("Music", SND_LABEL_W),
                              (g_music_level > 0 || g_pcm_level > 0) ? "On" : "Off");
                    break;
                case SR_MUSIC:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%s%s%d", n,
                              menu_pad("CD Music", SND_LABEL_W), g_music_level);
                    break;
                case SR_PCM:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%s%s%d", n,
                              menu_pad("PCM", SND_LABEL_W), g_pcm_level);
                    break;
                case SR_OK:
                    y++;
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%sOk", n);
                    break;
                case SR_CANCEL:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%sCancel", n);
                    break;
            }
        }
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    // Both exits above are a plain break, so the entry pause goes back here once
    // rather than on each of them -- the "remember to undo this on every exit path"
    // shape that menu.h's MenuBacking box was written about. Before the fade-out,
    // not after: the root Options menu this is leaving to is ducked, so the fade
    // should be too. Neither exit restarts anything now that both rows are
    // levels, so this holds whatever was already streaming. A duck, matching what the entry
    // above lifted -- and with no seek involved it cannot land the drive anywhere
    // else, which is the failure the old stop-and-restore had to document.
    if (was_paused) music_duck();
    page_fade_out(g_menu_page_fade);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
}

/*----------------------
 | display_options_page
 | Description: Display Options (full-screen, Ok/Cancel). The row list is rebuilt
 |   every frame because Dimming appears only under the Dynamic palette -- it
 |   offsets the wallpaper picture, and a colour preset has none -- and the
 |   Palette row that decides that sits directly above it, so the list can change
 |   under the cursor. The frame is sized for the full list either way, and `sel`
 |   is clamped after each rebuild.
 |   Left/Right applies each cycler row live (via display_cycle_row) so the
 |   result is visible behind the menu immediately; Cancel (or B/Backspace)
 |   restores the g_display snapshot taken on entry and re-applies it with
 |   display_apply(); Ok and Start/Esc persist it with options_save().
 |     This page used to pin the Dynamic palette to the picture already on NBG0
 |   for its own duration, so that cycling onto Dynamic could not send the
 |   category art system off to resolve a different mood picture, read the disc
 |   and stop the CD-DA head mid-track. There is no category art system now:
 |   Dynamic resolves to the room's own picture, which room_art holds resident
 |   in Low Work RAM, so cycling onto it costs a blit and never a disc read.
 |   The pin had already stopped doing anything on an authored game -- it
 |   resolved a disc image name and title_bg_loaded_file returns an area stem --
 |   and it is gone.
 |     Uses the full 40
 |   columns rather than the 38 the other pages use. Values print at x + 17,
 |   leaving 20 columns before the border, so "< %s >" fits a name of at most
 |   16 characters. Two sources feed these rows and both must stay under
 |   that: PRESETS in display.c (widest "Amstrad CPC 464", 15 chars) and the
 |   disc image names, capped at GFS_FNAME_LEN = 12 by ISO9660 8.3 -- a
 |   one-column margin that a longer preset name, or image names no longer
 |   bounded by 8.3, would need the value column moved for, not just a wider
 |   box (at 38 the Palette row already lands on the border). This is the
 |   one page where the value column CANNOT take the usual MENU_DIGIT_COLS
 |   shift: at x + 20 the widest value ("< Amstrad CPC 464 >", 19 chars)
 |   would run to column 40 and overwrite the border, and the box is already
 |   the full screen width so it cannot grow. The 3 columns instead come out
 |   of the LABEL side -- "System Palette" was shortened to "Palette" so the
 |   longest numbered label ("N) Background", ending at column 17) still
 |   clears the value column at 19.
 | Author: suinevere
 | Dependencies: display.c (DisplayState/display_palette_name/
 |   display_bg_name/display_text_name), options.c
 |   (display_apply/display_cycle_row/DCR_* / options_save), console_view.c
 |   (note_input_device/hint/g_kbd_visible), input.c (pad_repeat_update),
 |   menu.c, menu_layout.c (MENU_DIGIT_COLS), soft_reset.h
 |   (check_soft_reset)
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void display_options_page(void) {
    MenuBacking backing;
    // One bar width, one label field, and a value field as wide as the longest
    // preset name ("Amstrad CPC 464"), so the arrows hold still while a row
    // cycles. 3 + 14 + 2 + 15 + 2 is the 36 the box is sized to.
    const int DSP_ROW_W   = 36;
    const int DSP_LABEL_W = 14;
    const int DSP_VALUE_W = 15;
    enum { DR_PALETTE, DR_BG, DR_TEXT, DR_DIM, DR_OK, DR_CANCEL };
    int rows[6];
    int nrows = 0;

    // Remembered as a row ID, not an index: the Dimming row comes and goes with
    // the Palette row above it, so the same index names a different row
    // depending on the palette the page was left under. Resolved on the first
    // pass rather than here, because rows[] is not built until inside the loop.
    static int last_row = DR_PALETTE;
    int resume = last_row;
    int sel = 0;
    if (g_display.palette == DISP_PAL_DYNAMIC) g_display.image = display_dynamic_slot();
    DisplayState snapshot = g_display;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        // Rebuilt every frame because the Palette row above can remove a row
        // below it mid-page. Dimming only appears under Dynamic: it offsets the
        // wallpaper picture, and a colour preset carries none -- the row would
        // sit there moving a number with nothing on screen to answer it. The
        // stored value is left alone while hidden, so switching back to Dynamic
        // brings the player's setting back rather than a reset one.
        nrows = 0;
        rows[nrows++] = DR_PALETTE;
        rows[nrows++] = DR_BG;
        rows[nrows++] = DR_TEXT;
        if (g_display.palette == DISP_PAL_DYNAMIC) rows[nrows++] = DR_DIM;
        rows[nrows++] = DR_OK;
        rows[nrows++] = DR_CANCEL;
        if (resume >= 0) {
            for (int i = 0; i < nrows; i++) if (rows[i] == resume) { sel = i; break; }
            resume = -1;
        }
        if (sel >= nrows) sel = nrows - 1;

        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool commit = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;
        int row = rows[sel];
        last_row = row;   // every frame, so no exit path has to remember to

        if (cancel || (ok && row == DR_CANCEL)) {
            g_display = snapshot;
            display_apply();
            break;
        }
        if (commit) { options_save(); break; }
        int dir = right ? 1 : (left ? -1 : 0);
        if (dir != 0) {
            if      (row == DR_PALETTE) display_cycle_row(DCR_PALETTE, dir);
            else if (row == DR_BG)      display_cycle_row(DCR_BG,      dir);
            else if (row == DR_TEXT)    display_cycle_row(DCR_TEXT,    dir);
            else if (row == DR_DIM)     display_cycle_row(DCR_DIM,     dir);
        }
        if (ok && row == DR_OK) { options_save(); break; }

        menu_clear();
        int fx, fy, fw, fh;
        // Sized for the full row list, not the current one: the Dimming row
        // comes and goes with the Palette row directly above it, and a frame
        // that resized under the cursor while cycling Palette would read as the
        // page redrawing itself rather than as one row appearing.
        menu_box_fit("DISPLAY", DSP_ROW_W, (int)(sizeof(rows) / sizeof(rows[0])) + 3,
                     &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "DISPLAY");
        int y = fy + 4;
        bool nums = !g_kbd_visible;
        for (int i = 0; i < nrows; i++) {
            const char *n = menu_num(nums, i);
            switch (rows[i]) {
                case DR_PALETTE:
                    menu_rowf(fx, fw, y++, i == sel, DSP_ROW_W, "%s%s< %s >", n,
                              menu_pad("Palette", DSP_LABEL_W),
                              menu_pad(display_palette_name(&g_display), DSP_VALUE_W));
                    break;
                case DR_BG:
                    menu_rowf(fx, fw, y++, i == sel, DSP_ROW_W, "%s%s< %s >", n,
                              menu_pad("Background", DSP_LABEL_W),
                              menu_pad(display_bg_name(&g_display), DSP_VALUE_W));
                    break;
                case DR_TEXT:
                    menu_rowf(fx, fw, y++, i == sel, DSP_ROW_W, "%s%s< %s >", n,
                              menu_pad("Text", DSP_LABEL_W),
                              menu_pad(display_text_name(g_display.text), DSP_VALUE_W));
                    break;
                case DR_DIM:
                    menu_rowf(fx, fw, y++, i == sel, DSP_ROW_W, "%s%s< %s >", n,
                              menu_pad("Dimming", DSP_LABEL_W),
                              menu_pad(display_dim_name(g_display.dim), DSP_VALUE_W));
                    break;
                case DR_OK:
                    y++;
                    menu_rowf(fx, fw, y++, i == sel, DSP_ROW_W, "%sOk", n);
                    break;
                case DR_CANCEL:
                    menu_rowf(fx, fw, y++, i == sel, DSP_ROW_W, "%sCancel", n);
                    break;
            }
        }
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
}

/*----------------------
 | CREDITS_P1 .. CREDITS_P7 / CreditsPage / CREDITS_PAGES
 | Description: Static credit text, pre-wrapped to the 40-column screen and
 |   pre-split into pages so a section heading (ASSETS, INFOCOM STAFF, OTHER
 |   STAFF, PORT CREDITS) always starts a fresh page rather than falling as
 |   the last line on one -- a plain line-count split would let that happen.
 |   Mirrors the "Credits & license" section of the top-level README; keep the
 |   two in sync by hand, since the README additionally carries URLs that a
 |   text-mode page cannot show. CreditsPage pairs one page's line array with
 |   its length (taken via sizeof, not hand-counted, so an edited page cannot
 |   silently desync from CREDITS_PAGES); CREDITS_PAGES is the ordered list
 |   credits_page() walks.
 | Author: suinevere
 ----------------------*/
static const char *const CREDITS_P1[] = {
    "ASSETS",
    "",
    "Andrew Plotkin",
    "  Obsessively Complete Infocom",
    "  Catalog for Z3",
    "Icculus (Ryan C. Gordon)",
    "  Original MojoZork base to port",
    "ReyeMe",
    "  SaturnRingLib C++ SGL wrapper",
    "eaudunord",
    "  Netlink tunnel dialer",
    "Background Artwork",
    "  Guarav D Lathiya",
    "  Kevin et Laurianne Langlais",
    "  Wolfgang Hassel",
    "  Arnie Chou",
    "  Sergej Kaldesic",
    "  Markus Kroger",
};
static const char *const CREDITS_P2[] = {
    "ASSETS (cont.)",
    "",
    "Background Artwork (cont.)",
    "  Zoltan Tasi",
    "  Guu Baggerman",
    "  Laura Brain",
    "  Gabriel Kraus",
    "  Alex Knight",
    "  Kevin Kandlbinde",
    "  Alex Kalinowski",
    "  Brian McGowan",
    "  M.J. Tangonan",
    "  Cristian Palmer",
    "  Oliver Roos",
    "  Ricardo Gomez",
    "  Stephen Tafra",
    "  Peter Herman",
    "  Nicolas Hoizey",
    "  Kino",
    "  Chris Boyer",
    "  Yan Agrit",
    "  Ed Stone",
    "  A.J. Wallace",
    "  Mikel Ibarluzea",
    "  Johnny Briggs",
    "  Nils Leonhardt",
    "Kevin Bracey",
    "  Lurking Horror sound file",
    "archive.org",
    "  Asset backup hosting",
    "Jeff Nyman",
    "  Colossal Cave Adventure (Z3 port)",
    "Geno Andrews (Presto Studios)",
    "  \"Caldoria Theme\", from",
    "  The Journeyman Project Turbo",
    "Yuzo Koshiro / Motohiro Kawashima",
    "  Zork CD-DA music",
};
static const char *const CREDITS_P3[] = {
    "INFOCOM STAFF",
    "",
    "Marc Blank",
    "  Zork I-III, Deadline, Enchanter",
    "Dave Lebling",
    "  Zork I-III, Starcross,",
    "  Enchanter, Suspect,",
    "  The Lurking Horror",
    "Tim Anderson",
    "  Zork I-III (co-author)",
    "Bruce Daniels",
    "  Zork I-III (co-author)",
    "Steve Meretzky",
    "  Planetfall, Sorcerer,",
    "  Hitchhiker's Guide (co-author),",
    "  Leather Goddesses, Stationfall",
};
static const char *const CREDITS_P4[] = {
    "INFOCOM STAFF (cont.)",
    "",
    "Stu Galley",
    "  The Witness, Seastalker,",
    "  Moonmist (co-author)",
    "Michael Berlyn",
    "  Suspended, Infidel,",
    "  Cutthroats (co-author)",
    "Douglas Adams",
    "  Hitchhiker's Guide (co-author)",
    "Brian Moriarty",
    "  Wishbringer",
    "Jeff O'Neill",
    "  Ballyhoo",
    "Amy Briggs",
    "  Plundered Hearts",
};
/*----------------------
 | CREDITS_P5
 | Description: Credits page 5.
 | Author: suinevere
 ----------------------*/
static const char *const CREDITS_P5[] = {
    "INFOCOM STAFF (cont.)",
    "",
    "Dave Anderson",
    "  Hollywood Hijinx",
    "  (as \"Invisible Green\")",
    "Jim Lawrence",
    "  Seastalker, Moonmist (co-author)",
    "Patricia Furusho",
    "  Infidel (co-author)",
    "Jerry Wolper",
    "  Cutthroats (co-author)",
};
/*----------------------
 | CREDITS_P6
 | Description: Credits page 6.
 | Author: suinevere
 ----------------------*/
static const char *const CREDITS_P6[] = {
    "OTHER STAFF",
    "",
    "Joel Berez",
    "  Co-designed Z-Machine concept",
    "  and architecture",
    "Marc Blank",
    "  Designed ZIL, co-created",
    "  Z-Machine specs",
    "Stu Galley (S.W. Galley)",
    "  Maintained the ZIP interpreter",
    "  for Z3 games",
    "Al Vezza",
    "  Led the team that founded",
    "  Infocom",
};
/*----------------------
 | CREDITS_P7
 | Description: Credits page 7.
 | Author: suinevere
 ----------------------*/
static const char *const CREDITS_P7[] = {
    "PORT CREDITS",
    "",
    "MojoZork & multizorkd",
    "  Ryan C. \"Icculus\" Gordon",
    "  zlib license",
    "SaturnRingLib",
    "  ReyeMe et al.",
    "DreamPi / Netlink tunnel",
    "  eaudunord, derived from",
    "  Kazade's DreamPi work",
    "Zork I/II/III data files",
    "  Distributed free by Activision",
    "Saturn port and tooling",
    "  Suinevere",
};
/*----------------------
 | CreditsPage / CREDITS_PAGE / CREDITS_PAGES
 | Description: One credits page and its line count, the macro that pairs an array
 |   with its length, and the ordered table of pages.
 | Author: suinevere
 ----------------------*/
struct CreditsPage { const char *const *lines; int n; };
#define CREDITS_PAGE(a) { a, (int)(sizeof(a) / sizeof(a[0])) }
static const CreditsPage CREDITS_PAGES[] = {
    CREDITS_PAGE(CREDITS_P1), CREDITS_PAGE(CREDITS_P2), CREDITS_PAGE(CREDITS_P3),
    CREDITS_PAGE(CREDITS_P4), CREDITS_PAGE(CREDITS_P5), CREDITS_PAGE(CREDITS_P6),
    CREDITS_PAGE(CREDITS_P7),
};
#undef CREDITS_PAGE

/*----------------------
 | credits_page
 | Description: Read-only, paginated Credits page reached from the title
 |   mode-select menu's Credits row (main.cxx), not from Options. Left/Right
 |   step between CREDITS_PAGES entries (clamped, not wrapping, so the
 |   page-count indicator stays a reliable "how much is left" cue); any of
 |   B/A/C/Start/Enter/Esc/Backspace closes it, since there is nothing here
 |   to confirm or cancel -- unlike the editable pages, every exit is the
 |   same exit. Draws the full 40-column width so the credit text need not
 |   compete with a value column. Fades in/out via the same g_menu_page_fade
 |   gate every other menu-phase page uses (nonzero for the whole mode-select
 |   loop in main.cxx, so this page fades like its siblings there).
 | Author: suinevere
 | Dependencies: console_view.c (note_input_device/hint/g_kbd_visible),
 |   input.c (pad_repeat_update), menu.c, soft_reset.h (check_soft_reset)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void credits_page(void) {
    MenuBacking backing;
    const int fx = 0, fy = 2, fw = 40, fh = 24;
    const int npages = (int)(sizeof(CREDITS_PAGES) / sizeof(CREDITS_PAGES[0]));
    int page = 0;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool back  = g_pad->WasPressed(Button::B) || g_pad->WasPressed(Button::A) ||
                     g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
                     ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE ||
                     ke.kind == SATURN_KEY_ENTER;
        if (left  && page > 0)          page--;
        if (right && page < npages - 1) page++;
        if (back) break;

        menu_clear();
        menu_frame(fx, fy, fw, fh, "CREDITS");
        int x = fx + 2, y = fy + 3;
        const CreditsPage &cp = CREDITS_PAGES[page];
        for (int i = 0; i < cp.n; i++) text_print(x, y++, "%s", cp.lines[i]);
        text_print(x, y + 1, "%s Page %d/%d %s",
                           page > 0 ? "<" : " ", page + 1, npages, page < npages - 1 ? ">" : " ");
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
}

/*----------------------
 | gameplay_page
 | Description: Gameplay Options (full-screen box, Ok/Cancel): the Difficulty
 |   slider (Easy/Medium/Hard) and the Room text slider (Superbrief/Brief/
 |   Verbose), each with a description line, moved out of the top-level Options
 |   box so that list can stay plain dispatch rows. Difficulty governs two
 |   unrelated things -- the typeahead and how much of the map there is -- and
 |   its description line names both, since nothing else on screen says the Map
 |   row disappears on Hard. Left/Right adjust local copies; Ok and Start/Esc
 |   commit them to g_difficulty/g_verbosity and call options_save() only if
 |   something actually changed; Cancel (or B/Backspace) discards the copies,
 |   leaving every global untouched.
 |
 |   The Interface slider used to sit under these two and now heads the Controls
 |   page instead, where the rows below it are the bindings for whichever
 |   interface it names -- the setting and the configuration it governs read
 |   better together than apart. It kept its wording and its behaviour verbatim
 |   in the move; only its home changed.
 |
 |   Room text is applied by whoever is running the interpreter, not here: the
 |   parser owns that state and only takes it as a typed command, so this page
 |   just records the choice and saturn_glue.cxx hands it over on the way out.
 |   Reached only from the Options menu's Gameplay row.
 | Author: suinevere
 | Dependencies: options.c (options_save), console_view.c (note_input_device/
 |   hint/g_kbd_visible), input.c (pad_repeat_update), menu.c, soft_reset.h
 |   (check_soft_reset)
 | Globals: g_difficulty, g_verbosity
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void gameplay_page(void) {
    MenuBacking backing;
    // One bar width for the page, and a value field as wide as "Superbrief",
    // so both sliders' arrows sit in the same two columns.
    const int GP_ROW_W   = 29;
    const int GP_VALUE_W = 10;
    static const char *const NAMES[] = { "Easy", "Medium", "Hard" };
    // Both things the difficulty governs, because the map is only reachable
    // from the menu this page is a room off and a player turning Hard on has no
    // other warning that the Map row is about to go.
    static const char *const DESC[]  = { "Full map; typeahead: walkthrough",
                                         "Explored map; typeahead: words",
                                         "No map; typeahead off" };
    static const char *const VNAMES[] = { "Superbrief", "Brief", "Verbose" };
    static const char *const VDESC[]  = { "Room names only",
                                          "Full text on first visit",
                                          "Full text every visit" };
    enum { GR_DIFF, GR_VERB, GR_OK, GR_CANCEL };
    const int nrows = 4;
    static int last_sel = 0;   // held across visits; the four rows never change
    int sel = last_sel;
    int diff  = g_difficulty;
    int verb  = g_verbosity;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool commit = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;
        last_sel = sel;   // every frame, so no exit path has to remember to

        if (cancel || (ok && sel == GR_CANCEL)) break;
        if (commit || (ok && sel == GR_OK)) {
            if (diff != g_difficulty || verb != g_verbosity) {
                g_difficulty = diff; g_verbosity = verb;
                options_save();
            }
            break;
        }
        if (sel == GR_DIFF) { if (left && diff > DIFF_EASY) diff--; if (right && diff < DIFF_HARD) diff++; }
        else if (sel == GR_VERB) { if (left && verb > VERB_SUPERBRIEF) verb--; if (right && verb < VERB_VERBOSE) verb++; }

        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("GAMEPLAY", 34, 10, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "GAMEPLAY");
        int y = fy + 4;
        bool nums = !g_kbd_visible;
        menu_rowf(fx, fw, y, sel == GR_DIFF, GP_ROW_W, "%sDifficulty: %s %s %s",
                  menu_num(nums, GR_DIFF), diff > DIFF_EASY ? "<" : " ",
                  menu_pad(NAMES[diff], GP_VALUE_W), diff < DIFF_HARD ? ">" : " ");
        menu_text(fx, fw, y + 1, 0, DESC[diff]);
        y += 3;
        // Two spaces after the colon, so the value column lines up under
        // Difficulty's despite the shorter label.
        menu_rowf(fx, fw, y, sel == GR_VERB, GP_ROW_W, "%sRoom text:  %s %s %s",
                  menu_num(nums, GR_VERB), verb > VERB_SUPERBRIEF ? "<" : " ",
                  menu_pad(VNAMES[verb], GP_VALUE_W), verb < VERB_VERBOSE ? ">" : " ");
        menu_text(fx, fw, y + 1, 0, VDESC[verb]);
        y += 3;
        menu_rowf(fx, fw, y++, sel == GR_OK,     GP_ROW_W, "%sOk", menu_num(nums, GR_OK));
        menu_rowf(fx, fw, y++, sel == GR_CANCEL, GP_ROW_W, "%sCancel", menu_num(nums, GR_CANCEL));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
}

/*----------------------
 | controls_dispatch
 | Description: Opens the Controls page for whichever input device family is
 |   currently active, and re-opens the other one if controls_page or
 |   keyboard_controls_page reports that the player switched device families
 |   while it was up -- otherwise a mid-page switch would leave the wrong
 |   page on screen (gamepad remap rows under a real keyboard, or vice versa)
 |   with no way back except backing all the way out, per the same bug shape
 |   as the Options menu's Start handling above.
 | Author: suinevere
 | Dependencies: menu_pages.cxx (controls_page, keyboard_controls_page),
 |   console_view.c (g_kbd_visible)
 | Globals: g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void controls_dispatch(void) {
    bool again;
    do { again = g_kbd_visible ? controls_page() : keyboard_controls_page(); } while (again);
}

/*----------------------
 | options_menu
 | Description: Options menu (centered box): Resume, Save Game, Load Game, and
 |   Return to Title (shown only while a game is in progress), Gameplay, Display,
 |   Sound (shown only when there is audio to configure), Controls, and Network.
 |   Builds a dynamic item list -- Resume leads when g_in_game is set, then Save
 |   Game/Load Game; Gameplay, Display, Controls, and Network are always
 |   present (none has a hardware dependency); Sound appears only when there
 |   is audio to configure (CD-DA on the disc or the game's .BLB); Return to
 |   Title is gated behind g_in_game exactly like Save/Load -- there is
 |   nothing to "return" from when the mode-select menu already IS the title
 |   screen. Resume is gated the same way and for the same reason: it names the
 |   game this box is sitting on top of, and in the main menu there is no such
 |   game, only the box. It does exactly what backing out does, and exists
 |   because backing out is a keypress the player has to already know about.
 |   Credits lives on the title mode-select menu
 |   now, not here (main.cxx). Up/Down select a row with wraparound. A
 |   digit-row match is resolved before `item` is read (it can move `sel`)
 |   and OR'd into the activation flag. Activating dispatches to the
 |   matching sub-page (gameplay_page; network_page; controls_dispatch,
 |   which opens controls_page or keyboard_controls_page depending on
 |   g_kbd_visible and re-opens the other one if the player switches device
 |   families mid-page; display_options_page; sound_options_page), or for
 |   Return to Title, confirms via menu_confirm
 |   and, on yes, calls soft_reset_to_title() (which never returns). Save
 |   Game and Load Game close the menu immediately like Resume, but report
 |   which was picked through the return value instead of performing the
 |   save/restore themselves -- the menu has no access to the interpreter's
 |   game state, so the caller (saturn_glue.cxx's saturn_readline) must
 |   submit the matching "save"/"restore" command itself, the same way the
 |   F2/F3 quick keys do. Redraws with an unconditional menu_clear() before
 |   menu_frame() every frame -- MenuBacking only suppresses the image inside
 |   its own box rectangle, and does nothing to leftover text OUTSIDE it, so
 |   without this the wider menu that opened Options (e.g. the
 |   Single/Multiplayer list) would show through around this box. Rows are
 |   centered through menu_row, all padded to one width taken from the widest
 |   label -- centering each row on its own length was tried once before and
 |   reverted, because the digit column visibly zigzagged; padding first is what
 |   makes centering safe, since the block moves as one. Both the pad and the
 |   box width follow menu_select's conventions exactly, which they did not
 |   before: the box came from a hardcoded 18 rather than from the rows, and the
 |   pad kept the digit columns even when the digits were hidden. Between them
 |   those two put this list further right than every other menu on screen.
 |   The box is sized via menu_box_fit from the actual item count (4..8 rows,
 |   depending on g_in_game, sound_available and g_difficulty), not a fixed
 |   constant -- items[] is built first so nitems is known before the box is
 |   measured, keeping the title/blank/items/blank rhythm every other page uses
 |   instead of a gap that grows or shrinks with the fewest/most rows a given run
 |   shows. That build is a closure and runs again when Gameplay returns, because
 |   Hard removes the Map row and Gameplay is where the difficulty is set: built
 |   once, the row a player had just switched Hard on to be rid of would still be
 |   sitting there, and pickable, until the menu was closed and reopened.
 |   On exit (Resume, Save Game,
 |   Load Game, or B/Esc), blocks on menu_sync() until B/A/C/Start are all
 |   released, so the button that closed this menu cannot leak into whatever
 |   reads input next.
 | Author: suinevere
 | Dependencies: options.c (options_save, via gameplay_page), music.c
 |   (music_cdda_has_audio), sound.c (sound_has_audio), menu.c
 |   (menu_confirm), soft_reset.h (soft_reset_to_title, check_soft_reset),
 |   console_view.c (note_input_device/hint/g_kbd_visible), menu_pages.cxx
 |   (gameplay_page/network_page/controls_dispatch/display_options_page/
 |   sound_options_page), map_view.h (map_view_show)
 | Globals: g_in_game, g_difficulty
 | Params: N/A
 | Returns: OM_NONE, OM_SAVE, or OM_RESTORE
 ----------------------*/
int options_menu(void) {
    MenuBacking backing;
    enum { OI_RESUME, OI_MAP, OI_SAVE, OI_LOAD, OI_GAMEPLAY, OI_DISPLAY, OI_SOUND,
           OI_CONTROLS, OI_NETWORK, OI_RETURN, OI_N };
    // Hoisted out of the draw loop because the widest one sets the bar width
    // every row pads to, and that has to be known before the first row is drawn.
    static const char *const OI_LABEL[OI_N] = {
        "Resume", "Map", "Save Game", "Load Game", "Gameplay", "Display", "Sound",
        "Controls", "Network", "Title Screen"
    };
    bool sound_available = (music_cdda_has_audio() != 0) || (sound_has_audio() != 0);
    int items[OI_N], nitems = 0, label_w = 0, x0, y0, w, h;
    // Remembered as an item ID, not an index: Resume/Save/Load only appear
    // in-game and Sound only with audio present, so the same index names a
    // different row from the title screen than it does from a paused game.
    static int last_item = -1;
    int sel = 0;
    // A closure rather than straight-line setup because the Gameplay page can
    // change the difficulty from inside this menu, and Hard has no map -- so
    // the row set has to be able to be built a second time without the return
    // from that page having to know what else depends on it.
    auto build = [&]() {
        nitems = 0;
        // In-game only, and first: the menu is over a paused game there, so the
        // way back to it is the one thing worth naming. The main menu is not
        // over anything -- a Resume row would have to read as "close this box".
        if (g_in_game) items[nitems++] = OI_RESUME;
        // Hard turns the map off outright, and the row goes with it rather than
        // staying to be greyed: an absent row cannot be picked, and there is no
        // half-map to describe to a player who chose to do without one.
        if (g_in_game && g_difficulty != DIFF_HARD) items[nitems++] = OI_MAP;
        if (g_in_game) { items[nitems++] = OI_SAVE; items[nitems++] = OI_LOAD; }
        items[nitems++] = OI_GAMEPLAY;
        items[nitems++] = OI_DISPLAY;
        if (sound_available) items[nitems++] = OI_SOUND;
        items[nitems++] = OI_CONTROLS;
        if (!g_in_game) items[nitems++] = OI_NETWORK;
        if (g_in_game) items[nitems++] = OI_RETURN;

        label_w = 0;
        for (int i = 0; i < nitems; i++) {
            int n = 0; const char *s = OI_LABEL[items[i]];
            while (s[n]) n++;
            if (n > label_w) label_w = n;
        }
        // The box is sized from the rows it will actually hold, exactly as
        // menu_select does it -- the digit columns included unconditionally so
        // the box does not resize when the player switches pad<->keyboard
        // mid-menu. This used to be a hardcoded 18, left over from a wider label
        // set, which drew a 22-column box around an 11-column list: three or
        // four columns of dead air on each side, where every other menu's box
        // hugs its rows.
        menu_box_fit("OPTIONS", label_w + MENU_DIGIT_COLS, nitems + 2, &x0, &y0, &w, &h);

        sel = 0;
        for (int i = 0; i < nitems; i++) if (items[i] == last_item) { sel = i; break; }
    };
    build();
    int result = OM_NONE;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nitems) % nitems;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nitems;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool digit = menu_digit_row(ke, nitems, sel, left, right);
        int item = items[sel];
        last_item = item;   // every frame, so no exit path has to remember to
        bool act = digit
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || g_pad->WasPressed(Button::START)
                  || ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) break;
        if (act) {
            if (item == OI_RESUME) break;   // OM_NONE: exactly what backing out does
            else if (item == OI_MAP) { page_fade_out(g_menu_page_fade); map_view_show(); need_fade_in = true; }
            else if (item == OI_SAVE) { result = OM_SAVE; break; }
            else if (item == OI_LOAD) { result = OM_RESTORE; break; }
            else if (item == OI_GAMEPLAY) { page_fade_out(g_menu_page_fade); gameplay_page(); build(); need_fade_in = true; }
            else if (item == OI_NETWORK) { page_fade_out(g_menu_page_fade); network_page(); need_fade_in = true; }
            else if (item == OI_CONTROLS) {
                page_fade_out(g_menu_page_fade);
                controls_dispatch();
                menu_clear();
                need_fade_in = true;
            }
            else if (item == OI_DISPLAY) { page_fade_out(g_menu_page_fade); display_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_SOUND) { page_fade_out(g_menu_page_fade); sound_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_RETURN) {
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    soft_reset_to_title();
                }
            }
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "OPTIONS");
        bool nums = !g_kbd_visible;
        // The pad drops the digit columns when the digits are hidden, the same
        // way menu_select's does. Holding the wider pad and filling it with
        // menu_num's three spaces kept the gutter as leading whitespace inside
        // a centred block, which walked the whole list three columns right of
        // where every other menu puts it the moment a keyboard was in use.
        int pad = label_w + (nums ? MENU_DIGIT_COLS : 0);
        int ay = y0 + 4;
        for (int i = 0; i < nitems; i++)
            menu_rowf(x0, w, ay++, i == sel, pad, "%s%s",
                      nums ? menu_num(nums, i) : "", OI_LABEL[items[i]]);
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
    return result;
}
