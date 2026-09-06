---
name: lwram-sound-and-image-trim-handoff
description: The netbin's music was silent on real hardware because sound RAM was written a byte at a time behind the SCSP's sixteen-bit B-bus, which no emulator can fail; also Lurking Horror's fourteen sound effects now fit, in Low Work RAM, and both budget floors are clear -- one by taking 57 KB of build scratch out of .bss, one by building the synth's waveform tables on the machine.
metadata:
  type: project
---

Branch `synth-music`. Three squashed commits went out as `v0.0.9` (`9bf95a1`,
`ea31947`, `4a71363`), and the B-bus fix and this note followed them. The working
tree still carries the owner's own unstaged `MEDNAFEN_ALLOWMULTI=1` in
`saturn/run_with_mednafen.bat` and an untracked `out.wav` from 4 September;
neither is mine.

**Read the sound RAM section first.** It was found last and it is the one that
matters; the two sections before it describe work that is good and finished but
was never what was silencing anything.

Supersedes [[lurking-sound-budget-handoff]] on the route and every number in it.
That note's diagnosis was right and its arithmetic was right when written; what
follows changed both. Its account of what BOOTSND.MAP will not yield still
stands, and so does its warning about building an SCSP consumer on a slot guess.

## The route the owner picked, and what it turned into

Given the probe unrun, the owner chose **Low Work RAM** over sound RAM, with the
sound-RAM engine still the goal. That is what shipped.

`load_slice` allocates from `LowWorkRam` behind a gate checked before the
allocation rather than a NULL after it, and checks the long alignment
`LoadBytes` requires -- which the High Work RAM version never did, and which
only worked because that allocator happened to return aligned blocks. The gate
holds back `SLICE_KEEP_FREE`, the save scratch plus the 4 KB every other gate in
the client asks for. The area archive is deliberately not reserved: they run
16 KB (GENA) to 418 KB (BCEL), and no fixed reserve is both large enough for the
biggest and small enough to leave the samples anywhere. It is handled the other
way instead -- `sound_release_cache`, which `load_area` calls before refusing an
archive. The trade runs one way only, and the reason is in that function's box.

One real leak came out with it: `cached_slice` used to return a buffer that
found no free cache entry, and the calling slot set its own `buf` to `nullptr`,
so nobody freed it. Unreachable while the heap fitted at most two slices.

`saturn/tests/test_sound_slice_budget.py` measures `LURKING.BLB` rather than
restating it. **The margin is 3,264 bytes and it exists only because Lurking has
no item pane** -- `ITEM_GAME_N 1`, Zork I only. That exclusion is asserted, not
assumed, and a second check fails if the margin ever grows past the pane's own
size.

## Both floors are clear, and neither by the obvious route

| | was | now | floor |
|---|---|---|---|
| CD heap behind `LURKING.Z3` | 6,200 | 63,096 | 16,384 |
| netbin spare under 307,200 | 30,048 | 34,512 | 32,768 |

**The heap.** `typeahead_extract.c` held seven build tables as file-static
arrays: 57,592 bytes of `.bss`, and `__heap_start` follows `.bss`.
`build_typeahead_from_story` is the only non-static function in that file and
`create_word` copies every string it is handed, so nothing can read those tables
after it returns -- they were scratch, charged to the story heap for the whole
session. They come out of one `TYPEAHEAD_MALLOC` block now, borrowed for the
build and given back at the end of it. The declarations became pointers of the
matching shape, so every use site indexes them exactly as before.

**The netbin.** `synth_waves.c` was 5,120 bytes of `.rodata`. `genwaves.py`'s own
header explains why it generates offline -- "calling sin() thousands of times at
boot through soft-float libm" -- and that is true of the `smooth` voice, which
sums harmonics. The voice actually shipped is `nes`: three duty comparisons, a
sixteen-level integer staircase, and a fifteen-bit shift register. No floating
point, nothing from libm. `.bss` costs a netbin image no file bytes at all
because PlanetWeb bounds it by size, so building both tables at boot is 5,120
bytes off the image and neutral on the CD build.

