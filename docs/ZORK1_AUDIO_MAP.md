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
| `0x06048adc`, `0x06048b32` | loop | a byte read through a pointer argument |
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

| Rank | Track |
|---|---|
| (Japanese string) | 21 |
| Amateur Adventurer | 22 |
| Novice Adventurer | 23 |
| Junior Adventurer | 24 |
| Adventurer | 26 |
| Master | 27 |
| Wizard | 28 |
| Master Adventurer | 29 |

Track 25 (0:06) never appears in a table but the sound handler compares against it explicitly
at `0x06048a7c` and `0x06048b92`, so it is reached through the byte-pointer path.

## Tracks

Disc layout is one MODE1/2352 data track plus 31 CD-DA tracks (2-32). Twenty-four of the 31
are attributed below; seven are not.

| Track | Length | Rooms | Used for |
|---|---|---|---|
| 2 | 2:19 | 6 | room BGM, looped by 6 rooms; script music command 6 |
| 3 | 2:11 | 8 | room BGM, looped by 8 rooms |
| 4 | 1:52 | 12 | room BGM, looped by 12 rooms; script music command 0 |
| 5 | 2:21 | - | script music command 1 |
| 6 | 1:53 | 12 | room BGM, looped by 12 rooms |
| 7 | 1:48 | - | UNATTRIBUTED - needs a runtime capture |
| 8 | 2:12 | 5 | room BGM, looped by 5 rooms |
| 9 | 1:18 | - | script music command 2 |
| 10 | 1:15 | 7 | room BGM, looped by 7 rooms; script music command 3 |
| 11 | 1:51 | 8 | room BGM, looped by 8 rooms; script music command 4 |
| 12 | 1:58 | 5 | room BGM, looped by 5 rooms; script music command 5 |
| 13 | 1:49 | - | UNATTRIBUTED - needs a runtime capture |
| 14 | 2:38 | - | UNATTRIBUTED - needs a runtime capture |
| 15 | 1:57 | - | UNATTRIBUTED - needs a runtime capture |
| 16 | 1:58 | - | UNATTRIBUTED - needs a runtime capture |
| 17 | 2:18 | - | UNATTRIBUTED - needs a runtime capture |
| 18 | 2:33 | 15 | room BGM, looped by 15 rooms |
| 19 | 2:34 | 2 | room BGM, looped by 2 rooms |
| 20 | 3:01 | 19 | room BGM, looped by 19 rooms |
| 21 | 0:32 | - | rank screen: (Japanese) |
| 22 | 0:33 | - | script music command 7; rank screen: Amateur Adventurer |
| 23 | 0:28 | - | rank screen: Novice Adventurer |
| 24 | 0:29 | - | rank screen: Junior Adventurer |
| 25 | 0:06 | - | special-cased at 0x06048a7c and 0x06048b92 |
| 26 | 0:34 | - | rank screen: Adventurer |
| 27 | 0:25 | - | rank screen: Master |
| 28 | 0:31 | - | rank screen: Wizard |
| 29 | 0:39 | - | rank screen: Master Adventurer |
| 30 | 4:18 | - | played once from 0x0600b9f2 |
| 31 | 0:36 | 1 | room BGM, looped by 1 room |
| 32 | 4:18 | - | UNATTRIBUTED - needs a runtime capture |

### Still open

Tracks 7, 13, 14, 15, 16, 17 and 32 are reached only through the byte-pointer path at
`0x06048adc` / `0x06048bbe`, where the track number is a data byte the static trace cannot
resolve to a trigger. Their lengths are suggestive but not proof: 7 and 13-17 run 1:48-2:38
like the room themes, and 32 is 4:18 (the same length as 30, but a different file, and its
volume-table entry is 0). Pinning these down needs a runtime capture - see the breakpoint
procedure in the session notes.

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
