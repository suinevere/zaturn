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
 |   SRL
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "menu.h"
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"

extern "C" {
#include "menu_layout.h"
#include "sound.h"
#include "music.h"
#include "display.h"
}

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
 |   page or a telnet terminal, neither of which has audio to starve.
 ----------------------*/
void menu_sync(void) {
#ifndef NETBIN
    sound_service();
    music_tick();
#endif
    SRL::Core::Synchronize();
}

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
 | menu_intro_level
 | Description: Sets one brightness level for the menu intro fade: colour
 |   offset A (both NBG0 and NBG3 are pointed at it) plus the software-scaled
 |   backdrop colour, so the background picture, the menu text, and the solid
 |   fill all move together. SetColorOffsetA takes a non-const reference, so the
 |   ColorOffset is a named local.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: v -- offset, -255..0
 | Returns: N/A
 ----------------------*/
static void menu_intro_level(int v) {
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetA(off);
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(
        menu_intro_scale(display_bg_rgb(g_display.bg), v)));
}

/*----------------------
 | menu_intro_fade_arm
 | Description: See menu.h. Engages colour offset A on NBG0 and NBG3 and forces
 |   the whole screen black, so the menu that draws next is composed unseen.
 |   Call AFTER display_apply() (which sets the real backdrop colour): this
 |   overrides it back to black until menu_select runs the ramp.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_intro_fade_arm(void) {
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    menu_intro_level(-255);
}

/*----------------------
 | menu_fade_out
 | Description: See menu.h. Engages colour offset A on NBG0 and NBG3, then ramps
 |   the whole screen -- background picture, backdrop colour, and menu text
 |   together -- from normal down to black over `frames` fields, leaving it held
 |   black (channels still engaged) for whatever fades it back in.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: frames -- ramp length in fields
 | Returns: N/A
 ----------------------*/
void menu_fade_out(int frames) {
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    for (int i = 0; i <= frames; i++) {
        menu_intro_level(-(255 * i) / frames);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | menu_fade_in / menu_fade_in_ex
 | Description: See menu.h. Ramps a held-black screen back to normal over
 |   `frames` fields, then releases both offset channels and restores the full
 |   backdrop colour. The caller must have engaged the channels and held black
 |   already (via menu_fade_out, menu_intro_fade_arm, or loading_screen_end,
 |   which deliberately leaves them engaged) and drawn its first frame, or there
 |   is nothing to reveal.
 |
 |   The _ex form takes a per-frame callback handed the same 0..255 the picture is
 |   being lit by, for anything that has to rise in step with it. Deliberately not
 |   a place to dim the wallpaper as well: NBG0 is already on this ramp via colour
 |   offset A, so a step that also drove the in-game channel-B dimmer would darken
 |   it twice over.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
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
        SRL::Core::Synchronize();
    }
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
}

void menu_fade_in(int frames) { menu_fade_in_ex(frames, 0); }

/*----------------------
 | menu_fade_clear
 | Description: See menu.h. Pushes the composed frame, then instantly (no ramp)
 |   reveals a held-black screen: zeroes colour offset A, releases both channels,
 |   and restores the full backdrop colour. Used to cut straight into gameplay
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
    SRL::VDP2::ColorOffset zero((int16_t) 0, (int16_t) 0, (int16_t) 0);
    SRL::VDP2::SetColorOffsetA(zero);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
}

// ---- opaque backing for menus over an image --------------------------------

/*----------------------
 | WIN_W0_* / WIN_NBG0_MENU
 | Description: The VDP2 window-0 WCTL byte that suppresses the image behind a
 |   menu box. NBG3 (text) treats palette entry 0 as transparent, so a menu frame
 |   over an image would show the picture through its interior; a window switches
 |   NBG0 (the image) off inside the menu rectangle so the back-plane colour shows
 |   there while text still draws over it, leaving the picture untouched outside.
 |   SGL exposes no constants, so the encoding was read from the library:
 |   slScrWindowMode(scrn, mode) stores `mode` at 0x060ffd90 + scrn into SGL's
 |   WCTLA..WCTLD shadow (flushed at vblank), so `mode` is the raw per-screen WCTL
 |   byte. ENABLE = bit 1 (window 0 applies here), INSIDE/OUTSIDE = bit 0 (which
 |   side of the rect is the window). WIN_NBG0_MENU is the combined value; if the
 |   image ever hides everywhere except the box, swap INSIDE for OUTSIDE.
 | Author: suinevere
 ----------------------*/
#define WIN_W0_ENABLE  0x02
#define WIN_W0_INSIDE  0x00
#define WIN_W0_OUTSIDE 0x01
#define WIN_NBG0_MENU  (WIN_W0_ENABLE | WIN_W0_INSIDE)

