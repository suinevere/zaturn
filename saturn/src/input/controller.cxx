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
 | CURSOR_STEP / CURSOR_DIV
 | Description: Pixels the cursor moves per frame at full stick deflection, and
 |   the divisor applied to a mouse's reported delta. The mouse divisor exists
 |   because the Saturn mouse reports a raw count per frame that is far finer than
 |   a 320-pixel screen wants.
 | Author: suinevere
 ----------------------*/
static const int CURSOR_STEP = 4;
static const int CURSOR_DIV  = 2;

/*----------------------
 | TWIN_TRIG_L / TWIN_TRIG_R / TWIN_TOP_L / TWIN_TOP_R
 | Description: PROVISIONAL. Which bits of a standard digital report the Twin
 |   Stick's four thumb and trigger buttons are assumed to sit on. A Twin Stick
 |   reports id 0x02 like any control pad, so nothing here has been read off real
 |   hardware; this table is the single place to correct once somebody has.
 | Author: suinevere
 ----------------------*/
static const Button TWIN_TRIG_L = Button::L;
static const Button TWIN_TRIG_R = Button::R;
static const Button TWIN_TOP_L  = Button::A;
static const Button TWIN_TOP_R  = Button::C;

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
 | g_twin / g_mouse_mode / g_wedged
 | Description: The player's Twin Stick profile switch, the Mouse Mode switch, and
 |   the latch recording that a wedged port has been seen at least once.
 | Author: suinevere
 ----------------------*/
static int g_twin       = 0;
static int g_mouse_mode = 0;
static int g_wedged     = 0;

/*----------------------
 | g_bound_w / g_bound_h
 | Description: The rectangle the cursor is clamped inside, in screen pixels.
 | Author: suinevere
 ----------------------*/
static int g_bound_w = 320;
static int g_bound_h = 224;

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
 | controller_present
 | Description: Walks the ports looking for one classified as `k`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to look for
 | Returns: 1 if found, 0 otherwise
 ----------------------*/
int controller_present(DevKind k) {
    for (int p = 0; p < PORT_N; p++) if (controller_kind(p) == k) return 1;
    return 0;
}

/*----------------------
 | controller_kind_name
 | Description: Indexes a fixed name table by kind.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the DevKind to name
 | Returns: its display string; "None" when out of range
 ----------------------*/
const char *controller_kind_name(DevKind k) {
    static const char *N[DEV_KIND_N] = {
        "None", "6 Pad", "Flight Stick", "Analogue",
        "Mouse", "Twin Stick", "Light Gun", "Keyboard"
    };
    if (k < 0 || k >= DEV_KIND_N) return "None";
    return N[k];
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
}

/*----------------------
 | pointer_fire
 | Description: Records a pointing-device click at the current cursor position and
 |   contributes the non-positional fallback action that button carries, keeping
 |   the fallback's identity so controller_pointer_consume can withdraw it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ptr, g_ptr_fallback
 | Params: button -- DEV_BTN_LEFT, _MIDDLE or _RIGHT
 | Returns: N/A
 ----------------------*/
static void pointer_fire(int button) {
    static const DevAction FALLBACK[3] = { DA_LETTER, DA_BACK, DA_ACCEPT };
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
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void read_pad_family(void) {
    if (g_pad == nullptr) return;

    if (g_pad->WasPressed(Button::START)) mark(DA_MENU, 0);

    if (pad_fired(face_button(FA_TYPE)))   mark(DA_LETTER, 0);
    if (pad_fired(face_button(FA_BACK)))   mark(DA_BACK,   0);
    if (pad_fired(face_button(FA_SPACE)))  mark(DA_SPACE,  0);
    if (pad_fired(face_button(FA_ACCEPT))) mark(DA_ACCEPT, 0);

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
}

/*----------------------
 | read_sticks
 | Description: Reads one analogue port's stick: on a flight stick its two axes
 |   scroll a line and turn a page, per the Scrolling sheet; on either kind it
 |   drives the cursor while Mouse Mode is on. The analogue pad's scrolling cells
 |   are blank, so only the flight stick contributes scroll edges.
 | Author: suinevere
 | Dependencies: SRL (Input::Analog)
 | Globals: g_ptr, g_mouse_mode
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

    if (g_mouse_mode && (dx || dy)) {
        g_ptr.x = (int16_t) (g_ptr.x + dx * CURSOR_STEP);
        g_ptr.y = (int16_t) (g_ptr.y + dy * CURSOR_STEP);
        clamp_cursor();
    }
}

/*----------------------
 | read_mouse
 | Description: Accumulates a mouse's relative movement into the cursor and turns
 |   its three buttons into pointer clicks; its Start button is the Static sheet's
 |   "Blue button" and opens the menu.
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
    int dx = (int) d.X.As<int16_t>();
    int dy = (int) d.Y.As<int16_t>();

    if (dx || dy) {
        g_ptr.x = (int16_t) (g_ptr.x + dx / CURSOR_DIV);
        g_ptr.y = (int16_t) (g_ptr.y - dy / CURSOR_DIV);
        clamp_cursor();
    }

    if (m.WasPressed(P::Button::Start))  mark(DA_MENU, 0);
    if (m.WasPressed(P::Button::Left))   pointer_fire(DEV_BTN_LEFT);
    if (m.WasPressed(P::Button::Middle)) pointer_fire(DEV_BTN_MIDDLE);
    if (m.WasPressed(P::Button::Right))  pointer_fire(DEV_BTN_RIGHT);
}

/*----------------------
 | read_gun
 | Description: Places the cursor where a light gun is aimed and turns its trigger
 |   into a pointer click, except that a shot outside the bounds rectangle is the
 |   light gun column's "Shoot off screen" and accepts instead. The off-screen
 |   test is by coordinate and wants confirming against real hardware.
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
    }

    if (g.WasPressed(G::Button::Start)) mark(DA_MENU, 0);
    if (g.WasPressed(G::Button::Trigger)) {
        if (off) mark(DA_ACCEPT, 0);
        else     pointer_fire(DEV_BTN_LEFT);
    }
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
    g_ptr.offscreen = 0;
    g_ptr.valid = 0;
    g_ptr_fallback = -1;

    int saw_pad = 0;
    int saw_twin = 0;
    int saw_stick = 0;

    for (int p = 0; p < PORT_N; p++) {
        DevKind k = controller_kind(p);
        switch (k) {
            case DEV_PAD:  saw_pad  = 1; break;
            case DEV_TWIN: saw_twin = 1; saw_stick = 1; break;
            case DEV_FLIGHT:
            case DEV_ANALOG: saw_pad = 1; saw_stick = 1; read_sticks(p, k); break;
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
    if (g_mouse_mode && (saw_stick || saw_pad)) g_ptr.valid = 1;
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
 | controller_mouse_mode_set
 | Description: Stores the Mouse Mode switch.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mouse_mode
 | Params: on -- nonzero for cursor, zero for selection
 | Returns: N/A
 ----------------------*/
void controller_mouse_mode_set(int on) { g_mouse_mode = on ? 1 : 0; }

/*----------------------
 | controller_mouse_mode_get
 | Description: Reads the Mouse Mode switch.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mouse_mode
 | Params: N/A
 | Returns: the current setting
 ----------------------*/
int controller_mouse_mode_get(void) { return g_mouse_mode; }

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
    g_ptr = DevPointer{0, 0, 0, (int16_t) (g_bound_w / 2), (int16_t) (g_bound_h / 2), DEV_BTN_LEFT};
    g_ptr_fallback = -1;
    g_wedged = 0;
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
