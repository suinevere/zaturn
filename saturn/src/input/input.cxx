/*----------------------
 | input.cxx
 | Description: Implements the controller mapping tables, the MultiPad/g_pad
 |   aggregate pad declared in input.h, gamepad auto-repeat and shift-chord
 |   hold-repeat, gamepad- and keyboard-driven console scrollback, and shell-style
 |   command history recall.
 | Author: suinevere
 | Dependencies: input.h
 ----------------------*/

#include "input.h"
#include "controller.h"

/*----------------------
 | g_pad
 | Description: The aggregate pad declared in input.h, pointing at the client's
 |   MultiPad; every helper here reads controller state through it.
 | Author: suinevere
 ----------------------*/
MultiPad *g_pad = nullptr;

/*----------------------
 | g_face_btn / g_chord_slot
 | Description: The live controller mapping: which physical button (0=A,1=B,2=C)
 |   each face action uses, and which shift-chord slot each chord action uses.
 |   Edited by the Controls page; persisted in MOJOOPTS.
 | Author: suinevere
 ----------------------*/
int g_face_btn[FA_N]   = { 0, 1, 2, 4, 5 };
int g_chord_slot[CA_N] = { SL_NONE, SL_NONE, SL_CLR, SL_CUD, SL_NONE };

/*----------------------
 | FACE_DEFAULT / CHORD_DEFAULT / CHORD_BTN_DEFAULT
 | Description: The factory mappings, copied back over g_face_btn/g_chord_slot/
 |   mapping_reset_defaults. Only the two things a reader reaches
 |   for start bound: the line above and below on the modifier plus Up/Down, and
 |   the top and bottom of the scrollback on Left/Right. Page, Recall and
 |   Autocomplete start Unset -- a page at a time is what
 |   holding the line chord already does, and the other two are conveniences that
 |   cost a gesture a player has not asked for.
 | Author: suinevere
 ----------------------*/
static const int FACE_DEFAULT[FA_N]  = { 0, 1, 2, 4, 5 };
static const int CHORD_DEFAULT[CA_N] = { SL_NONE, SL_NONE, SL_CLR, SL_CUD, SL_NONE };

/*----------------------
 | SCROLL_PAGE / SCROLL_ALL
 | Description: Scrollback deltas: one screen page, and a sentinel large enough to
 |   mean "scroll all the way to the top" (clamped by the renderer).
 | Author: suinevere
 ----------------------*/
static const int SCROLL_PAGE = 16;
static const int SCROLL_ALL  = 1 << 30;

/*----------------------
 | scroll_handle_key
 | Description: A switch over the physical-keyboard nav keys, translating each
 |   into a g_scroll delta or absolute value. Up/Down and Ctrl+Up/Down both scroll
 |   one line: whichever pair ScrollLock has NOT assigned to history recall is the
 |   pair that reaches here. Left/Right are matched but left a no-op: they used to
 |   move the on-screen keyboard cursor and are now consumed here so they don't
 |   fall through and get typed as text.
 | Author: suinevere
 | Dependencies: app_state.h, saturn_keyboard.h
 | Globals: g_scroll
 | Params: ke -- the keyboard event to test
 | Returns: true if `ke` was a nav key (and thus consumed); false otherwise
 ----------------------*/
bool scroll_handle_key(const SaturnKeyEvent &ke) {
    switch (ke.kind) {
        case SATURN_KEY_UP:
        case SATURN_KEY_CTRL_UP:   g_scroll += 1;           return true;
        case SATURN_KEY_DOWN:
        case SATURN_KEY_CTRL_DOWN: g_scroll -= 1;           return true;
        case SATURN_KEY_PAGEUP:   g_scroll += SCROLL_PAGE; return true;
        case SATURN_KEY_PAGEDOWN: g_scroll -= SCROLL_PAGE; return true;
        case SATURN_KEY_HOME:     g_scroll  = SCROLL_ALL;  return true;
        case SATURN_KEY_END:      g_scroll  = 0;           return true;
        case SATURN_KEY_LEFT:
        case SATURN_KEY_RIGHT:                             return true;
        default:                                           return false;
    }
}

/*----------------------
 | PAD_SCROLL_DELAY / PAD_SCROLL_RATE
 | Description: Chord hold-repeat timing in frames: the initial delay before a
 |   held shift-chord repeats, then the faster repeat interval.
 | Author: suinevere
 ----------------------*/
static const int PAD_SCROLL_DELAY = 30;
static const int PAD_SCROLL_RATE  = 4;