/*----------------------
 | menu_window_rect
 | Description: Points VDP2 window 0 at a character-cell box, converting cell
 |   coordinates to pixels (cells are 8x8, the display is 320x224) and clamping
 |   to the screen. Called on every menu_frame draw rather than once on open,
 |   so a nested page's box takes over the window while it is up and the outer
 |   page's box is restored the moment it redraws.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: x0, y0 -- top-left corner in text cells; w, h -- box width/height in
 |   cells
 | Returns: N/A
 ----------------------*/
static void menu_window_rect(int x0, int y0, int w, int h) {
    int x1 = x0 * 8,             y1 = y0 * 8;
    int x2 = (x0 + w) * 8 - 1,   y2 = (y0 + h) * 8 - 1;
    if (x2 > 319) x2 = 319;
    if (y2 > 223) y2 = 223;
    if (x1 < 0)   x1 = 0;
    if (y1 < 0)   y1 = 0;
    slScrWindow0((uint16_t) x1, (uint16_t) y1, (uint16_t) x2, (uint16_t) y2);
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
 | Dependencies: SRL
 | Globals: g_menu_backing_depth
 | Params: N/A
 | Returns: N/A
 ----------------------*/
MenuBacking::MenuBacking() {
    if (g_menu_backing_depth++ == 0) {
        text_on_flush(nullptr);
        slScrWindowModeNbg0(WIN_NBG0_MENU);
    }
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
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void menu_backing_window_off(void) {
    slScrWindowModeNbg0(0);
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
 |   menu_window_rect, draws the box's +--+ chrome one row at a time, and
 |   centers `title` on the second row.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: x0, y0, w, h -- box geometry in text cells; title -- centered on row
 |   y0 + 1
 | Returns: N/A
 ----------------------*/
void menu_frame(int x0, int y0, int w, int h, const char *title) {
    menu_window_rect(x0, y0, w, h);
    for (int r = 0; r < h; r++) {
        char line[42]; int p = 0;
        for (int c = 0; c < w && p < (int) sizeof(line) - 1; c++)
            line[p++] = (r == 0 || r == h - 1) ? ((c == 0 || c == w - 1) ? '+' : '-')
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
 | menu_wait
 | Description: Drops the current frame's edge with one Synchronize, then
 |   polls both the gamepad face/start buttons and the keyboard every frame
 |   until one of them fires.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_wait(void) {
    SRL::Core::Synchronize();
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        SRL::Core::Synchronize();
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
    if (l1) text_print(x0 + 2, y0 + 3, "%s", line1);
    if (l2) text_print(x0 + 2, y0 + 5, "%s", line2);
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
 | Description: Sizes a box (menu_box_fit) to the longest item plus the "> "
 |   cursor and the reserved digit columns (MENU_DIGIT_COLS, added
 |   unconditionally so the box does not resize when the player switches
 |   between the pad and a keyboard mid-menu), also budgeting the wider of the
 |   two MENU_SELECT_HINT_* variants since the hint line shares the box's
 |   width. Height is the visible slice (up to 16 rows) plus the two scroll
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

    int content_w = 0;
    for (i = 0; i < count; i++) {
        int len = 0;
        while (items[i][len]) len++;
        if (len > content_w) content_w = len;
    }
    content_w += 2 + MENU_DIGIT_COLS;

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
        int cx = x0 + 2, cy = y0 + 3;
        text_print(cx, cy, "%s", top > 0 ? "^ more" : "      ");
        for (i = top; i < last; i++) {
            char mark = (i == sel) ? '>' : ' ';
            int  vis  = i - top;      // 0-based row within the window
            if (nums && vis < 9)
                text_print(cx, cy + 1 + vis, "%c %d) %s", mark, vis + 1, items[i]);
            else
                text_print(cx, cy + 1 + vis, "%c    %s", mark, items[i]);
        }
        text_print(cx, cy + 1 + (last - top), "%s", last < count ? "v more" : "      ");
        text_print(cx, cy + 3 + (last - top), "%s",
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
        int cx = x0 + 2, cy = y0 + 3;
        if (l1) text_print(cx, cy, "%s", line1);
        if (l2) text_print(cx, cy + 1, "%s", line2);
        int hy = cy + (l2 > 0 ? 3 : 2);
        if (!g_kbd_visible) text_print(cx, hy, "1) Yes    2) No");
        text_print(cx, hy + 1, "%s",
            hint("A / C = Yes     B = No", "Enter = Yes     Esc = No"));
        menu_sync();
    }
}
