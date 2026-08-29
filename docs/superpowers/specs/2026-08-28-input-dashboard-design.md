# Design: input dashboard — a marble panel behind the three input strips

**Date:** 2026-08-28
**Status:** Approved, pending implementation plan
**Extends:** `docs/superpowers/specs/2026-08-10-controller-command-interface-design.md`
**Touches, but does not change:** `2026-07-18-display-options-design.md` — the
player's text colour still comes from CRAM entry 1 and is unaffected here, but
see *Contrast risk* below.

## Goal

Give the on-screen input controls a drawn background instead of a black cutout:
a dark grey stone-marble panel with a bevelled outer frame, a drop shadow, and
grooved edges between the modules. Three sizes, one per input configuration —
gamepad with the command panel, gamepad with the on-screen keyboard, and a real
keyboard — each framing exactly the rows that configuration already occupies.

## What is on screen today

All three strips are ASCII on the text layer over a black rectangle. There is no
background art path for them at all.

`image_window_box()` (`saturn/src/video/console_view.cxx:100`) opens VDP2
window 0 over the strip and `image_window_on()` sets `WCTL` so NBG0 — the
wallpaper — is suppressed inside it. The modules are then read against the back
plane. The frames themselves are two printed strings:

- `CV_BORDER` (`saturn/src/video/command_view.cxx:53`), 40 columns, dividers at
  0, 14, 30, 39.
- `KB_STRIP_BORDER` (`saturn/src/video/console_view.cxx:695`), 39 columns,
  dividers at 0, 14, 38.

and a `|` printed per content row at each divider column
(`command_view.cxx:873` onward, `console_view.cxx:715` onward).

`console_height()` (`console_view.cxx:178`) reserves the rows: `avail - 1` with a
real keyboard, `avail - (1 + CV_STRIP_ROWS + 2)` for either in-game gamepad
interface, `avail - (1 + KB_ROWS)` for the off-game terminal.

## Why NBG2

NBG2 is unused, is a cell layer, and sits in a VRAM bank the wallpaper does not
touch. The three alternatives were considered and rejected:

**The text layer (NBG3).** Cheapest — no new layer, no VRAM risk, and it would
ride the flush discipline `text_map.h` already documents. Rejected on capacity.
Font 0 is a 128-tile block (`saturn/src/video/text_map.cxx:100`) and every code
in it is spoken for: `0x00–0x1F` are `glyph_invert`'s 32 reverse-video scratch
slots, `0x20–0x7E` are printable ASCII, `0x7F` is the block cursor
(`install_block_glyph`, `console_view.cxx:329`). A marble tileset would have to
evict roughly ten rarely-printed ASCII codes or shrink the scratch cache, and
everything would still be flat 8x8 blocks with no sub-cell shading — no bevel,
no shadow, which is most of what was asked for.

**VDP1 sprites.** A scaled nine-slice would make resizing free and touch no VDP2
bandwidth. Rejected because the panel is static: it would rebuild a command list
every frame to redraw an image that changes only when the player toggles
interface, and it would need sprite priority slotted below NBG3 to sit under the
text. A cell layer costs nothing per frame and composites under text by
construction.

**NBG1.** Equally free, but it is a `BmpScreen` (`srl_vdp2.hpp:999`) and the
natural way to use it is a bitmap, which is the expensive way to store a panel
that is mostly one repeating texture. NBG2 (`srl_vdp2.hpp:1060`) is tilemap-only
and is the right shape for the job. NBG1 stays free for whatever wants a bitmap
next.

NBG2's one documented restriction — unavailable when NBG0 is RGB555 — does not
bite: NBG0 here carries a 256-colour paletted wallpaper.

## VRAM and CRAM budget

SRL splits VDP2 VRAM into four banks and caps the top of B1 at
`VDP2_VRAM_B1 + 0x18000` (`srl_vdp2.hpp:93`), which is where font 0 begins, so
the allocator never walks into the font.

NBG0's wallpaper is loaded as a bitmap. `Init(BitmapInfo&)` builds the SGL size
flag as `height <= 256 ? 0x2 : 0x6` with no width bit below 512, so a 320x224
8bpp picture is stored as 512x256 — 131,072 bytes, one whole bank.

