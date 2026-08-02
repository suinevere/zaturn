/*----------------------
 | netbin_pages.cxx
 | Description: Implements the netbin build's three screens: the dial-number
 |   editor that is its root page, and the gamepad and physical-keyboard
 |   Controls pages that controls_dispatch switches between as the active input
 |   device changes. The bodies are lifted verbatim from menu_pages.cxx apart
 |   from the dialer, which trades its Cancel row for a Controls row -- the
 |   netbin has no title screen behind this page to cancel back to. Every page
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
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"

extern "C" {
#include "keyboard.h"
#include "menu_layout.h"
#include "display.h"
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
 | Description: Server dial-number editor, driven by either a real keyboard
 |   or the on-screen keyboard grid under pad control, plus Dial and Controls
 |   rows below the grid. Seeds the edit buffer from g_dialnum, then loops: a
 |   real-keyboard char/backspace/clear edits directly; Enter accepts, unless
 |   `arow` is on Controls (see below), in which case it opens Controls
 |   instead -- a real keyboard's only activation key, mirroring what C does
 |   for the pad. The pad moves a cursor that starts in the on-screen keyboard
 |   grid (Up/Down/Left/Right highlight a key, C types it) and, from the
 |   grid's bottom row, Down carries it onto Dial then Controls (Left/Right
 |   also toggle between the two once there, Up climbs back into the grid) --
 |   `arow` tracks this: -1 in the grid, 0 on Dial, 1 on Controls. A real
 |   keyboard's Up/Down drive the same `arow` the same way, so Controls stays
 |   reachable with no pad plugged in at all -- there is no Options screen in
 |   this build, so that cursor is the only path to it. B and Backspace
 |   backspace (and do nothing when the buffer is already empty -- there is no
 |   Cancel to fall through to); A always accepts regardless of cursor
 |   position. Accept validates the buffer with valid_dialnum before
 |   committing it into g_dialnum and calling options_save(); an invalid
 |   buffer keeps the page open with an inline error instead of closing.
 |   Controls opens controls_dispatch in place and, on return, re-syncs and
 |   re-enters this same loop rather than exiting -- this page is the
 |   netbin's root screen, so there is nowhere to leave it for. Redraws the
 |   NETWORK box, the current input line, the on-screen keyboard grid, and
 |   the Dial/Controls rows every frame.
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
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    int arow = -1;   // -1 = cursor in the KB grid; 0 = Dial; 1 = Controls
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool accept = false, controls = false;
        if      (ke.kind == SATURN_KEY_CHAR)      { if (k.input_len < DIALNUM_MAX) keyboard_type_char(&k, ke.ch); }
        else if (ke.kind == SATURN_KEY_BACKSPACE) { if (k.input_len > 0) keyboard_backspace(&k); }
        else if (ke.kind == SATURN_KEY_ENTER)     { if (arow == 1) controls = true; else accept = true; }
        else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
        else {
            // Up/Down also drive `arow` for a real keyboard (SATURN_KEY_UP/
            // DOWN), matching the pad -- Controls has no other way in, and
            // this is the only screen in the build, so a keyboard-only user
            // with no pad plugged in previously had no way to reach it at
            // all. Enter above is the keyboard's activation key once `arow`
            // gets there, mirroring how C activates the row it lands on for
            // the pad.
            if (g_pad->WasPressed(Button::Up) || ke.kind == SATURN_KEY_UP) {
                if      (arow == 1) arow = 0;
                else if (arow == 0) arow = -1;
                else                keyboard_move(&k, 0, -1);
            }
            if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) {
                if      (arow == -1 && k.cursor_row == KB_ROWS - 1) arow = 0;
                else if (arow == 0) arow = 1;
                else if (arow == -1) keyboard_move(&k, 0, 1);
            }
            if (g_pad->WasPressed(Button::Left))  { if (arow >= 0) arow = 0; else keyboard_move(&k, -1, 0); }
            if (g_pad->WasPressed(Button::Right)) { if (arow >= 0) arow = 1; else keyboard_move(&k,  1, 0); }
            if (g_pad->WasPressed(Button::C)) {
                if      (arow == 0) accept = true;
                else if (arow == 1) controls = true;
                else if (k.input_len < DIALNUM_MAX) keyboard_type(&k);
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
        menu_box_fit("NETWORK", 34, 14, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "NETWORK");
        int x = fx + 2, y = fy + 4;
        text_print(x, y++, "Server dial number:");
        text_print(x, y++, "> %s_", k.input);
        y++;
        for (int r = 0; r < KB_ROWS; r++) {
            char rowbuf[KB_COLS * 2 + 1]; int p = 0;
            for (int c = 0; c < KB_COLS; c++) {
                rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col && arow < 0) ? '[' : ' ';
                rowbuf[p++] = KB_LAYOUT[r][c];
            }
            rowbuf[p] = '\0';
            text_print(x + 2, y++, "%s", rowbuf);
        }
        y++;
        text_print(x, y++, "%c Dial",     arow == 0 ? '>' : ' ');
        text_print(x, y++, "%c Controls", arow == 1 ? '>' : ' ');
        if (err[0]) text_print(x, y, "%s", err);
        y++;
        y++;
        text_print(x, y, "%s",
            hint("C=type B=del  A=Dial", "type number Up/Dn=Ctrls Enter=Dial"));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
}

/*----------------------
 | FACE_LABEL / CHORD_LABEL
 | Description: Display names for the remappable face actions (FA_*) and chord
 |   actions (CA_*), shown as the row labels in the control-remap editor.
 | Author: suinevere
 ----------------------*/