That is only safe if the chip is uploaded the same bytes, so `synth_waves.c` is
kept free of includes and `test_synth_waves.py` **compiles it with a host
compiler and diffs all 5,120 against `genwaves.py`**. It passes. Do not weaken
that test into a reading of the two implementations side by side.

348 of 349 host tests pass. The one failure is the pre-existing
`test_every_frame_lies_inside_its_archive` -- Lurking's eight areas still have no
`.CGL` staged, which is art-v2's to finish and was failing before this session.

## The probe was answering nothing

The owner ran `tools/assets/scspfx.bat` and reported "I hear a tone", then the
screen text: `slot d of d`.

**The slot number was never printed.** SRL's `snprintfEx` switches on the
character straight after the `%`, so `%2d` matches no case and the conversion is
dropped entirely. Only `%d`, `%02d`, `%c`, `%s`, `%x`, `%u` and `%f` exist. Every
run of that probe named no slot, and "I hear a tone" was a true answer to a
question it never managed to ask.

Rebuilt, and not only for the format. It now **holds each slot until the listener
answers** -- A/C heard, B silent, LEFT to go back -- and keeps every verdict on
screen as a thirty-two mark map with `effects want 24-27` and `synth claims
28-31` read out underneath. The finished screen is the answer and a photograph of
it carries the whole sweep. The timed version asked someone to watch a number
changing every two seconds and carry thirty-two verdicts in their head, which is
not a thing to ask.

**Run, and the answer is: all thirty-two slots are ours.** `snaps/scspfx-0002.png`
is the clean sweep -- `driver: ON`, thirty-two `+`, both runs reading `all`.

An earlier screenshot (`scspfx-0001.png`) shows 18 ours and 14 the driver's.
**That one is not data.** It records button presses entered while the controls
were still unclear: Mednafen maps the keyboard's own **A** key to pad **X**,
which the probe ignored, so early answers went nowhere and later ones landed on
the wrong slots. The probe now takes the d-pad as well and both `.bat` files name
the keys (Z is pad A, X is pad B).

`scsp.h` has been corrected. It used to say the driver allocates from the bottom
and that a probe had confirmed slot 31 -- only 31 was ever confirmed, and 28-30
rode on an allocation story nothing had checked. The story is wrong and the
placement is fine anyway.

**What it settles: the sound-RAM route is open.** Four slots of our own for the
effects were the whole blocker, and there are thirty-two to choose from. 24-27
is as good as anything. That is the route the owner picked before the LWRAM one
was built, and nothing stands in front of it now except the work itself.

**What it does not settle.** The driver was *loaded*, not *playing*. The CD
build's PCM effects go through SRL's `slPCMOn`, which is this same driver, so a
sweep taken while an effect is sounding could come back differently. If music
ever breaks up when an effect plays, that is the sweep to run.

None of this blocks the LWRAM route that shipped: those effects go through SRL's
PCM and claim no slots of ours.

## The netbin's music: the chip was inherited, not taken over

The owner's own theory, and the best one: loading from the NetLink browser
changes what the sound hardware is when the program starts. It does, and not in
the way I first guessed -- `slSoundOffWait()` is `slRequestCommand(SMPC_SNDOFF,
SMPC_WAIT)`, an SMPC command that halts the 68K in hardware whatever program it
is running, so PlanetWeb's driver is stopped. That part was already right.

What is not stopped is the **SCSP's own register file**. The 68K is only what
writes those registers; the chip goes on doing whatever they say. PlanetWeb is a
browser that plays audio, so the netbin arrives on a chip with slots already
keyed, and a keyed slot loops its waveform forever once the CPU that would
release it is gone. `scsp_silence()` cleared **four** slots -- the ones this
synth uses -- and left the other twenty-eight exactly as found.

Heard: one instrument that is none of ours, over the music or instead of it,
different on every load. Which is what came back from NetLink three times.

