---
name: hihat-crunch-handoff
description: The drums, over four rounds of the owner's ear against measurement -- a strike that died in ten milliseconds, every strike played as an accent, one drum sound where there are two, and finally the tune being in 3/4 all along. Ends with the owner editing the drum tablature himself, which is what the session's tooling was for. Two of this note's own earlier conclusions are corrected inside it.
metadata:
  type: project
---

Branch `synth-music`, **19 commits ahead of `origin/main`**, the last of them
(`d1c2e0c`, "Tune song.") the owner's own edit of the drum tablature rather than
mine. Both Saturn targets link, netbin 223,904 bytes of 409,600, and all 26
Python and six C sound tests pass.

**Read this note bottom-up if you are short of time.** It grew over four rounds
and the later ones correct the earlier ones; the section "The drums were never
missing, they were quiet" is the current state and the two corrections it makes
matter more than anything above it. Continues
[[nes-voicing-shadowgate-handoff]], which carries the eleven faults, the eleven
traps and the reference; read it first. That note is **stale on one point only**:
its open item 1 says `SCSP_NOISE_TRIM` gives the drum a fine level in TL steps of
0.375 dB. It does not, and never did -- see below.

Still true: nothing has run on real hardware.

## What the owner reported, and what it was

"High hat still needs more crunch, check for tone/volume."

It was not tone. Measured on the chip against the same recording of the NES
original, with strikes isolated by subtracting the 23 ms before each onset from
the 23 ms after it, the energy a strike puts into 2-9 kHz per 10 ms:

| | 0 | 10 | 20 | 30 | 40 | 50 | 60 ms |
|---|---|---|---|---|---|---|---|
| NES original | 1.00 | 1.20 | 0.85 | 0.53 | 0.28 | 0.23 | 0.19 |
| ours, before | 1.00 | 0.05 | 0.00 | 0.00 | 0.00 | 0.08 | 0.01 |
| ours, after | 1.00 | 1.09 | 0.66 | 0.38 | 0.24 | 0.22 | 0.16 |

Our strike was **over inside ten milliseconds**. The original's is a burst still
audible past sixty. A ten-millisecond noise burst is a tick; the crunch the owner
was asking for is the other fifty milliseconds.

The cause is one field. `SCSP_EG_PERCUSSIVE` was the literal `0xFE1F`, which is
D2R 31, **D1R 24**, AR 31, and with DL at full attenuation D1R alone runs the
strike to silence -- so that one number is the length of a drum hit. It is now
`SCSP_EG_PERC_D1R` in `scsp.h`, composed into the register rather than spelled as
a magic word, and swept on the chip: 24 / 20 / 17 / 14 / 11 give decays of
`1.00 0.05 0.00`, `1.00 0.67 0.20`, `1.00 1.19 0.67 0.44 0.26`,
`1.00 1.34 1.08 0.86`, `1.00 1.10 1.02 0.89`. **17** is the original's curve
almost point for point. The waveform loops (LPCTL is 01), so the envelope is the
only thing that ends a hit -- there is no other candidate.

## Then the volume, because the two are not independent

A strike five times longer carries far more energy. At the old level it buried the
tune: whole-mix band error against the original went from 0.082 to 0.804. The
drum came down one DISDL step (`CH_VOL` 7 -> 6) and the rest came out of the
noise table's own amplitude, which is a new `--noise-amp` in `genwaves.py`.
Swept on the chip, DISDL 6 with amplitudes 100 / 80 / 64 / 55: band errors
**0.235 / 0.112 / 0.099 / 0.080**, which put it at 55 -- later re-swept to 65
once the beat accent below took energy back out of the drum, which is the
shipped value. The two tables in this section are the state after the first
commit, not the final one.

Calibration check worth keeping: amplitude 50 on DISDL 6 reproduces amplitude 100
on DISDL 5 **digit for digit**, which is the 6 dB step it has to be. That is what
told me the new knob was real.

