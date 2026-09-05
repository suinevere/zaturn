# Design: SH-2 generated music for the netbin

**Date:** 2026-09-04
**Status:** Proposed

## Goal

Give the netbin music without shipping a recorded sample. The netbin links no
sound at all today (`saturn/makefile:104`, `SRL_USE_SGL_SOUND_DRIVER = 0`), and
the recorded-loop route costs about 52 KB of driver and library before a single
audio byte: `SDDRVS.TSK` (26,610 B) plus `BOOTSND.MAP` (82 B) embedded, `LIBSND.A`
(4,276 B), and the `LIBPCM.A` members the streamed path pulls (~18.8 KB). The
sample itself then costs 21.5 KB per second at the 8-bit 22050 Hz the existing
pipeline emits -- `SPLASH.PCM` is 463,689 bytes for 21 seconds, already past the
netbin's assumed 400 KB ceiling on its own.

Instead the SCSP plays short generated waveforms with hardware loop points, and
the SH-2 writes only pitch, level and envelope registers when a note changes.
Target cost: **about 6 KB** against a netbin last measured at 200,464 bytes.

## What the spike established

A throwaway probe (SRL project, driver staged, run under Mednafen 1.32.1 with
`-soundrecord`, analysed by Goertzel rather than by ear) answered the three
questions the approach rested on:

| Question | Result |
| --- | --- |
| Do SH-2 writes to SCSP slot registers land? | Yes. Slot 31 register `0x00` read back `0x837` -- exactly the written KYONB + LPCTL=normal-loop + PCM8B + SA high nibble. KYONEX reads 0 because it is write-only, as the manual states. |
| Does the SGL sound driver clobber a high slot? | No. `0x837` was still there after 180 frames with the driver loaded, and the slot sounded a clean 689 Hz tone (RMS 21405; 689 Hz energy present, 344 Hz absent). |
| Does a slot play with the sound block off? | Yes. After `slSoundOffWait()` the same slot sounded 344 Hz (344 Hz energy present, 689 Hz absent) and held it. **No sound driver and no 68000 program are required.** |

Two incidental findings shape the design:

- **`CA` is useless as a liveness signal here.** Its LSB represents 4096 samples
  (SCSP User's Manual 4.2.8), so a 64-sample loop never advances it. The probe's
  first verdict of "dead" was this, not the hardware. Nothing in this design reads `CA`.
- **SRL never calls `slSoundOnWait()` when the driver is disabled**
  (`srl_core.hpp:107`, `srl_sound.hpp:73`), so the netbin never puts the sound
  block into any state. The synth must establish one itself.

Everything above is emulator evidence. Hardware confirmation is owed and is
tracked under Open items.

## Non-goals

- No sampled instruments, no MOD/XM import, no asset pipeline. The music is a
  hand-authored pattern table compiled into the image.
- No per-room or per-mood variation. One loop, everywhere, in both builds.
- No sound effects. The netbin has none today and gains none here.
- The synth never replaces CD-DA where CD-DA exists.

## Architecture

Three modules and their data under `saturn/src/sound/`, each with one job and
testable apart from the others:

| File | Responsibility | Knows about |
| --- | --- | --- |
| `scsp.c` / `scsp.h` | Register layer: sound-block init, slot key on/off, pitch, level, waveform upload. | SCSP registers only. No notion of music. |
| `synth.c` / `synth.h` | Voice layer: generates the waveforms into sound RAM, holds envelope presets, converts a note number to OCT/FNS. | `scsp.h` |
| `tracker.c` / `tracker.h` | Sequencer: walks pattern data, emits note-on/off per tick. | `synth.h` |
| `music_synth_data.c` | The authored loop: waveform assignments, patterns, order list. | data only |

The rest of the application sees five functions and no SCSP:

    void synth_init(void);            /* claim slots, upload waveforms, silence */
    void synth_start(void);           /* begin the loop from its first row */
    void synth_stop(void);            /* key off, leave slots idle */
    void synth_set_level(int level);  /* 0..7, the Options slider and the duck */
    void synth_tick(void);            /* one V-blank */

### Slot and volume ownership

The synth owns **SCSP slots 28-31** and writes no other slot. The spike proved
slot 31 survives the SGL driver; taking the top four keeps the synth away from
whatever the driver allocates from the bottom.

Level is applied through each slot's own `DISDL` (register `0x16`, bits 15-13)
and `TL` (register `0x0C`, bits 7-0). It is **not** applied through `MVOL`.
`MVOL` is the whole machine's master volume, shared with CD-DA and the splash
jingle; ducking the music during the online session must not duck everything.
`boot_music.h` documents the same trap from the other direction, where the
per-channel call killed the channel and only the master worked. Here the
per-slot registers are ours alone, so they are the correct lever.

**`MVOL` must still be raised once, and that is a different thing.** A build
with no sound driver has nothing that ever sets it, so every slot plays into a
muted output: correct registers, total silence. This shipped broken once and was
caught by recording the emulator rather than by any test. `scsp_enable_output()`
raises it at init, netbin only -- in the CD build `SND_Init` has already done it,
which is why the splash jingle is audible today without anyone touching it. The
rule is therefore "never `MVOL` for level", not "never `MVOL`".

### Note conversion

From the SCSP User's Manual 4.2.5, pitch is `OCT[3:0]` (register `0x10`, bits
14-11) and `FNS[9:0]` (bits 9-0), with `OCT = 0, FNS = 0` playing the waveform at
its sampling rate. Sega publishes the semitone table, which is the whole
conversion:

| Semitone | C | C# | D | D# | E | F | F# | G | G# | A | A# | B |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| FNS | 0x000 | 0x03D | 0x07D | 0x0C2 | 0x10A | 0x157 | 0x1A8 | 0x1FE | 0x25A | 0x2BA | 0x321 | 0x38D |

`synth_note(int semitone)` returns the packed register value: twelve table
entries plus an octave offset added to OCT. It is a pure function and is the
first thing the host tests cover.

### Waveforms

Four waveforms, 64 samples each, 8-bit signed, generated by code at init into
256 bytes of sound RAM: square, 25% pulse, triangle, saw. Each slot loops its
waveform in hardware (`LPCTL` = normal loop, `LSA` = 0, `LEA` = 63), so a held
note costs the SH-2 nothing between note changes. `LSA`/`LEA` are sample counts
from `SA`, not byte addresses (manual 4.2.1).

Noise needs no waveform data at all: `SSCTL` = 1 makes the slot output the
internal noise generator (manual 4.2.1), which is the percussion voice.

Total image cost for the instrument set: **zero bytes**. Total sound-RAM cost:
256 bytes at a fixed offset (`0x70000` in the spike).

### Tick and timing

`synth_tick()` runs from the V-blank interrupt at 60 Hz, the mechanism
`boot_music.cxx` already uses for its end-of-sample handler. This is not a
convenience: the netbin blocks on modem reads and the CD build blocks on CD
reads, and a tick driven from the main loop would drop tempo whenever either
happened. Tracker speed is expressed as ticks per row, so the tempo is a
divisor of 60 Hz, which is what tracker music has always assumed.

### Data format

Up to four channels. A row cell is two bytes -- a note byte (0 = no change,
1 = key off, 2+ = semitone index) and a packed byte carrying waveform index and
volume. Pattern length, channel count, speed and loop point are all fields of
the song rather than constants, so the tune's shape is data. The shipped loop is
four patterns of sixteen rows across three channels, about 400 bytes; the 2 KB
in the budget below is the ceiling that allows for growing it.

## Build reach and gating

Compiled into both builds. It starts only where there is nothing better:

- **Netbin:** always, since the build has no CD audio by definition.
- **CD build:** only when `music_cdda_has_audio()` is false -- a disc with no
  CD-DA. Where the disc has music, the synth stays silent and unstarted.

The netbin additionally establishes the sound block state before its first
write, because nothing else in that build ever touches it.

## Options UI

The Sound page (`menu_pages.cxx:697`, `sound_options_page`) already builds a
visible-row list from availability flags (`has_cd`, `has_blb`) and remembers the
selection as a row ID rather than an index, precisely so the same page can show
different rows on different discs. The synth extends that list; it does not get a
page of its own.

| Row | Shown when |
| --- | --- |
| Sound On/Off (master) | any source available -- now including the synth |
| CD Music level | `has_cd` |
| Synth Music level | `!has_cd` -- the synth is always compiled in, so its absence is never the reason to hide the row |
| PCM level | `has_blb` |
| Ok / Cancel | always |

**CD Music and Synth Music are mutually exclusive** -- where the disc has CD-DA
the synth row is hidden, and where it does not the CD row is hidden, so the page
never offers two music sliders at once.

The master On/Off row derives its displayed value from the levels rather than
storing a flag, and its toggle sets them; both behaviours extend to
`g_synth_level` so that On/Off remains a true music on/off when the synth is the
only source. **Assumption worth confirming at review:** "music on/off" is read
here as the existing master row extended to cover the synth, not a second
dedicated on/off row beside the slider; the page's own design note argues
against storing a separate flag.