/*----------------------
 | face_button
 | Description: Indexes a fixed {A,B,C,Y} table by the currently-mapped button
 |   number for face-action `action`. The fourth button is Y, not X, because
 |   controls.xls puts Space there; the stored numbers are indices into this table,
 |   so a save written before the change now reads its slot-3 action as Y.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE/FA_SPACE
 | Returns: the Button currently assigned to that action
 ----------------------*/
Button face_button(int action) {
    static const Button BTN[FA_BTN_N] = { Button::A, Button::B, Button::C,
                                          Button::X, Button::Y, Button::R };
    return BTN[g_face_btn[action]];
}

/*----------------------
 | face_btn_name
 | Description: Indexes a fixed name table by the currently-mapped button number
 |   for face-action `action`. Returned unpadded: the Controls page aligns its
 |   value column by padding the LABEL field beside it (menu_pad), so a value
 |   padded here would only add trailing space inside the highlight.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE/FA_SPACE
 | Returns: "A", "B", "C" or "Y"
 ----------------------*/
const char *face_btn_name(int action) {
    static const char *N[FA_BTN_N] = { "A", "B", "C", "X", "Y", "R" };
    return N[g_face_btn[action]];
}

/*----------------------
 | slot_name
 | Description: Indexes a fixed display-string table by slot.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of the SL_* slot constants
 | Returns: the slot's display string
 ----------------------*/
const char *slot_name(int slot) {
    static char buf[SL_N][16];
    static const char *SUFFIX[SL_N] = { "", "+Up/Dn", "+Left/Right", "+L/R" };
    if (slot <= SL_NONE || slot >= SL_N) return "Unset";
    const char *c = chord_btn_name();
    char *b = buf[slot];
    int n = 0;
    while (*c && n < 14) b[n++] = *c++;
    for (const char *t = SUFFIX[slot]; *t && n < 15; t++) b[n++] = *t;
    b[n] = 0;
    return b;
}

/*----------------------
 | chord_mod_held
 | Description: See input.h. Null-guarded because it is read from the renderer,
 |   which runs on frames the input side has not claimed a pad on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: true while the modifier is held
 ----------------------*/
bool chord_mod_held(void) {
    return g_pad != nullptr && g_pad->IsHeld(chord_btn_button());
}

/*----------------------
 | chord_btn_name
 | Description: See input.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: N/A
 | Returns: the modifier's display name
 ----------------------*/
const char *chord_btn_name(void) {
    return face_btn_name(FA_CHORD);
}

/*----------------------
 | chord_btn_button
 | Description: See input.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: N/A
 | Returns: the modifier as a Button
 ----------------------*/
Button chord_btn_button(void) {
    return face_button(FA_CHORD);
}

/*----------------------
 | slot_raw
 | Description: Reads g_pad directly for the modifier/direction pairs (Z, Y, X,
 |   L/R, D-pad) and switches on `slot` to return its raw held direction this
 |   frame. Trigger slots (the "t" suffix) return 0 when both L+R are held, since
 |   that combo is reserved for the caps toggle; the plain L/R slot returns 0
 |   under any shift (Z or Y) so it never collides with the shifted trigger
 |   slots. Only called from chord_tick, so it stays file-local.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: slot -- one of the SL_* slot constants
 | Returns: -1 (L/Up/Left), +1 (R/Down/Right), or 0 (idle / shift mismatch)
 ----------------------*/
static int slot_raw(int slot) {
    Button c = chord_btn_button();
    if (slot <= SL_NONE || slot >= SL_N || !g_pad->IsHeld(c)) return 0;
    bool up = g_pad->IsHeld(Button::Up),   dn = g_pad->IsHeld(Button::Down);
    bool lt = g_pad->IsHeld(Button::Left), rt = g_pad->IsHeld(Button::Right);
    bool l  = g_pad->IsHeld(Button::L),    r  = g_pad->IsHeld(Button::R);
    switch (slot) {
        case SL_CUD: return up ? -1 : dn ? 1 : 0;
        case SL_CLR: return lt ? -1 : rt ? 1 : 0;
        /* The modifier cannot be its own direction: under the default R that
           leaves only R+L, which counts as the previous direction. */
        case SL_CT:  if (c == Button::L) return r ? 1 : 0;
                     if (c == Button::R) return l ? -1 : 0;
                     return l ? -1 : r ? 1 : 0;
    }
    return 0;
}