static const char *const FACE_LABEL[FA_N]  = { "Accept", "Backspace/Cancel", "Type Letter" };
static const char *const CHORD_LABEL[CA_N] = { "Autocomplete", "Recall", "Home/End",
                                               "Line Up/Down", "Cursor Move", "Page Up/Down" };

/*----------------------
 | controls_page
 | Description: Gamepad Controls page -- live remap editor (3 face-button
 |   rows + 6 shift-chord rows), the fixed L+R Caps Toggle chord
 |   (informational, not remappable), a Keyboard Caps on/off toggle moved
 |   here from the old standalone gamepad landing page, then Reset to
 |   Defaults, Ok, and Cancel. Only the face/chord rows are numbered (there
 |   are just 9 digit keys, so Caps/Reset/Ok/Cancel stay reachable only by
 |   Up/Down). Snapshots g_face_btn/g_chord_slot on entry so Cancel (or
 |   B/Esc/Start) can restore them verbatim; Keyboard Caps takes effect
 |   immediately and is not part of that snapshot, matching the toggles on
 |   every other page. Up/Down move the row cursor with wraparound, resolved
 |   before the digit-row jump so a same-frame digit press wins the tie
 |   against the pad -- the order the other option pages use; resolving
 |   Up/Down first would let a simultaneous press move `sel` while
 |   left/right/act stayed set from the digit, cycling whichever row the pad
 |   happened to land on instead. Left/Right cycle the selected row's
 |   assignment via face_assign/chord_assign (applying their own
 |   tie-breaking rules), flip Keyboard Caps, or activate Reset/Ok/Cancel.
 |   The value column is drawn at a fixed offset of x + 20 + MENU_DIGIT_COLS,
 |   reserved unconditionally so it does not shift when the player switches
 |   between gamepad and keyboard mid-page; the widest value string is
 |   "Z+Left/Right" (12 chars), still clearing the box's right border at
 |   column 39. Returns true, without saving or restoring, if the active
 |   input device's family (pad vs. real keyboard) changed while this page
 |   was open, so controls_dispatch can hand off to keyboard_controls_page
 |   instead of leaving this page on screen showing the wrong device's
 |   controls with the music still paused; false on a genuine Ok/Cancel/
 |   B/Esc/Start exit.
 | Author: suinevere
 | Dependencies: input.c (g_face_btn/g_chord_slot/face_assign/chord_assign/
 |   mapping_reset_defaults/face_btn_name/slot_name), keyboard.c
 |   (keyboard_get_caps/keyboard_set_caps), console_view.c
 |   (note_input_device/hint/g_kbd_visible), menu.c, menu_layout.c
 |   (MENU_DIGIT_COLS), options.c (options_save), saturn_keyboard.h
 | Globals: g_face_btn, g_chord_slot, g_kbd_visible
 | Params: N/A
 | Returns: true if it exited because the input device family changed
 |   (caller should redispatch to the other Controls page); false on a
 |   normal exit
 ----------------------*/
