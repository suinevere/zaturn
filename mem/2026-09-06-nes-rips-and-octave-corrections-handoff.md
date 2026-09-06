---
name: nes-rips-and-octave-corrections-handoff
description: The Shadowgate NES soundtrack arrived as fourteen rips, so every tune in the catalogue could finally be matched to its own original by measurement rather than by filename -- which renamed three, found one piece sequenced twice and one MIDI that matches nothing -- and then turned on the voicing itself, where every constant this synth ships had been swept against the entryway theme alone: seven tunes agree with their original, four do not, and three of those were a fan sequence writing one voice an octave out.
metadata:
  type: project
---

Branch `synth-music`, fifteen commits ahead of `origin/main` at `d44614d`
(`d66e5d8` the matching and the octave corrections, `2e8902f` the re-measured
mood map, `8978fb8` the part separation, `3c6fa85` the release envelope and
lake's third voice, `027ed33` per-tune levels, `e65bfa6` the held notes,
`4734f5c` the wrapping list, the rest this note). The owner's
unstaged `MEDNAFEN_ALLOWMULTI=1` in `saturn/run_with_mednafen.bat`, their
`tools/scspprobe/src/probe_song.h` pointing at `lake` -- which is how they heard
it -- and the untracked `out.wav` are all theirs and untouched.

688 host tests pass and 3 skip, and all seven C sound tests pass built both
with and without `-DNETBIN` -- those are built by hand and have no runner, which
is how one of them went a whole session asserting a page of zeros. The one
failure is the same pre-existing
`test_lwram_budget.py::test_every_frame_lies_inside_its_archive` -- Lurking's
eight areas have no `.CGL` staged -- which is art-v2's and was failing before
this session as it was before the last two.

The three skips are the netbin budget tests, which want a `zaturn.netbin` in
`BuildDrop` and there is none. **The heap budgets do now measure a real link
map**: the owner built a disc part way through, and they pass. The netbin's
image size is still only the generator's own arithmetic -- the tune data is
21,072 bytes against 21,262 when the session started, so the direction is safe,
but that is arithmetic and not a link map.

Continues [[lwram-sound-and-image-trim-handoff]], which is still the current
note for the hardware fault and its four wrong theories, for the LWRAM sound
route and for both budget floors. Nothing here changes any of that. Supersedes
[[synth-song-catalogue-handoff]] on the tunes' **names** and on its open item 3
(the three unmatched tunes), and nothing else in it.

## What arrived, and what it made possible

Fourteen mp3s of the NES soundtrack, in `Downloads\Shadowgate Soundtrack`. They
are not in the repository and will not be -- the same bargain `gen_track_mood.py`
makes with Activision's audio. Two tools read whatever they are pointed at and
commit only derived numbers:

    python tools\match_nes_rips.py --dir "...\Shadowgate Soundtrack"
    python tools\assets\voicecmp.py --dir "...\Shadowgate Soundtrack" [--sweep]

The first writes `tools/assets/music/nes_refs.json`. The second writes nothing
and prints a table.

## Which tune is which, and three of the names were wrong

Matched by chroma correlation over every transposition, twenty-five tempo
ratios and every time offset, each lag normalised by the energy actually
overlapping at that lag. That last part matters: normalising by both whole
norms instead buried three matches, because a MIDI covering half a rip scored
half of what it deserved.

| rip | tune | score | runner-up |
|---|---|---|---|
| Courtyard | court | 0.859 | 0.242 |
| Entryway Main Theme | castle-halls | 0.701 | 0.357 |
| Game Over | **overworld** | 0.677 | 0.283 |
| Dragon's Den | dragon | 0.663 | 0.327 |
| Hall of Mirrors | **halls** | 0.633 | 0.231 |
| Twilight | **corridor** | 0.495 | 0.395 |
| Banquet Hall | banquet | 0.476 | 0.284 |
| Subterranean Cavern | shadow7 | 0.468 | 0.334 |
| Danger | shadow8 | 0.376 | 0.259 |
| Title Screen | title | 0.365 | 0.322 |
| Subterranean Cavern | lake | 0.365 | 0.316 |

