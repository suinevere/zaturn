/*----------------------
 | splash.cxx
 | Description: The boot splash: fades the SUINEVERE GAMES logo in, holds for a
 |   fixed ten seconds (and, on a first cold boot, for however long the online
 |   typeahead read takes on top of that), then fades out. It used to cover the
 |   background-art preload as well; that is gone, and the hold is now the
 |   screen's whole purpose rather than a cover for work. A short PCM jingle
 |   (boot_music.h) plays
 |   underneath the whole thing, loaded whole into Low Work RAM before the
 |   logo itself is read so it never competes with the splash's own CD reads
 |   for the drive; it fades up with the logo and back down with it, though by
 |   two different means -- the rise is baked into the sample before playback
 |   and only the fall touches the channel level, for the reasons set out in
 |   boot_music.h. The screen is held
 |   black from the first instruction here until the logo is decoded and
 |   uploaded, so the several seconds of CD work before that show nothing
 |   rather than a bare console. Any button or key during the fade-in or the hold
 |   skips ahead: the fade-out starts immediately from whatever brightness and
 |   volume the ramp had reached. A skip during the fade-in also defers the
 |   typeahead read until behind the black screen afterwards; a skip during the
 |   hold is after it, so there is nothing left to defer. That read is the one
 |   stretch a press still cannot be caught in -- blocking CD work with no seam
 |   fine enough to catch a tap in, and the box in input.h records the four
 |   attempts at making it otherwise. The game catalogue scan runs separately, after this
 |   splash returns, during the HOUSE1.TGA window in main.cxx. Fades using the
 |   VDP2 hardware color offset (SetColorOffsetA) -- a per-frame register
 |   write -- rather than rewriting the TGA's palette or re-uploading the
 |   bitmap every frame. The offset darkens every opaque NBG0 pixel
 |   uniformly; it assumes SUINE.TGA's black fill is a real (non-index-0)
 |   palette entry, since VDP2 treats palette index 0 as transparent and the
 |   offset cannot darken what shows through a transparent hole. If the logo
 |   ever looks like only its glyphs fade while the surround stays fixed,
 |   that is a palette authoring issue in the TGA, not a bug in this file.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, input.h, saturn_keyboard.h, SRL
 ----------------------*/
#include "splash.h"
#include "title.h"
#include "online.h"
#include "boot_music.h"
#include "input.h"
#include "saturn_keyboard.h"
#include <srl.hpp>

#include "game_catalog.h"

/*----------------------
 | SPLASH_FADE_FRAMES
 | Description: Fade-in/fade-out length, 90 frames = 1.5s at the 60fps NTSC field
 |   rate this codebase assumes elsewhere.
 |
 |   There is no hold constant beside it any more. The screen used to sit at full
 |   brightness for a flat 420 frames so that the whole splash came to ten seconds,
 |   which was the length it had back when it covered the background-art decode.
 |   That was ten seconds of the machine visibly doing nothing. It covers real work
 |   again now -- the typeahead trie and display_preload_categories() both run at
 |   peak brightness -- so the screen lasts as long as the load does and not a field
 |   longer.
 | Author: suinevere
 ----------------------*/
#define SPLASH_FADE_FRAMES   90

/*----------------------
 | SPLASH_PRELOAD_SLOTS
 | Description: How many category pictures this screen caches before handing over,
 |   the rest being left to the title screen.
 |
 |   Four is what fits rather than a preference: the jingle is resident through all
 |   of this and holds ~409 KB of the zone, so tga_cache_slot stops granting after
 |   the fourth anyway (see test_lwram_splash_budget.py). Saying it out loud keeps
 |   the splash's length tied to a number someone can read instead of to whatever
 |   the allocator happened to allow.
 | Author: suinevere
 ----------------------*/
#define SPLASH_PRELOAD_SLOTS 4

/*----------------------
 | SPLASH_AUDIO_RAMP_FRAMES
 | Description: How long the jingle takes to reach full level, 24 fields being
 |   four tenths of a second.
 |
 |   Much shorter than the picture's ramp on purpose. Sharing SPLASH_FADE_FRAMES
 |   made the music creep in over a second and a half, which reads as a fault
 |   rather than an entrance; the picture wants that long and the sound does not.
 | Author: suinevere
 ----------------------*/
#define SPLASH_AUDIO_RAMP_FRAMES 24

/*----------------------
 | SPLASH_SKIP_FADE_STEP
 | Description: How many points of the ramp a skipped fade-out crosses per frame,
 |   making the exit SPLASH_FADE_FRAMES/this frames long instead of the full 90 --
 |   6, so a quarter of a second.
 |
 |   Without this, a press late in the ramp is honoured and looks like it was not.
 |   The fade-out starts from wherever the ramp had got to, so a press half a
 |   second in gives a half-second exit and feels responsive, but a press at step
 |   89 gives the full ninety-frame ramp from near-full brightness -- which is
 |   indistinguishable from what the splash does when nobody presses anything.
 |
 |   3, not 6. At 6 the exit was a quarter of a second and read as a cut rather
 |   than a fade. Half a second is enough to read as leaving deliberately while
 |   still being unmistakably faster than the ninety frames of not skipping.
 | Author: suinevere
 ----------------------*/
