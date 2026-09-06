/*----------------------
 | synth.h
 | Description: The voice layer: what a note is, and what the four instruments
 |   sound like. Converts a semitone and octave into the SCSP's packed
 |   OCT/FNS word and builds the waveforms the slots loop. The five-function
 |   playback API that sits on top of this arrives with the tracker.
 | Author: suinevere
 | Dependencies: scsp.h
 ----------------------*/
#ifndef SYNTH_H
#define SYNTH_H

/* Included, not merely declared as a dependency: SYNTH_WAVE_NOISE is defined
   in terms of SCSP_NOISE_WAVE, so a translation unit that reaches for it
   without scsp.h already in scope does not compile. */
#include "scsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SYNTH_WAVE_*
 | Description: The four generated waveforms, in the order scsp_upload_wave
 |   receives them, so the index is both the instrument and its place in the
 |   waveform area. Named for the NES voices they carry: three pulse duties and
 |   the 2A03's stepped triangle. tools/assets/genwaves.py --voice smooth puts
 |   band-limited equivalents in the same four slots without changing these
 |   names, since the roles do not change with the shapes.
 | Author: suinevere
 ----------------------*/
#define SYNTH_WAVE_PULSE12  0
#define SYNTH_WAVE_PULSE25  1
#define SYNTH_WAVE_TRIANGLE 2
#define SYNTH_WAVE_PULSE50  3

/*----------------------
 | SYNTH_WAVE_NOISE
 | Description: The percussion voice, and a real waveform: a slice of the
 |   2A03's own 15-bit noise shift register, sixteen times the length of a
 |   tonal table because it is not a cycle of anything. The chip's internal
 |   noise generator was used here and cost no sound RAM at all, but it has one
 |   setting and the SCSP has no filter -- the slot registers stop at 0x16 --
 |   so how bright the percussion is could not be chosen. Held as a waveform,
 |   the note it is keyed at picks the shift register's clock rate, which is
 |   what the NES does with its sixteen periods.
 | Author: suinevere
 ----------------------*/
#define SYNTH_WAVE_NOISE    SCSP_NOISE_WAVE

/*----------------------
 | synth_pitch
 | Description: Packs a semitone and octave into the SCSP pitch register word
 |   (OCT bits 14-11, FNS bits 9-0). Semitone 0 at octave 0 is the waveform's
 |   own rate, which for a 64-sample wave is 689 Hz. OCT is four bits and
 |   negative octaves wrap into it, so an octave down is 0xF rather than -1.
 |   Inputs outside range are clamped rather than rejected, because a bad note
 |   byte in pattern data should sound wrong, not silence the channel.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: semitone -- 0 (C) to 11 (B); octave -- -8 to 7
 | Returns: the register word for slot offset 0x10
 ----------------------*/
unsigned short synth_pitch(int semitone, int octave);

/*----------------------
 | synth_wave_build
 | Description: Generates one waveform into a caller-supplied buffer. Costs no
 |   image bytes, which is the point -- four instruments for the price of the
 |   code that draws them.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- a SYNTH_WAVE_* value other than SYNTH_WAVE_NOISE, which is
 |   too long to resample and is uploaded whole; out -- buffer of at least len
 |   bytes; len -- samples to generate
 | Returns: N/A
 ----------------------*/
void synth_wave_build(int index, signed char *out, int len);

/*----------------------
 | synth_bind
 | Description: Points the synth at the sound chip. On the target this is the
 |   SCSP register window and the waveform area inside sound RAM; the host
 |   tests pass arrays. Call before synth_init.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: regs -- slot register file base; wave_ram -- waveform memory;
 |   wave_sa -- SCSP address of wave_ram
 | Returns: N/A
 ----------------------*/
void synth_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa);

/*----------------------
 | synth_note_on
 | Description: Sounds one note, or releases the channel when semitone is -1.
 |   This is the tracker's sink, public because it is also the only way to play
 |   a note that is not in the song -- which is how the noise voice is reached
 |   and tested, since the shipped loop carries no percussion yet. Scales the
 |   note's own volume by the current music level on the way to the chip, and
 |   remembers both so a later level change can rescale a sounding voice.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: channel -- 0..SCSP_VOICES-1; semitone -- 0..11 or -1 to release;
 |   octave -- -8..7; wave -- a SYNTH_WAVE_* value; vol -- 0..7
 | Returns: N/A
 ----------------------*/
void synth_note_on(int channel, int semitone, int octave, int wave, int vol);

