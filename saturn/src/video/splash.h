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
 | splash_show
 | Description: Shows the SUINEVERE GAMES logo (SUINE.TGA) full-screen, fading in
 |   over 90 fields, doing the boot's loading at full brightness, then fading out
 |   over 90 more. There is no fixed hold: the screen lasts as long as the work
 |   under it -- ensure_online_typeahead(), preload_game_catalog() and
 |   display_preload_categories() -- and not a field longer.
 |   Loads the boot jingle first, before the logo itself, and plays it from Low
 |   Work RAM -- being resident rather than streamed, it does not contend with any
 |   CD read for the drive -- with its level on the brightness ramp, so picture and
 |   sound rise together. It does NOT fall together: the exit fades the picture
 |   alone and leaves the jingle at full, because it carries the title screen too
 |   and title_and_seed is what finally fades it.
 |
 |   Loading happens at full brightness and nowhere else. A button during the ramp
 |   skips straight to the exit, and then this screen loads nothing at all -- the
 |   title page picks up every piece of it instead. That is the point of the split:
 |   a player who skips the logo has said they do not want to sit through it, and
 |   the title page has its own moment to cover the work in. The ramp being cut
 |   short is also why the level is forced to full afterwards rather than left
 |   wherever the ramp stopped.
 |
 |   The screen stays black until the logo is decoded, and is left black on return:
 |   main() blacks out again straight away to compose the title picture unseen, so
 |   this does not clear the offset it armed. If the splash art fails to load the
 |   jingle still plays and this returns at once, leaving the title page to do the
 |   loading.
 |
 |   Runs in full every time, cold boot or soft-reset return alike. Skipping to a
 |   music-only branch on the way back would buy a couple of seconds and cost a
 |   second boot path whose only job is to reproduce by hand what this one already
 |   does; the skip button is there for anyone who wants the seconds back.
 |
 |   Needs Low Work RAM a finished game session does not have spare, so main() calls
 |   title_bg_cache_release() before every call -- a no-op on the first.
 | Author: suinevere
 | Dependencies: title.h, online.h, boot_music.h, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void splash_show(void);

#ifdef __cplusplus
}
#endif

#endif
