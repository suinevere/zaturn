/*----------------------
 | controller.h
 | Description: The whole-peripheral layer: classifies every attached Saturn
 |   device into one of the seven families controls.xls names (6 pad, flight
 |   stick, analogue pad, mouse, twin stick, light gun, keyboard), translates each
 |   one's native input into the abstract actions that workbook lists, and carries
 |   the single on-screen pointer the mouse, the gun and the analogue sticks all
 |   drive. It sits beside input.h rather than inside it because MultiPad/g_pad is
 |   the raw button sensor every existing call site already reads; this module
 |   adds the devices that report no buttons for it to see.
 | Author: suinevere
 | Dependencies: none (SRL is reached only from controller.cxx)
 ----------------------*/
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include "saturn_keyboard.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DevKind
 | Description: One value per device column of controls.xls, plus DEV_NONE for a
 |   port that is empty, unrecognised or wedged. DEV_TWIN is never auto-detected:
 |   a Twin Stick reports the same id as a control pad, so it appears only when
 |   the player selects it (controller_twin_set).
 | Author: suinevere
 ----------------------*/
typedef enum {
    DEV_NONE = 0,
    DEV_PAD,        /* "6 pad"        Gamepad 0x02, Megadrive pads 0xe1/0xe2 */
    DEV_FLIGHT,     /* "Flight Stick" Mission Stick 0x15 */
    DEV_ANALOG,     /* "Analogue"     3D Control Pad 0x16, racing wheel 0x13 */
    DEV_MOUSE,      /* "mouse"        Saturn/NetLink mouse 0x23, shuttle 0xe3 */
    DEV_TWIN,       /* "twin stick"   player-selected profile over a 0x02 report */
    DEV_GUN,        /* "light gun"    0x25 and the Megadrive gun */
    DEV_KBD,        /* "Keyboard"     0x34, decoded by saturn_keyboard.h */
    DEV_KIND_N
} DevKind;

/*----------------------
 | DevAction
 | Description: One value per action row of controls.xls. DA_SCROLL, DA_PAGE and
 |   DA_ENDS are that workbook's three "(2)" rows and carry a direction; every
 |   other row is one-shot and is queried with dir 0.
 | Author: suinevere
 ----------------------*/
typedef enum {
    DA_MENU = 0,    /* Static sheet: open the menu */
    DA_LETTER,      /* Actions sheet */
    DA_BACK,
    DA_SPACE,
    DA_ACCEPT,
    DA_MAP,
    DA_RECALL,      /* a "(2)" row, but the pad column cycles up only */
    DA_SCROLL,      /* Scrolling sheet, "(2)": one line, dir -1 or +1 */
    DA_PAGE,        /* "(2)": one page */
    DA_ENDS,        /* "(2)": jump to top or bottom */
    DA_N
} DevAction;

/*----------------------
 | DevPointer
 | Description: The one on-screen pointer, whichever device is driving it. `hot`
 |   is set only on the frame a pointing device fires, so a caller hit-tests on an
 |   edge instead of every frame the cursor rests somewhere, and `offscreen` marks
 |   the light gun's off-screen shot, which fires as a right click -- the Back a
 |   gun has no other way to give.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int      valid;      /* a pointing device is driving the cursor */
    int      hot;        /* it fired this frame */
    int      held;       /* a button is down right now, for hold-repeat */
    int      offscreen;  /* the shot missed the raster entirely */
    int16_t  x, y;       /* screen pixels, clamped to controller_cursor_bounds */
    int      col, row;   /* the same point as a text cell, for hit tests */
    int      button;     /* which fired: DEV_BTN_LEFT / _MIDDLE / _RIGHT */
} DevPointer;

/*----------------------
 | DEV_HOLD_SCROLL_UP .. DEV_HOLD_N
 | Description: The hold-repeat slots controller_hold_fired keeps timers for, one
 |   per thing a player can hold a trigger on. Named centrally so two callers
 |   cannot pick the same number and share a timer by accident.
 | Author: suinevere
 ----------------------*/
enum {
    DEV_HOLD_SCROLL_UP = 0,
    DEV_HOLD_SCROLL_DOWN,
    DEV_HOLD_N
};