static bool controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    bool started_kbd = g_kbd_visible;
    int s_face[FA_N], s_chord[CA_N];
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
    const int NASSIGN  = FA_N + CA_N;
    const int R_CAPS   = NASSIGN;
    const int R_RESET  = NASSIGN + 1;
    const int R_DONE   = NASSIGN + 2;
    const int R_CANCEL = NASSIGN + 3;
    int sel = 0;
    bool switched = false;
    for (;;) {
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
        bool back  = g_pad->WasPressed(Button::B) || g_pad->WasPressed(Button::START)
                   || ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            break;
        }
        if (up)   sel = (sel - 1 + R_CANCEL + 1) % (R_CANCEL + 1);
        if (down) sel = (sel + 1) % (R_CANCEL + 1);
        if (menu_digit_row(ke, NASSIGN, sel, left, right)) act = true;
        if (sel == R_DONE)  { if (act) { options_save(); break; } }
        else if (sel == R_CANCEL) { if (act) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            break; } }
        else if (sel == R_RESET) { if (act) mapping_reset_defaults(); }
        else if (sel == R_CAPS) { if (left || right || act) keyboard_set_caps(!keyboard_get_caps()); }
        else if (left || right) {
            if (sel < FA_N) {
                int n = right ? (g_face_btn[sel] + 1) % 3 : (g_face_btn[sel] + 2) % 3;
                face_assign(sel, n);
            } else {
                int a = sel - FA_N;
                int n = right ? (g_chord_slot[a] + 1) % SL_N : (g_chord_slot[a] + SL_N - 1) % SL_N;
                chord_assign(a, n);
            }
        }

        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("CONTROLS", 36, FA_N + CA_N + 10, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 4;
        bool nums = !g_kbd_visible;
        const int vx = x + 20 + MENU_DIGIT_COLS;
        for (int a = 0; a < FA_N; a++) {
            char cur = sel == a ? '>' : ' ';
            if (nums) text_print(x, y, "%c %d) %s", cur, a + 1, FACE_LABEL[a]);
            else      text_print(x, y, "%c    %s", cur, FACE_LABEL[a]);
            text_print(vx, y++, "%s", face_btn_name(a));
        }
        for (int a = 0; a < CA_N; a++) {
            char cur = sel == FA_N + a ? '>' : ' ';
            if (nums) text_print(x, y, "%c %d) %s", cur, FA_N + a + 1, CHORD_LABEL[a]);
            else      text_print(x, y, "%c    %s", cur, CHORD_LABEL[a]);
            text_print(vx, y++, "%s", slot_name(g_chord_slot[a]));
        }
        text_print(x + 2 + MENU_DIGIT_COLS, y, "Caps Toggle");
        text_print(vx, y++, "L+R (fixed)");
        text_print(x, y++, "%c    Keyboard Caps: %s", sel == R_CAPS ? '>' : ' ',
                           keyboard_get_caps() ? "On" : "Off");
        y++;
        text_print(x, y++, "%c    Reset to Defaults", sel == R_RESET ? '>' : ' ');
        text_print(x, y++, "%c    Ok", sel == R_DONE ? '>' : ' ');
        text_print(x, y++, "%c    Cancel", sel == R_CANCEL ? '>' : ' ');
        y += 2;
        text_print(x, y++, "%s", hint("A/C=Ok B=Cancel",
                                             "Enter=Ok Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    SRL::Core::Synchronize();
    return switched;
}

/*----------------------
 | keyboard_controls_page
 | Description: Physical-keyboard settings page: 6 rows -- Insert mode, Caps
 |   Lock, Num Lock, Scroll Lock (all two-state toggles, so direction and
 |   activation are treated alike), then Ok and Cancel. Snapshots the
 |   keyboard.c getters on entry so Cancel (B/Esc/Start, or the Cancel row)
 |   can restore them. Ok commits and calls options_save(). The value column
 |   is fixed at x + 18 in BOTH digit and no-digit modes -- unlike the other
 |   pages it must NOT take the usual MENU_DIGIT_COLS shift: at x + 21 the
 |   widest value, "Off (overwrite)" (15 chars), would end on column 38, this
 |   box's right border (fx=1, fw=38). It does not need the shift regardless:
 |   the longest numbered label, "N) Insert mode", ends at column 18, still
 |   two columns clear of the value at 21. Returns true, without saving or
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
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    bool started_kbd = g_kbd_visible;
    int s_ins = keyboard_get_insert(), s_caps = keyboard_get_caps(),
        s_num = keyboard_get_num(), s_scrl = keyboard_get_scrolllock();
    const int N = 6;
    int sel = 0;
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
        bool back = g_pad->WasPressed(Button::B) || g_pad->WasPressed(Button::START)
                  || ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) {
            keyboard_set_insert(s_ins); keyboard_set_caps(s_caps);
            keyboard_set_num(s_num); keyboard_set_scrolllock(s_scrl);
            break;
        }
        if (menu_digit_row(ke, N, sel, left, right)) act = true;
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
        menu_box_fit("CONTROLS", 34, 14, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 4;
        text_print(x, y++, "Insert: type-insert, caret arrows.");
        text_print(x, y++, "ScrLk: Up/Dn scroll, Ctrl=history.");
        y++;
        bool nums = !g_kbd_visible;
        if (nums) text_print(x, y, "%c 1) Insert mode", sel == 0 ? '>' : ' ');
        else      text_print(x, y, "%c    Insert mode", sel == 0 ? '>' : ' ');
        text_print(x + 18, y++, "%s", keyboard_get_insert() ? "On" : "Off");
        if (nums) text_print(x, y, "%c 2) Caps Lock", sel == 1 ? '>' : ' ');
        else      text_print(x, y, "%c    Caps Lock", sel == 1 ? '>' : ' ');
        text_print(x + 18, y++, "%s", keyboard_get_caps() ? "On" : "Off");
        if (nums) text_print(x, y, "%c 3) Num Lock", sel == 2 ? '>' : ' ');
        else      text_print(x, y, "%c    Num Lock", sel == 2 ? '>' : ' ');
        text_print(x + 18, y++, "%s", keyboard_get_num() ? "On" : "Off");
        if (nums) text_print(x, y, "%c 4) Scroll Lock", sel == 3 ? '>' : ' ');
        else      text_print(x, y, "%c    Scroll Lock", sel == 3 ? '>' : ' ');
        text_print(x + 18, y++, "%s", keyboard_get_scrolllock() ? "On" : "Off");
        y++;
        if (nums) text_print(x, y++, "%c 5) Ok", sel == 4 ? '>' : ' ');
        else      text_print(x, y++, "%c    Ok", sel == 4 ? '>' : ' ');
        if (nums) text_print(x, y++, "%c 6) Cancel", sel == 5 ? '>' : ' ');
        else      text_print(x, y++, "%c    Cancel", sel == 5 ? '>' : ' ');
        y += 2;
        text_print(x, y++, "%s", hint("A/C=Ok  B=Cancel", "Enter=Ok  Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    SRL::Core::Synchronize();
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
