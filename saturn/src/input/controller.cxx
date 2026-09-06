/*----------------------
 | controller.cxx
 | Description: Implements controller.h -- port classification with the light-gun
 |   wedge guard, the per-device translation tables transcribed from controls.xls,
 |   analogue-axis edge and repeat detection, and the shared screen pointer that
 |   the mouse, the light gun and the analogue sticks drive.
 | Author: suinevere
 | Dependencies: srl.hpp, controller.h, input.h, saturn_keyboard.h
 ----------------------*/

#include <srl.hpp>
#include "controller.h"
#include "input.h"

/*----------------------
 | PORT_N
 | Description: How many peripheral ports the SMPC can report, counting both
 |   multitaps; matches SRL::Input::MaxPeripherals.
 | Author: suinevere
 ----------------------*/
static const int PORT_N = 12;

/*----------------------
 | RAW_ID / ID_WEDGED
 | Description: Byte offset of the peripheral id inside a raw SMPC report, and the
 |   id a wedged port reports. ID_WEDGED is SGL's PER_ID_ExtDigital, which sits in
 |   the Digital family, so SRL calls such a port connected and reads its all-zero
 |   data word as every button held; this module rejects it by id instead.
 | Author: suinevere
 ----------------------*/
static const int     RAW_ID    = 0;
static const uint8_t ID_WEDGED = 0x00;

/*----------------------
 | AXIS_MID / AXIS_DEAD
 | Description: Centre value and half-width of the dead zone for an 8-bit analogue
 |   axis, which SRL reports over 0 to 255.
 | Author: suinevere
 ----------------------*/
static const int AXIS_MID  = 128;
static const int AXIS_DEAD = 48;

/*----------------------
 | AXIS_DELAY / AXIS_RATE
 | Description: Frames a held axis waits before it repeats, and the interval
 |   between repeats after that; matched to the shift chords in input.cxx so a
 |   stick and a chord scroll at the same speed.
 | Author: suinevere
 ----------------------*/
static const int AXIS_DELAY = 30;
static const int AXIS_RATE  = 4;

/*----------------------
 | CURSOR_STEP_MIN / CURSOR_STEP_MAX / CURSOR_RAMP
 | Description: How far the cursor moves per frame, and how quickly a held
 |   direction works its way from the first to the second. Starting at one pixel is
 |   what makes a digital direction usable for pointing at a character cell at all;
 |   without the ramp above it, crossing the screen takes five seconds. CURSOR_RAMP
 |   is frames of holding per extra pixel.
 | Author: suinevere
 ----------------------*/
static const int CURSOR_STEP_MIN = 1;
static const int CURSOR_STEP_MAX = 7;
static const int CURSOR_RAMP     = 6;

/*----------------------
 | MOUSE_ACCEL_KNEE
 | Description: The reported distance at which a mouse's own acceleration doubles
 |   its travel. Below it the cursor tracks the hand one-to-one so a cell can be
 |   picked; above it a flick crosses the screen. Larger is less acceleration.
 | Author: suinevere
 ----------------------*/
static const int MOUSE_ACCEL_KNEE = 48;

/*----------------------
 | MOUSE_TRAVEL_MAX
 | Description: The most the cursor moves in one frame however hard the mouse is
 |   thrown. Without a bound the acceleration curve turns a fast flick into half
 |   the screen in a sixtieth of a second, which does not read as speed -- it reads
 |   as the cursor jumping.
 | Author: suinevere
 ----------------------*/
static const int MOUSE_TRAVEL_MAX = 40;

/*----------------------
 | g_mouse_last / g_mouse_seen / MOUSE_JUMP_MAX
 | Description: The previous frame's raw mouse reading per port, whether one has
 |   been taken yet, and the most movement a single frame is allowed to claim.
 |   SGL accumulates: its mouse handler adds each report's signed movement into
 |   the sixteen-bit x and y already sitting in PerPoint, and zeroes both when the
 |   port's id changes. So the reading is a running total -- the number stays
 |   where the hand left it -- the movement this frame is the difference against
 |   last frame, and a reading that never changes is a hand that is not moving.
 |   Reading the raw value as the delta is what made the cursor slide on until the
 |   mouse was carried back to where it started. One report carries a byte, its
 |   sign and an overflow bit, so nothing honest exceeds MOUSE_JUMP_MAX; a bigger
 |   jump is that hot-swap reset landing between two frames, and is dropped rather
 |   than thrown at the cursor.
 | Author: suinevere
 ----------------------*/
static int16_t g_mouse_last[PORT_N][2];
static uint8_t g_mouse_seen[PORT_N];
static const int MOUSE_JUMP_MAX = 512;

/*----------------------
 | mouse_delta
 | Description: This frame's movement on one axis: the difference against last
 |   frame, taken in the sixteen bits SGL's accumulator actually counts in.
 |   Narrowing that difference to a byte -- which this did -- was the snap: a
 |   frame that moved more than 127 counts came back with the wrong sign, and 127
 |   counts is a couple of millimetres of hand at any modern mouse's resolution,
 |   so ordinary small movements, and every frame of slowing down, threw the
 |   cursor backwards until it pinned against a screen edge.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: now -- this frame's reading; last -- the previous one
 | Returns: the signed movement, or 0 if it is too large to have been a hand
 ----------------------*/
static int mouse_delta(int now, int last) {
    int d = (int) (int16_t) ((uint16_t) now - (uint16_t) last);
    if (d > MOUSE_JUMP_MAX || d < -MOUSE_JUMP_MAX) return 0;
    return d;
}

/*----------------------
 | TWIN_TRIG_L / TWIN_TRIG_R / TWIN_TOP_L / TWIN_TOP_R
 | Description: Which bits of a standard digital report the Twin Stick's two index
 |   triggers and two thumb-top buttons sit on. Supplied by the project owner as
 |   the device's own table, not inferred here: a Twin Stick reports id 0x02 like
 |   any control pad, so the report says nothing about which is which and the two
 |   this file had guessed wrong -- the left thumb top is Z, not A, and A belongs
 |   to the right stick.
 | Author: suinevere
 ----------------------*/
static const Button TWIN_TRIG_L = Button::L;
static const Button TWIN_TRIG_R = Button::R;
static const Button TWIN_TOP_L  = Button::Z;
static const Button TWIN_TOP_R  = Button::C;

/*----------------------
 | TWIN_RS_UP / TWIN_RS_DOWN / TWIN_RS_LEFT / TWIN_RS_RIGHT
 | Description: The four bits the Twin Stick's right stick reports on, from the
 |   same owner-supplied table as the buttons above. The left stick is the digital
 |   D-pad, which is why it has no table of its own.
 | Author: suinevere
 ----------------------*/