## The fine trim never existed

`SCSP_NOISE_TRIM` is gone. TL (slot register `0x0C`, bits 7-0) does work, but not
as the 0.375 dB-a-step attenuator the previous note assumed: recorded off the chip
at 0 / 5 / 37 / 120, the drum's 4-8 kHz power goes **10340 / 7732 / 3122 / 2814**
-- about 5.7 dB in total, already clamped by 37. The drum's fine level is the
table amplitude instead, which is exact by construction and is also the number
the offline preview model plays, so the model and the chip cannot drift apart on
it the way the previous note records them doing.

## Where the measurements ended up

Octave-band shares of total energy over one 23.5-second statement, whole mix:

| | 60-125 | 125-250 | 250-500 | 500-1k | 1-2k | 2-4k | 4-8k | 8-16k | err |
|---|---|---|---|---|---|---|---|---|---|
| NES original | 7.9 | 31.5 | 23.0 | 21.8 | 5.0 | 4.5 | 3.7 | 2.0 | -- |
| before | 8.2 | 33.4 | 22.3 | 20.4 | 5.7 | 4.3 | 4.1 | 1.6 | 0.082 |
| after | 8.0 | 32.8 | 23.5 | 19.5 | 5.7 | 4.4 | 4.3 | 1.7 | **0.080** |

So the level balance is where it was -- slightly better -- and the strike is now
the right length. The strike's own colour also moved toward the original in the
bands it was hot in: 3k/4k/6k/8k go 0.834/0.768/0.469/0.359 to
0.766/0.626/0.410/0.275 against the original's 0.675/0.548/0.345/0.201.

Sizes: netbin **223,904 bytes** of 409,600; pattern data unchanged at 2,200. All
six synth host tests pass, both music host tests pass, 15 Python tests pass, and
both Saturn targets link.

## The second report: "too much high hat, but the triples sound right"

Right on both counts, and the level was not the whole of it. Aligning the shipped
pattern data against the recording by cross-correlation and asking the original
how much high-band flux it has on the rows we strike:

| relative to a strike on the eighth | original | before | after |
|---|---|---|---|
| on the eighth | +0.0 dB | +0.0 | +0.0 |
| the sixteenth between two eighths | **-8.0 dB** | -0.3 | -7.3 |
| a thirty-second offbeat | **-9.4 dB** | -1.7 | -8.8 |

We were playing all two hundred strikes as accents. The original accents the
eighth and puts everything between them 8 to 9 dB down -- which is what "too much
hi-hat" is, and why the triples sounded right: they are in the correct *places*,
they were simply as loud as the backbeat. `DRUM_UNACCENTED_DROP` in `mid2pat.py`
takes a strike that does not land on an eighth down one DISDL step, which is 6 dB
and the nearest the chip has; 96 strikes stay, 104 drop.

The sequence cannot supply this itself. Its drum velocities do track the beat --
100 on the eighths, 96 between them, 83 on the thirty-seconds -- but that is
1.6 dB against the 8 dB measured, and it cannot survive a 6 dB step anyway. The
converter discards velocity entirely; honouring it would not have been enough.

Levelling again afterwards, because the accent takes energy out: amplitudes
55 / 65 / 75 give whole-mix band errors 0.123 / 0.086 / 0.084, and 65 puts the
4-8 kHz band on 3.9 per cent against the original's 3.7 where 75 gives 4.6.
Shipped at 65.

## What "check the notes and pauses" turned up

- **The tonal parts are exact.** Walking the emitted song the way the tracker
  does and comparing every one of the 384 rows against the source parts: no
  disagreement on any voice, either a note sounding where the part rests or the
  reverse. That check is `rowcheck.py`-shaped and worth keeping; the previous
  note flagged it as never repeated after the grid changed, and it has now been
  run at 1/32.
