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
           two rows once a real keyboard hides it. */
        menu_box_fit("NETWORK", 24, g_kbd_visible ? 15 : 10, &fx, &fy, &fw, &fh);
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
                text_print(npx, y, rowbuf);
                if (arow < 0 && r == k.cursor_row && np_valid(r, k.cursor_col)) {
                    char one[2] = { np_char(r, k.cursor_col), '\0' };
                    text_print_hl(npx + k.cursor_col * 2, y, one);
                }
                y++;
            }
            y++;
        }
        menu_row(fx, fw, y++, arow == 0, DIAL_ROW_W, "Dial");
        menu_row(fx, fw, y++, arow == 1, DIAL_ROW_W, "Controls");
        if (err[0]) menu_text(fx, fw, y, 0, err);
        y++;
        y++;
        menu_text(fx, fw, y, 0,
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
    // One bar width for every selectable row, and one label field inside the
    // assignment block, so the centred rows keep a single left edge and a
    // single value column. CTL_ROW_W is the box's content width; CTL_IFACE_W is
    // the wider of INAMES, so the Interface slider's arrows do not move when
    // its value does.
    const int CTL_ROW_W   = 36;
    const int CTL_LABEL_W = 20;
    const int CTL_IFACE_W = 13;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    bool started_kbd = g_kbd_visible;
    int s_face[FA_N], s_chord[CA_N];
    int s_toggle = g_toggle_btn;
    int s_iface  = g_cmd_iface;
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
    const int R_IFACE = 0;
    // Held across visits so returning from Options lands on the row the player
    // was configuring. Clamped rather than trusted, because the row count moves
    // with the interface: a Command Panel view lists one more binding than a
    // keyboard one, and switching interface between visits would otherwise
    // leave the cursor past the end, where none of the sel == v.r_* tests match.
    static int last_sel = 0;
    int sel = last_sel;
    bool switched = false;
    for (;;) {
        CtlView v = ctl_view();
        if (sel < 0 || sel >= v.nrows) sel = 0;
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
        last_sel = sel;   // after every move, so no exit path has to remember to
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
        menu_box_fit("CONTROLS", CTL_ROW_W, CTL_BLOCK_ROWS + 9, &fx, &fy, &fw, &fh);
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        /* One row higher than every other page's content start, so the Interface
           row and its description line together end where a single row would
           have: the swapped block below opens on the same line either way. */
        int y = fy + 3;
        bool nums = !g_kbd_visible;
        menu_rowf(fx, fw, y++, sel == R_IFACE, CTL_ROW_W, "   Interface:  %s %s %s",
                  g_cmd_iface > IFACE_KEYBOARD ? "<" : " ",
                  menu_pad(INAMES[g_cmd_iface], CTL_IFACE_W),
                  g_cmd_iface < IFACE_PANEL ? ">" : " ");
        menu_text(fx, fw, y++, 0, IDESC[g_cmd_iface]);
        y++;
        const int block_y = y;
        for (int i = 0; i < v.nassign; i++) {
            const CtlRow &r = v.rows[i];
            const char *label = r.kind == CK_FACE
                              ? (v.panel ? PANEL_FACE_LABEL[r.idx] : FACE_LABEL[r.idx])
                              : CHORD_LABEL[r.idx];
            menu_rowf(fx, fw, y++, sel == i + 1, CTL_ROW_W, "%s%s%s",
                      menu_num(nums, i), menu_pad(label, CTL_LABEL_W),
                      r.kind == CK_FACE ? face_btn_name(r.idx)
                                        : slot_name(g_chord_slot[r.idx]));
        }
        if (v.panel)
            menu_rowf(fx, fw, y++, 0, CTL_ROW_W, "   %sL/R (fixed)",
                      menu_pad("Cycle Module", CTL_LABEL_W));
        menu_rowf(fx, fw, y++, 0, CTL_ROW_W, "   %sL+R (fixed)",
                  menu_pad("Caps Toggle", CTL_LABEL_W));
        y = block_y + CTL_BLOCK_ROWS;
        menu_rowf(fx, fw, y++, sel == v.r_caps, CTL_ROW_W, "   Keyboard Caps: %s",
                  keyboard_get_caps() ? "On" : "Off");
        menu_rowf(fx, fw, y++, sel == v.r_toggle, CTL_ROW_W, "   Panel/Keyboard Swap: %s",
                  g_toggle_btn == 1 ? "Y" : "Z");
        y++;
        menu_row(fx, fw, y++, sel == v.r_reset,  CTL_ROW_W, "   Reset to Defaults");
        menu_row(fx, fw, y++, sel == v.r_done,   CTL_ROW_W, "   Ok");
        menu_row(fx, fw, y++, sel == v.r_cancel, CTL_ROW_W, "   Cancel");
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
 |   picture. Dynamic is unreachable in this build anyway (display_image_count()
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
    static const char *const DESC[]  = { "Typeahead by Walkthrough",
                                         "Typeahead by Valid Words",
                                         "Typeahead Off" };
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
 | netbin_pause_menu
 | Description: See netbin_pages.h.
 | Author: suinevere
 | Dependencies: menu.c, menu_layout.c, console_view.c, input.c,
 |   saturn_keyboard.h, soft_reset.h (check_soft_reset,
 |   confirm_return_to_title)
 | Globals: g_kbd_visible, g_menu_page_fade
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void netbin_pause_menu(void) {
    MenuBacking backing;
    enum { PI_RESUME, PI_DISPLAY, PI_GAMEPLAY, PI_CONTROLS, PI_RESTART, PI_N };
    static const char *const LABEL[PI_N] = {
        "Resume", "Display", "Gameplay", "Controls", "Restart"
    };

    int label_w = 0;
    for (int i = 0; i < PI_N; i++) {
        int n = 0;
        while (LABEL[i][n]) n++;
        if (n > label_w) label_w = n;
    }
    const int PM_ROW_W = MENU_DIGIT_COLS + label_w;

    int x0, y0, w, h;
    menu_box_fit("PAUSED", 18, PI_N + 2, &x0, &y0, &w, &h);

    static int last_sel = 0;   // held across visits; the five rows never change
    int sel = last_sel;
    menu_sync();   // not a bare Synchronize: this frame must keep claiming NBG2
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + PI_N) % PI_N;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % PI_N;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool act = menu_digit_row(ke, PI_N, sel, left, right)
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || g_pad->WasPressed(Button::START)
                  || ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE;
        last_sel = sel;   // every frame, so no exit path has to remember to
        if (back) break;
        if (act) {
            if (sel == PI_RESUME) break;   // exactly what backing out does
            else if (sel == PI_DISPLAY)  { page_fade_out(g_menu_page_fade); display_options_page(); menu_clear(); need_fade_in = true; }
            else if (sel == PI_GAMEPLAY) { page_fade_out(g_menu_page_fade); gameplay_page(); need_fade_in = true; }
            else if (sel == PI_CONTROLS) { page_fade_out(g_menu_page_fade); controls_dispatch(); menu_clear(); need_fade_in = true; }
            // Never returns if accepted: confirm_return_to_title longjmps to
            // main()'s dial loop, which resets the backing depth and the
            // menu service this page was opened under.
            else if (sel == PI_RESTART)  { confirm_return_to_title("hang up and reboot back to the dial page?"); }
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "PAUSED");
        bool nums = !g_kbd_visible;
        int ay = y0 + 4;
        for (int i = 0; i < PI_N; i++)
            menu_rowf(x0, w, ay++, i == sel, PM_ROW_W, "%s%s",
                      menu_num(nums, i), LABEL[i]);
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
}
