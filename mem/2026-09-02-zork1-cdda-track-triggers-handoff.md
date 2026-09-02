---
name: zork1-cdda-track-triggers-handoff
description: The seven CD-DA tracks the Zork I authentic-presentation project parked as UNATTRIBUTED are resolved by static analysis alone -- five are villain and sword-glow event cues, one is a muted duplicate of the ending theme, and one is selected by nothing at all; no runtime capture was needed and none of it is wired into the port yet.
metadata:
  type: project
---

Closes sub-project E of [[zork1-authentic-presentation-handoff]] -- "the seven
unattributed CD-DA tracks ... needs the same Mednafen capture that would also
settle the maze and river ordering". **The capture was not needed.** All seven
fell to static analysis of `0ZORK.BIN`, and `docs/ZORK1_AUDIO_MAP.md` plus
`analysis/zork_bg/cd_tracks.csv` now carry zero `UNATTRIBUTED` rows.

The maze and river ordering is a separate question and is still open -- that
half of sub-project E's justification for a capture stands.

## Why the static trace could close it

The earlier work stopped at "a byte read through a pointer argument" at
`0x06048adc` / `0x06048bbe`. That pointer is a constant: every one of the six
calls to the two byte-pointer players passes `0x060afb10`. Twenty-six
instructions in the image load that address; six are those calls and fourteen
are stores, so the literals stored to it *are* the complete list of event cues.
Nothing is computed, nothing is table-driven, nothing is left.

The identifications rest on two things the trace could resolve on its own:

- `0x0603b2d4` is `IN?(object, container)` over 22-byte object records at
  `[0x0608ef98]+332`; the current room is the `u16` at `[0x0608ef98]+8`.
- `GAME.DAT` is the initial state image with the same layout, so each object's
  starting parent reads straight out of it (**little-endian**, unlike the
  big-endian code). 188 starts in room 20, 195 in 41, 204 in 64, 205 in 65,
  241 in 93, 180 in 15 -- troll, cyclops, thief, spirits, bat, elvish sword,
  each matching its ZIL `(IN ...)`.

## What the seven are

| Track | Trigger |
|---|---|
| 13 | danger cue -- sword glow level becomes 1, or the sword is examined at level 1, or a troll/cyclops fight ends with the sword still carried |
| 14 | troll present in the Troll Room |
| 15 | thief present anywhere but the Treasure Room |
| 16 | thief present in the Treasure Room |
| 17 | cyclops present in the Cyclops Room |
| 7 | **nothing selects it** -- normal volume-table entry, no caller |
| 32 | **unused** -- the same recording as track 30, volume-table entry 0 |

Three tracks that were already attributed also gained a second, more specific
trigger: 9 fires when the rainbow solidifies at End Of Rainbow, 19 fires in the
death sequence, and 25 is a one-shot armed by a successful TAKE (the loop player
refuses 25 at `0x06048a7c`, so only the play-once path sounds it).

Full addresses, conditions and the object table are in the new
"situational-music poller" section of `docs/ZORK1_AUDIO_MAP.md`.

## None of it is wired into the port

**Superseded the same day** -- all seven cues plus death and the ending are now
wired; see [[zork1-situational-cues-wired-handoff]]. What follows was true when
this file was written.

This is documentation and analysis only. `saturn/` still plays exactly what the
generated presentation table says -- one looping track per room. The port has no
notion of the troll being in the room, so tracks 13-17, 25 and 30 are on the
disc's `/BG` side of the project unused. Wiring any of them up would need a
per-turn hook that can see the story's object tree, which is a different
question from the one this file answers.

## Two soft spots

- **Track 7 is a negative result.** It is as strong as an exhaustive scan of one
  binary can be, but it is still "no caller found", not "proved silent". A
  runtime capture would only ever confirm it.
- **The 13-after-a-fight rule is what the code says, not what it means.** The
  test is literally `IN?(sword, player)`, not "is the sword still glowing", so
  the danger cue continues after any troll or cyclops fight the player walks
  away from while carrying the sword. Worth watching for on hardware if these
  ever get wired up.

Related: [[zork1-situational-cues-wired-handoff]],
[[zork1-authentic-presentation-handoff]], [[cgl-only-presentation-handoff]]
