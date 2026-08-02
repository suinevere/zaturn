# Options Menu Save/Load + Gameplay Sub-page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add "Save Game" and "Load Game" rows to the Options menu, visible only while a game is in progress, and move the Difficulty slider out of the top-level Options list into its own new "Gameplay" sub-page.

**Architecture:** A new global `g_in_game` (false before a game loads and after a soft-reset back to the title, true from just after `mojo_boot()` succeeds) gates the two new rows. `options_menu()` changes from `void` to returning `OM_NONE`/`OM_SAVE`/`OM_RESTORE`, since the menu itself cannot serialize or restore game state — only the interpreter's own save/restore opcodes can, and those only run off text that reaches `saturn_readline`. When the in-game caller (`saturn_glue.cxx`) sees `OM_SAVE`/`OM_RESTORE` come back, it calls the same `submit_command(k, "save"/"restore")` helper the F2/F3 quick keys already use, so the actual save/restore runs through the normal, already-tested path. Difficulty moves into a new file-local `gameplay_page()` in `menu_pages.cxx`, following the same snapshot/OK/Cancel pattern as the other sub-pages.

**Tech Stack:** C/C++ (SaturnRingLib / SGL), Sega Saturn VDP2. Build via `saturn/compile.bat` (run by the user, never by the assistant).

## Global Constraints

- **Build command:** `saturn/compile.bat` — the assistant does NOT run it; the user builds. Make edits only.
- **No host-runnable test surface for this code** — every file touched is Saturn cross-compiled (SRL/SGL, `MenuBacking`, VDP2 calls) with no host-gcc-testable pure logic added or changed (unlike `menu_layout.c`, which this plan does not touch). Verification is a read-through self-check after each task, plus a final build + manual hardware/emulator check by the user.
- **Save Game / Load Game always use the full device/slot picker** — matching the F2 (save) / F3 (restore) keys, never the F5/F6/F9 quick-repeat-last-slot behavior. Do not pre-arm `g_save_device`/`g_restore_device` for these menu rows.
- **No confirmation dialog on Load Game beyond what F3/F6/F9 already have** (none) — a restore triggered from the menu can discard unsaved progress with no prompt, exactly like the existing quick keys.
- **`g_in_game` gates only Save Game / Load Game.** Gameplay, Display, Sound, Controls, Network, Credits, Return to Title, and Done remain available both pre-game (from the title mode-menu) and in-game, exactly as `options_menu()` already works today.
- **Every one of `menu_pages.cxx`'s modal page functions is a self-contained loop** that draws its first frame, loops on input, then exits via `break` to a single post-loop point. `gameplay_page()` must follow this same shape (see `keyboard_controls_page`/`sound_options_page` for the pattern).

---

## File Structure

- Modify: `saturn/src/engine/app_state.h` — declare `g_in_game`.
- Modify: `saturn/src/engine/app_state.cxx` — define `g_in_game`.
- Modify: `saturn/src/main.cxx` — set/reset `g_in_game`; update its own doc-comment globals list.
- Modify: `saturn/src/menu/menu_pages.h` — declare the `OM_NONE`/`OM_SAVE`/`OM_RESTORE` result enum; change `options_menu`'s signature and doc comment.
- Modify: `saturn/src/menu/menu_pages.cxx` — add new file-local `gameplay_page()`; restructure `options_menu()`'s item list, layout, and return value.
- Modify: `saturn/src/engine/saturn_glue.cxx` — consume `options_menu()`'s return value at the F10/START/Esc call site and submit "save"/"restore" accordingly.

No new files, no Makefile changes.

---

### Task 1: `g_in_game` global + `main.cxx` wiring

