# Inventory item pictures — design

The gamepad command panel's inventory overlay lists what the player is carrying
as text. This design adds a picture beside that list: the Japanese Zork I disc
drew each of the game's nineteen treasures as its own 64x80 painting, and those
paintings are still on the disc in `OITEM.CZ`. Carrying the jewelled egg shows
the jewelled egg.

Sub-project C of the decomposition recorded in
[`2026-08-30-zork1-authentic-backgrounds-and-audio-design.md`](2026-08-30-zork1-authentic-backgrounds-and-audio-design.md),
which named it "the 19 `OITEM.CZ` pictures in a fixed pane beside the inventory
list, on NBG1 in VDP2 bank A1". Sound effects (D), the seven unattributed tracks
(E) and the ending art (F) remain out of scope.

Zork I only. Unlike the room backgrounds — where every game draws from Zork I's
picture pool through an assigned per-room table — these pictures are portraits of
*Zork I's own objects*. A crystal skull is not a picture another story's rooms
could be assigned. No other game gains a pane, and none loses anything.

## What makes this cheap

Four facts, all measured rather than assumed.

**The container is already understood, and it is simpler than a `.CGL`.**
`OITEM.CZ` is 40,840 bytes of back-to-back 4-byte-aligned Okumura-LZSS records:
records 0–18 are 5,120-byte 64x80 8bpp pictures, records 19–37 are 512-byte
RGB555 CLUTs, and picture *i* pairs with CLUT *19+i*. Confirmed by decoding all
38 — every declared size matched its expansion exactly. This is the same LZSS
`cgl_decode` already implements, so the hard part is ported.

**Nothing new is downloaded and nothing copyrighted is committed.** `OITEM.CZ` is
on the same data track `music.bat` already fetches for its CD audio and discards.
It joins `BG_MANIFEST` and rides the `/BG` injection built for the room
archives — no new script, no new source of bytes.

**The picture set is provably the treasure set.** `1dungeon.zil` gives 22 objects
carrying a `TVALUE`. Drop `SWORD` (TVALUE 0) and the damaged variants
`BROKEN-EGG` and `BROKEN-CANARY`, and exactly 19 treasures remain, against
exactly 19 pictures. That turns the binding from an open identification problem
into a 19↔19 assignment, of which 17 cells fall out unambiguously by eye.

**The pane needs no transparency work.** None of the 19 pictures uses palette
index 0 — each carries its own opaque background index (32, 50, 182, 255, …),
verified by histogram. So the picture paints its own black plate, and the rest of
the NBG1 container stays index 0, which VDP2 already treats as transparent by
default. Nothing is remapped and no pixel is modified: the bytes on screen are
the bytes on the disc.

## The data

### On the disc

One line in `tools/extract_bg.py`:

```python
"OITEM.CZ": (40840, "04344f3bbc6404ab6163e0d2df16614e4fc67d53855a1472baab3cfe9f54a2e0"),
```

Measured off the reference disc under `cd/` and confirmed byte-identical to the
tracked reverse-engineering copy in `analysis/zork_bg/raw/OITEM.CZ`.

Everything downstream is manifest-driven and needs no edit:

| Stage | Mechanism |
|---|---|
| Stage | `bg.bat` → `extract_bg.py -o tools/assets/BG`, mirrored into `saturn/cd/data/BG` |
| Inject | `games.bat`'s existing single xorriso commit, `-map` for `/BG` |
| Verify | `full-image.yml` reads every `BG_MANIFEST` entry back off the finished image and compares size and SHA-256 |

The hash is load-bearing rather than defensive, for a sharper version of the
reason it is on the `.CGL` archives: the runtime holds measured byte offsets into
this archive. A different disc revision would not fail to open — it would
decompress from the wrong offset and show garbage, or hang the LZSS loop.

### Record offsets