static const Button TWIN_RS_UP    = Button::X;
static const Button TWIN_RS_DOWN  = Button::B;
static const Button TWIN_RS_LEFT  = Button::Y;
static const Button TWIN_RS_RIGHT = Button::A;

/*----------------------
 | g_rstick_held
 | Description: Frames the Twin Stick's right stick has been held one way, which
 |   is what its cursor ramp is measured in. Reset the moment it centres.
 | Author: suinevere
 ----------------------*/
static int g_rstick_held = 0;

/*----------------------
 | CSRC_NAME
 | Description: What each device calls each thing that could drive its cursor,
 |   indexed [DevKind][CSRC_*]. An empty entry means that device has no such input:
 |   a control pad has no axis at all, and the two that do call the same reading
 |   different things -- a 3D Control Pad's 3D Stick is a Mission Stick's Right
 |   Stick, and a Twin Stick's left stick is the digital pad every other device
 |   calls a D-pad.
 | Author: suinevere
 ----------------------*/
static const char *const CSRC_NAME[DEV_KIND_N][CSRC_N] = {
    { "Off", "",            ""             },  /* DEV_NONE   */
    { "Off", "D-Pad",       ""             },  /* DEV_PAD    */
    { "Off", "Left Stick",  "Right Stick"  },  /* DEV_FLIGHT */
    { "Off", "D-Pad",       "3D Stick"     },  /* DEV_ANALOG */
    { "Off", "",            ""             },  /* DEV_MOUSE  */
    { "Off", "Left Stick",  "Right Stick"  },  /* DEV_TWIN   */
    { "Off", "",            ""             },  /* DEV_GUN    */
    { "Off", "",            ""             },  /* DEV_KBD    */
};

/*----------------------
 | g_cursor_src
 | Description: What drives each device's cursor. A device with an axis pair
 |   starts on it, since nothing else wants it; a device whose only candidate is
 |   the D-pad starts Off, because that D-pad is also what steps the selection and
 |   taking it away is the player's call to make.
 | Author: suinevere
 ----------------------*/
static uint8_t g_cursor_src[DEV_KIND_N] = {
    CSRC_OFF,   /* DEV_NONE   */
    CSRC_OFF,   /* DEV_PAD    */
    CSRC_STICK, /* DEV_FLIGHT */
    CSRC_STICK, /* DEV_ANALOG */
    CSRC_OFF,   /* DEV_MOUSE  */
    CSRC_STICK, /* DEV_TWIN   */
    CSRC_OFF,   /* DEV_GUN    */
    CSRC_OFF,   /* DEV_KBD    */
};

/*----------------------
 | MOUSE_GAIN / MOUSE_GAIN_UNIT / g_mouse_speed
 | Description: How far the cursor travels per count the mouse reports, as a
 |   fraction over MOUSE_GAIN_UNIT, and the step in force. These were divisors
 |   while the reading was being taken as a running total, where the numbers were
 |   enormous; against a real per-frame delta the same divisors left the cursor
 |   barely able to cross the screen, so they are gains now.
 | Author: suinevere
 ----------------------*/
static const int MOUSE_GAIN[CTL_MOUSE_SPEED_N] = { 2, 3, 4, 6, 9 };
static const int MOUSE_GAIN_UNIT = 4;
static int g_mouse_speed = 2;

/*----------------------
 | g_mouse_rem
 | Description: The sub-pixel remainder each axis is carrying. Without it the
 |   gain's integer division throws away every movement too small to make a whole
 |   pixel, which at any gain below 1 is most of a slow hand -- the cursor reads as
 |   having no resolution rather than as being slow.
 | Author: suinevere
 ----------------------*/
static int g_mouse_rem[2];

/*----------------------
 | g_fired
 | Description: This frame's action edges, indexed [action][dir + 1]; rebuilt from
 |   nothing by every controller_tick.
 | Author: suinevere
 ----------------------*/
static uint8_t g_fired[DA_N][3];

/*----------------------
 | g_ptr / g_ptr_fallback
 | Description: This frame's pointer state, and which action index the pointer's
 |   own click contributed to g_fired so controller_pointer_consume can withdraw
 |   exactly that one; -1 when the pointer contributed nothing.
 | Author: suinevere
 ----------------------*/
static DevPointer g_ptr;
static int        g_ptr_fallback = -1;

/*----------------------
 | g_nav
 | Description: This frame's menu directions from a device that has no D-pad to
 |   report them with; rebuilt from nothing by every controller_tick.
 | Author: suinevere
 ----------------------*/
static uint8_t g_nav[NAV_N];

/*----------------------
 | g_twin / g_wedged
 | Description: The player's Twin Stick profile switch, and the latch recording
 |   that a wedged port has been seen at least once.
 | Author: suinevere
 ----------------------*/
static int g_twin       = 0;
static int g_wedged     = 0;

/*----------------------
 | g_bound_w / g_bound_h
 | Description: The rectangle the cursor is clamped inside, in screen pixels.
 | Author: suinevere
 ----------------------*/
static int g_bound_w = 320;
static int g_bound_h = 240;

/*----------------------
 | CELL_W / CELL_H
 | Description: A text cell in pixels. The screen is 40 by 30 cells over 320 by
 |   240, so both are 8 and DevPointer.col/row are the cursor divided by them.
 | Author: suinevere
 ----------------------*/
static const int CELL_W = 8;
static const int CELL_H = 8;

/*----------------------
 | HOLD_DELAY / HOLD_RATE_SLOW / HOLD_RATE_FAST / HOLD_ACCEL
 | Description: controller_hold_fired's timing in frames: the wait before a held
 |   trigger starts repeating, the interval it starts repeating at, the floor it
 |   accelerates to, and how many repeats it takes to gain one frame of speed.
 | Author: suinevere
 ----------------------*/
static const int HOLD_DELAY     = 24;
static const int HOLD_RATE_SLOW = 10;
static const int HOLD_RATE_FAST = 2;
static const int HOLD_ACCEL     = 3;

/*----------------------
 | HoldRep / g_hold
 | Description: Per-slot hold-repeat state: the countdown to the next tick and how
 |   many ticks this hold has already produced, which is what makes it accelerate.
 | Author: suinevere
 ----------------------*/
struct HoldRep { int16_t timer; int16_t ticks; };
static HoldRep g_hold[DEV_HOLD_N];

/*----------------------
 | controller_hold_fired
 | Description: Fires immediately when a hold begins, then after HOLD_DELAY starts
 |   repeating at HOLD_RATE_SLOW and shortens the interval by one frame every
 |   HOLD_ACCEL repeats until it reaches HOLD_RATE_FAST. Releasing clears the slot
 |   so the next hold starts slow again.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_hold
 | Params: slot -- one of the DEV_HOLD_* constants; active -- nonzero while held
 | Returns: nonzero on the frames it fires
 ----------------------*/