| Bank | Occupant | Free |
| --- | --- | ---: |
| A0 | NBG0 wallpaper bitmap (512x256, 8bpp) | 0 |
| A1 | — | 128 KB |
| B0 | **NBG2 dashboard (new)** | ~111 KB after |
| B1 | NBG3 font 0 at `+0x18000`, map below | ~96 KB |

Putting NBG2 in **B0** is the load-bearing decision: it keeps the dashboard's
pattern-name and character-pattern fetches in a different bank from the
wallpaper's bitmap fetches, which is what makes the access-cycle patterns
satisfiable. This is why `dash_init()` calls
`VRAM::Allocate(size, boundary, VramBank::B0, cycles)` (`srl_vdp2.hpp:128`)
directly rather than letting `AutoAllocateCell`/`AutoAllocateMap` choose — the
auto path tries A0 first for cells (`srl_vdp2.hpp:164`), which is exactly the
bank that must stay clear.

CRAM is already partitioned. NBG3 is 4bpp on palette 0, entries 0–15, of which
entry 1 is the glyph foreground, 2 the reverse-video punched letter, and 15 the
block cursor (`saturn/src/menu/options.cxx:61`). The wallpaper's 256-colour
palette is loaded into bank 1 at entries 256+. Entries 16–255 are unclaimed, so
the dashboard takes **palette index 1, entries 16–31**.

Allocation sizes:

| Item | Size |
| --- | ---: |
| Character patterns, 35 tiles x 32 B (4bpp, 1x1 cells) | 1,120 B |
| Pattern name table, 64x64 cells, 2-word | 16,384 B |
| **Total in bank B0** | **~17 KB** |

Two-word pattern names rather than one-word: one-word packs the palette and the
upper character bits into supplementary register fields, and the cell base then
has to satisfy an alignment the allocator does not promise. Two-word gives a
full character number and an explicit palette per cell for 8 KB more in a bank
with 111 KB spare, and removes a class of bug that would show up as the whole
panel drawn in the wrong palette. Confirm SRL's `TilemapInfo::MapMode` value for
two-word during implementation.

## The four variants

One table in `dash_map.c` is the whole answer to sizing. Rows are given relative
to `base`, the row the input line already occupies; columns are absolute screen
cells. Every gamepad variant is ten rows: the input well, then the nine-row box
(two frame rows and `CV_STRIP_ROWS` = 7 content rows).

| Variant | Rows | Frame | Module rects (cols) |
| --- | ---: | --- | --- |
| `DASH_PANEL` — gamepad, command panel | 10 | 0–39 | rose 1–13, word 15–29, command 31–38 |
| `DASH_GAMEKB` — gamepad, on-screen keyboard | 10 | 0–38 | rose 1–13, keys 15–37, plus an inner rule across the keys module at content row 2 |
| `DASH_LINE` — real keyboard | 1 | 0–39 | none |
| `DASH_OVERLAY` — gamepad, command panel with the inventory overlay up | 10 | 0–39 | one undivided field, 1–38 |

`DASH_OVERLAY` is `DASH_PANEL`'s rectangle with the dividers removed. The
overlay draws its own 34-column box across all seven content rows from
`CV_OVERLAY_X` = 2 and prints no verticals of its own; its interior is mostly
spaces and NBG3 leaves palette entry 0 transparent, so the marble's grooves at
columns 14 and 30 would show through the item list and cut the box into three.
`render_command_panel` selects it whenever `p.overlay` is set — the row in the
table this design anticipated for a later variant, arriving earlier than
expected.

**Each variant reproduces its own current column geometry exactly, including the
disagreement between them.** The command panel closes at column 39, the keyboard
strip at 38. Nothing printed by either renderer moves. Making the two right edges
agree is a separate change and is deliberately not bundled here — it would shift
the keyboard module and is a behaviour change the player would see.

The keyboard module's content is unaffected either way: keys draw at
`17 + col * 2` with `GKB_COLS` = 10, so the last key lands on column 35, well
inside the 15–37 rect.

