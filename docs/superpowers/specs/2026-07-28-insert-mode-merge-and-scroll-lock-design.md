# Insert-mode merge and Scroll Lock line scrolling

Date: 2026-07-28
Status: approved, ready for implementation planning

## Problem

Three related defects and gaps in physical-keyboard handling:

1. **The Insert key does not drive the "Insert mode" option.** `saturn_keyboard_poll`
   emits `SATURN_KEY_INSERT` (`src/input/saturn_keyboard.cxx:242`), and its only
   consumer, `typeahead_edit` (`src/video/console_view.cxx:357`), flips
   `g_caret_arrows` — the *"Arrows move"* row. The real insert/overwrite state,
   `keyboard_get_insert()` (`src/input/keyboard.c:48`), is writable only from the
   Options page, so its row shows whatever was last set there no matter how often
   Insert is pressed.

   Caps Lock and Num Lock behave correctly because they are latched inside
   `saturn_keyboard_poll` itself (`src/input/saturn_keyboard.cxx:209-216`) and write
   shared `keyboard.c` state. That makes them live in every context, including while
   the Options page is open. Insert is absent from that block.

2. **Two options express one idea.** "Arrows move" (caret vs suggestions) and
   "Insert mode" (insert vs overwrite) are separate rows and separate state, but
   both describe the same line-editing posture.

3. **No line-at-a-time scrollback from the keyboard.** `scroll_handle_key`
   (`src/input/input.cxx:61`) maps plain Up/Down to ±1 line, but at the prompt
   `typeahead_edit` claims Up/Down first for command-history recall
   (`src/video/console_view.cxx:397-398`), so those cases are unreachable there.
   Only PgUp/PgDn (one page) and Home/End (top/bottom) work.

## Goals

- The Insert key toggles insert/overwrite mode, live, in every context — matching
  Caps Lock and Num Lock.
- "Arrows move" and "Insert mode" become a single setting, presented as
  **Insert mode: On/Off**.
- Ctrl+Up/Down scroll the console one line, repeating while held.
- Scroll Lock swaps which of Up/Down and Ctrl+Up/Down means history recall and
  which means line scroll, and is itself a latched toggle like Caps/Num.

## Non-goals

- Alt as a modifier. Alt is not tracked, SaturnRingLib defines no Alt constant, and
  right-Alt's Saturn code is unknown. Explicitly out of scope.
- An on-screen "SCRL" indicator beside the `CAPS` one at
  `src/video/console_view.cxx:459`.
- Any change to menu navigation. Menus compare against `SATURN_KEY_UP`/`DOWN`, so
  holding Ctrl while navigating a menu will stop moving the selection. Accepted.
- Any change to the MOJOOPTS save format (see Persistence, below).

## Design

### Merged setting

`g_caret_arrows` is deleted. Its declaration (`src/video/console_view.h:31`),
definition (`src/video/console_view.cxx:47`), and all readers move to
`keyboard_get_insert()`.

| Insert mode | Typing | Left/Right | Ctrl+Left/Right |
|---|---|---|---|
| **Off** (default) | overwrite | cycle suggestions | move caret |
| **On** | insert | move caret | cycle suggestions |

`g_caret_arrows` and `g_insert` both default to off, so the merge changes nothing
for an existing player's starting state.

### Vertical bindings

| Scroll Lock | Up/Down | Ctrl+Up/Down |
|---|---|---|
| **Off** (default) | history recall | scroll one line |
| **On** | scroll one line | history recall |

Fixed with respect to Insert mode: the vertical pair never swaps with the
horizontal one. Whichever pair is not claimed for history falls through to
`scroll_handle_key` and is consumed there.

Hold-to-repeat requires no work. Auto-repeat is keyed on scancode
(`src/input/saturn_keyboard.cxx:224-231`) with `ctrl_down` read at emit time, so a
held Ctrl+Up repeats after 30 frames at 4-frame intervals like any other key.

### Component changes

**`src/input/keyboard.h` / `keyboard.c`** — add a fourth toggle beside
caps/insert/num: `static int g_scrolllock = 0;` plus
`keyboard_set_scrolllock(int)` / `keyboard_get_scrolllock(void)`, normalizing to
0/1 like its siblings. Update the `g_caps / g_insert / g_num` and
`keyboard_set/get_*` doc comments.

**`src/input/saturn_keyboard.h`** — remove `SATURN_KEY_INSERT`; add
`SATURN_KEY_CTRL_UP` and `SATURN_KEY_CTRL_DOWN` beside the existing
`SATURN_KEY_CTRL_LEFT`/`RIGHT`. Update the `SaturnKeyKind` doc comment. Enum values
are not persisted anywhere, so renumbering is safe.

**`src/input/saturn_keyboard.cxx`** —
- Add `KBD_CODE_INSERT` (129, currently a bare literal at `:242`) and
  `KBD_CODE_SCRLK` (0x7E) to the `KBD_*` defines.
