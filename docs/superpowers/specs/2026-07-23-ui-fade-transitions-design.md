# UI fade transitions: title exit, Display Options cycling, menu navigation

## Problem

The boot splash (see `docs/superpowers/specs/2026-07-23-boot-splash-design.md`)
established a VDP2
hardware color-offset fade for the SUINE logo. The developer wants that same
polish applied to three more places that currently cut instantly:

1. The title screen's `HOUSE.TGA` background, when the player presses a
   button to leave the title and enter the main mode-select menu.
2. Display Options' three cyclable rows -- Background, Text, and Palette
   (Palette mixes plain color presets and picture backgrounds together) --
   which currently swap instantly on every Left/Right press.
3. Navigating between menu "pages" -- the main mode-select menu, the Options
   list, and its six sub-screens (Display, Sound, Controls in both its
   keyboard and pad-only forms, the nested Configure Controls screen,
   Credits, and Config/Network) -- which currently cut instantly from one
   box's content to the next.

## Decisions

1. **Two fade "levels", not one.** A menu page (Options, Display Options,
   etc.) is mostly *text* (drawn on VDP2's NBG3 layer) inside a box whose
   interior shows the plain backdrop color -- the background *picture* (NBG0)
   is only visible around the box's edges, if at all. This means:
   - **Appearance-only fade** (image + backdrop color, NBG3/text untouched):
     used for Display Options row-cycling, where the row labels must stay
     legible while background/color choices flip past underneath, and for
     the title-screen exit, where there is no text-page transition at all
     yet.
   - **Whole-screen fade** (image + backdrop + text, all three together):
     used for page-to-page menu navigation. A first draft of this feature
     faded only the text and left the box's solid-color fill and any
     background picture at full brightness throughout -- confirmed with the
     developer that this reads as broken (only the letters dim, the box's
     own fill stays lit) and corrected to fade all three layers in lockstep.
2. **The backdrop color cannot take a hardware color offset.** VDP2's color
   offset applies only to scroll-screen/sprite layers (NBG0, NBG3, ...); the
   solid back-plane color (`SRL::VDP2::SetBackColor`) is a separate,
   non-offsettable plane. Fading it is software RGB interpolation --
   recompute the color each frame and call `SetBackColor` -- not a hardware
   trick. This runs in the same per-frame loop as the NBG0/NBG3 hardware
   ramps, so all channels move together, but two different mechanisms are
   involved under the hood.
3. **NBG0 (Offset A) and NBG3 (Offset B) are independent channels.** VDP2
   provides two color offset registers; assigning the picture layer to A and
   the text layer to B lets the appearance-fade and the text-fade component
   of a whole-screen fade run simultaneously without fighting over one
   register, and lets the boot splash (already using Offset A) coexist with
   this feature without change.
4. **True fade in/out everywhere the code owns both sides of the
   transition; fade-out-then-instant-reveal at the one boundary it
   doesn't.** Every page function (`options_menu`, `config_page`,
   `controls_page`, `keyboard_controls_page`, `configure_controls_page`,
   `display_options_page`, `sound_options_page`, `credits_page`) is a
   self-contained modal loop: it draws its first frame, then loops on input
   until the player backs out, then returns. Because the assistant controls
   the full body of each of these functions, each can draw its first frame
   while still fully dark and then ramp brightness up -- a true, gradual
   fade-in -- and can ramp back down to dark immediately before every return
   path. The one exception is the outermost hop: the main mode-select menu
   is built directly on the shared `menu_select()` primitive (used all over
   the codebase -- save-slot pickers, etc. -- which must NOT gain
   page-specific fade timing baked into it). `menu_select()`'s own loop
   reads input on its very first iteration, so there is no safe seam to
   interleave a multi-frame brightness ramp without risking the player
   reacting to a menu they can't fully see yet. That one hop -- returning
   from Options to the mode-select menu -- gets a true fade-out (Options
   darkens itself before returning) followed by an **instant** reset to full
   brightness (no ramp) right before the mode-select menu redraws. Leaving
   the mode-select menu *into* Options is not subject to this limitation
   (Options owns its own entry and can ramp in normally), so only the return
   direction of this one hop is asymmetric.
