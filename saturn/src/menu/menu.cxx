/*----------------------
 | menu.cxx
 | Description: Implements the menu-drawing framework: box chrome and the
 |   VDP2 window that suppresses a background image behind it, the refcounted
 |   opaque-backing guard, and the modal wait/message/list/confirm primitives.
 |   Pure UI mechanism -- no page owns any state here; every page constructs a
 |   MenuBacking and calls into these primitives to draw and read input.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.c, app_state.h, console_view.cxx, input.h,
 |   saturn_keyboard.h, soft_reset.h (defined in main.cxx), sound.c, music.c,
 |   dash_view.h/dash_map.c (the NBG2 border), SRL
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "menu.h"
#ifndef NETBIN
#include "field_clock.h"
#endif
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
#include "dash_view.h"
#ifndef NETBIN
// The screen-wide fade primitives live in title.cxx, which this build links and
// the netbin one does not. Netbin shows no wallpaper at all -- display_apply's
// image branch is compiled out there -- so its fades drive both layers off
// channel A directly, the way every fade did before the wallpaper dim existed.
#include "title.h"
#endif

extern "C" {
#include "menu_layout.h"
#include "sound.h"
#include "music.h"
#include "display.h"
}

#ifdef NETBIN
/*----------------------
 | g_menu_service / g_menu_service_ctx
 | Description: The callback menu_sync runs each frame and its context. Null
 |   unless a caller has registered one; see menu_set_service.
 | Author: suinevere
 ----------------------*/
static MenuServiceFn g_menu_service     = nullptr;
static void         *g_menu_service_ctx = nullptr;
#endif

/*----------------------
 | menu_sync
 | Description: Services looping PCM sound (sound_service) and advances the
 |   music mixer (music_tick, which commits any debounced Dynamic-mix switch
 |   or one-shot mix) before synchronizing to vblank. The looping-PCM
 |   ping-pong hand-off needs sound_service() called every frame or it starves
 |   and goes silent, which a bare Synchronize() would not provide -- every
 |   loop that holds a screen while waiting calls this instead, menu or not.
 |   That is not a style preference. CD-DA tracks are issued a pass at a time
 |   so the engine can count them, so music_tick is what starts the next pass:
 |   a wait loop that calls Synchronize() directly plays the current track
 |   once and then sits in silence for as long as the player stays on it. The
 |   title screen did exactly that, which is how this rule was learned.
 | Author: suinevere
 | Dependencies: sound.c, music.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 |   Under NETBIN both service calls are compiled out: that build links no
 |   sound or music object at all, and the loops that call this hold a dial
 |   page or a telnet terminal, neither of which has audio to starve. What that
 |   build has instead is a carrier to starve, so the registered service runs in
 |   the same slot -- see menu_set_service.
 ----------------------*/
void menu_sync(void) {
#ifdef NETBIN
    if (g_menu_service != nullptr) g_menu_service(g_menu_service_ctx);
#else
    sound_service();
    music_tick();
#endif
    dash_box_hold();
    SRL::Core::Synchronize();
}

#ifdef NETBIN
/*----------------------
 | menu_set_service
 | Description: See menu.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_menu_service, g_menu_service_ctx
 | Params: fn -- the callback, or NULL to clear; ctx -- passed back to fn
 | Returns: N/A
 ----------------------*/
void menu_set_service(MenuServiceFn fn, void *ctx) {
    g_menu_service     = fn;
    g_menu_service_ctx = ctx;
}
#endif

/*----------------------
 | menu_clear
 | Description: Clears every console text row via text_clear_line.
 | Author: suinevere
 | Dependencies: text_map.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_clear(void) {
    for (int r = 0; r <= 28; r++) text_clear_line(r);
}

/*----------------------
 | g_menu_intro_fade
 | Description: See menu.h. 0 = the next menu_select draws instantly; >0 = it
 |   fades its first frame up from black over that many fields, then clears
 |   itself back to 0. Only main() sets it, once, for the title -> mode-select
 |   hand-off; every other menu_select caller leaves it 0 and is unaffected.
 | Author: suinevere
 ----------------------*/
