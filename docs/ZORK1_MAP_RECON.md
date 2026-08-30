# Zork I (Sega Saturn, Japan) - map screen reconnaissance

State of the investigation into the in-game auto-map (menu -> map: a stick figure
on a tan canvas with a trail drawn as you explore, plus room-name labels).

## Confirmed

### Savestate mechanics

Mednafen stores Saturn work RAM with **each 16-bit word byte-swapped** relative
to the SH-2 big-endian view. Read a `u16` with `<H`, or swap adjacent bytes
before treating the image as a flat address space. Every address below is a real
SH-2 address; subtract `0x06000000` for a `WorkRAMH` image offset, then apply the
swap.

### Reading SH-2 registers from a savestate

**No manual transcription is ever needed.** The master SH-2 general-purpose
registers are a savestate variable named `R`, 0x40 bytes, sixteen 32-bit
little-endian values R0-R15, stored immediately before the master `PC` variable
(itself 4 bytes little-endian). Walk the variable table with
`zork_ui_rip.savestate_blocks()` and take the first `R` / `PC` pair.

Caveat that cost a round trip: a savestate is a **single instant**. Saving after
stepping through several breakpoints captures the state *after* they have all
passed, not the state at each one. To capture register values at a breakpoint,
the state must be saved while the debugger is halted **at** that break, one slot
per break.

### Live state addresses

| Address | Type | Meaning |
|---|---|---|
| `0x060A597C` | u16 | current room index, 0-109 (keys the room record at `0x06079060`) |
| `0x060B4F50` | SJIS | current room title buffer |
| `0x060B5040` | SJIS | current room description buffer |
| `0x060B2DAC` | struct | CDC play spec: `+4/+5` start track/index, `+12/+13` end, `+16` mode |

Verified against a five-state walk (slots 0-8, alternating map-closed/map-open):
room index read 12 -> 7 -> 8 -> 5 -> 3, i.e. West of House -> North of House ->
Forest Path -> Clearing -> Forest, which matches the room-name fragments visible
on the map in the paired screenshots.

### Map screen art

All render-to-texture into VDP1 VRAM at `cel_charaddr + 0x10000`; geometry from
the 45-entry cel table at `0x06078b74` (`analysis/zork_cels.py`).

| Cel | Size | What |
|---|---|---|
| 31 | 16x24 | the stick figure |
| 32 | 16x15 | the trail stamp drawn along the path |
| 29 | 88x88 | compass ring |
| 30 | 88x88 | compass direction labels |
| 24 | 64x96 | the room-view compass |

### The menu

The in-game menu is a jump-table dispatcher: table at `0x06014ab4`, nine entries,
offsets relative to the table base. The map is one of those nine.

## Ruled out

These were tested and are **not** where the map lives:

* **Cel 28 (160x255)** - exactly the width of the left map panel, so the obvious
  candidate for the canvas. It is **byte-identical across all four map-open
  savestates** (0 of 20400 bytes differ), so it is stale data, not the canvas.
* **A visited-room list in RAM.** Searched both work RAM banks for the ordered
  sequence `12, 7, 8, 5, 3` as u8 and u16 at strides 1-16. Zero hits. The map
  does not keep the visited rooms as a list of room ids in visit order.
* **A per-room map coordinate table in ROM.** Scanned for 110-entry (x, y) arrays
  as u8 and u16 pairs with plausible canvas bounds; all 144 candidates fell inside
  the code region and are instruction bytes, not tables.
* **`GAME.DAT`** - the 5828-byte initial game state is dense, with no zeroed
  region that would serve as an unexplored-map array.
* **VDP2 VRAM** - nearly empty while the map is on screen.
* **Cel drawing.** Opening the menu and map produces exactly **8** `draw_cel`
  breaks, then no further breaks while the map sits on screen. Eight cels cannot
  account for a multi-segment trail plus room-name labels, so the trail is not
  drawn with cels, and the map is drawn once on open rather than per frame.