The input line joins the panel. In every gamepad variant the marble starts at
`base` — the prompt sits in its own flat well above the module boxes, so the
strip reads as one instrument cluster rather than a floating prompt over a box.
`console_height()` is unchanged, because the input row already existed in every
variant; nothing reflows.

## Row anatomy

The tile set follows from where the frame edges actually fall, which is not
obvious: the panel is ten rows and the box was nine, so there is no row left
over for an outer top edge — the only cells it could occupy are the input
row's own, and those already carry the prompt. It therefore has none. Reading a
gamepad variant down from `base`:

| Row | Contents |
| ---: | --- |
| `base` | input well — flat marble (`DT_WELL_L` / `DT_WELL` / `DT_WELL_R`), no top bevel, text printed over it |
| `base+1` | horizontal groove: floor of the well, ceiling of the modules, and where each vertical divider begins |
| `base+2` .. `base+8` | content rows — left edge, marble field, vertical dividers, right edge |
| `base+9` | outer bottom edge, where each vertical divider ends |

The `base` row's flatness is the hardware fix, not an economy. The bevel shares
its cells with the input line's glyphs and ran through them, so it is gone and
the row is plain marble end to end (`cell_at`, `dash_map.c`). The panel loses
its outer top highlight; the groove at `base+1` still floors the well, so the
input line still reads as recessed rather than as unframed. The same fix moved
the printed text off the row's two end caps as well — see *Call-site changes*.

`DASH_GAMEKB` has one extra rule: `render_game_keyboard` prints a horizontal
`-----` across the keys module at content row 2, separating the number row from
the letters. That is chrome, not content, so the dashboard draws it as an inner
groove and the renderer stops printing it — otherwise it would be exactly the
competing-frames problem this design exists to remove.

`DASH_LINE` is one row, and with no top bevel anywhere it wants exactly what
every other variant's first row wants: a flat well, capped at each end. It uses
tiles 32–34 and so does the `base` row of all three ten-row variants — the
"three tiles of its own" this design first gave it are shared by everything.

## Tile vocabulary

35 tiles, index 0 reserved:

| Index | Count | Role |
| ---: | ---: | --- |
| 0 | 1 | fully transparent — every cell outside the strip |
| 1–16 | 16 | marble field, addressed `field[(y & 3) * 4 + (x & 3)]` |
| 17–20 | 4 | outer corners: TL, TR, BL, BR — 17 (TL) and 18 (TR) retained but dead |
| 21–24 | 4 | outer edges: top, bottom, left, right — 21 (top) retained but dead |
| 25 | 1 | horizontal groove body — serves both the well floor and the inner rule |
| 26–27 | 2 | groove meeting the left edge, groove meeting the right edge |
| 28 | 1 | vertical divider body |
| 29–30 | 2 | divider T-down (groove above, divider begins), T-up (bottom edge, divider ends) |
| 31 | 1 | divider–groove cross, where an inner rule leaves a vertical divider |
| 32–34 | 3 | flat well: left cap, body, right cap — every variant's top row |

The 4x4 field patch gives a 32-pixel repeat on both axes, which is enough that
the stone reads as stone rather than as an 8x8 grid — the single failing of the
plain nine-slice this replaces.

**Indices 17, 18 and 21 are retained but unreachable.** Losing the top bevel
left `cell_at` with no path that returns `DT_CORNER_TL`, `DT_CORNER_TR` or
`DT_EDGE_TOP`: every variant's first row is a well row. They are not renumbered
away because the tile index *is* the character number the pattern name carries —
`dash_view.cxx` uploads the set in enum order and adds `DT_*` to `g_char_base` —
so a renumber would silently move every tile above the hole, and the three dead
cells cost 96 bytes in a bank with 111 KB spare. They are also the tiles a
future variant that does own its top row would want back.

Indices 32–34 are no longer a `DASH_LINE`-only bar. They are the shared well
tiles every variant's top row uses, and two of the three are byte-identical to
edge tiles the generator already emits: 32 is `apply_left(solid)` like 23, and
34 is `apply_right(solid)` like 24. Only 33 — plain body, no bevel at all — is
unique to the well. The duplication is deliberate: the generator states each
tile by its role rather than by its bytes, so the day the well grows a cap the
edges do not have, nothing else moves.