int g_menu_intro_fade = 0;

/*----------------------
 | g_menu_page_fade
 | Description: See menu.h. Ramp length the Options pages (menu_pages.cxx) read
 |   for their entry/exit fades; 0 = instant. main() sets it around its
 |   options_menu() call and clears it after, so in-game page openings stay
 |   instant.
 | Author: suinevere
 ----------------------*/
int g_menu_page_fade = 0;

/*----------------------
 | menu_intro_scale
 | Description: Scales a packed RGB555 colour's three channels by (255+v)/255,
 |   v in -255..0 (so -255 is black, 0 is the colour unchanged). Fades the
 |   solid backdrop plane, which -- unlike NBG0/NBG3 -- cannot take a VDP2
 |   hardware colour offset and must be re-written each frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: packed -- a DISP_RGB555 colour; v -- offset, -255..0
 | Returns: the scaled colour, same packed format
 ----------------------*/
static unsigned short menu_intro_scale(unsigned short packed, int v) {
    int f = 255 + v;
    if (f < 0)   f = 0;
    if (f > 255) f = 255;
    int r = ( packed        & 0x1F) * f / 255;
    int g = ((packed >> 5)  & 0x1F) * f / 255;
    int b = ((packed >> 10) & 0x1F) * f / 255;
    return (unsigned short) (0x8000 | (b << 10) | (g << 5) | r);
}

/*----------------------
 | menu_offset_engage / menu_offset_release
 | Description: Claim and release the layers a menu fade drives. The shared
 |   screen-wide fade (title.h) owns them: NBG3 and NBG2 on colour offset channel
 |   A, the background picture on channel B where the player's held wallpaper dim
 |   is composed in. NBG2 -- the menu borders and the input dashboard -- goes on A
 |   with the text it frames rather than on B with the picture: it is chrome, not
 |   scenery, and a border composed into the wallpaper dim would darken every time
 |   the player dimmed the wallpaper.
 |
 |   Going through the shared fade rather than pointing both layers at channel A
 |   here is what keeps a dimmed wallpaper dimmed across a menu fade -- taking the
 |   picture onto A dropped the dim for the length of the fade and left it
 |   dropped, so the Display page's Dimming row then appeared to do nothing until
 |   it was stepped back to Normal.
 | Author: suinevere
 | Dependencies: title.h (non-netbin), SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void menu_offset_engage(void) {
#ifdef NETBIN
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG2::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
#else
    title_bg_fade_engage();
#endif
}

static void menu_offset_release(void) {
#ifdef NETBIN
    SRL::VDP2::ColorOffset zero((int16_t) 0, (int16_t) 0, (int16_t) 0);
    SRL::VDP2::SetColorOffsetA(zero);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG2::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
#else
    title_bg_fade_reset();
#endif
}

/*----------------------
 | menu_intro_level
 | Description: Sets one brightness level for the menu intro fade: the shared
 |   screen-wide fade (text on colour offset A, picture on B with the held
 |   wallpaper dim composed in) plus the software-scaled backdrop colour, so the
 |   background picture, the menu text, and the solid fill all move together.
 |   SetColorOffsetA takes a non-const reference, so the ColorOffset is a named
 |   local.
 | Author: suinevere
 | Dependencies: title.h (non-netbin), SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: v -- offset, -255..0
 | Returns: N/A
 ----------------------*/
static void menu_intro_level(int v) {
#ifdef NETBIN
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetA(off);
#else
    title_bg_fade_level(v);
#endif
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(
        menu_intro_scale(display_bg_rgb(g_display.bg), v)));
}

