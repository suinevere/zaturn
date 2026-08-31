/*----------------------
 | splash.h
 | Description: The boot splash: shows the SUINEVERE GAMES logo full-screen for
 |   a fixed six seconds -- fade in, hold, fade out, the two ramps equal -- with
 |   a short PCM jingle (boot_music.h) playing from Low Work RAM underneath and
 |   fading with it. Nothing loads under it. Skippable with any button or key at
 |   any point after the logo is on screen. The game catalogue scan runs
 |   separately, behind the title screen's own art.
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
 | Description: Shows the SUINEVERE GAMES logo (SUINE.TGA) full-screen: 90 fields
 |   fading in, 180 holding, 90 fading out -- six seconds exactly, every time.
 |
 |   Nothing is loaded under it, and that is the point. The screen used to carry
 |   whatever boot work would fit (the online vocabulary read, the catalogue
 |   scan, a picture-cache warm), which made its length a property of the drive
 |   rather than a decision, and made every new read someone parked here a
 |   silent extension of a logo nobody asked to watch for longer. The catalogue
 |   scan runs behind the title screen's art instead, where there is already a
 |   picture and a prompt to look at.
 |
 |   Loads the boot jingle first, before the logo itself, and plays it from Low
 |   Work RAM -- being resident rather than streamed, it does not contend with any
 |   CD read for the drive -- with its level on the brightness ramp, so picture and
 |   sound rise together. It does NOT fall together: the exit fades the picture
 |   alone and leaves the jingle at full, because it carries the title screen too
 |   and title_and_seed is what finally fades it.
 |
 |   A button during the fade-in or the hold cuts to the exit, which then runs in
 |   SPLASH_SKIP_FADE_STEP-sized strides so the way out is visibly faster than
 |   the ramp the player just declined. The ramp being cut short is why the
 |   jingle's level is forced to full afterwards rather than left wherever the
 |   ramp stopped.
 |
 |   The screen stays black until the logo is decoded, and is left black on return:
 |   main() blacks out again straight away to compose the title picture unseen, so
 |   this does not clear the offset it armed. If the splash art fails to load the
 |   jingle still plays and this returns at once.
 |
 |   Runs in full every time, cold boot or soft-reset return alike. Skipping to a
 |   music-only branch on the way back would buy six seconds and cost a second
 |   boot path whose only job is to reproduce by hand what this one already
 |   does; the skip button is there for anyone who wants the seconds back.
 |
 |   Needs Low Work RAM for the jingle that a finished game session does not have
 |   spare, so main() calls room_art_release() before every call -- a no-op on
 |   the first.
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