## Palette and bevel convention

Sixteen entries at CRAM 16–31, a cool grey ramp — components out of 31, packed
into the same Saturn RGB555 word `text_set_color` writes:

| Entry | Role | R, G, B |
| ---: | --- | --- |
| 0 | transparent | — |
| 1 | drop shadow | 2, 2, 3 |
| 2 | groove, deep | 4, 4, 6 |
| 3 | groove | 6, 6, 8 |
| 4 | shadow face | 8, 8, 10 |
| 5–9 | stone body | 10,10,12 to 15,15,17 |
| 10–12 | veining | 16,16,18 to 20,20,22 |
| 13–14 | bevel highlight | 22,22,24 / 24,24,26 |
| 15 | specular edge | 27, 27, 29 |

The blue channel runs two steps above red and green throughout, which is what
makes the grey read as stone rather than as television grey.

Light comes from the top left. The ramps are **two pixels deep, not three**:
lit edges carry 15 then 13, shaded edges 2 then 1, and grooves 2 then 13, with
the darkest entry in the edge tile's own final pixel row and column. The
generator centres a groove by width rather than by a fixed offset, which is why
thinning it left the divider tiles byte-identical. Entries 4 and 14 stay in the
palette unused by any ramp, so widening one again is a generator edit and not a
palette one. Module dividers are grooves rather than lines: dark on the left
face, light on the right, so the boundary between modules reads as a cut in the
stone.

**The shadow is a contact shadow inside the panel, not a ring outside it.** A
ring falling on the wallpaper would read better, and it is not available: the
ten-row variants sit at screen rows 18–27 and `DASH_PANEL` closes at column 39,
so the panel already occupies the last row and the last column. There is no cell
left to cast into, and taking one would mean shrinking `console_height()` and
reflowing the story text — out of all proportion to three pixels of shadow.

## Components

Three new files in `saturn/src/video/`, following the split this project already
uses for video work — pure arithmetic apart from the hardware, so the arithmetic
can be tested on the host (`bg_dim.c` / `title.cxx`, `command_panel.c` /
`command_view.cxx`).

**`dash_map.c` / `dash_map.h`** — pure logic, includes no SRL header. Owns the
variant enum, the rectangle table above, a work-RAM shadow of the map, and a
dirty span. The shadow is 40 columns by 32 rows, one byte of tile index per cell:
40 is what a 320-pixel screen shows and 32 covers the 28 rows the program draws
on, matching `text_map`'s `TEXT_ROWS`. The hardware map's 64-cell pitch and the
2-word pattern name format are `dash_view`'s business, not this file's, which is
what keeps the tests readable. `dash_build(variant, base_row)` paints the shadow;
`dash_frame_end()` takes the panel down when a frame passed without one. Nothing
in this file may include `srl.hpp`, for the same reason `bg_dim.h` says so.

**`dash_view.cxx` / `dash_view.h`** — the hardware half. `dash_init()` allocates,
uploads, sets the palette and priority, and enables NBG2. `dash_set()` is the
call sites' entry point: it forwards to `dash_build`. The `OnAfterSync`
subscriber closes the frame and then copies the dirty rows, expanding each tile
index into its 2-word pattern name on the way out.

**`dash_tiles.c`** — generated tile and palette data. File header only, per the
comment rules for generated files.

## Data flow

Nothing changes per frame. The renderers `text_print` into the text shadow and
`flush_hook` pushes it on `OnAfterSync` exactly as now.

The dashboard touches VRAM only when the layout changes — an interface toggle,
entering or leaving a game, or a device swap. `dash_set()` rebuilds the shadow
and marks it dirty; `dash_flush()` is registered on the same `OnAfterSync` hook
and copies the dirty span during vblank. A steady frame costs nothing; a layout
change copies at most 40 x 10 x 4 = 1,600 bytes.

Riding the existing hook rather than writing at draw time is not optional. It is
the same tearing hazard `text_map.h`'s file header describes for the tilemap and
`flush_hook` describes for glyph tiles: VDP2 re-reads a cell's pattern name on
every scanline of that cell's row, so a store landing mid-row shows one tile
above the beam and another below it.