- **The drum plays about fifty strikes more than the original.** At a threshold
  calibrated to recover our own 200 strikes with 100 per cent row accuracy, the
  original strikes 145-159 rows of the 384 against our 200. Structurally the
  original plays the hat on *eighths* -- all twelve bars, all eight positions --
  with sixteenths and thirty-seconds as decoration; we play sixteenths
  throughout. The accent above makes that a dynamic difference rather than a
  density one, which is what the original sounds like; actually thinning the
  pattern would mean transcribing the drum track out of the recording, which is
  not done.
- **The original breathes and the sequence never does.** In one statement the
  original spends 5.8 per cent of its 10 ms frames under a quarter of median
  energy and 1.0 per cent under a tenth; ours spends 0.1 and 0.1. That is 17
  stretches of 20 to 190 ms, 1.33 seconds in all, and they cluster at the ends of
  four-bar phrases -- bar 3 (rows 80-100), bar 6 (rows 176-192) and bar 12 (rows
  366-381). The fan sequence has **no row at all** where nothing is sounding, so
  neither do we. Unfixed; see Open.

## The third report: "open/close sound, ours all one tone"

The owner heard two drum sounds in the original where ours has one. That is
right, and it took two attempts to find which two.

**The wrong pair, built and removed.** The sequence labels notes 46 and 49 open
hat and crash, so those got a second envelope that rings -- a sixth wave index
sharing the percussion table's bytes, `SCSP_EG_PERC_D1R_RING`, swept on the chip
to 12 because that reproduced the original's ratio of ringing-row to ordinary-row
sustain exactly (1.82 measured, 1.82 target, against 0.76 with no ring). The
owner's verdict was "end high hat open sound weird", so it is reverted -- the
whole engine change, the tests and all. Kept here because the sweep was sound and
the mechanism is a clean twenty lines if a later report wants a longer drum: a
second wave index whose `g_wave_off` copies the noise wave's, `SCSP_IS_NOISE`
covering both, and the envelope chosen in `scsp_key_on`.

**The right pair.** This arrangement's recurring figure is three strikes carrying
a closed hat then one or two carrying only a snare -- `h.h.h.s.` -- which is the
owner's "three bright taps then a quick sharp tap". Measured on the original in
2-8 kHz, a hat row and a snare-only row differ by +74 Hz of centroid and 1.13x in
the 5-8 kHz share; ours differed by -53 Hz and 0.96x, i.e. not at all. On a
machine with one noise channel the only thing separating two drums is the rate
the shift register is clocked at, so `DRUM_DARK_SEMITONES` strikes snare-only
rows ten semitones lower: +61 Hz and 1.13x, the original's separation, with the
whole-mix band error unmoved (0.088 against 0.086).

**Then the drums stopped coming from the MIDI at all.** The owner began writing
the part out as tablature, which is the right answer to a drum channel that plays
about fifty strikes a statement more than the recording does.
`tools/assets/music/castle-halls-drums.tab` is now the drum part -- one line a
bar, sixteen slots, `h` `s` `k` `.`, slashes decorative, a trailing `xN` to
repeat -- and `--drums-tab` makes `mid2pat` ignore MIDI channel 10 entirely. The
tonal parts still come from the sequence. Pattern data went 2,200 to 2,968 bytes
and the netbin to 224,672, because the drum pattern no longer repeats on the same
16-row boundary the tonal parts do.

**A slot can hold a triplet, and that is the whole point of the notation.** The
first tablature was read at plain sixteenths and the emitted part came back with
run lengths `{1: 124}` -- every strike a single, the fast three-strike figure the
tune is built on gone entirely, which is what the owner meant by "yours sounded
right just not right tablature". Three of the same letter is now **one slot**
struck three times on consecutive thirty-seconds. That also resolves what looked
like typos: the owner's groups ran five and six characters against a four-slot
beat, and with `sss` counting as one they come to exactly sixteen a bar.

Two traps in that parser, both of which bit:

- Slashes cannot be decoration. A beat ending `hh` beside one starting `h` is
  three letters in a row and is indistinguishable from a triplet unless the beat
  boundaries are honoured, so they are parsed as real.
- The accumulator and the per-beat list were both called `slots`, which silently
  returned only the last bar -- twenty strikes over two and a half bars, and the
  tune stopped. Pinned now in `saturn/tests/test_drum_tab.py`, ten tests.

Where the triplets belong, measured off the recording: the original plays them
**only in bars 6-11 and never in bars 0-5**, always starting on an eighth --
rows 192, 204, 228, 240, 252, 276, 288, 300, 324, 336, 348, 372. The tablature
in the file does not follow that yet; it repeats the owner's opening figure
through both halves, because the owner's answer was that this is the part they
want played rather than a transcription to be scored against the recording.

The emitted part now runs **152 strikes against the original's ~150**, with run
lengths `{1: 92, 3: 10, 5: 6}` against `{1: 83, 2: 16, 3: 12}` -- the density
problem is closed. The runs of five are a triplet written in a bar's last slot
running into the next bar's first, which is legal but worth knowing.

The tablature is still **my reading of what the owner wrote**, and the second and
third bars especially need checking. Correcting it is one command: edit the .tab
and re-run the line recorded in the header of `music_synth_data.c`.

## The tune is in 3/4, and everything above assumed 4/4

The owner brought a sheet-music transcription from another model with a 3/4 time
signature on it. It is right, and it is the largest single correction in this
whole run of sessions.

Autocorrelating the original's own drum flux on the 1/32 row grid:

| lag in rows | 8 | 12 | 16 | 24 | 32 | 48 | 64 | 96 |
|---|---|---|---|---|---|---|---|---|
| | +0.27 | **+0.85** | +0.30 | **+0.84** | +0.32 | **+0.91** | +0.30 | **+0.84** |

Every multiple of twelve scores 0.79 to 0.91; every four-four lag that is not
also a multiple of twelve scores 0.27 to 0.42. The melody band agrees more
quietly (lag 24 beats lag 32, 0.30 against 0.16). **A bar is 24 rows and there
are sixteen of them in a statement**, not twelve bars of 32.

Read on the right grid the part is obvious, and it was not before:

    bars 0-7    XX..X.X./X...XX../X.X.X...   -- the same bar eight times
    bars 8,10,12,14  XXX.X.../X...XXX./X...X...
    bars 9,11,13,15  X...X.../X...XXX./X...X...

Everything in this note above this section that names a bar number is in 4/4
bars of 32 rows and has to be divided by 24 instead to mean anything. The
triplets are at beat 1 and beat 2-and of the even bars of the second half and
beat 2-and of the odd ones -- one clean two-bar figure, where in 4/4 they looked
scattered across bars 6 to 11 at no consistent position.

