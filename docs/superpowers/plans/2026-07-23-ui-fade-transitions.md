# UI Fade Transitions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace instant screen cuts with VDP2 color-offset fades in three places: the title screen's exit into the main menu, all three Display Options cycling rows (Background/Text/Palette), and navigation between the main mode-select menu, the Options list, and its sub-pages.

**Architecture:** Two fade "levels" share one per-frame mechanism. An **appearance-only fade** (`options.cxx`: `display_fade_out`/`display_fade_in`/`display_fade_step`/`display_fade_reset`) darkens the background picture (NBG0, via VDP2 color offset A) and the solid backdrop color (software RGB interpolation, since the backdrop plane can't take a hardware offset) — used for the title-screen exit and Display Options row-cycling, where the menu text must stay legible. A **whole-screen fade** (`menu.cxx`: `menu_fade_out`/`menu_fade_in`/`menu_fade_reset`) additionally darkens the menu text layer (NBG3, via color offset B) in the same per-frame loop — used for page-to-page menu navigation, reusing `display_fade_step` so all three layers move together. One boundary (returning from Options to the shared `menu_select()`-based mode-select menu) uses an instant reset instead of a ramped fade-in, since that primitive is used by many non-page callers and reads input on its first loop iteration.

**Tech Stack:** C/C++ (SaturnRingLib / SGL), Sega Saturn VDP2. Build via `saturn/compile.bat` (run by the user, never by the assistant).

## Global Constraints

- **Build command:** `saturn/compile.bat` — the assistant does NOT run it; the user builds. Make edits only.
- **`SRL::VDP2::SetColorOffsetA`/`SetColorOffsetB` take a non-const `ColorOffset&`** — always pass a named local variable, never a temporary.
- **NBG0 uses color offset channel A; NBG3 (menu text) uses channel B.** They are independent hardware registers and never conflict, including with the boot splash (which also uses channel A on NBG0, but only runs once at boot, before any of this code runs).
- **The backdrop color (`SRL::VDP2::SetBackColor`) cannot take a hardware color offset** — it is a separate, non-scroll-screen plane. Fading it means recomputing the RGB value in software each frame and calling `SetBackColor` again; this happens in the same per-frame loop as the NBG0/NBG3 hardware ramps so everything still moves together.
- **Every fade-out must be paired with a fade-in or an explicit reset downstream.** The color offset holds whatever value it was last set to — there is no automatic decay. A fade-out with nothing to undo it leaves the screen stuck black.
- **Frame-count convention:** this codebase assumes 60fps NTSC for frame-counted waits (no wall-clock timer), matching `title.cxx`'s `SOFT_RESET_HOLD` and the boot splash's `SPLASH_FADE_FRAMES`. `QUICK_FADE_FRAMES = 15` (~0.25s) is used for Display Options row-cycling and all menu-page navigation; `TITLE_FADE_FRAMES = 90` (~1.5s) is used only for the title-screen exit. Each file that needs one of these defines its own local `#define` (matching this codebase's existing convention of file-local constants rather than a shared header) — a small, deliberate duplication, not an oversight.
- **`options.h` is plain C++, not `extern "C"`-guarded** (per its own header comment) — safe to `#include` directly from any `.cxx` file, no wrapping needed.
- **Every one of the 8 menu-page functions in `menu_pages.cxx`** (`options_menu`, `config_page`, `controls_page`, `keyboard_controls_page`, `configure_controls_page`, `display_options_page`, `sound_options_page`, `credits_page`) is a self-contained modal loop that draws its first frame, loops on input, then exits via `break` to a single post-loop point — **except `config_page`**, which uses two direct `return` statements instead of `break` and has no post-loop code to funnel through; it needs its fade-out call at each `return` individually rather than once after the loop.

---

## File Structure

- Modify: `saturn/src/menu/options.h` — declare `display_fade_step`, `display_fade_out`, `display_fade_in`, `display_fade_reset`.
- Modify: `saturn/src/menu/options.cxx` — define the four functions above (plus a small private `display_fade_scale_rgb555` helper); wire `display_fade_out`/`display_fade_in` into `display_cycle_row`.
- Modify: `saturn/src/video/title.h` — declare `title_bg_fade_out`.
- Modify: `saturn/src/video/title.cxx` — define `title_bg_fade_out`.
- Modify: `saturn/src/menu/menu.h` — declare `menu_fade_out`, `menu_fade_in`, `menu_fade_reset`.
- Modify: `saturn/src/menu/menu.cxx` — `#include "options.h"`; define the three functions above.
- Modify: `saturn/src/menu/menu_pages.cxx` — wire the whole-screen fade into all 8 page functions and the 6 sub-page dispatch sites (`options_menu`'s 5, `controls_page`'s 1).
- Modify: `saturn/src/main.cxx` — wire the title-screen-exit fade and the mode-select-menu/Options fade; add a defensive `menu_fade_reset()` to the soft-reset re-entry path.

No new files, no Makefile changes.

---

### Task 1: Appearance-fade primitives + title-screen-exit wiring

**Files:**
- Modify: `saturn/src/menu/options.h` (insert after `display_apply`'s declaration, currently lines 70-71)
- Modify: `saturn/src/menu/options.cxx` (insert after `display_apply`'s definition, currently lines 78-98)
- Modify: `saturn/src/video/title.h` (insert after `title_bg_hide`'s declaration, currently lines 61-70)
- Modify: `saturn/src/video/title.cxx` (insert after `title_bg_hide`'s definition, currently lines 665-676)
- Modify: `saturn/src/main.cxx` (add constants after line 44; rewire the title-exit sequence at lines 119-120)

**Interfaces:**
- Consumes: `SRL::VDP2::ColorOffset`, `SRL::VDP2::SetColorOffsetA`, `SRL::VDP2::NBG0::UseColorOffset`, `SRL::VDP2::OffsetChannel::{OffsetA,NoOffset}`, `SRL::VDP2::SetBackColor`, `SRL::Types::HighColor`, `SRL::Core::Synchronize` (all from `<srl.hpp>`, already included in every file this task touches); `display_bg_rgb(int)`, `g_display` (already visible in `options.cxx`).
- Produces: `void display_fade_step(int offset)`, `void display_fade_out(int frames)`, `void display_fade_in(int frames)`, `void display_fade_reset(void)` (declared in `options.h`, defined in `options.cxx` — used by Task 2, Task 3, Task 5); `void title_bg_fade_out(int frames)` (declared in `title.h`, defined in `title.cxx` — used only by this task's `main.cxx` wiring).

**Note on testing:** no host-runnable test surface for this Saturn cross-compiled code; verification is a read-through self-check plus, at the end of the whole plan, a build+manual check by the user.

- [ ] **Step 1: Add the four appearance-fade declarations to `options.h`**

Find this exact text in `saturn/src/menu/options.h` (currently lines 70-73):
```c
bool display_apply(void);

/*----------------------
 | display_cycle_row
```

Replace it with:
```c
bool display_apply(void);

/*----------------------
 | display_fade_step
 | Description: Applies one frame's worth of appearance darkening/
 |   brightening at `offset` (-255 black .. 0 unchanged): sets VDP2 color
 |   offset A (NBG0, the background picture) to (offset,offset,offset), and
 |   scales the solid backdrop color by the same amount via SetBackColor.
 |   Does not call Synchronize -- the caller's ramp loop does. Exposed so
 |   menu_fade_out/menu_fade_in (menu.cxx) can drive this same appearance
 |   component from within their own per-frame loop, alongside their NBG3
 |   text-offset step, so every layer moves together.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: offset -- signed VDP2 color offset, -255..0
 | Returns: N/A
 ----------------------*/
void display_fade_step(int offset);

/*----------------------
 | display_fade_out / display_fade_in
 | Description: Ramp the current appearance (NBG0's picture plus the solid
 |   backdrop color) to/from black over `frames` fields, one Synchronize per
 |   step. display_fade_out enables NBG0's color offset channel and leaves
 |   everything dark; display_fade_in ramps back up to whatever the
 |   appearance is *now* (the caller must have already applied any state
 |   change -- title_bg_show/title_bg_hide, display_apply, ...) and disables
 |   the channel again. Always pair one with the other, or with
 |   display_fade_reset: the color offset holds whatever value it was last
 |   set to, so a fade-out with nothing downstream to reveal it again leaves
 |   the screen stuck black.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void display_fade_out(int frames);
void display_fade_in(int frames);

/*----------------------
 | display_fade_reset
 | Description: Instantly (no ramp) resets NBG0's color offset to zero/
 |   disabled and the backdrop color to g_display's current color. Not used
 |   by the normal display_fade_out/display_fade_in pair (which already leave
 |   things correct on their own); exists for menu_fade_reset's one
 |   asymmetric menu-navigation hop (see menu.cxx) and as a defensive reset on
 |   soft-reset re-entry, in case a reset chord fires mid-ramp and leaves an
 |   offset stuck non-zero.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_fade_reset(void);

/*----------------------
 | display_cycle_row
```

- [ ] **Step 2: Add the four definitions to `options.cxx`**

Find this exact text in `saturn/src/menu/options.cxx` (currently lines 96-100):
```cpp
    }
    return true;
}

/*----------------------
 | display_cycle_row
```

Replace it with:
```cpp
    }
    return true;
}

/*----------------------
 | display_fade_scale_rgb555
 | Description: Scales a packed RGB555 color's three 5-bit channels by
 |   (255+offset)/255, clamped 0..255 -- offset -255 gives black, 0 gives the
 |   color unchanged. Used to fade the solid backdrop plane, which (unlike
 |   NBG0/NBG3) cannot take a VDP2 hardware color offset.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: packed -- a DISP_RGB555-format color; offset -- -255..0
 | Returns: the scaled color, same packed format
 ----------------------*/
static unsigned short display_fade_scale_rgb555(unsigned short packed, int offset) {
    int frac = 255 + offset;
    if (frac < 0)   frac = 0;
    if (frac > 255) frac = 255;
    int r = ( packed        & 0x1F) * frac / 255;
    int g = ((packed >> 5)  & 0x1F) * frac / 255;
    int b = ((packed >> 10) & 0x1F) * frac / 255;
    return (unsigned short) (0x8000 | (b << 10) | (g << 5) | r);
}

/*----------------------
 | display_fade_step
 | Description: See options.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: offset -- signed VDP2 color offset, -255..0
 | Returns: N/A
 ----------------------*/
void display_fade_step(int offset) {
    SRL::VDP2::ColorOffset off((int16_t) offset, (int16_t) offset, (int16_t) offset);
    SRL::VDP2::SetColorOffsetA(off);
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(
        display_fade_scale_rgb555(display_bg_rgb(g_display.bg), offset)));
}

/*----------------------
 | display_fade_out
 | Description: See options.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void display_fade_out(int frames) {
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    for (int i = 0; i <= frames; i++) {
        display_fade_step(-(255 * i) / frames);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | display_fade_in
 | Description: See options.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void display_fade_in(int frames) {
    for (int i = 0; i <= frames; i++) {
        display_fade_step(-255 + (255 * i) / frames);
        SRL::Core::Synchronize();
    }
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
}

/*----------------------
 | display_fade_reset
 | Description: See options.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_fade_reset(void) {
    SRL::VDP2::ColorOffset zero((int16_t) 0, (int16_t) 0, (int16_t) 0);
    SRL::VDP2::SetColorOffsetA(zero);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
}

/*----------------------
 | display_cycle_row
```

- [ ] **Step 3: Add `title_bg_fade_out`'s declaration to `title.h`**

Find this exact text in `saturn/src/video/title.h` (currently lines 61-72):
```c
void title_bg_hide(void);

/*----------------------
 | title_and_seed
```

Replace it with:
```c
void title_bg_hide(void);

/*----------------------
 | title_bg_fade_out
 | Description: Ramps NBG0's color offset A from 0 to -255 over `frames`
 |   fields, darkening whatever TGA is currently shown (unconditionally --
 |   this is only ever called at the title screen's exit, where an image is
 |   always showing, so unlike display_fade_out in options.cxx it does not
 |   consult g_display, which does not describe the title's HOUSE.TGA at all
 |   and has not been applied to the screen yet at that point). Leaves the
 |   offset held at -255 and the channel enabled; pair with display_fade_in
 |   (options.h) once display_apply() has run, which is g_display-aware and
 |   correct for revealing whatever comes next.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void title_bg_fade_out(int frames);

/*----------------------
 | title_and_seed
```

- [ ] **Step 4: Add `title_bg_fade_out`'s definition to `title.cxx`**

Find this exact text in `saturn/src/video/title.cxx` (currently lines 674-678):
```cpp
void title_bg_hide(void) {
    SRL::VDP2::NBG0::ScrollDisable();
}

/*----------------------
 | title_and_seed
```

Replace it with:
```cpp
void title_bg_hide(void) {
    SRL::VDP2::NBG0::ScrollDisable();
}

/*----------------------
 | title_bg_fade_out
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void title_bg_fade_out(int frames) {
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    for (int i = 0; i <= frames; i++) {
        int v = -(255 * i) / frames;
        SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
        SRL::VDP2::SetColorOffsetA(off);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | title_and_seed
```

- [ ] **Step 5: Add the frame-count constant and wire the title-exit fade in `main.cxx`**

Find this exact text in `saturn/src/main.cxx` (currently lines 44-45):
```cpp
using namespace SRL::Types;

```

Replace it with:
```cpp
using namespace SRL::Types;

// Ramp length for the title-screen exit fade: ~1.5s at 60fps, matching the
// boot splash's own SPLASH_FADE_FRAMES speed since this happens once per
// session rather than repeatedly during navigation (contrast QUICK_FADE_FRAMES
// in Task 5, below).
#define TITLE_FADE_FRAMES 90

```

Find this exact text in `saturn/src/main.cxx` (currently lines 119-120):
```cpp
    int seed = title_and_seed();
    display_apply();
```

Replace it with:
```cpp
    int seed = title_and_seed();
    title_bg_fade_out(TITLE_FADE_FRAMES);
    display_apply();
    display_fade_in(TITLE_FADE_FRAMES);
```

- [ ] **Step 6: Read-through self-check**

Confirm:
1. `display_fade_step` always writes both NBG0's color offset A and the backdrop color, regardless of whether an image is currently active (harmless when NBG0 is disabled -- the register write has no visible effect with nothing to darken).
2. `display_fade_out`/`display_fade_in` each declare `off` as a named local before passing it to `SetColorOffsetA` (non-const reference -- a temporary will not compile).
3. `title_bg_fade_out` does NOT touch the backdrop color or `g_display` at all -- HOUSE.TGA covers the full 320x224 screen, so there is nothing else to fade at that point, and `g_display` does not yet describe what's on screen.
4. In `main.cxx`, `title_bg_fade_out` runs *before* `display_apply()`, and `display_fade_in` runs *after* -- reversing this order would fade in based on the OLD appearance and fade out based on nothing.

- [ ] **Step 7: Commit**

```bash
cd "C:/Users/saggl/CLionProjects/saturn-mojozork"
git add saturn/src/menu/options.h saturn/src/menu/options.cxx saturn/src/video/title.h saturn/src/video/title.cxx saturn/src/main.cxx
git commit -m "Add appearance-fade primitives and fade the title-screen exit"
```

---

### Task 2: Fade Display Options row-cycling (Background, Text, and Palette)

**Files:**
- Modify: `saturn/src/menu/options.cxx` (add a constant near the top; rewrite `display_cycle_row`, currently lines 120-135)

**Interfaces:**
- Consumes: `display_fade_out(int)`, `display_fade_in(int)` (Task 1, same file).
- Produces: no new symbols; `display_cycle_row`'s external signature is unchanged.

**Note on testing:** no host-runnable test surface; verification is a read-through self-check.

- [ ] **Step 1: Add the quick-fade frame-count constant**

Find this exact text in `saturn/src/menu/options.cxx` (currently lines 19-22, right after the includes):
```cpp
extern "C" {
#include "saturn_backup.h"
#include "music.h"
}
```

Replace it with:
```cpp
extern "C" {
#include "saturn_backup.h"
#include "music.h"
}

// Ramp length for Display Options row-cycling: ~0.25s at 60fps, quick enough
// that holding a direction button to browse several backgrounds/colors still
// feels responsive (contrast TITLE_FADE_FRAMES in main.cxx, used only once
// per session for the slower title-screen exit).
#define QUICK_FADE_FRAMES 15
```

- [ ] **Step 2: Wire the fade into `display_cycle_row`**

Find this exact text in `saturn/src/menu/options.cxx` (currently lines 120-135):
```cpp
void display_cycle_row(DisplayCycleRow which, int dir) {
    if (which != DCR_PALETTE) {
        if (which == DCR_BG) display_cycle_bg(&g_display, dir);
        else                 display_cycle_text(&g_display, dir);
        display_apply();     // colours only; nothing here can fail to load
        return;
    }
    int tries = display_palette_count();
    while (tries-- > 0) {
        display_cycle_palette(&g_display, dir);
        DisplayState want = g_display;
        if (display_apply()) return;   // showing what was asked for
        g_display = want;              // keep our place and step past the bad entry
    }
    display_apply();
}
```

Replace it with:
```cpp
void display_cycle_row(DisplayCycleRow which, int dir) {
    display_fade_out(QUICK_FADE_FRAMES);
    if (which != DCR_PALETTE) {
        if (which == DCR_BG) display_cycle_bg(&g_display, dir);
        else                 display_cycle_text(&g_display, dir);
        display_apply();     // colours only; nothing here can fail to load
    } else {
        int tries = display_palette_count();
        while (tries-- > 0) {
            display_cycle_palette(&g_display, dir);
            DisplayState want = g_display;
            if (display_apply()) break;    // showing what was asked for
            g_display = want;              // keep our place and step past the bad entry
        }
    }
    display_fade_in(QUICK_FADE_FRAMES);
}
```

Note what changed beyond adding the two fade calls: both early `return` statements became `break`/fall-through, so every path (plain color, successful palette cycle, or every palette candidate exhausted) reaches the single `display_fade_in` call at the end exactly once. This is also why the Palette row's own doc comment already promises: any candidate image that fails to load during this retry loop is skipped while the screen is already dark (from the one `display_fade_out` at the top), not as a separate flicker.

- [ ] **Step 3: Read-through self-check**

Confirm:
1. Every path through the rewritten function reaches exactly one `display_fade_in(QUICK_FADE_FRAMES)` call -- the non-palette branch falls out of its `if`/`else` into it; the palette branch's `while` loop falls out (via `break` on success, or exhausting `tries`) into it too.
2. `DisplayCycleRow`'s three values (`DCR_PALETTE`, `DCR_BG`, `DCR_TEXT`) are unchanged -- this task only reorganizes control flow inside `display_cycle_row`, not its parameter type.
3. No other caller of `display_cycle_row` needs to change -- it still returns `void` and takes the same two parameters.

- [ ] **Step 4: Commit**

```bash
cd "C:/Users/saggl/CLionProjects/saturn-mojozork"
git add saturn/src/menu/options.cxx
git commit -m "Fade Display Options row-cycling (Background, Text, and Palette)"
```

---

### Task 3: Whole-screen fade primitives (`menu.h` / `menu.cxx`)

**Files:**
- Modify: `saturn/src/menu/menu.h` (update the header's own Dependencies line; insert new declarations before the closing `#endif`, currently lines 145-146)
- Modify: `saturn/src/menu/menu.cxx` (add `#include "options.h"`; append new definitions at the end of the file)

**Interfaces:**
- Consumes: `display_fade_step(int)`, `display_fade_reset(void)` (Task 1, `options.h`); `SRL::VDP2::NBG3::UseColorOffset`, `SRL::VDP2::OffsetChannel::{OffsetB,NoOffset}`, `SRL::VDP2::SetColorOffsetB`, `SRL::VDP2::ColorOffset`, `SRL::Core::Synchronize` (SRL, already available via `<srl.hpp>`).
- Produces: `void menu_fade_out(int frames)`, `void menu_fade_in(int frames)`, `void menu_fade_reset(void)` (declared in `menu.h`, defined in `menu.cxx` — used by Task 4 and Task 5).

**Note on testing:** no host-runnable test surface; verification is a read-through self-check.

- [ ] **Step 1: Update `menu.h`'s Dependencies line and add the three declarations**

Find this exact text in `saturn/src/menu/menu.h` (currently lines 11-14):
```c
 | Dependencies: menu_layout.c (box-fit/digit-mapping geometry), console_view.cxx
 |   (hint/note_input_device/render_console/g_kbd_visible), input.h (g_pad,
 |   Button), saturn_keyboard.h (SaturnKeyEvent/SATURN_KEY_*), soft_reset.h
 |   (check_soft_reset), sound.c (sound_service), music.c (music_tick), SRL
```

Replace it with:
```c
 | Dependencies: menu_layout.c (box-fit/digit-mapping geometry), console_view.cxx
 |   (hint/note_input_device/render_console/g_kbd_visible), input.h (g_pad,
 |   Button), saturn_keyboard.h (SaturnKeyEvent/SATURN_KEY_*), soft_reset.h
 |   (check_soft_reset), sound.c (sound_service), music.c (music_tick),
 |   options.h (display_fade_step/display_fade_reset), SRL
```

Find this exact text in `saturn/src/menu/menu.h` (currently lines 144-146):
```c
bool menu_confirm(const char *line1, const char *line2);

#endif /* MENU_H */
```

Replace it with:
```c
bool menu_confirm(const char *line1, const char *line2);

/*----------------------
 | menu_fade_out / menu_fade_in
 | Description: Ramp the whole screen -- background appearance (NBG0/
 |   backdrop, via display_fade_step) and menu text (NBG3, via VDP2 color
 |   offset B) together -- to/from black over `frames` fields, for
 |   page-to-page menu transitions. An earlier design pass fading only the
 |   text left the box's own solid-color fill and any background picture at
 |   full brightness throughout; this fades all three layers in lockstep
 |   instead. Always pair one with the other, or with menu_fade_reset: the
 |   color offset holds whatever value it was last set to, so a fade-out
 |   with nothing downstream to reveal it again leaves the screen stuck
 |   black.
 | Author: suinevere
 | Dependencies: options.h, SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void menu_fade_out(int frames);
void menu_fade_in(int frames);

/*----------------------
 | menu_fade_reset
 | Description: Instantly (no ramp) resets both color offset channels and the
 |   backdrop color. Used only where a true ramped fade-in is not safe:
 |   returning from Options to the mode-select menu, which is built on the
 |   shared menu_select() primitive (used by many non-page callers too) and
 |   reads input on its very first loop iteration, leaving no safe seam to
 |   interleave a multi-frame brightness ramp without risking the player
 |   reacting to a menu they cannot yet fully see. Also used defensively on
 |   soft-reset re-entry, in case a reset chord fires mid-ramp and leaves an
 |   offset stuck non-zero.
 | Author: suinevere
 | Dependencies: options.h, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_fade_reset(void);

#endif /* MENU_H */
```

- [ ] **Step 2: Add the include and the three definitions to `menu.cxx`**

Find this exact text in `saturn/src/menu/menu.cxx` (currently lines 14-21):
```cpp
#include <srl.hpp>

#include "menu.h"
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
```

Replace it with:
```cpp
#include <srl.hpp>

#include "menu.h"
#include "app_state.h"
#include "console_view.h"
#include "input.h"
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
```

Find this exact text at the very end of `saturn/src/menu/menu.cxx` (the last lines of the file, closing out the `menu_confirm` function -- its `return true`/`return false` exits are inside `if` checks near the top of its loop, so the loop's own closing brace is immediately followed by the function's closing brace, with nothing in between):
```cpp
        menu_clear();
        menu_frame(x0, y0, w, h, "CONFIRM");
        int cx = x0 + 2, cy = y0 + 3;
        if (l1) SRL::Debug::Print(cx, cy, "%s", line1);
        if (l2) SRL::Debug::Print(cx, cy + 1, "%s", line2);
        int hy = cy + (l2 > 0 ? 3 : 2);
        if (!g_kbd_visible) SRL::Debug::Print(cx, hy, "1) Yes    2) No");
        SRL::Debug::Print(cx, hy + 1, "%s",
            hint("A / C = Yes     B = No", "Enter = Yes     Esc = No"));
        menu_sync();
    }
}
```

Replace it with:
```cpp
        menu_sync();
    }
}

// ---- whole-screen fade for page-to-page menu navigation --------------------

/*----------------------
 | menu_fade_out
 | Description: See menu.h.
 | Author: suinevere
 | Dependencies: options.h (display_fade_step), SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void menu_fade_out(int frames) {
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetB);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    for (int i = 0; i <= frames; i++) {
        int v = -(255 * i) / frames;
        display_fade_step(v);
        SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
        SRL::VDP2::SetColorOffsetB(off);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | menu_fade_in
 | Description: See menu.h.
 | Author: suinevere
 | Dependencies: options.h (display_fade_step), SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
void menu_fade_in(int frames) {
    for (int i = 0; i <= frames; i++) {
        int v = -255 + (255 * i) / frames;
        display_fade_step(v);
        SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
        SRL::VDP2::SetColorOffsetB(off);
        SRL::Core::Synchronize();
    }
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
}

/*----------------------
 | menu_fade_reset
 | Description: See menu.h.
 | Author: suinevere
 | Dependencies: options.h (display_fade_reset), SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_fade_reset(void) {
    display_fade_reset();
    SRL::VDP2::ColorOffset zero((int16_t) 0, (int16_t) 0, (int16_t) 0);
    SRL::VDP2::SetColorOffsetB(zero);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
}
```

- [ ] **Step 3: Read-through self-check**

Confirm:
1. `menu_fade_out` enables BOTH `NBG3::UseColorOffset(OffsetB)` and `NBG0::UseColorOffset(OffsetA)` before its loop; `menu_fade_in` disables both after its loop -- neither channel is left enabled if the function pair completes normally.
2. Every `SetColorOffsetB` call passes a named local (`off` or `zero`), never a temporary.
3. `menu_fade_out`/`menu_fade_in` call `display_fade_step(v)` -- NOT `display_fade_out`/`display_fade_in` -- so the appearance component and the NBG3 text component ramp in the exact same per-frame loop, not two sequential ramps.
4. `menu_fade_reset` touches both channels and the backdrop color, matching what `menu_fade_out` could have engaged.

- [ ] **Step 4: Commit**

```bash
cd "C:/Users/saggl/CLionProjects/saturn-mojozork"
git add saturn/src/menu/menu.h saturn/src/menu/menu.cxx
git commit -m "Add whole-screen fade primitives for page-to-page menu navigation"
```

---

### Task 4: Fade every Options page's entry/exit and sub-page dispatch

**Files:**
- Modify: `saturn/src/menu/menu_pages.cxx` (add a constant near the top; modify all 8 page functions)

**Interfaces:**
- Consumes: `menu_fade_out(int)`, `menu_fade_in(int)` (Task 3, `menu.h`, already included in this file).
- Produces: no new symbols; every page function's external signature is unchanged.

**Note on testing:** no host-runnable test surface; verification is a read-through self-check per function.

This task applies the same small pattern to all 8 page functions in this file: draw the first frame while still dark (inherited from whichever caller faded out before entering), fade in once after that first draw, then fade out once before returning. 7 of the 8 use `break` exclusively, funneling to one point right after their loop closes; `config_page` uses two direct `return` statements instead and needs the fade-out at each one. `options_menu` and `controls_page` additionally dispatch to sub-pages from inside their own loops and need a fade-out/back-in wrapped around each dispatch call.

- [ ] **Step 1: Add the quick-fade frame-count constant**

Find this exact text in `saturn/src/menu/menu_pages.cxx` (currently lines 33-36):
```cpp
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"

```

Replace it with:
```cpp
#include "options.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"

// Ramp length for menu-page fades: ~0.25s at 60fps -- see options.cxx's
// identical QUICK_FADE_FRAMES for why this speed (matches Display Options
// row-cycling; each file defines its own copy rather than sharing a header
// constant, per this codebase's existing convention).
#define QUICK_FADE_FRAMES 15

```

- [ ] **Step 2: `config_page`**

Find this exact text (currently lines 94-99):
```cpp
static void config_page(void) {
    MenuBacking backing;
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    SRL::Core::Synchronize();
```

Replace it with:
```cpp
static void config_page(void) {
    MenuBacking backing;
    KeyboardState k; keyboard_reset(&k);
    for (int i = 0; g_dialnum[i] && k.input_len < DIALNUM_MAX; i++) keyboard_type_char(&k, g_dialnum[i]);
    const char *err = "";
    SRL::Core::Synchronize();
    bool need_fade_in = true;
```

Find this exact text (currently lines 120-130):
```cpp
        if (cancel) return;
        if (accept) {
            if (!valid_dialnum(k.input)) err = "Invalid number (digits only).";
            else {
                int j;
                for (j = 0; k.input[j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = k.input[j];
                g_dialnum[j] = '\0';
                options_save();
                return;
            }
        }
```

Replace it with:
```cpp
        if (cancel) { menu_fade_out(QUICK_FADE_FRAMES); return; }
        if (accept) {
            if (!valid_dialnum(k.input)) err = "Invalid number (digits only).";
            else {
                int j;
                for (j = 0; k.input[j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = k.input[j];
                g_dialnum[j] = '\0';
                options_save();
                menu_fade_out(QUICK_FADE_FRAMES);
                return;
            }
        }
```

Find this exact text (currently lines 146-150):
```cpp
        SRL::Debug::Print(fx + 2, fy + 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
}
```

Replace it with:
```cpp
        SRL::Debug::Print(fx + 2, fy + 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
}
```

- [ ] **Step 3: `configure_controls_page`**

Find this exact text (currently lines 192-194):
```cpp
static void configure_controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
```

Replace it with:
```cpp
static void configure_controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
```

Find this exact text (currently lines 264-270):
```cpp
        SRL::Debug::Print(x, y++, "%c    Reset to Defaults", sel == R_RESET ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c    OK", sel == R_DONE ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c    Cancel", sel == R_CANCEL ? '>' : ' ');
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

Replace it with:
```cpp
        SRL::Debug::Print(x, y++, "%c    Reset to Defaults", sel == R_RESET ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c    OK", sel == R_DONE ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c    Cancel", sel == R_CANCEL ? '>' : ' ');
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    SRL::Core::Synchronize();
}
```

- [ ] **Step 4: `controls_page`** (also wraps its own dispatch to `configure_controls_page`)

Find this exact text (currently lines 291-294):
```cpp
static void controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    int sel = 0;
```

Replace it with:
```cpp
static void controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    int sel = 0;
```

Find this exact text (currently line 309):
```cpp
        if (sel == 0 && act) configure_controls_page();
```

Replace it with:
```cpp
        if (sel == 0 && act) { menu_fade_out(QUICK_FADE_FRAMES); configure_controls_page(); need_fade_in = true; }
```

Find this exact text (currently lines 337-342):
```cpp
        if (nums) SRL::Debug::Print(x, y++, "%c 3) Done", sel == 2 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Done", sel == 2 ? '>' : ' ');
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

Replace it with:
```cpp
        if (nums) SRL::Debug::Print(x, y++, "%c 3) Done", sel == 2 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Done", sel == 2 ? '>' : ' ');
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    SRL::Core::Synchronize();
}
```

- [ ] **Step 5: `keyboard_controls_page`**

Find this exact text (currently lines 367-370):
```cpp
void keyboard_controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    int s_arrows = g_caret_arrows, s_ins = keyboard_get_insert(),
```

Replace it with:
```cpp
void keyboard_controls_page(void) {
    MenuBacking backing;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    int s_arrows = g_caret_arrows, s_ins = keyboard_get_insert(),
```

Find this exact text (currently lines 427-432):
```cpp
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("A/Start=OK  B=Cancel", "Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

Replace it with:
```cpp
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("A/Start=OK  B=Cancel", "Enter=OK  Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    SRL::Core::Synchronize();
}
```

- [ ] **Step 6: `sound_options_page`**

Find this exact text (currently lines 494-496):
```cpp
    int aidx = -1;
    int cur = music_cdda_current_track();
    if (cur > 0) for (int i = 0; i < an; i++) if (atracks[i] == cur)         { aidx = i; break; }
```

This line does not need to change — it is here only to confirm you are looking at the right function. Instead find this exact text (currently line 495):
```cpp
    SRL::Core::Synchronize();
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
```

Replace it with:
```cpp
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
```

Find this exact text (currently lines 586-591):
```cpp
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel", "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

Replace it with:
```cpp
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel", "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    SRL::Core::Synchronize();
}
```

- [ ] **Step 7: `display_options_page`**

Find this exact text (currently lines 633-636):
```cpp
    int sel = 0;
    DisplayState snapshot = g_display;
    SRL::Core::Synchronize();
    for (;;) {
```

Replace it with:
```cpp
    int sel = 0;
    DisplayState snapshot = g_display;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
```

Find this exact text (currently lines 699-705):
```cpp
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel",
                                             "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

Replace it with:
```cpp
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel",
                                             "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    SRL::Core::Synchronize();
}
```

- [ ] **Step 8: `credits_page`**

Find this exact text (currently lines 860-865):
```cpp
static void credits_page(void) {
    MenuBacking backing;
    const int fx = 0, fy = 2, fw = 40, fh = 24;
    const int npages = (int)(sizeof(CREDITS_PAGES) / sizeof(CREDITS_PAGES[0]));
    int page = 0;
    SRL::Core::Synchronize();
```

Replace it with:
```cpp
static void credits_page(void) {
    MenuBacking backing;
    const int fx = 0, fy = 2, fw = 40, fh = 24;
    const int npages = (int)(sizeof(CREDITS_PAGES) / sizeof(CREDITS_PAGES[0]));
    int page = 0;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
```

Find this exact text (currently lines 887-892):
```cpp
        SRL::Debug::Print(x, fy + fh - 3, "%s Page %d/%d %s",
                           page > 0 ? "<" : " ", page + 1, npages, page < npages - 1 ? ">" : " ");
        SRL::Debug::Print(x, fy + fh - 2, "%s", hint("</> page  B=Back", "</> page  Esc=Back"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

Replace it with:
```cpp
        SRL::Debug::Print(x, fy + fh - 3, "%s Page %d/%d %s",
                           page > 0 ? "<" : " ", page + 1, npages, page < npages - 1 ? ">" : " ");
        SRL::Debug::Print(x, fy + fh - 2, "%s", hint("</> page  B=Back", "</> page  Esc=Back"));
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    SRL::Core::Synchronize();
}
```

- [ ] **Step 9: `options_menu`** (also wraps its 5 sub-page dispatches)

Find this exact text (currently lines 956-958):
```cpp
    int diff = g_difficulty, sel = 0;
    SRL::Core::Synchronize();
    for (;;) {
```

Replace it with:
```cpp
    int diff = g_difficulty, sel = 0;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
```

Find this exact text (currently lines 975-988):
```cpp
        if (act) {
            if (item == OI_CONFIG) { config_page(); }
            else if (item == OI_CONTROLS) { if (g_kbd_visible) controls_page(); else keyboard_controls_page(); menu_clear(); }
            else if (item == OI_DISPLAY) { display_options_page(); menu_clear(); }
            else if (item == OI_SOUND) { sound_options_page(); menu_clear(); }
            else if (item == OI_CREDITS) { credits_page(); menu_clear(); }
            else if (item == OI_RETURN) {
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                    soft_reset_to_title();
                }
            }
            else if (item == OI_DONE) break;
        }
```

Replace it with:
```cpp
        if (act) {
            if (item == OI_CONFIG) { menu_fade_out(QUICK_FADE_FRAMES); config_page(); need_fade_in = true; }
            else if (item == OI_CONTROLS) {
                menu_fade_out(QUICK_FADE_FRAMES);
                if (g_kbd_visible) controls_page(); else keyboard_controls_page();
                menu_clear();
                need_fade_in = true;
            }
            else if (item == OI_DISPLAY) { menu_fade_out(QUICK_FADE_FRAMES); display_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_SOUND) { menu_fade_out(QUICK_FADE_FRAMES); sound_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_CREDITS) { menu_fade_out(QUICK_FADE_FRAMES); credits_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_RETURN) {
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                    soft_reset_to_title();
                }
            }
            else if (item == OI_DONE) break;
        }
```

Find this exact text (currently lines 1016-1025):
```cpp
        SRL::Debug::Print(x0 + 2, y0 + 14, "%s", hint("Up/Dn A=pick  <>=diff", "Up/Dn Enter  B=back"));
        menu_sync();
    }
    bool diff_changed = (diff != g_difficulty);
    g_difficulty = diff;
    if (diff_changed) options_save();
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
}
```

Replace it with:
```cpp
        SRL::Debug::Print(x0 + 2, y0 + 14, "%s", hint("Up/Dn A=pick  <>=diff", "Up/Dn Enter  B=back"));
        menu_sync();
        if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }
    }
    menu_fade_out(QUICK_FADE_FRAMES);
    bool diff_changed = (diff != g_difficulty);
    g_difficulty = diff;
    if (diff_changed) options_save();
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
}
```

- [ ] **Step 10: Read-through self-check**

For each of the 8 functions, confirm:
1. `need_fade_in` starts `true` and is declared before the function's `for (;;)` loop.
2. The `if (need_fade_in) { menu_fade_in(QUICK_FADE_FRAMES); need_fade_in = false; }` check sits immediately after that function's own `menu_sync()` call at the bottom of its loop body -- so it fires once, right after the first real frame is drawn (or, in `options_menu`/`controls_page`, once again after any sub-page dispatch sets `need_fade_in` back to `true` and control falls through to that same redraw+`menu_sync()`).
3. Every exit path calls `menu_fade_out(QUICK_FADE_FRAMES)` exactly once: the 7 `break`-based functions call it right after their loop closes (before any trailing `SRL::Core::Synchronize()`); `config_page` calls it at each of its two `return` statements individually.
4. In `options_menu` and `controls_page`, every sub-page dispatch call is preceded by `menu_fade_out(QUICK_FADE_FRAMES)` and followed by `need_fade_in = true;` -- so the outer page darkens itself before handing control to the sub-page (which fades itself in per point 2, and fades itself out per point 3 before returning), and the outer page then redraws and fades itself back in.
5. `options_menu`'s `OI_RETURN` branch (confirm dialog + `soft_reset_to_title()`) is untouched -- no fade wrapping, matching the plan's page-level-only scope (a Yes/No popup, not a page).

- [ ] **Step 11: Commit**

```bash
cd "C:/Users/saggl/CLionProjects/saturn-mojozork"
git add saturn/src/menu/menu_pages.cxx
git commit -m "Fade every Options page's entry/exit and sub-page dispatch"
```

---

### Task 5: Wire the mode-select-menu/Options hop and the soft-reset defensive reset

**Files:**
- Modify: `saturn/src/main.cxx` (add a constant; wire the fade around the `options_menu()` call, currently line 129; add a defensive reset to the soft-reset re-entry path, currently around line 101)

**Interfaces:**
- Consumes: `menu_fade_out(int)`, `menu_fade_reset(void)` (Task 3, `menu.h`, already included in `main.cxx`).
- Produces: no new symbols.

**Note on testing:** no host-runnable test surface. This is also the last task in the plan — Step 4 below is a build+manual verification, which the user must run (the assistant does not run `saturn/compile.bat` or observe hardware/emulator output).

- [ ] **Step 1: Add the quick-fade constant**

Find this exact text in `saturn/src/main.cxx` (this was added by Task 1, Step 5 — confirm it reads exactly this before proceeding):
```cpp
// Ramp length for the title-screen exit fade: ~1.5s at 60fps, matching the
// boot splash's own SPLASH_FADE_FRAMES speed since this happens once per
// session rather than repeatedly during navigation (contrast QUICK_FADE_FRAMES
// in Task 5, below).
#define TITLE_FADE_FRAMES 90

```

Replace it with:
```cpp
// Ramp length for the title-screen exit fade: ~1.5s at 60fps, matching the
// boot splash's own SPLASH_FADE_FRAMES speed since this happens once per
// session rather than repeatedly during navigation (contrast
// QUICK_FADE_FRAMES, below, used for the mode-select-menu/Options hop).
#define TITLE_FADE_FRAMES 90

// Ramp length for the mode-select-menu <-> Options fade: ~0.25s at 60fps,
// matching options.cxx's and menu_pages.cxx's identical QUICK_FADE_FRAMES
// (each file keeps its own copy rather than sharing a header constant, per
// this codebase's existing convention).
#define QUICK_FADE_FRAMES 15

```

- [ ] **Step 2: Wire the fade around the `options_menu()` call**

Find this exact text in `saturn/src/main.cxx` (currently line 129):
```cpp
        if (mode == 3) { options_menu(); continue; }
```

Replace it with:
```cpp
        if (mode == 3) {
            menu_fade_out(QUICK_FADE_FRAMES);
            options_menu();
            menu_fade_reset();   // instant reveal -- see menu.h for why this
                                  // hop can't use a ramped menu_fade_in
            continue;
        }
```

- [ ] **Step 3: Add the defensive reset to the soft-reset re-entry path**

Find this exact text in `saturn/src/main.cxx` (currently lines 100-103):
```cpp
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    slScrWindowModeNbg0(0);
    console_init();
```

Replace it with:
```cpp
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    slScrWindowModeNbg0(0);
    menu_fade_reset();   // in case a reset chord fired mid-ramp and left a
                          // color offset stuck non-zero (see menu.h)
    console_init();
```

- [ ] **Step 4: Read-through self-check**

Confirm:
1. `menu_fade_out(QUICK_FADE_FRAMES)` runs before `options_menu()` is called (darkening the mode-select menu's own last-drawn frame), and `menu_fade_reset()` runs after `options_menu()` returns (instantly reverealing whatever `menu_select("Z-ATURN", ...)` draws next, per Task 4's internal fade-out already having darkened `options_menu`'s own content before it returned).
2. The defensive reset in the soft-reset re-entry path sits after `slScrWindowModeNbg0(0)` and before `console_init()` -- alongside the other VDP2-state cleanup this path already does, and before anything else redraws the screen.
3. `main.cxx` still includes `menu.h` (added long before this plan; unchanged) -- `menu_fade_out`/`menu_fade_reset` resolve without a new include.

- [ ] **Step 5: Build (user step)**

Ask the user to run `saturn/compile.bat`. Expected: clean build. Likely error sources if it doesn't build clean:
- A missing `int16_t` -- `<srl.hpp>` should already provide it everywhere this plan touches (matching the boot splash's precedent in `splash.cxx`), but if not, the fix is `#include <cstdint>` in the affected file.
- A mismatched `menu_fade_out`/`display_fade_out` signature between a header declaration and its definition.

- [ ] **Step 6: Manual verification (user step, on emulator or hardware)**

Ask the user to confirm:
1. Leaving the title screen: `HOUSE.TGA` fades to black, then the configured appearance (color or picture, per the player's saved Display Options) fades up.
2. In Display Options: cycling Background, Text, and Palette all show a quick fade rather than an instant swap; cycling Palette past a broken/unreadable image (if any exist on the disc) shows no flicker.
3. Opening Options from the mode-select menu: the mode-select menu fades to black, then Options fades up.
4. Opening each of Options' sub-pages (Network/Config, Controls -- both the keyboard-visible and pad-only variants -- the nested Configure Controls screen reached from Controls, Display, Sound, Credits): each fades in on entry and out on exit, with Options itself visibly staying dark across the boundary rather than flashing.
5. Backing all the way out of Options to the mode-select menu: Options fades to black, then the mode-select menu appears immediately at full brightness (no stuck-black screen, no ramp on this specific hop -- that's expected, see Decision 4 in the design spec).
6. Popups untouched by this plan still cut instantly: the "Return to the title screen?" Y/N confirm, save/load result messages, and the on-screen/physical keyboard entry screens.
7. Trigger a soft reset (the existing reset chord) while a fade is visibly mid-ramp, if practical, and confirm the title screen comes back at normal brightness rather than stuck dark or tinted.

- [ ] **Step 7: Commit**

```bash
cd "C:/Users/saggl/CLionProjects/saturn-mojozork"
git add saturn/src/main.cxx
git commit -m "Fade the mode-select-menu/Options hop; reset offsets on soft-reset re-entry"
```

---

## Self-Review

**1. Spec coverage:**
- "Appearance-only fade for title exit and all 3 Display Options rows" → Task 1 (`display_fade_step/out/in/reset`, `title_bg_fade_out`, title-exit wiring), Task 2 (row-cycling wiring, all 3 rows). ✓
- "Whole-screen fade for page-to-page menu navigation, both box and text together" → Task 3 (`menu_fade_out/in/reset`, reusing `display_fade_step`). ✓
- "Every page function fades in on entry, out on exit" → Task 4, all 8 functions. ✓
- "Sub-page dispatches (Options' 5, Controls' 1) wrap with fade-out-before/fade-in-after" → Task 4, Steps 4 and 9. ✓
- "Mode-select-menu/Options hop: true fade-out, instant reset instead of ramped fade-in" → Task 5, Step 2. ✓
- "Every fade-out paired with a fade-in or reset" → verified per-function in Task 4 Step 10, and the defensive reset in Task 5 Step 3 covers the one path (a mid-ramp soft reset) that could otherwise violate this. ✓
- Out-of-scope items from the spec (popups, `online_mode`, `game_select`, save/restore pickers, a true ramped fade-in for the mode-select-menu reveal) → not implemented anywhere in this plan, called out explicitly in Task 5 Step 6.6. ✓

**2. Placeholder scan:** No "TBD"/"TODO"/vague steps; every code step shows complete code; every "Files" block names exact paths and current line numbers, re-verified against the live files immediately before writing this plan (including `title.cxx`'s current post-boot-splash state). ✓

**3. Type consistency:** `display_fade_step(int)`, `display_fade_out(int)`, `display_fade_in(int)`, `display_fade_reset(void)` — identical signatures across `options.h` (Task 1 Step 1) and `options.cxx` (Task 1 Step 2), called with matching signatures from `options.cxx`'s `display_cycle_row` (Task 2), `menu.cxx` (Task 3), and `main.cxx` (Task 1 Step 5, Task 5). `menu_fade_out(int)`, `menu_fade_in(int)`, `menu_fade_reset(void)` — identical across `menu.h` (Task 3 Step 1) and `menu.cxx` (Task 3 Step 2), called consistently from `menu_pages.cxx` (Task 4) and `main.cxx` (Task 5). `title_bg_fade_out(int)` — identical across `title.h` and `title.cxx` (Task 1 Steps 3-4), called once from `main.cxx` (Task 1 Step 5). `QUICK_FADE_FRAMES` is defined independently in `options.cxx` (Task 2), `menu_pages.cxx` (Task 4), and `main.cxx` (Task 5) at the same value (15) — a deliberate, documented duplication (see Global Constraints), not a drift risk since none of these files share the literal via a header. ✓
