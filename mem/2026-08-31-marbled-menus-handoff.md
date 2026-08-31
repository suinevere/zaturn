---
name: marbled-menus-handoff
description: Menu boxes are marble slabs instead of outlines with the wallpaper showing through -- one branch removed from dash_map's cell_at, which routes them through the same bevel-over-marble path the gamepad panel already used.
metadata:
  type: project
---

Continues [[dynamic-default-title-brightness-and-one-shot-jingle-handoff]].

## What the owner asked for

"Add marbled background to all menus."

## What was actually there

The marble already existed and every menu box already went through it. `dash_map`
paints an NBG2 tile layer, and `cell_at` had two branches:

- the panel path -- bevelled frame with the field's own marble behind every frame
  tile, and `DT_FIELD0 + ((y&3)<<2) + (x&3)` inside, giving the stone a 32-pixel
  repeat rather than an 8-pixel one;
- a `g->box` short-circuit returning the `DT_BOX_*` set: the same bevel drawn over
  transparency, with `DT_BLANK` inside.

Every menu takes the second one, because `menu_frame` calls `dash_box`
unconditionally when `dash_ready()`. So a menu was an outline with the wallpaper
or the back colour showing through its middle.

## The change

The `g->box` branch is gone, and with it the `box` field on `DashGeom`. A menu
box already carries `ndiv = 0` and `rule_row = -1`, so every divider and rule test
in the panel path falls through and it renders exactly as `DASH_OVERLAY` does:
bevelled frame, marble field. That is the whole edit -- one branch and one struct
field.

The `DT_BOX_*` tiles are now unpainted. They stay in the set rather than being
cut: every enum value after them is a literal index into `dash_tiles.c`, so
removing eight would renumber the map tiles to reclaim 256 bytes of VRAM.

## The consequence to know about

`display.h` and `options.cxx` both said the background colour "is what shows
through the transparent menu frames" -- and that was true. It is not any more:
a menu box is opaque from corner to corner (the marble field and every panel
frame tile are solid; no `0x00` in any of them).

The background still reaches the inside of a menu, by a different route that was
already wired: `text_set_color` calls `dash_tint(bg555)`, which walks each marble
CRAM entry half-way toward the background's hue while leaving the dominant channel
alone. So a blue ground gives blue-grey stone and an amber one warm stone, and the
Display Options background row still previews live. Both comments are corrected to
say so.

**Text contrast is the thing to look at on screen.** Menu text now sits on
mid-tone stone (palette entries 5..9 of 16) rather than on the player's chosen
background. `display_cycle_bg`/`display_cycle_text` check a text colour against
the BACKGROUND for clashes, not against the marble, so nothing stops a player
picking dark grey text and landing it on grey stone. The gamepad panel has always
printed its labels on this same marble, so it is an established look rather than
a new risk -- but it has never carried a whole menu before.

## Not changed

- The title screen. It prints its text directly rather than through `menu_frame`,
  so it keeps its CGL wallpaper. "All menus" was read as the boxes, not the
  title.
- The `menu_window_rect` NBG0 suppression, now redundant for the marble path
  (the box is opaque anyway) but still load-bearing for the ASCII `+--+` chrome
  the code falls back to when `dash_ready()` is 0.
- The map view, the command panel and the inventory overlay, which were already
  on the marble.

## Verification actually performed

- `test_dash_map.c` passes, with a new case pinning the whole interior of a menu
  box to the field tile its coordinates imply, plus the four corners and one tile
  of each edge run. It also asserts two adjacent interior cells differ, so a
  constant cannot satisfy it. The two `DT_BOX_TL` assertions became
  `DT_CORNER_TL`.
- `sh syntax-check.sh` clean in DEBUG and release on `dash_map.c`, `menu.cxx`,
  `options.cxx`, `dash_view.cxx`; `NETBIN=1` clean on `dash_map.c` and `menu.cxx`.
- A full `compile-cd.bat release`.

**Not seen on screen.** How the stone reads behind a menu, and whether any
text/background pair goes muddy on it, are the two things a build is needed for.