`scsp_silence_all()` releases all thirty-two and is called from
`synth_target_init` under `#ifdef NETBIN` only. The CD build must keep clearing
just its own four: the SGL driver owns the rest there, and clearing those would
stop the CD-DA and the sound effects.

**Why no probe caught it.** Every probe is a CD image. `scspfx-nodrv` disables
*SGL's* driver; it cannot reproduce "another program's driver was already
resident and playing". The slot sweeps were taken on a chip nobody else had
touched, which is exactly the condition that does not hold on NetLink. If this
fix does not land, the next tool is the probe built as a netbin so it loads the
same way -- the build is `make all NETBIN=1 LDFILE=./sgl-netbin.linker`.

## Sound RAM was also being written a byte at a time

Also real, also emulator-invisible, and found first.
`scsp_upload_wave` copied waveforms into sound RAM through
a `volatile signed char *`, one byte per store, for as long as the synth has
existed. The SCSP is behind the Saturn's B-bus, which is sixteen bits wide; a
byte write there is not narrowed on your behalf, it is an access the bus has no
way to express.

Mednafen performs it anyway. The chip does not, and what comes back is a
waveform table that is part right and part whatever was there before -- which is
neither silence nor music. Over NetLink onto a real Saturn, one unchanged binary
gave "bleeps and bloops, no instrument" on one run and silence on the next.
`scsp.c` writes words now, and `tools/scspfx` lays its own tone down the same
way, since a probe answering which slots are usable against a waveform the chip
may never have received would be wrong convincingly.

**Every result this session called good was Mednafen.** drums-chip sounding
great, both thirty-two-slot sweeps, the "all slots are ours" conclusion in
`scsp.h` -- all emulator, none hardware. The sweeps are probably still right,
but they were taken with a tone written by the broken path, so they are not
evidence about the chip and should be re-run now that the probe writes properly.
`saturn/tests/test_scsp_access_width.py` holds the shape of both, because nothing
on a host can execute this and no emulator can fail it.

The level bug below is real and still worth fixing, but it is not what was
silencing the netbin.

## What was chased before that, and ruled out

None of it was the cause. Kept because it is all still true, and because the
next fault in this area will be looked for in the same places.

Reported mid-session and not chased, because it is a different fault. What was
ruled out by reading:

* The tune data reaches the tracker. With no `MUSIC.PAT` bound, `g_header` is
  NULL and `song_bank_at` falls back to `music_synth_song_at` -- the linked
  tables -- on every path.
* `synth_should_play(0)` returns 1, so `synth_start` runs.
* `synth_target_init` does the netbin-only work: `slSoundOffWait`, `synth_bind`,
  `synth_init`, `scsp_enable_output`, and the V-blank subscription.

What is left: `scsp_enable_output`, the waveform upload at `0x70000`, whether the
V-blank tick is actually firing, and `MUSIC_SYNTH_DEFAULT`'s range. Note that
`synth_target.cxx` records that a recording of this build was **flat until
`scsp_enable_output` existed**, so it was audible once.

**`tools\assets\scspfx-nodrv.bat` is the instrument for it**, built this session.
Same sweep, built with `SRL_USE_SGL_SOUND_DRIVER=0`, and it reproduces
`synth_target.cxx`'s netbin path -- `slSoundOffWait` and the master volume set by
hand -- because without that last part every slot would be silent into a muted
output and the probe would blame the chip for a volume register. It splits the
candidates in half:

**Run, and all thirty-two sound with the driver off too** (`scspfx-0003.png`,
`driver: OFF`). So in the netbin's own audio environment the chip, the sound RAM
write at `0x60000`, the master volume and slot keying are all good. **The fault
is above the tracker.** Nothing below it is worth looking at.

Ruled out from there by reading, not by running:

* the tune data -- with no `MUSIC.PAT` bound, `g_header` is NULL and
  `song_bank_at` falls back to `music_synth_song_at` on every path; song 0 is
  well formed (17 patterns, 24 order entries, speed 3+174/256);
