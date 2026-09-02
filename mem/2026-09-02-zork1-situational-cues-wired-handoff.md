---
name: zork1-situational-cues-wired-handoff
description: Zork I's troll, cyclops, thief, sword-glow, take, death and ending cues are now wired into the port through a generated per-game table and a new CAT_KIND_CUE in the music engine; the random neutral draw every un-tabled story falls back on can no longer land on a track a cue has spoken for; and the 90-frame prompt freeze at a track change is gone, because only a picture change is worth waiting for. Three host test binaries prove it against the real story image, the room model moved a step earlier in the turn to make it possible, and none of it has been built or run.
metadata:
  type: project
---

Implements the finding in [[zork1-cdda-track-triggers-handoff]]. That file says
what the tracks are; this one says what the port now does with them.

## The owner has not built or run any of this

Verified by host `gcc` only -- `test/cue_test.c`, `test/cue_engine_test.c` and
the repaired `test/music_test.c`, all clean under `-Wall -Wextra`, plus 99
Python tests -- and by `-fsyntax-only` against the real SRL headers in DEBUG and
release. Nothing has been seen on Mednafen or hardware. Two notes on what that
checking is and is not:

- The `.c` files were checked with the SH-2 **gcc** at `-std=c2x`, not through
  `syntax-check.sh`: that script drives `$CXX` at every file, and a `.c` unit
  type-checked as C++ fails on linkage mismatches the real build never sees
  (`mojozork_saturn.c` produces a screenful of them, unrelated to this work).
- **The netbin build compiles no sound files at all** -- its `SOURCES` in
  `saturn/makefile` is an explicit list and `music.c`, `music_cdda.cxx` and
  `cues.c` are not on it. Checking them under `-DNETBIN` proves nothing; it was
  done anyway and passed, but the claim to make is CD-build coverage only.
  (`music_cdda.cxx` does not type-check under `-DNETBIN` at all, at HEAD as
  much as here, because SRL's sound stack is deliberately excluded from that
  configuration.)

## The shape of it

- `tools/gen_cues.py` -> `saturn/src/scene/game_cues.inc`, keyed by release and
  serial the way the presentation table is. Only Zork I has a row; every other
  story keeps exactly today's behaviour.
- `saturn/src/scene/cues.c` evaluates the rules against `room_model`'s snapshot.
- `music.c` gains `CAT_KIND_CUE` (the category IS the track, like
  `CAT_KIND_ROOM`) and `CAT_KIND_ONCE` (the take sting). A cue outranks the
  room's theme, an event outranks a cue.
- `room_model` gains `room_model_object_parent` and `room_model_object_attr`.

## Two things about it that are not obvious

**The room model moved earlier in the turn.** `music_on_turn` runs from
`mojozork.c` before the client's readline, and the model used to be refreshed
inside that readline -- so a cue reading the snapshot would have answered for
the room the player was in last turn, and the troll's music would have started
one command late. `mojozork.c` now refreshes it immediately before
`music_on_turn`, and `saturn_glue.cxx` no longer does. Refreshing twice would
have been the smaller edit but feeds the player-object inference a duplicate
sample, and that inference is what the take sting is gated on.

**INVISIBLE had to be solved, not assumed.** The thief is in a room from the
first turn of the game and visible in it for very little of that, so a cue that
ignored the attribute would fire at random while he wanders. The bit is not
stated anywhere: the generator matches every ZIL object whose `DESC` resolves to
exactly one story object against that object's attribute word and takes the bit
set on every `INVISIBLE` object and no other. 116 objects agree on bit 7. Fewer
than exactly one candidate raises rather than writing a zero.

## Where this is faithful and where it is an analogue

Faithful: the troll and cyclops rules are the original's own conditions with the
object numbers translated. The thief's two tracks and their Treasure Room split
are the original's. "A troll in the room outranks a thief in it" is the
original's extra condition, expressed here as rule order.

