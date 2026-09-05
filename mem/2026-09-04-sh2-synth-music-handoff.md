---
name: sh2-synth-music-handoff
description: The netbin gained music generated on the SH-2 -- four SCSP slots driven directly with no sound driver, a V-blank tracker, and a MIDI-to-pattern converter -- now voiced as a Ricoh 2A03; nineteen commits on an unmerged branch, three real bugs caught only by recording the emulator, and nothing yet heard on hardware.
metadata:
  type: project
---

Branch `synth-music`, **nineteen commits, unmerged**, forked from `origin/main` at
`95e0802`. Only `saturn/run_with_mednafen.bat` is uncommitted, and that edit
predates this work. Nothing here has run on real hardware.

The spec and plan carry the design; do not restate them:

- `docs/superpowers/specs/2026-09-04-sh2-synth-music-design.md` -- includes a
  **Measured** section with the sizes and the emulator verification.
- `docs/superpowers/plans/2026-09-04-sh2-synth-music.md` -- the ten tasks, all done.

## What the sound is

The Saturn's sound chip is a **Yamaha YMF292 (SCSP)**: 32 PCM voices at 44.1 kHz,
16-bit, 512 KB of dedicated sound RAM, a 128-step DSP, per-voice LFOs. This build
uses four voices, 8-bit, 1 KB of that RAM, and no DSP -- so it is nowhere near the
chip's ceiling, and the owner has twice said so. Sound RAM is a separate 512 KB
that the netbin's 400 KB image ceiling does not touch; the remaining headroom in
the image is about 189 KB.

The synth writes SCSP slots 28-31 directly. **No SGL sound driver is loaded** --
that was the whole size argument, and it means the 52 KB figure in early notes no
longer applies to anything, including a recorded-sample route.

Voices are the **Ricoh 2A03's**: pulse at 12.5 / 25 / 50 per cent duty and the
NES's 32-step, 16-level triangle staircase, generated offline by
`tools/assets/genwaves.py`. `--voice smooth` swaps in band-limited equivalents in
the same four slots. The trade is real and unresolved: NES waves are hard-edged on
purpose and alias when pitched high, which is the same physics behind the buzz the
owner rejected earlier.

## The three bugs that only recording caught

Each was invisible to every test and to the register read-backs, and each was
found by recording Mednafen with `-soundrecord` and analysing the WAV. Keep this
loop; it is the only thing that has caught a real fault here.

1. **Total silence.** Nothing raised the SCSP master volume `MVOL`, and with no
   sound driver nothing else does. Every slot register was correct and the machine
   was mute. Fixed by `scsp_enable_output()`, netbin only -- `SND_Init` already
   does it in the CD build.
2. **A dental drill.** The drum voice had the sustained envelope and the pattern
   data never keys a drum off, so the first hit latched the noise generator on
   permanently. Fixed with a percussive envelope. Measured as spectral flatness:
   0.60-0.72 (broadband) before, 0.03-0.30 (tonal) after.
3. **Two tunes at once, and a poisoned cache.** The synth started at boot, under
   the title's PCM jingle. Worse, the gate called `music_cdda_audio_tracks()`,
   which **caches its answer in a static on first call** (`music_cdda.cxx:366`) --
   asking before the drive's TOC was readable could freeze "no CD audio" for the
   session, starting the synth over a disc that has CD-DA and swapping the Sound
   page's CD row for the synth row. Both builds now ask at the menu.

## Tools, and how to hear a change

- `tools/assets/preview.bat|.sh IN.mid OUT.wav [--seconds N] [--grid 8]` renders a
  MIDI through a software model of the synth in about a second. It imports
  `mid2pat` so the conversion is the same one the build uses. The wrappers resolve
  their own location; the bare `preview.py` must be run from the repo root, which
  has already tripped the owner once.
- It is a model, not the chip: no envelope rates, no interpolation, no output
  filtering. It will not warn about aliasing at extreme pitches.