**Files:**
- Modify: `saturn/src/engine/app_state.h` (insert after `g_story_filename`'s declaration, currently lines 92-94)
- Modify: `saturn/src/engine/app_state.cxx` (insert after `g_story_filename`'s definition, currently lines 88-94)
- Modify: `saturn/src/main.cxx` (doc-comment globals list at lines 87-90; re-entry reset at lines 108-111; post-`mojo_boot` set at lines 200-203)

**Interfaces:**
- Produces: `extern bool g_in_game;` (declared in `app_state.h`, defined in `app_state.cxx`) — consumed by Task 4's `options_menu()`.

**Note on testing:** no host-runnable test surface for this Saturn cross-compiled code; verification is a read-through self-check plus, at the end of the whole plan, a build + manual check by the user.

- [ ] **Step 1: Declare `g_in_game` in `app_state.h`**

Find this exact text in `saturn/src/engine/app_state.h` (currently lines 92-99):
```c
// Story file currently loaded from CD (set by main after game selection);
// re-read by saturn_read_story_file for save/restart.
extern const char *g_story_filename;

// Console scroll offset from the live bottom, in lines (0 = latest text).
// Written by the input module (scroll_handle_key, pad_scroll_update) and read
// by console_view's render_console.
extern int g_scroll;
```

Replace it with:
```c
// Story file currently loaded from CD (set by main after game selection);
// re-read by saturn_read_story_file for save/restart.
extern const char *g_story_filename;

// True once a game is loaded and running (set by main after mojo_boot
// succeeds); false before then and after a soft-reset back to the title.
// Gates the Options menu's Save Game / Load Game rows.
extern bool g_in_game;

// Console scroll offset from the live bottom, in lines (0 = latest text).
// Written by the input module (scroll_handle_key, pad_scroll_update) and read
// by console_view's render_console.
extern int g_scroll;
```

- [ ] **Step 2: Define `g_in_game` in `app_state.cxx`**

Find this exact text in `saturn/src/engine/app_state.cxx` (currently lines 88-102):
```cpp
/*----------------------
 | g_story_filename
 | Description: The loaded story's CD filename; drives per-game save-slot names and
 |   is re-read by saturn_read_story_file for save/restart.
 | Author: suinevere
 ----------------------*/
const char *g_story_filename = "ZORK1.Z3";

/*----------------------
 | g_scroll
 | Description: The console scrollback position (0 = live bottom). Written by the
 |   input module's scroll handlers, read by console_view's renderer.
 | Author: suinevere
 ----------------------*/
int g_scroll = 0;
```

Replace it with:
```cpp
/*----------------------
 | g_story_filename
 | Description: The loaded story's CD filename; drives per-game save-slot names and
 |   is re-read by saturn_read_story_file for save/restart.
 | Author: suinevere
 ----------------------*/
const char *g_story_filename = "ZORK1.Z3";

/*----------------------
 | g_in_game
 | Description: True once a game is loaded and running; false before then and
 |   after a soft-reset back to the title. Gates the Options menu's Save
 |   Game / Load Game rows -- there is nothing to save or restore before a
 |   game has loaded.
 | Author: suinevere
 ----------------------*/
bool g_in_game = false;

/*----------------------
 | g_scroll
 | Description: The console scrollback position (0 = live bottom). Written by the
 |   input module's scroll handlers, read by console_view's renderer.
 | Author: suinevere
 ----------------------*/
int g_scroll = 0;
```

- [ ] **Step 3: Update `main.cxx`'s doc-comment globals list**

Find this exact text in `saturn/src/main.cxx` (currently lines 87-90):
```cpp
 | Globals: g_display, g_pad, g_title_jmp, g_title_jmp_armed, g_z3_dir_valid,
 |   g_menu_backing_depth, g_music_level, g_pcm_level, g_mix_mode, g_sel_track,
 |   g_story_filename, g_restore_device, g_restore_slot, g_autocmd, g_output_start
 | Params: N/A
```

Replace it with:
```cpp
 | Globals: g_display, g_pad, g_title_jmp, g_title_jmp_armed, g_z3_dir_valid,
 |   g_menu_backing_depth, g_music_level, g_pcm_level, g_mix_mode, g_sel_track,
 |   g_story_filename, g_restore_device, g_restore_slot, g_autocmd,
 |   g_output_start, g_in_game
 | Params: N/A
```

- [ ] **Step 4: Reset `g_in_game` at the soft-reset re-entry point**

Find this exact text in `saturn/src/main.cxx` (currently lines 107-111):
```cpp
    GFS_Reset();
    cd_capture_root();
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    slScrWindowModeNbg0(0);
```

Replace it with:
```cpp
    GFS_Reset();
    cd_capture_root();
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    g_in_game = false;
    slScrWindowModeNbg0(0);
```

- [ ] **Step 5: Set `g_in_game` once the game has actually loaded**

Find this exact text in `saturn/src/main.cxx` (currently lines 200-204):
```cpp
    mojo_boot(story, len, seed);

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
```

Replace it with:
```cpp
    mojo_boot(story, len, seed);
    g_in_game = true;

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
```

- [ ] **Step 6: Read-through self-check**

Confirm: `g_in_game` is declared exactly once in `app_state.h` and defined exactly once in `app_state.cxx`, both matching `bool` type; `main.cxx` sets it `false` at the one re-entry point and `true` at the one post-`mojo_boot` point; no other file references it yet (Task 4 adds the only reader).

- [ ] **Step 7: Commit**

```bash
git add saturn/src/engine/app_state.h saturn/src/engine/app_state.cxx saturn/src/main.cxx
git commit -m "Add g_in_game global, set around game load/soft-reset"
```

---

### Task 2: `options_menu()` result enum + header update

**Files:**
- Modify: `saturn/src/menu/menu_pages.h` (replace `options_menu`'s declaration, currently lines 19-32)

**Interfaces:**
- Consumes: none new.
- Produces: `enum { OM_NONE = 0, OM_SAVE, OM_RESTORE };` and `int options_menu(void);` (both in `menu_pages.h`) — consumed by Task 4 (the new `options_menu()` body returns these) and Task 5 (`saturn_glue.cxx` reads the return value).

**Note on testing:** no host-runnable test surface; this is a declaration-only change, verified by read-through (Task 4 and Task 5 will not compile if this doesn't match what they use).

- [ ] **Step 1: Replace the `options_menu` declaration and doc comment**

Find this exact text in `saturn/src/menu/menu_pages.h` (currently lines 19-32):
```c
/*----------------------
 | options_menu
 | Description: Opens the Options menu: a difficulty slider plus Network,
 |   Controls, Display, Sound (shown only when there is audio to configure),
 |   Return to Title, and Done. Blocks until the player picks Done, backs out
 |   with B/Esc, or confirms Return to Title (which soft-resets and does not
 |   return). Persists the difficulty change, if any, on exit.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_menu(void);
```

Replace it with:
```c
/*----------------------
 | OM_NONE / OM_SAVE / OM_RESTORE
 | Description: options_menu()'s result. OM_SAVE/OM_RESTORE mean the player
 |   picked Save Game/Load Game (shown only while g_in_game is set); the menu
 |   itself cannot serialize or restore game state, so the caller must submit
 |   the matching "save"/"restore" command through the normal input path
 |   (saturn_glue.cxx's saturn_readline does this via submit_command, the
 |   same helper the F2/F3 quick keys use). OM_NONE covers every other exit
 |   (Done, B/Esc, or Return to Title, which never returns since it calls
 |   soft_reset_to_title()).
 | Author: suinevere
 ----------------------*/
enum { OM_NONE = 0, OM_SAVE, OM_RESTORE };

/*----------------------
 | options_menu
 | Description: Opens the Options menu: Save Game and Load Game (shown only
 |   while a game is in progress), Gameplay, Display, Sound (shown only when
 |   there is audio to configure), Controls, Network, Credits, Return to
 |   Title, and Done. Blocks until the player picks one. Save Game/Load Game
 |   close the menu immediately and report which was picked via the return
 |   value (see OM_SAVE/OM_RESTORE above) without performing the save/restore
 |   themselves. Return to Title confirms via menu_confirm and, on yes,
 |   soft-resets to the title screen (never returns). Every other exit
 |   returns OM_NONE.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_in_game, g_difficulty
 | Params: N/A
 | Returns: OM_NONE, OM_SAVE, or OM_RESTORE
 ----------------------*/
int options_menu(void);
```

- [ ] **Step 2: Read-through self-check**

Confirm: the enum values are `OM_NONE = 0`, `OM_SAVE`, `OM_RESTORE` (in that order, so `OM_NONE` is falsy); `options_menu` returns `int`, not the enum type by name (matches this codebase's plain-`enum`-as-`int` style used elsewhere, e.g. `DIFF_EASY`/`DIFF_MEDIUM`/`DIFF_HARD` in `app_state.h`).

- [ ] **Step 3: Commit**

```bash
git add saturn/src/menu/menu_pages.h
git commit -m "Declare options_menu() result enum and new int signature"
```

---

### Task 3: New `gameplay_page()` sub-page

**Files:**
- Modify: `saturn/src/menu/menu_pages.cxx` (insert between `credits_page`'s closing brace and `options_menu`'s doc comment, currently lines 932-934)

**Interfaces:**
- Consumes: `MenuBacking`, `menu_clear`/`menu_frame`/`menu_sync`, `page_fade_out`/`page_fade_in`, `menu_digit_row`, `note_input_device`/`hint`, `g_kbd_visible`, `g_pad`, `check_soft_reset`, `saturn_keyboard_poll`, `options_save`, `g_difficulty`, `DIFF_EASY`/`DIFF_HARD` (all already visible in this file, used identically by the current inline Difficulty code in `options_menu` and by `keyboard_controls_page`/`sound_options_page`).
- Produces: `static void gameplay_page(void);` (file-local, no header declaration) — called by Task 4's restructured `options_menu()`.

**Note on testing:** no host-runnable test surface; verified by read-through against the existing sub-page pattern (`keyboard_controls_page` is the closest analog: snapshot on entry, OK commits + saves, Cancel/B/Esc restores).

- [ ] **Step 1: Insert `gameplay_page()` before `options_menu`**

Find this exact text in `saturn/src/menu/menu_pages.cxx` (currently lines 930-935):
```cpp
    page_fade_out(g_menu_page_fade);
    SRL::Core::Synchronize();
}

/*----------------------
 | options_menu
```

Replace it with:
```cpp
    page_fade_out(g_menu_page_fade);
    SRL::Core::Synchronize();
}

/*----------------------
 | gameplay_page
 | Description: Gameplay Options (full-screen box, OK/Cancel). Currently just
 |   the Difficulty slider (Easy/Medium/Hard) and its description line, moved
 |   out of the top-level Options box so that list can stay plain dispatch
 |   rows. Left/Right adjust a local `diff` copy; OK commits it to
 |   g_difficulty and calls options_save() only if it actually changed;
 |   Cancel (or B/Esc) discards the local copy, leaving g_difficulty
 |   untouched. Reached only from the Options menu's Gameplay row.
 | Author: suinevere
 | Dependencies: options.c (options_save), console_view.c (note_input_device/
 |   hint/g_kbd_visible), input.c (pad_repeat_update), menu.c, soft_reset.h
 |   (check_soft_reset)
 | Globals: g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void gameplay_page(void) {
    MenuBacking backing;
    static const char *const NAMES[] = { "Easy", "Medium", "Hard" };
    static const char *const DESC[]  = { "Walkthrough steps only",
                                         "Valid-command typeahead",
                                         "Typeahead off" };
    enum { GR_DIFF, GR_OK, GR_CANCEL };
    const int nrows = 3;
    int sel = 0;
    int diff = g_difficulty;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                    || ke.kind == SATURN_KEY_BACKSPACE;
        if (menu_digit_row(ke, nrows, sel, left, right)) ok = true;

        if (cancel || (ok && sel == GR_CANCEL)) break;
        if (sel == GR_DIFF) { if (left && diff > DIFF_EASY) diff--; if (right && diff < DIFF_HARD) diff++; }
        else if (ok && sel == GR_OK) {
            if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
            break;
        }

        menu_clear();
        const int fx = 1, fy = 8, fw = 38, fh = 13;
        menu_frame(fx, fy, fw, fh, "GAMEPLAY");
        int x = fx + 2, y = fy + 3;
        bool nums = !g_kbd_visible;
        char dmark = sel == GR_DIFF ? '>' : ' ';
        if (nums) SRL::Debug::Print(x, y, "%c 1) Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        else      SRL::Debug::Print(x, y, "%c    Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        SRL::Debug::Print(x + 4, y + 1, "%s", DESC[diff]);
        y += 3;
        if (nums) SRL::Debug::Print(x, y++, "%c 2) OK", sel == GR_OK ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    OK", sel == GR_OK ? '>' : ' ');
        if (nums) SRL::Debug::Print(x, y++, "%c 3) Cancel", sel == GR_CANCEL ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Cancel", sel == GR_CANCEL ? '>' : ' ');
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("Up/Dn <>diff  A/Start=OK B=Cancel",
                                             "Up/Dn <>diff  Enter=OK Esc=Cancel"));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    SRL::Core::Synchronize();
}

/*----------------------
 | options_menu
```

- [ ] **Step 2: Read-through self-check**

Confirm: `gameplay_page` is `static` (file-local, matching `config_page`/`display_options_page`/`credits_page`, not the public-header trio `keyboard_controls_page`/`sound_options_page`/`options_menu`); Cancel/B/Esc never touch `g_difficulty` or call `options_save()`; OK only calls `options_save()` when `diff` actually changed (matching the old inline behavior in `options_menu`, which the spec requires preserved); the row count (`nrows = 3`) matches the three cases `menu_digit_row` and the up/down wraparound use.

- [ ] **Step 3: Commit**

```bash
git add saturn/src/menu/menu_pages.cxx
git commit -m "Add gameplay_page sub-page (Difficulty moved out of Options)"
```

---

### Task 4: Restructure `options_menu()`

**Files:**
- Modify: `saturn/src/menu/menu_pages.cxx` (file header dependency comment at lines 20-23; the whole `options_menu` function, currently lines 934-1073)

**Interfaces:**
- Consumes: `g_in_game` (Task 1), `OM_NONE`/`OM_SAVE`/`OM_RESTORE` (Task 2), `gameplay_page()` (Task 3), everything the old `options_menu` already used (`music_cdda_has_audio`, `sound_has_audio`, `menu_confirm`, `soft_reset_to_title`, `config_page`, `controls_page`, `keyboard_controls_page`, `display_options_page`, `sound_options_page`, `credits_page`).
- Produces: `int options_menu(void)` returning `OM_NONE`/`OM_SAVE`/`OM_RESTORE` — consumed by Task 5.

**Note on testing:** no host-runnable test surface; verified by read-through against the spec's required row order and the "always full picker" / "no confirm on Load" constraints above.

- [ ] **Step 1: Add `g_in_game` to the file's dependency doc comment**

Find this exact text in `saturn/src/menu/menu_pages.cxx` (currently lines 20-23):
```cpp
 |   g_kbd_visible/g_caret_arrows), options.h (options_save/display_apply/
 |   display_cycle_row/valid_dialnum), app_state.h (g_difficulty/g_dialnum/
 |   g_display/g_mix_mode/g_sel_track/g_music_level/g_pcm_level), keyboard.h,
 |   saturn_keyboard.h, soft_reset.h, display.h, sound.h, music.h, SRL
```

Replace it with:
```cpp
 |   g_kbd_visible/g_caret_arrows), options.h (options_save/display_apply/
 |   display_cycle_row/valid_dialnum), app_state.h (g_difficulty/g_dialnum/
 |   g_display/g_mix_mode/g_sel_track/g_music_level/g_pcm_level/g_in_game),
 |   keyboard.h, saturn_keyboard.h, soft_reset.h, display.h, sound.h, music.h,
 |   SRL
```

- [ ] **Step 2: Replace the entire `options_menu` function**

Find this exact text in `saturn/src/menu/menu_pages.cxx` (currently lines 934-1073 — the whole function, start to end):
```cpp
/*----------------------
 | options_menu
 | Description: Options menu (centered box): a difficulty slider plus
 |   actions (Network, Controls, Display, Sound, Credits, Return to Title,
 |   Done). Builds a dynamic item list -- Difficulty is always items[0];
 |   Network, Controls, Display, and Credits are always present (none has a
 |   hardware dependency); Sound appears only when there is audio to
 |   configure (CD-DA on the disc or the game's .BLB); Return to Title and
 |   Done always follow, with Credits sitting just above them so it reads as
 |   the last "informational" row before the exit actions. Up/Down select a
 |   row with wraparound; on Difficulty, Left/Right adjust a local `diff`
 |   copy (committed to g_difficulty and saved only on exit, and only if it
 |   actually changed) while every other row ignores direction. A digit-row
 |   match is resolved before `item` is read (it can move `sel`) and OR'd
 |   into the activation flag; direction only matters on the difficulty
 |   slider, so every other row ignores left/right and Difficulty ignores
 |   activation. Activating dispatches to the matching sub-page (config_page;
 |   controls_page or keyboard_controls_page depending on g_kbd_visible;
 |   display_options_page; sound_options_page; credits_page), or for Return
 |   to Title, confirms via menu_confirm and, on yes, commits any difficulty
 |   change before calling soft_reset_to_title() (which never returns).
 |   Redraws with an unconditional menu_clear() before menu_frame() every
 |   frame -- MenuBacking only suppresses the image inside its own box
 |   rectangle, and does nothing to leftover text OUTSIDE it, so without this
 |   the wider menu that opened Options (e.g. the Single/Multiplayer list)
 |   would show through around this box. The box is one row taller (h=15)
 |   than the six-row layout needs, since Credits can push the visible list
 |   to seven rows when Sound is also present; the hint row moves from y0+13
 |   to y0+14 to match. On exit (Done or B/Esc), commits any difficulty
 |   change and options_save()s it, then blocks on menu_sync() until
 |   B/A/C/Start are all released, so the button that closed this menu
 |   cannot leak into whatever reads input next.
 | Author: suinevere
 | Dependencies: options.c (options_save), music.c (music_cdda_has_audio),
 |   sound.c (sound_has_audio), menu.c (menu_confirm), soft_reset.h
 |   (soft_reset_to_title, check_soft_reset), console_view.c
 |   (note_input_device/hint/g_kbd_visible), menu_pages.cxx (config_page/
 |   controls_page/keyboard_controls_page/display_options_page/
 |   sound_options_page/credits_page)
 | Globals: g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_menu(void) {
    MenuBacking backing;
    static const char *const NAMES[] = { "Easy", "Medium", "Hard" };
    static const char *const DESC[]  = { "Walkthrough steps only",
                                         "Valid-command typeahead",
                                         "Typeahead off" };
    const int x0 = 5, y0 = 8, w = 30, h = 16;
    enum { OI_DIFF, OI_CONFIG, OI_CONTROLS, OI_DISPLAY, OI_SOUND, OI_CREDITS, OI_RETURN, OI_DONE };
    bool sound_available = (music_cdda_has_audio() != 0) || (sound_has_audio() != 0);
    int items[8], nitems = 0;
    items[nitems++] = OI_DIFF;
    items[nitems++] = OI_CONFIG;
    items[nitems++] = OI_CONTROLS;
    items[nitems++] = OI_DISPLAY;
    if (sound_available) items[nitems++] = OI_SOUND;
    items[nitems++] = OI_CREDITS;
    items[nitems++] = OI_RETURN;
    items[nitems++] = OI_DONE;

    int diff = g_difficulty, sel = 0;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nitems) % nitems;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nitems;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool digit = menu_digit_row(ke, nitems, sel, left, right);
        int item = items[sel];
        if (item == OI_DIFF) { if (left && diff > DIFF_EASY) diff--; if (right && diff < DIFF_HARD) diff++; }
        bool act = digit
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                  || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) break;
        if (act) {
            if (item == OI_CONFIG) { page_fade_out(g_menu_page_fade); config_page(); need_fade_in = true; }
            else if (item == OI_CONTROLS) {
                page_fade_out(g_menu_page_fade);
                if (g_kbd_visible) controls_page(); else keyboard_controls_page();
                menu_clear();
                need_fade_in = true;
            }
            else if (item == OI_DISPLAY) { page_fade_out(g_menu_page_fade); display_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_SOUND) { page_fade_out(g_menu_page_fade); sound_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_CREDITS) { page_fade_out(g_menu_page_fade); credits_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_RETURN) {
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    if (diff != g_difficulty) { g_difficulty = diff; options_save(); }
                    soft_reset_to_title();
                }
            }
            else if (item == OI_DONE) break;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "OPTIONS");
        bool nums = !g_kbd_visible;
        char dmark = item == OI_DIFF ? '>' : ' ';
        if (nums) SRL::Debug::Print(x0 + 2, y0 + 3, "%c 1) Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        else      SRL::Debug::Print(x0 + 2, y0 + 3, "%c    Difficulty: %s %s %s", dmark,
                          diff > DIFF_EASY ? "<" : " ", NAMES[diff], diff < DIFF_HARD ? ">" : " ");
        SRL::Debug::Print(x0 + 2, y0 + 4, "    %s", DESC[diff]);
        int ay = y0 + 6;
        for (int i = 0; i < nitems; i++) {
            char cur = (i == sel) ? '>' : ' ';
            const char *label = 0;
            switch (items[i]) {
                case OI_DIFF: continue;
                case OI_CONFIG:   label = "Network";         break;
                case OI_CONTROLS: label = "Controls";        break;
                case OI_DISPLAY:  label = "Display";         break;
                case OI_SOUND:    label = "Sound";           break;
                case OI_CREDITS:  label = "Credits";         break;
                case OI_RETURN:   label = "Return to Title"; break;
                case OI_DONE:     label = "Done";            break;
            }
            if (nums) SRL::Debug::Print(x0 + 2, ay++, "%c %d) %s", cur, i + 1, label);
            else      SRL::Debug::Print(x0 + 2, ay++, "%c    %s", cur, label);
        }
        SRL::Debug::Print(x0 + 2, y0 + 14, "%s", hint("Up/Dn A=pick  <>=diff", "Up/Dn Enter  B=back"));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
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
/*----------------------
 | options_menu
 | Description: Options menu (centered box): Save Game and Load Game (shown
 |   only while a game is in progress), Gameplay, Display, Sound (shown only
 |   when there is audio to configure), Controls, Network, Credits, Return to
 |   Title, and Done. Builds a dynamic item list -- Save Game/Load Game lead
 |   when g_in_game is set; Gameplay, Display, Controls, Network, and Credits
 |   are always present (none has a hardware dependency); Sound appears only
 |   when there is audio to configure (CD-DA on the disc or the game's .BLB);
 |   Return to Title and Done always follow, with Credits sitting just above
 |   them so it reads as the last "informational" row before the exit
 |   actions. Up/Down select a row with wraparound. A digit-row match is
 |   resolved before `item` is read (it can move `sel`) and OR'd into the
 |   activation flag. Activating dispatches to the matching sub-page
 |   (gameplay_page; config_page; controls_page or keyboard_controls_page
 |   depending on g_kbd_visible; display_options_page; sound_options_page;
 |   credits_page), or for Return to Title, confirms via menu_confirm and, on
 |   yes, calls soft_reset_to_title() (which never returns). Save Game and
 |   Load Game close the menu immediately like Done, but report which was
 |   picked through the return value instead of performing the save/restore
 |   themselves -- the menu has no access to the interpreter's game state, so
 |   the caller (saturn_glue.cxx's saturn_readline) must submit the matching
 |   "save"/"restore" command itself, the same way the F2/F3 quick keys do.
 |   Redraws with an unconditional menu_clear() before menu_frame() every
 |   frame -- MenuBacking only suppresses the image inside its own box
 |   rectangle, and does nothing to leftover text OUTSIDE it, so without this
 |   the wider menu that opened Options (e.g. the Single/Multiplayer list)
 |   would show through around this box. The box is sized for the worst case
 |   -- in-game with Sound present, 10 rows -- the same way it was already
 |   sized for its own prior worst case (7 rows) regardless of how many rows
 |   actually draw; removing the old inline Difficulty slider (now
 |   gameplay_page's job) freed exactly the three rows the two new Save/Load
 |   rows need, so the box's position and size (x0/y0/w/h) and the hint row's
 |   position (y0+14) are unchanged from before. On exit (Done, Save Game,
 |   Load Game, or B/Esc), blocks on menu_sync() until B/A/C/Start are all
 |   released, so the button that closed this menu cannot leak into whatever
 |   reads input next.
 | Author: suinevere
 | Dependencies: options.c (options_save, via gameplay_page), music.c
 |   (music_cdda_has_audio), sound.c (sound_has_audio), menu.c
 |   (menu_confirm), soft_reset.h (soft_reset_to_title, check_soft_reset),
 |   console_view.c (note_input_device/hint/g_kbd_visible), menu_pages.cxx
 |   (gameplay_page/config_page/controls_page/keyboard_controls_page/
 |   display_options_page/sound_options_page/credits_page)
 | Globals: g_in_game
 | Params: N/A
 | Returns: OM_NONE, OM_SAVE, or OM_RESTORE
 ----------------------*/
int options_menu(void) {
    MenuBacking backing;
    const int x0 = 5, y0 = 8, w = 30, h = 16;
    enum { OI_SAVE, OI_LOAD, OI_GAMEPLAY, OI_DISPLAY, OI_SOUND, OI_CONTROLS,
           OI_CONFIG, OI_CREDITS, OI_RETURN, OI_DONE };
    bool sound_available = (music_cdda_has_audio() != 0) || (sound_has_audio() != 0);
    int items[10], nitems = 0;
    if (g_in_game) { items[nitems++] = OI_SAVE; items[nitems++] = OI_LOAD; }
    items[nitems++] = OI_GAMEPLAY;
    items[nitems++] = OI_DISPLAY;
    if (sound_available) items[nitems++] = OI_SOUND;
    items[nitems++] = OI_CONTROLS;
    items[nitems++] = OI_CONFIG;
    items[nitems++] = OI_CREDITS;
    items[nitems++] = OI_RETURN;
    items[nitems++] = OI_DONE;

    int sel = 0;
    int result = OM_NONE;
    SRL::Core::Synchronize();
    bool need_fade_in = true;
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nitems) % nitems;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nitems;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool digit = menu_digit_row(ke, nitems, sel, left, right);
        int item = items[sel];
        bool act = digit
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                 || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool back = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                  || ke.kind == SATURN_KEY_BACKSPACE;
        if (back) break;
        if (act) {
            if (item == OI_SAVE) { result = OM_SAVE; break; }
            else if (item == OI_LOAD) { result = OM_RESTORE; break; }
            else if (item == OI_GAMEPLAY) { page_fade_out(g_menu_page_fade); gameplay_page(); need_fade_in = true; }
            else if (item == OI_CONFIG) { page_fade_out(g_menu_page_fade); config_page(); need_fade_in = true; }
            else if (item == OI_CONTROLS) {
                page_fade_out(g_menu_page_fade);
                if (g_kbd_visible) controls_page(); else keyboard_controls_page();
                menu_clear();
                need_fade_in = true;
            }
            else if (item == OI_DISPLAY) { page_fade_out(g_menu_page_fade); display_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_SOUND) { page_fade_out(g_menu_page_fade); sound_options_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_CREDITS) { page_fade_out(g_menu_page_fade); credits_page(); menu_clear(); need_fade_in = true; }
            else if (item == OI_RETURN) {
                if (menu_confirm("Return to the title screen?", "Are you sure?")) {
                    soft_reset_to_title();
                }
            }
            else if (item == OI_DONE) break;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "OPTIONS");
        bool nums = !g_kbd_visible;
        int ay = y0 + 3;
        for (int i = 0; i < nitems; i++) {
            char cur = (i == sel) ? '>' : ' ';
            const char *label = 0;
            switch (items[i]) {
                case OI_SAVE:     label = "Save Game";       break;
                case OI_LOAD:     label = "Load Game";       break;
                case OI_GAMEPLAY: label = "Gameplay";        break;
                case OI_DISPLAY:  label = "Display";         break;
                case OI_SOUND:    label = "Sound";           break;
                case OI_CONTROLS: label = "Controls";        break;
                case OI_CONFIG:   label = "Network";         break;
                case OI_CREDITS:  label = "Credits";         break;
                case OI_RETURN:   label = "Return to Title"; break;
                case OI_DONE:     label = "Done";            break;
            }
            if (nums) SRL::Debug::Print(x0 + 2, ay++, "%c %d) %s", cur, i + 1, label);
            else      SRL::Debug::Print(x0 + 2, ay++, "%c    %s", cur, label);
        }
        SRL::Debug::Print(x0 + 2, y0 + 14, "%s", hint("Up/Dn A=pick", "Up/Dn Enter  B=back"));
        menu_sync();
        if (need_fade_in) { page_fade_in(g_menu_page_fade); need_fade_in = false; }
    }
    page_fade_out(g_menu_page_fade);
    while (g_pad->IsHeld(Button::B) || g_pad->IsHeld(Button::A) ||
           g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::START))
        menu_sync();
    return result;
}
```

- [ ] **Step 3: Read-through self-check**

Confirm, item by item:
- `items[10]` is large enough for the worst case (`g_in_game` true + `sound_available` true = 10 entries: SAVE, LOAD, GAMEPLAY, DISPLAY, SOUND, CONTROLS, CONFIG, CREDITS, RETURN, DONE).
- Save Game/Load Game only appear when `g_in_game` is true; every other row appears exactly as before regardless of `g_in_game`.
- `OI_SAVE`/`OI_LOAD` both `break` out of the `for(;;)` loop after setting `result`, reaching the same post-loop `page_fade_out`/button-release-wait/`return` as `OI_DONE` and `back` — so Save Game and Load Game close the menu exactly like Done.
- No path sets `result` to anything but `OM_NONE` (the initial value) except the two lines above.
- The row-label `switch` has a case for every value in the `OI_*` enum (10 cases) — nothing falls through to the uninitialized `label = 0`.
- The old `diff`/`OI_DIFF`/`DESC`/`NAMES` machinery is gone from this function entirely (it now lives only in `gameplay_page`, Task 3).
- `ay` starts at `y0 + 3` (no more 2-line Difficulty header before it) and the hint stays at the same `y0 + 14` as before — with `nitems` capped at 10, the last drawn row lands at `y0 + 12`, two rows clear of the hint, matching this box's existing margin convention.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/menu/menu_pages.cxx
git commit -m "Restructure options_menu: Save/Load rows, Gameplay dispatch, new row order"
```

---

### Task 5: Wire `saturn_glue.cxx`'s in-game call site

**Files:**
- Modify: `saturn/src/engine/saturn_glue.cxx` (doc comment at lines 183-186; F10/START/Esc branch at lines 235-242)

**Interfaces:**
- Consumes: `int options_menu(void)` and `OM_SAVE`/`OM_RESTORE` (Task 2/4), `static void submit_command(KeyboardState &k, const char *cmd)` (already defined earlier in this same file, at line 162, and already used by the F2/F5/F3/F6/F9 branches directly below this one).
- Produces: nothing new for other files — this is the last task.

**Note on testing:** no host-runnable test surface; verified by read-through, then by the user's hardware/emulator build at the end. Manual check once built: from in-game, press Start/Esc/F10, pick "Save Game" — should behave identically to pressing F2 (device picker, slot picker, name editor, "Overwrite?" if applicable, "Saved."/"Save FAILED" message); pick "Load Game" — should behave identically to pressing F3 (device picker, slot picker, loads or reports empty).

- [ ] **Step 1: Update the `saturn_readline` doc comment**

Find this exact text in `saturn/src/engine/saturn_glue.cxx` (currently lines 183-186):
```cpp
 |   The frame loop runs the soft-reset chord, the F10/F11/F12 menu shortcuts
 |   (Sound only when there is audio to configure), the F2/F5 save and
 |   F3/F6/F9 restore keys (which submit the game's own command so the blob hooks
 |   do the work), and the shared typeahead editor, then services audio. On
```

Replace it with:
```cpp
 |   The frame loop runs the soft-reset chord, the F10/F11/F12 menu shortcuts
 |   (Sound only when there is audio to configure; F10's Options menu can
 |   itself report a Save Game/Load Game pick, submitted the same way as
 |   below), the F2/F5 save and F3/F6/F9 restore keys (which submit the
 |   game's own command so the blob hooks do the work), and the shared
 |   typeahead editor, then services audio. On
```

- [ ] **Step 2: Consume `options_menu()`'s return value**

Find this exact text in `saturn/src/engine/saturn_glue.cxx` (currently lines 235-242):
```cpp
        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE
            || ke.kind == SATURN_KEY_F10) {
            options_menu();
            ensure_typeahead();
            typeahead_scan_screen(g_typeahead_root);
            SRL::Core::Synchronize();
            continue;
        }
```

Replace it with:
```cpp
        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE
            || ke.kind == SATURN_KEY_F10) {
            int om = options_menu();
            ensure_typeahead();
            typeahead_scan_screen(g_typeahead_root);
            SRL::Core::Synchronize();
            if (om == OM_SAVE)    { submit_command(k, "save");    continue; }
            if (om == OM_RESTORE) { submit_command(k, "restore"); continue; }
            continue;
        }
```

- [ ] **Step 3: Read-through self-check**

Confirm: `k` is the same `KeyboardState` local already in scope for this `for (;;)` loop (declared `static KeyboardState k;` earlier in `saturn_readline`) — the exact variable the F2/F5/F3/F6/F9 branches just below already pass to `submit_command`. Confirm neither new `if` pre-arms `g_save_device`/`g_save_slot`/`g_restore_device`/`g_restore_slot` (per the "always full picker" constraint) — unlike the F5/F6/F9 branches, which do.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/engine/saturn_glue.cxx
git commit -m "Submit save/restore when options_menu reports a Save/Load pick"
```

---

## Final Verification

- [ ] **Build and manual hardware/emulator check (user-run, not the assistant):**

Run `saturn/compile.bat`, then on real hardware or an emulator:
1. From the title screen's mode menu, open Options — confirm Save Game/Load Game do NOT appear, and Gameplay opens the Difficulty slider (adjust it, OK, re-open Options, confirm it's Gameplay not a direct slider anymore).
2. Start a game, press Start/Esc/F10 — confirm Save Game and Load Game now appear at the top of the list.
3. Pick Save Game — confirm it matches F2's flow exactly (device/slot picker, name editor, "Saved." message).
4. Pick Load Game — confirm it matches F3's flow exactly (device/slot picker, loads the game).
5. Confirm Return to Title, Credits, Display, Sound, Controls, and Network all still work as before, in the new order (Gameplay, Display, Sound, Controls, Network, Credits, Return to Title, Done after the two Save/Load rows).
6. Soft-reset (or quit) back to the title, open Options again — confirm Save Game/Load Game are gone again.