The tablature is now `--tab-beats 3`, and a beat may be written either as four
slots of a sixteenth (where three of a letter is a triplet) or written out as
eight thirty-seconds, chosen by the group's own length -- the fast figures are
easier to read literally. The emitted part measures **152 strikes against the
original's 151**, run lengths `{1: 84, 2: 16, 3: 12}` against `{1: 83, 2: 16,
3: 12}`. That is the drum timing solved.

Which strikes are hats and which are snares is **not** measured and cannot be
with what is here -- the two differ by 74 Hz of centroid in the original and the
pulse voices share the band. The tab puts a hat on each beat and a snare on the
figures between, which is the owner's "three bright taps then a quick sharp
tap"; changing a letter and re-running is a second's work.

A snare inside a fast figure is darkened three semitones instead of ten
(`DRUM_FAST_SEMITONES`), because ten suits a snare standing alone and made the
triplets dull -- the owner's "more crunch on that triplet snare".

## The drums were never missing, they were quiet

Two claims made earlier in this same note are **wrong** and are corrected here.

**Wrong: "the original plays no triplets in the first half."** It plays them from
bar one. Detecting strikes in a single pass drops the third of every triplet,
because that strike carries about a third the energy of the ones beside it --
levels 1.19 and 1.10 against 2.7 to 3.5. Averaging the four times the original's
96-row block repeats lifts them clear. Do that before transcribing anything.

**Wrong: "the original plays about 150 strikes to our 200."** It plays **184**.
The fan sequence's drum channel was much closer than this note said, and the
whole "we are 30 per cent too busy" line above should be read with that in mind.

Transcribed properly the part is one figure, beat one alternating a triplet with
a double:

    sss.h.h./h...sss./h.h.h...
    ss..h.h./h...sss./h.h.h...

which is exactly what the owner had been describing in words for three rounds --
"sss, two hats / hat, sss / three hats" -- while I kept mis-parsing it.

**Then the triplets still did not sound, and the data was not the problem.**
176 of 184 strikes were sounding. What was wrong was their level: the beat accent
drops any strike off the eighth by 6 dB, which is right for a lone off-beat snare
and ruinous for a triplet, whose second and third strikes are off the beat by
construction. Measured on the chip against the recording:

| | row 0 | row 1 | row 2 |
|---|---|---|---|
| original | 2.73 | 1.93 | 1.19 |
| ours, before | 2.52 | **0.67** | 1.08 |
| ours, now | 2.15 | 1.62 | 1.85 |

A strike with another beside it now keeps the accent (`fast` in the drum emit
block of `mid2pat.py`) and is darkened three semitones instead of ten. That put
88 strikes back up 6 dB, so `NOISE_AMP` came down to 56 to pay for it; 4-8 kHz
reads 3.7, the original's exactly.

## The loop the owner now drives himself

The last commit on the branch is the owner editing
`tools/assets/music/castle-halls-drums.tab` and regenerating -- which is the
point of everything built this round. **Do not re-transcribe over his edits.**
His current part is sparser than the measured transcription; that is a choice,
not a mistake, and he answered explicitly earlier that the drums are the part he
wants to author rather than have scored against the recording.

Three commands, all from the repo root:

- `tools/assets/drums.bat` -- renders the tab through the offline model and
  plays it, about a second. Right for judging **which rows are struck**; wrong
  for judging how a strike sounds, because the model has neither the SCSP's
  envelope rates nor its interpolation.
- `tools/assets/drums-chip.bat` -- regenerates, builds a disc that boots
  straight into the tune, launches Mednafen. About thirty seconds against
  `compile.bat`'s two and a half minutes.
- `tools/assets/drums-emit.bat` -- just regenerates the pattern data. It is the
  exact command recorded in the header of `music_synth_data.c`.

The probe project those rest on is now `tools/scspprobe/` **in the repo**. Every
measurement across these sessions was made on a version of it that had to be
rebuilt from these notes each time; it persists now. `preview.py` honours the
tablature too, which it did not despite already accepting the arguments.

Tablature format lives in the header of the .tab file. Two things about it that
cost time: a beat is four slots **or** eight written out and the reader tells
them apart by length, so `sss.h...` and `sssh.` mean different things; and the
slashes are real beat boundaries, because a beat ending `hh` beside one starting
`h` is otherwise indistinguishable from a triplet.

## Traps, on top of the eleven already recorded

- **An unguarded `#define` in a header beats `-D` on the command line, silently.**
  This is the whole reason the previous session's trim was believed to work and
  the reason I first concluded it did nothing. `SCSP_NOISE_TRIM` was
  `#define SCSP_NOISE_TRIM 5` with no `#ifndef`, so a sweep passing
  `-DSCSP_NOISE_TRIM=13/21/29/37/255` compiled **five identical binaries**, which
  read as "the register is inert" -- a conclusion I wrote into a source comment
  before a stray 1.3 dB discrepancy between two builds that should have matched
  forced me to re-test it. Every tunable a sweep will override needs the
  `#ifndef` guard `SCSP_EG_PERC_D1R` now has. When a sweep returns *identical*
  numbers rather than merely close ones, suspect the build before the chip.
