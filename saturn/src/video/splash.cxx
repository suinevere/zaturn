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

/*----------------------
 | SPLASH_FADE_FRAMES / SPLASH_SETTLE_FRAMES
 | Description: Fade-in/fade-out length (90 frames = 1.5s at the 60fps NTSC
 |   field rate this codebase already assumes elsewhere, e.g. title.cxx's
 |   SOFT_RESET_HOLD) and the settle pause held at full brightness before the
 |   fade-out begins.
 |
 |   The three numbers add up on purpose: 90 + 420 + 90 is 600 fields, ten
 |   seconds, and that is now the whole of what this screen does. It used to be a
 |   cover for real work -- the background art was decoded here, several seconds
 |   of CD reads, and the settle was a flat 2s top-up so that a run finding
 |   everything already cached did not flash the logo past unrecognizably fast.
 |   The art is not decoded here any more (see title.cxx's TGA_CACHE_SLOTS box:
 |   thirty-seven pictures do not fit a 1 MB zone, so they are read on demand
 |   instead), which left the logo appearing and leaving in about three seconds.
 |   So the hold is now the point rather than a top-up, and it is sized to the
 |   duration the screen used to have.
 |
 |   Still a flat number rather than a measured floor. ensure_online_typeahead()
 |   below is opaque frame-count-wise -- there is no counter to read back without
 |   changing its signature, and it blocks in the CD driver where nothing is
 |   ticking a frame count anyway -- so on a first cold boot its read lands on top
 |   of these ten seconds rather than inside them.
 | Author: suinevere
 ----------------------*/
#define SPLASH_FADE_FRAMES   90
#define SPLASH_SETTLE_FRAMES 420

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
 | splash_set_light / splash_set_step
 | Description: One point on the ramp, where step 0 is black and step
 |   SPLASH_FADE_FRAMES is full. splash_set_light moves the picture alone; that is
 |   all the fade-in needs, because the jingle's rise is already baked into its
 |   sample data before playback (boot_music_fade_in) and re-ramping it live would
 |   square the curve. splash_set_step also pulls the channel level down and is for
 |   the fade-out only -- see the fade box in boot_music.h for why the two
 |   directions do not use the same mechanism.
 | Author: suinevere
 | Dependencies: boot_music.h, SRL
 | Globals: N/A
 | Params: step -- position on the ramp, clamped to 0..SPLASH_FADE_FRAMES
 | Returns: N/A
 ----------------------*/
static void splash_set_light(int step) {
    if (step < 0) step = 0;
    if (step > SPLASH_FADE_FRAMES) step = SPLASH_FADE_FRAMES;
    splash_set_offset((int16_t) (-255 + (255 * step) / SPLASH_FADE_FRAMES));
}

static void splash_set_step(int step) {
    splash_set_light(step);
    if (step < 0) step = 0;
    if (step > SPLASH_FADE_FRAMES) step = SPLASH_FADE_FRAMES;
    boot_music_set_level((BOOT_MUSIC_LEVEL_MAX * step) / SPLASH_FADE_FRAMES);
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

    // Black before anything else: the jingle read and the logo read below are
    // seconds of CD work, and there is nothing worth showing during them.
    title_bg_fade_arm();

    boot_music_load();   // first thing: resident in RAM before any other splash CD read

    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);

    bool have_logo = title_bg_show_oneoff("SUINE.TGA");

    // The rise is written into the sample now, while it is still just bytes in
    // LWRAM; once slPCMOn has the buffer it is too late to shape it. No fade-in
    // when there is no logo to fade in with.
    if (have_logo) boot_music_fade_in(SPLASH_FADE_FRAMES);
    boot_music_play();

    if (!have_logo) {
        ensure_online_typeahead();
        boot_music_stop();
        return;
    }

    bool skipped = false;   // a button cut the splash short; drives the fast exit
    bool loaded  = false;   // ...but the trie read is a separate question: a skip
                            // during the hold happens AFTER it, and re-running it
                            // below would read the disc twice behind a black screen
    int  step    = 0;       // where the ramp is now, so fade-out can pick it up

    for (; step <= SPLASH_FADE_FRAMES; step++) {
        splash_set_light(step);   // the jingle is rising on its own, from the sample
        SRL::Core::Synchronize();
        if (splash_skip_pressed()) { skipped = true; break; }
    }
    if (step > SPLASH_FADE_FRAMES) step = SPLASH_FADE_FRAMES;

    // The typeahead read is still the one stretch a press cannot be caught in.
    // Not for want of trying -- four different ways of sampling input during the
    // boot loads were built and taken back out, and the box in input.h is the
    // record of each. The short version is that it is blocking CD work with no
    // seam fine enough to sample a tap at, and abandoning it part-way would leave
    // a half-built trie.
    //
    // Skipping during the fade-in defers it until after the fade-out: the point of
    // a skip is that the picture starts leaving at once, and this is seconds of
    // blocking CD work that would strand it on screen.
    if (!skipped) {
        ensure_online_typeahead();
        loaded = true;

        // Polled, unlike the hold this replaced. That one was two seconds long and
        // sat right after several seconds of opaque CD work, so honouring a press
        // inside it and not a moment earlier would have read as a coin toss. This
        // one is seven seconds of nothing happening on purpose, and making a
        // player sit through all of it with the machine visibly idle is the worse
        // trade -- the inconsistency is now confined to a first cold boot, where
        // the trie read is the only unresponsive stretch left.
        for (int i = 0; i < SPLASH_SETTLE_FRAMES; i++) {
            SRL::Core::Synchronize();
            if (splash_skip_pressed()) { skipped = true; break; }
        }
    }

    // Down from wherever the ramp got to, so a skip two frames in is a two-frame
    // fade rather than a jarring full-length one from a nearly black screen --
    // and in SPLASH_SKIP_FADE_STEP-sized strides if the player asked to move on,
    // so the exit is half a second instead of a ninety-frame ramp they have
    // already said they do not want (see that box).
    const int dec = skipped ? SPLASH_SKIP_FADE_STEP : 1;
    for (; step >= 0; step -= dec) {
        splash_set_step(step);
        SRL::Core::Synchronize();
    }
    splash_set_step(0);   // a stride that overshot still has to land on black

    title_bg_hide();
    boot_music_stop();
    // The color offset is left armed at black on purpose: main() blacks out again
    // immediately (title_bg_fade_arm) to compose HOUSE1.TGA unseen, so clearing it
    // here would only open a window for a stray bright frame in between.

    if (!loaded) {
        ensure_online_typeahead();
    }
}
