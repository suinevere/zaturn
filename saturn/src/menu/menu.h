/*----------------------
 | menu.h
 | Description: The menu-drawing framework -- box chrome, the image-suppressing
 |   VDP2 window that keeps a background picture from showing through a menu's
 |   interior, the opaque-backing RAII guard, and the modal wait/message/list/
 |   confirm primitives every menu page (Options, Controls, Sound, Display,
 |   Network, save/load, online) is built from. Pages own their own content and
 |   input handling; this module owns only the chrome and the three interaction
 |   primitives.
 | Author: suinevere
 | Dependencies: menu_layout.c (box-fit/digit-mapping geometry), console_view.cxx
 |   (hint/note_input_device/render_console/g_kbd_visible), input.h (g_pad,
 |   Button), saturn_keyboard.h (SaturnKeyEvent/SATURN_KEY_*), soft_reset.h
 |   (check_soft_reset), sound.c (sound_service), music.c (music_tick), SRL
 ----------------------*/

#ifndef MENU_H
#define MENU_H

#include "text_map.h"   // menu_rowf formats through the same path text_print does

// Refcounted RAII guard that switches the image-suppressing VDP2 window on
// while at least one menu page is open, and off again once the last one
// closes. Refcounted rather than paired on/off calls because pages nest
// (Options opens Display, and the inner page closing must not clear the
// windowing while the outer one is still up); scoped rather than paired calls
// because every page has several exit paths, and "remember to undo this on
// all of them" is the exact shape of bug that has already cost this project a
// release -- a destructor cannot forget. Construct one at the top of any menu
// page that draws over a possible image background; the box owns its area for
// as long as the guard is alive. The window goes off one step later than the
// guard dies -- at the next frame that changes the text -- because the box is
// still drawn at the `return` that destroys it.
/*----------------------
 | MenuBacking
 | Description: Scope guard that owns a menu page's opaque backing box, so the
 |   box cannot be left behind by an early return.
 | Author: suinevere
 ----------------------*/
struct MenuBacking {
    MenuBacking();
    ~MenuBacking();
};

/*----------------------
 | g_menu_backing_depth
 | Description: MenuBacking's refcount, force-reset to 0 by main()'s soft-reset
 |   path because the longjmp skips the destructors that would decrement it.
 | Author: suinevere
 ----------------------*/
extern int g_menu_backing_depth;

/*----------------------
 | menu_sync
 | Description: Advances one frame for a modal menu loop -- services looping
 |   PCM sound and the debounced Dynamic-mix music switch before synchronizing
 |   to vblank, so a menu wait does not let looping sound starve into silence
 |   or miss a pending music transition. Menu loops call this in place of a
 |   bare SRL::Core::Synchronize().
 | Author: suinevere
 | Dependencies: sound.c, music.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_sync(void);

/*----------------------
 | menu_pointer_act / menu_pointer_back / menu_pointer_row
 | Description: A pointing device in menu terms: act is a left or middle click,
 |   back a right one, and menu_pointer_row says which of `n` rows laid out one per
 |   line from `y0` the cursor is over (-1 for none). Pages OR the first two into
 |   their own act/back so a mouse reaches the same code the pad does. See the
 |   implementation for why act takes two buttons and back exactly one.
 | Author: suinevere
 | Dependencies: controller.h
 | Globals: N/A
 | Params: y0 -- the first row's cell row; n -- how many rows follow it
 | Returns: act/back are true on the click edge; row is 0..n-1 or -1
 ----------------------*/
bool menu_pointer_act(void);
bool menu_pointer_back(void);
int  menu_pointer_row(int y0, int n);

/*----------------------
 | menu_pointer_at / menu_row_x / menu_pointer_step
 | Description: Hit tests for the parts of a row. menu_pointer_at asks whether the
 |   cursor is inside `n` columns from (x, y); menu_row_x says where a row drawn
 |   through menu_row/menu_rowf with that pad actually starts, which is what those
 |   columns are measured from; menu_pointer_step folds the two into the question a
 |   slider row asks -- did a click land on my `<` or my `>` -- since a pointing
 |   device has no Left and Right of its own and would otherwise be unable to move
 |   any row a pad steps.
 | Author: suinevere
 | Dependencies: controller.h
 | Globals: N/A
 | Params: x/y/n -- a span of cells; x0/w -- the box; pad -- the row's padded
 |   width; loff/roff -- columns from the row's left edge to its two arrows
 | Returns: at is nonzero inside the span; row_x is a column; step is -1, +1 or 0
 ----------------------*/