An analogue: the danger cue (13). The original set it off a sword-glow level the
Saturn engine maintained; the port asks whether any rule's villain stands in a
room this one has an open exit to, with the sword carried. That is what ZIL's
own glow routine means by level 1, but it is a reimplementation, not a
translation -- and it only sees exits the room object declares unconditionally,
so a villain behind a door or a routine-guarded exit does not raise it.

## Four things to watch for on hardware

- **The take sting costs two seeks.** Six seconds of track 25 between two seeks,
  every time the player picks something up. It is what the original did, but the
  original was not re-issuing plays the way this engine does. If it grates, the
  cheapest retreat is `TAKE = 0` in `gen_cues.py` and a regenerate.
- **The take is inferred, not reported.** Any turn the inventory grows counts --
  including the thief handing something back. `room_model` has to have worked
  out which object the player is first, which takes one room change.
- **Death now plays track 19, which is also the Hades rooms' theme.** Dying in
  the Land of the Living Dead will sound like nothing happened.
- **Nothing tells the art the cue changed.** `music_set_room_fn` still fires on
  room changes only, so the picture stays on the room while the troll's music
  plays over it. That is deliberate -- an event never announced itself either --
  but it has never been seen.

## The reserved list, added straight after

Wiring the cues gave the disc's tracks meanings the random fallback did not know
about. A story with no authored per-room table draws its music out of the
neutral pool, and that pool held track 30 (the ending theme), 16 (the thief's
lair cue) and three rank fanfares -- so an un-tabled game could open a room, or
the title screen, on the victory music.

`music_data.c` now carries a `RESERVED` bitmask and `music_track_reserved()`:
13-17, 19, 25, 30 and the eight fanfares, plus 32 for a different reason (it is
the muted duplicate of 30, so drawing it plays 4:18 of nothing). Track 10 is
deliberately not on it -- it is the opening's fixed track, but nothing has
reserved it, so a Return to Title may still draw it. The neutral
pool was rebuilt to hold only what is left -- `{2,3,4,5,6,7,8,9,10,11,12,18,20,31}`,
which is more tracks than it had before, not fewer -- and `music.c`'s
`pool_allows` filters the draw as well, so a later edit to the pool cannot
quietly reintroduce one. The two ending pools are exempt on purpose.

## The opening plays one track; only a return draws

The title screen draws from that same pool, which meant the first thing the
machine ever played was different every boot. It now names one instead:
`music_start_menu` takes a track, `MUSIC_OPENING_TRACK` is 10 -- the house
theme, which is also what Zork I's above-ground rooms loop, so picking Zork I
and stepping out of the front door carries it through without a change of
music -- and a named track is held as `CAT_KIND_ROOM` so the loop-end rules
replay it rather than cycling off it every third pass.

Only a Return to Title passes 0 and draws. Telling the two apart needed a
discriminator, because main's title sequence is deliberately identical on both
paths: `setjmp`'s own return value is now kept in `g_returned_to_title`, which
is the only thing below the setjmp that reads which boot it is.

## Music off used to pause the game anyway

Reported after the above landed, and worth reading as a consequence of it: the
engine had no idea whether music was switched on. `music_set_level` went
straight to the CD-DA volume register, and everything in `music.c` went on
running -- a room change armed a debounced switch, the ramp took
`MUSIC_FADE_FRAMES` (45) down and 45 back up, and `run_room_transition` spins
the prompt frame by frame for the whole of `music_transition_active()`. With the
music off that is **a second and a half of frozen game at every point a track
would have changed**, for a swap nobody could hear. On a story with no authored
table it fired every third room, off the `MUSIC_ROTATE_ROOMS` rotation.

`music_set_audible(int on)` now tells the engine. Off, it keeps deciding what
*should* sound -- so switching back on starts the right thing immediately rather
than waiting for another room -- but `play_dyn` issues nothing and
`music_on_turn` adopts the target on the spot instead of arming. The hook is in
`music_set_level`, deliberately not `music_set_volume`: the latter also carries
the fade ramp, which walks to 1 and back many times a session, and hooking it
would switch the engine off on every transition. Every path that changes the
player's setting -- boot, game start, both ways out of the Options sound page --
goes through `music_set_level`.

