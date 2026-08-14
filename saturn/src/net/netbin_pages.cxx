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
#include "numpad.h"
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
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    int arow = 0;   // 0 = Dial; 1 = Controls
    SRL::Core::Synchronize();
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
           two rows once a real keyboard hides it. */
        menu_box_fit("NETWORK", 24, g_kbd_visible ? 15 : 10, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "NETWORK");
        int x = fx + 2, y = fy + 4;
        text_print(x, y++, "Server dial number:");
        text_print(x, y++, "> %s_", k.input);
        y++;
        if (g_kbd_visible) {
            int npx = fx + 2 + ((fw - 4) - (NP_COLS * 2 - 1)) / 2;   // centre the pad
            for (int r = 0; r < NP_ROWS; r++) {
                char rowbuf[NP_COLS * 2]; int p = 0;
                for (int c = 0; c < NP_COLS; c++) { rowbuf[p++] = np_char(r, c); if (c < NP_COLS - 1) rowbuf[p++] = ' '; }
                rowbuf[p] = '\0';
                text_print(npx, y, rowbuf);
                if (arow < 0 && r == k.cursor_row && np_valid(r, k.cursor_col)) {
                    char one[2] = { np_char(r, k.cursor_col), '\0' };
                    text_print_hl(npx + k.cursor_col * 2, y, one);
                }
                y++;
            }
            y++;
        }
        text_print(x, y++, "%c Dial",     arow == 0 ? '>' : ' ');
        text_print(x, y++, "%c Controls", arow == 1 ? '>' : ' ');
        if (err[0]) text_print(x, y, "%s", err);
        y++;
        y++;
        text_print(x, y, "%s",
            hint("A=Dial C=type  B=del", "type number  Enter=Dial"));
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
static const char *const FACE_LABEL[FA_N]  = { "Accept", "Backspace/Cancel", "Type Letter",
                                               "Space" };
static const char *const CHORD_LABEL[CA_N] = { "Autocomplete", "Recall", "Home/End",
                                               "Line Up/Down", "Cursor Move", "Page Up/Down" };

/*----------------------
 | PANEL_FACE_LABEL
 | Description: The Command Panel view's names for the face actions, differing
 |   from FACE_LABEL only at FA_TYPE: the panel commits a whole word off the rose,
 |   not a letter off a grid. Indexed by FA_* like its counterpart rather than by
 |   visible-row number, so a row lookup never has to know which table it is in.
 |   FA_SPACE's entry is never drawn -- the panel view does not list it -- but is
 |   present so the two tables stay index-compatible.
 | Author: suinevere
 ----------------------*/
static const char *const PANEL_FACE_LABEL[FA_N] = { "Accept", "Backspace/Cancel",
                                                    "Type Word", "Space" };

/*----------------------
 | INAMES / IDESC
 | Description: The Interface row's value names and description lines, indexed by
 |   IFACE_KEYBOARD/IFACE_PANEL. Word for word what the Gameplay page showed
 |   before the row moved here; file scope rather than function-local because the
 |   row now heads a page that is drawn from tables either side of it.
 | Author: suinevere
 ----------------------*/
static const char *const INAMES[] = { "Keyboard", "Command Panel" };
static const char *const IDESC[]  = { "Type words, autocomplete",
                                      "Pick words with the pad" };

/*----------------------
 | CtlRow / CTL_PANEL / CTL_KBD / CTL_PANEL_N / CTL_KBD_N / CTL_ASSIGN_MAX
 | Description: The remappable rows each Controls view lists, in the order they
 |   are drawn. Both tables index the one shared g_face_btn/g_chord_slot mapping --
 |   a view is a filter over it, not a second copy -- so a row absent from a view
 |   is still live in the interface that uses it. The panel drops Space (it types
 |   whole words), Autocomplete (it has no completion list) and Cursor Move (its
 |   D-pad drives the rose directly, unshifted), and orders its chords the way the
 |   hand reaches them rather than by CA_* number.
 |
 |   A consequence worth knowing: remapping a shown row onto a hidden row's slot
 |   still swaps, silently, because chord_assign only sees the mapping and not the
 |   view. Moving Recall onto L/R from the panel view sends Autocomplete to
 |   Recall's old slot with nothing on screen to say so; it is visible again the
 |   moment the player switches to the Keyboard view.
 |
 |   CTL_ASSIGN_MAX is the longer of the two lists, used to reserve the rows the
 |   block occupies so the box and everything below it hold still when the player
 |   flips the Interface row.
 | Author: suinevere
 ----------------------*/
