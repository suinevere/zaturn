/*----------------------
 | loading_screen.cxx
 | Description: See loading_screen.h. Split across begin/tick/end because the
 |   screen has to stay up for the whole of main()'s load, whose duration this
 |   module cannot know.
 | Author: suinevere
 | Dependencies: loading_screen.h, loading_text.h, loading_music.h, menu.h,
 |   options.h, app_state.h, input.h, saturn_keyboard.h, title.h, SRL
 ----------------------*/
#include "loading_screen.h"
#include "loading_text.h"
#include "loading_music.h"
#include "title.h"
#include "menu.h"
#include "options.h"
#include "app_state.h"
#include "input.h"
#include "saturn_keyboard.h"
#include <srl.hpp>
#include "text_map.h"

extern "C" {
#include "display.h"
#include "music.h"
}

using namespace SRL::Types;

/*----------------------
 | LOADING_FADE_FRAMES / LOADING_TEXT_TOP_ROW / TYPE_* constants
 | Description: LOADING_FADE_FRAMES (45 = 0.75s at 60fps) sits between
 |   QUICK_FADE_FRAMES (15, snappy menu transitions) and SPLASH_FADE_FRAMES
 |   (90, the prominent boot-splash logo fade) -- noticeable but not a long
 |   wait, given the typing itself already takes several seconds.
 |   LOADING_TEXT_TOP_ROW is where the block's first row lands, and 1 puts the
 |   "**** SEGA SATURN 32-BIT OS V1.00 ****" banner on the second line from the
 |   top -- one blank row above it and nothing else, the way a machine that has
 |   just been switched on prints its banner. Not row 0: a boot screen that
 |   starts hard against the top edge reads as clipped rather than as the top of
 |   a listing. The block runs eleven rows from here, so rows 1..11, which keeps
 |   it clear of the DEBUG cue readout on row 22 (see loading_screen_tick).
 |   The TYPE_* constants time the three phases; see the cadence box below.
 | Author: suinevere
 ----------------------*/
#define LOADING_FADE_FRAMES   45
#define LOADING_TEXT_TOP_ROW  3

/*----------------------
 | LOADING_ROW_LOAD / the three-phase cadence
 | Description: The block is not one uniform crawl, because the machine it is
 |   imitating did not produce it uniformly. Three phases, split at the one row a
 |   person actually typed:
 |
 |     rows 0..LOADING_ROW_LOAD-1  the power-on banner, RAM count and the first
 |       READY! -- printed whole, in a single frame, before anything waits. A
 |       real machine has all of this on screen the instant it is switched on;
 |       crawling it out a character at a time was the one part of the effect
 |       that read as an animation rather than as a computer.
 |     row LOADING_ROW_LOAD        the LOAD "<name>",8,1 line -- one key at a
 |       time, at irregular intervals (see type_hold).
 |     rows LOADING_ROW_LOAD+1..   the drive's replies -- each printed whole,
 |       because the machine emitted them whole, but spaced by an irregular
 |       TYPE_REPLY_MIN..MIN+SPAN-1 gap, which is the drive taking as long as it
 |       takes rather than a fixed beat.
 |
 |   LOADING_ROW_LOAD is 5 to match loading_text.c's put_line order, the same way
 |   the old PAUSE_AFTER_READY table indexed rows 4 and 9 -- one row number
 |   against that file rather than a string compare on the drawn text. That table
 |   is gone: its row 4 is now inside the instant block and its row 9 gets the
 |   same irregular reply gap as every other reply, so a one-entry lookup table
 |   and its rationale earned less than it cost.
 | Author: suinevere
 ----------------------*/
#define LOADING_ROW_LOAD 5

/*----------------------
 | TYPE_KEY_* / TYPE_HESITATE_* / TYPE_REPLY_*
 | Description: Per-keystroke and per-reply hold ranges, in video fields.
 |
 |   The keystroke timing is deliberately not one uniform random range. Uniform
 |   jitter across a wide range does not read as a person typing, it reads as a
 |   machine with a broken clock -- the average is what you hear, and it is
 |   mushy. People type in fast runs broken by occasional hesitations, so that is
 |   what this is: every key holds TYPE_KEY_MIN..MIN+SPAN-1 fields (8..19), and
 |   one key in TYPE_HESITATE_ODDS also takes an extra
 |   TYPE_HESITATE_MIN..MIN+SPAN-1 (24..63, a beat of looking for the next
 |   character). That averages ~17 fields per key, so a 15-character LOAD line
 |   runs ~260 fields with a couple of hitches in it and the 40-column worst case
 |   ~690.
 |
 |   Four times the original rate, which was 2..4 fields per key -- a brisk
 |   15-30 characters a second. That was a good imitation of somebody who can
 |   type and a bad one of the thing this screen is selling, which is a wait. At
 |   ~3.5 characters a second it reads as somebody picking out a command on a
 |   machine they are not in a hurry with, and the block lasts long enough to be
 |   read rather than merely seen.
 |
 |   The reply gaps below are untouched, so the drive still answers at the pace it
 |   always did. They are the machine's timing, not a person's, and slowing them
 |   to match would have read as the drive getting slower rather than the typist.
 |
 |   The cost is real but bounded: loading_screen_begin() blocks until the block
 |   has typed itself out, so this adds around three seconds to the wait before
 |   the story file is even opened. It gates nothing else -- the screen stays lit
 |   for the entire load either way.
 | Author: suinevere
 ----------------------*/