**`sgover.mid` is Game Over, not an overworld.** "sgover" is the filename and
the manifest read a title off it. It is the second largest tune in the image at
5,206 bytes and it is a death jingle, which is worth knowing before deciding
what the netbin plays behind a menu. `sghalls.mid` is Hall of Mirrors and
`sglitrod.mid` is Twilight; both were named off their filenames too.

**`sglake.mid` and `shadow7.mid` are the same piece.** 0.728 against each other,
both claiming Subterranean Cavern, and their note counts and lengths agree to
within four per cent. The catalogue carries one tune in two slots for 6,871
bytes of the netbin image, and the mood matcher's complaint that "several tunes
measure almost identically" was partly this. **Which of the two to drop is a
listening call and neither has been dropped.** `shadow7` is the better sequence
by every measurement here.

**`sgmirror.mid` matches nothing.** Not at any tempo from 0.4x to 2.6x, against
any of the fourteen. Its name is now `Shadowgate, sgmirror (unidentified)`
rather than `Shadowgate, Mirror`, which claimed a piece `sghalls.mid` turned out
to be. It converts and plays; nobody knows what it is.

**Four rips no tune claims**: Battle to the Death, Hero of Stormhaven, Unused
Track, and the Deja Vu flute. The first three are Shadowgate tracks with no
sequence in the catalogue; the flute is 5 seconds long and matches everything
weakly for that reason, which is what `MINOVER` is in the matcher for.

## The voicing, measured against ten originals instead of one

`CH_WAVE_TONAL`, `CH_VOL`, `DRUM_UNACCENTED_DROP`, `DRUM_DARK_SEMITONES`,
`DRUM_NOTE`, `NOISE_AMP`: every one of them measured, and every one of them
measured against the entryway theme. There was no way to find out whether that
generalised until the rest of the soundtrack was to hand.

`voicecmp.py` renders each tune through the offline model and compares its share
of energy per octave band against its own rip's, as an L1 over ten bands. The
bands run **20 Hz to Nyquist with no gaps, deliberately** -- the first sweep run
here used 55 Hz to 14 kHz and cheerfully proposed dropping a bass two octaves,
because energy that leaves the measured range looks like energy that went away.

Where it stands after this session's corrections:

| tune | L1 | |
|---|---|---|
| lake | 0.129 | was 0.993 |
| title | 0.130 | |
| corridor | 0.151 | |
| castle-halls | 0.160 | the one everything was tuned on |
| dragon | 0.198 | |
| shadow7 | 0.236 | scored 0.104 with an octave correction on its echo -- see below |
| court | 0.287 | **was 0.719**, and still on the pitch-order fallback |
| overworld | 0.636 | |
| halls | 0.651 | |
| shadow8 | 0.926 | |
| banquet | 0.943 | |

Read that table with the sections below beside it. **A low number here does not
mean a tune is right, and a higher one does not mean it is wrong.** Two proofs,
both heard on the chip: `lake` scored 0.194 while playing its melody on a
triangle so far above the table's range that the waveform was gone, and
`shadow7` scored 0.104 with an octave correction that had turned its lead's echo
into a parallel line, against 0.236 without. Both times the measurement preferred
the version that sounded wrong. It is an instrument for level and timbre, and it
cannot see which voice is playing what.

**Seven tunes agree with their own original.** The voicing generalises, and that
is the headline: the entryway theme was not a special case and none of those six
constants needs re-opening on this evidence.

### The three that were an octave out

`songs.json` grew an `octaves` field -- a whole-octave correction per tonal
lane, `[bass, lead, harmony]` -- and three tunes carry one:

* `lake` `[1]`. Its bass sat 42% of all energy in 55-110 Hz where the original
  puts 0.1%. Independently confirmed: raising it an octave puts its pitch range
  on `shadow7`'s exactly, and those two are the same piece.
* `court` `[1]`. Same fault, 0.102 and 0.269 in the two lowest bands against
  0.003 and 0.078.
* ~~`shadow7` `[0, 0, -1]`~~ **WITHDRAWN.** That lane is not a harmony, it is
  the lead's echo -- delayed twelve rows at pitch offset zero. See below.

