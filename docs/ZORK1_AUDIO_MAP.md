# Zork I (Sega Saturn, Japan) - audio map

Room music comes out of the same 16-byte presentation record that selects the room
background (see `ZORK1_ROOM_BACKGROUNDS.md`). Field `+0` is a **raw disc track number**, not an
index into the audio tracks.

The room-change handler at `0x06048c1c` reads `record[+0]` and passes it to the looping player
at `0x0602a4d8`, which fills a CDC play spec at `0x060b2dac`:

| Byte | Value |
|---|---|
| +4 / +5 | start track = the field value, start index = 1 |
| +12 / +13 | end track = the field value, end index = 99 |
| +16 | play mode `0x0F` (repeat until told otherwise) |

A second entry point at `0x0602a578` is byte-identical except for mode `0x00` (play once).
Both scale output using a per-track volume byte table at address `0x0608ef74`
(file offset `0x8af74`): 127 for every track except 32, which is 0.

Field `+2` of the same record selects the sound-effect bank, one of
`SEALL SEMINA SEMINB SEMIR SEDAM SECEL SEHDS SERIV SEWOD SEMAZ SEBAR`.

## Every play path

Those two functions are the **only** code that touches the play struct at `0x060b2dac`, and
nothing reaches them by `bsr`/`bra` - so every note the game plays comes through one of eight
call sites:

| Call site | Player | Track source |
|---|---|---|
| `0x06048c1c` | loop | `record[+0]` for the room being entered |
| `0x0600b2fe` | loop | same, on the room-change path |
| `0x06048adc`, `0x06048b32` | loop | the pending-track byte at `0x060afb10` |
| `0x06048bbe` | once | the same byte |
| `0x06010b30` | loop | literal 0 (stop) |
| `0x0601461c` | loop | rank index into the endgame track array |
| `0x0600b9f2` | once | literal 30 |

Two static tables feed those sites:

* **Script music commands.** A jump table at `0x06010ac8` switches on a command id 0-10 read
  from the event stream. Ids 0-7 store a track number: 4, 5, 9, 10, 11, 12, 2, 22. Ids 8-10
  are the music/sound on-off toggles in the config screen, not track selectors.
* **Endgame rank screen.** Rank-name pointers at `0x060143a4` sit beside a parallel `u16`
  track array at `0x060143c8`, giving one short fanfare per rank:

Both byte-pointer players take the same argument every time they are called:
`0x060afb10`, passed from `0x0600b2ac` and from five screen routines. Every
literal stored to that byte is therefore the complete list of event cues - see
the situational-music section below.

| Rank | Track |
|---|---|
| `????` placeholder | 21 |
| Amateur Adventurer | 22 |
| Novice Adventurer | 23 |
| Junior Adventurer | 24 |
| Adventurer | 26 |
| Master | 27 |
| Wizard | 28 |
| Master Adventurer | 29 |

Rank slot 0's name string is the literal ASCII `????`; the Japanese rank names live in a
second ladder at `0x0602acd0`, which writes the same eight tracks to `0x060afb10` by score
band (350, >330, >300, >200, >100, >50, >25, else).

Track 25 (0:06) never appears in a table but the sound handler compares against it explicitly
at `0x06048a7c` and `0x06048b92`, so it is reached through the byte-pointer path.

## Tracks

Disc layout is one MODE1/2352 data track plus 31 CD-DA tracks (2-32). All 31 are accounted
for below; 7 and 32 are accounted for as unused.