int controller_hold_fired(int slot, int active) {
    if (slot < 0 || slot >= DEV_HOLD_N) return 0;
    HoldRep &h = g_hold[slot];
    if (!active) { h.timer = 0; h.ticks = -1; return 0; }
    if (h.ticks < 0) { h.ticks = 0; h.timer = (int16_t) HOLD_DELAY; return 1; }
    if (--h.timer > 0) return 0;
    h.ticks++;
    int rate = HOLD_RATE_SLOW - h.ticks / HOLD_ACCEL;
    if (rate < HOLD_RATE_FAST) rate = HOLD_RATE_FAST;
    h.timer = (int16_t) rate;
    return 1;
}

/*----------------------
 | AxisRep
 | Description: Edge and hold-repeat state for one analogue axis: the direction it
 |   is currently deflected in, and the countdown to its next repeat tick.
 | Author: suinevere
 ----------------------*/
struct AxisRep {
    int8_t  held;
    int16_t timer;
};

/*----------------------
 | g_axis
 | Description: Repeat state for the axes this module reads, one row per port:
 |   0 is the primary stick's X, 1 its Y.
 | Author: suinevere
 ----------------------*/
static AxisRep g_axis[PORT_N][2];

/*----------------------
 | mark
 | Description: Records that action `a` fired in direction `dir` this frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fired
 | Params: a -- the action; dir -- -1, 0 or +1
 | Returns: N/A
 ----------------------*/
static void mark(DevAction a, int dir) {
    if (a < 0 || a >= DA_N || dir < -1 || dir > 1) return;
    g_fired[a][dir + 1] = 1;
}

/*----------------------
 | raw_id
 | Description: The peripheral id byte reported by `port`, or ID_WEDGED when the
 |   port reports nothing at all.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: port -- 0 to PORT_N - 1
 | Returns: the id byte
 ----------------------*/
static uint8_t raw_id(int port) {
    const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(port);
    return raw != nullptr ? raw[RAW_ID] : ID_WEDGED;
}

/*----------------------
 | controller_kind
 | Description: Maps a port's reported id onto a controls.xls column, rejecting
 |   the wedged id 0x00 before SRL's family test can mistake it for a live digital
 |   pad, and honouring the Twin Stick profile over an ordinary gamepad id.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: g_twin, g_wedged
 | Params: port -- 0 to PORT_N - 1
 | Returns: the DevKind, or DEV_NONE
 ----------------------*/
DevKind controller_kind(int port) {
    using T = SRL::Input::PeripheralType;

    if (port < 0 || port >= PORT_N) return DEV_NONE;
    uint8_t id = raw_id(port);
    if (id == ID_WEDGED) { g_wedged = 1; return DEV_NONE; }

    switch ((T) id) {
        case T::Gamepad:      return g_twin ? DEV_TWIN : DEV_PAD;
        case T::MD3ButtonPad:
        case T::MD6ButtonPad: return DEV_PAD;
        case T::AnalogPad:    return DEV_FLIGHT;
        case T::Analog3dPad:
        case T::Racing:       return DEV_ANALOG;
        case T::Mouse:
        case T::ShuttleMouse: return DEV_MOUSE;
        case T::Gun:
        case T::MDGun:        return DEV_GUN;
        case T::Keyboard:     return DEV_KBD;
        default:              return DEV_NONE;
    }
}

/*----------------------
 | controller_kind_port
 | Description: The first port carrying a device of kind `k`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to look for
 | Returns: the port, or -1 when nothing of that kind is attached
 ----------------------*/
int controller_kind_port(DevKind k) {
    for (int p = 0; p < PORT_N; p++) if (controller_kind(p) == k) return p;
    return -1;
}

/*----------------------
 | controller_present
 | Description: Walks the ports looking for one classified as `k`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to look for
 | Returns: 1 if found, 0 otherwise
 ----------------------*/
int controller_present(DevKind k) {
    return controller_kind_port(k) >= 0 ? 1 : 0;
}

/*----------------------
 | controller_wheel_present
 | Description: Walks the ports for a racing wheel's own id, which is what tells
 |   one apart from the 3D Control Pad it shares a column with.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: N/A
 | Returns: 1 when a wheel is attached, 0 otherwise
 ----------------------*/
int controller_wheel_present(void) {
    for (int p = 0; p < PORT_N; p++)
        if (raw_id(p) == (uint8_t) SRL::Input::PeripheralType::Racing) return 1;
    return 0;
}

/*----------------------
 | controller_nav_fired
 | Description: Reads back one of this frame's synthesised menu directions.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_nav
 | Params: nav -- one of the NAV_* constants
 | Returns: nonzero if that direction fired
 ----------------------*/
int controller_nav_fired(int nav) {
    if (nav < 0 || nav >= NAV_N) return 0;
    return g_nav[nav];
}

/*----------------------
 | controller_kind_name
 | Description: Indexes a fixed name table by kind. This names the controls.xls
 |   column, not the hardware: several models share one column, so a page that
 |   wants to tell the player what they are holding wants controller_kind_label.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to name
 | Returns: its display string; "None" when out of range
 ----------------------*/
const char *controller_kind_name(DevKind k) {
    static const char *N[DEV_KIND_N] = {
        "None", "Control Pad", "Flight Stick", "Analogue",
        "Mouse", "Twin Stick", "Light Gun", "Keyboard"
    };
    if (k < 0 || k >= DEV_KIND_N) return "None";
    return N[k];
}

/*----------------------
 | controller_port_name
 | Description: What the device on `port` calls itself, read from the id it
 |   reports rather than from the column it was sorted into, so a 3D Control Pad
 |   and a wheel do not both answer to "Analogue".
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: g_twin
 | Params: port -- 0 to PORT_N - 1
 | Returns: the model name; "None" for an empty or wedged port
 ----------------------*/
const char *controller_port_name(int port) {
    using T = SRL::Input::PeripheralType;

    if (port < 0 || port >= PORT_N) return "None";
    uint8_t id = raw_id(port);
    if (id == ID_WEDGED) return "None";

    switch ((T) id) {
        case T::Gamepad:      return g_twin ? "Twin Stick" : "Control Pad";
        case T::MD3ButtonPad: return "MD 3-Button";
        case T::MD6ButtonPad: return "MD 6-Button";
        case T::AnalogPad:    return "Mission Stick";
        case T::Analog3dPad:  return "3D Control Pad";
        case T::Racing:       return "Racing Wheel";
        case T::Mouse:        return "Netlink Mouse";
        case T::ShuttleMouse: return "Shuttle Mouse";
        case T::Gun:          return "Virtua Gun";
        case T::MDGun:        return "MD Light Gun";
        case T::Keyboard:     return "Keyboard";
        default:              return "Unknown";
    }
}