#define TYPE_KEY_MIN        8
#define TYPE_KEY_SPAN       12
#define TYPE_HESITATE_ODDS  8
#define TYPE_HESITATE_MIN   24
#define TYPE_HESITATE_SPAN  40
#define TYPE_REPLY_MIN      20
#define TYPE_REPLY_SPAN     50

/*----------------------
 | g_type_rng / type_rng / type_hold
 | Description: The cadence's randomness. Same LCG and same 15-bit extraction as
 |   music.c's rng_next, which is the house idiom for this; nothing shared is
 |   exposed to reuse, so it is restated here rather than made into a dependency
 |   for two call sites.
 |
 |   Deliberately never re-seeded to a constant. loading_screen_type stirs the
 |   LOAD line's own bytes in and otherwise lets the state run on, so different
 |   games type differently and a second load in one session differs from the
 |   first, while a cold boot is still reproducible for anyone bisecting this.
 |   Run-to-run variation is not the point of the effect -- irregularity within a
 |   single line is, and a fixed seed already gives that.
 | Author: suinevere
 ----------------------*/
static unsigned int g_type_rng = 0x5a17u;

static unsigned int type_rng(void) {
    g_type_rng = g_type_rng * 1103515245u + 12345u;
    return (g_type_rng >> 16) & 0x7fffu;
}

static int type_hold(void) {
    int hold = TYPE_KEY_MIN + (int) (type_rng() % TYPE_KEY_SPAN);
    if (type_rng() % TYPE_HESITATE_ODDS == 0)
        hold += TYPE_HESITATE_MIN + (int) (type_rng() % TYPE_HESITATE_SPAN);
    return hold;
}

/*----------------------
 | loading_screen_skip_pressed
 | Description: Same check menu_wait() uses for "press any key/button" --
 |   A/B/C/START plus any keyboard key.
 | Author: suinevere
 | Globals: g_pad
 ----------------------*/
static bool loading_screen_skip_pressed(void) {
    if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
        g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return true;
    return saturn_keyboard_poll().kind != SATURN_KEY_NONE;
}

/*----------------------
 | loading_screen_set_offset
 | Description: Writes one brightness offset to colour offset channel A.
 | Author: suinevere
 | Dependencies: SRL (VDP2)
 | Globals: N/A
 | Params: v -- -255 (black) to 0 (normal)
 | Returns: N/A
 ----------------------*/
static void loading_screen_set_offset(int v) {
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetA(off);
}

/*----------------------
 | g_fade_in_left
 | Description: How many frames of the screen's fade-in ramp are still owed,
 |   armed by loading_screen_fade_in and spent one per frame by
 |   loading_screen_wait. The ramp is not run as a block of its own because at
 |   fade-in time there is nothing on screen to reveal -- the console was just
 |   cleared and the backdrop forced black, so a ramp there brightens black
 |   into black and reads as three quarters of a second of nothing. Owing the
 |   frames instead means the ramp is spent by the first characters being
 |   typed, and what the player sees is text materialising as the screen comes
 |   up, which is the effect that was wanted. Counts down to 0 exactly once
 |   per loading_screen_begin() and stays there, so later lines and the fade-out are
 |   untouched.
 | Author: suinevere
 ----------------------*/
static int g_fade_in_left = 0;

/*----------------------
 | loading_screen_wait
 | Description: Synchronizes for `frames` fields, polling the skip check
 |   each frame. While the fade-in ramp is still owed (see g_fade_in_left) it
 |   also advances the colour offset one step toward 0 per frame, so typing
 |   and brightening happen together.
 | Author: suinevere
 | Globals: g_fade_in_left
 | Returns: true if the player skipped during the wait
 ----------------------*/