enum { CK_FACE, CK_CHORD };
struct CtlRow { unsigned char kind; unsigned char idx; };

static const CtlRow CTL_PANEL[] = {
    { CK_FACE,  FA_ACCEPT }, { CK_FACE,  FA_BACK },    { CK_FACE,  FA_TYPE },
    { CK_CHORD, CA_RECALL }, { CK_CHORD, CA_PAGE },    { CK_CHORD, CA_HOMEEND },
    { CK_CHORD, CA_LINE },
};
static const CtlRow CTL_KBD[] = {
    { CK_FACE,  FA_ACCEPT }, { CK_FACE,  FA_BACK },    { CK_FACE,  FA_TYPE },
    { CK_FACE,  FA_SPACE },
    { CK_CHORD, CA_AUTO },   { CK_CHORD, CA_RECALL },  { CK_CHORD, CA_HOMEEND },
    { CK_CHORD, CA_LINE },   { CK_CHORD, CA_CURSOR },  { CK_CHORD, CA_PAGE },
};
#define CTL_PANEL_N   7
#define CTL_KBD_N     10
#define CTL_ASSIGN_MAX CTL_KBD_N

/*----------------------
 | CTL_BLOCK_ROWS
 | Description: Rows reserved for the assignable list plus the unremappable ones
 |   under it -- the panel's Cycle Module and Caps Toggle, the keyboard's Caps
 |   Toggle alone. The keyboard's 10 + 1 is the taller of the two; the panel's
 |   7 + 2 leaves the last two blank. Reserved rather than measured so Keyboard
 |   Caps, the Swap row and Ok/Cancel keep their screen position across a view
 |   flip instead of sliding two rows up and back under the cursor.
 | Author: suinevere
 ----------------------*/
#define CTL_BLOCK_ROWS (CTL_KBD_N + 1)

/*----------------------
 | CtlView / ctl_view
 | Description: The Controls page's row geometry for whichever interface the
 |   Interface row currently names: which table the assignable rows come from, how many there
 |   are, and the `sel` index of every row below them (all offset by one for the
 |   Interface row at index 0). Derived rather than stored because the page
 |   calls it twice per frame -- once before reading input and again after, so the
 |   frame that flips the Interface row already draws the list it flipped to
 |   instead of
 |   showing the old one for a frame.
 | Author: suinevere
 | Dependencies: app_state.h (g_cmd_iface, IFACE_PANEL)
 | Globals: g_cmd_iface
 | Params: N/A
 | Returns: the row geometry for the named interface
 ----------------------*/
struct CtlView {
    const CtlRow *rows;
    bool panel;
    int nassign, r_caps, r_toggle, r_reset, r_done, r_cancel, nrows;
};

static CtlView ctl_view(void) {
    CtlView v;
    v.panel    = (g_cmd_iface == IFACE_PANEL);
    v.rows     = v.panel ? CTL_PANEL : CTL_KBD;
    v.nassign  = v.panel ? CTL_PANEL_N : CTL_KBD_N;
    v.r_caps   = v.nassign + 1;
    v.r_toggle = v.nassign + 2;
    v.r_reset  = v.nassign + 3;
    v.r_done   = v.nassign + 4;
    v.r_cancel = v.nassign + 5;
    v.nrows    = v.nassign + 6;
    return v;
}

