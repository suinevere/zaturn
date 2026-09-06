/*----------------------
 | input.h
 | Description: Controller input for the game loop and the mapping-editor menu
 |   pages: the MultiPad multi-port/multi-family gamepad aggregate and the single
 |   shared g_pad instance every input read goes through; the remappable face-
 |   button (Accept/Backspace/Type/Space) and shift-chord (Autocomplete/Recall/Home-End/
 |   Line/Cursor/Page) mapping tables and their tie-swap assign helpers; gamepad
 |   auto-repeat for the editing buttons and hold-repeat for the shift chords;
 |   gamepad-driven console scrollback and its physical-keyboard counterpart; and
 |   shell-style Up/Down command history recall. Owns no rendering -- callers draw
 |   what these report.
 | Author: suinevere
 | Dependencies: app_state.h, keyboard.h, saturn_keyboard.h, SRL
 ----------------------*/

#ifndef INPUT_H
#define INPUT_H

#include <srl.hpp>
#include "app_state.h"
#include "keyboard.h"
#include "saturn_keyboard.h"

using Button = SRL::Input::Digital::Button;

// Aggregates both hardware controller ports and both pad families (digital and
// analog / 3D control pad) so a controller in port 1 OR port 2 works in any
// configuration. Keeps the WasPressed/IsHeld interface so every call site is
// unchanged. The keyboard is polled separately (saturn_keyboard_poll already
// scans all ports), so a controller in one port + keyboard in the other works.
struct MultiPad {
    SRL::Input::Digital d0, d1;
    SRL::Input::Analog  a0, a1;
    MultiPad() : d0(0), d1(1), a0(0), a1(1) {}
    bool WasPressed(Button b) const {
        return (d0.IsConnected() && d0.WasPressed(b)) ||
               (d1.IsConnected() && d1.WasPressed(b)) ||
               (a0.IsConnected() && a0.WasPressed(b)) ||
               (a1.IsConnected() && a1.WasPressed(b));
    }
    bool IsHeld(Button b) const {
        return (d0.IsConnected() && d0.IsHeld(b)) ||
               (d1.IsConnected() && d1.IsHeld(b)) ||
               (a0.IsConnected() && a0.IsHeld(b)) ||
               (a1.IsConnected() && a1.IsHeld(b));
    }
    // True on the frame any nav/action button edges down (edge state is not
    // consumed, so this is safe to call alongside the per-button WasPressed calls).
    bool AnyPressed() const {
        return WasPressed(Button::Up)   || WasPressed(Button::Down)  ||
               WasPressed(Button::Left) || WasPressed(Button::Right) ||
               WasPressed(Button::A)    || WasPressed(Button::B)     ||
               WasPressed(Button::C)    || WasPressed(Button::X)     ||
               WasPressed(Button::Y)    || WasPressed(Button::Z)     ||
               WasPressed(Button::L)    || WasPressed(Button::R)     ||
               WasPressed(Button::START);
    }
    // There is deliberately no AnyHeld(). One was added here and had to come
    // straight back out: SRL reads pads active-low -- IsHeld is
    // `(data & button) == 0` -- so a peripheral that is not reporting, which
    // includes the phantom Analog device this struct opens on a port holding a
    // Digital pad, reads as *every button held, forever*, and IsConnected does
    // not screen it out. An "is anything down" built on that is true on every
    // frame of every boot. WasPressed is immune because it needs a transition
    // and a constant never transitions, which is exactly why AnyPressed behaved
    // and AnyHeld did not.
    //
    // A screened level test -- accept a peripheral only while it claims one to
    // three buttons, on the theory that a hand presses a few and a phantom
    // presses all thirteen -- was tried after that and also came out. It is not
    // wrong, it is just not sufficient, and what it was there to rescue does
    // not work anyway: see the box below.
};

/*----------------------
 | g_pad
 | Description: The one shared multi-port gamepad, owned by main() and valid
 |   everywhere from before the game loop onwards.
 | Author: suinevere
 ----------------------*/
extern MultiPad *g_pad;