`saturn/src/scene/oitem_records.inc`, generated: the 38 measured (offset, length)
pairs. Not scanned at runtime, because scanning means decompressing every earlier
record to reach record *n* — the exact cost `game_presentation.inc`'s per-frame
offsets already exist to avoid.

### The binding

`tools/assets/zork1_items.json` maps picture index to object **name**:

```json
{ "7": "jewel-encrusted egg", "3": "golden clockwork canary", ... }
```

Names rather than object numbers, because a name is checkable by a human reading
the diff and a number is not. Same reasoning as `tools/assets/zork1_room_aliases.json`.

The full table, all nineteen treasures used exactly once:

| # | Object | Obj | # | Object | Obj |
|---|---|---|---|---|---|
| 00 | sceptre | 209 | 10 | pot of gold | 137 |
| 01 | gold coffin | 208 | 11 | painting | 149 |
| 02 | sapphire-encrusted bracelet | 125 | 12 | beautiful jeweled scarab | 116 |
| 03 | golden clockwork canary | 84 | 13 | beautiful brass bauble | 85 |
| 04 | platinum bar | 139 | 14 | chalice | 191 |
| 05 | leather bag of coins | 165 | 15 | crystal skull | 231 |
| 06 | huge diamond | 171 | 16 | torch | 104 |
| 07 | jewel-encrusted egg | 87 | 17 | crystal trident | 188 |
| 08 | large emerald | 163 | 18 | trunk of jewels | 101 |
| 09 | jade figurine | 170 | | | |

Object numbers are shown for reference only; the JSON carries names and the
generator resolves them.

Two of these were a judgement call rather than a reading, and it is worth
recording which so a later reader does not mistake them for measurement. `#00`
(a straight white shaft with a banded head and a blunt butt) and `#13` (a thin
tapering rod topped by a polished brass sphere) are the sceptre and the bauble in
one order or the other. The ZIL's long description — "an ornamented sceptre,
tapering to a sharp point" — argues for `#13`. The objects themselves argue the
other way: a sceptre is a staff and a bauble is a small shiny brass trinket. The
owner called it on the objects. If the pane ever shows a bauble that looks like a
staff, this is the row to swap.

`broken jewel-encrusted egg` (86) and `broken clockwork canary` (83) are
deliberately **unbound**. A broken egg is not a picture of an unbroken one, so
they take the blank plate.

`tools/gen_items.py` emits `saturn/src/scene/game_items.inc` and **refuses**
rather than writing a zero on any of:

- a name matching zero objects, or more than one, in `ZORK1.Z3`
- a duplicate picture index, or a duplicate object
- a picture index outside 0–18
- a story whose release and serial are not 88 / `840726`

A zero would show up only as a pane that silently fails to change, which is the
same failure the room table's refusals exist to prevent. Regenerates
byte-identically; `.gitattributes` already pins `saturn/src/scene/**` to `eol=lf`.

## The decoder

`saturn/src/video/oitem.c` / `.h`, beside `cgl.c`. Pure logic — no SRL, no disc,
no VDP2 — so the host tests link it with plain gcc and the port is proved before
it runs on hardware once, exactly as `cgl.c` was.

`cgl.c` grows a record-agnostic entry point (the LZSS expansion with the palette
step removed) and `oitem.c` is the layout on top of it: given the archive, a
picture index 0–18, and the generated record table, fill a 5,120-byte pixel
buffer and a 256-word CRAM palette. It refuses on the same conditions
`cgl_decode` does — a null argument, a record too short, a declared size of zero,
a declared size larger than the destination — returning 0, which every caller
reads as "hold the picture already showing".

`cgl_palette`'s conversion is reused unchanged: the CLUT records are the same
256-entry RGB555 little-endian format, so it is a little-endian read plus the
opaque bit, with no channel arithmetic.

## The runtime

`saturn/src/video/item_art.cxx` / `.h`, beside `room_art.cxx`, following its
policy exactly.

### Memory