5. **Every fade-out must be paired with a fade-in or an explicit reset.**
   The color offset holds whatever value it was last set to -- there is no
   automatic decay. A fade-out with no matching fade-in/reset downstream
   would leave the screen stuck black. This is why decision 4's "instant
   reveal" step is mandatory, not optional cleanup: skipping it would freeze
   the mode-select menu at zero brightness forever after leaving Options.
6. **Two speeds, not one.** Display Options row-cycling and menu-page
   navigation both use a quick, snappy fade (so holding a direction button
   or backing out of a stack of menus doesn't feel sluggish). The
   title-screen exit keeps the same slower, more deliberate speed the boot
   splash already established, since it happens once per session rather
   than repeatedly during active navigation.
7. **Display Options' background-cycling retry loop already runs entirely
   inside one fade-out/fade-in pair.** `display_cycle_row`'s `DCR_PALETTE`
   branch silently retries and skips any candidate image that fails to
   load, landing on the next valid one before returning. Wrapping the whole
   call (not each retry) in one fade-out/fade-in means any failed candidates
   are skipped while the screen is already dark -- no flicker of a broken
   image, as a free side effect of the wrapping, not extra code.

## Design

### Appearance-only fade (`options.h` / `options.cxx`)

New functions, alongside the existing `display_apply()`:

- `void display_fade_out(int frames)` -- ramps the currently-applied
  appearance (per `g_display`: NBG0's color offset A if
  `display_is_image(&g_display)`, and the backdrop color via `SetBackColor`
  either way) from full brightness down to black over `frames` fields, one
  `SRL::Core::Synchronize()` per step. Leaves everything dark; does not
  disable the offset channel (a paired `display_fade_in` will).
- `void display_fade_in(int frames)` -- mirrors `display_fade_out`, ramping
  from black back up to whatever `g_display` describes *at the time it is
  called* (so the caller must have already changed `g_display` and/or called
  `display_apply()` before calling this). Disables NBG0's offset channel
  once it reaches full brightness.
- `void display_fade_step(int offset)` -- the shared per-frame body (applies
  one `offset` value to NBG0 color offset A if an image is active, and
  interpolates+applies the backdrop color for that same `offset`), exposed
  so `menu.cxx`'s whole-screen fade (below) can drive the appearance
  component from the *same* per-frame loop as its own text-fade step,
  keeping every layer moving in lockstep rather than sequencing two
  separate ramps.

**Title-screen exit is a special case**, addressed in `title.h`/`title.cxx`
directly rather than through `display_fade_out`: at the moment the title
screen ends, what's on NBG0 is unconditionally `HOUSE.TGA` (hardcoded),
*not* whatever `g_display` currently describes -- `g_display` only becomes
the on-screen truth once `display_apply()` runs, which happens *after* this
fade-out. So `main.cxx` calls a new `title_bg_fade_out(int frames)`
(hardware-level, no `g_display` awareness, ramps NBG0's offset down
unconditionally, since an image is always showing at that exact point) for
the *out* side, then runs the existing `display_apply()` unchanged, then
calls the normal `display_fade_in(int frames)` (which *is* `g_display`-aware
and correct here, since `display_apply()` has already run) for the *in*
side. This asymmetry is intentional: the two sides of this one transition
are answering different questions ("what's on screen right now" vs. "what
did we just configure"), and forcing them through one shared abstraction
would require inventing a state `g_display` doesn't actually hold yet.

**Display Options row-cycling** (`display_cycle_row` in `options.cxx`)
wraps its existing body in `display_fade_out(QUICK_FRAMES)` /
`display_fade_in(QUICK_FRAMES)`. All three rows -- Background, Text, and
Palette -- get this now (an earlier round of this same design explicitly
scoped it to Palette only; the developer has since asked to include
Background and Text too). The existing early `return` statements inside the
`DCR_PALETTE` retry loop and after the plain-color branches need to fall
through to one shared `display_fade_in` call at the end instead of
returning directly, so the fade-in always fires exactly once per call
regardless of which path was taken.

### Whole-screen fade (`menu.h` / `menu.cxx`)

New functions, alongside the existing `MenuBacking`/`menu_frame`/etc.:

- `void menu_fade_out(int frames)` -- enables NBG3's color offset B (and,
  via `display_fade_step`, NBG0's offset A if an image is active) and ramps
  every channel from full brightness to black together over `frames`
  fields.
- `void menu_fade_in(int frames)` -- mirrors `menu_fade_out`, ramping every
  channel back up, then disabling both offset channels.
- `void menu_fade_reset(void)` -- instantly (no ramp, one frame) sets both
  offset channels back to zero/disabled. Used only at the one asymmetric
  hop identified in Decision 4.

**Page functions** (`options_menu`, `config_page`, `controls_page`,
`keyboard_controls_page`, `configure_controls_page`, `display_options_page`,
`sound_options_page`, `credits_page`): each draws its first frame (box +
initial content) while still fully dark, then calls
`menu_fade_in(QUICK_FRAMES)` once before entering its normal input-polling
loop, and calls `menu_fade_out(QUICK_FRAMES)` once immediately before every
return path (there is exactly one physical return point in each of these
functions today, reached via `break` out of the loop followed by
shared cleanup code, so this is one call site per function, not several).

**`options_menu`'s own dispatch to each sub-page** additionally wraps the
six sub-page calls (`config_page`, `controls_page`/`keyboard_controls_page`,
`display_options_page`, `sound_options_page`, `credits_page` -- *not* the
"Return to Title" confirm, which is a small popup, out of scope per the
page-level-only decision already made earlier in this design process) with
`menu_fade_out(QUICK_FRAMES)` immediately before the call (darkening
Options' own current content) and, after the call returns and `options_menu`
has redrawn its own box/list (now dark), `menu_fade_in(QUICK_FRAMES)` to
reveal it again. Since every sub-page already fades itself in/out internally
per the paragraph above, this produces one continuous dark-to-dark hold
across the boundary rather than two independent flickers.

**`controls_page`'s own dispatch to `configure_controls_page`** (its nested
sub-screen, reached by picking its first row) gets the identical treatment:
`menu_fade_out(QUICK_FRAMES)` before the call, and after
`configure_controls_page()` returns and `controls_page` has redrawn its own
box/list, `menu_fade_in(QUICK_FRAMES)` to reveal it again -- the same
one-continuous-dark-hold pattern as `options_menu`'s dispatch to its six
sub-pages, just one level deeper in the navigation tree.

**`main.cxx`'s dispatch to `options_menu`** (the one hop touching the
shared `menu_select` primitive): `menu_fade_out(QUICK_FRAMES)` before
calling `options_menu()` (darkening the mode-select menu's own content;
`options_menu()` then fades itself in normally, per the paragraph above),
then `menu_fade_reset()` (instant, no ramp) immediately after
`options_menu()` returns and before the loop calls `menu_select("Z-ATURN",
...)` again -- per Decision 4/5, this must never be skipped, or the
mode-select menu stays stuck black.

### Frame-count constants

- `QUICK_FADE_FRAMES = 15` (~0.25s at the 60fps NTSC field rate this
  codebase already assumes elsewhere) -- Display Options row-cycling and all
  menu-page navigation.
- `TITLE_FADE_FRAMES = 90` (~1.5s, matching the boot splash's existing
  `SPLASH_FADE_FRAMES`) -- the title-screen exit only.

Both `display_fade_out`/`display_fade_in`/`menu_fade_out`/`menu_fade_in`
take `frames` as an explicit parameter rather than hardcoding a speed, so
one set of functions serves both the quick and deliberate cases.

## Testing

No emulator or hardware run is available to this assistant. Verification is
a build (`saturn/compile.bat`, run by the user) plus a manual pass
confirming: the title screen fades out before the mode-select menu appears;
cycling Background/Text/Palette in Display Options shows a quick fade
rather than an instant swap (and any failing candidate image in Palette
cycling is invisible, not a flicker); every Options sub-page fades in on
entry and out on exit, with the Options list itself visibly dark-to-dark
across each sub-page boundary; and leaving Options back to the main
mode-select menu shows a fade-out followed by an immediate, fully-lit
mode-select menu with no stuck-black state.

## Out of scope

- Any menu screen outside the enumerated set: `menu_confirm`/`menu_message`
  popups (Y/N prompts, save/load result screens), keyboard entry screens,
  `online_mode()`, `game_select()`, and save/restore device/slot pickers all
  stay exactly as they are today.
- A true ramped fade-in for the mode-select-menu reveal (Decision 4 already
  explains why this is deferred rather than attempted unsafely).
- Any change to `DISP_IMAGE_MAX`, the Display Options background list
  itself, or the boot splash's own fade (already shipped and reviewed).