/*----------------------
 | controller_hold_fired
 | Description: Accelerating auto-repeat for something held rather than tapped.
 |   Fires once the moment `active` goes true, waits out an initial delay, then
 |   repeats -- slowly at first and quicker the longer the hold lasts, the way a
 |   held keyboard key behaves. Releasing resets the slot, so the next hold starts
 |   slow again.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of the DEV_HOLD_* constants; active -- nonzero while held
 | Returns: nonzero on the frames it fires
 ----------------------*/
int controller_hold_fired(int slot, int active);

/*----------------------
 | DEV_BTN_LEFT / DEV_BTN_MIDDLE / DEV_BTN_RIGHT
 | Description: The DevPointer.button values. The light gun has one trigger, so it
 |   reports a shot on the raster as DEV_BTN_LEFT and one off it as DEV_BTN_RIGHT.
 | Author: suinevere
 ----------------------*/
#define DEV_BTN_LEFT   0
#define DEV_BTN_MIDDLE 1
#define DEV_BTN_RIGHT  2

/*----------------------
 | controller_init
 | Description: Zeroes the module's per-frame and latched state; call once before
 |   the first controller_tick.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_init(void);

/*----------------------
 | controller_tick
 | Description: Reclassifies the ports, folds every attached device's native input
 |   into this frame's action edges, and advances the pointer. Call once per input
 |   frame, after Core::Synchronize and before any controller_fired query.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A (all state is file-static)
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_tick(void);

/*----------------------
 | controller_fired
 | Description: Whether action `a` fired this frame in direction `dir` on any
 |   attached device -- an edge or an auto-repeat tick, never a held level.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a -- the DevAction to test; dir -- -1, 0 or +1 (0 for the one-shot rows)
 | Returns: nonzero if it fired
 ----------------------*/
int controller_fired(DevAction a, int dir);

/*----------------------
 | controller_any_fired
 | Description: Whether any device this module reads produced anything at all this
 |   frame -- a mouse button, the mouse's Blue button, a gun trigger on screen or
 |   off it, a keyboard key already fed in. What a "press any key" prompt needs,
 |   and deliberately not the same question as any one action: a prompt that only
 |   accepts the four buttons someone thought of is a prompt that strands whoever
 |   is holding something else. The pad is not in here -- callers pair this with
 |   g_pad->AnyPressed(), which already covers all thirteen of its buttons.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero if anything fired
 ----------------------*/
int controller_any_fired(void);

/*----------------------
 | controller_kind
 | Description: The DevKind on `port`, or DEV_NONE when that port is empty,
 |   unrecognised, or reporting the wedged id 0x00.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: port -- 0 to 11
 | Returns: the classification
 ----------------------*/
DevKind controller_kind(int port);

/*----------------------
 | controller_kind_port
 | Description: The first port carrying a device of kind `k`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to look for
 | Returns: the port, or -1 when none is attached
 ----------------------*/
int controller_kind_port(DevKind k);

/*----------------------
 | controller_present
 | Description: Whether any port currently carries a device of kind `k`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to look for
 | Returns: nonzero if one is attached
 ----------------------*/
int controller_present(DevKind k);

/*----------------------
 | controller_kind_name
 | Description: Display name of a DevKind -- the controls.xls column, not the
 |   hardware. Use controller_kind_label to name what is actually plugged in.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to name
 | Returns: its display string; "None" for DEV_NONE and anything out of range
 ----------------------*/
const char *controller_kind_name(DevKind k);

/*----------------------
 | controller_port_name
 | Description: The model name the device on `port` reports, so two devices
 |   sharing a controls.xls column are still told apart on screen.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: port -- 0 to 11
 | Returns: its display string; "None" for an empty or wedged port
 ----------------------*/
const char *controller_port_name(int port);

/*----------------------
 | controller_kind_label
 | Description: The name to show for a kind: the model actually attached, or the
 |   column's own name when nothing of that kind is. Re-read every frame, so a
 |   hot-swap renames whatever is showing it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to name
 | Returns: its display string
 ----------------------*/
const char *controller_kind_label(DevKind k);

/*----------------------
 | controller_pointer
 | Description: This frame's pointer state.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: the module's DevPointer, valid until the next controller_tick
 ----------------------*/
const DevPointer *controller_pointer(void);