The art half needed nothing: `room_art_changes_for` already answers 0 for a room
with no entry, so an unmapped story arms no picture transition. Music off plus no
mapping is therefore `music_transition_active() == 0` on every turn, and
`run_room_transition` returns without spinning a frame -- which is the property
`music_test.c` now asserts, with a music-on pass first so the music-off
assertions cannot pass vacuously.

A picture transition with the music off keeps its ramp, on purpose -- the screen
has to be dark to swap the picture whether or not anything is playing.

## Then the pause with the music ON, which was the same mistake one level up

`run_room_transition` waited on `music_transition_active()`, which is true for
*any* pending change. But only a picture has to be put up unseen: a track change
moves nothing on screen and its ramp is a volume ramp. The engine did not
distinguish them either -- `on_music_fade` drove `title_bg_dyn_fade` on every
ramp -- so a pure track change dimmed the room the player was reading and froze
the prompt for 90 frames while it did.

The engine now carries `g_fade_art` beside `g_fade_audio`, the fade callback
takes both, and `music_transition_art()` is what the client waits on:

- **Track only:** screen and sound effects untouched, prompt not held at all. The
  read loop already calls `music_tick()` every frame, so the debounce and the
  volume ramp finish under the player's typing. Nothing is lost by not waiting.
- **Picture:** the ramp still runs, but nobody waits for it either. The wait was
  only ever buying an ordering -- old screen out, swap at the bottom, new text
  drawn onto the new picture -- and the fade never needed it, because the fade
  does not touch the text: `title_bg_dyn_fade` drives colour offset channel B,
  which carries the picture and the backdrop, while the console and the marble
  chrome sit on channel A and do not move. So the text is drawn at once and read
  at full brightness while the picture dims out behind it, swaps and comes back.
  `run_room_transition` now only flushes the picture's settle so the ramp starts
  at the prompt rather than after 90 frames of debounce, and returns.

  Two things make that safe. The read loop renders the interface every frame
  (`render_game_keyboard` and `render_command_panel` both claim the dash), so the
  `dash_hold` the old spin needed is already being done. And `on_art_commit`
  shows `g_art_room`, which `on_text_room` rewrites on every room change, so a
  ramp begun two rooms ago still puts up the room the player is standing in --
  walking on during a fade cannot strand it on a stale picture.

**There is no blocking path left at all.** `run_room_transition` runs no loop
and calls no `Synchronize`; the prompt is never held for music or for art.

The numbers, for whoever tunes this next: `MUSIC_DEBOUNCE_FRAMES` is 90 and
`MUSIC_FADE_FRAMES` is 45 each way. A picture change flushes the debounce and
ramps 90; a track change spends the full 90-frame debounce and then ramps 90 --
and all of it now happens under the player's typing.

What this changes on screen, and has never been seen: for the first frames of a
picture change the new room's text sits over the *previous* room's picture while
it dims. That is the ordering the wait used to buy, traded for the wait itself.
Walking briskly through authored rooms will also pulse the picture -- down, swap,
up, down again -- where it used to freeze once per room instead.

## Not touched, then repaired

`test/music_test.c` did not build before this work -- it called `EV_DANGER`,
`EV_TRIUMPH` and `music_set_category_fn`, none of which still exist -- and the
first pass of this project left it alone. It has since been rewritten around
what the engine actually does: the death banner, the three pools, and the new
rule that no reserved track can come out of a neutral draw. Its overflow test
now reads which *pool* the engine drew from rather than whether it played at
all, which the CAT_KIND_POOL fallback had made meaningless.

Related: [[zork1-cdda-track-triggers-handoff]],
[[zork1-authentic-presentation-handoff]]
