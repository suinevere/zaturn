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
#define SCSP_WAVES    5
#define SCSP_WAVE_MAX 256

/*----------------------
 | SCSP_NOISE_WAVE / SCSP_NOISE_LEN / SCSP_WAVE_BYTES
 | Description: The fifth waveform is the percussion voice, and it is longer
 |   than the four tonal ones because it is not a cycle of anything -- it is a
 |   slice of the 2A03's noise shift register, and one pass at the rate a drum
 |   is keyed at has to outlast the gap between two hits or the repeat is heard
 |   as a pitch. The chip's own noise generator was used here until a recording
 |   showed it peaking at 4 kHz and still bright at 15 kHz where the NES peaks
 |   at 2 kHz and rolls off; it has one setting and the SCSP has no filter --
 |   the slot registers stop at 0x16 -- so the brightness has to come from the
 |   rate the sequence is played at, which is what the NES's sixteen clock
 |   periods do. SCSP_WAVE_BYTES is the whole area, which is what the caller
 |   must leave free at wave_sa.
 | Author: suinevere
 ----------------------*/
#define SCSP_NOISE_WAVE  4
#define SCSP_NOISE_LEN   4096
#define SCSP_WAVE_BYTES  ((SCSP_WAVES - 1) * SCSP_WAVE_MAX + SCSP_NOISE_LEN)

/*----------------------
 | SCSP_NOISE_RUN / SCSP_NOISE_STRIDE
 | Description: Where in the percussion table each hit starts. The chip restarts
 |   a slot from SA every key-on, so without this every strike replays the same
 |   bytes -- 176 byte-identical hits in a 23.6-second loop, which the ear hears
 |   as one pitched click repeating rather than as noise. Measured: successive
 |   hits correlated 0.797 against the NES original's 0.486. RUN is how much of
 |   the table one hit can read before its envelope has ended, so the start may
 |   move anywhere in the rest; STRIDE is how far it moves each time, chosen odd
 |   and coprime with the sixteen-row drum pattern so a given slice does not land
 |   on the same beat twice. tools/assets/genwaves.py holds the same two numbers
 |   and saturn/tests/test_noise_table.py fails if they drift apart.
 | Author: suinevere
 ----------------------*/
#define SCSP_NOISE_RUN     1024
#define SCSP_NOISE_STRIDE  293

/*----------------------
 | SCSP_KEY_SETTLE
 | Description: How long to wait between keying a percussion voice off and on
 |   again. The chip re-strikes a slot only on a KYONB transition and walks all
 |   thirty-two slots once per output sample, so two KYONEX pulses a few
 |   instructions apart land in the same pass, the key-off is never seen, and the
 |   strike is silent. Measured on the chip: with no wait, one strike sounded in
 |   four seconds where thirty were scheduled; with a two-hundred iteration wait,
 |   all thirty did. This is comfortably more, and costs one wait per drum hit --
 |   at most one per V-blank -- in a handler that has 16.7 ms.
 | Author: suinevere
 ----------------------*/
#define SCSP_KEY_SETTLE    256

/* TL, the per-slot attenuator (register 0x0C, bits 7-0), is written as zero and
   the drum's fine level lives in its waveform table instead -- see NOISE_AMP in
   tools/assets/genwaves.py. TL does work, but not as a calibrated scale:
   recorded off the chip at 0 / 5 / 37 / 120, the drum's 4-8 kHz power goes
   10340 / 7732 / 3122 / 2814, so it is worth about 5.7 dB in total and is
   already clamped by 37 -- nothing like the 0.375 dB a step its eight bits
   suggest. The table's amplitude is exact by construction, is the same number
   the offline preview model uses, and halving it reproduces one DISDL step to
   the digit. A `SCSP_NOISE_TRIM` constant lived here claiming the 0.375 dB
   scale; it was never measured, because an unguarded `#define` in this header
   silently overrode every -D the sweep passed on the command line. */

/*----------------------
 | SCSP_EG_PERC_D1R
 | Description: The decay rate of the percussion envelope, register 0x08 bits
 |   10-6. Higher is faster, and the scale is geometric -- four steps is about
 |   a factor of two in time. This is how long a drum strike lasts, and it is
 |   the whole difference between a hat and a tick: at 24 the strike was gone
 |   inside 10 ms, where the NES original's burst is still at half power 30 ms
 |   in and audible past 60. Measured on the chip against a recording of the
 |   original, not read off a rate table. Cannot be slowed without limit -- the
 |   strike has to be over before the next one, and the closest pair in this
 |   tune is three frames, 50 ms.
 | Author: suinevere
 ----------------------*/
#ifndef SCSP_EG_PERC_D1R
#define SCSP_EG_PERC_D1R   17
#endif

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
 |   SCSP_WAVE_MAX, or SCSP_NOISE_LEN for SCSP_NOISE_WAVE
 | Returns: N/A
 ----------------------*/
void scsp_upload_wave(int index, const signed char *data, int len);

/*----------------------
 | scsp_key_on
 | Description: Starts a voice on a waveform at a pitch and level, looping in
 |   hardware so the note holds with no further writes. percussive picks the
 |   envelope: a pitched note sustains until the tracker keys it off, and a drum
 |   is never keyed off at all -- the pattern data only ever strikes it -- so it
 |   has to decay to silence by itself or the first hit latches on and every
 |   note afterwards plays under it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: voice -- 0..SCSP_VOICES-1; pitch -- packed OCT/FNS word; wave --
 |   waveform index; level -- 0..7; percussive -- nonzero to decay by itself
 | Returns: N/A
 ----------------------*/
void scsp_key_on(int voice, unsigned short pitch, int wave, int level, int percussive);

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