// ---- configurable controller mapping ---------------------------------------
// Two tied groups the player can remap (Options > Controller > Configure):
//   Group 1 (face buttons): Accept, Backspace/Cancel, Type-letter and Space --
//     always a permutation of {A,B,C,Y}; reassigning one swaps with whoever held
//     that button ("alternate when changed").
//   Group 2 (shift chords): Autocomplete, Recall, Home/End, Line and Page -- each
//     either Unset or in one of three slots held under the chord
//     modifier: chord+Up/Dn, chord+Left/Right, chord+L/R. Reassigning to a used
//     slot swaps; Unset is not a slot and never swaps, so any number of actions
//     can be off at once. The slots are named from the modifier rather than
//     written down, so moving the modifier renames all of them at once.
//   Fixed: L alone swaps the dashboard's Keyboard and Command Panel modes, and Z
//     opens the map.
// Everything reads through face_button()/chord_fired() so both editors honor it.
//
// Space joined group 1 rather than staying the fixed X it was: it is a typing
// button like the other three -- on the on-screen keyboard it enters a space, or
// accepts the showing completion and adds one -- and a player who has moved
// Type-letter off C has no way to reason about why Space alone cannot move.
//
// The group's fourth button is Y, not X: controls.xls puts Space on Y. That frees
// X, so SL_XUD no longer overlaps a typing button, and moves the overlap onto Y,
// where the default Page (Y+Up/Dn) and Home/End (Y+Left/Right) chords now sit
// under the Space button the same way Recall used to sit under X. It also costs
// nothing on the Panel/Keyboard swap, which has moved off the shift buttons
// entirely and onto the fixed L+R combo (mode_combo_fired).
enum { FA_ACCEPT, FA_BACK, FA_TYPE, FA_SPACE, FA_CHORD, FA_N };
enum { CA_AUTO, CA_RECALL, CA_HOMEEND, CA_LINE, CA_PAGE, CA_N };

/*----------------------
 | CG_CHORDS
 | Description: The configuration group a chord action is edited in. There is one
 |   now: every chord lives on the Chords sheet, so a swap has nowhere else to
 |   reach and the group is what says so rather than an unwritten assumption. It is
 |   kept as a group rather than deleted because the sheets were separate once and
 |   the next reader will want to know that the answer is deliberate.
 | Author: suinevere
 ----------------------*/
enum { CG_CHORDS = 0 };

/*----------------------
 | chord_group
 | Description: Which configuration group (CG_CHORDS) chord action
 |   `a` is edited in.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a -- one of the CA_* constants
 | Returns: CG_CHORDS
 ----------------------*/
int chord_group(int a);

/*----------------------
 | FA_BTN_N
 | Description: How many physical buttons the group draws from -- A, B, C, X, Y, R
 |   -- and so the modulus the editors cycle a row's assignment by. One more than
 |   FA_N, so the five actions are an injection into six buttons rather than a
 |   permutation of their own: exactly one button is spare at any time, moving onto
 |   it just moves, and moving onto a used one swaps. Either way no two actions can
 |   hold the same button, which is the whole point of one pool.
 |     Z and L are not in it. They carry Map and the interface swap, which are
 |   fixed, and a pool that could take them would let a binding land on top of one
 |   of the two things a player uses to get out of where they are.
 | Author: suinevere
 ----------------------*/
#define FA_BTN_N 6

// Chord slots, all held under one modifier the player picks (FA_CHORD, R by
// default): SL_CUD is that button plus Up/Down, SL_CLR plus Left/Right, SL_CT
// plus a shoulder trigger. SL_NONE is not a gesture at all -- it is the action
// switched off, which is where Page, Recall and Autocomplete start.
//
// SL_CT is one-directional when the modifier is itself a trigger: R+R cannot be
// held, so under the default modifier only R+L is reachable and it counts as the
// "previous" direction. Pick a modifier that is not a trigger to get both.
//
// The whole vocabulary changed here -- these numbers are persisted by index, and
// a blob written before the change stores the old eight-slot numbering, so the
// mapping block carries a sentinel of its own and an older one is read for its
// face buttons alone. See options.cxx.
enum { SL_NONE, SL_CUD, SL_CLR, SL_CT, SL_N };

