# Zork I (Sega Saturn, Japan) - map screen reconnaissance

State of the investigation into the in-game auto-map (menu -> map: a stick figure
on a tan canvas with a trail drawn as you explore, plus room-name labels).

**Resolved.** The map stores no room coordinates. It recomputes its layout every
time it opens, by walking the room graph outward from the player at 32 pixels
per exit. See "The layout rule" below; `0x06072D38` is a visited bitmap and
nothing more.

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
| `0x060B4F50` | SJIS | current room title buffer (lags one move behind in a savestate; trust `0x060A597C` instead) |
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

* **`0x06072D38`** - 110 x u16 indexed by room. **Bit 15 is the whole payload.**
  The low 15 bits are a snapshot of `0x060B4830` taken at the moment of entry
  and are never interpreted by anything.
* **`0x0600AA8A`** - on entering a room, gated on `*(u16*)0x060A5988 == 0`:
  `maptbl[room] = *(u16*)0x060B4830 | 0x8000`. The write is unconditional, not
  first-visit-only, so a revisit rewrites the same entry.
* **`0x060B4830`** - **not a map variable.** It is referenced from ~110
  constant-pool sites across `0x06040000`-`0x06042700`, the text engine. The
  map borrows it for a nonzero marker and discards its value.
* **`0x06019B84`** - the draw loop: iterates all 110 rooms (`cmp/eq #110`) and
  skips any whose table entry is zero.

### The low bits are never read

Three independent sites touch `0x06072D38`. All three test it against zero and
nothing else:

| Site | Instructions | Use |
|---|---|---|
| `0x06019B8E` | `mov.w @(r0,r1),r1` / `tst r1,r1` / `bf` | `r1` clobbered two instructions later by the label record |
| `0x0601A55E` | `mov.w @r6,r1` / `tst r1,r1` / `bf` | same shape, different function |
| `0x0600AAA6` | `mov.w r1,@(r0,r3)` | the writer |

So the table is a **visited bitmap**, and the low 15 bits are noise.

### Correction: the hi/lo split belongs to the label records

An earlier revision of this document attributed the decomposition
`hi = (v >> 8) & 0x7F`, `lo = v & 0xFF`, scaled by `0x1800` or `0x3000`, to the
cursor. That is the right code read against the wrong variable. The split is
performed at `0x06019BB4` on **`record[i]`**, an entry in the 20-byte per-room
label record, and it computes an *address*, not a coordinate:

```
r4 = *(0x0605E960) + 0x8000 + hi*16 + lo*0x3000
```

The `cmp/hi #11` at `0x06019BE8` selects between the two scales. `hi` and `lo`
are glyph selectors.

### Where the geometry actually lives

The per-room records are built **on the stack** and thrown away when the map
closes:

* `0x06019B86` - `mov r15,r12`, so the record pointer is the stack pointer.
* `0x0601A536` - `shll2` sequence producing `room*20`, indexed off `r15`.

Two full scans of HWRAM across ten-state walks - one with the map closed, one
with the map open - found no per-room coordinate pair anywhere. That is what a
layout computed on open and discarded on close predicts.

### The layout rule

`0x0601A000`-`0x0601A05A` steps a coordinate by **32 units per move**, chosen by
a direction index tested against 2 and 12, offset from `*(0x0605FBB2)`. That
location reads a constant **-16** in all twenty captured states, which is the
same immediate `0x06019AF4` hardcodes for the fixed figure's x.

So the map is recomputed each time it opens, by walking the room graph outward
from the room the player is standing in at 32 pixels per exit, drawing a label
wherever the visited bit is set. Nothing is stored because nothing needs to be:
laying out relative to the player is what lets the figure stay nailed to the
centre while the map scrolls under it.

## Verified against two walks

Ten states each, read with `analysis/zork_savestate.py`.

**Above ground**, W of House -> S -> E -> N -> W -> N -> N -> U -> D -> N:

| Room | | Cursor |
|---|---|---|
| 12 | West of House | `0x005B` |
| 9 | South of House | `0x00D6` |
| 1 | Behind House | `0x00B5` |
| 7 | North of House | `0x006D` |
| 8 | Forest Path | `0x0155` |
| 11 | Up a Tree | `0x0200` |
| 5 | Clearing | `0x0054` |

**Underground**, Living Room -> D -> N -> S -> S -> E -> N -> S -> N -> U:

| Room | | Cursor |
|---|---|---|
| 15 | Living Room | `0x0204` |
| 16 | Cellar | `0x0011` |
| 20 | Troll Room | `0x0010` |
| 17 | East of Chasm | `0x0005` |
| 18 | Gallery | `0x0041` |
| 19 | Studio | `0x0110` |
| 14 | Kitchen | `0x0144` |

Two findings fall out of the pair:

* **Revisits restore a room's value exactly**, within a session - rooms 12, 7,
  8, 16, 18 and 19 each read identically on both visits.
* **The value is path-dependent across sessions.** Behind House reads `0x00B5`
  when reached through South of House and `0x00F5` when reached through North
  of House - a difference of exactly 64, or 2 x 32. A stored identity would not
  do that; a walked position would.

The second observation is the empirical half of the layout rule, and it also
disposes of the old open question about rooms 5 and 3 sharing `0x0054`. They
share it because the value is not a coordinate.

## Incidental

The bird sound heard in the Forest is **not** a CD track. The play struct shows
track 11 (the forest BGM) still looping with mode `0x0F` in every state of the
walk, so the birds come from the `SEWOD` sound-effect bank. This does not
attribute any of the seven unexplained CD tracks (see `ZORK1_AUDIO_MAP.md`).