Each was accepted only because the **whole profile converges band by band**, not
because the number fell. Four other proposals from the same sweep were rejected
for trading one band's error for another's -- `dragon` `[0,1]` moves 440-880 Hz
from 0.113 to 0.041 against the original's 0.087, which is worse, and scores
better. **`--sweep` prints a proposal and writes nothing, and that is why.**

The percussion lane is not swept and must not be: its note is the shift
register's clock rate, calibrated at `DRUM_NOTE` against these same recordings.

### The four that were still wrong, and are not octaves

**Superseded by the section below**, which found their common cause: all four
are reduced by pitch order rather than one voice per part, and `banquet` is
fixed by the separation that came out of it. What follows was written before
that and is kept because the rejected proposals still stand.

`banquet` 0.943, `shadow8` 0.925, `halls` 0.646, `overworld` 0.636. An octave
move helps each of them and none of them converges -- `banquet`'s best proposal
takes 110-220 Hz from 0.290 to 0.577 against the original's 0.720 but overshoots
880-1k7 from 0.084 to 0.151 against 0.058. Something other than register is
different about these four arrangements and it has not been found. They are the
worklist.

### Two things learned about the manifest on the way

**`fold` is a no-op on eleven of the twelve tunes.** All three modes -- `off`,
`up`, `down` -- score identically on every tune except `castle-halls`, where
`up` is worth 0.842 to 0.138. No other sequence has an exact octave doubling
inside one MIDI channel. The eleven `"fold": "up"` entries are copy-paste and
imply a per-tune decision that is not being made.

**Four callers spelled `convert`'s eight arguments out**: the emitter, the
preview, the mood measurement and the tests. Adding a ninth would have reached
whichever of them was remembered -- which is the same shape as the rot in
`drums-chip.bat` last session. They all go through `mid2pat.convert_song(record)`
now.

## What to run

    python tools\match_nes_rips.py --dir "...\Shadowgate Soundtrack"
    python tools\match_nes_rips.py --dir ... --report      the whole matrix
    python tools\assets\voicecmp.py --dir ...              the table above
    python tools\assets\voicecmp.py --dir ... --sweep      + octave proposals
    python tools\assets\voicecmp.py --dir ... --song banquet --sweep
    tools\assets\songs.bat lake                            hear one, a second
    python tools\gen_synth_moods.py --report               the mood map

After any change to a tune, regenerate both -- **the moods FIRST**:

    python tools\gen_synth_moods.py
    python tools\assets\mid2pat.py --manifest tools\assets\music\songs.json ^
           --out saturn\src\sound\music_synth_data.c --pat saturn\cd\data\BG\MUSIC.PAT

Neither is optional and the order is not free. The mood map is measured off the
renders, so changing how a tune renders invalidates it -- doing it this session
moved tracks 15, 17 and 25 -- and `mid2pat` bakes `track_songs.json` into
`MUSIC_TRACK_SONG` in the generated C, so running it first ships the *previous*
map. This note carried the order the wrong way round for most of the session and
the committed table was one revision stale until a dirty working tree caught it.

## The owner listened to lake, and all three reports were one fault

Reported after `drums-chip.bat lake` -- so from the chip, not the model:
"needs rounding", "seems harsh square", "one high note getting squashed".

`plan_parts` follows one source part per voice and **gives up entirely the
moment any part sounds two notes at once**. The fallback then re-picks which
voice plays which note on every row, by pitch order -- which is the failure
`plan_parts`' own docstring describes, applied to seven of the twelve tunes.
Those seven are exactly the seven that disagreed with their own original, and
the five that planned are exactly the five that agreed. The split is perfect.

In `lake` it has a number. Its MIDI channel 0 carries a melody **and** a bass
two to three octaves apart on one channel, so the row's lowest note -- what the
fallback sends to the triangle -- is the bass while the bass plays and the
melody while it rests. The melody reached **1228 Hz on the NES triangle**, whose
32-step staircase lives in a 256-sample table: at that pitch the table is read
7.13 samples at a time, leaving **1.1 samples a stair**. There is no staircase.
What is left is aliasing, and an aliased triangle is buzzy, square-ish and
clipped -- which is all three words the owner used.

