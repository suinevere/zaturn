/*----------------------
 | netbin_pages.cxx
 | Description: Implements the netbin build's own screens: the dial-number
 |   editor that is its root page, the gamepad and physical-keyboard Controls
 |   pages that controls_dispatch switches between as the active input device
 |   changes, and the pause menu online_mode opens over a live session, with the
 |   Display and Gameplay pages under it. The bodies are lifted verbatim from
 |   menu_pages.cxx apart from two: the dialer trades its Cancel row for a
 |   Controls row -- the netbin has no title screen behind this page to cancel
 |   back to -- and the Display page drops the Dynamic palette's dimming row and
 |   image-slot pinning, which is what keeps title.cxx out of the link. Every page
 |   constructs a MenuBacking on entry (menu.h) and drops the input edge that
 |   opened it with an initial SRL::Core::Synchronize() before entering its poll
 |   loop, so the press that opened the page cannot also act inside it.
 |   tests/test_netbin_lift.py gates the lifted bodies against their originals.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.c, input.h (g_pad/g_face_btn/g_chord_slot/
 |   face_assign/chord_assign/face_btn_name/slot_name/pad_repeat_update/
 |   mapping_reset_defaults), console_view.h (note_input_device/hint/
 |   g_kbd_visible), options.h (options_save/valid_dialnum),
 |   app_state.h (g_dialnum), keyboard.h, saturn_keyboard.h, soft_reset.h,
 |   display.h, SRL
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "netbin_pages.h"
#include "menu.h"
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "controller.h"
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
#include "map_view.h"

extern "C" {
#include "keyboard.h"
#include "numpad.h"
#include "menu_layout.h"
#include "display.h"
#include "synth.h"
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
 | controls_dispatch
 | Description: Forward declaration; defined below but called from the page above.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void controls_dispatch(void);

/*----------------------
 | netbin_dial_page
 | Description: Server dial-number editor. Seeds the edit buffer from g_dialnum,
 |   then loops. The on-screen numpad is shown only while a gamepad is the active
 |   device (g_kbd_visible) -- it is how a controller enters digits; once a real
 |   keyboard is in hand it is hidden and the box shrinks to just the input line
 |   and the Dial/Controls rows, since the number is typed directly then. `arow`
 |   is -1 on the numpad, 0 on Dial, 1 on Controls: the D-pad walks the numpad and
 |   drops off its bottom onto Dial then Controls, C types the highlighted digit or
 |   activates the row; a real keyboard's Up/Down move between Dial and Controls
 |   (it never enters the hidden numpad) and Enter activates the selected one, so
 |   Controls stays reachable with no pad plugged in -- there is no Options screen
 |   in this build, so that row is the only path to it. B and Backspace backspace
 |   (a no-op on an empty buffer -- there is no Cancel to fall through to); A always
 |   accepts. Accept validates with valid_dialnum before committing into g_dialnum
 |   and calling options_save(); an invalid buffer keeps the page open with an
 |   inline error. Controls opens controls_dispatch in place and, on return,
 |   re-syncs and re-enters this same loop -- this page is the netbin's root
 |   screen, so there is nowhere to leave it for.
 | Author: suinevere
 | Dependencies: keyboard.c, saturn_keyboard.h, soft_reset.h, options.c
 |   (valid_dialnum, options_save), menu.c, console_view.c, netbin_pages.cxx
 |   (controls_dispatch)
 | Globals: g_dialnum
 | Params: N/A
 | Returns: N/A -- returns only once g_dialnum holds a committed valid number
 ----------------------*/