int menu_pointer_at(int x, int y, int n);
int menu_row_x(int x0, int w, int pad);
int menu_pointer_step(int x0, int w, int pad, int y, int loff, int roff);

/*----------------------
 | MenuYesNo
 | Description: The state of one Yes/No box: which cell is lit, and which the
 |   cursor was over last frame. The second is what stops a resting cursor pinning
 |   the highlight so the pad cannot move it, and it belongs to the box rather than
 |   to the module because two boxes can be open one inside the other.
 | Author: suinevere
 ----------------------*/
typedef struct { int yes; int hov; } MenuYesNo;

/*----------------------
 | menu_yesno_init / menu_yesno_draw / menu_yesno_input / menu_yesno_hit
 | Description: A two-cell Yes/No answer, drawn as the words themselves rather than
 |   as a legend of which button does what: Left/Right move the lit cell, A/C/Start
 |   and Enter take it, B/Backspace/Esc/a right click/a gun shot off the screen are
 |   No, and a click takes the cell it lands on. Any page asking a yes/no question
 |   uses these rather than printing its own prompt, so every one of them answers a
 |   mouse and a gun the same way.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h, controller.h
 | Globals: g_pad
 | Params: s -- the widget; yes_default -- which cell starts lit; x0/w -- the box;
 |   y -- the row the cells are drawn on
 | Returns: input gives 1 yes, 0 no, -1 undecided; hit gives 1, 0 or -1
 ----------------------*/
void menu_yesno_init(MenuYesNo *s, int yes_default);
void menu_yesno_draw(const MenuYesNo *s, int x0, int w, int y);
int  menu_yesno_input(MenuYesNo *s, int x0, int w, int y);
int  menu_yesno_hit(int x0, int w, int y);

#ifdef NETBIN
/*----------------------
 | MenuServiceFn
 | Description: A per-frame callback menu_sync runs for the netbin, in the slot
 |   where the CD build services sound and music.
 | Author: suinevere
 | Params: ctx -- whatever menu_set_service was handed
 | Returns: N/A
 ----------------------*/
typedef void (*MenuServiceFn)(void *ctx);

/*----------------------
 | menu_set_service
 | Description: Registers the callback menu_sync runs each frame, or clears it
 |   with a null fn. The netbin's modal pages hold the screen while a telnet
 |   session is live, and transport_uart.c reads the 16550's own FIFO with no
 |   software ring behind it -- a page that stops pumping drops bytes after a
 |   dozen or so. online_mode registers its RX pump around any modal it opens.
 |   Only one is ever installed; there is no stack, so a caller that registers
 |   must clear before returning, and main()'s soft-reset landing clears it too,
 |   since a longjmp out of a page leaves the pointer aimed at a dead frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: fn -- the callback, or NULL to clear; ctx -- passed back to fn
 | Returns: N/A
 ----------------------*/
void menu_set_service(MenuServiceFn fn, void *ctx);
#endif

/*----------------------
 | menu_clear
 | Description: Blanks every console text row, so a menu page can redraw its
 |   box chrome cleanly this frame.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_clear(void);

/*----------------------
 | menu_frame
 | Description: Draws a w x h box of +--+ chrome at (x0, y0) with `title`
 |   centered on its second row, and aims the image-suppressing VDP2 window at
 |   the same rectangle. Every menu page uses this so the chrome and title
 |   placement stay identical; pages differ only in the box they ask for. The
 |   caller owns the interior: content starts at (x0 + 2, y0 + 3) by convention
 |   (row y0 + 2 stays blank under the title) and must stay inside x0 + w - 2
 |   so it never overwrites the right border.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: x0, y0 -- top-left corner of the box, in text cells; w, h -- box
 |   width/height in cells; title -- text centered on the box's second row
 | Returns: N/A
 ----------------------*/
void menu_frame(int x0, int y0, int w, int h, const char *title);