/*----------------------
 | controller_feed_key
 | Description: Folds one already-polled keyboard event into this frame's action
 |   edges, giving the keyboard column of controls.xls its two entries (Esc opens
 |   the menu, F8 opens the map). It takes an event rather than polling because
 |   saturn_keyboard_poll consumes one key per call and the game loop already
 |   makes that call; a second poll in here would eat every other keystroke.
 | Author: suinevere
 | Dependencies: saturn_keyboard.h
 | Globals: N/A
 | Params: ke -- the event the caller's own poll returned this frame
 | Returns: N/A
 ----------------------*/
void controller_feed_key(SaturnKeyEvent ke);

/*----------------------
 | controller_pointer_consume
 | Description: Marks this frame's pointer edge as handled: clears `hot` and drops
 |   the fallback action that edge would otherwise have fired. A view hit-tests
 |   controller_pointer() first and calls this when the cursor was over something
 |   of its own, so a click on the Map entry does not also arrive as a bare
 |   letter-click.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_pointer_consume(void);

/*----------------------
 | controller_pointer_flush
 | Description: Discards a pending pointer edge and this frame's action edges, for
 |   a screen that would otherwise inherit the click that opened it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_pointer_flush(void);

/*----------------------
 | controller_twin_set
 | Description: Turns the Twin Stick profile on or off. A Twin Stick reports the
 |   same id 0x02 as a control pad and cannot be told apart from one, so this is
 |   the only thing that makes DEV_TWIN appear.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: on -- nonzero to read 0x02 ports as Twin Sticks
 | Returns: N/A
 ----------------------*/
void controller_twin_set(int on);

/*----------------------
 | controller_twin_get
 | Description: Whether the Twin Stick profile is on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero when 0x02 ports are being read as Twin Sticks
 ----------------------*/
int controller_twin_get(void);

/*----------------------
 | controller_mouse_mode_set
 | Description: Turns Mouse Mode on or off -- the controls.xls sheet where a stick
 |   or D-pad drives the free cursor instead of stepping a selection. A real mouse
 |   ignores it and is always a cursor, which is that sheet's "N/A (no mouse
 |   on/off)".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: on -- nonzero for cursor, zero for selection
 | Returns: N/A
 ----------------------*/
void controller_mouse_mode_set(int on);

/*----------------------
 | controller_mouse_mode_get
 | Description: Whether Mouse Mode is on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero when a stick or D-pad is driving the cursor
 ----------------------*/
int controller_mouse_mode_get(void);

/*----------------------
 | CSRC_DPAD / CSRC_STICK / CSRC_N
 | Description: Which input drives the cursor while Mouse Mode is on. The two
 |   values are what they read from -- the digital direction bits, or an analogue
 |   axis pair -- not what they are called: a 3D Control Pad calls its axes the
 |   Analogue Stick and a Mission Stick calls the same reading its Left Stick, so
 |   the *names* come from controller_cursor_src_name and differ per device.
 | Author: suinevere
 ----------------------*/
enum { CSRC_DPAD = 0, CSRC_STICK, CSRC_N };

/*----------------------
 | controller_cursor_src_count
 | Description: How many cursor sources device `k` offers: 0 when it has no cursor
 |   of its own to steer, 1 when it has exactly one (show it, do not offer to
 |   change it), 2 when the player can pick.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the device kind
 | Returns: 0, 1 or 2
 ----------------------*/
int controller_cursor_src_count(DevKind k);

/*----------------------
 | controller_cursor_src_name
 | Description: What device `k` calls cursor source `src` -- "D-Pad" and
 |   "Analogue Stick" on a 3D Control Pad, "D-Pad" and "Left Stick" on a Mission
 |   Stick, "Left Stick" alone on a Twin Stick. Naming the reading rather than the
 |   register is the whole point: a player looking for a stick on the box should
 |   find the same word in the menu.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the device kind; src -- CSRC_DPAD or CSRC_STICK
 | Returns: the display string, or "" when that device has no such source
 ----------------------*/
const char *controller_cursor_src_name(DevKind k, int src);

/*----------------------
 | controller_cursor_src_set / controller_cursor_src_get
 | Description: Set or read which source drives the cursor for device `k`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the device kind; src -- CSRC_DPAD or CSRC_STICK
 | Returns: get returns the current source
 ----------------------*/
void controller_cursor_src_set(DevKind k, int src);
int  controller_cursor_src_get(DevKind k);

