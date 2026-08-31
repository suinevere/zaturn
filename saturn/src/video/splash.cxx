/*----------------------
 | splash.cxx
 | Description: The boot splash: fades the SUINEVERE GAMES logo in, holds, and
 |   fades out, in a fixed six seconds end to end. Nothing is loaded underneath
 |   it -- the logo is not a cover for work, and its length does not vary with a
 |   drive's speed or with which boot this is. A short PCM jingle
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
 |   volume the ramp had reached, so every field of the six is responsive. The
 |   two CD reads before the ramp -- the jingle and the logo itself -- are the
 |   one stretch a press cannot be caught in: blocking work with no seam fine
 |   enough to catch a tap in, and the box in input.h records the four attempts
 |   at making it otherwise. The game catalogue scan runs separately,
 |   behind the title screen's own art (title_and_seed). Fades using the
 |   VDP2 hardware color offset (SetColorOffsetA) -- a per-frame register
 |   write -- rather than rewriting the TGA's palette or re-uploading the
 |   bitmap every frame. The offset darkens every opaque NBG0 pixel
 |   uniformly; it assumes SUINE.TGA's black fill is a real (non-index-0)
 |   palette entry, since VDP2 treats palette index 0 as transparent and the
 |   offset cannot darken what shows through a transparent hole. If the logo
 |   ever looks like only its glyphs fade while the surround stays fixed,
 |   that is a palette authoring issue in the TGA, not a bug in this file.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, input.h, saturn_keyboard.h,
 |   console_view.h, SRL
 ----------------------*/
#include "splash.h"
#include "title.h"
#include "online.h"
#include "boot_music.h"
#include "input.h"
#include "saturn_keyboard.h"
#include "console_view.h"
#include <srl.hpp>
#include "text_map.h"

/*----------------------
 | SPLASH_FADE_FRAMES / SPLASH_HOLD_FRAMES
 | Description: The ramp each way and the flat top between them: 90 fields is
 |   1.5s at the 60fps NTSC field rate this codebase assumes elsewhere, and 180
 |   is 3s, so the whole screen is a level six seconds.
 |
 |   Six, and fixed. It was ten once, back when the hold covered a background-art
 |   decode; then the hold constant was dropped entirely and the screen lasted as
 |   long as whatever loading had been put under it, which made a logo whose
 |   length nobody could predict and which grew with every read someone parked
 |   there. Nothing loads under it now -- the catalogue scan moved behind the
 |   title screen's own art, and the picture cache it also fed no longer exists
 |   -- so the length is a decision rather than a measurement.
 |
 |   The two ramps are equal on purpose: a logo that fades in over a second and
 |   a half and out over a quarter reads as being cut off rather than as leaving.
 |   A skip is the one thing that breaks the symmetry, and it is meant to (see
 |   SPLASH_SKIP_FADE_STEP).
 | Author: suinevere
 ----------------------*/
#define SPLASH_FADE_FRAMES   90
#define SPLASH_HOLD_FRAMES   180

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
 | splash_set_offset
 | Description: One step of the logo's ramp, through the shared screen-wide fade
 |   so the picture layer moves with it -- a bare SetColorOffsetA reaches the text
 |   layer only, and would leave the logo stuck wherever the arm left it.
 |   splash_show's title_bg_fade_arm is the matching engage.
 | Author: suinevere
 | Dependencies: title.h
 | Globals: N/A
 | Params: v -- signed offset, -255 (black) .. 0 (unmodified)
 | Returns: N/A
 ----------------------*/
static void splash_set_offset(int16_t v) {
    title_bg_fade_level((int) v);
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
 | splash_show
 | Description: See splash.h.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, input.h, saturn_keyboard.h, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void splash_show(void) {
    // Black before anything else: the jingle read and the logo read below are
    // seconds of CD work, and there is nothing worth showing during them.
    title_bg_fade_arm();

    boot_music_load();   // first thing: resident in RAM before any other splash CD read

    for (int r = 0; r < console_screen_rows(); r++) text_clear_line(r);

    bool have_logo = title_bg_show_oneoff("SUINE.TGA");

    boot_music_set_level(have_logo ? 0 : BOOT_MUSIC_LEVEL_MAX);
    boot_music_play();

    if (!have_logo) return;

    bool skipped = false;   // a button cut the splash short; drives the fast exit
    int  step    = 0;       // where the ramp is now, so fade-out can pick it up

    for (; step <= SPLASH_FADE_FRAMES; step++) {
        splash_set_step(step);
        SRL::Core::Synchronize();
        if (splash_skip_pressed()) { skipped = true; break; }
    }
    if (step > SPLASH_FADE_FRAMES) step = SPLASH_FADE_FRAMES;
    boot_music_set_level(BOOT_MUSIC_LEVEL_MAX);

    // The flat top. Polled per field rather than slept through, so a press
    // during it reaches the fade-out on the frame it was made -- the hold is
    // the longest stretch of the screen and would be the worst one to be deaf
    // in.
    for (int held = 0; !skipped && held < SPLASH_HOLD_FRAMES; held++) {
        SRL::Core::Synchronize();
        if (splash_skip_pressed()) skipped = true;
    }

    // Down from wherever the entry ramp got to, so a skip two frames in is a
    // two-frame fade rather than a jarring full-length one from a nearly black
    // screen (a skip during the hold is already at the top and gets the full
    // stride) --
    // and in SPLASH_SKIP_FADE_STEP-sized strides if the player asked to move on,
    // so the exit is half a second instead of a ninety-frame ramp they have
    // already said they do not want (see that box).
    const int dec = skipped ? SPLASH_SKIP_FADE_STEP : 1;
    for (; step >= 0; step -= dec) {
        splash_set_light(step);
        SRL::Core::Synchronize();
    }
    splash_set_light(0);   // a stride that overshot still has to land on black

    title_bg_hide();
    // The color offset is left armed at black on purpose: main() blacks out again
    // immediately (title_bg_fade_arm) to compose the title screen unseen, so
    // clearing it here would only open a window for a stray bright frame in
    // between.
}