/*----------------------
 | menu_intro_fade_arm
 | Description: See menu.h. Engages the screen-wide fade and forces the whole
 |   screen black, so the menu that draws next is composed unseen.
 |   Call AFTER display_apply() (which sets the real backdrop colour): this
 |   overrides it back to black until menu_select runs the ramp.
 | Author: suinevere
 | Dependencies: title.h (non-netbin), SRL
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_intro_fade_arm(void) {
    menu_offset_engage();
    menu_intro_level(-255);
}

/*----------------------
 | menu_fade_out
 | Description: See menu.h. Engages the screen-wide fade, then ramps the whole
 |   screen -- background picture, backdrop colour, and menu text together --
 |   from normal down to black over `frames` fields, leaving it held black
 |   (channels still engaged) for whatever fades it back in.
 | Author: suinevere
 | Dependencies: title.h (non-netbin), SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: frames -- ramp length in fields
 | Returns: N/A
 ----------------------*/
void menu_fade_out(int frames) {
    menu_offset_engage();
    for (int i = 0; i <= frames; i++) {
        menu_intro_level(-(255 * i) / frames);
        // A fade holds the screen for `frames` frames without redrawing, so
        // without this the box it is fading out vanishes on the first of them.
        dash_box_hold();
        SRL::Core::Synchronize();
    }
}

#ifndef NETBIN
/*----------------------
 | g_out_from / g_out_span / g_out_running
 | Description: The clock-paced fade-out's state: the field it started on, how
 |   many fields it runs for, and whether one is in flight.
 | Author: suinevere
 ----------------------*/
static unsigned int g_out_from    = 0;
static int          g_out_span    = 0;
static bool         g_out_running = false;

/*----------------------
 | menu_fade_out_begin / menu_fade_out_tick / menu_fade_out_hold
 | Description: See menu.h. The level comes from how many fields have actually
 |   elapsed rather than from how many times tick was called, which is the whole
 |   difference between this and menu_fade_out: a caller reading the CD blocks the
 |   main line for whole frames at a time, and a ramp that stepped once per call
 |   would simply be stretched by the reading instead of covering it.
 | Author: suinevere
 | Dependencies: field_clock.h, SRL
 | Globals: g_out_from, g_out_span, g_out_running
 | Params: frames -- ramp length in fields
 | Returns: N/A
 ----------------------*/
void menu_fade_out_begin(int frames) {
    menu_offset_engage();
    menu_intro_level(0);
    field_clock_start();
    g_out_from    = field_clock_now();
    g_out_span    = (frames < 1) ? 1 : frames;
    g_out_running = true;
}

void menu_fade_out_tick(void) {
    if (!g_out_running) return;
    unsigned int done = field_clock_now() - g_out_from;
    if (done >= (unsigned int) g_out_span) {
        menu_intro_level(-255);
        g_out_running = false;
        return;
    }
    menu_intro_level(-(int) ((255u * done) / (unsigned int) g_out_span));
}

void menu_fade_out_hold(void) {
    while (g_out_running) {
        menu_fade_out_tick();
        if (!g_out_running) break;
        SRL::Core::Synchronize();
    }
    menu_intro_level(-255);
}
#endif /* NETBIN: no CD, no story, nothing to fade a load behind */

/*----------------------
 | menu_fade_in / menu_fade_in_ex
 | Description: See menu.h. Ramps a held-black screen back to normal over
 |   `frames` fields, then releases the offset channels and restores the full
 |   backdrop colour. "Normal" here is the player's held wallpaper dim, not an
 |   unmodified picture -- the release goes through title_bg_fade_reset, which is
 |   what leaves the dim in force on the far side of the ramp. The caller must
 |   have engaged the channels and held black already (via menu_fade_out,
 |   menu_intro_fade_arm, or menu_fade_out_hold, which deliberately leaves them
 |   engaged) and drawn its first frame, or there is nothing to reveal.
 |
 |   The _ex form takes a per-frame callback handed the same 0..255 the picture is
 |   being lit by, for anything that has to rise in step with it. Deliberately not
 |   a place to dim the wallpaper as well: the ramp already drives the picture on
 |   channel B with the dim composed in, and title_bg_dyn_fade is inert for as
 |   long as this fade is engaged.
 | Author: suinevere
 | Dependencies: title.h (non-netbin), SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: frames -- ramp length in fields
 | Returns: N/A
 ----------------------*/