/*----------------------
 | controller_kind_label
 | Description: The name to show for a kind: the model actually plugged in, so
 |   swapping a pad for a wheel renames the row on the frame it happens, falling
 |   back to the column's own name when nothing of that kind is attached to read
 |   a model off.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to name
 | Returns: its display string
 ----------------------*/
const char *controller_kind_label(DevKind k) {
    int port = controller_kind_port(k);
    return port >= 0 ? controller_port_name(port) : controller_kind_name(k);
}

/*----------------------
 | axis_dir
 | Description: Which way an 8-bit axis is deflected, outside its dead zone.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: v -- the raw axis value, 0 to 255
 | Returns: -1, 0 or +1
 ----------------------*/
static int axis_dir(int v) {
    if (v < AXIS_MID - AXIS_DEAD) return -1;
    if (v > AXIS_MID + AXIS_DEAD) return  1;
    return 0;
}

/*----------------------
 | axis_travel
 | Description: How far the cursor should move this frame for one axis reading:
 |   nothing inside the dead zone, then proportional to deflection up to
 |   CURSOR_STEP_MAX. This is where an analogue stick stops behaving like a
 |   D-pad -- axis_dir throws the magnitude away, which is right for a scroll edge
 |   and wrong for a cursor.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: v -- the raw axis value, 0 to 255
 | Returns: signed pixels for this frame
 ----------------------*/
static int axis_travel(int v) {
    int d = v - AXIS_MID;
    int a = d < 0 ? -d : d;
    if (a <= AXIS_DEAD) return 0;
    a -= AXIS_DEAD;
    int span = 128 - AXIS_DEAD;
    int step = CURSOR_STEP_MIN + (a * (CURSOR_STEP_MAX - CURSOR_STEP_MIN)) / span;
    if (step > CURSOR_STEP_MAX) step = CURSOR_STEP_MAX;
    return d < 0 ? -step : step;
}

/*----------------------
 | mouse_travel
 | Description: Applies the mouse's acceleration curve and speed gain to one
 |   reported axis, carrying the sub-pixel remainder so nothing is lost to the
 |   division. The quadratic term is what makes a slow hand precise and a fast one
 |   quick; without it the only choice is one or the other.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mouse_speed, g_mouse_rem
 | Params: d -- the reported movement on one axis; axis -- 0 for X, 1 for Y
 | Returns: whole pixels to move this frame
 ----------------------*/
static int mouse_travel(int d, int axis) {
    int a = d < 0 ? -d : d;
    int scaled = d + (d * a) / MOUSE_ACCEL_KNEE;
    int num = scaled * MOUSE_GAIN[g_mouse_speed] + g_mouse_rem[axis];
    int px  = num / MOUSE_GAIN_UNIT;
    g_mouse_rem[axis] = num - px * MOUSE_GAIN_UNIT;
    if (px >  MOUSE_TRAVEL_MAX) { px =  MOUSE_TRAVEL_MAX; g_mouse_rem[axis] = 0; }
    if (px < -MOUSE_TRAVEL_MAX) { px = -MOUSE_TRAVEL_MAX; g_mouse_rem[axis] = 0; }
    return px;
}

/*----------------------
 | axis_step
 | Description: Advances one axis's repeat state and reports whether it fires this
 |   frame -- once on the frame it leaves the dead zone, then again on each repeat
 |   tick while it is held over.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_axis
 | Params: port -- the port the axis belongs to; slot -- 0 for X, 1 for Y;
 |   dir -- this frame's axis_dir
 | Returns: the direction that fired, or 0
 ----------------------*/
static int axis_step(int port, int slot, int dir) {
    AxisRep &r = g_axis[port][slot];
    if (dir == 0) { r.held = 0; r.timer = 0; return 0; }
    if (r.held != dir) { r.held = (int8_t) dir; r.timer = AXIS_DELAY; return dir; }
    if (--r.timer <= 0) { r.timer = AXIS_RATE; return dir; }
    return 0;
}

/*----------------------
 | clamp_cursor
 | Description: Holds the cursor inside the bounds rectangle.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr, g_bound_w, g_bound_h
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void clamp_cursor(void) {
    if (g_ptr.x < 0) g_ptr.x = 0;
    if (g_ptr.y < 0) g_ptr.y = 0;
    if (g_ptr.x > (int16_t) (g_bound_w - 1)) g_ptr.x = (int16_t) (g_bound_w - 1);
    if (g_ptr.y > (int16_t) (g_bound_h - 1)) g_ptr.y = (int16_t) (g_bound_h - 1);
    g_ptr.col = g_ptr.x / CELL_W;
    g_ptr.row = g_ptr.y / CELL_H;
}

/*----------------------
 | pointer_fire
 | Description: Records a pointing-device click at the current cursor position and
 |   contributes the non-positional fallback action that button carries, keeping
 |   the fallback's identity so controller_pointer_consume can withdraw it.
 |
 |   The fallbacks follow the split menu_pointer_act and menu_pointer_back are
 |   built on -- left and middle accept, right goes back -- rather than the mouse
 |   column of controls.xls, which reads right as Accept and middle as Backspace.
 |   SRL's names for those two bits are its own reading of SGL's digital bits and
 |   the two candidate layouts disagree about them, so the only defensible thing
 |   is to make one button mean one thing everywhere; back is the meaning a player
 |   cannot work around by clicking something else.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr, g_ptr_fallback
 | Params: button -- DEV_BTN_LEFT, _MIDDLE or _RIGHT
 | Returns: N/A
 ----------------------*/
static void pointer_fire(int button) {
    static const DevAction FALLBACK[3] = { DA_LETTER, DA_ACCEPT, DA_BACK };
    g_ptr.hot    = 1;
    g_ptr.button = button;
    g_ptr_fallback = (int) FALLBACK[button];
    mark(FALLBACK[button], 0);
}