/*----------------------
 | pad_nav
 | Description: See input.h. The pad's edge first, so a pad and a wheel plugged in
 |   together still behave like a pad.
 | Author: suinevere
 | Dependencies: controller.h
 | Globals: g_pad
 | Params: b -- one of the four direction buttons
 | Returns: true on the frame that direction fires
 ----------------------*/
bool pad_nav(Button b) {
    if (g_pad->WasPressed(b)) return true;
    if (b == Button::Up)    return controller_nav_fired(NAV_UP)    != 0;
    if (b == Button::Down)  return controller_nav_fired(NAV_DOWN)  != 0;
    if (b == Button::Left)  return controller_nav_fired(NAV_LEFT)  != 0;
    if (b == Button::Right) return controller_nav_fired(NAV_RIGHT) != 0;
    return false;
}

/*----------------------
 | g_mtog_was
 | Description: mode_combo_fired's held latch, file-scope rather than
 |   function-local so mode_toggle_reset can clear it from outside.
 | Author: suinevere
 ----------------------*/
static bool g_mtog_was = false;

/*----------------------
 | mode_combo_fired
 | Description: Latches held state in g_mtog_was, comparing it to the current
 |   L+R-without-shift read so only the rising edge reports true. L and R are
 |   tested together deliberately: each alone already has a job in both
 |   interfaces, and only the pair is free.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad, g_mtog_was
 | Params: N/A
 | Returns: true on the frame the combo first becomes held
 ----------------------*/
bool mode_combo_fired(void) {
    bool now = g_pad->IsHeld(Button::L) && !g_pad->IsHeld(chord_btn_button());
    bool fired = now && !g_mtog_was;
    g_mtog_was = now;
    return fired;
}

/*----------------------
 | mode_toggle_reset
 | Description: Zeroes g_mtog_was, discarding whatever edge mode_combo_fired was
 |   mid-tracking. See input.h for why this has to be a hard discard rather than a
 |   "only count presses observed here" flag: the press edge was legitimately
 |   observed, before the modal that is now returning ever opened; the fix is to
 |   forget it entirely, not to gate it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mtog_was
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mode_toggle_reset(void) {
    g_mtog_was = false;
}

/*----------------------
 | ChordRep / g_chordrep
 | Description: Per-slot edge + hold-repeat state (held direction, countdown
 |   timer, and whether it fired this frame), ticked once per input frame by
 |   chord_tick.
 | Author: suinevere
 ----------------------*/
struct ChordRep { int dir; int timer; bool fired; };
static ChordRep g_chordrep[SL_N];

/*----------------------
 | g_tap_held / g_tap_used / g_tap_fired
 | Description: The modifier's tap detector: whether it was held last frame,
 |   whether any direction was taken while it was, and whether the release
 |   completed a tap. Measured on the release rather than on the press because a
 |   chord begins with the same press -- fired on the press, every scroll the
 |   player asked for would be preceded by one they did not. A tap is the Down
 |   half of the line chord, not the Up half: the reader who taps is following the
 |   text, and the direction that follows it is the one that has to be reachable
 |   without a second button.
 | Author: suinevere
 ----------------------*/
static bool g_tap_held  = false;
static bool g_tap_used  = false;
static bool g_tap_fired = false;

/*----------------------
 | g_chord_ticked
 | Description: Whether chord_tick has run since the last time anyone asked. The
 |   chord state is a per-frame result, and the screens that do not tick it -- every
 |   menu, and the title -- would otherwise read whatever the last screen that did
 |   left behind.
 | Author: suinevere
 ----------------------*/
static bool g_chord_ticked = false;

/*----------------------
 | chord_ticked
 | Description: Whether chord_tick ran this frame, clearing the flag as it reports
 |   so each frame answers once. A caller that reads chord_fired without this is
 |   reading a stale frame's result on any screen that does not tick.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_ticked
 | Params: N/A
 | Returns: true if chord_tick has run since the last call
 ----------------------*/
bool chord_ticked(void);