void menu_fade_in_ex(int frames, MenuFadeStep step) {
    for (int i = 0; i <= frames; i++) {
        int level = (255 * i) / frames;
        menu_intro_level(-255 + level);
        // Before the Synchronize, so whatever the step sets is in effect for the
        // frame that brightness belongs to -- the same ordering title_bg_fade_in_ex
        // uses, and the reason the CD-DA rises with the picture rather than behind it.
        if (step) step(level);
        dash_box_hold();      // as in menu_fade_out: a fade redraws nothing
        SRL::Core::Synchronize();
    }
    menu_offset_release();
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
}

void menu_fade_in(int frames) { menu_fade_in_ex(frames, 0); }

/*----------------------
 | menu_fade_clear
 | Description: See menu.h. Pushes the composed frame, then instantly (no ramp)
 |   reveals a held-black screen: releases the screen-wide fade (which restores
 |   the picture to the held wallpaper dim rather than to nothing) and restores
 |   the full backdrop colour. Used to cut straight into gameplay
 |   after the menu has faded to black and the story has loaded behind the black,
 |   and to make an error screen visible if a load fails mid-black.
 |
 |   The text flush is not optional here and not merely tidy. The shadow reaches
 |   VRAM on OnAfterSync, and both call sites clear the screen and reveal it
 |   without a Synchronize in between -- main()'s game hand-off runs the trie
 |   build and the cache warm between the two, neither of which syncs -- so
 |   releasing the hold uncovered a tilemap still holding the loading screen's
 |   boot block, which then sat there until the interpreter's first render
 |   pushed a frame. Flushing off-beam is safe at this one point precisely
 |   because the screen is still held black when it happens.
 | Author: suinevere
 | Dependencies: SRL, text_map.h, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_fade_clear(void) {
    text_flush();
    menu_offset_release();
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
}

// ---- opaque backing for menus over an image --------------------------------

/*----------------------
 | menu_window_rect
 | Description: Aims the image-suppressing window at a menu box. Called on every
 |   menu_frame draw rather than once on open, so a nested page's box takes over
 |   the window while it is up and the outer page's box is restored the moment it
 |   redraws. The window itself lives in console_view.c, shared with the in-game
 |   interface strip, which blacks its own rectangle the same way.
 | Author: suinevere
 | Dependencies: console_view.h (image_window_box)
 | Globals: N/A
 | Params: x0, y0 -- top-left corner in text cells; w, h -- box width/height in
 |   cells
 | Returns: N/A
 ----------------------*/
static void menu_window_rect(int x0, int y0, int w, int h) {
    image_window_box(x0, y0, w, h);
}

/*----------------------
 | g_menu_backing_depth
 | Description: Backs MenuBacking's refcount (declared extern in menu.h). Also
 |   reset to 0 by main()'s soft-reset recovery path, since the longjmp skips the
 |   destructors that would otherwise balance it -- see menu.h.
 | Author: suinevere
 ----------------------*/
int g_menu_backing_depth = 0;

/*----------------------
 | MenuBacking::MenuBacking
 | Description: On the outermost construction (refcount 0 -> 1), cancels any
 |   window-off still owed by an earlier guard and switches on the
 |   image-suppressing VDP2 window for NBG0. Refcounted so a nested page
 |   (e.g. Options opening Display) does not disturb the outer page's
 |   windowing when it opens.
 | Author: suinevere
 | Dependencies: console_view.h (image_window_on)
 | Globals: g_menu_backing_depth
 | Params: N/A
 | Returns: N/A
 ----------------------*/
MenuBacking::MenuBacking() {
    if (g_menu_backing_depth++ == 0) image_window_on();
}