/*----------------------
 | menu_row
 | Description: Draws one selectable list row, centered inside the box's
 |   interior (the w - 4 columns between the borders and their pad). Brightness
 |   is the selection marker: the selected row is drawn in the text colour the
 |   player chose, every other row in a dimmed mix of that colour and the
 |   background (text_print_dim), so the cursor is the one line at full
 |   strength rather than a block of reverse video, and pages spend no column
 |   on a '>' that only one row ever used. `pad` right-pads the text to a common
 |   width before centering, which is how a list keeps one left edge: without it
 |   every row would center on its own length and the digit column would zigzag,
 |   the exact regression that had per-row centering reverted the last time it
 |   was tried. Use menu_text, not this, for a line that is not a row the cursor
 |   can land on -- a description, a hint, a message -- so it is not dimmed for
 |   being unselected when it was never selectable.
 | Author: suinevere
 | Dependencies: text_map.h (text_print_str/text_print_dim)
 | Globals: N/A
 | Params: x0 -- box left column; w -- box width; y -- row; sel -- nonzero for
 |   the selected row; pad -- width to pad to, or 0 for the text's own; text --
 |   the composed row
 | Returns: N/A
 ----------------------*/
void menu_row(int x0, int w, int y, int sel, int pad, const char *text);

/*----------------------
 | menu_text / menu_textf
 | Description: menu_row's placement without its selection colouring: one
 |   centered line at the full text colour. For everything in a box that is not
 |   a row the cursor can reach -- titles under the frame, description lines,
 |   the controls hint, a menu_message's body -- which must not read as "an
 |   unselected row" just because nothing selected it.
 | Author: suinevere
 | Dependencies: menu_row, srl.hpp
 | Globals: N/A
 | Params: x0, w, y, pad -- as menu_row; text/fmt/args -- the line
 | Returns: N/A
 ----------------------*/
void menu_text(int x0, int w, int y, int pad, const char *text);

template <typename ...Args>
inline void menu_textf(int x0, int w, int y, int pad, const char *fmt, Args...args)
{
    char buffer[TEXT_FORMAT_MAX];
    SRL::string stringObj;

    if (stringObj.snprintfEx(buffer, TEXT_FORMAT_MAX, fmt, args ...) > 0)
        menu_text(x0, w, y, pad, buffer);
}

/*----------------------
 | menu_pad
 | Description: `s` right-padded with spaces to `w` columns, in one of a small
 |   ring of scratch buffers so several padded fields can be composed in a
 |   single menu_rowf call. Exists because a value column inside a centered row
 |   only stays a column if every row's value occupies the same width -- with
 |   the rows centered rather than left-anchored, a short value would otherwise
 |   drag the closing arrow left. The result is valid until the ring wraps, so
 |   use it as an argument and never store it.
 | Author: suinevere
 | Dependencies: menu_layout.h (MENU_SCREEN_COLS)
 | Globals: N/A
 | Params: s -- the text; w -- column width to pad to
 | Returns: the padded copy
 ----------------------*/
const char *menu_pad(const char *s, int w);

/*----------------------
 | menu_num
 | Description: A row's three-column number prefix -- "3) " when digit
 |   shortcuts are live, three spaces when a keyboard has hidden them.
 |   Reserved either way, the same reason MENU_DIGIT_COLS is unconditional: a
 |   row that changed width when the player picked up a keyboard would move
 |   the centered block under the cursor. Rows past the tenth get spaces,
 |   since the digits only reach that far.
 | Author: suinevere
 | Dependencies: menu_layout.h (menu_row_digit_char)
 | Globals: N/A
 | Params: nums -- nonzero when digit shortcuts are shown; row -- 0-based row
 | Returns: the three-column prefix
 ----------------------*/
const char *menu_num(int nums, int row);

/*----------------------
 | menu_rowf
 | Description: menu_row with printf formatting, through the same SRL::string
 |   path text_print uses. Split from the plain overload for the same reason
 |   text_print is: a bare string carrying a '%' must print literally.
 | Author: suinevere
 | Dependencies: menu_row, srl.hpp
 | Globals: N/A
 | Params: x0, w, y, sel, pad -- as menu_row; fmt/args -- format and arguments
 | Returns: N/A
 ----------------------*/
template <typename ...Args>
inline void menu_rowf(int x0, int w, int y, int sel, int pad, const char *fmt, Args...args)
{
    char buffer[TEXT_FORMAT_MAX];
    SRL::string stringObj;

    if (stringObj.snprintfEx(buffer, TEXT_FORMAT_MAX, fmt, args ...) > 0)
        menu_row(x0, w, y, sel, pad, buffer);
}