`dash_set()` is idempotent. Called with the variant and base row already in the
shadow it returns without marking anything dirty, so the renderers may call it
unconditionally every frame and the common path stays free.

**The panel expires rather than being hidden explicitly.** A printed border
disappeared for free — a menu cleared the text rows and the frame went with them.
A cell layer does not: once painted it stays painted, so without this the marble
would sit behind every menu, the Options pages and the title screen. Adding a
`dash_hide()` call to each screen that leaves the console view would be a list
that goes wrong the moment someone adds a screen. Instead `dash_frame_end()` runs
once per frame from the flush hook, and if no `dash_set` arrived during that frame
it builds the empty variant. The panel therefore vanishes one frame after the
console view stops drawing, and no other screen has to know the dashboard exists.

That rule reads "stopped drawing" as "stopped being displayed", and the prompt
loop is the one place those differ: `saturn_readline` advances a frame at the
turn boundary, and `run_room_transition` advances up to `MUSIC_FADE_FRAMES * 2`
of them across the fade, both with the console view still on screen and no
renderer in between. `on_text_category` clears only the console's own rows, so
the strip's rows keep the previous turn's text through all of it.
`hold_input_strip` (`saturn/src/engine/saturn_glue.cxx`) therefore claims the
panel with the arguments the renderers would have used before each of those two
Synchronizes. This is not the rejected `dash_hide()` list inverted: it is two
calls in the one function that owns the turn boundary, and every other screen
still knows nothing.

Scroll position is fixed at (0, 0) so map cell *(x, y)* is screen cell *(x, y)*
and `dash_build` can write absolute rows.

## Bring-up

`dash_init()` runs in `main()` after `text_map_init()`
(`saturn/src/main.cxx:342`), which is after `SRL::Core::Initialize`
(`main.cxx:337`) — VDP2 and the font are guaranteed up by then, and the
dashboard has no reason to exist earlier. In order:

1. `VRAM::Allocate` the character patterns and the pattern name table in
   `VramBank::B0`. Either returning null aborts the whole init.
2. Upload the 31 tiles; blank the pattern name table to index 0.
3. Load the 16-entry palette to CRAM index 1 (entries 16–31).
4. `slPriority` so NBG3 is above NBG2 and NBG2 is above NBG0.
5. `NBG2::SetPosition(0, 0)`, then `NBG2::ScrollEnable()`.
6. Register `dash_flush` on `OnAfterSync`.

## Fallback

If either allocation fails, `dash_init()` returns false and sets `dash_ready` to
0. `dash_set()` then becomes a no-op, and the renderers print `CV_BORDER`,
`KB_STRIP_BORDER`, their `|` dividers and the keyboard module's `-----` rule
exactly as they do today.

**None of those strings are deleted — they are gated behind `dash_ready`.** This
is what keeps the netbin build (which links neither `title.cxx` nor this file)
and any future VRAM pressure on a working path, and it means the change cannot
make the strip worse than it is now, only better.

The netbin needs one more thing than the runtime flag, because `console_view.cxx`
*is* in its source list and will now call `dash_set`. Rather than add the
dashboard files to that list — which `saturn/tests/test_netbin_sources.py` pins
at exactly 27 objects, and which would put bytes into a size-gated build for a
feature it does not use — `dash_view.h` defines all three entry points as
`#ifdef NETBIN` no-op inlines, with `dash_ready()` a compile-time zero. The
branches fold away, the borders print, and there is no link edge.

`image_window_box` / `image_window_on` stay in place for the same reason. Over an
opaque marble panel the window is invisible, but it costs nothing and guarantees
legibility on the fallback path.

**The window's one-row widening is gated on `dash_ready` too**, and this is the
subtle part. The window grows to cover the input row only because the marble now
paints there. On the fallback path the input row is bare text over the wallpaper,
exactly as today, and widening the window unconditionally would black out a row
that has always shown the picture — a visible regression on the path whose whole
purpose is to regress nothing. So the rectangle starts at `input_row` when the
dashboard is up and at `border_top` when it is not.