/*----------------------
 | menu_backing_window_off
 | Description: Switches the image-suppressing window back off. Deferred through
 |   text_on_flush rather than run from ~MenuBacking, because the guard object
 |   dies at the `return` that picked a menu item and the box it backs is still
 |   on screen at that point -- every caller either fades the box out or draws
 |   over it afterwards. Dropping the window there put the wallpaper back inside
 |   the box's rectangle while its text was still lit, so a main-menu pick showed
 |   the picture through the box for the whole of the outgoing fade.
 | Author: suinevere
 | Dependencies: console_view.h (image_window_off)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void menu_backing_window_off(void) {
    image_window_off();
}

/*----------------------
 | MenuBacking::~MenuBacking
 | Description: On the outermost destruction (refcount 1 -> 0), owes the window
 |   off to the next frame that changes the text (see menu_backing_window_off)
 |   rather than switching it off here. A destructor rather than a paired call
 |   because every page has several exit paths and forgetting to undo the
 |   windowing on one of them is the exact bug shape that has already cost this
 |   project a release.
 | Author: suinevere
 | Dependencies: text_map.h
 | Globals: g_menu_backing_depth
 | Params: N/A
 | Returns: N/A
 ----------------------*/
MenuBacking::~MenuBacking() {
    if (--g_menu_backing_depth == 0) text_on_flush(&menu_backing_window_off);
}

/*----------------------
 | menu_frame
 | Description: Aims the image-suppressing window at (x0, y0, w, h) via
 |   menu_window_rect, draws the box's border, and centers `title` on the second
 |   row. The border is an NBG2 bevel via dash_box when that layer is up and the
 |   printed +--+ chrome when it is not; either way the interior is cleared to
 |   spaces, so the two forms occupy the same cells and every caller's text
 |   lands in the same place.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: x0, y0, w, h -- box geometry in text cells; title -- centered on row
 |   y0 + 1
 | Returns: N/A
 ----------------------*/
void menu_frame(int x0, int y0, int w, int h, const char *title) {
    menu_window_rect(x0, y0, w, h);
    // The chrome comes from NBG2 when the layer is up and from the text font
    // when it is not, exactly as the gamepad strip's border does. The printed
    // form is not dead code: it is what a failed VRAM allocation falls back to.
    bool chrome = (dash_ready() == 0);
    if (!chrome) dash_box(x0, y0, w, h);
    for (int r = 0; r < h; r++) {
        char line[42]; int p = 0;
        for (int c = 0; c < w && p < (int) sizeof(line) - 1; c++)
            line[p++] = !chrome ? ' '
                      : (r == 0 || r == h - 1) ? ((c == 0 || c == w - 1) ? '+' : '-')
                      : ((c == 0 || c == w - 1) ? '|' : ' ');
        line[p] = '\0';
        text_print(x0, y0 + r, "%s", line);
    }
    int len = 0; while (title[len]) len++;
    int tx = x0 + (w - len) / 2;
    if (tx < x0 + 1) tx = x0 + 1;
    text_print(tx, y0 + 1, "%s", title);
}

/*----------------------
 | menu_pad
 | Description: Right-pads `s` to `w` columns into one of four rotating scratch
 |   buffers, so a single menu_rowf call can pad more than one field. Four is
 |   the most any row in this project pads at once; the ring, rather than one
 |   buffer, is what makes that legal.
 | Author: suinevere
 | Dependencies: menu_layout.h (MENU_SCREEN_COLS)
 | Globals: N/A
 | Params: s -- the text; w -- column width
 | Returns: the padded copy, valid until the ring wraps
 ----------------------*/
const char *menu_pad(const char *s, int w) {
    static char ring[4][MENU_SCREEN_COLS + 1];
    static int  next = 0;
    char *b = ring[next];
    next = (next + 1) & 3;
    int n = 0;
    while (s[n] && n < MENU_SCREEN_COLS) { b[n] = s[n]; n++; }
    while (n < w && n < MENU_SCREEN_COLS) b[n++] = ' ';
    b[n] = 0;
    return b;
}

/*----------------------
 | menu_num
 | Description: The three-column row-number prefix, or three spaces when the
 |   digits are hidden or the row is past the tenth. Two rotating buffers, like
 |   menu_pad's four, so a row that numbers two fields does not read one
 |   prefix twice.
 | Author: suinevere
 | Dependencies: menu_layout.h (menu_row_digit_char)
 | Globals: N/A
 | Params: nums -- nonzero when digit shortcuts are shown; row -- 0-based row
 | Returns: a three-column prefix
 ----------------------*/