/*----------------------
 | synth_init
 | Description: Silences the owned slots and uploads the four generated
 |   waveforms. Safe to call twice.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_init(void);

/*----------------------
 | synth_start
 | Description: Starts the default tune from its beginning. The first
 |   synth_tick after this sounds the first row.
 | Author: suinevere
 | Dependencies: tracker.h, music_synth_data.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_start(void);

/*----------------------
 | synth_start_song
 | Description: Starts one tune of the catalogue from its beginning. Asking for
 |   the tune already playing does nothing at all, which is the whole reason
 |   this is not just synth_stop plus synth_start: the room engine re-asserts
 |   its choice on every room change inside a category, and restarting the loop
 |   each time would chop the music up on a walk through four rooms of the same
 |   mood.
 | Author: suinevere
 | Dependencies: tracker.h, music_synth_data.h
 | Globals: N/A
 | Params: index -- 0..synth_song_count()-1; out of range plays the default
 | Returns: N/A
 ----------------------*/
void synth_start_song(int index);

/*----------------------
 | synth_song
 | Description: Which tune is loaded, whether or not it is sounding.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: a song index
 ----------------------*/
int synth_song(void);

/*----------------------
 | synth_song_count
 | Description: How many tunes this build carries.
 | Author: suinevere
 | Dependencies: music_synth_data.h
 | Globals: N/A
 | Params: N/A
 | Returns: the catalogue size
 ----------------------*/
int synth_song_count(void);

/*----------------------
 | synth_play_track
 | Description: Plays whichever tune stands in for a CD-DA track, and stops on
 |   track 0. Shaped as music.h's music_play_fn so the room engine can be given
 |   this instead of the CD-DA backend on a disc that carries no audio -- which
 |   is what makes the tunes room music rather than one loop over everything.
 |   The loop argument is ignored: a tune has no end to not repeat at.
 | Author: suinevere
 | Dependencies: music_synth_data.h
 | Globals: N/A
 | Params: track -- a CD-DA track number, or 0 to stop; loop -- ignored
 | Returns: N/A
 ----------------------*/
void synth_play_track(int track, int loop);

/*----------------------
 | synth_track_is_short
 | Description: Whether a track plays once rather than looping. Always no: the
 |   tracker loops every tune forever, so nothing the synth plays ends on its
 |   own. Shaped for music_set_isshort.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: track -- ignored
 | Returns: 0
 ----------------------*/
int synth_track_is_short(int track);

/*----------------------
 | synth_pause
 | Description: Holds the tune where it is and silences the voices. The
 |   position is kept, so synth_resume carries on rather than starting the
 |   piece again -- which is the difference between an in-game menu and a room
 |   change, and both reach this through the same engine.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_pause(void);

/*----------------------
 | synth_resume
 | Description: Lifts a synth_pause. The next row sounds; notes that were
 |   holding across the pause do not, because nothing re-keys a voice until the
 |   pattern says so.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_resume(void);

/*----------------------
 | synth_stop
 | Description: Stops the loop and keys every voice off.
 | Author: suinevere
 | Dependencies: tracker.h, scsp.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_stop(void);

/*----------------------
 | synth_cut
 | Description: Stops the music the way a menu wants it stopped -- at once,
 |   with no release. synth_stop lets each voice run down at the release rate,
 |   which is what a note ending should sound like and not what stepping off a
 |   track in a list should: there the fade is heard as the track you have left
 |   still playing. Use synth_stop for music that is ending and this for a
 |   preview that is being abandoned.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_cut(void);

/*----------------------
 | synth_set_level
 | Description: Sets the music level, 0 (silent) to 7. Reaches voices that are
 |   already sounding without restarting them, because the in-game duck happens
 |   mid-note. Applied per slot, never through the machine's master volume,
 |   which CD-DA and the splash jingle share.
 | Author: suinevere
 | Dependencies: scsp.h
 | Globals: N/A
 | Params: level -- 0..7, clamped
 | Returns: N/A
 ----------------------*/
void synth_set_level(int level);

/*----------------------
 | synth_tick
 | Description: One V-blank of playback. A no-op when stopped, so the handler
 |   can call it unconditionally.
 | Author: suinevere
 | Dependencies: tracker.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_tick(void);

/*----------------------
 | synth_playing
 | Description: Whether the loop is running.
 | Author: suinevere
 | Dependencies: tracker.h
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero while playing
 ----------------------*/
int synth_playing(void);

/*----------------------
 | synth_should_play
 | Description: Whether the synth is the right music source. It is the
 |   fallback, so the answer is no wherever the disc brought its own music.
 |   The netbin passes 0 because it has no disc, which makes this one rule for
 |   both builds rather than a compile-time switch.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: has_cd_audio -- nonzero when the disc carries CD-DA
 | Returns: nonzero when the synth should play
 ----------------------*/
int synth_should_play(int has_cd_audio);

#ifdef __cplusplus
}
#endif
#endif /* SYNTH_H */