/*----------------------
 | chord_tick
 | Description: For each real slot, reads slot_raw and compares it to the slot's
 |   stored direction: idle resets the timer and clears fired; a changed direction
 |   fires immediately and arms the PAD_SCROLL_DELAY timer; an unchanged held
 |   direction fires again each time the timer counts down to 0, then rearms at the
 |   faster PAD_SCROLL_RATE. SL_NONE is skipped -- it is the absence of a gesture,
 |   and giving it a repeat timer would let every switched-off action fire at once.
 |     Also runs the modifier's tap detector: a press that is released without any
 |   direction having been taken under it is one line forward.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
bool chord_ticked(void) {
    bool r = g_chord_ticked;
    g_chord_ticked = false;
    return r;
}

void chord_tick(void) {
    g_chord_ticked = true;
    bool any = false;
    for (int s = SL_NONE + 1; s < SL_N; s++) {
        int d = slot_raw(s);
        ChordRep &r = g_chordrep[s];
        if (d != 0) any = true;
        if (d == 0)              { r.dir = 0; r.timer = 0; r.fired = false; }
        else if (d != r.dir)     { r.dir = d; r.timer = PAD_SCROLL_DELAY; r.fired = true; }
        else if (--r.timer <= 0) { r.timer = PAD_SCROLL_RATE; r.fired = true; }
        else                     { r.fired = false; }
    }
    bool held = g_pad->IsHeld(chord_btn_button());
    if (held && any)          g_tap_used = true;
    if (held && !g_tap_held)  g_tap_used = any;
    if (!held && g_tap_held && !g_tap_used) g_tap_fired = true;
    g_tap_held = held;
}

/*----------------------
 | chord_fired
 | Description: Looks up the slot currently mapped to `action` and checks
 |   chord_tick's per-slot fired flag and direction against `dir`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: action -- one of the CA_* constants; dir -- -1 or +1
 | Returns: true if that action's slot fired in that direction this frame
 ----------------------*/
bool chord_fired(int action, int dir) {
    if (g_chord_slot[action] == SL_NONE) return false;
    const ChordRep &r = g_chordrep[g_chord_slot[action]];
    return r.fired && r.dir == dir;
}

/*----------------------
 | chord_tap_fired
 | Description: See input.h. Cleared by the read, so the frame that acts on a tap
 |   is the only one that sees it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_tap_fired
 | Params: N/A
 | Returns: true once per completed tap
 ----------------------*/
bool chord_tap_fired(void) {
    bool r = g_tap_fired;
    g_tap_fired = false;
    return r;
}

/*----------------------
 | pad_scroll_update
 | Description: Six chord_fired checks (Line +/-, Home/End, Page +/-) against the
 |   CA_LINE/CA_HOMEEND/CA_PAGE actions, each adjusting g_scroll. Depends on
 |   chord_tick having run this frame; the on-screen keyboard cursor moves on the
 |   plain D-pad (no shift) elsewhere, in the typeahead-editing input handler.
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_scroll_update(void) {
    if (chord_tap_fired())           g_scroll -= 1;
    if (chord_fired(CA_LINE,    -1)) g_scroll += 1;
    if (chord_fired(CA_LINE,    +1)) g_scroll -= 1;
    if (chord_fired(CA_HOMEEND, -1)) g_scroll  = SCROLL_ALL;
    if (chord_fired(CA_HOMEEND, +1)) g_scroll  = 0;
    if (chord_fired(CA_PAGE,    -1)) g_scroll += SCROLL_PAGE;
    if (chord_fired(CA_PAGE,    +1)) g_scroll -= SCROLL_PAGE;
}

/*----------------------
 | PAD_REPEAT_DELAY / PAD_REPEAT_RATE
 | Description: Button auto-repeat timing in frames: the editing buttons (D-pad,
 |   C, B, Y, L, R) repeat while held like a real keyboard -- the initial delay,
 |   then the faster repeat interval.
 | Author: suinevere
 ----------------------*/
#define PAD_REPEAT_DELAY 30
#define PAD_REPEAT_RATE  4

/*----------------------
 | PadRepeat / g_padrep
 | Description: Per-button auto-repeat state (button, countdown timer, fired-this-
 |   frame). pad_repeat_update ticks every entry once per frame; pad_fired then
 |   reports the initial press plus each repeat tick.
 | Author: suinevere
 ----------------------*/
struct PadRepeat { Button btn; int timer; bool fired; };
static PadRepeat g_padrep[] = {
    { Button::Up, 0, false }, { Button::Down, 0, false },
    { Button::Left, 0, false }, { Button::Right, 0, false },
    { Button::L, 0, false }, { Button::R, 0, false },
    { Button::A, 0, false }, { Button::C, 0, false },
    { Button::B, 0, false }, { Button::X, 0, false },
};

