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
 |   the light gun's off-screen shot, which controls.xls assigns to Accept.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int      valid;      /* a pointing device is driving the cursor */
    int      hot;        /* it fired this frame */
    int      offscreen;  /* the shot missed the raster entirely */
    int16_t  x, y;       /* screen pixels, clamped to controller_cursor_bounds */
    int      button;     /* which fired: DEV_BTN_LEFT / _MIDDLE / _RIGHT */
} DevPointer;

/*----------------------
 | DEV_BTN_LEFT / DEV_BTN_MIDDLE / DEV_BTN_RIGHT
 | Description: The DevPointer.button values. The light gun reports every shot as
 |   DEV_BTN_LEFT because it has one trigger.
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
 | Description: Display name of a DevKind, for the Controls page and diagnostics.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to name
 | Returns: its display string; "None" for DEV_NONE and anything out of range
 ----------------------*/
const char *controller_kind_name(DevKind k);

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