static bool loading_screen_wait(int frames) {
    for (int f = 0; f < frames; f++) {
        if (g_fade_in_left > 0) {
            g_fade_in_left--;
            loading_screen_set_offset(-255 + (255 * (LOADING_FADE_FRAMES - g_fade_in_left)) / LOADING_FADE_FRAMES);
        }
        SRL::Core::Synchronize();
        if (loading_screen_skip_pressed()) return true;
    }
    return false;
}

/*----------------------
 | loading_screen_fade_in
 | Description: Holds the screen black, engages the colour offset channels,
 |   and bakes and starts LOADCD.PCM's fade-in. The matching visual ramp is
 |   not run here -- it is armed on g_fade_in_left and spent by the first
 |   LOADING_FADE_FRAMES frames loading_screen_type waits, so the two still rise
 |   over the same span (mirroring splash.cxx's logo/jingle pairing) but the
 |   picture has real content to rise over.
 |
 |   Those frames now belong to the LOAD line being typed, since the phase before
 |   it waits for nothing (see the cadence box). So what comes up out of black is
 |   the whole power-on block at once with the LOAD line appearing across it,
 |   which is the same effect aimed at before and a closer match to a machine
 |   warming up. The shortest possible block still waits ~120 fields against a
 |   45-field ramp, so it cannot end part-lit; loading_screen_begin's backstop
 |   covers that anyway.
 | Author: suinevere
 | Globals: g_fade_in_left
 ----------------------*/
static void loading_screen_fade_in(void) {
    loading_screen_set_offset(-255);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);

    g_fade_in_left = LOADING_FADE_FRAMES;

    loading_music_fade_in(LOADING_FADE_FRAMES);
    loading_music_play();
}

/*----------------------
 | loading_screen_fade_out
 | Description: Ramps the screen and LOADCD.PCM's master-volume level back
 |   down together over LOADING_FADE_FRAMES, and leaves them there. The colour
 |   offset channels are deliberately left engaged at -255: main() expects the
 |   screen to be held black when loading_screen_end returns and calls
 |   menu_fade_clear when the game is ready to cut in -- which is what releases
 |   the channels. Releasing them here would leave menu_fade_clear with nothing
 |   to reveal and open a window for a stray bright frame in between.
 | Author: suinevere
 ----------------------*/