/*----------------------
 | controller_dpad_is_cursor
 | Description: Whether the D-pad is currently steering the cursor rather than
 |   stepping a selection -- Mouse Mode on, and some attached device pointed at its
 |   digital directions. Gameplay reads this through pad_fired, which refuses the
 |   four direction buttons while it is true, so the D-pad does one job at a time.
 |   Menus are deliberately outside it: they read the pad directly and still need
 |   the D-pad to navigate.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero while the D-pad belongs to the cursor
 ----------------------*/
int controller_dpad_is_cursor(void);

/*----------------------
 | CTL_MOUSE_SPEED_N
 | Description: How many mouse speeds there are to step through.
 | Author: suinevere
 ----------------------*/
#define CTL_MOUSE_SPEED_N 5

/*----------------------
 | controller_mouse_speed_set / controller_mouse_speed_get
 | Description: Set or read how far the cursor travels per unit the mouse reports,
 |   as an index into CTL_MOUSE_SPEED_N steps. A Saturn mouse counts far finer than
 |   a 320-pixel screen wants, so this is a divisor and a low index is slow.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: n -- 0 to CTL_MOUSE_SPEED_N - 1, clamped
 | Returns: get returns the current index
 ----------------------*/
void controller_mouse_speed_set(int n);
int  controller_mouse_speed_get(void);

/*----------------------
 | controller_mouse_raw
 | Description: The first attached mouse's raw SMPC report bytes, and the x/y
 |   fields SRL reads out of it. Exists because SGL's peripheral decoder is only in
 |   the precompiled LIBSGL.A: nothing in the headers says whether a mouse's
 |   movement lands in PerPoint's x/y or stays in the report's own bytes, and two
 |   guesses at it have now been wrong. Shown on the mouse's Mouse Mode sheet so
 |   one run settles it.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: b -- receives report bytes 2 to 7; xy -- receives PerPoint's x and y
 | Returns: nonzero if a mouse was found
 ----------------------*/
int controller_mouse_raw(int *b, int *xy);

/*----------------------
 | controller_wedged
 | Description: Whether a port has been seen reporting id 0x00, the state the SMPC
 |   layer falls into permanently once a light gun has been selected. This is for
 |   explaining a dead pad, not repairing one: the peripheral data is gone and no
 |   software can bring it back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero once the wedge has been observed
 ----------------------*/
int controller_wedged(void);

/*----------------------
 | controller_cursor_bounds
 | Description: Sets the rectangle the pointer is clamped inside, in screen
 |   pixels. Defaults to 320x224.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: w -- width in pixels; h -- height in pixels
 | Returns: N/A
 ----------------------*/
void controller_cursor_bounds(int w, int h);

/*----------------------
 | what a pointing device can and cannot decide here
 | Description: Most of the mouse's column in controls.xls is positional -- the
 |   same click is Letter over a key, Space over the space bar, Map over the Map
 |   entry, Scroll over an arrow -- and only the view knows what the cursor is
 |   over. So this module reports the edge and the button in DevPointer and stops
 |   there, contributing just the three non-positional fallbacks a click means
 |   when it lands on nothing: left is DA_LETTER, middle DA_BACK, right DA_ACCEPT.
 |   A view hit-tests first and calls controller_pointer_consume when it took the
 |   click. The light gun is the same shape with one binding it can decide alone:
 |   a shot that misses the raster is DA_ACCEPT, per that column's "Shoot off
 |   screen", and it needs no hit test to know it missed.
 | Author: suinevere
 ----------------------*/

/*----------------------
 | why the twin stick's bit table is provisional
 | Description: Every other column of controls.xls maps onto a peripheral id SRL
 |   already distinguishes, so its bindings are as certain as the workbook is. The
 |   Twin Stick is not one of them: it reports id 0x02 with a standard 16-bit
 |   button word, indistinguishable from a control pad, so both which ports are
 |   Twin Sticks and which bits its four triggers sit on are assumptions. The port
 |   half is handed to the player (controller_twin_set). The bit half is the
 |   TWIN_* table in controller.cxx, which is one edit away from correct once
 |   somebody reads a real HSS-0136 -- nothing else in this module encodes it.
 | Author: suinevere
 ----------------------*/

#ifdef __cplusplus
}
#endif

#endif /* CONTROLLER_H */