/*----------------------
 | controls_page
 | Description: Gamepad Controls page. The top row is the Interface slider,
 |   moved here verbatim from the Gameplay page -- same wording, same clamped
 |   Left/Right, same description line under it -- and everything below it is
 |   that interface's own configuration: the Command Panel lists Accept, Backspace/
 |   Cancel, Type Word, Recall, Page Up/Down, Home/End and Line Up/Down, then a
 |   fixed Cycle Module (L/R) and the fixed L+R Caps Toggle; the Keyboard lists
 |   all four face actions and all six chords, then the Caps Toggle alone. Below
 |   the swapped block, shared by both views: a Keyboard Caps on/off toggle, a
 |   Panel/Keyboard Swap row cycling g_toggle_btn between the Z and Y shift
 |   buttons that flip the in-game interface, then Reset to Defaults, Ok and
 |   Cancel.
 |
 |   The two views are filters over the one g_face_btn/g_chord_slot mapping, not
 |   separate tables -- see CTL_PANEL/CTL_KBD for what that costs. Interface is
 |   g_cmd_iface, the persisted preference main.cxx seeds g_cmd_mode from at game
 |   start, and this page is now its only editor; a session already in progress
 |   keeps whatever the toggle button last picked, exactly as it did while the
 |   row lived on the Gameplay page. It is deliberately NOT touched by Reset to
 |   Defaults, which restores bindings and has no business throwing the player
 |   out of the view they are reading.
 |
 |   Content starts one row higher than the other option pages, at fy + 3 rather
 |   than fy + 4, so the Interface row plus its description line finish where a
 |   single row would have and the swapped block below opens on the same line it
 |   did when the head row was one line tall.
 |
 |   Only the assignable rows are numbered -- the keyboard view's ten use every
 |   digit key there is, so Interface, Caps, Swap, Reset, Ok and Cancel stay
 |   reachable only by Up/Down. menu_digit_row therefore indexes the assignable
 |   list and its result is offset past the Interface row rather than used as
 |   `sel` directly. Snapshots g_face_btn/g_chord_slot/g_toggle_btn/g_cmd_iface on
 |   entry so Cancel (or B/Backspace) can restore them verbatim; Start/Esc leave
 |   the other way, saving what is on screen exactly as the Ok row does. Keyboard
 |   Caps takes effect immediately and is not part of that snapshot, matching the
 |   toggles on every other page; Panel/Keyboard Swap and Interface ARE
 |   snapshotted, since a stray edit to either should be as cancellable as a
 |   face/chord remap. Up/Down move the row cursor with wraparound, resolved
 |   before the digit-row jump so a same-frame digit press wins the tie against
 |   the pad -- the order the other option pages use; resolving Up/Down first
 |   would let a simultaneous press move `sel` while left/right/act stayed set
 |   from the digit, cycling whichever row the pad happened to land on instead.
 |   Left/Right cycle the selected row's assignment via face_assign/chord_assign
 |   (applying their own tie-breaking rules), move Interface, flip Keyboard Caps or
 |   the Panel/Keyboard Swap, or activate Reset/Ok/Cancel.
 |
 |   Only the Interface row can change the row count, and it sits at index 0,
 |   so `sel` is always 0 at the moment the count changes and never needs
 |   clamping. The swapped block is drawn to the reserved CTL_BLOCK_ROWS height
 |   for the same reason the value column is reserved: nothing below it may move
 |   when the view flips. The value column stays at x + 20 + MENU_DIGIT_COLS,
 |   reserved unconditionally so it does not shift when the player switches
 |   between gamepad and keyboard mid-page; the widest value string is still
 |   "Z+Left/Right" (12 chars), clearing the box's right border at column 39.
 |
 |   Returns true, without saving or restoring, if the active input device's
 |   family (pad vs. real keyboard) changed while this page was open, so
 |   controls_dispatch can hand off to keyboard_controls_page instead of leaving
 |   this page on screen showing the wrong device's controls with the music still
 |   paused; false on a genuine Ok/Cancel/B/Backspace/Start/Esc exit.
 | Author: suinevere
 | Dependencies: input.c (g_face_btn/g_chord_slot/face_assign/chord_assign/
 |   mapping_reset_defaults/face_btn_name/slot_name), keyboard.c
 |   (keyboard_get_caps/keyboard_set_caps), console_view.c
 |   (note_input_device/hint/g_kbd_visible), menu.c, menu_layout.c
 |   (MENU_DIGIT_COLS), options.c (options_save), app_state.h (g_toggle_btn,
 |   g_cmd_iface, IFACE_PANEL/IFACE_KEYBOARD), saturn_keyboard.h
 | Globals: g_face_btn, g_chord_slot, g_toggle_btn, g_cmd_iface, g_kbd_visible
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
    int s_toggle = g_toggle_btn;
    int s_iface  = g_cmd_iface;
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
    const int R_IFACE = 0;
    int sel = 0;
    bool switched = false;
    for (;;) {
        CtlView v = ctl_view();
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
            g_toggle_btn = s_toggle;
            g_cmd_iface  = s_iface;
            break;
        }
        if (done) { options_save(); break; }
        if (up)   sel = (sel - 1 + v.nrows) % v.nrows;
        if (down) sel = (sel + 1) % v.nrows;
        int drow = 0;
        if (menu_digit_row(ke, v.nassign, drow, left, right)) { sel = drow + 1; act = true; }
        if (sel == v.r_done)  { if (act) { options_save(); break; } }
        else if (sel == v.r_cancel) { if (act) {
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            g_toggle_btn = s_toggle;
            g_cmd_iface  = s_iface;
            break; } }
        else if (sel == v.r_reset) { if (act) mapping_reset_defaults(); }
        else if (sel == v.r_caps) { if (left || right || act) keyboard_set_caps(!keyboard_get_caps()); }
        else if (sel == v.r_toggle) { if (left || right || act) g_toggle_btn = 1 - g_toggle_btn; }
        else if (sel == R_IFACE) {
            if (left  && g_cmd_iface > IFACE_KEYBOARD) g_cmd_iface--;
            if (right && g_cmd_iface < IFACE_PANEL)    g_cmd_iface++;
        }
        else if (left || right) {
            const CtlRow &r = v.rows[sel - 1];
            if (r.kind == CK_FACE) {
                int n = right ? (g_face_btn[r.idx] + 1) % FA_BTN_N
                              : (g_face_btn[r.idx] + FA_BTN_N - 1) % FA_BTN_N;
                face_assign(r.idx, n);
            } else {
                int n = right ? (g_chord_slot[r.idx] + 1) % SL_N
                              : (g_chord_slot[r.idx] + SL_N - 1) % SL_N;
                chord_assign(r.idx, n);
            }
        }

        v = ctl_view();
        menu_clear();
        int fx, fy, fw, fh;
        menu_box_fit("CONTROLS", 36, CTL_BLOCK_ROWS + 11, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        /* One row higher than every other page's content start, so the Interface
           row and its description line together end where a single row would
           have: the swapped block below opens on the same line either way. */
        int x = fx + 2, y = fy + 3;
        bool nums = !g_kbd_visible;
        const int vx = x + 20 + MENU_DIGIT_COLS;
        const int lx = x + 2 + MENU_DIGIT_COLS;
        text_print(x, y++, "%c    Interface:  %s %s %s", sel == R_IFACE ? '>' : ' ',
                           g_cmd_iface > IFACE_KEYBOARD ? "<" : " ", INAMES[g_cmd_iface],
                           g_cmd_iface < IFACE_PANEL ? ">" : " ");
        text_print(x + 4, y++, "%s", IDESC[g_cmd_iface]);
        y++;
        const int block_y = y;
        for (int i = 0; i < v.nassign; i++) {
            const CtlRow &r = v.rows[i];
            const char *label = r.kind == CK_FACE
                              ? (v.panel ? PANEL_FACE_LABEL[r.idx] : FACE_LABEL[r.idx])
                              : CHORD_LABEL[r.idx];
            char cur = sel == i + 1 ? '>' : ' ';
            if (nums) text_print(x, y, "%c %c) %s", cur, menu_row_digit_char(i), label);
            else      text_print(x, y, "%c    %s", cur, label);
            text_print(vx, y++, "%s", r.kind == CK_FACE ? face_btn_name(r.idx)
                                                        : slot_name(g_chord_slot[r.idx]));
        }
        if (v.panel) {
            text_print(lx, y, "Cycle Module");
            text_print(vx, y++, "L/R (fixed)");
        }
        text_print(lx, y, "Caps Toggle");
        text_print(vx, y++, "L+R (fixed)");
        y = block_y + CTL_BLOCK_ROWS;
        text_print(x, y++, "%c    Keyboard Caps: %s", sel == v.r_caps ? '>' : ' ',
                           keyboard_get_caps() ? "On" : "Off");
        text_print(x, y++, "%c    Panel/Keyboard Swap: %s", sel == v.r_toggle ? '>' : ' ',
                           g_toggle_btn == 1 ? "Y" : "Z");
        y++;
        text_print(x, y++, "%c    Reset to Defaults", sel == v.r_reset ? '>' : ' ');
        text_print(x, y++, "%c    Ok", sel == v.r_done ? '>' : ' ');
        text_print(x, y++, "%c    Cancel", sel == v.r_cancel ? '>' : ' ');
        y++;
        text_print(x, y++, "%s", hint("A/C/Start=Ok  B=Cancel",
                                             "Enter/Esc=Ok  Bksp=Cancel"));
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
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_BACKSPACE;
        bool done = g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ESCAPE;
        if (back) {
            keyboard_set_insert(s_ins); keyboard_set_caps(s_caps);
            keyboard_set_num(s_num); keyboard_set_scrolllock(s_scrl);
            break;
        }
        if (done) { options_save(); break; }
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
        text_print(x, y++, "%s", hint("A/C/Start=Ok  B=Cancel", "Enter/Esc=Ok  Bksp=Cancel"));
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
