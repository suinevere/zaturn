/*----------------------
 | loading_music.h
 | Description: The post-selection loading screen's background PCM cue: a
 |   short sample loaded whole into Low Work RAM from MSC/LOADCD.PCM and
 |   played on an SCSP PCM channel. Mirrors boot_music.h's shape exactly --
 |   see that header's "the two fades, and why they work differently" box
 |   for why fade-in is baked into the sample and fade-out goes through the
 |   driver's master volume rather than the channel's own level
 |   (SRL::Sound::Pcm::SetVolumePan kills a running channel outright).
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef LOADING_MUSIC_H
#define LOADING_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | loading_music_load
 | Description: Loads MSC/LOADCD.PCM whole into a Low Work RAM buffer. A
 |   no-op if already loaded, or if the file is missing -- pvms.bat's
 |   conversion is a warning, not a hard build failure, so a build without
 |   sox/loadCD.ogg must still boot cleanly. Steps into /MSC to do it and
 |   leaves the CD pointed back at /Z3, not at the root: this runs between
 |   game_select() and the story-file open, both of which need /Z3 current.
 |   See the loading_music_cd_restore box in loading_music.cxx.
 | Author: suinevere
 | Dependencies: SRL (Cd::File, Memory::LowWorkRam), title.h (cd_enter_root,
 |   cd_restore_z3), msc_dir.h (cd_enter_msc)
 ----------------------*/
void loading_music_load(void);

/*----------------------
 | LOADING_MUSIC_LEVEL_MAX
 | Description: Full volume for the SCSP PCM channel, 0..127 scale (not the
 |   0..7 CD-DA scale music.h's calls take).
 | Author: suinevere
 ----------------------*/
#define LOADING_MUSIC_LEVEL_MAX 127

/*----------------------
 | loading_music_fade_in
 | Description: Scales the first `frames` frames of the loaded sample by a
 |   rising ramp in place. Call after loading_music_load and BEFORE
 |   loading_music_play -- it edits the buffer the SCSP is about to be
 |   handed, so it has no effect once that buffer is playing. A no-op if
 |   nothing was loaded. Not idempotent.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
void loading_music_fade_in(int frames);

/*----------------------
 | loading_music_play
 | Description: Starts the loaded sample on a free PCM channel at full level and
 |   keeps it going round for as long as the loading screen is up: each time a
 |   pass ends the sample is started again from just past its faded-in head, so
 |   the screen holds its cue for however long the load takes rather than falling
 |   silent after one pass. Any fade-in must already be baked in. A no-op if
 |   nothing was loaded or a channel is already playing it.
 |
 |   Nothing has to be called to keep it running. The repeat is issued from the
 |   V-blank interrupt, which is the only way it can happen at all: the loading
 |   screen is a sequence of multi-second blocking operations -- the story read,
 |   mojo_boot, the walk through the Blorb index -- and there is no point inside
 |   any of them where a caller could service it. See the loading_music_vblank
 |   box in loading_music.cxx.
 |
 |   The seam is not gapless: it costs one field of silence, and it returns from
 |   a mid-phrase waveform to the sample's opening level.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm, Core::OnVblank)
 ----------------------*/
void loading_music_play(void);

/*----------------------
 | loading_music_debug
 | Description: Reports the cue's loop state for the loading screen's DEBUG-only
 |   readout: how many times it has come round, how many video fields into the
 |   current pass it is, and how many fields that pass lasts. Any argument may be
 |   NULL. Present in every build -- the counter costs one increment -- but only
 |   printed by a debug one. `frames` climbing while the main line is blocked is
 |   the sign that the V-blank loop is alive; frozen numbers mean it is not.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
void loading_music_debug(int *loops, int *frames, int *span);

/*----------------------
 | loading_music_set_level
 | Description: Turns the sound driver's *master* volume down to `level`,
 |   0 (silent) to LOADING_MUSIC_LEVEL_MAX (full) -- the fade-out ramp.
 |   Never touches the PCM channel's own level (see the header box above).
 |   Safe to call when nothing is playing; values outside the range are
 |   clamped. Must be put back by loading_music_stop.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl)
 ----------------------*/
void loading_music_set_level(int level);

/*----------------------
 | loading_music_stop
 | Description: Restores the driver's master volume, stops playback if
 |   active, and frees the Low Work RAM buffer. Call once the loading
 |   screen's fade-out has finished -- the typeahead trie is built moments
 |   later and needs that Low Work RAM headroom.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm, Memory), SGL (SND_SetTlVl)
 ----------------------*/
void loading_music_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* LOADING_MUSIC_H */
