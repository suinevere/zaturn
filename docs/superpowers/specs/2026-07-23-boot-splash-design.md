# Boot splash: SUINE logo hides first-boot loading time

## Problem

On first cold boot, `main()` front-loads every real CD read into the silent
window before the menu track starts (see `main.cxx`'s boot-order comment and
[[cd-reads-stop-cdda]]): `preload_game_catalog()` (Z3 folder scan + per-title
header reads), `display_preload_images()` (decodes all registered `/TGA`
backgrounds into the Low Work RAM cache), and `ensure_online_typeahead()`
(reads `ZORK1.Z3` to build the online typeahead trie). Today the player just
stares at the `HOUSE.TGA` title screen while this runs, with no indication
anything is happening and no fixed bound on how long it takes.

The developer wants to use the "SUINEVERE GAMES" logo (`tools/assets/png/SUINE.PNG`,
already 320x224) as a boot splash that fades in, holds while that real loading
work executes, and fades out — obfuscating the load as a deliberate credit
screen rather than dead air. Typical total pass: roughly 10 seconds.

## Decisions

1. **Dedicated screen, not an overlay.** The splash replaces the title screen
   for its duration: no `HOUSE.TGA`, no "Z-A T U R N" text. Once it fades out,
   the boot sequence proceeds exactly as it does today (`title_bg_show("HOUSE.TGA")`,
   `title_draw_art()`, `title_and_seed()`).
2. **The hold tracks real load time, not a fixed clock.** Fade-in and fade-out
   are short, fixed-length animations (~1.5s each). Between them, the splash
   holds at full brightness for exactly as long as the real CD work takes —
   whatever that is on a given disc/build. "~10 sec total" is the expected
   typical case (short fades + however long 8 image decodes + a catalog scan
   + one typeahead read actually take), not a hard timer. A small minimum
   hold (~2s) covers the case where everything is already cached and the real
   work would otherwise return instantly.
3. **Shown once per cold boot only.** `preload_game_catalog()`,
   `display_preload_images()`, and `ensure_online_typeahead()` are each
   already idempotent and survive the soft-reset `longjmp` back to `main()`'s
   `setjmp`. A soft-reset re-entry has nothing real left to hide, so the
   splash is gated by its own static flag (same pattern as `g_cache_ready` /
   `g_catalog_ready`) and skipped entirely on re-entry.
4. **Fade via VDP2 hardware color offset, not palette rewriting.** SRL exposes
   `VDP2::ScrollScreen::UseColorOffset(OffsetChannel::OffsetA)` and
   `VDP2::SetColorOffsetA(ColorOffset)` — a per-scroll-screen signed RGB delta
   applied after all other VDP2 color processing (`srl_vdp2.hpp`). Enabling
   this on NBG0 and interpolating the offset from `(-255,-255,-255)` (black)
   to `(0,0,0)` (unmodified) and back is a single register write per frame:
   no CRAM/VRAM re-upload, no re-decoding the bitmap, no full `tga_blit_nbg0`
   re-run per frame (that call re-DMAs both pixels and palette, which would
   be wasteful to do every frame just to animate a fade).
5. **`SUINE.TGA` lands in `/TGA` (per the requested path) but is excluded from
   the Display Options background picker.** `display_scan_images()` scans
   every `.TGA` file in that folder and registers it as a selectable
   background, capped at `DISP_IMAGE_MAX = 8` — a cap the folder is already
   at (`ANCIENT`, `BUNKER`, `CASTLE`, `CLIFF`, `CMPLAB`, `FOREST`, `HOUSE`,
   `TYPEWRTR`). Adding a 9th file would either silently bump `TYPEWRTR.TGA`
   out of the picker (alphabetical scan, `SUINE` sorts before `TYPEWRTR`) or
   need `DISP_IMAGE_MAX` raised — and either way would let players select the
   dev-credit logo as a playable room background, which isn't the intent.
   Chosen fix: `display_scan_images()` skips a file named `SUINE.TGA`
   specifically, so it's readable by the splash code but invisible to the
   background picker and doesn't displace anything.

## Design

### New module: `saturn/src/video/splash.h` / `splash.cxx`

One entry point:

```c
void splash_show_once(void);
```

- No-op immediately if a static `g_splash_shown` flag is already set (mirrors
  `g_cache_ready` in `title.cxx`); sets it unconditionally on entry so a
  failure partway through does not retry on the next call.