* the level -- `SYNTH_LEVEL_DEFAULT` is 5 and nothing persists it;
* `music_source.cxx` -- not in the netbin's link at all;
* the ordering -- `synth_target_init()` runs at main_netbin.cxx:271, well before
  the `setjmp` and the `synth_start()` at :338;
* `SRL::Core::OnVblank` -- invoked from `VblankHandling`, which
  `Core::Initialize` registers with `slIntFunction`.

**The next instrument is `tools\assets\drums-chip.bat`**, and it had rotted:
`synth.c` took a dependency on the song bank when the catalogue landed and that
`.bat`'s copy list never followed, so the harness could not link the live synth
and could only ever have been run against something else. Fixed. It now builds
with no sound driver and `-DNETBIN`, links the real `scsp.c`, `synth.c`,
`tracker.c`, `song_bank.c` and tune tables, and boots straight into a tune --
which makes it the netbin's music path with no menus, no modem and no room
engine attached, about a hundred and eighty lines of surface.

* **it plays there** -> the synth stack is sound; the fault is in what the netbin
  does around it, and the candidates are whatever stops or re-levels the synth
  after `synth_start()` (netbin_pages.cxx's Sound page calls
  `synth_set_level`/`synth_start_song`, and `synth_play_track(0)` calls
  `synth_stop`).
* **silent there** -> the fault is inside those five files and reproducible with
  nothing else in the frame.

My `synth_init` change now calls `synth_waves_build()` before the upload. It
cannot be the cause -- the silence predates it -- but it is new code on that
path, so rule it out first by checking the four commits above out one at a time
if the silence turns out to have moved.

## Open

1. **Put the netbin on real hardware again** and hear whether the word-wide
   sound RAM write fixed it. Everything else waits on that, and nothing about it
   can be settled under an emulator, which cannot reproduce the fault.
2. **Re-run both SCSP sweeps** now that the probe writes its tone in words. The
   "all thirty-two are ours" answer was measured against a tone laid down by the
   broken path, and `scsp.h` currently records it as settled.
3. **The synth level is applied to a logarithmic field.** `synth_note_on` passes
   `(vol * g_level) / 7` to `scsp_key_on`, which writes it to DISDL -- register
   0x16, three bits, 6 dB a step. The tune's own volumes are 4 to 6, so at the
   default level of 5 they become 2, 3 and 4: every voice about 12 dB down, and
   the quiet ones about 5 dB further down than the loud ones because the division
   truncates. drums-chip hardcodes level 7, which is why it has never shown this.
   The player's level belongs on the master volume (MVOL, register 0x400, four
   bits, already written by `scsp_enable_output`) rather than multiplied into
   each voice's send level -- but MVOL is global, so on the CD build it would
   duck CD-DA and the PCM effects too. That fork is unresolved.
2. **Sweep again with a PCM effect playing.** The driver was idle for the sweep
   that answered "all thirty-two", and the CD build's effects go through it. If
   it keys 28-31 while playing, an effect disrupts the music.
3. **Nothing in this session has been heard or seen.** The LWRAM slices, the
   reclaim path, the rebuilt probe, the boot-built waveform tables. The tables
   are proven byte-identical on the host, which is as far as a host can go.
4. **~19.8 KB more netbin, available and not taken.** `MUSIC_CELLS` is 20,160
   two-byte cells using only **173 distinct (note, flags) pairs**, so a one-byte
   index into a pair table saves 19,814 bytes. Not done: it touches the tracker's
   hot path while the tracker is under suspicion, and 1,744 bytes of margin was
   enough to clear the floor. It is the lever to reach for next, after the
   silence is understood.
5. The item-pane exclusion is load-bearing for the sound budget. If a second
   story ever gets an item-picture table, `test_sound_slice_budget` fails and
   should be believed.

## Suggested skills

* **`superpowers:verification-before-completion`** -- carried over from the note
  this supersedes, and earned again twice: the probe's format bug, and the slice
  budget's own check failing the moment the heap moved, which is what forced the
  re-derivation rather than a shrug.
* **`superpowers:systematic-debugging`** -- for the netbin silence, which has
  four candidates and no symptom that distinguishes them.