/*----------------------
 | read_pad_family
 | Description: Folds a digital or analogue pad's buttons into this frame's
 |   actions by delegating to the existing remappable mapping -- face_button for
 |   the four typing actions, chord_fired for recall and the three scrolling rows
 |   -- so the pad has exactly one mapping and the Controls page still owns it.
 |   Reads edges rather than repeat state, because this runs on screens that never
 |   advance the repeat timers.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void read_pad_family(void) {
    if (g_pad == nullptr) return;

    if (g_pad->WasPressed(Button::START)) mark(DA_MENU, 0);
    /* Map is Z, which is the workbook's own cell for it. Skipped when Z is the
       chord modifier: the map would then open on the press that begins every
       scroll, and a screen that takes the whole display is not something to open
       by accident. */
    if (chord_btn_button() != Button::Z && g_pad->WasPressed(Button::Z)) mark(DA_MAP, 0);

    /* Edges, not pad_fired: this runs on screens that never call
       pad_repeat_update -- every menu, and the title -- where pad_fired reports
       whatever the last screen that did call it left in the repeat table. A flag
       stuck true there makes controller_any_fired true on every frame forever,
       which is a title screen that cannot be waited on. An edge needs no timer. */
    if (g_pad->WasPressed(face_button(FA_TYPE)))   mark(DA_LETTER, 0);
    if (g_pad->WasPressed(face_button(FA_BACK)))   mark(DA_BACK,   0);
    if (g_pad->WasPressed(face_button(FA_SPACE)))  mark(DA_SPACE,  0);
    if (g_pad->WasPressed(face_button(FA_ACCEPT))) mark(DA_ACCEPT, 0);

    /* The chords have no edge-only form, so they are gated on having been ticked
       this frame instead. */
    if (!chord_ticked()) return;
    for (int d = -1; d <= 1; d += 2) {
        if (chord_fired(CA_RECALL,  d)) mark(DA_RECALL, d);
        if (chord_fired(CA_LINE,    d)) mark(DA_SCROLL, d);
        if (chord_fired(CA_PAGE,    d)) mark(DA_PAGE,   d);
        if (chord_fired(CA_HOMEEND, d)) mark(DA_ENDS,   d);
    }
}

/*----------------------
 | read_twin
 | Description: Folds a Twin Stick's four triggers and Start into this frame's
 |   actions, per the twin stick column of controls.xls. Its Map, Recall and
 |   scrolling cells are blank, so this reports none of them.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void read_twin(void) {
    if (g_pad == nullptr) return;
    if (g_pad->WasPressed(Button::START)) mark(DA_MENU,   0);
    if (g_pad->WasPressed(TWIN_TRIG_R))   mark(DA_LETTER, 0);
    if (g_pad->WasPressed(TWIN_TRIG_L))   mark(DA_BACK,   0);
    if (g_pad->WasPressed(TWIN_TOP_R))    mark(DA_SPACE,  0);
    if (g_pad->WasPressed(TWIN_TOP_L))    mark(DA_ACCEPT, 0);

    /* The right stick is the cursor and the left one steps the selection, which
       is what having two sticks is for. A held direction accelerates the same way
       a D-pad cursor used to: one pixel a frame to begin with, so a cell can be
       picked at all, working up to CURSOR_STEP_MAX so the far corner stays
       reachable. */
    int dx = g_pad->IsHeld(TWIN_RS_RIGHT) ? 1 : g_pad->IsHeld(TWIN_RS_LEFT) ? -1 : 0;
    int dy = g_pad->IsHeld(TWIN_RS_DOWN)  ? 1 : g_pad->IsHeld(TWIN_RS_UP)   ? -1 : 0;
    if (!dx && !dy) { g_rstick_held = 0; return; }
    int step = CURSOR_STEP_MIN + g_rstick_held / CURSOR_RAMP;
    if (step > CURSOR_STEP_MAX) step = CURSOR_STEP_MAX;
    g_rstick_held++;
    g_ptr.x = (int16_t) (g_ptr.x + dx * step);
    g_ptr.y = (int16_t) (g_ptr.y + dy * step);
    clamp_cursor();
}

/*----------------------
 | read_sticks
 | Description: Reads one analogue port's stick: on a flight stick its two axes
 |   scroll a line and turn a page, per the Scrolling sheet; on either kind it
 |   drives the cursor while Mouse Mode is on. The analogue pad's scrolling cells
 |   are blank, so only the flight stick contributes scroll edges.
 | Author: suinevere
 | Dependencies: SRL (Input::Analog)
 | Globals: g_ptr
 | Params: port -- the port to read; kind -- DEV_FLIGHT or DEV_ANALOG
 | Returns: N/A
 ----------------------*/
static void read_sticks(int port, DevKind kind) {
    SRL::Input::Analog a(port);
    if (!a.IsConnected()) return;

    int x = (int) a.GetAxis(SRL::Input::Analog::Axis::Axis1);
    int y = (int) a.GetAxis(SRL::Input::Analog::Axis::Axis2);
    int dx = axis_dir(x);
    int dy = axis_dir(y);

    if (kind == DEV_FLIGHT) {
        int sy = axis_step(port, 1, dy);
        int sx = axis_step(port, 0, dx);
        if (sy) mark(DA_SCROLL, -sy);
        if (sx) mark(DA_PAGE,    sx);
    }

    /* Only when the player has pointed this device's cursor at its stick. A wheel
       never gets here even if they have: it answers GetAxis(Axis2) with a zero it
       does not have, which reads as a stick held hard up, and the cursor would
       climb on its own for as long as the wheel was plugged in. */
    if (g_cursor_src[kind] == CSRC_STICK && !controller_wheel_present()) {
        int tx = axis_travel(x);
        int ty = axis_travel(y);
        if (tx || ty) {
            g_ptr.x = (int16_t) (g_ptr.x + tx);
            g_ptr.y = (int16_t) (g_ptr.y + ty);
            clamp_cursor();
        }
    }
}

/*----------------------
 | read_wheel
 | Description: Turns a racing wheel into the four menu directions it has no D-pad
 |   to send. Steering is Left and Right, through the same repeat the analogue
 |   sheets use, so a held lock walks a list rather than jumping it; the two
 |   paddles are Up and Down -- right pulls up, left pulls down, which is the way
 |   the paddles read on the wheel itself. The wheel has nothing else on it: no
 |   D-pad, no second stick, and its own Start is already the menu.
 | Author: suinevere
 | Dependencies: SRL (Input::Analog)
 | Globals: g_nav
 | Params: port -- the port the wheel is on
 | Returns: N/A
 ----------------------*/
static void read_wheel(int port) {
    SRL::Input::Analog a(port);
    if (!a.IsConnected()) return;

    int step = axis_step(port, 0, axis_dir((int) a.GetAxis(SRL::Input::Analog::Axis::Axis1)));
    if (step < 0) g_nav[NAV_LEFT]  = 1;
    if (step > 0) g_nav[NAV_RIGHT] = 1;
    if (a.WasPressed(Button::R)) g_nav[NAV_UP]   = 1;
    if (a.WasPressed(Button::L)) g_nav[NAV_DOWN] = 1;
}

/*----------------------
 | g_dpad_held
 | Description: Frames the current digital direction has been held, which is what
 |   the D-pad cursor's ramp is measured in. Reset the moment nothing is held.
 | Author: suinevere
 ----------------------*/
static int g_dpad_held = 0;