/*----------------------
 | pad_repeat_update
 | Description: For each tracked button: released resets its timer and clears
 |   fired; first held frame fires immediately and arms PAD_REPEAT_DELAY; held
 |   frames after that fire again each time the timer counts down to 0, then
 |   rearm at the faster PAD_REPEAT_RATE.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_repeat_update(void) {
    for (auto &r : g_padrep) {
        if (!g_pad->IsHeld(r.btn))      { r.timer = 0; r.fired = false; }
        else if (r.timer == 0)          { r.fired = true;  r.timer = PAD_REPEAT_DELAY; }
        else if (--r.timer <= 0)        { r.fired = true;  r.timer = PAD_REPEAT_RATE; }
        else                            { r.fired = false; }
    }
}

/*----------------------
 | pad_fired
 | Description: Looks `b` up in g_padrep; if tracked, returns its repeat-aware
 |   fired flag, otherwise falls back to a plain WasPressed edge -- except that the
 |   four directions report nothing while a device has its Mouse row pointed at its
 |   D-pad, so the D-pad does one job at a time. Menus are unaffected: they read the
 |   pad through pad_nav and still need the D-pad to navigate. The map is the other
 |   exception and reads the directions itself.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: b -- the button to check
 | Returns: true if it fired (pressed or repeated) this frame
 ----------------------*/
bool pad_fired(Button b) {
    if (controller_dpad_is_cursor() &&
        (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right))
        return false;
    return pad_fired_raw(b);
}

/*----------------------
 | pad_fired_raw
 | Description: pad_fired without the cursor gate, for the one screen that wants
 |   the D-pad whatever the cursor is doing: the map owns the whole display and has
 |   a crosshair of its own to steer.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: b -- the button to check
 | Returns: true if it fired (pressed or repeated) this frame
 ----------------------*/
bool pad_fired_raw(Button b) {
    for (auto &r : g_padrep) if (r.btn == b) return r.fired;
    return g_pad->WasPressed(b);
}

/*----------------------
 | HISTORY_MAX / g_history / g_hist_count / g_hist_head / g_hist_browse
 | Description: The command-history ring (Up/Down recall previously entered
 |   commands, shell-style): the fixed-capacity buffer, how many entries are
 |   valid, the write head, and the current browse offset (-1 = not browsing).
 | Author: suinevere
 ----------------------*/
#define HISTORY_MAX 16
static char g_history[HISTORY_MAX][KB_INPUT_MAX];
static int  g_hist_count  = 0;
static int  g_hist_head   = 0;
static int  g_hist_browse = -1;

/*----------------------
 | history_push
 | Description: Resets browsing to -1 first (so the next Up starts from the
 |   newest entry). Returns early on a blank line, or on a line that matches the
 |   most-recently-stored entry character-for-character (dedupes consecutive
 |   repeats). Otherwise copies it into the ring buffer at g_hist_head and
 |   advances the head, growing g_hist_count up to HISTORY_MAX.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the command line just submitted
 | Returns: N/A
 ----------------------*/