| Track | Length | Rooms | Used for |
|---|---|---|---|
| 2 | 2:19 | 6 | room BGM, looped by 6 rooms; script music command 6 |
| 3 | 2:11 | 8 | room BGM, looped by 8 rooms |
| 4 | 1:52 | 12 | room BGM, looped by 12 rooms; script music command 0 |
| 5 | 2:21 | - | script music command 1 |
| 6 | 1:53 | 12 | room BGM, looped by 12 rooms |
| 7 | 1:48 | - | UNUSED - no code path selects it |
| 8 | 2:12 | 5 | room BGM, looped by 5 rooms |
| 9 | 1:18 | - | script music command 2; the rainbow solidifies at End Of Rainbow (room 81), set at 0x06038c6c |
| 10 | 1:15 | 7 | room BGM, looped by 7 rooms; script music command 3 |
| 11 | 1:51 | 8 | room BGM, looped by 8 rooms; script music command 4 |
| 12 | 1:58 | 5 | room BGM, looped by 5 rooms; script music command 5 |
| 13 | 1:49 | - | danger cue: sword glow drops to level 1 (0x060347a4), sword examined at level 1 (0x06038814), or a troll/cyclops fight ends with the sword still carried (0x06048dc8) |
| 14 | 2:38 | - | troll in the Troll Room: room 20 and IN?(188, 20), set at 0x06048c9a |
| 15 | 1:57 | - | thief present outside the Treasure Room, set at 0x06048e48 |
| 16 | 1:58 | - | thief present in the Treasure Room (room 43), set at 0x06048e48 |
| 17 | 2:18 | - | cyclops in the Cyclops Room: room 41 and IN?(195, 41), set at 0x06048d3a |
| 18 | 2:33 | 15 | room BGM, looped by 15 rooms |
| 19 | 2:34 | 2 | room BGM, looped by 2 rooms; death sequence, set at 0x0603a5dc |
| 20 | 3:01 | 19 | room BGM, looped by 19 rooms |
| 21 | 0:32 | - | rank screen: (placeholder '????') (score <= 25) |
| 22 | 0:33 | - | script music command 7; rank screen: Amateur Adventurer (score > 25) |
| 23 | 0:28 | - | rank screen: Novice Adventurer (score > 50) |
| 24 | 0:29 | - | rank screen: Junior Adventurer (score > 100) |
| 25 | 0:06 | - | one-shot cue armed by a successful TAKE (0x060331d8); the loop player refuses it at 0x06048a7c so only the play-once path at 0x06048b92 sounds it |
| 26 | 0:34 | - | rank screen: Adventurer (score > 200) |
| 27 | 0:25 | - | rank screen: Master (score > 300) |
| 28 | 0:31 | - | rank screen: Wizard (score > 330) |
| 29 | 0:39 | - | rank screen: Master Adventurer (score = 350) |
| 30 | 4:18 | - | ending screen (END.CGD/END.COL), played once from 0x0600b9f2 |
| 31 | 0:36 | 1 | room BGM, looped by 1 room |
| 32 | 4:18 | - | UNUSED - the same recording as track 30 (292 differing bytes in 46 MB) with volume-table entry 0 |

### Nothing left unattributed

Every track now has a code path except **7** and **32**, and both are unused for
reasons the code states rather than leaves open:

* **Track 7** (1:48) is selected by nothing. The pending-track byte at
  `0x060afb10` is the only argument either byte-pointer player ever reads, and
  the twenty-six instructions that load its address across the whole image
  resolve to six player calls and fourteen stores, writing only 255, 9, 13
  (three sites), 14, 15, 16, 17, 19, 25 and the eight rank fanfares. No
  presentation record holds 7, no arm of the script-command jump table holds 7,
  and the rank track array holds 7 nowhere. Its volume-table entry is a normal
  127, so it was meant to play; nothing ever asks for it.
* **Track 32** (4:18) is the same recording as track 30 - one sector shorter,
  292 differing bytes across 46 MB - and its volume-table entry is 0. A muted
  duplicate of the ending theme, reachable by no call site.

## The situational-music poller at `0x06048c6c`

Five of the seven formerly unattributed tracks come out of one function that
runs on the room-change path and stores a track number into `0x060afb10`. It
tests object containment with `IN?` at `0x0603b2d4`, which walks the 22-byte
object records at `[0x0608ef98]+332`; the current room is the `u16` at
`[0x0608ef98]+8`. Object ids were read off GAME.DAT's initial-parent field
(little-endian `u16` at record `+0`) and confirmed against the ZIL:

| Object | Initial parent | Identity |
|---|---|---|
| 145 | 65535 | the player (the `IN?` container in every inventory test) |
| 180 | 15 Living Room | the elvish sword |
| 188 | 20 Troll Room | the troll |
| 195 | 41 Cyclops Room | the cyclops |
| 204 | 64 Round Room | the thief |
| 205 | 65 Entrance To Hades | the spirits |
| 241 | 93 Bat Room | the vampire bat |

The same five ids drive the villain portrait loader at `0x0600b454`, which
picks one of five `0x7020`-byte images by the same containment tests - bat,
cyclops, troll, thief, spirits.

| Track | Condition |
|---|---|
| 14 | room is 20 **and** `IN?(troll, 20)` **and** the troll latch `0x0608fdc4` is clear |
| 17 | room is 41 **and** `IN?(cyclops, 41)` **and** the cyclops latch `0x0608fdc5` is clear |
| 16 | `IN?(thief, room)` **and** not `IN?(troll, room)` **and** room is 43 (Treasure Room) |
| 15 | the same thief test in any other room |
| 13 | the troll or cyclops latch is set but the player has left that room, **and** `IN?(sword, player)` |

Leaving a fight without the sword takes the other arm: `0x060a4840` (the
currently-playing track) is set to 14 or 17 so the next room-music update sees a
mismatch and switches back to the room's own theme.

Track 13 has two more sources outside this function, both about the sword's
glow level - the byte at `[0x0608ef98]+0x10d2`, 0 / 1 / 2:

* `0x060347a4`, the per-turn glow update, when the level becomes 1
  ("your sword is glowing with a faint blue glow"). Level 2 ("has begun to glow
  very brightly") sets no track, because the villain that caused it has already
  set 14/15/16/17.
* `0x06038814`, the sword-examine handler, when the level is already 1.

The other two:

* **Track 19** at `0x0603a5dc`, in the death sequence, right after
  "It appears that that last blow was too much for you" - the same theme the
  Entrance To Hades and Land Of The Living Dead rooms loop.
* **Track 9** at `0x06038c6c`, when the rainbow solidifies at End Of Rainbow
  (room 81) - the same track script music command 2 selects.

Track 25 (0:06) is a one-shot, not a loop. A successful TAKE clears the latch at
`0x0608fdcc` and stores 25; the loop player at `0x06048a7c` explicitly refuses
25 and falls through to the resume-room-music path, so the only thing that
sounds it is the play-once player at `0x06048b92`.


## What the port does with this

`tools/gen_cues.py` turns the rules above into `saturn/src/scene/game_cues.inc`,
keyed by release and serial the way the presentation table is. The Saturn
release's object numbers mean nothing to a Z-machine, so every object is
resolved by short name out of `saturn/cd/data/Z3/ZORK1.Z3` itself, and the one
thing neither source states -- which attribute bit is `INVISIBLE`, without which
the thief cue would fire on his wanderings -- is solved by matching 116 ZIL
`FLAGS` lists against the story's own attribute words. It comes out unique, at
7. The resolved numbers:

| | Saturn object | Z-machine object |
|---|---|---|
| troll | 188 | 217 |
| cyclops | 195 | 186 |
| thief | 204 | 114 |
| elvish sword | 180 | 110 |
| player | 145 | 4 |

`scene/cues.c` evaluates the rules against `room_model`'s per-prompt snapshot
and `music.c` plays the answer as `CAT_KIND_CUE`, which outranks the room's own
theme and is decided fresh every turn -- a villain who dies takes his music with
him without the room having changed. The danger cue reads the snapshot's open
exits and asks whether a villain stands on the other side of one, which is the
Z-machine equivalent of the original's sword-glow level 1.

The same table is why `music_data.c` now carries a reserved list. A story with no
authored per-room table draws its music at random out of the neutral pool, and
that draw must not land on a track a cue has spoken for -- 13-17, 19, 25, 30 and
the eight rank fanfares -- because a room that opens on the victory fanfare has
announced something that did not happen. 32 is on the list for a different
reason: it is the muted duplicate of 30, so drawing it plays 4:18 of nothing.
The filter sits in `music.c`'s `pool_allows` rather than only in the pool's
contents, so a later edit to the pool cannot quietly reintroduce one; the two
ending pools are exempt, since an ending playing the ending theme is the point.
The title screen draws from the same neutral pool when it is reached by a Return
to Title, so that draw is a room theme too. A cold boot does not draw at all: it
plays `MUSIC_OPENING_TRACK`, track 10, and holds it for as long as the menu is
open. That is the house theme, which is also what Zork I's above-ground rooms
loop, so picking Zork I and stepping outside carries it straight through without
a change of music.

## Rooms

| # | Room | Track | Length | SE bank |
|---|---|---|---|---|
| 0 | Clearing | 11 | 1:51 | SEWOD |
| 1 | Behind House | 10 | 1:15 | SEALL |
| 2 | Forest | 11 | 1:51 | SEWOD |
| 3 | Forest | 11 | 1:51 | SEWOD |
| 4 | Forest | 11 | 1:51 | SEWOD |
| 5 | Clearing | 11 | 1:51 | SEWOD |
| 6 | Forest | 11 | 1:51 | SEWOD |
| 7 | North Of House | 10 | 1:15 | SEALL |
| 8 | Forest Path | 11 | 1:51 | SEWOD |
| 9 | South Of House | 10 | 1:15 | SEALL |
| 10 | Barrow Entrance | - | silent | SEBAR |
| 11 | Up A Tree | 11 | 1:51 | SEWOD |
| 12 | West Of House | 10 | 1:15 | SEALL |
| 13 | Attic | 10 | 1:15 | SEALL |
| 14 | Kitchen | 10 | 1:15 | SEALL |
| 15 | Living Room | 10 | 1:15 | SEALL |
| 16 | Cellar | 4 | 1:52 | SECEL |
| 17 | East Of Chasm | 4 | 1:52 | SECEL |
| 18 | Gallery | 4 | 1:52 | SECEL |
| 19 | Studio | 4 | 1:52 | SECEL |
| 20 | Troll Room | 4 | 1:52 | SECEL |
| 21 | Dead End | 20 | 3:01 | SEMAZ |
| 22 | Dead End | 20 | 3:01 | SEMAZ |
| 23 | Dead End | 20 | 3:01 | SEMAZ |
| 24 | Dead End | 20 | 3:01 | SEMAZ |
| 25 | Grating Room | - | silent | SEMAZ |
| 26 | Maze | 20 | 3:01 | SEMAZ |
| 27 | Maze | 20 | 3:01 | SEMAZ |
| 28 | Maze | 20 | 3:01 | SEMAZ |
| 29 | Maze | 20 | 3:01 | SEMAZ |
| 30 | Maze | 20 | 3:01 | SEMAZ |
| 31 | Maze | 20 | 3:01 | SEMAZ |
| 32 | Maze | 20 | 3:01 | SEMAZ |
| 33 | Maze | 20 | 3:01 | SEMAZ |
| 34 | Maze | 20 | 3:01 | SEMAZ |
| 35 | Maze | 20 | 3:01 | SEMAZ |
| 36 | Maze | 20 | 3:01 | SEMAZ |
| 37 | Maze | 20 | 3:01 | SEMAZ |
| 38 | Maze | 20 | 3:01 | SEMAZ |
| 39 | Maze | 20 | 3:01 | SEMAZ |
| 40 | Maze | 20 | 3:01 | SEMAZ |
| 41 | Cyclops Room | 4 | 1:52 | SECEL |
| 42 | Narrow Passage | 4 | 1:52 | SECEL |
| 43 | Treasure Room | 4 | 1:52 | SECEL |
| 44 | Stream | 12 | 1:58 | SEDAM |
| 45 | Reservoir | 12 | 1:58 | SEDAM |
| 46 | Reservoir North | 12 | 1:58 | SEDAM |
| 47 | Reservoir South | 12 | 1:58 | SEDAM |
| 48 | Stream View | 12 | 1:58 | SEDAM |
| 49 | Atlantis Room | - | silent | SECEL |
| 50 | Cold Passage | 3 | 2:11 | SEMIR |
| 51 | Mirror Room | 3 | 2:11 | SEMIR |
| 52 | Mirror Room | 3 | 2:11 | SEMIR |
| 53 | Narrow Passage | 3 | 2:11 | SEMIR |
| 54 | Shaft | 3 | 2:11 | SEMIR |
| 55 | Shaft | 3 | 2:11 | SEMIR |
| 56 | Winding Passage | 3 | 2:11 | SEMIR |
| 57 | Curved Passage | 3 | 2:11 | SEMIR |
| 58 | Chasm | 4 | 1:52 | SECEL |
| 59 | Damp Cave | 6 | 1:53 | SERIV |
| 60 | Deep Canyon | 8 | 2:12 | SEDAM |
| 61 | East-West Passage | 4 | 1:52 | SECEL |
| 62 | Loud Room | 8 | 2:12 | SEDAM |
| 63 | North-South Passage | 4 | 1:52 | SECEL |
| 64 | Round Room | 4 | 1:52 | SECEL |
| 65 | Entrance To Hades | 19 | 2:34 | SEHDS |
| 66 | Land Of The Living Dead | 19 | 2:34 | SEHDS |
| 67 | Dome Room | 2 | 2:19 | SEALL |
| 68 | Egypt Room | 2 | 2:19 | SEALL |
| 69 | Engravings Cave | 2 | 2:19 | SEALL |
| 70 | Temple | 2 | 2:19 | SEALL |
| 71 | Altar | 2 | 2:19 | SEALL |
| 72 | Torch Room | 2 | 2:19 | SEALL |
| 73 | Dam Lobby | 8 | 2:12 | SEDAM |
| 74 | Flood Control Dam | 8 | 2:12 | SEDAM |
| 75 | Control Room | 8 | 2:12 | SEDAM |
| 76 | Aragain Falls | 6 | 1:53 | SERIV |
| 77 | Canyon Bottom | 6 | 1:53 | SERIV |
| 78 | Canyon View | 6 | 1:53 | SERIV |
| 79 | Ledge | 6 | 1:53 | SERIV |
| 80 | Base Of Dam | - | silent | SERIV |
| 81 | End Of Rainbow | 6 | 1:53 | SERIV |
| 82 | On The Rainbow | 6 | 1:53 | SERIV |
| 83 | Frigid River | - | silent | SERIV |
| 84 | Frigid River | - | silent | SERIV |
| 85 | Frigid River | - | silent | SERIV |
| 86 | Frigid River | - | silent | SERIV |
| 87 | Frigid River | - | silent | SERIV |
| 88 | Sandy Beach | 6 | 1:53 | SERIV |
| 89 | Sandy Cave | 6 | 1:53 | SERIV |
| 90 | Shore | 6 | 1:53 | SERIV |
| 91 | White Cliffs Beach | 6 | 1:53 | SERIV |
| 92 | White Cliffs Beach | 6 | 1:53 | SERIV |
| 93 | Bat Room | 31 | 0:36 | SEMINA |
| 94 | Dead End | 18 | 2:33 | SEMINA |
| 95 | Gas Room | 18 | 2:33 | SEMINA |
| 96 | Ladder Bottom | 18 | 2:33 | SEMINB |
| 97 | Ladder Top | 18 | 2:33 | SEMINB |
| 98 | Drafty Room | 18 | 2:33 | SEMINA |
| 99 | Machine Room | 18 | 2:33 | SEMINA |
| 100 | Mine Entrance | 18 | 2:33 | SEMINA |
| 101 | Shaft Room | 18 | 2:33 | SEMINA |
| 102 | Foul Room | 18 | 2:33 | SEMINA |
| 103 | Squeaky Room | 18 | 2:33 | SEMINA |
| 104 | Timber Room | 18 | 2:33 | SEMINA |
| 105 | Coal Mine | 18 | 2:33 | SEMINA |
| 106 | Coal Mine | 18 | 2:33 | SEMINA |
| 107 | Coal Mine | 18 | 2:33 | SEMINA |
| 108 | Coal Mine | 18 | 2:33 | SEMINA |
| 109 | Slide Room | - | silent | SECEL |