const char *menu_num(int nums, int row) {
    static char ring[2][4];
    static int  next = 0;
    char *b = ring[next];
    next ^= 1;
    char d = nums ? menu_row_digit_char(row) : 0;
    b[0] = d ? d : ' ';
    b[1] = d ? ')' : ' ';
    b[2] = ' ';
    b[3] = 0;
    return b;
}

/*----------------------
 | menu_row
 | Description: Right-pads a composed row to `pad` columns, centers it inside
 |   the box interior and draws it, in reverse video when it is the selected
 |   one. The centering matches menu_frame's title so a row and the title above
 |   it share an axis; the padding is what gives a whole list one left edge
 |   (see menu.h); and the left clamp keeps a row wider than its box off the
 |   border rather than letting a negative offset walk it outside.
 | Author: suinevere
 | Dependencies: text_map.h, menu_layout.h (MENU_SCREEN_COLS)
 | Globals: N/A
 | Params: x0 -- box left column; w -- box width; y -- row; sel -- nonzero to
 |   highlight; pad -- width to pad to, or 0; text -- the composed row
 | Returns: N/A
 ----------------------*/
void menu_row(int x0, int w, int y, int sel, int pad, const char *text) {
    char buf[MENU_SCREEN_COLS + 1];
    int len = 0;
    while (text[len] && len < MENU_SCREEN_COLS) { buf[len] = text[len]; len++; }
    while (len < pad && len < MENU_SCREEN_COLS) buf[len++] = ' ';
    buf[len] = 0;
    int x = x0 + 2 + ((w - 4) - len) / 2;
    if (x < x0 + 1) x = x0 + 1;
    if (sel) text_print_hl(x, y, buf);
    else     text_print_str(x, y, buf);
}

/*----------------------
 | menu_wait
 | Description: Drops the current frame's edge with one menu_sync, then polls
 |   both the gamepad face/start buttons and the keyboard every frame until one
 |   of them fires. menu_sync rather than a bare Synchronize, which is what this
 |   used to call: a loop that holds a screen has to service sound and re-claim
 |   the menu's border, and this one holds it for however long the player takes
 |   to press a key.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_wait(void) {
    menu_sync();
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        menu_sync();
    }
}

/*----------------------
 | menu_message
 | Description: Sizes a box (menu_box_fit) to fit whichever of line1/line2 is
 |   longer -- so a hint passed as line2 is budgeted like any other row -- and
 |   draws it once via menu_clear + menu_frame, with one blank row between
 |   line1 and line2 (every caller passes line2 as a "(press any key)"-style
 |   hint, so it reads as the box's controls line, one gap below the message
 |   rather than stacked directly under it). Where a caller passes a
 |   device-paired hint() string as line2, the pad and keyboard variants must
 |   be the same length (e.g. "L+R = cancel" / "Esc = cancel", both 12) so the
 |   box does not resize when the player switches input device mid-screen; if
 |   a pair ever differs, size the box off the longer one. Returns immediately
 |   without waiting or synchronizing: the save/load result screens follow it
 |   with menu_wait(), while the dialing screens redraw it every frame. The
 |   caller owns any MenuBacking guard -- screens that are a single blocking
 |   message construct one; loops that already hold one do not need a second.
 | Author: suinevere
 | Dependencies: menu_layout.c, SRL
 | Globals: N/A
 | Params: title -- box title; line1 -- first line of text; line2 -- second
 |   line of text, or NULL for a single-line box
 | Returns: N/A
 ----------------------*/
void menu_message(const char *title, const char *line1, const char *line2) {
    int l1 = 0, l2 = 0;
    while (line1 && line1[l1]) l1++;
    while (line2 && line2[l2]) l2++;

    int content_w = (l1 > l2 ? l1 : l2);
    int rows      = (l2 > 0) ? 3 : 1;
    int x0, y0, w, h;
    menu_box_fit(title, content_w, rows, &x0, &y0, &w, &h);

    menu_clear();
    menu_frame(x0, y0, w, h, title);
    if (l1) menu_row(x0, w, y0 + 3, 0, 0, line1);
    if (l2) menu_row(x0, w, y0 + 5, 0, 0, line2);
}

