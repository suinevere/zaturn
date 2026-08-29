# Design: menu borders on the tile layer

**Date:** 2026-08-29
**Status:** Implemented, hardware verification outstanding
**Amends:** `2026-08-28-input-dashboard-design.md`, which built the NBG2 layer
for the gamepad strip alone. Every menu box now draws its border there too.
**Follows:** `2026-08-29-netbin-dashboard-design.md`, which is what makes this
worth doing — until the netbin linked the layer, half the program's menus would
have had stone borders and half would not.

## What changed

`menu_frame()` draws its border as a bevel on NBG2 instead of a run of `+`, `-`
and `|` on the text layer. One function, 20 call sites, both builds: every
Options page, netbin page, message box and confirm changes together.

## Why it was nearly free

`cell_at()` in `dash_map.c` was already a general rectangle painter — corners,
the four edge runs, dividers and interior — and `DASH_OVERLAY` was already the
variant meaning "a plain rectangle with no dividers". A menu box is that shape
at a different size and place.

Three things were genuinely missing, and one visual decision had to be made.

## Eight tiles, not twenty

The first estimate was 20 new tiles — four phases each for top, bottom, left and
right, plus four corners, matching how the panel's frame is built. That was
wrong, and the reason is worth writing down:

**The panel's frame tiles come in fours because each carries the marble field
behind the bevel, and the field has a 32-pixel repeat that the frame must stay
in register with** — hence one tile per `x & 3` for the horizontal runs and per
`y & 3` for the vertical ones.

The menu-box tiles have nothing behind the bevel. There is no repeat to hold in
register, so there is one tile per edge:

| tile | |
|---|---|
| `DT_BOX_TOP` / `DT_BOX_BOTTOM` / `DT_BOX_LEFT` / `DT_BOX_RIGHT` | the four runs |
| `DT_BOX_TL` / `DT_BOX_TR` / `DT_BOX_BL` / `DT_BOX_BR` | corners, mitred on the diagonal by the same minimum-depth rule |

`DT_N` goes 55 → 63. VRAM in bank B0 rises by 256 bytes, in a bank with ~111 KB
spare.

`tools/gen_dash_tiles.py` needed eight lines: `framed()` already took a `base=`
argument, so the same `FRAME` ramp (rim, groove, groove, highlight) goes over
`blank()` instead of over `field(rp, cp)`.

## Why a transparent interior rather than marble

Three options were on the table:

- **Reuse the panel's frame tiles with a blank interior.** Zero new tiles, but
  every one of those tiles is marble with a bevel overlaid on one edge — the
  other four pixels are bare stone. Against an empty interior that reads as an
  8-pixel grey rim, not a border.
- **Fill the box with marble**, like the command strip. Zero new tiles and the
  most consistent with the panel, but it moves all menu text off black onto
  stone, which is a larger change than the one asked for.
- **A bevel over transparency.** 256 bytes, and the interior is untouched.

The third. The point of a border is to be a border; the menu already owns what
is inside it, and the image-suppressing window still hides the wallpaper there
exactly as before.

## A rectangle supplied per call

`dash_build(variant, base_row)` reads geometry from a static table and varies
only the top row. Menus are sized and placed at runtime by `menu_box_fit`, so
`DASH_BOX` takes its geometry from a runtime slot instead, written by a new
`dash_box(x, y, w, h)`; `geom_of()` picks the table or the slot.

Two consequences worth stating:

- **The idempotency key is the whole rectangle**, not `(variant, base)`. Two
  boxes of different widths at the same `y` are different pictures, and the
  panel's key cannot tell them apart.
- **`dash_build` refuses `DASH_BOX`.** Naming it would repaint whatever
  rectangle `dash_box` last left behind, which is exactly the class of bug the
  runtime slot introduces. `tests/test_dash_map.c` pins the refusal.

One thing is on the layer at a time, as before: a box clears the panel and the
panel clears a box.

## The border has to be re-claimed, and that fixed an older bug

`dash_frame_end()` takes the layer down on any frame nobody claimed it — the
rule that keeps the strip from surviving behind a menu. A printed border never
needed re-drawing to stay on screen; a cell layer does.

`menu_message()` paints once and `menu_wait()` then holds the screen for as long
as the player takes to press a key, so without a hold the box would lose its
border one frame after drawing and never get it back.

So `menu_sync()` re-claims the last rectangle every frame while a `MenuBacking`
is alive, and the outermost destructor releases it. The refcount is the right
signal because it already means exactly "a menu page is open" — the border
expires on the same event the image-suppressing window does, rather than on a
rule of its own. Unlike the window, the release is immediate rather than
deferred to the next text flush: a border that outlived its page would sit over
the game.

**This forced `menu_wait()` off its bare `SRL::Core::Synchronize()` and onto
`menu_sync()`, which fixes a pre-existing bug unrelated to borders.**
`menu_sync`'s own header states that every loop holding a screen must call it
and that this "is not a style preference" — because looping PCM starves and
CD-DA does not advance to its next pass otherwise. `menu_wait` was not calling
it, so sound and music stalled for as long as any save/load result screen waited
for a keypress. That is now the only behavioural change here that is not
cosmetic.

## The fallback is still there

`menu_frame` branches on `dash_ready()`, the same split `command_view.cxx` and
`console_view.cxx` already use. When the layer is down the printed `+--+` chrome
returns unchanged. Either way the interior is cleared to spaces, so both forms
occupy the same cells and every caller's text lands in the same place.

## Size

Clean rebuilds.

| build | before | after | delta |
|---|---:|---:|---:|
| `zaturn.netbin` | 181,920 | 182,992 | **+1,072** |
| CD `.elf` | 824,720 | 824,956 | +236 |

The two are not comparable — `compile.bat` builds the CD side with `-DDEBUG`.
The netbin figure is the exact one, being a flat binary.

## Previewing it

`tools/preview_dash.py OUTDIR` renders three PNGs: the box tiles at 22x on a
checkerboard, the pause menu before and after, and a menu over the gamepad
strip. It parses the palette and tiles out of `dash_tiles.c` and the `DT_*`
names out of `dash_map.h`, and fails loudly when the two disagree, so it cannot
drift from what the build ships.

Its text is not the Saturn's — SRL's NBG3 font is not in this repo, so it
substitutes whatever monospace face it finds. Judge the chrome, not the glyphs.

## Not done

- **Nothing has been on a screen.** The previews are exact about tile geometry
  and cell placement because both come from the shipped data, but no Saturn has
  drawn any of this.
- **Contrast against a wallpaper.** In the CD build a menu opens over a picture
  with the NBG0-suppressing window behind it, so the bevel is seen against the
  backdrop colour rather than the art. That is the same window the printed
  border relied on, but the bevel is a mid-grey ramp rather than white text and
  has not been looked at over a dark palette preset.
- **`dash_hold` and the menu hold can both fire in one frame.** They cannot
  today — `saturn_glue.cxx`'s loops do not run while `g_menu_backing_depth > 0`
  — but nothing enforces it, and if it ever happens the two would thrash the
  dirty span between a panel and a box every frame.