/*----------------------
 | menu_wait
 | Description: Blocks until any button or key is pressed. Used for "press any
 |   key" prompts.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_wait(void);

/*----------------------
 | menu_message
 | Description: Draws a centered box titled `title` with one or two lines of
 |   text, sized to fit both lines (including any hint text passed as line2)
 |   without resizing when the player switches input device. Returns at once
 |   without waiting or synchronizing: callers that want a blocking prompt
 |   follow it with menu_wait(); callers that redraw every frame (the dialing
 |   screens) do not.
 | Author: suinevere
 | Dependencies: menu_layout.c, SRL
 | Globals: N/A
 | Params: title -- box title; line1 -- first line of text; line2 -- second
 |   line of text, or NULL for a single-line box
 | Returns: N/A
 ----------------------*/
void menu_message(const char *title, const char *line1, const char *line2);

/*----------------------
 | menu_select
 | Description: Modal, scrollable list menu titled `title` over `count` items
 |   in `items`. Navigable by gamepad (D-pad to move, A/C/Start to pick, B to
 |   cancel) or keyboard (number keys pick a visible row directly, Enter picks
 |   the highlighted item, Backspace/Esc cancels), by mouse (hover to highlight,
 |   click to pick, right click to cancel, click a scroll marker to page) and by
 |   gun. Draws no controls hint: no page does now, since the one that used to --
 |   menu_confirm -- draws the answer itself instead. Polls the soft-reset chord
 |   every loop.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, input.h, saturn_keyboard.h,
 |   soft_reset.h, SRL
 | Globals: g_pad, g_kbd_visible
 | Params: title -- box title; items -- array of item strings; count -- number
 |   of items
 | Returns: the chosen item's 0-based index, or -1 if cancelled
 ----------------------*/
int menu_select(const char *title, const char *const *items, int count);

/*----------------------
 | menu_select_at
 | Description: menu_select that opens on a remembered row instead of the top,
 |   and writes the row back on both exits. Callers keep the variable across
 |   calls (a function static), so backing out of a list and re-opening it
 |   resumes where the cursor was rather than resetting -- the thing a list
 |   nested under another list otherwise loses every time the player steps
 |   back up. Out-of-range values (a list that shrank since the last visit)
 |   fall back to the top rather than being clamped to the end, since the
 |   remembered row no longer means anything once the list has changed shape.
 |   menu_select is this with a throwaway zero.
 | Author: suinevere
 | Dependencies: as menu_select
 | Globals: g_pad, g_kbd_visible
 | Params: title, items, count -- as menu_select; sel -- in/out remembered row
 | Returns: the chosen item's 0-based index, or -1 if cancelled
 ----------------------*/
int menu_select_at(const char *title, const char *const *items, int count, int *sel);

/*----------------------
 | menu_select_final
 | Description: menu_select_at with no way out: B, Esc and Backspace do nothing
 |   and the call returns only on a pick. For a list that has nowhere to go back
 |   to -- the mode menu at the root of the boot, whose caller answered a cancel
 |   by re-entering the same list, which redrew the box and read on screen as a
 |   flicker rather than as input being ignored.
 | Author: suinevere
 | Dependencies: as menu_select_at
 | Globals: as menu_select_at
 | Params: as menu_select_at
 | Returns: the chosen item's 0-based index; never -1
 ----------------------*/
int menu_select_final(const char *title, const char *const *items, int count, int *sel);

/*----------------------
 | menu_confirm
 | Description: Modal Yes/No confirmation box showing `line1` and an optional
 |   `line2`, answered on a menu_yesno widget: the words Yes and No, moved between
 |   with Left/Right, taken with C/A/Start/Enter/Y, declined with B/N/Esc/Backspace,
 |   and either one pickable by clicking or shooting it. Unlike
 |   confirm_return_to_title, only reports the answer -- it does not act on it.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, controller.h, menu_yesno, SRL
 | Globals: g_menu_intro_fade
 | Params: line1 -- first line of the question; line2 -- second line, or NULL
 | Returns: true if confirmed, false if declined
 ----------------------*/
bool menu_confirm(const char *line1, const char *line2);