static void loading_screen_fade_out(void) {
    for (int i = 0; i <= LOADING_FADE_FRAMES; i++) {
        loading_screen_set_offset(-(255 * i) / LOADING_FADE_FRAMES);
        loading_music_set_level(LOADING_MUSIC_LEVEL_MAX - (LOADING_MUSIC_LEVEL_MAX * i) / LOADING_FADE_FRAMES);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | loading_screen_type
 | Description: Draws the block in the three phases described in the cadence box:
 |   the power-on rows at once, the LOAD line a key at a time at irregular
 |   intervals, then the drive's replies whole but irregularly spaced. On skip,
 |   immediately draws every remaining line in full and stops.
 |
 |   Empty rows are not printed at all, only waited through -- printing "" would
 |   draw nothing over a console menu_clear already blanked, and the row exists to
 |   space the block, not to carry text.
 | Author: suinevere
 | Globals: g_type_rng (stirred from the LOAD line, then advanced)
 ----------------------*/
static void loading_screen_type(char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]) {
    bool skipped = false;
    char buf[LOADING_TEXT_COLS + 1];

    /* Phase 1: switched on. No waits at all, so this costs a single frame and the
       fade-in ramp below has the whole block already on screen to rise over. */
    for (int row = 0; row < LOADING_ROW_LOAD; row++)
        if (lines[row][0]) text_print(0, LOADING_TEXT_TOP_ROW + row, "%s", lines[row]);

    /* Phase 2: somebody types. */
    const char *load = lines[LOADING_ROW_LOAD];
    for (const char *p = load; *p; p++) g_type_rng = g_type_rng * 16u + (unsigned char) *p;

    int len = 0;
    while (load[len]) len++;
    for (int c = 1; c <= len && !skipped; c++) {
        for (int j = 0; j < c; j++) buf[j] = load[j];
        buf[c] = '\0';
        text_print(0, LOADING_TEXT_TOP_ROW + LOADING_ROW_LOAD, "%s", buf);
        skipped = loading_screen_wait(type_hold());
    }

    /* Phase 3: the drive answers, a whole line at a time. */
    for (int row = LOADING_ROW_LOAD + 1; row < LOADING_TEXT_LINES && !skipped; row++) {
        if (lines[row][0]) text_print(0, LOADING_TEXT_TOP_ROW + row, "%s", lines[row]);
        skipped = loading_screen_wait(TYPE_REPLY_MIN + (int) (type_rng() % TYPE_REPLY_SPAN));
    }

    if (skipped) {
        /* A skip during the fade-in abandons the rest of its ramp; land it at
           once so the filled-in block is actually visible, the same way the
           text is filled in rather than finished at typing speed. */
        if (g_fade_in_left > 0) { g_fade_in_left = 0; loading_screen_set_offset(0); }
        for (int row = 0; row < LOADING_TEXT_LINES; row++)
            text_print(0, LOADING_TEXT_TOP_ROW + row, "%s", lines[row]);
    }
}

/*----------------------
 | g_wallpaper_hidden
 | Description: Whether loading_screen_begin turned an image preset's wallpaper
 |   off, so loading_screen_end knows to turn it back on. It has to outlive the
 |   call that set it now that the screen spans main()'s whole load, and it is
 |   the display state at begin-time that matters rather than at end-time --
 |   re-reading g_display in loading_screen_end would work today only because
 |   nothing in between touches it.
 | Author: suinevere
 ----------------------*/
static bool g_wallpaper_hidden = false;

extern "C" void loading_screen_begin(const char *name) {
    music_pause();

    /* The wallpaper still goes, even though the colours below are now the
       player's own: the backdrop colour is behind NBG0, not over it, so a picture
       left up would have the boot text type across it and hide the background
       choice entirely. Just a ScrollDisable; the cached image is untouched and
       comes straight back in _end. */
    g_wallpaper_hidden = display_is_image(&g_display) != 0;
    if (g_wallpaper_hidden) title_bg_hide();

    /* The player's Display Options pair rather than a fixed black on white, so
       the load reads as part of their theme instead of a flash of someone else's.
       Safe to take verbatim: the display model refuses a background that clashes
       with its text (see clashes() in display.c), so whatever is set here is
       legible, and the fade below is a brightness offset that works off any pair. */
    menu_clear();
    SRL::VDP2::SetBackColor(HighColor(display_bg_rgb(g_display.bg)));
    text_set_color(display_text_rgb(g_display.text));

    loading_music_load();

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(name, lines);

    loading_screen_fade_in();
    loading_screen_type(lines);

    /* The typing normally spends the whole ramp several times over, and a skip
       lands it deliberately -- but nothing after this point spends it, so a
       block short enough to type in under LOADING_FADE_FRAMES would strand the
       screen part-lit for the entire load with no way back up. Cheap to make
       that impossible rather than rely on the text staying long. */
    if (g_fade_in_left > 0) { g_fade_in_left = 0; loading_screen_set_offset(0); }

    /* Deliberately no fade-out here: the block stays lit, at full brightness,
       with the cue still running, and main() reads the story underneath it.
       loading_screen_end finishes the job once the game is actually ready. */
}

/*----------------------
 | loading_screen_tick
 | Description: See loading_screen.h.
 | Author: suinevere
 | Dependencies: SRL, loading_music.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void loading_screen_tick(void) {
#ifdef DEBUG
    /* Row 22, below the boot block's eleven rows and clear of it. Debug builds
       only, and the format is limited to %d on purpose -- text_print
       supports %c/%s/%d/%0Nd and one stray %6d garbles the whole line.
         loop  -- times the cue has come round.
         of    -- how far into the current pass, in video fields, against how
                  long the pass is. Both are maintained by the V-blank handler,
                  so what this reports is whether that handler is running: the
                  count climbing between two prints means it serviced the cue
                  across whatever blocked in between, and a frozen count means
                  it did not. */
    int l = 0, f = 0, span = 0;
    loading_music_debug(&l, &f, &span);
    text_print(0, 22, "loop %d  %d of %d fields    ", l, f, span);
#endif
}

/*----------------------
 | loading_screen_end
 | Description: See loading_screen.h.
 | Author: suinevere
 | Dependencies: loading_music.h, title.h, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void loading_screen_end(void) {
    loading_screen_fade_out();
    loading_music_stop();

    /* Re-assert the player's colours under the black hold. _begin already set
       this pair, so this is a no-op on the ordinary path and stays here for the
       one that is not: display_apply() can swap both out mid-load if a picture
       fails to open, and the game's first screen is drawn with whatever is set
       the moment main()'s menu_fade_clear releases the hold. The wallpaper comes
       back the same way -- re-enabled while the screen is still black, so there
       is no flash. */
    text_set_color(display_text_rgb(g_display.text));
    SRL::VDP2::SetBackColor(HighColor(display_bg_rgb(g_display.bg)));
    if (g_wallpaper_hidden) { SRL::VDP2::NBG0::ScrollEnable(); g_wallpaper_hidden = false; }
    menu_clear();
}