/*----------------------
 | read_dpad_cursor
 | Description: Drives the cursor from the D-pad, for a device whose Mouse row
 |   names it -- a control pad has nothing else to offer, so this is the only way
 |   one gets a cursor at all. A held direction accelerates: one pixel a frame to
 |   begin with, so a cell can be picked, working up to CURSOR_STEP_MAX so the far
 |   corner is still reachable. The cost is that those directions stop stepping the
 |   selection in game (pad_fired withholds them), which is why the row starts Off.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_pad, g_ptr, g_dpad_held
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void read_dpad_cursor(void) {
    if (g_pad == nullptr) return;
    int dx = g_pad->IsHeld(Button::Right) ? 1 : g_pad->IsHeld(Button::Left) ? -1 : 0;
    int dy = g_pad->IsHeld(Button::Down)  ? 1 : g_pad->IsHeld(Button::Up)   ? -1 : 0;
    if (!dx && !dy) { g_dpad_held = 0; return; }
    int step = CURSOR_STEP_MIN + g_dpad_held / CURSOR_RAMP;
    if (step > CURSOR_STEP_MAX) step = CURSOR_STEP_MAX;
    g_dpad_held++;
    g_ptr.x = (int16_t) (g_ptr.x + dx * step);
    g_ptr.y = (int16_t) (g_ptr.y + dy * step);
    clamp_cursor();
}

/*----------------------
 | read_mouse
 | Description: Moves the cursor by however far the mouse has travelled since last
 |   frame and turns its three buttons into pointer clicks; its Start button is the
 |   Static sheet's "Blue button" and opens the menu.
 |
 |   Two things about the reading, both measured rather than assumed. It is a
 |   running total, not a per-frame delta, so the movement is the difference
 |   against last frame -- taken as a delta it kept pushing the cursor until the
 |   mouse was carried back to where it started. And Y is added, not subtracted:
 |   the reported sign already runs the way the screen does.
 | Author: suinevere
 | Dependencies: SRL (Input::Pointer)
 | Globals: g_ptr
 | Params: port -- the port the mouse is on
 | Returns: N/A
 ----------------------*/
static void read_mouse(int port) {
    using P = SRL::Input::Pointer;
    P m(port);
    if (!m.IsConnected()) return;

    SRL::Math::Types::Vector2D d = m.GetPosition();
    int px = (int) d.X.As<int16_t>();
    int py = (int) d.Y.As<int16_t>();
    int dx = 0, dy = 0;
    if (g_mouse_seen[port]) {
        dx = mouse_delta(px, (int) g_mouse_last[port][0]);
        dy = mouse_delta(py, (int) g_mouse_last[port][1]);
    }
    g_mouse_last[port][0] = (int16_t) px;
    g_mouse_last[port][1] = (int16_t) py;
    g_mouse_seen[port] = 1;

    if (dx || dy) {
        g_ptr.x = (int16_t) (g_ptr.x + mouse_travel(dx, 0));
        g_ptr.y = (int16_t) (g_ptr.y + mouse_travel(dy, 1));
        clamp_cursor();
    }

    if (m.IsHeld(P::Button::Left) || m.IsHeld(P::Button::Middle) ||
        m.IsHeld(P::Button::Right)) g_ptr.held = 1;

    if (m.WasPressed(P::Button::Start))  mark(DA_MENU, 0);
    if (m.WasPressed(P::Button::Left))   pointer_fire(DEV_BTN_LEFT);
    if (m.WasPressed(P::Button::Middle)) pointer_fire(DEV_BTN_MIDDLE);
    if (m.WasPressed(P::Button::Right))  pointer_fire(DEV_BTN_RIGHT);
}

/*----------------------
 | read_gun
 | Description: Places the cursor where a light gun is aimed and turns its trigger
 |   into a pointer click: on screen a left one, off screen a right one, which is
 |   the same Back a mouse's right button gives. A gun has one trigger and no way
 |   to point at a Cancel row it cannot see, so the shot that misses the raster is
 |   the only Back it has. The off-screen test is by coordinate and wants
 |   confirming against real hardware.
 | Author: suinevere
 | Dependencies: SRL (Input::Gun)
 | Globals: g_ptr
 | Params: port -- Gun::Player1 or Gun::Player2
 | Returns: N/A
 ----------------------*/
static void read_gun(int port) {
    using G = SRL::Input::Gun;
    G g((G::Player) port);
    if (!g.IsConnected()) return;

    SRL::Math::Types::Vector2D p = g.GetPosition();
    int px = (int) p.X.As<int16_t>();
    int py = (int) p.Y.As<int16_t>();
    int off = (px < 0 || py < 0 || px >= g_bound_w || py >= g_bound_h);

    g_ptr.offscreen = off;
    if (!off) {
        g_ptr.x = (int16_t) px;
        g_ptr.y = (int16_t) py;
        clamp_cursor();
    }

    if (!off && g.IsHeld(G::Button::Trigger)) g_ptr.held = 1;

    if (g.WasPressed(G::Button::Start)) mark(DA_MENU, 0);
    if (g.WasPressed(G::Button::Trigger)) pointer_fire(off ? DEV_BTN_RIGHT : DEV_BTN_LEFT);
}

/*----------------------
 | g_twin_chord_was
 | Description: The Twin Stick toggle chord's held latch, so the switch flips once
 |   per press rather than once per frame it is held.
 | Author: suinevere
 ----------------------*/
static bool g_twin_chord_was = false;