| Held | Bytes | When |
|---|---|---|
| `OITEM.CZ` | 40,840 | while the overlay is open |
| decoded picture | 5,120 | while the overlay is open |
| CRAM palette | 512 | while the overlay is open |
| | **46,472** | peak |

Against `room_art`'s 486 KB peak, this is small, but it is not free and it is not
permanent: the archive is read when the overlay opens and freed when it closes.
`saturn/tests/test_lwram_budget.py` gains the term.

Decoding happens on cursor move, not per frame. A 5 KB LZSS expansion is cheap;
sixty of them a second is not.

### Failure

Every failure holds what the pane is showing and says nothing on screen: no game
set, an unbound object, an archive that will not open, a read that comes up
short, a stream that will not decode. Art is decoration; a failed load must never
blank the screen or stop the game.

Reading `/BG` steps out of the story directory, so `item_art` owes the same CD
restore on the way back that every post-selection detour owes.

### The layer

NBG1, 8bpp, in a 512x256 container — 131,072 bytes, one VRAM bank. NBG0's
wallpaper holds A0 and the dashboard cells hold part of B0, so SRL's
`AutoAllocateBmp` falls through to **A1**, the bank the input-dashboard design
left free "for whatever wants a bitmap next".

The picture is written into the container **at its own screen offset** — x=216,
y=144 — with the layer positioned at (0,0). There is no scroll arithmetic, and
the rest of the container stays index 0. Index-0 transparency is VDP2's default
and is left alone: `TransparentDisable()`, which `main.cxx:361` calls on NBG0 so
a room frame's index 0 paints as a real colour, must **not** be called here.

Priority 3: above NBG2's marble (2) and NBG0's wallpaper (1), below NBG3's text.
`dash_view.cxx` already sets NBG0 to 1 and NBG2 to 2; this adds
`slPriorityNbg1(3)` beside them.

Blanking the pane is writing zeros over the 64x80 region — which is what an
unbound object gets, and what `item_art_hide()` does.

### The risk, and the mitigation

A second VDP2 bitmap layer costs VRAM access cycles, and the pattern with
NBG0-bitmap + NBG1-bitmap + NBG2-tiles + NBG3-tiles may not be satisfiable. The
failure mode is silent: a layer that does not draw, or draws as static.

**The first phase of implementation is a spike** — bring NBG1 up with one
hardcoded picture and look at it in Mednafen, before anything else is built. If
the pattern refuses, the fallback is NBG1 as an 8bpp **tilemap**: 64x80 is 80
tiles of 8x8, 5,120 bytes of cell data plus a small map, which fits in the
leftover of an existing bank and costs fewer cycles. Same pixels, different
arrangement; only `item_art`'s upload changes, and nothing else in this design
moves.

## The overlay

### Geometry

The screen is 40x30 cells of 8 pixels. In game the transcript holds rows 1–19 and
the input line is row 20. Below that the strip runs rows 21–29 — its own top
border at 21, seven content rows at 22–28, its bottom border at 29 — and the
inventory overlay draws its own seven-row box inside that content area, at rows
22–28. A 64x80 picture needs ten rows of interior, which neither has, so while
the overlay is up everything above the strip's bottom border rises by five.

| | Today | With the pane |
|---|---|---|
| input line | row 20 | row 15 |
| strip (the marble slab) | rows 21–29 (9) | rows 16–29 (14) |
| overlay box | rows 22–28 (7) | rows 17–28 (12) |
| box interior | rows 23–27 (5 items) | rows 18–27 (10 items, 80px) |
| columns | 2–35 | 2–35, unchanged |

Interior columns split: list text 3–25 (23 chars), divider 26, picture pane 27–34
(8 columns = 64px). The box keeps its current width and its current bottom edge:
row 29 is the strip's bottom border and does not move, which is what makes this a
rise rather than a resize.