/*----------------------
 | MENU_SELECT_HINT_PAD / MENU_SELECT_HINT_KBD
 | Description: The pad and keyboard hint lines at the bottom of the menu_select
 |   box, named once so their width feeds both the sizing math and the draw call --
 |   change the wording and the box width follows automatically instead of drifting
 |   from a hardcoded column count.
 | Author: suinevere
 ----------------------*/
static const char MENU_SELECT_HINT_PAD[] = "A/C=Ok   B=Back";
static const char MENU_SELECT_HINT_KBD[] = "Enter=Ok   Esc=Back";

/*----------------------
 | menu_select
 | Description: Sizes a box (menu_box_fit) to the longest item plus the
 |   reserved digit columns (MENU_DIGIT_COLS, added unconditionally so the box
 |   does not resize when the player switches between the pad and a keyboard
 |   mid-menu), also budgeting the wider of the two MENU_SELECT_HINT_*
 |   variants since the hint line shares the box's width. Rows draw through
 |   menu_row, so they sit centered and the selected one is drawn in reverse
 |   video rather than carrying a '>' in a column every other row wastes.
 |   Height is the visible slice (up to 16 rows) plus the two scroll
 |   markers, a blank row, and the hint -- the markers keep their rows whether
 |   or not they are drawn, so the box does not jump as the list scrolls. Each
 |   loop iteration polls the soft-reset chord, then D-pad/A/C/Start/B on the
 |   gamepad and Up/Down/Enter/Backspace/Esc plus row-selecting digits (mapped
 |   through the visible scroll window via menu_visible_digit, so every entry
 |   of a long list stays reachable while scrolled) on the keyboard, before
 |   redrawing the list and calling menu_sync.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, input.h, saturn_keyboard.h,
 |   soft_reset.h, SRL
 | Globals: g_pad, g_kbd_visible
 | Params: title -- box title; items -- array of item strings; count -- number
 |   of items
 | Returns: the chosen item's 0-based index, or -1 if cancelled
 ----------------------*/
int menu_select(const char *title, const char *const *items, int count) {
    const int VIS = 16;         // max list rows shown at once; longer lists scroll
    MenuBacking backing;        // opaque while the list is up; restored on exit
    int intro = g_menu_intro_fade;  // one-shot: fade this first frame up from black
    g_menu_intro_fade = 0;
    int sel = 0;
    int top = 0;                // index of the first visible row
    int i;

    int item_w = 0;
    for (i = 0; i < count; i++) {
        int len = 0;
        while (items[i][len]) len++;
        if (len > item_w) item_w = len;
    }
    int content_w = item_w + MENU_DIGIT_COLS;

    int hint_w = (int) sizeof(MENU_SELECT_HINT_PAD) - 1;
    int hint_kbd_w = (int) sizeof(MENU_SELECT_HINT_KBD) - 1;
    if (hint_kbd_w > hint_w) hint_w = hint_kbd_w;
    if (hint_w > content_w) content_w = hint_w;

    int rows = (count < VIS ? count : VIS) + 4;

    int x0, y0, w, h;
    menu_box_fit(title, content_w, rows, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // consume any stale button/key edge
    for (;;) {
        check_soft_reset();   // A+B+C+Start -> back to the title screen
        if (g_pad->WasPressed(Button::Up))    sel = (sel - 1 + count) % count;
        if (g_pad->WasPressed(Button::Down))  sel = (sel + 1) % count;
        bool pick = g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START);
        bool cancel = g_pad->WasPressed(Button::B);
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) pick = true;
        else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
        else if (ke.kind == SATURN_KEY_CHAR) {
            int idx = menu_visible_digit(ke.ch, top, VIS, count);
            if (idx >= 0) { sel = idx; pick = true; }
        }
        else if (ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + count) % count;
        else if (ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % count;
        if (cancel) return -1;
        if (pick)   return sel;

        // scroll the window so the selection stays visible
        if (sel < top)             top = sel;
        else if (sel >= top + VIS) top = sel - VIS + 1;
        int last = top + VIS; if (last > count) last = count;

        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand

        menu_clear();
        menu_frame(x0, y0, w, h, title);
        // One pad width for the whole list, so every row shares a left edge and
        // the highlight is a bar of one width; the tenth visible row and beyond
        // spend the digit columns on spaces, since digits only reach nine.
        int pad = item_w + (nums ? MENU_DIGIT_COLS : 0);
        int cy = y0 + 3;
        if (top > 0) menu_row(x0, w, cy, 0, 0, "^ more");
        for (i = top; i < last; i++) {
            int vis = i - top;        // 0-based row within the window
            if (nums && vis < 9)
                menu_rowf(x0, w, cy + 1 + vis, i == sel, pad, "%d) %s", vis + 1, items[i]);
            else if (nums)
                menu_rowf(x0, w, cy + 1 + vis, i == sel, pad, "   %s", items[i]);
            else
                menu_row(x0, w, cy + 1 + vis, i == sel, pad, items[i]);
        }
        if (last < count) menu_row(x0, w, cy + 1 + (last - top), 0, 0, "v more");
        menu_row(x0, w, cy + 3 + (last - top), 0, 0,
            hint(MENU_SELECT_HINT_PAD, MENU_SELECT_HINT_KBD));
        menu_sync();
        if (intro) { menu_fade_in(intro); intro = 0; }
    }
}