- **The previous note's reference row for the percussion is wrong at the top.**
  It has the original at 0.21 / 0.25 relative at 11k / 15k; measured here with a
  validated method it is **0.104 / 0.053**, and the 15 kHz figure is suppressed
  further by the MP3's own hard cut at 16 kHz. Our drum is not short of top end
  against the original -- it was *hot* from 3 to 8 kHz. Any rate sweep scored
  against the old row was scored against a target that was too bright.
- **Validate the isolation method on a signal whose answer you know.** White
  noise bursts on a tonal bed, through the same onset detector and the same
  pre-minus-post subtraction, read 0.95-1.04 flat across all eight bands and
  found 143 of 144 hits. Without that control I would not have trusted a
  measurement that disagreed with the note I was continuing.
- **The strike-energy-against-mix number stops working once strikes overlap**,
  because the "before" window fills with the previous strike's tail: across the
  D1R sweep it reads 0.69 / 2.22 / 2.63 / 1.19 / 0.26, which is not monotonic in
  anything. Use the whole-mix band profile for level. It needs no isolation.
- **Drop the 20-60 Hz band before scoring.** Neither machine puts anything there
  (the original 0.6 per cent, ours 0.0-0.1), so the log ratio of two numbers that
  are both nearly zero swamped the mean and made a 0.269 out of a 0.082.
- **Do not force-kill mednafen. It rewrites its config on the way out.** Every
  recording in these sessions ended in `Stop-Process -Force`, and one of them
  landed mid-write: `mednafen.cfg` came back **2,030 lines short**, truncated on
  a half-written setting name, and the emulator then refused to start at all --
  "Line 21690: Misformatted setting-value pair". The file is not in git and has
  per-project settings in it (`ss.cart none`, custom port-1 bindings), so there
  is nothing to restore from; the repair was to keep this project's intact lines
  and splice the missing tail off a sibling project's copy, which is line-aligned
  with it. `CloseMainWindow()` with a force fallback after five seconds avoids
  the whole thing, and the recording is unaffected because `-soundrecord`
  flushes as it goes. The record script now warns if the file comes back under
  23,719 lines.
- **The emulator's boot time is not constant.** Music started at 11.75 s on most
  runs of the same binary and at 9.50 and 9.75 on others, and every measurement
  here is taken at a fixed offset from the start of the recording. A ring-length
  sweep read 1.59 / 0.73 / 2.11 for settings that must be monotonic, and the 0.73
  was simply a recording that booted 2.25 s early. Check where the music starts in
  every file before comparing any two, and re-record until they agree rather than
  trying to correct the offset afterwards -- the strike measure is sensitive to
  alignment at the 30 ms level, and three different alignment methods gave three
  different answers for the same recording.
- **The 8-16 kHz band cannot see this engine's drum.** The shift register's own
  band ends at 7.8 kHz, so what is up there is the pulse voices' harmonics. A
  sweep of the snare's clock rate scored in 8-16 kHz moved 0.97 to 1.12 and looked
  useless; the same recordings scored in 2-8 kHz moved -53 Hz to +61 Hz and picked
  the answer cleanly. Score drum timbre where the drum is.
- **The per-strike long/short classification fails its own control.** Sorting the
  original's strikes by how much they sustain looks bimodal and looks like an
  open/closed hat pattern, and it is mostly the tonal voices: run the identical
  statistic on our own recording, where every strike is byte-identical, and it
  gives a signal-to-noise of 1.11 against the original's 1.27. Subtracting our
  sustain from the original's to cancel the bleed makes it worse, not better
  (1.07). What does work is comparing *classes* of row -- 16 ringing rows against
  136 ordinary ones -- where the noise averages down.