/*----------------------
 | chord_btn_name / chord_btn_button
 | Description: The chord modifier as a name for a menu row, and as the Button to
 |   test. It is FA_CHORD's button: the modifier is in the same pool as the four
 |   typing actions, so moving it onto one of theirs displaces that one rather than
 |   quietly sharing a button with it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: N/A
 | Returns: name gives the button's name; button gives the Button
 ----------------------*/
const char *chord_btn_name(void);
Button chord_btn_button(void);

/*----------------------
 | chord_tap_fired
 | Description: Whether the chord modifier was tapped -- pressed and released with
 |   no direction taken under it -- which scrolls back one line. It is the one
 |   thing the shift button does on its own, and it exists because a reader who
 |   only wants the line above should not have to hold a button and press another.
 |   Reading it clears it, like the chord edges themselves.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: true on the frame the tap completes
 ----------------------*/
bool chord_tap_fired(void);

/*----------------------
 | g_face_btn
 | Description: Which of {0=A,1=B,2=C,3=X} each FA_* action fires on; persisted
 |   by main.cxx's options_load/options_save.
 | Author: suinevere
 ----------------------*/
extern int g_face_btn[FA_N];

/*----------------------
 | g_chord_slot
 | Description: Which SL_* slot each CA_* action fires on; persisted by main.cxx's
 |   options_load/options_save.
 | Author: suinevere
 ----------------------*/
extern int g_chord_slot[CA_N];

/*----------------------
 | face_button
 | Description: The A/B/C/X button currently carrying face-action `action`
 |   (FA_ACCEPT/FA_BACK/FA_TYPE/FA_SPACE), per the player's remapping.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE/FA_SPACE
 | Returns: the Button (A, B, C or Y) currently assigned to that action
 ----------------------*/
Button face_button(int action);

/*----------------------
 | face_btn_name
 | Description: Display name ("A"/"B"/"C"/"X") of the button currently carrying
 |   face-action `action`, for the mapping-editor rows and in-game hints.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: action -- one of FA_ACCEPT/FA_BACK/FA_TYPE/FA_SPACE
 | Returns: "A", "B", "C" or "Y"
 ----------------------*/
const char *face_btn_name(int action);

/*----------------------
 | slot_name
 | Description: Display name of a chord slot ("L/R", "Z+Up/Dn", ...), for the
 |   mapping-editor rows.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of the SL_* slot constants
 | Returns: the slot's display string
 ----------------------*/
const char *slot_name(int slot);

/*----------------------
 | pad_nav
 | Description: One frame's menu direction: the pad's own edge on that button, or
 |   the same direction synthesised for a device that has no D-pad to press it
 |   with -- a racing wheel's steering and paddles. Every menu reads its
 |   Up/Down/Left/Right through this rather than through g_pad, which is what makes
 |   one wheel reading reach every page instead of each page growing its own.
 | Author: suinevere
 | Dependencies: controller.h
 | Globals: g_pad
 | Params: b -- Button::Up, ::Down, ::Left or ::Right
 | Returns: true on the frame that direction fires
 ----------------------*/
bool pad_nav(Button b);

/*----------------------
 | mode_combo_fired
 | Description: Reports the rising edge of the fixed L+R (no shift) combo, which
 |   swaps the dashboard between its Keyboard and Command Panel modes -- fires
 |   once per press, not once per held frame. This is the combo that used to
 |   toggle Caps; Caps is now an Options row and has no pad binding at all,
 |   because a modifier the player cannot see the state of is worth less than a
 |   swap they can.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: true on the frame the combo first becomes held
 ----------------------*/
bool mode_combo_fired(void);

