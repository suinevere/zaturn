---
name: logo-timing-title-cgl-and-tga-removal-handoff
description: The boot logo is a fixed six seconds that loads nothing, the title screen shows a random CGL frame and leaves at menu speed, and the TGA path is gone from the port except for the logo itself -- which is also the only thing the Python pipeline still produces.
metadata:
  type: project
---

Continues [[cgl-only-presentation-handoff]]. That file still holds everything
about the CGL supply itself -- the eleven archives, the injection through
`bg.bat`, the presentation table, the review app. This one covers only what
changed after it.

## What the owner asked for

Three things, in one instruction:

1. The SUINEVERE logo is not load-bearing; its full length should be six
   seconds maximum, with even ramps up and down.
2. Fade the title in with a randomly chosen background from any CGL. Its
   fade-out should match the rest of the menus rather than the current
   slowness.
3. Moving around with a non-Dynamic background was showing `SUINEVERE.TGA`.
   Remove any TGA usage from the game code -- and from the Python pipeline --
   leaving only `SUINE.TGA`.

## The reported bug, and what it actually was

`map_view_show` saved `title_bg_loaded_file()` on entry and restored it with
`title_bg_show(was)` on exit. That was wrong in both directions:

- For a CGL frame the recorded name is the **area stem** (`BCEL`), not a file.
  `title_bg_show` would look for `/TGA/BCEL`, fail, and the room background
  never came back after closing the map.
- On a game with no art at all, nothing had written that record since the boot
  splash -- so it still said `SUINE.TGA`, which *did* resolve. Closing the map
  put the SUINEVERE logo up behind the game and left it there. That is what the
  owner saw, and it needed no Dynamic palette to reproduce; only a map open and
  close.

It is now `room_art_reshow()`, guarded on the palette being Dynamic and the
game having art, which is free when NBG0 already holds the frame.

## What changed

Verified against the source; commits are on `main`.

**Splash (`video/splash.cxx`, `splash.h`).** `SPLASH_HOLD_FRAMES 180` beside
the existing `SPLASH_FADE_FRAMES 90`, so the screen is 90 + 180 + 90 = 360
fields = six seconds, ramps equal. `preload_game_catalog()` and
`title_preload_art()` are gone from it, along with `SPLASH_PRELOAD_SLOTS` and
the `game_catalog.h` include -- the logo now covers nothing. The hold is polled
per field rather than slept through so a press during it is honoured on the
frame it was made.

**Title (`main.cxx`).** `title_pick_wallpaper()` picks a frame index from
`boot_entropy()` modulo `room_art_frame_count()` and shows it through
`room_art_show_frame()`, retrying up to six times (stepping the seed by 7) when
a pick is refused. The fade-out is `QUICK_FADE_FRAMES` (15) instead of
`TITLE_FADE_FRAMES` (90); the fade-*in* is deliberately still 90, which is what
was asked. `room_art_release()` runs immediately after the fade-out, so the
title's archive does not hold up to 408.5 KB through the whole menu phase.

**Room art (`video/room_art.cxx/.h`).** The picture path is factored into
`frame_put(image)`, shared by `room_art_show(obj)` and the new
`room_art_show_frame(image)`; `room_art_frame_count()` returns `PRES_FRAME_N`.
`show_frame` is deliberately **not** gated on `room_art_set_game` -- the frames
belong to the disc, not to a story, and the title screen has no story. The
short-circuit's NBG0 tag comparison is now `nbg0_shows_area()`. `load_area`'s
restores go through `art_dir_restore()` (`cd_enter_root()` then
`cd_restore_z3()`) because on the title path the catalogue scan has not captured
`/Z3` yet, so the bare `cd_restore_z3()` was a no-op and would have left the
drive standing in `/BG`.

**TGA removal (`video/title.cxx/.h`).** Deleted: the nine-slot Low Work RAM
cache and every constant describing it (`LWRAM_TOTAL`, `TGA_CACHE_SLOTS`,
`TGA_PLANE_MAX`, `TGA_PAL_BYTES`, `TGA_SLOT_BYTES`, `TGA_CACHE_FLOOR`),
`tga_name_eq`, `tga_cache_find/slot/admit`, `title_bg_show`,
`title_bg_cache_release`, `title_art_file`, `title_art_random`,
`title_preload_art`, and `src/scene/title_art.inc`. `cd_enter_mood` became
`cd_enter_tga` (no mood subfolder left to walk). `TgaImage` lost `Bytes`,
`Cap`, `LowRam`, `LastUse` and `Name`; `tga_decode` lost its reuse mode and its
zone argument and now always allocates in High Work RAM. What survives is
`title_bg_show_oneoff` (the logo), `title_bg_show_raw` (every CGL frame),
`title_bg_hide`, `title_bg_loaded_file` and the fades.