The netbin gains the Sound page, which it does not have today. It is added to
the pause menu list (`netbin_pages.cxx:898`) between "Gameplay" and "Controls",
matching where Sound sits in the CD build's menu. On the netbin only the master
row and the Synth Music level appear, since it has neither CD-DA nor a Blorb.

## Persistence

`g_synth_level` (0..7) joins the MOJOOPTS blob. The blob is strictly positional,
so a new field cannot simply be appended in the middle -- but the layout already
carries a **dead 3-byte sound block**: sentinel `1` followed by a mix mode and a
track number, both settings long gone, kept only because everything behind them
is measured from them (`options.cxx:275`).

That block is reclaimed without moving anything:

- Sentinel `10` marks the new form: `[10][synth_level][reserved]`. Width is
  unchanged at three bytes, so every block behind it stays where it is.
- Sentinel `1` is still recognised and its two bytes are still skipped, exactly
  as now, leaving `g_synth_level` at its compiled default. An older save is not
  misparsed and is not reset.
- `10` is safe as a discriminator: display sentinels use 1-4, 6, 8 and 9, and
  gameplay uses 5 and 7.

## Size budget

| Item | Estimate |
| --- | --- |
| `scsp.c` | ~1.5 KB |
| `synth.c` (incl. waveform generation) | ~1 KB |
| `tracker.c` | ~1 KB |
| `music_synth_data.c` (shipped loop ~400 B; ceiling for a longer tune) | ~2 KB |
| Options row + persistence changes | ~0.3 KB |
| **Total** | **~5.8 KB** |

Against the recorded-loop alternative at 52 KB plus 21.5 KB per second, and
against a netbin whose last measured size was 200,464 bytes.

## Testing

Host-side, in the existing suite:

- `synth_note()` against all twelve of Sega's published FNS values and across
  octaves.
- Register encoding: the packed values for `0x00`, `0x10` and `0x16` given known
  inputs, asserted against the bit positions in this document.
- Tracker stepping: row advance at a given speed, pattern order traversal, loop
  point, and key-off emission.
- Gating: synth start/stop decisions given the four combinations of
  `has_cd` / `has_blb`.

On hardware, via an ODE, using the CD build on a disc with no CD-DA -- which is
also the only practical way to hear the netbin's synth before a NetLink session.

## Measured

Built and measured on 2026-09-04, on branch `synth-music`.

| | Bytes |
| --- | --- |
| netbin at the branch point (`95e0802`) | 207,760 |
| netbin with the generated music | 215,488 |
| **Feature total** | **7,728** |
| of which the engine's own objects (`scsp`, `synth`, `tracker`, song data, target glue) | 4,192 |

Against the loader's 409,600 ceiling, and against 52 KB plus 21.5 KB per second
for the recorded-sample route. The remaining ~3.5 KB is the Sound page the
netbin did not have, its second page implementation, the options block and the
level global.

**Audio verified in emulation, not on hardware.** A throwaway image linking the
shipped modules was recorded under Mednafen and analysed by Goertzel against the
frequencies the song data implies. The first bar sounds 579 Hz -- semitone A at
octave -1, from a 64-sample waveform whose base is 44100/64 -- and at 13 s it
moves to 434 Hz, semitone E, which is what pattern 0 row 8 specifies. So the
waveform upload, the note conversion, the pitch register and the tracker's
timing are all confirmed end to end.

## Risks and open items

- **No hardware run yet.** Every result above is Mednafen. The register writes
  are from the Sega manual and were confirmed by read-back and by recorded
  audio, but emulator agreement is not hardware agreement. This is the open item
  that matters most: the silence bug above proves the emulator catches real
  faults, but it cannot prove the reverse.
- **The CD-build fallback has never been heard.** It only plays on a disc with
  no CD-DA, and this project's own disc has 31 audio tracks, so every recording
  here came from a purpose-built image. It also takes the one path that does
  NOT call `scsp_enable_output()`, relying on the SGL driver having raised
  `MVOL` instead -- reasoning that is sound but untested.
- **The netbin's true starting state is untested.** The spike reached "sound
  block off" by turning it off after the driver had initialised. The netbin
  arrives with whatever PlanetWeb left behind, which is not the same starting
  point, and the sound-block init in `scsp.c` is the code that has to answer for
  the difference.
- **Sound-RAM collision is unconfirmed.** `0x70000` took a 64-byte write with
  the driver loaded and nothing broke, but `BOOTSND.MAP`'s region layout was not
  decoded, so 256 bytes there is not yet proven safe in the CD build. Confirm on
  the ODE run before treating the address as settled.
- **The synth is only as good as the pattern table.** The engine can be right
  and the tune still poor; that is an authoring problem, not an engineering one,
  and it is worth hearing early rather than at the end.