`CV_OVERLAY_ROWS` goes from `CV_STRIP_ROWS - 2` (5) to 10. A new
`DASH_OVERLAY_TALL` row in `dash_map.c`'s `g_geom` carries 14 rows against
`DASH_OVERLAY`'s 9, and takes base row 16 rather than 21.

Nothing has to repaint the five borrowed transcript rows when the overlay closes.
`render_console()` clears and rewrites all of rows 1–19 on every frame, and
`saturn_glue.cxx:595` calls it immediately before `render_command_panel` — so the
transcript comes back on the first frame the box is not drawn.

### Conditional on the story

A story with no bound items keeps today's 9-row, 5-item overlay. The 12-row
overlay with the pane appears only when the running story is release 88 /
serial `840726`. Thirty games would otherwise show eight columns of permanently
dead black, and the layout should say something true: this game has pictures and
that one does not.

This means two overlay geometries and one branch in `render_command_panel`,
keyed on `item_art_available()`.

### Ownership of the layer

`render_command_panel` calls `item_art_show(m.carried[p.cursor])` while the
overlay is up and `item_art_hide()` on every other path — the panel without the
overlay, the keyboard interface, a menu, the map. One thing is on this layer at a
time, the contract `dash_map` already holds for NBG2.

### What deliberately does not change

`dash_view.cxx`'s `flush_hook` raises the wallpaper by `console_strip_shift()`
(36px, half the strip's nine rows) so the picture stays centred in what the strip
leaves visible. A 12-row box hides more of it, but tracking that would make the
wallpaper jump every time the player opens their inventory — underneath an opaque
box, where the movement buys nothing. The shift stays at 36.

The list field narrows from 31 characters to 23. `cv_overlay_row_text` already
truncates cleanly and `full[16]` caps the recovered spelling anyway, so no
carried item's name is newly cut.

### Netbin

`src/video/command_view.cxx` is in the netbin source list; `room_art.cxx` is not.
The `item_art` include and its call sites go behind `#ifndef NETBIN`, the same
guard `dash_view.cxx:16` uses for the wallpaper. The netbin has no CD, and the
size gate would not survive an art path.

## Tests

- **`oitem.c` on the host decodes all 19 pictures** and matches the reference
  PNGs already tracked in `analysis/zork_ui/items/`. This proves the port before
  it runs on hardware once, the pattern `cgl.c` established.
- **`gen_items.py` refuses** each of its five conditions, and regenerates
  `game_items.inc` byte-identically.
- **The binding is complete and disjoint**: all 19 picture indices used, all 19
  resolved objects distinct, and every one of them carries a `TVALUE` in the
  story.
- **`test_lwram_budget.py`** gains the 46,472-byte term.
- **Overlay layout**: a host test that the box, the list field and the pane do not
  overlap, that the pane is exactly 8x10 cells, and that a story without bound
  items gets the 9-row geometry.
- **`syntax-check.sh` clean** for both the CD and netbin configurations, with the
  netbin size gate unmoved.

## Relationship to other work

Continues [`mem/2026-08-31-cgl-only-presentation-handoff.md`](../../../mem/2026-08-31-cgl-only-presentation-handoff.md),
which built the `/BG` injection this rides on, and
[`mem/2026-08-31-marbled-menus-handoff.md`](../../../mem/2026-08-31-marbled-menus-handoff.md),
which is why the pane sits on marble rather than on a transparent box.

Discharges sub-project C of
[`2026-08-30-zork1-authentic-backgrounds-and-audio-design.md`](2026-08-30-zork1-authentic-backgrounds-and-audio-design.md),
with one correction to that document's framing: it assumed the 19 would need
"identifying by eye and bound to object numbers", and expected their names to be
undecodable. The `TVALUE` count settles the *set* by measurement; only two cells
within it were a judgement call.

Not attempted here, and deliberately: the other thirty games get no item art, and
the room-contents half of `RoomModel` (`here` / `nhere`) is untouched — the
overlay lists carried items and that is what gets a picture.
