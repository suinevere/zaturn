/*----------------------
 | splash.h
 | Description: The boot splash: shows the SUINEVERE GAMES logo full-screen,
 |   fading in, holding for ten seconds (plus, on a first cold boot, the
 |   online-vocabulary CD read), then fading out, with a short PCM
 |   jingle (boot_music.h) playing from Low Work RAM underneath and fading
 |   with it. Skippable with any button or key except during that read. The game
 |   catalogue scan runs separately, after this splash, during the
 |   title-picture window in main.cxx.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, input.h, saturn_keyboard.h, SRL
 ----------------------*/
#ifndef SPLASH_H
#define SPLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | splash_show_once
 | Description: Shows the SUINEVERE GAMES logo (SUINE.TGA) full-screen, fading
 |   in, holding, then fading out -- 90 + 420 + 90 fields, ten seconds all told.
 |   The one piece of real work left under it is ensure_online_typeahead(), and
 |   only on a first cold boot; the background art used to be decoded here too
 |   and is not any more (see the note where display_preload_images used to be
 |   declared in title.h), which is why the hold is a deliberate ten seconds
 |   rather than a two-second top-up to however long the loads took.
 |   Loads the boot jingle first, before the logo itself, and plays it from
 |   Low Work RAM for the whole splash -- being resident in RAM rather than
 |   streamed, it does not contend with the logo or typeahead CD reads
 |   for the drive -- with its volume on the same ramp as the brightness, so
 |   the two rise and fall together. The screen stays black until the logo is
 |   decoded and uploaded, and is left black on return: main() blacks out
 |   again straight away to compose the title picture unseen, so this does not
 |   clear the offset it armed. Any button or key during the fade-in or the hold
 |   skips ahead, starting the fade-out at once from whatever brightness and
 |   volume the ramp had reached; a skip during the fade-in defers the typeahead
 |   read until afterwards, behind the black screen, since it is required either
 |   way and is seconds of blocking CD work that a skipped logo should not have
 |   to sit through. Runs only once per cold boot: a static flag survives the
 |   soft-reset longjmp back into main()'s setjmp, so a soft-reset return to the
 |   title just runs the typeahead load directly with no splash and no jingle --
 |   it is idempotent and near-instant once cached, so there is nothing
 |   real left to hide. If the splash art itself fails to load (e.g. missing
 |   from the disc), the jingle still plays, and this falls back to running
 |   that load with no fade rather than holding a blank or garbled
 |   screen. The game catalogue scan (preload_game_catalog) is not covered
 |   here -- main() runs it separately, in its own silent beat once the title
 |   picture is showing but before CD-DA starts.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void splash_show_once(void);

#ifdef __cplusplus
}
#endif

#endif