**The octave correction this session added made it audible.** It moved that lane
up an octave, from 614 Hz and 2.2 samples a stair to 1228 and 1.1. The
correction was right about the octave and wrong about the lane, because that
lane was not a line.

Fixed by separating such a part at the midpoint of its two registers and
planning again. Two guards, both of which cost a run to find:

* **Only where the plan already failed.** Applied unconditionally it separates
  `castle-halls` -- the tune every voicing constant was swept against -- and
  takes its triangle from eleven semitones to forty-six.
* **Only after `fold_octaves`.** A raw sequence doubling its bass at the octave
  is indistinguishable from two lines on one channel until the fold removes it,
  which is exactly how `castle-halls` gets caught.

It fires on `lake`, `mirror` and `banquet` and leaves the other nine
byte-identical. `lake`'s triangle goes from 19..58 to **19..30 -- exactly the
eleven semitones `shadow7` puts it on**, which is the same piece sequenced by
someone whose channels were separate to begin with. That agreement, not the band
measurement, is what confirms both the separation and the octave correction.

**The band measurement got worse: 0.194 to 0.268.** It prefers the broken
version, because a melody smeared across the spectrum by aliasing happens to fill
the bands the original fills, and nothing in an energy-per-octave profile can see
which voice is playing a line. `voicecmp.py` is still the right instrument for
level and timbre and is not an instrument for this; the triangle-range check in
`test_part_planning.py` is.

Four tunes still cannot be planned, for two other reasons, and their triangles
still top out at 868 to 1158 Hz -- 0.8 to 1.3 samples a stair, all of them past
the point where the staircase is gone:

| tune | why it cannot be planned |
|---|---|
| court | 5 source parts for 4 voices |
| overworld | 4 source parts for 3 voices |
| halls | channel 6 sounds a chord, members ~10 semitones apart |
| shadow8 | channel 1 sounds a chord, members 5-6 semitones apart |

A chord part is one line thickened and reduces to its top note; more parts than
voices means dropping the least active. Neither is written.

**`court`'s octave correction is now suspect and was deliberately left alone.**
It was fitted to the band profile of a lane that is not a line, and it doubles
the aliasing on that tune's worst note -- index 64 at 1736 Hz and 0.8 samples a
stair, against 52 at 868 Hz and 1.6 without it. Withdrawing it would be a second
guess fitted to the same discredited evidence; fixing court's part planning
settles it properly.

One thing looked at and left: `lake`'s lead is now doubled in unison on a 50%
and a 25% pulse. That is not a regression -- `corridor` does the same on 100% of
its rows and is one of the best-measuring tunes, and two pulse channels doubling
a lead is what an NES does. `echo_delay` never detects it because it searches
delays 1 to 16 and a unison duplicate is delay 0, so the two never come to share
a duty. If the harshness outlives the triangle fix, that is where to look.

## The treble, three more reports, and only one was the timbre

After the fix above, from the chip again: "treble note needs to be more flute
like, seems too high pitch, very sharp release not rounded or soft release."

**The table is right and it is not a flute.** Measured the way the entryway
theme's duty was measured -- strongest peak above 380 Hz as the fundamental,
energy at 2f..7f against it:

| | h2 | h3 | h4 | h5 | h6 | h7 |
|---|---|---|---|---|---|---|
| Subterranean Cavern | 0.031 | 0.305 | 0.018 | 0.207 | 0.019 | 0.144 |
| 50% square | 0.001 | 0.333 | 0.002 | 0.198 | 0.005 | 0.146 |
| NES triangle | 0.002 | 0.109 | 0.002 | 0.037 | 0.002 | 0.018 |
| the soundtrack's own **Flute** | 0.089 | 0.247 | 0.056 | 0.152 | 0.039 | 0.120 |

lake's lead is a 50% square to three decimals, and so is the flute cue -- odd
harmonics, nothing like a triangle. **On this machine a flute is a pulse with a
different envelope**, which is the answer to "more flute like" and it is the
same answer as the third report.