* **The VDP1 command list.** Parsed from map-open states: only five commands
  (three normal sprites, one polyline, one terminator), and byte-identical
  between the least- and most-explored states. Stale by save time; the map
  geometry is not recoverable from it.
* **The VDP1 framebuffer** does hold the rendered map (it differs by ~1000 bytes
  between consecutive map states, with the non-zero pixel count falling by a
  consistent 960 per move), but most of those pixels are in palette mode rather
  than RGB mode, so colouring them requires resolving VDP2 sprite-type registers.
* **The VDP1 VRAM regions that do change between map states** (`0x029000`-
  `0x032600`, climbing with each state) are the text-glyph sprite uploads moving
  through a bump allocator, not canvas pixels.

## Solved: how the map is drawn

Recovered by breakpointing `draw_cel` (`0x0601b95c`) once per call and saving a
state at each break, then reading `R4`/`R5`/`R6` and `SysRegs[2]` (PR) out of the
savestates. PR gave the caller directly, which located the renderer.

### The renderer

`0x06019AE0`-`0x06019B24` is a straight-line block of eight `draw_cel` calls, and
**every coordinate is a hardcoded immediate**:

| Cel | y | x | What |
|---|---|---|---|
| 26 | -160 | 0 | indirect cel |
| 27 | -160 | 0 | indirect cel |
| 12 | -96 | -16 | 32x32 |
| 28 | 0 | -112 | the canvas |
| 29 | 32 | -80 | compass ring |
| 30 | 32 | -80 | compass labels |
| 31 | -96 | -16 | stick figure |
| 32 | -96 | -16 | trail stamp |

The stick figure therefore **never moves**: it is painted at a fixed screen
position and the map scrolls underneath it. There is no room-to-screen coordinate
mapping in this routine, which is why no such table exists to find.

### Per-room map state

* **`0x060B4830`** - u16 canvas cursor, the live map position.
* **`0x0600AA96`** - on entering a room: `maptbl[room] = cursor | 0x8000`.
* **`0x06072D38`** - 110 x u16 indexed by room. Bit 15 = visited; the low 15 bits
  are the cursor value captured on first visit.
* **`0x06019B84`** - the draw loop: iterates all 110 rooms (`cmp/eq #110`),
  skips any whose table entry is zero, and for each visited room emits its label
  from a 20-byte record (`add #20,r12`) holding up to ten u16 glyph entries,
  zero-terminated.

The cursor splits into two fields per the draw code: `hi = (v >> 8) & 0x7F`
(compared against 11) and `lo = v & 0xFF` (scaled by `0x1800` or `0x3000`
depending on that comparison).

Verified against a five-room northward walk, with the table containing exactly
the rooms visited and nothing else:

| Room | Cursor | hi | lo |
|---|---|---|---|
| 12 West of House | `0x005B` | 0 | 91 |
| 7 North of House | `0x006D` | 0 | 109 |
| 8 Forest Path | `0x0155` | 1 | 85 |
| 5 Clearing | `0x0054` | 0 | 84 |
| 3 Forest | `0x0054` | 0 | 84 |

## Open

Rooms 5 (Clearing) and 3 (Forest) carry the **same** cursor, `0x0054`, in every
savestate checked. So the cursor is not a unique per-room coordinate. Either it
only advances on some transitions, or the two labels are separated by the 20-byte
name record rather than by the cursor.

A save from a room reached by moving **east or west** would show whether the `hi`
field is the axis that changes. Note that the savestates which produced the table
above no longer exist (see the warning at the top of this document), so any
further capture starts from a fresh playthrough.

## Incidental

The bird sound heard in the Forest is **not** a CD track. The play struct shows
track 11 (the forest BGM) still looping with mode `0x0F` in every state of the
walk, so the birds come from the `SEWOD` sound-effect bank. This does not
attribute any of the seven unexplained CD tracks (see `ZORK1_AUDIO_MAP.md`).