/*----------------------
 | g_menu_intro_fade / menu_intro_fade_arm
 | Description: One-shot hook that makes the NEXT menu_select() call fade its
 |   first frame up from black instead of appearing instantly -- used once, for
 |   the title-screen -> mode-select hand-off, so the menu box and its
 |   background rise together after the title has faded out. Every other
 |   menu_select call leaves g_menu_intro_fade at 0 and is unaffected. Sequence
 |   at the call site: after the title fade-out and display_apply(), call
 |   menu_intro_fade_arm() (holds the screen black through the shared screen-wide
 |   fade, backdrop forced dark) and set g_menu_intro_fade to the ramp
 |   length; the first menu_select draws its box unseen, then ramps everything
 |   up and clears the hold. The fade covers the background picture, the solid
 |   backdrop colour, and the menu text/palette in one motion.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_menu_intro_fade, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern int g_menu_intro_fade;
void menu_intro_fade_arm(void);

/*----------------------
 | menu_fade_out / menu_fade_in
 | Description: Quick whole-screen fades for Options-menu page transitions.
 |   menu_fade_out engages the shared screen-wide fade (title.h: text on colour
 |   offset A, background picture on B, where the player's held wallpaper dim is
 |   composed in) and ramps the screen -- background picture, backdrop colour,
 |   and menu text together -- from normal down to black over `frames` fields,
 |   leaving it held black. menu_fade_in ramps a held-black screen back to normal
 |   -- meaning back to that held dim, not to an unmodified picture -- and
 |   releases the channels. Every menu_fade_out must reach a menu_fade_in (or the
 |   g_menu_intro_fade one-shot) downstream, or the screen stays black -- the
 |   offset has no automatic decay. A page fades itself in after drawing its
 |   first (black) frame and fades out before returning; a parent fades out
 |   before dispatching to a sub-page and back in when it redraws, giving one
 |   continuous dark hold across the boundary.
 |     g_menu_page_fade is the ramp length the Options pages read for their own
 |   entry/exit fades: main() sets it while it drives the title -> Options
 |   transition and leaves it 0 elsewhere, so the same pages opened in-game
 |   (F10/F11/F12 over live gameplay, from saturn_glue.cxx) stay instant and are
 |   never left stuck black by an exit fade.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: frames -- ramp length in fields
 | Returns: N/A
 ----------------------*/
extern int g_menu_page_fade;
void menu_fade_out(int frames);
void menu_fade_in(int frames);

/*----------------------
 | MenuFadeStep / menu_fade_in_ex
 | Description: menu_fade_in with a per-frame callback, handed the same 0..255 the
 |   screen is being lit by, called before that frame's Synchronize so what it sets
 |   applies to the brightness it belongs to. For raising something that must
 |   arrive with the picture rather than after it -- the game hand-off uses it to
 |   bring the CD-DA up on the fade. Do NOT drive the wallpaper's own channel-B
 |   dimmer from here: NBG0 is already riding this ramp on channel A.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: frames -- ramp length in fields; step -- per-frame callback, or NULL
 | Returns: N/A
 ----------------------*/
typedef void (*MenuFadeStep)(int level);
void menu_fade_in_ex(int frames, MenuFadeStep step);

/*----------------------
 | menu_fade_clear
 | Description: Instant (no ramp) reveal of a held-black screen: pushes the
 |   composed text frame (the callers clear and reveal without a Synchronize in
 |   between, so the tilemap would otherwise still hold the outgoing screen),
 |   zeroes colour offset A, releases both channels, restores the backdrop.
 |   The game hand-off no longer uses it -- the first prompt ramps the opening
 |   frame up instead -- so what is left is the paths that have to un-black a
 |   screen nobody is going to ramp: a story that ended before it ever reached a
 |   prompt, and saturn_glue's menu_ramp_cut.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_fade_clear(void);

/*----------------------
 | menu_back_override
 | Description: Makes every fade drive `packed` as the backdrop instead of the
 |   player's chosen background colour, for a page that paints its own ground.
 |   0 clears it, and is not a colour any caller can mean: a backdrop carries the
 |   opaque bit.
 |
 |   Set it before the page's fade-in and clear it after its fade-out, so both
 |   ramps run on the colour the page is actually showing. Without it a page that
 |   sets the back colour itself keeps that colour only until the first frame of
 |   its own fade, which recomputes it from g_display.bg -- the map's tan ground
 |   lasted exactly that long. Cleared on the way to the title as well, since a
 |   soft reset out of such a page skips the far end.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: packed -- a DISP_RGB555 colour with its opaque bit, or 0 to clear
 | Returns: N/A
 ----------------------*/
void menu_back_override(unsigned short packed);

#endif /* MENU_H */
