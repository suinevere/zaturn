/*----------------------
 | scsp.h
 | Description: The SCSP register layer: the four slots the synth owns, keyed
 |   on and off directly rather than through the SGL sound driver, which this
 |   build does not load. Knows nothing about music -- pitch arrives as a
 |   ready-made register word. The register file and the waveform memory are
 |   injected by scsp_bind so the host tests can substitute arrays for the two
 |   hardware windows.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SCSP_H
#define SCSP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SCSP_VOICES / SCSP_SLOT_FIRST
 | Description: The synth owns four SCSP slots starting at 28. The top of the
 |   file is deliberate: the SGL sound driver allocates from the bottom, and a
 |   probe confirmed it leaves slot 31 untouched across three seconds of
 |   playback, which is what lets this coexist with the CD build's driver.
 | Author: suinevere
 ----------------------*/
#define SCSP_VOICES     4
#define SCSP_SLOT_FIRST 28

/*----------------------
 | SCSP_WAVES / SCSP_WAVE_MAX
 | Description: Four waveforms, 256 samples each, laid end to end in the
 |   waveform area. 256 samples at OCT 0 sounds 44100/256 = 172 Hz, about F3,
 |   which puts the tables where a bass line lives and leaves the pitch register
 |   to carry everything upward. The length is what gives an additive waveform
 |   room for its harmonics: at 64 samples a hard-edged square aliased into the
 |   buzz that made this sound like a PC speaker.
 | Author: suinevere
 ----------------------*/
#define SCSP_WAVES    4
#define SCSP_WAVE_MAX 256

/*----------------------
 | SCSP_REG_WORDS
 | Description: How many 16-bit words the register window spans: the 32 slots
 |   (0x000-0x3FF) plus the common control block that starts at 0x400. A host
 |   test's stand-in array must be this long, because scsp_enable_output writes
 |   past the last slot.
 | Author: suinevere
 ----------------------*/
#define SCSP_REG_WORDS 0x210

/*----------------------
 | scsp_bind
 | Description: Points the layer at its register file and waveform memory.
 |   wave_ram is where waveform bytes are copied; wave_sa is the SCSP byte
 |   address of that same memory, which is what goes in the slot's SA field.
 |   On hardware they name one place, on the host they do not, which is the
 |   whole reason they are separate parameters.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: regs -- slot register file base; wave_ram -- waveform bytes;
 |   wave_sa -- SCSP address of wave_ram
 | Returns: N/A
 ----------------------*/
void scsp_bind(volatile unsigned short *regs, volatile signed char *wave_ram, unsigned long wave_sa);

/*----------------------
 | scsp_silence
 | Description: Zeroes the four owned slots and nothing else.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void scsp_silence(void);

/*----------------------
 | scsp_enable_output
 | Description: Raises the SCSP's master volume to full, once, so the slots
 |   have an output path at all. This is NOT the music level: MVOL is the whole
 |   machine's volume, shared with CD-DA and the splash jingle, and the per-slot
 |   DISDL is what a level or a duck moves. It exists because a build with no
 |   sound driver has nothing that would otherwise set it -- every slot plays
 |   into a muted output and the result is total silence with every slot
 |   register correct, which is exactly the bug that shipped this once.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void scsp_enable_output(void);

/*----------------------
 | scsp_upload_wave
 | Description: Copies one waveform into its slice of the waveform area and
 |   remembers its length, which becomes the slot's loop end.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- 0..SCSP_WAVES-1; data -- signed 8-bit samples; len -- up to
 |   SCSP_WAVE_MAX
 | Returns: N/A
 ----------------------*/
void scsp_upload_wave(int index, const signed char *data, int len);

/*----------------------
 | scsp_key_on
 | Description: Starts a voice on a waveform at a pitch and level, looping in
 |   hardware so the note holds with no further writes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; pitch -- packed OCT/FNS word; wave --
 |   waveform index; level -- 0..7
 | Returns: N/A
 ----------------------*/
void scsp_key_on(int voice, unsigned short pitch, int wave, int level);

/*----------------------
 | scsp_key_on_noise
 | Description: Starts a voice on the SCSP's internal noise generator, which
 |   needs no waveform data at all -- the percussion voice.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; level -- 0..7
 | Returns: N/A
 ----------------------*/
void scsp_key_on_noise(int voice, int level);

/*----------------------
 | scsp_key_off
 | Description: Releases a voice.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1
 | Returns: N/A
 ----------------------*/
void scsp_key_off(int voice);

/*----------------------
 | scsp_set_voice_level
 | Description: Changes a sounding voice's level without disturbing its pitch
 |   or restarting it. This is DISDL, the slot's own send level -- never MVOL,
 |   which is the whole machine's and is shared with CD-DA.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; level -- 0..7
 | Returns: N/A
 ----------------------*/
void scsp_set_voice_level(int voice, int level);

#ifdef __cplusplus
}
#endif
#endif /* SCSP_H */
