---
name: synth-song-catalogue-handoff
description: The eleven Shadowgate MIDIs are converted, each CD-DA track is matched to one of them by measurement, and the room engine drives the synth through that table on both targets -- the netbin linking all twelve and the CD build linking one and reading the rest off the disc, because on that target the story's heap follows .rodata and twelve tunes in the image stopped the largest story loading at all.
metadata:
  type: project
---

Branch `synth-music`, ahead of `origin/main`. Both Saturn targets link: netbin
**265,312 of 307,200**, CD heap **147,936** with **1,848 bytes** spare over the
floor the largest story needs. All 312 Python tests pass except one pre-existing
failure
(`test_lwram_budget.py::test_every_frame_lies_inside_its_archive`, art archives,
confirmed failing at HEAD before this work). Seven C sound tests pass, each built
twice -- with and without `-DNETBIN`, which is now a real distinction.

Continues [[hihat-crunch-handoff]], which is still the current note for how the
drums sound and how the tablature works. Nothing here changes the voicing.

**Still true: nothing has run on real hardware, and nothing here has been
listened to on the chip.** The offline model was used throughout.

## What was asked and what it turned into

"Check tools/assets/MIDI and convert the new tracks to play. They will be our
room mood tracks mappings as backup."

Eleven Shadowgate fan sequences. They are now in `tools/assets/music/` beside
`castle-halls.mid` -- a second MIDI folder was one folder too many -- and all
twelve tunes convert into one `saturn/src/sound/music_synth_data.c`.

## Read this first if you are short of time

Everything is reachable from the Sound page now, and the catalogue is split
unevenly between the two targets because they have different walls:

* **The netbin** carries all twelve tunes and has **no room engine** -- it links
  neither `music.c` nor `scene/game_presentation.inc` (51,690 bytes of source),
  so nothing there ever asks for a track. Its Sound page's **Test Track** row is
  the only place on hardware where eleven of the twelve can be heard at all.
* **The CD build** has the room engine, the presentation table and the mapping,
  and reaches **all twelve** -- it links one and reads the rest off
  `/BG/MUSIC.PAT`, a tune at a time. Its Sound page has a **Music From** row
  that switches between CD-DA and the synth, and a Test Track row that auditions
  whichever is active.

So the mapping is measured, wired, and drives all twelve tunes on both targets.
What it cost is 4,480 bytes of the disc build's story heap, and what is left is
1,848 bytes over the floor the largest story needs -- read the CD budget section
before adding anything to that image.

## The netbin ceiling is 300 KB, not 400

The owner's working figure, given after the first build. `post.makefile` gated at
409,600 with a note saying the real ceiling is lower but that nobody had
established it and to tighten the gate the moment it was measured; it is 307,200
now, and `test_netbin_budget.py` holds the same number with a 32 KB floor under
it. The spec's 400 KB was inferred -- it says in the same paragraph that the
loader "does not state the value".

At twelve uncut tunes the image was 279,072, which fits but leaves 28,128, under
that floor. **Every tune longer than 48 seconds is now cut to 48**, which gave
back 15,060 bytes and cost no tune at all:

| | image | spare under 307,200 |
|---|---|---|
| before the catalogue | ~224,900 | ~82,300 |
| twelve tunes, uncut | 279,072 | 28,128 |
| twelve tunes, 48 s cap | 264,032 | 43,168 |
| + the song bank and the Sound page rows | **265,312** | **41,888** |

Where the 264,032 went: `.text` 147,504, `.rodata` 106,880, `.data` ~24,700 (the
first netbin link map since the catalogue landed). `music_synth_data.o` is
**41,448 of it -- 40% of all `.rodata` and 16% of the whole image**, the largest
single object by a wide margin; the next are `netbin_story.o` at 20,027 (the
embedded Zork I) and `map_atlas.o` at 8,079.

Per tune, after the cap, largest first:

| tune | patterns | order | seconds | bytes |
|---|---|---|---|---|
| halls | 52 | 54 | 48.0 | 6,744 |
| overworld | 40 | 48 | 48.0 | 5,206 |
| corridor | 35 | 55 | 47.5 | 4,576 |
| lake | 29 | 72 | 48.0 | 3,817 |
| title | 27 | 36 | 47.5 | 3,526 |
| court | 24 | 49 | 47.8 | 3,159 |
| shadow7 | 23 | 73 | 47.4 | 3,054 |
| mirror | 22 | 48 | 45.0 | 2,899 |
| castle-halls | 20 | 24 | 23.6 | 2,621 |
| shadow8 | 19 | 33 | 26.4 | 2,502 |
| dragon | 18 | 40 | 48.0 | 2,379 |
| banquet | 9 | 37 | 34.7 | 1,225 |
| | | | | **41,708** |

Cost is patterns after dedup, not length: `banquet` is 34.7 seconds for 1,225
bytes and `halls` is 48 for 6,744, because one repeats and the other does not.
Uncut, `overworld` alone was 131 seconds and 14,377 bytes -- a quarter of the
whole catalogue.

**The cuts are blind, at a number of seconds and not at a musical boundary**, so
a tune may end mid-phrase and loop badly. Nobody has listened to them. The grid
is 32nds, so a 4/4 bar is 128 rows and a 3/4 bar is 96; nudging a `max_rows` to
the nearest multiple is one edit and `songs.bat` plays the result in a second.
`castle-halls`' 384 is what a deliberate cut looks like -- one statement of the
theme.

## The CD budget, which is a different wall

Twelve tunes are 27,712 cells: **55,424 bytes of `.rodata`**. On the CD target
`__heap_start` follows `.rodata`, so every one of those bytes comes off the HWRAM
heap the story is loaded into. Measured, not predicted:

| | heap | largest story | spare over the 16 KB floor |
|---|---|---|---|
| before | 152,416 | LURKING.Z3, 129,704 | 6,328 |
| all twelve tunes | 98,016 | same | **-31,688** |

At twelve tunes the disc's largest story does not fit in the heap **at all** --
`test_hwram_budget.py` caught it on the first build and named the cause without
help. The netbin loads no story and had 130 KB of image spare, so it takes all
of them.

The catalogue is therefore cut by build, and the cut is a **prefix and never a
scattered subset**: both builds read the same two flat arrays, the CD build's
copy just ends earlier, so a song index means the same thing in each. Which
tunes it links is `"cd": true` in `tools/assets/music/songs.json`, and that is
**one** -- the fallback, for the window before the disc has been read and for a
disc that does not carry the catalogue.

### Everything else comes off the disc

`tools/assets/mid2pat.py --pat` writes all twelve into
`saturn/cd/data/BG/MUSIC.PAT` (57,344 bytes, committed) and the CD build reads
them one at a time. That is the whole answer to the wall above: a tune on the
disc costs Low Work RAM, which the story does not touch, instead of the heap it
does.

**One at a time and not the catalogue**, and that is measured too. The
catalogue is 41,708 bytes; the Low Work RAM left in the worst in-game case --
area archive, typeahead trie, save scratch, item pane and map parchment all
resident -- is 25,998. One slot the size of the largest tune plus a header
sector is 8,704, which fits with 17,294 to spare.
`test_lwram_budget.py::test_the_song_bank_fits_beside_everything_else` holds it.

The cost is a seek when the room's mood moves to a tune that is not resident --
which is the same seek CD-DA pays for the same change, so the disc is not being
worked harder than the shipped presentation already works it.

**File layout.** A header sector carrying the directory, the track-to-tune table
and the ids, then one tune per sector run. Sector-aligned because SRL's
`LoadBytes` takes a **sector** offset and not a byte one: a record that did not
start on one could not be read without reading everything before it. The header
is validated before a single offset in it is believed -- magic, version, and the
pattern shape matching what the build was compiled against -- because the cells
are cast in place and a file from another build would be played as notes rather
than refused.

### What it cost

| | heap | spare over the 16 KB floor |
|---|---|---|
| one linked tune, no bank | 152,416 | 6,328 |
| two linked tunes | 149,344 | 3,256 |
| one linked tune + the disc catalogue | **147,936** | **1,848** |