**The release was the chip's maximum.** `SCSP_EG_SUSTAINED` was `0x001F`: bits
4-0 of register 0x0A are RR, and 31 is the fastest rate the field has. Every
pitched note has stopped rather than ended for as long as this synth has
existed. It is now `SCSP_EG_SUSTAINED_RR`, a named dial at 7.

**Why it was never caught: the model reproduced it.** `preview.render` did
`amp[ch] = 0.0` on key-off -- instant, no release at all -- so the preview
agreed with the chip about a fault they both had. It models the release now,
which is what makes the dial turnable in a second through `songs.bat` instead of
a thirty-second disc. The rate lives in two places, a C header and a Python
module, because a host cannot read the header;
`saturn/tests/test_release_envelope.py` fails if they part.

**The time is extrapolated, not measured.** `scsp.h` records that the envelope
scale is geometric at about four steps to a factor of two, and the percussion
decay is the only rate this project has ever measured -- 17, a half-life of
about 4 ms. On that scale 7 is about 23 ms, inaudible some 90 ms after the note
is let go. **Nobody has heard it.** Raise toward 31 for a shorter tail, lower for
longer, and do not go below 4: the bottom of a Yamaha rate field is where "no
change" lives and a voice that never releases never stops. A slow release costs
nothing on a held line -- the voices are monophonic and the next key-on restarts
the envelope -- so it is heard only at rests, which is where a release is heard.

The attack is still `AR` 31, instant, and was deliberately left: it changes the
articulation of every note rather than only the ends, and one dial at a time.

**"Too high pitch" was the third voice in unison with the second.** After the
separation, lake's lane 2 sat at 32..49 -- the same semitones as its lead --
where `shadow7` puts the same line at 20..37, an octave below. `lake` takes
`octaves [1, 0, -1]` and now agrees with shadow7 **voice for voice**: 19..30,
32..49, 20..37 in both. `test_part_planning.py` pins all three lanes now, not
just the bass. That also dissolves the unison doubling flagged in the section
above, so `echo_delay`'s blind spot at delay 0 is no longer reachable from lake
-- it is still there, and `corridor` still runs into it.

**One thing found while checking the C tests, which are not in the pytest run
and which nobody had built since the image trim.** `test_synth_note` has been
asserting that the waveform tables are a page of zeros: they moved from
`.rodata` to `.bss` built at boot last session -- the 5,120-byte trim -- and the
test reads them without calling `synth_waves_build()`. Repaired. All seven C
sound tests now pass, built both with and without `-DNETBIN`. **They are built
by hand and no runner exists**, which is how this went unnoticed; the command is
in the plan doc at `docs/superpowers/plans/2026-09-04-sh2-synth-music.md`.

## "Flute too loud compared to other notes"

Measured, and it is not what the release did. Lake's lead carried **0.260 of the
tune's energy in the 440-880 Hz octave where the NES original carries 0.178**,
and 0.030 against 0.018 in the octave above it, with the bass band starved to
match -- 0.376 where the original has 0.483.

One DISDL step down on the lead:

| band | 110-220 | 220-440 | 440-880 | 880-1k7 | 1k7-3k5 | L1 |
|---|---|---|---|---|---|---|
| NES | 0.483 | 0.263 | 0.178 | 0.018 | 0.026 | |
| lead at 5 | 0.376 | 0.264 | 0.260 | 0.030 | 0.041 | 0.224 |
| lead at 4 | 0.539 | 0.271 | 0.131 | 0.018 | 0.022 | **0.132** |

Every band moves toward the original and 880-1k7 lands on it exactly. The true
answer is between the two -- 110-220 overshoots and 440-880 undershoots -- which
is the 6 dB granularity of a three-bit field, and the only per-voice dial the
chip has. The finer trim this project already uses for the drum is the waveform
table's own amplitude, and that cannot be it: the tables are shared by every
tune and uploaded once.

