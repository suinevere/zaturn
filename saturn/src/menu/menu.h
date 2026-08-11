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
 |   the highlighted item, Backspace/Esc cancels). Polls the soft-reset chord
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
 | menu_confirm
 | Description: Modal Yes/No confirmation box showing `line1` and an optional
 |   `line2`. Accepts C/A/Start/Enter/Y as yes and B/N/Esc/Backspace as no.
 |   Unlike confirm_return_to_title, only reports the answer -- it does not act
 |   on it.
 | Author: suinevere
 | Dependencies: menu_layout.c, console_view.cxx, input.h, saturn_keyboard.h,
 |   SRL
 | Globals: g_pad, g_kbd_visible
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
 |   main() uses it to
 |   cut into gameplay after the menu faded to black and the story loaded behind
 |   the black (the game screen itself is not faded in -- it appears at full
 |   brightness once the interpreter renders its first frame), and to un-black
 |   an error screen if the load fails.
 | Author: suinevere
 | Dependencies: SRL, display.c (display_bg_rgb)
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_fade_clear(void);

#endif /* MENU_H */
