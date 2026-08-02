# Split boot preload between SUINE.TGA and HOUSE.TGA

## Problem

The boot splash (`splash_show_once`, SUINE.TGA) currently runs all three
first-cold-boot CD preloads before anything else shows: `preload_game_catalog`
(the Z3 folder scan + per-game header reads, used to build the category/game
picker), `display_preload_images` (TGA background art for the Options
display picker), and `ensure_online_typeahead` (the online Zork I vocabulary
cache). Bundling all three under one splash makes that screen hold longer
than it needs to before the title screen ever appears.

## Change

Move `preload_game_catalog()` out of `splash_show_once()` and into `main.cxx`,
called once after `title_bg_fade_in(TITLE_FADE_FRAMES)` and before
`music_set_level()`/`music_cdda_play()`.

- SUINE.TGA's splash keeps `display_preload_images()` and
  `ensure_online_typeahead()` -- the "tga loads" stay on the splash.
- The categories scan now runs during the HOUSE.TGA window, while the title
  art is already visible but before CD-DA starts and before
  `title_and_seed()` is called. Since `title_and_seed()` is what draws
  "Press any button to begin" and polls input, and it isn't called until
  after this preload returns, the prompt is naturally suspended for the
  duration of the load -- no new suspend/resume logic is needed.
- `preload_game_catalog()` is already idempotent (guarded by
  `g_catalog_ready`) and `game_select()` already calls it defensively, so
  this is a pure reordering: no signature or caller-contract changes.

## Out of scope

- `ensure_online_typeahead()` and `display_preload_images()` stay exactly
  where they are; the user only asked to split out the categories load.
- No visual loading indicator is added for the HOUSE.TGA-window preload --
  it stays silent, matching the existing splash pattern of holding a fixed
  screen with no progress text during a CD read.

## Files touched

- `saturn/src/video/splash.cxx` -- remove the three `preload_game_catalog()`
  call sites (fast path, splash-art-missing fallback, main fade sequence).
  Update the file-header and function-doc comments describing what the
  splash covers.
- `saturn/src/main.cxx` -- add one `preload_game_catalog();` call in the
  boot sequence; update the file-header/`main` doc comments that describe
  the CD front-loading order.