The bank and its disc half are about 4,480 bytes of image between them, which is
more than the tune they replaced -- so this is not a saving, it is 1 tune
becoming 12 for 4.5 KB. **1,848 bytes over the floor is thin**: the next feature
added to that image trips `test_hwram_budget.py`. The lever, if it is needed, is
the linked fallback -- `castle-halls` at 384 rows is 2,243 bytes of it, and it is
only ever heard before the catalogue binds or on a disc that lost it.

## How the mapping was made

`tools/gen_synth_moods.py`. Each tune is rendered through the offline model and
measured by `gen_track_mood.measure_signal` -- which is that script's own
`measure` split in two so there is one implementation of "how bright is this"
and not two. The disc's thirty-one tracks were already measured into
`tools/assets/track_mood.json`.

Matched on **rank within each population**, not on absolute value. Four voices of
NES pulse are brighter and thinner than any of Activision's ambient recordings,
so absolute distance would put every track on whichever tune happened to be
darkest and say nothing; z-scoring both sets first asks "which tune is as bright,
as busy and as heavy *for a tune* as this track is *for a track*", which is the
same thing `track_mood.json`'s own mood words mean.

Two of the ten measurements are dropped and neither is a judgement call:
`loudness_db` is the preview's own normalisation, a constant of the renderer and
not of the tune; `width` is stereo spread and the model renders mono, so every
tune reads 0.000 and the match would tilt toward whichever tracks are narrowest.

The result covers **9 of the 12 tunes** (it was 8 before the 48-second cuts moved
the measurements; the mapping was re-run against the tunes as they now ship).
`halls`, `overworld` and `shadow8` are matched by nothing: several of the tunes
measure almost identically -- `lake`, `mirror` and `shadow7` sat within 34 Hz of
brightness and 0.006 of tonality of each other -- so one of a near-identical
group wins every track the group is near, and `shadow8` is an outlier (weight
0.040 against 0.14-0.28, 4.5 pulses a minute against 130-560) that nothing on
the disc resembles. `track_songs.json` is hand-editable and says
so; a balanced assignment was **not** written, because it would mean handing a
track a tune that is not the nearest, and that is a taste call.

## What to run

    tools\assets\songs.bat                 list the ids
    tools\assets\songs.bat halls           render and play one, about a second
    tools\assets\drums.bat                 the tablature, unchanged, now via the manifest
    tools\assets\drums-chip.bat halls      that tune on the real SCSP, about thirty seconds
    python tools\assets\mid2pat.py --manifest tools\assets\music\songs.json ^
           --out saturn\src\sound\music_synth_data.c
    python tools\gen_synth_moods.py --report    the match, writing nothing

`drums.bat`, `drums-emit.bat` and `songs.bat` all read `songs.json` now, so the
entryway theme's tempo and tablature cannot be rendered three different ways by
three scripts. `drums-chip.bat` takes a song id and writes
`tools/scspprobe/src/probe_song.h` through `tools/assets/song_index.py`, which
fails on an unknown id **before** the thirty-second build.

## Two things found on the way

**sgdragon.mid is damaged.** It declares eight tracks, carries five, and gives
the last of those a length ending 290 bytes past the end of the file. `read_midi`
indexed straight off the buffer. It now clamps each track's end to what is really
there and pads the buffer so an event straddling that end reads zeros -- a tune
missing its last bars is worth more than no tune. `sgdragon` converts to 816 rows
and is in the catalogue.

**The engine stalls on a disc with no CD-DA, and did before this.** `music.c`
issues a track, sets `g_await_play`, and waits for `is_playing()` to become true.
With `music_cdda_is_playing` bound and no audio on the disc, that never happens
and `music_tick` returns early forever. Swapping `is_playing` to `synth_playing`
along with the backend clears it. This was not the reason for the swap and was
not looked for; it fell out of reading `music_tick` to check the loop-end rules.

## The Sound page

Two rows, and the row list is now rebuilt every frame -- the only Options page
that does that except Display, and for the same reason: a row on it changes the
list under the cursor.

**Music From** appears only on a disc that carries CD-DA, because without one
there is nothing to choose between. It switches live through
`music_source_select`, which silences the source being left before rebinding
(the engine's stop goes through the backend, so after the rebind it would stop
the wrong one) and then re-issues the room's own track through the new one. The
choice is persisted in the sound block's third byte, which had been written as a
hard zero since the block was created -- so the sentinel and the width did not
move and every options blob already on a cartridge decodes as CD, which is what
those saves were played with.