void history_push(const char *s) {
    g_hist_browse = -1;
    if (s == nullptr || s[0] == '\0') return;
    if (g_hist_count > 0) {
        int last = (g_hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        int i = 0; while (s[i] && g_history[last][i] && s[i] == g_history[last][i]) i++;
        if (s[i] == '\0' && g_history[last][i] == '\0') return;
    }
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { g_history[g_hist_head][n] = s[n]; n++; }
    g_history[g_hist_head][n] = '\0';
    g_hist_head = (g_hist_head + 1) % HISTORY_MAX;
    if (g_hist_count < HISTORY_MAX) g_hist_count++;
}

/*----------------------
 | history_load
 | Description: Computes the ring-buffer index g_hist_browse steps back from the
 |   newest entry (mod-wrapped through HISTORY_MAX*2 to stay positive) and copies
 |   that string into k->input, placing the caret at its end.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to overwrite
 | Returns: N/A
 ----------------------*/
/*----------------------
 | history_entry
 | Description: The ring slot g_hist_browse steps back from the newest,
 |   mod-wrapped through HISTORY_MAX*2 to stay positive. File-local because the
 |   browse offset it reads is file-local; callers outside go through
 |   history_load or history_recall_text.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_history, g_hist_head, g_hist_browse
 | Params: N/A
 | Returns: the selected history string
 ----------------------*/
static const char *history_entry(void) {
    return g_history[(g_hist_head - 1 - g_hist_browse + HISTORY_MAX * 2) % HISTORY_MAX];
}

void history_load(KeyboardState *k) {
    const char *s = history_entry();
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { k->input[n] = s[n]; n++; }
    k->input[n] = '\0';
    k->input_len = n;
    k->cursor = n;
}

/*----------------------
 | history_recall
 | Description: older != 0 steps the browse offset one further back (toward
 |   older commands) if more remain, then loads it. older == 0 steps one closer
 |   to the newest; once browsing hits -1 (nothing older than the newest is
 |   selected), it clears the input line instead of loading anything. No-op when
 |   history is empty.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to update; older -- nonzero for older, zero for newer
 | Returns: N/A
 ----------------------*/
void history_recall(KeyboardState *k, int older) {
    const char *s = history_recall_text(older);
    if (s == nullptr) return;
    int n = 0; while (s[n] && n < KB_INPUT_MAX - 1) { k->input[n] = s[n]; n++; }
    k->input[n] = '\0';
    k->input_len = n;
    k->cursor = n;
}

/*----------------------
 | history_recall_text
 | Description: The stepping half of history_recall, split out so the command
 |   panel can browse the same ring without a KeyboardState to write into --
 |   it keeps its command in CommandPanel::line, not in the input line, so
 |   sharing the browse position rather than the destination is what makes Up
 |   and Down walk one history in both interfaces.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_hist_count, g_hist_browse
 | Params: older -- nonzero to step toward older commands, zero toward newer
 | Returns: the entry now selected; "" when stepping past the newest (the
 |   caller should clear its line); nullptr when nothing moved, which is an
 |   empty history or an end already reached
 ----------------------*/
const char *history_recall_text(int older) {
    if (g_hist_count == 0) return nullptr;
    if (older) {
        if (g_hist_browse >= g_hist_count - 1) return nullptr;
        g_hist_browse++;
        return history_entry();
    }
    if (g_hist_browse > 0) { g_hist_browse--; return history_entry(); }
    g_hist_browse = -1;
    return "";
}

/*----------------------
 | face_assign
 | Description: Scans the other face actions for one that currently holds button
 |   `b`; if found, gives it `a`'s previous button (the swap), then sets `a` to
 |   `b`. Four actions over four buttons, so the result is always a permutation:
 |   no action is ever left without a button and no button ever carries two.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: a -- the face action being reassigned; b -- the button
 |   (0=A, 1=B, 2=C, 3=X) to give it
 | Returns: N/A
 ----------------------*/
void face_assign(int a, int b) {
    for (int o = 0; o < FA_N; o++) if (o != a && g_face_btn[o] == b) g_face_btn[o] = g_face_btn[a];
    g_face_btn[a] = b;
}

/*----------------------
 | chord_group
 | Description: Which configuration group a chord action belongs to. All six are
 |   on the Chords sheet now -- Recall and Autocomplete moved there from Actions,
 |   which is where a reader looking for "what does the modifier do" expects to
 |   find them -- so there is one group and a swap cannot reach a row the player
 |   is not looking at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a -- one of the CA_* constants
 | Returns: CG_CHORDS
 ----------------------*/
int chord_group(int a) {
    (void) a;
    return CG_CHORDS;
}

/*----------------------
 | chord_assign
 | Description: Scans the other chord actions of the same chord_group for one that
 |   currently holds slot `s`; if found, gives it `a`'s previous slot (the swap; a
 |   free spare slot has no owner, so this is skipped and `a` simply moves), then
 |   sets `a` to `s`. Restricting the scan to one group is what makes each
 |   controls.xls sheet its own configuration group: a Scrolling row can no longer
 |   move an Actions row the player is not looking at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: a -- the chord action being reassigned; s -- the SL_* slot to give it
 | Returns: N/A
 ----------------------*/
void chord_assign(int a, int s) {
    /* Unset is not a slot and so has no owner to displace: without this guard,
       giving one action Unset would hand its gesture to whichever other action
       happened to be off, which is the opposite of switching something off. */
    if (s != SL_NONE)
        for (int o = 0; o < CA_N; o++)
            if (o != a && g_chord_slot[o] == s && chord_group(o) == chord_group(a))
                g_chord_slot[o] = g_chord_slot[a];
    g_chord_slot[a] = s;
}

/*----------------------
 | mapping_reset_defaults
 | Description: Copies FACE_DEFAULT into g_face_btn and CHORD_DEFAULT into
 |   g_chord_slot.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn, g_chord_slot
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mapping_reset_defaults(void) {
    for (int a = 0; a < FA_N; a++) g_face_btn[a]   = FACE_DEFAULT[a];
    for (int a = 0; a < CA_N; a++) g_chord_slot[a] = CHORD_DEFAULT[a];
}