/*----------------------
 | mode_toggle_reset
 | Description: Clears mode_combo_fired's held latch. Call after any blocking UI
 |   (a menu, a device/slot picker, any modal that runs its own poll loop and does
 |   not itself call mode_combo_fired) returns to the caller's own frame loop --
 |   L+R can be pressed and released entirely while that modal owned the screen,
 |   and without this the next mode_combo_fired call would see a stale "was held"
 |   and swap interfaces on a press the player spent on something else, or never
 |   made at all.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mode_toggle_reset(void);

/*----------------------
 | chord_tick
 | Description: Advances the per-slot edge/hold-repeat state for every shift-
 |   chord slot; must be called once per input frame before chord_fired.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void chord_tick(void);

/*----------------------
 | chord_ticked
 | Description: Whether chord_tick ran this frame, clearing the flag as it reports.
 |   Anything reading chord_fired from a screen that might not tick -- a menu, the
 |   title -- has to gate on this, or it reads whatever the last screen that did
 |   tick left behind, forever.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: true if chord_tick has run since the last call
 ----------------------*/
bool chord_ticked(void);

/*----------------------
 | chord_fired
 | Description: Whether chord action `action` (one of the CA_* constants) fired
 |   in direction `dir` (-1 or +1) on the frame chord_tick was last called.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: action -- one of the CA_* constants; dir -- -1 or +1
 | Returns: true if that action's slot fired in that direction this frame
 ----------------------*/
bool chord_fired(int action, int dir);

/*----------------------
 | pad_scroll_update
 | Description: Applies the configurable Line/Home-End/Page scrollback chords
 |   (default shift Y) to the console scroll position. Call once per input frame,
 |   after chord_tick.
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: g_scroll
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_scroll_update(void);

/*----------------------
 | pad_repeat_update
 | Description: Advances the auto-repeat timers for the editing buttons (D-pad,
 |   A, C, B, X, L, R) so pad_fired can report both the initial press and each
 |   repeat tick while held. Call once per input frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void pad_repeat_update(void);

/*----------------------
 | pad_fired
 | Description: Whether button `b` fired this frame -- the initial press or an
 |   auto-repeat tick for the tracked editing buttons, a plain edge otherwise.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: b -- the button to check
 | Returns: true if it fired (pressed or repeated) this frame
 ----------------------*/
bool pad_fired(Button b);

/*----------------------
 | pad_fired_raw
 | Description: pad_fired without the cursor gate, for the map -- it owns the whole
 |   display and steers a crosshair of its own, so a D-pad given to the cursor must
 |   still reach it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: b -- the button to check
 | Returns: true if it fired this frame
 ----------------------*/
bool pad_fired_raw(Button b);


/*----------------------
 | scroll_handle_key
 | Description: Routes a physical-keyboard navigation key (arrows, Page Up/Down,
 |   Home/End) to the console scroll position.
 | Author: suinevere
 | Dependencies: app_state.h, saturn_keyboard.h
 | Globals: g_scroll
 | Params: ke -- the keyboard event to test
 | Returns: true if `ke` was a nav key and was consumed; false otherwise
 ----------------------*/
bool scroll_handle_key(const SaturnKeyEvent &ke);

/*----------------------
 | history_push
 | Description: Remembers a submitted command for Up/Down recall. Skips blank
 |   lines and a line identical to the most recently stored one, and ends any
 |   in-progress browsing so the next Up starts from the newest entry again.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the command line just submitted
 | Returns: N/A
 ----------------------*/
void history_push(const char *s);

/*----------------------
 | history_load
 | Description: Copies the history entry at the current browse offset into the
 |   keyboard input line, with the caret placed at its end.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to overwrite
 | Returns: N/A
 ----------------------*/
void history_load(KeyboardState *k);

/*----------------------
 | history_recall
 | Description: Steps the history browse position and loads the resulting entry
 |   into the input line -- older (`older` != 0) moves toward earlier commands,
 |   newer (`older` == 0) moves back toward the freshest, clearing to an empty
 |   line once it steps past the newest.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard state to update; older -- nonzero for older, zero for newer
 | Returns: N/A
 ----------------------*/
void history_recall(KeyboardState *k, int older);