- `tools/assets/mid2pat.py IN.mid saturn/src/sound/music_synth_data.c --name ...
  --source ...` regenerates the tune. It writes the `.h` too, because the `.c`
  asserts its own length against the counts there.
- The real check is `mednafen -soundrecord out.wav <cue>` over a throwaway SRL
  project that links the shipped modules. One is in this session's scratchpad at
  `scspprobe/`; it needs a junction named `sdk` pointing at `SaturnRingLib`
  recreating (`mklink /J`), because the SDK path must stay short and relative.

## Traps that cost time here

- **Heredocs eat backslashes.** Two multi-line `python - <<'PY'` blocks with
  backslash continuations failed as syntax errors. Write the script to a file and
  run it by path, or use the edit tool. This is already recorded in auto-memory.
- **`compile-netbin.bat clean` and the build chained in one PowerShell pipeline
  race**, and reported 196,032 bytes while the artifact on disk was 215,344. Run
  clean, then build, then measure the file separately.
- **`BuildDrop` is shared between both targets.** Netbin builds overwrite the CD
  image's `.elf/.iso/.bin/.cue`, which is also why `test_hwram_budget`'s two checks
  flip between pass and fail -- they read `BuildDrop/<CD_NAME>.elf`. `compile.bat`
  restores both.
- **`syntax-check.sh` cannot check `src/main_netbin.cxx`** -- it passes no
  `-DNETBIN`, so `menu_set_service` resolves out of scope. Pre-existing; verified
  by stashing. The netbin build is that file's only gate.

## Open, in the order they matter

1. **Nothing has run on hardware.** Every result is Mednafen. The silence bug
   proves the emulator catches real faults; it cannot prove the reverse.
2. **The owner is not happy with the sound.** "Sounds like shit... computer speaker
   noises" was about the pre-NES voices, and the NES rebuild has not been judged
   yet. The next cheapest gain is **envelopes on the pulse channels** -- NES notes
   decay rather than holding flat, and the SCSP's envelope generator already does
   this for the drums. After that: real sampled instruments in sound RAM, which is
   what the chip is actually for and is 512 KB empty.
3. **A converted MIDI keeps its original arrangement.** NES music sounds like NES
   partly because it was written for two pulse channels -- arpeggios instead of
   held chords, echo lines. No converter can invent that.
4. **The CD-build fallback has never been heard.** It only plays on a disc with no
   CD-DA, and this project's disc has 31 audio tracks. It also takes the one path
   that does not call `scsp_enable_output()`, trusting the driver to have raised
   `MVOL`.
5. **Attribution owed.** `revenant-capoeira.mid` came with "credit both me and
   Gustavo6046"; the second name is unknown and the file carries no metadata. Ask,
   then re-run the converter -- the credit lives in the `--source` string. The CD
   build has a Credits menu entry if it should be player-visible.
6. **`castle-halls.mid` is a fan sequence of Shadowgate**, copyrighted 1987 game
   music, unlike the Public Domain Mutopia material. Fine for testing, not settled
   for release.

The shipped tune is currently Grieg's *Ase's Death* (Public Domain, Mutopia). The
capoeira and Shadowgate MIDIs are in `tools/assets/music/` and are one converter
run away from being swapped in.

## Suggested skills

- `superpowers:systematic-debugging` for anything about how it sounds. Every fault
  here looked like a code question and turned out to be answerable only by
  measuring the output; three were found that way and none by reading.
- `superpowers:test-driven-development` if the pulse envelopes get built -- the
  drum envelope landed with a test pinning "a drum decays, a note sustains", which
  is the assertion that would have caught the drill.
- `superpowers:brainstorming` before choosing between sampled instruments and more
  synthesis. That is an architecture decision with a real size and download cost,
  and the owner has already reversed direction once on it.
- `superpowers:requesting-code-review` before merge. This branch has had no
  independent review: the owner declined the reviewer subagents, so every review
  was mine, and my own blind spots are unaudited.

Related: [[map-paging-session-handoff]] for the branch this forked behind.