/*----------------------
 | menu_confirm
 | Description: Sizes a box titled "CONFIRM" (menu_box_fit) to fit line1/line2
 |   against a 24-column floor -- the widest non-message row is the keyboard
 |   hint "Enter = Yes     Esc = No" (24 columns), the digit row is 15, and the
 |   floor is unconditional (pad wording is shorter, but sizing to it would grow
 |   the box the moment the player switched to a keyboard). Deliberately does
 |   not add MENU_DIGIT_COLS the way menu_select does: there the digits are a
 |   per-row prefix shifting every item's text rightward, so the columns add to
 |   the item width; here they are a standalone row prefixing nothing, and the
 |   24-column floor already covers it. Each loop iteration checks the keyboard
 |   (Enter/Esc/Backspace/Y/N/1/2) and the gamepad (A/C/Start = yes, B = no)
 |   before redrawing and calling menu_sync.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, input.h, saturn_keyboard.h,
 |   SRL
 | Globals: g_pad, g_kbd_visible
 | Params: line1 -- first line of the question; line2 -- second line, or NULL
 | Returns: true if confirmed, false if declined
 ----------------------*/
bool menu_confirm(const char *line1, const char *line2) {
    MenuBacking backing;        // opaque behind the box while the prompt is up
    int l1 = 0, l2 = 0;
    while (line1 && line1[l1]) l1++;
    while (line2 && line2[l2]) l2++;

    int content_w = (l1 > l2 ? l1 : l2);
    if (content_w < 24) content_w = 24;
    int x0, y0, w, h;
    menu_box_fit("CONFIRM", content_w, (l2 > 0 ? 5 : 4), &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // consume the edge that got us here
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) return true;
        if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) return false;
        if (ke.kind == SATURN_KEY_CHAR) {
            if (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1') return true;
            if (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2') return false;
        } else {
            if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START))
                return true;
            if (g_pad->WasPressed(Button::B)) return false;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "CONFIRM");
        int cy = y0 + 3;
        if (l1) menu_row(x0, w, cy, 0, 0, line1);
        if (l2) menu_row(x0, w, cy + 1, 0, 0, line2);
        int hy = cy + (l2 > 0 ? 3 : 2);
        if (!g_kbd_visible) menu_row(x0, w, hy, 0, 0, "1) Yes    2) No");
        menu_row(x0, w, hy + 1, 0, 0,
            hint("A / C = Yes     B = No", "Enter = Yes     Esc = No"));
        menu_sync();
    }
}