/*----------------------
 | history_recall_text
 | Description: Steps the history browse position and hands back the entry,
 |   instead of writing it into an input line -- what the command panel needs,
 |   since it keeps its command in CommandPanel::line. Shares the browse
 |   position with history_recall, so Up and Down walk one history however the
 |   player switches interface mid-game.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: older -- nonzero to step toward older commands, zero toward newer
 | Returns: the entry now selected; "" when stepping past the newest (clear the
 |   line); nullptr when nothing moved (empty history, or an end reached)
 ----------------------*/
const char *history_recall_text(int older);

/*----------------------
 | chord_shift_held
 | Description: Whether a shift button any chord slot is built on (Z, Y or X) is
 |   currently down, so a cursor can hold still while a chord is being pressed --
 |   the D-pad is both the cursor and the direction half of every chord.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad
 | Params: N/A
 | Returns: true while Z, Y or X is held
 ----------------------*/
bool chord_shift_held(void);

/*----------------------
 | face_assign
 | Description: Assigns face-action `a` to button `b`. If another action already
 |   holds `b`, that action takes over whatever button `a` previously had (a
 |   swap), keeping the four face actions a permutation of {A,B,C,Y}.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn
 | Params: a -- the face action being reassigned; b -- the button
 |   (0=A, 1=B, 2=C, 3=Y) to give it
 | Returns: N/A
 ----------------------*/
void face_assign(int a, int b);

/*----------------------
 | chord_assign
 | Description: Assigns chord action `a` to slot `s`. If another chord action
 |   already holds `s`, that action takes over `a`'s previous slot (a swap);
 |   otherwise (the slot was a free spare) `a` simply moves.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_chord_slot
 | Params: a -- the chord action being reassigned; s -- the SL_* slot to give it
 | Returns: N/A
 ----------------------*/
void chord_assign(int a, int s);

/*----------------------
 | why there is no way to press past a CD load
 | Description: There is no "skip latch" here, and four attempts at one were
 |   taken back out. Anything added in this direction will be the fifth, so this
 |   box is the record of what was tried and what each attempt actually did.
 |
 |   The problem it kept failing to solve: the boot splash holds a logo on
 |   screen while ensure_online_typeahead() runs,
 |   which is several seconds of blocking CD work, and a button pressed during
 |   that does nothing. (The art preload named here alongside it was the other
 |   half of the problem and is gone; the splash's ten-second hold IS polled
 |   now, so what is left unreachable is the trie build alone.) The pad is only readable as a rising edge against the
 |   previous Core::Synchronize's snapshot, and nothing synchronizes inside a
 |   blocking read.
 |
 |   What was tried:
 |     1. An edge-triggered latch polled between units of work. The polls sit a
 |        whole picture-decode apart, so a press and release between two of them
 |        leaves nothing behind.
 |     2. MultiPad::AnyHeld, a level test, so a press did not have to survive
 |        until a poll. Active-low made it true on every frame of every boot
 |        (see the AnyHeld note above), so the splash stopped depending on the
 |        player at all and always took the skipped exit.
 |     3. A progress hook through the typeahead builder, to get more polls. The
 |        polls could not see an edge because the poll itself was not
 |        synchronizing, so all seven compared the same stale snapshot pair.
 |     4. A screened level test (one to three buttons = a hand, thirteen = a
 |        phantom). Honest, but it still only reads at poll instants, and a tap
 |        between two of them is exactly the case that started all this.
 |
 |   The conclusion, and the reason this is closed rather than open: sampling
 |   between units of blocking CD work is not enough resolution to catch a tap,
 |   and there is no finer seam to sample at without abandoning a load part-way,
 |   which would leave a half-filled art cache or a half-built trie -- far worse
 |   than an unresponsive logo. So the splash is skippable only during its
 |   fade-in, before the loads begin, and rides out the rest at its own pace.
 |   See splash.cxx.
 | Author: suinevere
 ----------------------*/

/*----------------------
 | mapping_reset_defaults
 | Description: Restores both the face-button and shift-chord mappings to their
 |   compiled defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_face_btn, g_chord_slot
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mapping_reset_defaults(void);

#endif /* INPUT_H */