void netbin_dial_page(void) {
    MenuBacking backing;
    const int DIAL_ROW_W = 8;   // "Controls", the wider of the page's two rows
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    int arow = 0;   // 0 = Dial; 1 = Controls
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        // Switching to a real keyboard hides the numpad, so the cursor cannot be
        // left sitting on it -- drop it to Dial.
        if (!g_kbd_visible && arow < 0) arow = 0;
        bool accept = false, controls = false;
        if      (ke.kind == SATURN_KEY_CHAR)      { if (k.input_len < DIALNUM_MAX) keyboard_type_char(&k, ke.ch); }
        else if (ke.kind == SATURN_KEY_BACKSPACE) { if (k.input_len > 0) keyboard_backspace(&k); }
        else if (ke.kind == SATURN_KEY_ENTER)     { if (arow == 1) controls = true; else accept = true; }
        else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
        else {
            bool up   = g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP;
            bool down = g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN;
            np_dpad(up, down, g_pad->WasPressed(Button::Left), g_pad->WasPressed(Button::Right),
                    g_kbd_visible, &arow, &k.cursor_row, &k.cursor_col);
            if (g_pad->WasPressed(Button::C)) {
                if      (arow == 0) accept = true;
                else if (arow == 1) controls = true;
                else if (np_valid(k.cursor_row, k.cursor_col) && k.input_len < DIALNUM_MAX)
                    keyboard_type_char(&k, np_char(k.cursor_row, k.cursor_col));
            }
            if (g_pad->WasPressed(Button::B))     { if (k.input_len > 0) keyboard_backspace(&k); }
            if (g_pad->WasPressed(Button::A))     accept = true;
        }
        if (controls) {
            page_fade_out(g_menu_page_fade);
            controls_dispatch();
            SRL::Core::Synchronize();
            need_fade_in = true;
            continue;
        }
        if (accept) {
            if (!valid_dialnum(k.input)) err = "Invalid number (digits only).";
            else {
                int j;
                for (j = 0; k.input[j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = k.input[j];
                g_dialnum[j] = '\0';
                options_save();
                page_fade_out(g_menu_page_fade);
                return;
            }
        }
        menu_clear();
        int fx, fy, fw, fh;
        /* Taller when the numpad is up (gamepad), shrinking to the line and the
           two rows once a real keyboard hides it. No controls hint on either --
           only the confirm box still spells the buttons out. */
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
        menu_row(fx, fw, y++, arow == 0, DIAL_ROW_W, "Dial");
        menu_row(fx, fw, y++, arow == 1, DIAL_ROW_W, "Controls");
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
 |   keyboard.c getters on entry so Cancel (B/Esc/Start, or the Cancel row)
 |   can restore them. Ok commits and calls options_save(). Rows are composed
 |   whole and drawn through menu_row, so the label field (KB_LABEL_W) is what
 |   holds the value column in place rather than an absolute screen column, and
 |   it holds in BOTH digit and no-digit modes because menu_num reserves its
 |   three columns either way. Returns true, without saving or
 |   restoring, if the active input device's family changed while this page
 |   was open, so controls_dispatch can hand off to controls_page instead of
 |   leaving this page on screen showing the wrong device's controls with the
 |   music still paused; false on a genuine Ok/Cancel/B/Esc/Start exit.
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
static bool keyboard_controls_page(void) {
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
 | display_options_page
 | Description: The netbin's Display page: Palette, Background and Text, over
 |   Ok and Cancel. Lifted from menu_pages.cxx minus everything that serves the
 |   Dynamic palette -- the pin/unpin of the wallpaper's image slot, and the
 |   Dimming row, which only ever appeared under Dynamic because it offsets a
 |   picture. Dynamic is unreachable in this build anyway (display_has_art()
 |   is 0 with no game selected, so display_cycle_palette already steps past it,
 |   and display_apply's image branch is #ifndef NETBIN), but dropping the calls
 |   rather than relying on that is what keeps title.cxx out of the link.
 |   Cancel restores the entry snapshot; Ok and Start persist via options_save.
 | Author: suinevere
 | Dependencies: display.c (cycle/name helpers), options.c (display_cycle_row,
 |   display_apply, options_save), menu.c, console_view.c, input.c,
 |   saturn_keyboard.h, soft_reset.h
 | Globals: g_display, g_kbd_visible, g_menu_page_fade
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
    enum { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    static const int NROWS = 5;
    int rows[NROWS];

    static int last_sel = 0;   // held across visits; the five rows never change
    int sel = last_sel;
    DisplayState snapshot = g_display;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        int nrows = 0;
        rows[nrows++] = DR_PALETTE;
        rows[nrows++] = DR_BG;
        rows[nrows++] = DR_TEXT;
        rows[nrows++] = DR_OK;
        rows[nrows++] = DR_CANCEL;

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
        last_sel = sel;   // every frame, so no exit path has to remember to

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
        }
        if (ok && row == DR_OK) { options_save(); break; }

        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("DISPLAY", DSP_ROW_W, NROWS + 3, &fx, &fy, &fw, &fh);
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
 | gameplay_page
 | Description: Difficulty (which picks the typeahead's ranking mode) over Room
 |   text (the parser verbosity). Lifted verbatim from menu_pages.cxx, which is
 |   what tests/test_netbin_lift.py checks. Difficulty takes effect here the way
 |   it does in the CD build, through g_difficulty; Room text does not, because
 |   the story is running on the server -- online_mode compares g_verbosity
 |   across the pause menu and types the verbosity command at the parser, the
 |   same handling saturn_glue.cxx gives the CD build's Options menu.
 | Author: suinevere
 | Dependencies: app_state.c (g_difficulty/g_verbosity), options.c
 |   (options_save), menu.c, console_view.c, input.c, saturn_keyboard.h,
 |   soft_reset.h
 | Globals: g_difficulty, g_verbosity, g_kbd_visible, g_menu_page_fade
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
    // Both things the difficulty governs, matching the CD build's wording: the
    // Map row on the pause menu goes when Hard is picked, and this line is the
    // only warning of it.
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
 | sound_options_page
 | Description: Sound Options for the netbin. The CD build has a page of this
 |   name in menu_pages.cxx, which this build does not link -- it carries its
 |   own Display and Gameplay pages for the same reason -- so this is a second
 |   implementation rather than a shared one. What IS shared is the row rule:
 |   sound_page_rows decides which rows exist, so the netbin cannot drift from
 |   the CD build about when a music slider is offered. With no disc there is
 |   no CD-DA and no Blorb, so the list here is always Music, Synth Music, Ok,
 |   Cancel. Cancel restores the level it found, live, since the row is judged
 |   by ear while it is moved.
 | Author: suinevere
 | Dependencies: menu.c, menu_layout.c (sound_page_rows), synth.h, options.h,
 |   input.c, saturn_keyboard.h, soft_reset.h
 | Globals: g_synth_level, g_kbd_visible, g_menu_page_fade
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void sound_options_page(void) {
    MenuBacking backing;
    const int SND_ROW_W   = 31;
    const int SND_LABEL_W = 14;

    int rows[8];
    int nrows = sound_page_rows(0, 0, rows, 8);

    static int last_row = SND_ROW_MASTER;
    int sel = 0;
    for (int i = 0; i < nrows; i++) if (rows[i] == last_row) { sel = i; break; }
    int s_syn = g_synth_level;

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

        if (cancel || (ok && row == SND_ROW_CANCEL)) {
            g_synth_level = s_syn;
            synth_set_level(g_synth_level);
            break;
        }
        if (commit || (ok && row == SND_ROW_OK)) {
            options_save();
            break;
        }
        if (row == SND_ROW_MASTER && (left || right || ok)) {
            g_synth_level = (g_synth_level > 0) ? 0 : SYNTH_LEVEL_DEFAULT;
            synth_set_level(g_synth_level);
        }
        else if (row == SND_ROW_SYNTH) {
            if (left && g_synth_level > 0) g_synth_level--;
            if (right && g_synth_level < 7) g_synth_level++;
            if (left || right) synth_set_level(g_synth_level);
        }

        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("SOUND", 34, nrows + 4, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "SOUND");
        int y = fy + 4;
        bool nums = !g_kbd_visible;
        for (int i = 0; i < nrows; i++) {
            const char *n = menu_num(nums, i);
            switch (rows[i]) {
                case SND_ROW_MASTER:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%s%s%s", n,
                              menu_pad("Music", SND_LABEL_W),
                              g_synth_level > 0 ? "On" : "Off");
                    break;
                case SND_ROW_SYNTH:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%s%s%d", n,
                              menu_pad("Synth Music", SND_LABEL_W), g_synth_level);
                    break;
                case SND_ROW_OK:
                    y++;
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%sOk", n);
                    break;
                case SND_ROW_CANCEL:
                    menu_rowf(fx, fw, y++, i == sel, SND_ROW_W, "%sCancel", n);
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
 | netbin_pause_menu
 | Description: See netbin_pages.h. The Map row is present at Easy and Medium
 |   and absent at Hard, the same rule the CD build's options_menu applies. The
 |   row set is therefore built by a closure that runs again when Gameplay
 |   returns, since that page is where the difficulty changes.
 | Author: suinevere
 | Dependencies: menu.c, menu_layout.c, console_view.c, input.c,
 |   saturn_keyboard.h, soft_reset.h (check_soft_reset,
 |   confirm_return_to_title), map_view.h (map_view_show)
 | Globals: g_kbd_visible, g_menu_page_fade, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void netbin_pause_menu(void) {
    MenuBacking backing;
    enum { PI_RESUME, PI_MAP, PI_DISPLAY, PI_GAMEPLAY, PI_SOUND, PI_CONTROLS, PI_RESTART, PI_N };
    static const char *const LABEL[PI_N] = {
        "Resume", "Map", "Display", "Gameplay", "Sound", "Controls", "Restart"
    };

    int items[PI_N], nitems = 0, label_w = 0, row_w = 0, x0, y0, w, h;
    // Remembered as an item ID rather than an index, because Hard drops a row
    // and the same index then names a different entry than it did last visit.
    static int last_item = PI_RESUME;
    int sel = 0;
    // A closure because Gameplay is on this menu and Gameplay is where the
    // difficulty is set: built once, the Map row a player had just turned Hard
    // on to be rid of would sit there, pickable, until the menu was reopened.
    auto build = [&]() {
        nitems = 0;
        items[nitems++] = PI_RESUME;
        if (g_difficulty != DIFF_HARD) items[nitems++] = PI_MAP;
        items[nitems++] = PI_DISPLAY;
        items[nitems++] = PI_GAMEPLAY;
        items[nitems++] = PI_SOUND;
        items[nitems++] = PI_CONTROLS;
        items[nitems++] = PI_RESTART;

        label_w = 0;
        for (int i = 0; i < nitems; i++) {
            int n = 0;
            while (LABEL[items[i]][n]) n++;
            if (n > label_w) label_w = n;
        }
        row_w = MENU_DIGIT_COLS + label_w;
        menu_box_fit("PAUSED", 18, nitems + 2, &x0, &y0, &w, &h);

        sel = 0;
        for (int i = 0; i < nitems; i++) if (items[i] == last_item) { sel = i; break; }
    };
    build();
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
        bool act = menu_digit_row(ke, nitems, sel, left, right)
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || g_pad->WasPressed(Button::START)
                  || ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE;
        int item = items[sel];
        last_item = item;   // every frame, so no exit path has to remember to
        if (back) break;
        if (act) {
            if (item == PI_RESUME) break;   // exactly what backing out does
            else if (item == PI_MAP)      { page_fade_out(g_menu_page_fade); map_view_show(); menu_clear(); need_fade_in = true; }
            else if (item == PI_DISPLAY)  { page_fade_out(g_menu_page_fade); display_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == PI_GAMEPLAY) { page_fade_out(g_menu_page_fade); gameplay_page(); build(); need_fade_in = true; }
            // menu_clear rather than Gameplay's build(): build() exists because
            // Gameplay can change the difficulty and so change which items are
            // listed, and the Sound page changes no item's visibility.
            else if (item == PI_SOUND)    { page_fade_out(g_menu_page_fade); sound_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == PI_CONTROLS) { page_fade_out(g_menu_page_fade); controls_dispatch(); menu_clear(); need_fade_in = true; }
            // Never returns if accepted: confirm_return_to_title longjmps to
            // main()'s dial loop, which resets the backing depth and the
            // menu service this page was opened under.
            else if (item == PI_RESTART)  { confirm_return_to_title("hang up and reboot back to the dial page?"); }
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "PAUSED");
        bool nums = !g_kbd_visible;
        int ay = y0 + 4;
        for (int i = 0; i < nitems; i++)
            menu_rowf(x0, w, ay++, i == sel, row_w, "%s%s",
                      menu_num(nums, i), LABEL[items[i]]);
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
}
