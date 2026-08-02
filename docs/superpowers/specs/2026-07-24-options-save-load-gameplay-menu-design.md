# Options menu: Save Game / Load Game rows, Gameplay sub-page

## Purpose

Add "Save Game" and "Load Game" entries to the Options menu, visible only
while a game is in progress, so the player can save/restore without typing
the command or reaching for a function key. Fold the Difficulty slider
(currently drawn inline in the Options menu) into its own "Gameplay"
sub-page to make room and keep the top-level list to plain dispatch rows.

## New global: `g_in_game`

`app_state.h` gains:

```c
// True once a game is loaded and running (set after mojo_boot succeeds);
// false before then and after a soft-reset back to the title. Gates the
// Options menu's Save Game / Load Game rows -- there is nothing to save or
// restore before a game has loaded.
extern bool g_in_game;
```

Defined in `app_state.cxx` as `bool g_in_game = false;`.

`main.cxx` sets it:
- **false** at the soft-reset re-entry point, alongside the existing
  `g_z3_dir_valid = false; g_menu_backing_depth = 0;` reset block (so a
  soft reset back to the title hides Save/Load again on the next Options
  visit).
- **true** immediately after `mojo_boot(story, len, seed)` succeeds, before
  the sound/music setup block and `mojo_run()`.

## `options_menu()` return value

The menu itself cannot serialize or restore game state -- only the
interpreter's save/restore opcodes can, and those only run off whatever
text next reaches `saturn_readline`. `options_menu()` changes signature
from `void` to a result enum (declared in `menu_pages.h`):

```c
enum { OM_NONE = 0, OM_SAVE, OM_RESTORE };
```

Returns `OM_SAVE` / `OM_RESTORE` when the player picked Save Game / Load
Game (before returning, the menu has already faded out and released input
exactly as it does for Done today); `OM_NONE` for every other exit (Done,
B/Esc, or Return to Title, which itself never returns since it calls
`soft_reset_to_title()`).

**Call sites:**
- `main.cxx` (title mode-menu, pre-game): discards the return value. The
  Save/Load rows never appear there since `g_in_game` is false, so it is
  always `OM_NONE`.
- `saturn_glue.cxx`'s `saturn_readline` (in-game F10/START/Esc): captures
  the return value. On `OM_SAVE`, calls `submit_command(k, "save")`; on
  `OM_RESTORE`, calls `submit_command(k, "restore")` -- the same
  `submit_command` helper the F2/F3 quick keys already use to feed a
  command through the normal input path. Either way it then falls through
  to the same `continue` the F10 branch already ends with.

No pre-armed device/slot (`g_save_device`/`g_restore_device` etc.): Save
Game and Load Game always go through the full device/slot picker, exactly
like the F2 (save) and F3 (restore) keys -- not the F5/F6/F9 quick-repeat
behavior. Save still gets its own "Overwrite?" confirm from inside
`saturn_save_blob`; Load has no confirm, consistent with how F3/F6/F9
behave today (a restore can already discard unsaved progress with no
prompt).

## Options menu layout

Top-level order becomes:

1. **Save Game** (in-game only)
2. **Load Game** (in-game only)
3. Gameplay
4. Display
5. Sound (only when there is audio to configure, as today)
6. Controls
7. Network
8. Credits
9. Return to Title
10. Done

The Difficulty slider (name + description line, Left/Right adjust) is
removed from the top-level box entirely, along with `options_menu()`'s
local `diff` variable and the `OI_DIFF` special-case handling. In its
place, "Gameplay" is a plain dispatch row like Network/Controls/Display/
Sound/Credits.

Because the slider's two header lines go away, the top-level box keeps its
existing position and size (`x0=5, y0=8, w=30, h=16`); the row list simply
starts a few lines higher, with the same fixed hint-row position as today
(the layout is sized for the worst case -- in-game with Sound present,
10 items -- the same way the current code already sizes for its own worst
case regardless of how many rows actually draw).

## New `gameplay_page()`

A new file-local sub-page in `menu_pages.cxx`, following the same
snapshot/OK/Cancel pattern as `keyboard_controls_page`/`sound_options_page`
(entry snapshots `g_difficulty`, Cancel restores it, OK calls
`options_save()`). Three rows: Difficulty (Left/Right cycles Easy/Medium/
Hard, description line beneath as today), OK, Cancel. Reached only from
the Options menu's Gameplay row; no direct hotkey (matches Network/
Display, which have none either).

## Out of scope

- No changes to the save/restore backend (`saturn_save_blob`/
  `saturn_load_blob`, `save_ui.cxx`) -- Save Game/Load Game reuse the
  existing full-picker flow verbatim.
- No confirmation dialog added to Load Game beyond what F3/F6/F9 already
  have (none).
- Quick-key behavior (F2/F3/F5/F6/F9) is unchanged.