- **The CD build plays no music.** Fifty-four seconds of `run_with_mednafen.bat`
  is the BIOS chime and then silence; the tune is the netbin's. Everything here
  was measured on the probe project, which is the only way to hear it.

## The probe, rebuilt

The previous note is right that it has to be rebuilt every session. What worked,
in the scratchpad: a plain SRL project whose `src/` is a **copy** of
`saturn/src/sound/*` plus a `main.cxx` that calls `synth_target_init`,
`synth_set_level(7)`, `synth_start()` and then spins on `SRL::Core::Synchronize`;
`SRL_USE_SGL_SOUND_DRIVER = 0`; `SRL_CUSTOM_CCFLAGS += -DNETBIN -Isrc`; `SOURCES`
listed explicitly; a junction beside it pointing at `SaturnRingLib`; and
`make all` run from PowerShell with `Compiler\sh2eb-elf\bin`,
`Compiler\msys2\usr\bin` and `Compiler\Other Utilities` prepended to PATH --
without the msys2 entry `make` cannot find `rm` and dies in
`clean-preserve-audio` before it compiles anything.

Recording: `mednafen.exe -soundrecord OUT.wav CUE`, with **both paths quoted
inside a single argument string** -- `Start-Process -ArgumentList` splits on the
spaces in the disc name otherwise and Mednafen reports it cannot open a file
called `Zaturn`, which is the only symptom you get. Kill it after about 42 s;
music starts at 11.2 s and one statement is 23.6. The emulator is deterministic:
the same binary recorded twice gives the same RMS to five figures, which is what
made "identical" readable as a build failure rather than a null result.

## The six versions, and how to get back to any of them

The owner asked to judge the session's states side by side and thought one of the
earlier ones sounded better, so they were cut to MP3 -- one loop each -- and
handed over. In order, with what to change to return to each:

| | what it is | how to get back |
|---|---|---|
| 1 | the hat as a tick | `SCSP_EG_PERC_D1R` 24, `NOISE_AMP` 100, `CH_VOL[3]` 7, no accent |
| 2 | long hat, flat, no accent | D1R 17, amp 55, no `DRUM_UNACCENTED_DROP` |
| 3 | beat accent, one drum tone | D1R 17, amp 65, drop 1, `DRUM_DARK_SEMITONES` 0 |
| 4 | hat and snare split | as 3 plus dark 10 |
| 5 | tablature, 4/4, triplets | as 4 plus `--drums-tab`, `--tab-beats 4` |
| 6 | tablature, 3/4 -- shipped | as 5 with `--tab-beats 3` and the 3/4 tab |

**If the owner picks an earlier one, the thing to establish is which axis they
prefer**, because these differ on four at once -- strike length, level, accent
and drum density. The versions are not a ladder; 5 and 6 changed the *notes*
where 1 to 4 changed the *sound*, and a preference for an early one may be about
the fan sequence's busier drum part rather than about the voicing.

Recordings live in the session scratchpad and do not survive it. Rebuilding one
is a `sweep.ps1` run per row of that table, a couple of minutes each.

## Open

0. **HELD AT THE OWNER'S REQUEST: several tunes on the disc.** They have put
   sixteen Shadowgate MIDIs in `tools/assets/MIDI/` (overworld, title, throne
   room, dragon, lake, and more halls takes) and want them all, not one at a
   time. The engine plays exactly one song: `synth.c` calls `music_synth_song()`
   and there is no selector. What it needs is `mid2pat` emitting several songs
   into one file, a table, and something to pick -- room, area or menu. Cheap on
   space: about 2.2 KB of pattern data a tune against 185 KB of netbin headroom.
   The per-tune settings are the obstacle worth thinking about first, not the
   size: `CH_VOL`, `NOISE_AMP`, `DRUM_NOTE`, the darkenings and the percussion
   envelope are all module-level globals swept against *this* recording, so
   either every tune shares Shadowgate's balance or they become per-song fields.
   Note `sghalls.mid` and `sghalls2.mid` are both 21,621 bytes and are probably
   the same file; the owner said they would tidy the folder.