- Loads `SUINE.TGA` onto NBG0 via the existing `title_bg_show()` path (so it
  benefits from the same cache-or-one-off-decode logic already in
  `title.cxx` — no new TGA-loading code needed).
- Clears the text rows (same `SRL::Debug::PrintClearLine` loop `main()`
  already does before showing title art), so no text overlays the logo.
- Calls `SRL::VDP2::NBG0::UseColorOffset(VDP2::OffsetChannel::OffsetA)`.
- **Fade in:** 90 frames (1.5s at the Saturn's 60fps NTSC field rate; if the
  build targets PAL's 50fps at any point this becomes a 75-frame constant --
  out of scope here since every other timed loop in this codebase already
  assumes 60fps), each iteration setting `SetColorOffsetA` to a linearly
  interpolated `ColorOffset` from `(-255,-255,-255)` to `(0,0,0)`, then
  `SRL::Core::Synchronize()`.
- **Hold:** call `preload_game_catalog()`, `display_preload_images()`,
  `ensure_online_typeahead()` in sequence (offset stays at `(0,0,0)`, no
  per-frame work needed here — these calls already call `Synchronize()`
  internally where they read the disc, e.g. `display_preload_images()`'s
  per-image loop). After they return, always run a fixed 120-frame (2s)
  settle pause (plain `Synchronize()` loop) before fade-out begins. This is
  a fixed add-on rather than a measured top-up: the three preload calls are
  opaque frame-count-wise (no counter to read back without changing their
  signatures), and a hardware wall-clock measurement (`SRL::Timer`) would be
  the first use of that subsystem anywhere in `saturn/src` for a detail this
  minor. A flat 2s floor after every load (cached or not) costs nothing
  when a real load already took several seconds, and guarantees the logo
  never flashes by unrecognizably on a fully-cached run.
- **Fade out:** mirror of fade-in, 90 frames, `(0,0,0)` back to
  `(-255,-255,-255)`.
- Disable the offset (`UseColorOffset(OffsetChannel::NoOffset)`) and hide the
  splash bitmap (`title_bg_hide()`), leaving NBG0 in the same state the
  existing title code expects to find it in before it calls
  `title_bg_show("HOUSE.TGA")`.

### `main.cxx` change

Replace the current unconditional block:

```c
preload_game_catalog();
display_preload_images();
ensure_online_typeahead();
```

with a call to `splash_show_once()`, which performs those same three calls
internally during its hold phase on first boot. On soft-reset re-entry
(`g_splash_shown` already true), `splash_show_once()` still must ensure those
three calls happen (they're cheap no-ops when cached) — so it calls them
itself in both the shown and already-shown paths; the only difference is
whether the fade animation and text-clearing/bitmap-swap happen around them.

### `title.cxx` / `display_scan_images()` change

In the per-entry loop that builds `g_image_name`/`g_image_ptr`, skip an entry
whose decoded name equals `SUINE.TGA` (case-sensitive is fine; ISO9660 names
are already uppercase) before it counts toward `DISP_IMAGE_MAX` or gets
registered.

### Asset pipeline

No changes. `tools/assets/png/SUINE.PNG` is already exactly 320x224 (RGBA),
matching every other background source; the existing
`python tools/make_tga.py <png-dir> <tga-dir>` batch step (already part of
the normal build) will convert it into `saturn/cd/data/TGA/SUINE.TGA`
untouched, the same way it produces `HOUSE.TGA` et al.

## Testing

No emulator or hardware run is available to this assistant, so verification
is limited to: build succeeds (`compile.bat`, run by the user per
[[build-with-compile-bat]] / [[do-not-run-compile]]), and a manual read-through
confirming: fade timing constants are frame-count based (not wall-clock), the
hold phase truly wraps all three CD-reading calls, `g_splash_shown` survives
the soft-reset `longjmp` (plain static, like `g_cache_ready`), and
`SUINE.TGA` is absent from the Display Options background list after the
change. The user should visually confirm the actual fade look/timing on
hardware or in an emulator once built.

## Out of scope

- Any change to the existing 8 background TGAs, the Display Options menu, or
  `DISP_IMAGE_MAX`.
- A skip-splash button/input (not requested; can be added later if desired).
- Looping or animating the logo itself beyond the brightness fade.