**Not taken globally.** Swept across all eleven tunes with a recording, dropping
the lead a step everywhere costs `castle-halls` 0.16 to 0.54, `corridor` 0.15 to
0.53, `title` 0.13 to 0.29 and `shadow7` 0.11 to 0.26, for a mean that goes
0.401 to 0.487. CH_VOL's global answer is right and lake is the exception to it:
a sequence that holds its lead longer is a louder lead at the same level, which
belongs to the sequence and not to the voicing. So `levels` joins `octaves` as a
per-tune field, exactly one tune carries one, and the other ten are
byte-identical.

lake is now 0.132 -- third best in the catalogue, from worst-but-three.

**The release was ruled out rather than assumed.** It was the newest change and
the obvious suspect. Swept from the chip's fastest rate to 5, it moves lake by
0.015 and the catalogue mean not at all; it very slightly *improves* lake. The
balance was already like this.

## Two faults in the Sound page itself

**Old notes held across a track change.** Reported as "when switching tracks in
sound menu, old notes hold until new note of new song plays", and it reads
straight off the code. The pitched envelope decays at zero -- a held note has to
sustain until the tracker keys it off -- and `tracker_stop()` sets `g_playing = 0`
and nothing else. So every voice sounding at the moment of a switch stayed keyed
until the incoming tune happened to write that same voice, which for a voice the
new tune rests on is never.

`synth_stop()` and `synth_pause()` each carried their own copy of the loop that
releases the voices. `synth_start_song()` carried none. Three copies, one
missing -- there is one `release_voices()` now and all three call it.
`test_synth_select.c` fails without the fix.

Note the interaction with the release rate: a switch used to be a click, and now
that RR is 7 rather than 31 it is a short fade. Both were wrong before; one made
the other visible.

**The track list did not wrap.** Every menu's rows already come round with a
modulo; the list of tunes and tracks was the one thing on that page that stopped
dead at each end. `menu_list_step` in `menu_layout.h`, used by both Sound pages
-- the disc's and the browser's are separate builds and each had its own copy of
the arithmetic, which is the same shape as the bug above.

The level rows still clamp on purpose. A volume is a scale rather than a list,
and 7 stepping to 0 off one right press is a surprise nobody asked for.

## A silence between tunes, an echo put back, and lake's snare

**Releasing the voices was only half of the track change.** Reported again:
"still hearing notes from the previous track selecting new track, okay if
there's a silence between the two." A release takes time on purpose -- 23 ms to
half power -- and `tracker_start` sets its countdown to zero, so the incoming
tune's first row lands on the very next tick, on top of the outgoing fade.
`tracker_hold(SYNTH_SWITCH_SILENCE)` now holds the sequencer quiet for twelve
ticks, 200 ms, comfortably past the fade and audible as the gap that was asked
for. It costs the same 200 ms at boot and on a room whose mood moves, which is
not a fault. Two tests in `test_synth_api.c` ticked exactly once after a start
and had to learn to tick past it.

**shadow7's third voice is an echo, and an octave correction had been put on
it.** Measured: its part 1 is part 0 delayed twelve rows at pitch offset **zero**
on 713 rows -- which is what `echo_delay` exists to find, and what the NES does
with its second pulse channel. An echo is the same line heard again and belongs
at the same pitch. The band measurement scored 0.105 with the correction and
0.236 without, **preferred the wrong one for the third time**, and the owner
heard it as "an odd third instrument repeating main track". Withdrawn, and
`test_part_planning.py` now refuses an octave correction on any lane the echo
detection has linked.

That is the third time the octave-band profile has been wrong about a lane
question -- lake's aliased triangle, court's, and now this. **It is an
instrument for level and timbre and not for which voice plays what.**

The lake/shadow7 voice-for-voice test came off with it, back to the bass claim
the separation actually proved. Their third voices legitimately differ: shadow7
writes the lead's echo at twelve rows' delay, lake writes an exact unison
duplicate at no delay. Two sequencers, one piece, two ideas about the same part
-- and it was the test demanding they agree, not the measurement, that kept the
correction on shadow7 until someone listened.