**Python (`tools/`).** `gen_title_art.py` -> `gen_logo_tga.py`, converting
`assets/png/SUINE.PNG` -> `cd/data/TGA/SUINE.TGA` and nothing else; it no longer
writes any `.inc`. `convert-title-art.sh` -> `convert-logo.sh`, and
`saturn/pre.makefile` calls that. **The old script was already broken and could
not have run**: it used `struct` without importing it and referenced `WIDTH`,
`HEIGHT` and `SOURCE_EXT`, none of which were defined. It only ever "passed"
because `assets/png/TITLE/` does not exist, so `convert_title` returned 0 before
reaching any of it. The new one is exercised on every run and reproduces the
committed `SUINE.TGA` byte for byte.

## The budget test was measuring a cache that no longer exists

`saturn/tests/test_lwram_splash_budget.py` -> `test_lwram_budget.py`, rewritten.
The old one had been **erroring, not passing**, since the TGA art was removed:
`compute_budget()` raised `RuntimeError("no background art found under
cd/data/TGA")` because only `SUINE.TGA` was left and it excludes that by name.
Five tests, five errors, and nothing in `pyproject.toml`'s `testpaths` runs it,
so nobody saw.

The replacement measures the pairings that actually exist:

```
  LWRAM                     1048576
  boot jingle                463689  (splash + title only)
  largest of 11 archives     418264  (BCEL.CGL)
  + decode target + 4K       499160  (what load_area asks for)
  game trie (measured)       325632
  save scratch                65536
  title:  archive + jingle            962849  of 1048576
  game:   archive + trie + scratch    890328  of 1048576
```

85,727 bytes spare on the tightest pairing, which is the title screen -- the new
one. **This is the check that makes the title wallpaper safe**: `load_area`
refuses an archive that will not fit and says nothing, so had the biggest
archive not cleared the jingle, the title would have shown no picture whenever
the random pick landed in that area and looked fine the rest of the time. A
fourth test walks `IMAGE_FRAME` against the archives on disc and confirms all 74
frames lie inside their own file.

## Verification actually performed

- `sh syntax-check.sh` clean on `main.cxx`, `title.cxx`, `splash.cxx`,
  `room_art.cxx`, `map_view.cxx` in both DEBUG and release; `NETBIN=1` clean on
  `main.cxx`.
- `sh tools/convert-logo.sh` end to end; regenerated `SUINE.TGA` is
  byte-identical to the committed one (it is gitignored, so `git status` alone
  proves nothing -- the comparison was the point).
- 94 Python tests pass (`test_lwram_budget`, `test_title_bg_dim`,
  `test_no_classifier`, `test_netbin_lift`, `test_netbin_sources`,
  `tools/tests`, `tests`).
- A full `compile-cd.bat release`.

**Nothing here has been seen on screen.** Every claim about how the six seconds
feel, how the random wallpaper reads, and whether the map close now restores the
right picture is an argument from the source, not an observation.

## A trap worth writing down

Two `'\0'` character literals in `room_art.cxx` were written as literal NUL
**bytes** in the file rather than the two-character escape, because a
shell-heredoc Python edit lost a backslash. The SH-2 compiler accepted it with
only `warning: null character(s) preserved in literal` -- the code would have
compiled and compared against the wrong character. Repeated attempts to fix it
through the same heredoc route silently did nothing; PowerShell byte-rewriting
the file was what worked. If a comparison against a string terminator ever
starts behaving oddly, check the bytes, not the rendering.

## Next

- Build to an ISO and watch the boot: the six seconds, the random wallpaper, the
  title's faster exit, and a map open/close on a game with no art (the case that
  used to leave the logo on screen).
- The three stale branches named in [[cgl-only-presentation-handoff]] are still
  waiting to be deleted.