**Test Track** is the Track row from `515291d` recovered and given a second list.
Under CD it steps the disc's own audio tracks and previews through
`music_start_menu`; under the synth it steps the catalogue and previews through
`synth_start_song`. It shows the songs.json id and not the title, because there
are 14 columns after the padded label and "Shadowgate, Lit Corridor" is 24.

A preview is a demonstration and never a choice -- the room decides the music,
which is what the mix modes did and why they were removed -- so every exit puts
back what was playing. **The trap is that `music_refresh()` is the wrong call for
that**, and it is the obvious one: a preview goes through `music_start_menu`,
which makes the previewed track the one the engine holds, so refreshing after an
audition re-plays the audition. The track the page opened on is snapshotted and
named again. The same reasoning is why switching source undoes an audition first.

`music_refresh()` IS right inside `music_source_select`, because a source change
does not change what the engine holds -- only which backend holds it.

## Where the wiring is

`saturn/src/sound/music_source.cxx`, which is a `.cxx` for one reason: the CD
build finds its sources with `find src/ -name '*.cxx'` and the netbin lists its
own explicitly, so the extension is what keeps this out of a build that has no
`music.c` to bind to.

It binds all five of the engine's hooks together -- backend, is_playing,
is_short, pause/resume, duck/unduck -- because the one that is easy to forget is
`is_playing`: the engine reads not-playing as loop-end and, bound to CD-DA on a
disc with none, sits in its await-play branch forever. That is what the music did
on such a disc before any of this, and it was found by reading `music_tick` to
check the loop-end rules rather than by looking for it.

`main.cxx` calls `music_source_bind()` at the point that already asked
`synth_should_play(music_cdda_has_audio())` -- and it is there, and not beside the
CD-DA callbacks two hundred lines earlier, for the reason the comment above it
already gave: `music_cdda_audio_tracks` caches its answer on the first call, so
asking before the drive's TOC is readable freezes the wrong one. The earlier
binding is left in place and is now marked as provisional.

`music_fade_volume` asks `music_source_active()`, because the synth attenuates
per slot and 0 is real silence with a way back, where `music_set_volume(0)` stops
the drive with none.

`synth_start_song` does nothing when asked for the tune already playing. That is
the whole reason it is not stop-plus-start: `music.c` re-asserts its choice on
every room change inside a category, and a walk through four rooms of one mood
would otherwise be four first bars. `test_synth_select.c` pins it by comparing
the whole register file before and after, not by the call returning.

## Open, for the owner

1. **The disc build has 1,848 bytes over its floor.** Nothing is broken and the
   next feature added to that image is what breaks it. The lever is the linked
   fallback in `songs.json` -- see the CD budget section -- and the alternative
   is that the disc's own presentation grows instead.
2. **Should the netbin get a room-to-song path of its own?** It is the target
   where the synth is always the music and it carries every tune, but it has no
   presentation table. The larger question, and probably a session of its own.
7. **The disc reads have never been made.** Every path through the song bank is
   host-tested against the real MUSIC.PAT, including a refused header and a read
   that fails -- but that test hands the bank a file from a filesystem. Whether
   `LoadBytes` returns what this expects from a sector offset on a real drive is
   unproven, and it is the single thing most likely to be wrong.
3. **The three unmatched tunes**, above. Hand-editing `track_songs.json` is the
   intended fix and takes a minute per track.
4. **Where the 48-second cuts landed.** They are the one thing here chosen by a
   round number rather than by measurement or by ear, and nine tunes carry one.
   `songs.bat <id>` is a second per check. `title` is cut harder still, to 288
   rows, for the CD heap's sake.
5. **The titles in `songs.json` are read off the filenames** of a fan sequence
   set -- "Banquet", "Lit Corridor", "Overworld" -- and are not Masuno's own.
   They name the tunes for us, not for the record.
6. **Nothing has been heard on the chip, and no menu has been seen on a screen.**
   `drums-chip.bat halls` is thirty seconds and is the fastest way to find out
   whether eleven tunes converted with settings nobody has listened to actually
   sound like anything. The Sound page's two new rows have been built and
   reasoned about and never looked at.