/*----------------------
 | twin_chord_tick
 | Description: Flips the Twin Stick profile on the rising edge of L+R+Z+X. That
 |   profile is the only thing that can tell a Twin Stick from a control pad -- the
 |   two report the same id and the same button word -- and it used to be a row on
 |   the Controls page, which is a poor place for it: a player whose stick is being
 |   read as a pad has to work a menu with the wrong bindings to say so. A chord
 |   works from wherever they are, and the same four buttons exist on both devices.
 |     Run from controller_tick, which every menu and both game loops call, so
 |   "anywhere" means anywhere. Not persisted: it says what is plugged in right
 |   now, and the next boot asks again.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_pad, g_twin, g_twin_chord_was
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void twin_chord_tick(void) {
    if (g_pad == nullptr) { g_twin_chord_was = false; return; }
    bool now = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R) &&
               g_pad->IsHeld(Button::Z) && g_pad->IsHeld(Button::X);
    if (now && !g_twin_chord_was) g_twin = g_twin ? 0 : 1;
    g_twin_chord_was = now;
}

/*----------------------
 | controller_tick
 | Description: Clears the frame's edges, then walks every port folding whatever
 |   is on it into actions and pointer motion. The pad and analogue families are
 |   read once rather than per port because MultiPad already aggregates both ports
 |   and both families behind one query. `valid` is rebuilt here from what is
 |   actually attached rather than latched by the readers, so unplugging the only
 |   pointing device takes the cursor away with it.
 | Author: suinevere
 | Dependencies: SRL, input.h
 | Globals: g_fired, g_ptr, g_ptr_fallback
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_tick(void) {
    for (int a = 0; a < DA_N; a++) g_fired[a][0] = g_fired[a][1] = g_fired[a][2] = 0;
    g_ptr.hot = 0;
    g_ptr.held = 0;
    g_ptr.offscreen = 0;
    g_ptr.valid = 0;
    g_ptr_fallback = -1;
    for (int d = 0; d < NAV_N; d++) g_nav[d] = 0;

    twin_chord_tick();

    int saw_pad = 0;
    int saw_twin = 0;
    int saw_stick = 0;

    for (int p = 0; p < PORT_N; p++) {
        DevKind k = controller_kind(p);
        switch (k) {
            case DEV_PAD:  saw_pad  = 1; break;
            case DEV_TWIN: saw_twin = 1; saw_stick = 1; break;
            case DEV_FLIGHT:
            case DEV_ANALOG:
                saw_pad = 1; saw_stick = 1;
                if (raw_id(p) == (uint8_t) SRL::Input::PeripheralType::Racing) read_wheel(p);
                else                                                          read_sticks(p, k);
                break;
            case DEV_MOUSE:  g_ptr.valid = 1; read_mouse(p); break;
            case DEV_GUN:
                if (p == SRL::Input::Gun::Player::Player1 ||
                    p == SRL::Input::Gun::Player::Player2) { g_ptr.valid = 1; read_gun(p); }
                break;
            default: break;
        }
    }

    if (saw_pad)  read_pad_family();
    if (saw_twin) read_twin();
    /* A cursor exists where the player has said what drives it. The stick readers
       above have already moved it if that is what they were pointed at; the D-pad
       is read here because it belongs to no one port -- MultiPad aggregates every
       pad's directions into one word. */
    for (int p = 0; p < PORT_N; p++) {
        DevKind k = controller_kind(p);
        if (k > DEV_NONE && k < DEV_KIND_N && g_cursor_src[k] != CSRC_OFF) g_ptr.valid = 1;
    }
    if (controller_dpad_is_cursor()) { g_ptr.valid = 1; read_dpad_cursor(); }
    (void) saw_stick;
}

/*----------------------
 | controller_feed_key
 | Description: Maps the two keyboard cells controls.xls fills in -- Esc for the
 |   menu and F8 for the map -- onto this frame's actions. Every other keyboard
 |   cell is blank because the keyboard types directly.
 | Author: suinevere
 | Dependencies: saturn_keyboard.h
 | Globals: N/A
 | Params: ke -- the event the caller's own poll returned
 | Returns: N/A
 ----------------------*/
void controller_feed_key(SaturnKeyEvent ke) {
    if (ke.kind == SATURN_KEY_ESCAPE) mark(DA_MENU, 0);
    if (ke.kind == SATURN_KEY_F8)     mark(DA_MAP,  0);
}

/*----------------------
 | controller_fired
 | Description: Reads back one of this frame's recorded edges.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fired
 | Params: a -- the action; dir -- -1, 0 or +1
 | Returns: nonzero if that action fired in that direction
 ----------------------*/
int controller_fired(DevAction a, int dir) {
    if (a < 0 || a >= DA_N || dir < -1 || dir > 1) return 0;
    return g_fired[a][dir + 1];
}

/*----------------------
 | controller_mouse_raw
 | Description: Copies the first mouse port's report bytes 2 to 7 and the x/y
 |   PerPoint claims, for the readout on the Mouse Mode sheet.
 | Author: suinevere
 | Dependencies: SRL (Input::Management)
 | Globals: N/A
 | Params: b -- six ints, report bytes 2..7; xy -- two ints, PerPoint x and y
 | Returns: 1 if a mouse was found, 0 otherwise
 ----------------------*/
int controller_mouse_raw(int *b, int *xy) {
    for (int p = 0; p < PORT_N; p++) {
        if (controller_kind(p) != DEV_MOUSE) continue;
        const uint8_t *raw = (const uint8_t *) SRL::Input::Management::GetRawData(p);
        if (raw == nullptr) return 0;
        for (int i = 0; i < 6; i++) b[i] = raw[2 + i];
        SRL::Input::Pointer m(p);
        SRL::Math::Types::Vector2D d = m.GetPosition();
        xy[0] = (int) d.X.As<int16_t>();
        xy[1] = (int) d.Y.As<int16_t>();
        return 1;
    }
    return 0;
}

/*----------------------
 | controller_any_fired
 | Description: Any action edge this frame, or a pointing device's click. The two
 |   together are the whole set: an off-screen gun shot marks DA_ACCEPT and sets no
 |   click, an on-screen one sets a click, and every other device marks an action.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fired, g_ptr
 | Params: N/A
 | Returns: nonzero if anything fired
 ----------------------*/
int controller_any_fired(void) {
    if (g_ptr.valid && g_ptr.hot) return 1;
    for (int a = 0; a < DA_N; a++)
        if (g_fired[a][0] || g_fired[a][1] || g_fired[a][2]) return 1;
    return 0;
}

/*----------------------
 | controller_pointer
 | Description: Hands back the module's pointer state.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr
 | Params: N/A
 | Returns: the DevPointer, valid until the next controller_tick
 ----------------------*/
const DevPointer *controller_pointer(void) { return &g_ptr; }

/*----------------------
 | controller_pointer_flush
 | Description: Discards a pending pointer edge. Call where a screen begins and
 |   would otherwise inherit the click that opened it -- the same debt
 |   mode_toggle_reset settles for the L+R combo. Without it the click that
 |   dismissed the title arrives again as the first frame of the menu behind it and
 |   picks whatever the cursor happens to be over.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr, g_ptr_fallback
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_pointer_flush(void) {
    g_ptr.hot = 0;
    g_ptr_fallback = -1;
    for (int a = 0; a < DA_N; a++) g_fired[a][0] = g_fired[a][1] = g_fired[a][2] = 0;
}

/*----------------------
 | controller_pointer_consume
 | Description: Clears the pointer's edge and withdraws the fallback action that
 |   edge contributed, so a click a view has already handled does not arrive a
 |   second time as a bare letter, backspace or accept.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr, g_ptr_fallback, g_fired
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_pointer_consume(void) {
    g_ptr.hot = 0;
    if (g_ptr_fallback >= 0) g_fired[g_ptr_fallback][1] = 0;
    g_ptr_fallback = -1;
}

/*----------------------
 | controller_twin_set
 | Description: Stores the Twin Stick profile switch.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_twin
 | Params: on -- nonzero to read 0x02 ports as Twin Sticks
 | Returns: N/A
 ----------------------*/
void controller_twin_set(int on) { g_twin = on ? 1 : 0; }

/*----------------------
 | controller_twin_get
 | Description: Reads the Twin Stick profile switch.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_twin
 | Params: N/A
 | Returns: the current setting
 ----------------------*/