**`lake` has a snare now**, and neither the meter nor the figure is a guess.
Carrying every voice's sounding pitch forward and asking what row offset the
tune repeats at: 32 rows -- four beats, 1.33 s -- self-matches 0.454 against
0.352 for the next candidate, so it is 4/4 and 1152 rows is 36 bars. Its
strongest *phrase* is 192 rows: six bars, 8.0 s, 0.209 against 0.171. Two snares
a bar across that phrase is **exactly the twelve strikes the owner counted**
("12/9s repeat"), looping six times over the tune. `music/lake-drums.tab`, one
line, `x36`, and the tab is the file to edit if it is the wrong two of the four
beats.

Note `tab_hits` does not loop a tablature: past its end there are no strikes, so
the repeat count has to cover `max_rows`.

**The trills are not closed.** "the trills for the held notes are either too
quick/they don't sound right". What is measured: lake's cells contain **no trill
figure at all** -- no A-B-A inside six rows on any lane -- so it is not being
introduced by the conversion. The source has 102 onsets on channel 0 arriving
under three rows apart, but **92 of them straddle the register split**: they are
the bass and the melody, written on one channel, landing two rows apart. Those
the separation now handles correctly. That leaves **ten genuine fast figures
inside the melody, at two rows -- 83 ms, the finest a 32nd grid at this tempo
offers**, which is the source's own timing and not a quantisation artifact.
Whether 83 ms is too quick needs the original, and isolating an ornament from a
four-voice mix is not something the band measurement can do.

## Open

Carried forward from [[lwram-sound-and-image-trim-handoff]], unchanged and still
the priority list:

1. **None of Lurking Horror's fourteen sound effects has ever been played.** The
   whole LWRAM route is host-tested arithmetic. The music works on hardware now,
   so the PCM path is the next thing that should be heard.
2. **The synth level is multiplied into a logarithmic field.** `synth_note_on`
   passes `(vol * g_level) / 7` into DISDL, three bits at 6 dB a step, so the
   default level of 5 puts every voice about 12 dB down. The fix is MVOL, which
   is global and would duck CD-DA and the effects on the CD build. **Still a
   decision and not an implementation** -- and note that everything in this
   session's table was measured through the offline model, which does not model
   that field, so none of it is affected either way.
3. **Sweep the SCSP slots with a PCM effect sounding**, not merely with the
   driver loaded.
4. The item-pane exclusion is load-bearing for the sound budget.

New here:

5. **`lake` or `shadow7` should go.** Same piece, 6,871 bytes, and one of them is
   a worse sequence by measurement. Listening decides.
6. **`sgmirror.mid` is unidentified** and ships anyway, 2,899 bytes, credited as
   unidentified. It may be a Shadowgate track absent from this rip set, or not
   Shadowgate at all.
7. **Four tunes still cannot be planned one voice per part** -- court and
   overworld have more parts than voices, halls and shadow8 have a part
   sounding a chord. All four play their melody on the triangle above the pitch
   its staircase survives. This is the next fix and it is two fixes, not one.
8. **Three Shadowgate tracks have no sequence in the catalogue**: Battle to the
   Death, Hero of Stormhaven, Unused Track. Whether that matters depends on
   whether the catalogue is meant to be complete.
9. **The 48-second cuts are still blind**, still at a round number of seconds
   rather than at a musical boundary, and still nobody has listened to one. This
   session did not touch them. `songs.bat <id>` is a second per check.
10. **Nothing here has been heard.** Every number in this note is the offline
    model against an mp3. The model does not reproduce the SCSP's envelopes, its
    interpolation or its output filtering, and it is not the chip -- see
    `preview.py`'s own header. `drums-chip.bat <id>` is thirty seconds and is
    the fastest way to put one of these on real hardware.

## Suggested skills

* **`superpowers:verification-before-completion`** -- for the same reason the
  last note gave it. Nothing in this session has been listened to; every claim
  here is a measurement of a model, and the project has now been taught twice
  that a model agreeing with you is not evidence about the machine.
* **`superpowers:brainstorming`** -- before open items 5 and 6. Which of two
  sequences of one piece to keep, and whether to ship a tune nobody can name,
  are taste calls with the owner's ear in the loop.