- In the latch block (`:209-216`), add `insert_held` and `scrl_held` statics with
  the same rising-edge debounce Caps/Num use, calling `keyboard_set_insert` and
  `keyboard_set_scrolllock`. This block runs before the `(cond & MAKE) == 0`
  early-out so break reports are seen.
- Delete the `code == 129` line that emitted `SATURN_KEY_INSERT`. Like Caps/Num,
  Insert now falls through to the end and emits nothing (`kbd_map[129]` is out of
  the 128-entry table's range and the code is not matched elsewhere).
- Decode arrows against `ctrl_down`:
  `if (code == 137) ev.kind = ctrl_down ? SATURN_KEY_CTRL_UP : SATURN_KEY_UP;` and
  the matching line for 138 / `CTRL_DOWN`.
- Update the `saturn_keyboard_poll` doc comment.

**`src/input/input.cxx`** — `scroll_handle_key` gains `SATURN_KEY_CTRL_UP` →
`g_scroll += 1` and `SATURN_KEY_CTRL_DOWN` → `g_scroll -= 1`, both consumed. The
existing plain `UP`/`DOWN` cases stay: they are what the Scroll-Lock-on path
reaches. Update the doc comment.

**`src/video/console_view.cxx` / `.h`** —
- Drop the `g_caret_arrows` global, its extern, and the header comment describing
  it; point the doc comments at `keyboard_get_insert()`.
- Delete `:357` (the `SATURN_KEY_INSERT` handler).
- `:359-365` read `keyboard_get_insert()` in place of `g_caret_arrows`.
- `:396-399` choose the history pair from `keyboard_get_scrolllock()` using the same
  ternary shape as the caret lines, leaving the other pair to
  `scroll_handle_key(ke)`.

**`src/menu/menu_pages.cxx` and `src/net/netbin_pages.cxx`** — two near-duplicate
copies of `keyboard_controls_page`; both change identically.
- Row list stays six: Insert mode · Caps Lock · Num Lock · Scroll Lock · Ok ·
  Cancel. `N` and the `menu_digit_row` shortcuts are unchanged.
- Snapshot/restore drops `s_arrows`, gains `s_scrl`; both the `back` path and the
  Cancel row restore it.
- The toggle chain drops the `sel == 0` `g_caret_arrows` case; rows renumber so
  Insert mode is 0, Caps 1, Num 2, Scroll Lock 3, Ok 4, Cancel 5.
- Insert mode renders `On`/`Off` (not `On (insert)` / `Off (overwrite)`); Scroll
  Lock renders `On`/`Off`. Value column stays at `x + 18`.
- Rewrite the help text at `menu_pages.cxx:422-423`. Its current second clause,
  "Ctrl+Left/Right always move caret", is already false — `g_caret_arrows` swaps
  exactly those.
- Update both doc comments (dependencies, globals, row list).

### Persistence

MOJOOPTS (`src/menu/options.cxx:178-230`) persists difficulty, dial number, music
and PCM levels, button/chord mappings, and display settings. It does **not**
persist caps/num/insert/arrows — the Options page only snapshots them so Cancel can
restore. The new Scroll Lock toggle follows suit, so there is no save-format or
version change.

## Risks

- **Scroll Lock scancode `0x7E` is inferred, not verified.** It comes from the same
  PS/2 set-2 table `kbd_map` follows (Caps `0x58`, Num `0x77`, LCtrl `0x14` all
  match), and `kbd_map[126]` is 0 so nothing conflicts. But the Saturn keyboard may
  not report the key, and an emulator or host OS may intercept it. Mitigation: the
  Options row makes the toggle fully usable even if the key is dead. Confirm on
  hardware or in Mednafen before considering the feature done.
- **Releasing Ctrl mid-hold** while still holding Up flips subsequent repeats to
  history recall. Pre-existing behavior for Ctrl+Left/Right; not diverging.
- **Two copies of the Options page** can drift. Both must land in the same change.

## Verification

`keyboard.c` is pure C and already covered by `saturn/tests/test_keyboard.c`, so the
new toggle gets unit coverage there (default off, set/get normalization).

Everything else — `input.cxx`, `console_view.cxx`, `saturn_keyboard.cxx` — sits
behind `input.h`, which includes `srl.hpp`, and is therefore unreachable from the
host-side gcc tests. A host test of `scroll_handle_key` would need a stub layer or
would merely restate its switch. So the rest is verified by build plus manual pass:

1. `saturn/compile.bat` builds both targets (CD image, then netbin) clean.
2. In Mednafen with a keyboard attached:
   - Insert flips the Insert mode row live while the Options page is open.
   - Insert mode On gives insert-typing and caret motion on plain Left/Right;
     Off gives overwrite and suggestion cycling.
   - Ctrl+Up/Down scrolls one line and repeats while held.
   - Scroll Lock swaps that with plain Up/Down; history recall stays reachable in
     both states.
   - PgUp/PgDn still page, Home/End still jump.
   - The same checks in the online terminal, which shares `typeahead_edit`.