1. **The original's phrase-end breaths are not in the tune.** Measured above:
   17 stretches, 1.33 s, at bars 3, 6 and 12. Putting them in means keying the
   tonal voices off for a measured number of rows at measured places -- the row
   positions are in this note -- which is overriding the sequence from the
   recording, the same move `--bpm` and `--fold-octaves` already are. It is the
   largest remaining audible difference now that the drum is right, and it is
   what makes ours sound continuous where the original phrases.
2. **The top octave is still short and now measured**: the strike reads 0.074 at
   11 kHz and 0.006 at 15 against the original's 0.104 and 0.053. It is the shift
   register's own band edge -- the table is two samples a bit and at the note the
   drum is keyed at that is 15.6 kbit/s, so there is nothing above 7.8 kHz.
   Halving `--noise-oversample` doubles the bandwidth and was tried: 11 kHz goes
   to 0.342 and 15 kHz to 0.156, but 3-8 kHz overshoots badly and the band error
   goes 0.080 -> 0.116. **Rejected by measurement.** Do not retry it without a way
   to shape the mid band, which the chip does not have.
3. **D1R 17 decays slightly faster than the original** (0.66 against 0.85 at
   30 ms). 16 would be the next step; not tried, because 17 already matches
   inside what one recording of one performance settles.
4. Everything the previous note left open stands: hardware, licensing on
   `castle-halls.mid`, `test_music_pause.c` still not compiling since 659e630,
   and attribution owed on the Shadowgate sequence and the capoeira.

## Picking this up

The working tree is clean apart from three things that are not this work and
should be left: `saturn/run_with_mednafen.bat` carries the owner's own
uncommitted `MEDNAFEN_ALLOWMULTI` line, `out.wav` is an old leftover, and
`tools/assets/MIDI/` holds sixteen Shadowgate MIDIs the owner dropped there for
the multi-song work he has held (open item 0). He said he would tidy that folder;
do not tidy it for him.

The first thing to do is **ask what he thinks of the current drums**, not to
measure anything. The last four rounds were all his ear finding something the
measurements had missed or mis-weighted, and each time the fix was small once the
right thing was measured. He is editing the tablature himself now, so the loop no
longer needs me in the middle of it.

If he reports something wrong with the drums, the order that has worked is:
check the emitted data says what the tab says, then check the chip sounds what
the data says, then check the *level* of what sounded -- the last round's fault
was in the third of those and looked exactly like the first.

## Suggested skills

- `superpowers:systematic-debugging`, and by now it has earned it four times over.
  Every fault this session was found by measuring the output and none by reading
  the code, and three separate conclusions I had already written into source
  comments were reversed by a later measurement -- the TL register, the missing
  triplets, and the strike count. Write the measurement down before the fix.
- **Validate a measure against a case whose answer you know, every time.** This
  is the single highest-value habit from these sessions. An all-identical control
  killed a bimodality result; a known-flat white-noise burst validated the strike
  isolator; and the one measure that was never controlled -- single-pass onset
  detection -- is the one that produced two wrong conclusions and cost three
  rounds of the owner's time.
- `superpowers:requesting-code-review` before merge. Nineteen commits, none
  reviewed, touching the SCSP register layer, the converter's whole reduction,
  a new tablature language and both build targets.
- `superpowers:brainstorming` before starting the multi-song work in open item 0,
  which is a design question (per-song settings against one shared balance) with
  no measurable answer.

Related: [[nes-voicing-shadowgate-handoff]], [[sh2-synth-music-handoff]],
[[bash-heredoc-eats-backslashes]], [[zaturn-make-from-git-bash-drops-c-sources]].