## Call-site changes

| File | Change |
| --- | --- |
| `video/command_view.cxx:873` `render_command_panel` | `dash_set(DASH_PANEL, base)`; skip the two `CV_BORDER` prints and the four per-row divider prints when `dash_ready` |
| `video/console_view.cxx:715` `render_game_keyboard` | `dash_set(DASH_GAMEKB, base)`; same suppression for `KB_STRIP_BORDER`, its three divider prints, and the `-----` rule at content row 2 |
| `video/console_view.cxx:793` `render_keyboard` | real-keyboard branch calls `dash_set(DASH_LINE, input_row)` |
| both gamepad renderers | `image_window_box` starts at `input_row` rather than `border_top` **when `dash_ready`**, one row taller, now that the prompt is inside the panel; unchanged on the fallback path |
| `video/command_view.cxx:897`, `console_view.cxx:732`, `console_view.cxx:823` | the prompt and the input line print at column 2 rather than 0 when `dash_ready`, clearing the well's left cap |
| `video/console_view.cxx:733` | `CAPS` moves from column 35 to 33 when `dash_ready`, off the right cap |
| `video/console_view.cxx:824` | `more v` moves from column 34 to 31 when `dash_ready`, for the same reason |
| `video/command_view.cxx:884` `render_command_panel` | `dash_set(DASH_OVERLAY, base)` instead of `DASH_PANEL` while `p.overlay` is set |
| `video/console_view.cxx:178` `console_height` | unchanged |
| `saturn/src/main.cxx:342` | `dash_init()` after `text_map_init()` |

## Testing

`saturn/tests/test_dash_map.c`, on the host with no SRL, against `dash_map.c`:

- each variant writes corners at its corners, edges along its edges, and divider
  bodies at exactly the columns in the rectangle table;
- interior cells resolve to `field[(y & 3) * 4 + (x & 3)]`;
- every cell outside the variant's rectangle is index 0, including after
  switching from the ten-row panel to the one-row line, which must clear the nine
  rows the taller variant left behind;
- `DASH_LINE` uses only the three bar tiles and never a corner or edge tile, so a
  one-row panel cannot silently lose one of its two bevels;
- `DASH_GAMEKB`'s inner rule runs from the divider at column 14 to the right edge
  at 38, starting on the divider–groove cross and ending on the groove-meets-edge
  tile;
- a repeat `dash_set` with the same variant and base leaves the shadow
  byte-identical and the dirty span empty;
- a frame that claimed the panel leaves it up, and the next frame that does not
  takes it down and stays down — the expiry that keeps the marble out from behind
  the menus;
- `DASH_GAMEKB` closes at column 38 and `DASH_PANEL` at 39, pinning the
  deliberate asymmetry so a later tidy-up is a conscious edit rather than a
  silent one;
- `DASH_OVERLAY` paints `DASH_PANEL`'s frame and groove row but leaves columns
  14 and 30 ordinary marble on the content rows, and switching between the two
  repaints those columns in both directions rather than leaving the other
  variant's cells behind.

Then a cross-compile of the changed translation units to scratch for the syntax
gate. The real build and the emulator run are the author's.

## Contrast risk

The panel body sits at roughly 40–48% grey. The player picks the text colour
through the Display Options page, and it is written to CRAM entry 1 by
`text_set_color`. Before this ships, walk the colour presets and confirm none of
them puts dark text on the stone body. If one does, the fix is to lighten the
body range rather than to constrain the presets — the wallpaper behind the
console text is already arbitrary, so the presets are not currently required to
work against a dark field and should not start being.

Reverse video is unaffected: `gi_invert_tile` paints a solid block of entry 1
with the letter punched out in entry 2, which reads against marble as well as it
reads against black.

## Out of scope

- The title screen's online terminal (`1 + KB_ROWS`, a 13x4 grid) and the netbin
  build keep today's look. They are one more variant that the same builder will
  take later with no structural change — a row in the table, the way
  `DASH_OVERLAY` was.
- Aligning the two gamepad strips' right edges.
- Any change to the wallpaper, the dim, or the display presets.