int controller_twin_get(void) { return g_twin; }

/*----------------------
 | controller_cursor_src_count
 | Description: Counts the non-empty names device `k` has in CSRC_NAME.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the device kind
 | Returns: 0, 1 or 2
 ----------------------*/
int controller_cursor_src_count(DevKind k) {
    if (k < 0 || k >= DEV_KIND_N) return 0;
    int n = 0;
    for (int c = CSRC_OFF + 1; c < CSRC_N; c++) if (CSRC_NAME[k][c][0]) n++;
    return n;
}

/*----------------------
 | controller_cursor_src_name
 | Description: Indexes CSRC_NAME.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the device kind; src -- CSRC_DPAD or CSRC_STICK
 | Returns: the name, or "" when that device has no such source
 ----------------------*/
const char *controller_cursor_src_name(DevKind k, int src) {
    if (k < 0 || k >= DEV_KIND_N || src < 0 || src >= CSRC_N) return "";
    return CSRC_NAME[k][src];
}

/*----------------------
 | controller_cursor_src_set / controller_cursor_src_get / controller_cursor_src_cycle
 | Description: See controller.h. Set refuses a source the device does not have,
 |   so a stored value can never name an empty string; cycle walks the values that
 |   device does have, Off included, and wraps.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cursor_src
 | Params: k -- the device kind; src -- a CSRC_* value; dir -- -1 or +1
 | Returns: get returns the current source
 ----------------------*/
void controller_cursor_src_set(DevKind k, int src) {
    if (k < 0 || k >= DEV_KIND_N || src < 0 || src >= CSRC_N) return;
    if (src != CSRC_OFF && CSRC_NAME[k][src][0] == 0) return;
    g_cursor_src[k] = (uint8_t) src;
}

int controller_cursor_src_get(DevKind k) {
    if (k < 0 || k >= DEV_KIND_N) return CSRC_OFF;
    return g_cursor_src[k];
}

void controller_cursor_src_cycle(DevKind k, int dir) {
    if (k < 0 || k >= DEV_KIND_N || dir == 0) return;
    int c = g_cursor_src[k];
    for (int step = 0; step < CSRC_N; step++) {
        c = (c + (dir > 0 ? 1 : CSRC_N - 1)) % CSRC_N;
        if (c == CSRC_OFF || CSRC_NAME[k][c][0]) { g_cursor_src[k] = (uint8_t) c; return; }
    }
}

/*----------------------
 | controller_pointer_place
 | Description: See controller.h. Clamped like any other cursor move, so a caller
 |   naming a cell off the bounds rectangle lands on its edge rather than outside.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr
 | Params: col -- text column; row -- text row
 | Returns: N/A
 ----------------------*/
void controller_pointer_place(int col, int row) {
    g_ptr.x = (int16_t) (col * CELL_W);
    g_ptr.y = (int16_t) (row * CELL_H);
    clamp_cursor();
}

/*----------------------
 | controller_dpad_is_cursor
 | Description: True while some attached device has its digital directions pointed
 |   at the cursor. A device nobody has plugged in cannot be steering anything, so
 |   the walk is over the ports rather than over the table.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cursor_src
 | Params: N/A
 | Returns: nonzero while the D-pad belongs to the cursor
 ----------------------*/
int controller_dpad_is_cursor(void) {
    for (int p = 0; p < PORT_N; p++) {
        DevKind k = controller_kind(p);
        if (k > DEV_NONE && k < DEV_KIND_N && g_cursor_src[k] == CSRC_DPAD) return 1;
    }
    return 0;
}

/*----------------------
 | controller_mouse_speed_set
 | Description: Stores the mouse speed step, clamped into range.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mouse_speed
 | Params: n -- the step
 | Returns: N/A
 ----------------------*/
void controller_mouse_speed_set(int n) {
    if (n < 0) n = 0;
    if (n >= CTL_MOUSE_SPEED_N) n = CTL_MOUSE_SPEED_N - 1;
    g_mouse_speed = n;
}

/*----------------------
 | controller_mouse_speed_get
 | Description: Reads the mouse speed step.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mouse_speed
 | Params: N/A
 | Returns: the step
 ----------------------*/
int controller_mouse_speed_get(void) { return g_mouse_speed; }

/*----------------------
 | controller_wedged
 | Description: Reads the latch recording that a port has reported the wedged id.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_wedged
 | Params: N/A
 | Returns: nonzero once the wedge has been seen
 ----------------------*/
int controller_wedged(void) { return g_wedged; }

/*----------------------
 | controller_cursor_bounds
 | Description: Sets the cursor's clamp rectangle and pulls the cursor inside it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_bound_w, g_bound_h
 | Params: w -- width in pixels; h -- height in pixels
 | Returns: N/A
 ----------------------*/
void controller_cursor_bounds(int w, int h) {
    if (w > 0) g_bound_w = w;
    if (h > 0) g_bound_h = h;
    clamp_cursor();
}

/*----------------------
 | controller_init
 | Description: Clears every latch and parks the cursor in the middle of the
 |   bounds rectangle, so a pointing device plugged in later starts somewhere
 |   visible rather than in a corner.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: all of this file's statics
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void controller_init(void) {
    for (int a = 0; a < DA_N; a++) g_fired[a][0] = g_fired[a][1] = g_fired[a][2] = 0;
    for (int p = 0; p < PORT_N; p++) { g_axis[p][0] = AxisRep{0, 0}; g_axis[p][1] = AxisRep{0, 0}; }
    for (int i = 0; i < DEV_HOLD_N; i++) g_hold[i] = HoldRep{0, -1};
    for (int p = 0; p < PORT_N; p++) { g_mouse_seen[p] = 0; g_mouse_last[p][0] = 0; g_mouse_last[p][1] = 0; }
    g_mouse_rem[0] = g_mouse_rem[1] = 0;
    g_rstick_held = 0;
    g_ptr = DevPointer{0, 0, 0, 0,
                       (int16_t) (g_bound_w / 2), (int16_t) (g_bound_h / 2),
                       0, 0, DEV_BTN_LEFT};
    clamp_cursor();
    g_ptr_fallback = -1;
    g_wedged = 0;
    g_twin_chord_was = false;
}

/*----------------------
 | the ordering contract this module depends on
 | Description: controller_tick reads the pad through pad_fired and chord_fired
 |   rather than re-deriving the mapping, which means it must run after the frame's
 |   pad_repeat_update and chord_tick, not before -- those two advance the timers
 |   pad_fired and chord_fired report on, and a tick that ran first would read the
 |   previous frame's edges. It must not call them itself either: they are not
 |   idempotent, and a second call per frame doubles every repeat rate.
 | Author: suinevere
 ----------------------*/
