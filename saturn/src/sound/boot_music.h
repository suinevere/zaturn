/*----------------------
 | boot_music.h
 | Description: The boot splash's background jingle: a short PCM sample loaded
 |   whole into Low Work RAM from MSC/SPLASH.PCM and played on an SCSP PCM
 |   channel, independent of the CD-DA music engine (music.h) and the CD-DA
 |   hardware channel it drives. Because the sample is fully resident in RAM
 |   before it plays, it is unaffected by the splash's own CD reads -- unlike
 |   CD-DA, PCM playback does not touch the drive.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef BOOT_MUSIC_H
#define BOOT_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*----------------------
 | boot_music_load
 | Description: Loads MSC/SPLASH.PCM into a Low Work RAM buffer, up to the
 |   BOOT_MUSIC_MAX_SECONDS cap; see that cap's box in boot_music.cxx before
 |   raising it. Call this before any other splash CD read (see splash.cxx) so the
 |   sample is resident by the time boot_music_play runs and by the time the
 |   splash's own logo read starts. A no-op if already loaded or if the file is
 |   missing.
 | Author: suinevere
 | Dependencies: SRL (Cd::File, Memory::LowWorkRam)
 ----------------------*/
void boot_music_load(void);

/*----------------------
 | BOOT_MUSIC_LEVEL_MAX
 | Description: Full volume for the SCSP PCM channel. This is the PCM level scale
 |   (0..127), which is NOT the 0..7 scale music.h's CD-DA calls take -- the two sit
 |   side by side in this codebase and handing a CD-DA-shaped 0..7 to a PCM channel
 |   asks for roughly 5% of full output.
 | Author: suinevere
 ----------------------*/
#define BOOT_MUSIC_LEVEL_MAX 127

/*----------------------
 | the two fades, and why they work differently
 | Description: The jingle's level is shaped by the caller (boot_music_set_level)
 |   and fades out by lowering the sound driver's master volume (boot_music_set_level).
 |   Neither is the obvious choice, and the obvious choice is why. SRL::Sound::Pcm::
 |   SetVolumePan -- slPCMParmChange under it, the only per-channel volume control
 |   SRL exposes -- does not lower a running PCM channel, it kills it. Opening the
 |   channel quiet and ramping up gave a completely silent splash, including a
 |   two-second hold at level 127; opening it at full and ramping down gave correct
 |   audio right up to the first ramp step and dead silence from then on. One call,
 |   both symptoms. SRL ships no sample that calls it, so there was never a working
 |   example behind it. So the channel level is now set exactly once, at slPCMOn, and
 |   never touched again: the rise is baked into the bytes before playback, where
 |   nothing downstream can ignore it, and the fall goes through SND_SetTlVl, a
 |   different mechanism entirely -- the driver command family whose sibling
 |   SND_SetCdDaLev this codebase already changes live and successfully in Sound
 |   Options. Do not reintroduce SetVolumePan here in either direction.
 | Author: suinevere
 ----------------------*/


/*----------------------
 | boot_music_play
 | Description: Plays the loaded sample once on a free PCM channel at full level,
 |   and once only: when the sample reaches its end a V-blank handler stops the
 |   channel and leaves it stopped, so the title screen is silent from there until
 |   the player presses on. Any fade-in is the caller's, via boot_music_set_level.
 |   A no-op if nothing was loaded or a channel is already playing it.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm)
 ----------------------*/
void boot_music_play(void);

/*----------------------
 | boot_music_playing
 | Description: Whether the jingle is still sounding. False once the sample has
 |   run to its end as well as before it starts, which is the point -- the cue
 |   plays once, so a title screen left up long enough reaches the press with
 |   nothing left to fade. Ask before any
 |   fade: boot_music_set_level moves the driver's MASTER volume, so ramping it for
 |   a sample that is not playing turns the whole machine down for nothing, and the
 |   restore that would undo it is the one boot_music_stop skips.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
bool boot_music_playing(void);

/*----------------------
 | boot_music_set_level
 | Description: Turns the sound driver's master volume down to `level`, 0 (silent)
 |   to BOOT_MUSIC_LEVEL_MAX (full) -- the splash's fade-out ramp. See the fade box
 |   above before changing how this works, and never build a fade-in on it. Two
 |   things follow from this being the *master* volume rather than the jingle's own:
 |   it would duck anything else playing at the same time (nothing is, during the
 |   splash), and it must be put back, which boot_music_stop does unconditionally.
 |   Values outside the range are clamped. Safe to call when nothing is playing.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl)
 ----------------------*/
void boot_music_set_level(int level);

/*----------------------
 | boot_music_stop
 | Description: Stops playback if active, restores the master volume, and frees the
 |   Low Work RAM buffer. Call once the splash has faded out, so the buffer does not
 |   sit on the heap for the rest of the session.
 |
 |   Blocks for about a second when something was playing, holding frames while it
 |   scrubs the sound driver's staged samples to silence -- without which the tail of
 |   this jingle sounds again underneath the next PCM cue to start, a whole menu
 |   later (see the scrub box in boot_music.cxx). Every caller reaches here with the
 |   screen already black, so the wait is unseen, but it is a wait: do not call this
 |   from anywhere that owes the player a responsive frame.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm, Core::Synchronize, Memory)
 ----------------------*/
void boot_music_stop(void);


#ifdef __cplusplus
}
#endif
#endif /* BOOT_MUSIC_H */