#define SPLASH_SKIP_FADE_STEP 3

/*----------------------
 | g_splash_shown
 | Description: True once the splash has run. Plain static RAM, so it
 |   survives the soft-reset longjmp back into main()'s setjmp exactly like
 |   title.cxx's art cache -- a soft reset re-enters with the trie already
 |   built, so there is no real load left to hide.
 | Author: suinevere
 ----------------------*/
static bool g_splash_shown = false;

/*----------------------
 | splash_set_offset
 | Description: Sets VDP2 color offset A to (v,v,v) on all three channels
 |   and applies it. Offset A must already be enabled on NBG0 via
 |   UseColorOffset for this to have any visible effect.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: v -- signed offset, -255 (black) .. 0 (unmodified)
 | Returns: N/A
 ----------------------*/
static void splash_set_offset(int16_t v) {
    SRL::VDP2::ColorOffset offset(v, v, v);
    SRL::VDP2::SetColorOffsetA(offset);
}

/*----------------------
 | splash_set_light
 | Description: One point on the picture's ramp alone, 0 black to
 |   SPLASH_FADE_FRAMES full; used by the exit, which leaves the jingle at level.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: step -- position on the ramp, clamped to 0..SPLASH_FADE_FRAMES
 | Returns: N/A
 ----------------------*/
static void splash_set_light(int step) {
    if (step < 0) step = 0;
    if (step > SPLASH_FADE_FRAMES) step = SPLASH_FADE_FRAMES;
    splash_set_offset((int16_t) (-255 + (255 * step) / SPLASH_FADE_FRAMES));
}

/*----------------------
 | splash_set_step
 | Description: One point on the entry ramp, moving the picture and the jingle's
 |   output level -- the sound on its own shorter ramp, full well before the
 |   picture is.
 |
 |   The level rather than the sample, which is the whole point. The rise used to
 |   be multiplied into the first 1.5 seconds of SPLASH.PCM before playback, so
 |   those bytes were permanently ducked and the loop had to restart past them --
 |   every repeat began 1.5 seconds in, at the same wrong place. Shaping the output
 |   instead leaves the sample intact, so a repeat can start where the music does.
 | Author: suinevere
 | Dependencies: boot_music.h, SRL
 | Globals: N/A
 | Params: step -- 0 (black/silent) to SPLASH_FADE_FRAMES (full)
 | Returns: N/A
 ----------------------*/
static void splash_set_step(int step) {
    splash_set_light(step);
    if (step < 0) step = 0;
    if (step > SPLASH_AUDIO_RAMP_FRAMES) step = SPLASH_AUDIO_RAMP_FRAMES;
    boot_music_set_level((BOOT_MUSIC_LEVEL_MAX * step) / SPLASH_AUDIO_RAMP_FRAMES);
}


/*----------------------
 | splash_skip_pressed
 | Description: True on any gamepad button or any keyboard key this frame -- the
 |   "I have seen this logo, move on" signal. Deliberately the widest possible
 |   test (AnyPressed covers the d-pad and START as well as the face buttons),
 |   since a player reaching for a way past a logo should not have to find a
 |   particular button. Edge-triggered, matching title_and_seed, so a button left
 |   held from one screen does not carry into the next.
 | Author: suinevere
 | Dependencies: input.h, saturn_keyboard.h
 | Globals: g_pad
 | Params: N/A
 | Returns: true if the splash should be cut short
 ----------------------*/
static bool splash_skip_pressed(void) {
    if (g_pad && g_pad->AnyPressed()) return true;
    return saturn_keyboard_poll().kind != SATURN_KEY_NONE;
}

/*----------------------
 | splash_show_once
 | Description: See splash.h.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, input.h, saturn_keyboard.h, SRL
 | Globals: g_splash_shown
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void splash_show_once(void) {
    if (g_splash_shown) {
        ensure_online_typeahead();
        return;
    }
    g_splash_shown = true;

    title_bg_fade_arm();
    boot_music_load();

    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);

    bool have_logo = title_bg_show_oneoff("SUINE.TGA");

    boot_music_set_level(have_logo ? 0 : BOOT_MUSIC_LEVEL_MAX);
    boot_music_play();

    if (!have_logo) return;

    bool skipped = false;
    int  step    = 0;

    for (; step <= SPLASH_FADE_FRAMES; step++) {
        splash_set_step(step);
        SRL::Core::Synchronize();
        if (splash_skip_pressed()) { skipped = true; break; }
    }
    if (step > SPLASH_FADE_FRAMES) step = SPLASH_FADE_FRAMES;
    boot_music_set_level(BOOT_MUSIC_LEVEL_MAX);

    if (!skipped) {
        ensure_online_typeahead();
        preload_game_catalog();
        display_preload_categories(SPLASH_PRELOAD_SLOTS);
    }

    const int dec = skipped ? SPLASH_SKIP_FADE_STEP : 1;
    for (; step >= 0; step -= dec) {
        splash_set_light(step);
        SRL::Core::Synchronize();
    }
    splash_set_light(0);

    title_bg_hide();
}
