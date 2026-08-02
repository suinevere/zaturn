# Insert-mode Merge and Scroll Lock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the physical Insert key drive a single merged "Insert mode" setting, and add Ctrl+Up/Down one-line console scrolling with a Scroll Lock key that swaps it against command-history recall.

**Architecture:** Four latched keyboard toggles (Caps, Num, Insert, Scroll Lock) all live in `saturn/src/input/keyboard.c` behind setter/getter pairs, and are all flipped inside `saturn_keyboard_poll`'s modifier block so they update in every context — including while a menu is open. The old `g_caret_arrows` global is deleted and its readers point at `keyboard_get_insert()`. Ctrl+Up/Down become their own key kinds, decoded the same way Ctrl+Left/Right already are.

**Tech Stack:** C99 and C++ for the SH-2 target (SaturnRingLib SDK, built via `saturn/compile.bat`); host-side unit tests in plain C built with `gcc`.

**Spec:** `docs/superpowers/specs/2026-07-28-insert-mode-merge-and-scroll-lock-design.md`

## Global Constraints

- **Two copies of the Options page exist and must stay identical:** `saturn/src/menu/menu_pages.cxx` (`keyboard_controls_page`, from line 378) and `saturn/src/net/netbin_pages.cxx` (`static keyboard_controls_page`, from line 387). The bodies are byte-identical; only the line offsets differ (netbin is +9). Every edit to one must be applied verbatim to the other, in the same commit.
- **Options-page text width is 34 columns.** `menu_box_fit("CONTROLS", 34, 14, ...)` sets it. The longest existing line is exactly 34 characters, so 34 is proven safe and 35 is not.
- **No MOJOOPTS save-format change.** `saturn/src/menu/options.cxx` does not persist caps/num/insert; Scroll Lock follows suit. Do not touch `options_load`/`options_save`.
- **Every task must leave the tree compiling.** Do not split a task such that a symbol is deleted in one commit and its last reference removed in the next.
- **Doc-comment style is mandatory here.** Every file in this codebase uses `/*---------------------- | Name | Description: ... | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----------------------*/` banners. When you change what a function or global does, update its banner in the same edit.
- **Host test build+run command** (Git Bash, from repo root), used by Task 1:
  ```bash
  gcc -std=c99 -Wall -Wextra -o /tmp/tk.exe saturn/tests/test_keyboard.c saturn/src/input/keyboard.c && /tmp/tk.exe
  ```
  It prints `test_keyboard: OK` on success. This is verified working against the current tree.
- **Saturn build command** (Tasks 2-4), from the repo root:
  ```bash
  cd saturn && ./compile.bat debug
  ```
  `compile.bat` is a polyglot script — it runs under Git Bash and under cmd. It builds **both** targets: the CD image first, then `zaturn.netbin`. Both must compile clean.

---

### Task 1: Scroll Lock toggle state

Adds a fourth latched toggle beside caps/insert/num. Nothing consumes it yet — Task 3 does. This is the only part of the feature reachable from host-side tests, because `keyboard.c` is pure C with no SDK dependency.

**Files:**
- Modify: `saturn/src/input/keyboard.h:76-89`
- Modify: `saturn/src/input/keyboard.c:40-62`
- Test: `saturn/tests/test_keyboard.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void keyboard_set_scrolllock(int on);` and `int keyboard_get_scrolllock(void);` — defaults to 0, setter normalizes any nonzero to 1. Tasks 3 and 4 both call these.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_keyboard.c`, insert this block immediately **before** the `printf("test_keyboard: OK\n");` line at the end of `main`:

```c
    /* Scroll Lock: defaults off; setter normalizes any nonzero to 1 */
    assert(keyboard_get_scrolllock() == 0);
    keyboard_set_scrolllock(1);
    assert(keyboard_get_scrolllock() == 1);
    keyboard_set_scrolllock(42);
    assert(keyboard_get_scrolllock() == 1);
    keyboard_set_scrolllock(0);
    assert(keyboard_get_scrolllock() == 0);
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
gcc -std=c99 -Wall -Wextra -o /tmp/tk.exe saturn/tests/test_keyboard.c saturn/src/input/keyboard.c && /tmp/tk.exe
```
Expected: FAIL at compile time — `implicit declaration of function 'keyboard_get_scrolllock'` and an undefined-reference link error. The binary must not be produced.

- [ ] **Step 3: Write minimal implementation**

In `saturn/src/input/keyboard.h`, replace the toggles banner and declarations at lines 76-89 with:

```c
/*----------------------
 | toggles (keyboard_{set,get}_{caps,insert,num,scrolllock})
 | Description: CapsLock selects the KB_LAYOUT_UPPER layer; Insert makes mid-line
 |   typing insert (shift the tail right) rather than overwrite (append is the same
 |   either way) AND makes the plain arrows move the caret rather than cycle
 |   suggestions; NumLock (defaults on, shared with the physical key and the
 |   Keyboard Controls page) suppresses the physical numpad digits when off;
 |   ScrollLock swaps whether plain or Ctrl Up/Down recall history vs scroll the
 |   console one line. All four are shared by the physical key, the on-screen
 |   keyboard layer, and the Keyboard Controls page.
 | Author: suinevere
 ----------------------*/
void keyboard_set_caps(int on);
int  keyboard_get_caps(void);
void keyboard_set_insert(int on);
int  keyboard_get_insert(void);
void keyboard_set_num(int on);
int  keyboard_get_num(void);
void keyboard_set_scrolllock(int on);
int  keyboard_get_scrolllock(void);
```

In `saturn/src/input/keyboard.c`, replace lines 40-62 with:

```c
/*----------------------
 | g_caps / g_insert / g_num / g_scrolllock
 | Description: Keyboard toggle state: CapsLock (selects the shifted layer),
 |   Insert (insert vs overwrite while typing, and caret vs suggestion arrows),
 |   NumLock (defaults on, so the numpad produces digits), and ScrollLock (swaps
 |   the plain and Ctrl Up/Down roles).
 | Author: suinevere
 ----------------------*/
static int g_caps = 0;
static int g_insert = 0;
static int g_num = 1;
static int g_scrolllock = 0;

/*----------------------
 | keyboard_set/get_caps / _insert / _num / _scrolllock
 | Description: Setters and getters for the four toggle flags; setters normalize
 |   to 0/1.
 | Author: suinevere
 ----------------------*/
void keyboard_set_caps(int on) { g_caps = on ? 1 : 0; }
int  keyboard_get_caps(void)   { return g_caps; }
void keyboard_set_insert(int on) { g_insert = on ? 1 : 0; }
int  keyboard_get_insert(void)   { return g_insert; }
void keyboard_set_num(int on) { g_num = on ? 1 : 0; }
int  keyboard_get_num(void)   { return g_num; }
void keyboard_set_scrolllock(int on) { g_scrolllock = on ? 1 : 0; }
int  keyboard_get_scrolllock(void)   { return g_scrolllock; }
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
gcc -std=c99 -Wall -Wextra -o /tmp/tk.exe saturn/tests/test_keyboard.c saturn/src/input/keyboard.c && /tmp/tk.exe
```
Expected: PASS, printing `test_keyboard: OK`, with no warnings.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/input/keyboard.h saturn/src/input/keyboard.c saturn/tests/test_keyboard.c
git commit -m "feat(input): add shared Scroll Lock toggle state"
```

---

### Task 2: Merge "Arrows move" into "Insert mode"

Deletes `g_caret_arrows`, points its readers at `keyboard_get_insert()`, and makes the physical Insert key latch that state inside `saturn_keyboard_poll` the way Caps/Num already do. This is the bug fix. All five files change together because removing `SATURN_KEY_INSERT` breaks every reference at once.

The Options page temporarily drops to 5 rows; Task 4 restores it to 6 with the Scroll Lock row.

**Files:**
- Modify: `saturn/src/input/saturn_keyboard.h:16-54`
- Modify: `saturn/src/input/saturn_keyboard.cxx:29-39,163-264`
- Modify: `saturn/src/video/console_view.h:24-31,132-157`
- Modify: `saturn/src/video/console_view.cxx:39-47,266-402`
- Modify: `saturn/src/menu/menu_pages.cxx:378-442`
- Modify: `saturn/src/net/netbin_pages.cxx:387-451`

**Interfaces:**
- Consumes: `keyboard_get_insert()` / `keyboard_set_insert()` from `keyboard.h` (pre-existing).
- Produces: `SATURN_KEY_INSERT` no longer exists — Task 3 must not reference it. `g_caret_arrows` no longer exists. Options-page row indices become: 0 Insert mode, 1 Caps Lock, 2 Num Lock, 3 Ok, 4 Cancel, with `N = 5`.

- [ ] **Step 1: Retire the SATURN_KEY_INSERT event kind**

In `saturn/src/input/saturn_keyboard.h`, delete this line from the `SaturnKeyKind` enum:

```c
    SATURN_KEY_INSERT,      /* Insert: toggle arrow-key vs Ctrl+arrow roles */
```

and replace the enum's banner Description line so it no longer advertises INSERT:

```c
 | Description: The kind of a decoded key event: NONE, a printable CHAR (in .ch),
 |   the editing/navigation keys, TAB (accept completion), CLEAR (Ctrl+C),
 |   CTRL_LEFT/RIGHT (word caret), DELETE, and the mapped function keys F2..F12
 |   (F1/F4/F7/F8 have no role and stay unreported). Insert is not an event: it is
 |   a latched toggle handled inside saturn_keyboard_poll, like Caps and Num.
```

- [ ] **Step 2: Latch Insert inside saturn_keyboard_poll**

In `saturn/src/input/saturn_keyboard.cxx`, add to the `KBD_*` defines block (after `#define KBD_CODE_NUM 0x77`):

```c
#define KBD_CODE_INSERT 129
```

Add a `static int insert_held = 0;` beside the existing `caps_held`/`num_held` statics near the top of `saturn_keyboard_poll`, so that block reads:

```c
    static uint8_t last_code = 0;
    static int repeat_timer = 0;
    static int ctrl_down = 0;
    static int shift_down = 0;
    static int caps_held = 0;
    static int num_held  = 0;
    static int insert_held = 0;
```

Then, immediately after the existing `KBD_CODE_NUM` latch block, add:

```c
    if (code == KBD_CODE_INSERT) {
        if (cond & KBD_COND_MAKE)  { if (!insert_held) { keyboard_set_insert(!keyboard_get_insert()); insert_held = 1; } }
        if (cond & KBD_COND_BREAK) { insert_held = 0; }
    }
```

This must sit **above** the `if ((cond & KBD_COND_MAKE) == 0 || code == 0)` early-out, exactly like the Caps and Num blocks, so break reports are seen.

Finally delete this line from the scancode dispatch:

```c
    if (code == 129)              { ev.kind = SATURN_KEY_INSERT;   return ev; }
```

With that line gone, code 129 falls through to the end of the function. It is not `< 128`, so it never indexes `kbd_map`, and the function returns `SATURN_KEY_NONE` — the same way Caps and Num already do.

Update the `saturn_keyboard_poll` banner: the Description currently says "Insert/Delete" among the emitted events and lists Caps/Num as the latched toggles. Change it to name Caps, Num, and Insert as the latched toggles, and drop Insert from the emitted list:

```c
 | Description: Reads this frame's keyboard report and returns one SaturnKeyEvent.
 |   Modifier bookkeeping runs first (before any early-out) so break reports are
 |   seen: Ctrl and Shift are held-state flags tracked from their own make/break;
 |   Caps, Num and Insert are latched toggles that flip the shared keyboard state
 |   on their rising edge only (debounced), which is why they take effect in every
 |   context including an open menu. With no key down it clears the held-key latch.
 |   Otherwise it gates emission through auto-repeat (fresh press, or a repeat
 |   tick) and maps the scancode to an event: Enter/Tab/Backspace/Esc, the arrow
 |   keys (Ctrl+Left/Right distinguished for word motion), Delete, the nav-cluster
 |   and Page keys, the function keys F2..F12, Ctrl+C to clear the line, and
 |   finally a character (numpad digits suppressed when NumLock is off, Shift/Caps
 |   applied).
```

- [ ] **Step 3: Delete g_caret_arrows and repoint its readers**

In `saturn/src/video/console_view.h`, delete these four lines (24-31 region — the comment and the extern):

```c
// Inline-edit mode: which arrows move the text caret vs cycle suggestions. false
// (default): Ctrl+Left/Right move the caret, plain Left/Right cycle. true: plain
// Left/Right move the caret, Ctrl+Left/Right cycle. Toggled by Insert.
extern bool g_caret_arrows;
```

In the same file, change the `typeahead_edit` banner's `Globals:` line from `g_pad, g_caret_arrows` to `g_pad`.

In `saturn/src/video/console_view.cxx`, replace the `g_kbd_visible / g_caret_arrows` banner and its two definitions (lines 39-47) with:

```c
/*----------------------
 | g_kbd_visible
 | Description: Tracks whether the on-screen keyboard is showing (gamepad in hand)
 |   vs hidden (real keyboard). The caret-vs-suggestion arrow roles that used to
 |   live beside this now come from keyboard_get_insert().
 | Author: suinevere
 ----------------------*/
bool g_kbd_visible = true;
```

Delete line 357 entirely:

```c
    if (ke.kind == SATURN_KEY_INSERT) { g_caret_arrows = !g_caret_arrows; ke.kind = SATURN_KEY_NONE; }
```

Replace the four `g_caret_arrows` reads (lines 359-365) with a single local read of the merged state:

```c
    bool ins = keyboard_get_insert();
    bool caret_l = ins ? (ke.kind == SATURN_KEY_LEFT)  : (ke.kind == SATURN_KEY_CTRL_LEFT);
    bool caret_r = ins ? (ke.kind == SATURN_KEY_RIGHT) : (ke.kind == SATURN_KEY_CTRL_RIGHT);
    if (caret_l) keyboard_caret_left(&k);
    if (caret_r) keyboard_caret_right(&k);

    bool kb_prev = ins ? (ke.kind == SATURN_KEY_CTRL_LEFT)  : (ke.kind == SATURN_KEY_LEFT);
    bool kb_next = ins ? (ke.kind == SATURN_KEY_CTRL_RIGHT) : (ke.kind == SATURN_KEY_RIGHT);
```

In the `typeahead_edit` banner in this file, change the `Globals:` line from `g_pad, g_caret_arrows` to `g_pad`, and rewrite the Description sentence that reads "Insert toggles whether plain or Ctrl arrows move the caret vs cycle suggestions." to:

```c
 |   Insert mode (keyboard_get_insert, latched by the physical Insert key) selects
 |   whether plain or Ctrl arrows move the caret vs cycle suggestions.
```

- [ ] **Step 4: Merge the two Options rows into one**

Apply this to **both** `saturn/src/menu/menu_pages.cxx` (from line 378) and `saturn/src/net/netbin_pages.cxx` (from line 387). The bodies are identical; make the edits verbatim in both.

Replace the snapshot and row count:

```c
    int s_ins = keyboard_get_insert(),
        s_caps = keyboard_get_caps(), s_num = keyboard_get_num();
    const int N = 5;
```

Replace the `back` restore block:

```c
        if (back) {
            keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num);
            break;
        }
```

Replace the toggle chain:

```c
        if      (sel == 0 && toggle) keyboard_set_insert(!keyboard_get_insert());
        else if (sel == 1 && toggle) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 2 && toggle) keyboard_set_num(!keyboard_get_num());
        else if (sel == 3 && act) { options_save(); break; }
        else if (sel == 4 && act) {
            keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num); break; }
```

Replace the two help lines and the row rendering (from `SRL::Debug::Print(x, y++, "Insert key also flips Arrows;");` through the Cancel row) with:

```c
        SRL::Debug::Print(x, y++, "Insert: type-insert, caret arrows.");
        SRL::Debug::Print(x, y++, "Off: overwrite, arrows suggest.");
        y++;
        bool nums = !g_kbd_visible;
        if (nums) SRL::Debug::Print(x, y, "%c 1) Insert mode", sel == 0 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Insert mode", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_insert() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y, "%c 2) Caps Lock", sel == 1 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Caps Lock", sel == 1 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_caps() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y, "%c 3) Num Lock", sel == 2 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Num Lock", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_num() ? "On" : "Off");
        y++;
        if (nums) SRL::Debug::Print(x, y++, "%c 4) Ok", sel == 3 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Ok", sel == 3 ? '>' : ' ');
        if (nums) SRL::Debug::Print(x, y++, "%c 5) Cancel", sel == 4 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Cancel", sel == 4 ? '>' : ' ');
```

In both files, update the `keyboard_controls_page` banner: drop `g_caret_arrows` from the `Globals:` and `Dependencies:` lines, and change the Description's row list from "arrows-vs-suggestions, Insert mode, Caps Lock, Num Lock" to "Insert mode, Caps Lock, Num Lock".

- [ ] **Step 5: Verify both Saturn targets build**

Run:
```bash
cd saturn && ./compile.bat debug
```
Expected: PASS — the CD image builds, then `zaturn.netbin` builds, with no errors. A leftover reference to `g_caret_arrows` or `SATURN_KEY_INSERT` will show up here as a compile error; grep for both to confirm zero hits outside the spec and plan documents:
```bash
git grep -n "g_caret_arrows\|SATURN_KEY_INSERT" -- saturn/src
```
Expected: no output.

- [ ] **Step 6: Verify the host test still passes**

Run:
```bash
gcc -std=c99 -Wall -Wextra -o /tmp/tk.exe saturn/tests/test_keyboard.c saturn/src/input/keyboard.c && /tmp/tk.exe
```
Expected: PASS, printing `test_keyboard: OK`.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/input/saturn_keyboard.h saturn/src/input/saturn_keyboard.cxx \
        saturn/src/video/console_view.h saturn/src/video/console_view.cxx \
        saturn/src/menu/menu_pages.cxx saturn/src/net/netbin_pages.cxx
git commit -m "fix(input): make the Insert key drive Insert mode

Insert emitted SATURN_KEY_INSERT, whose only consumer flipped g_caret_arrows
-- the 'Arrows move' row -- so the 'Insert mode' row was writable only from
the menu. Latch Insert inside saturn_keyboard_poll like Caps and Num, and
merge the two settings into one."
```

---

### Task 3: Ctrl+Up/Down line scrolling with the Scroll Lock swap

Adds the two new key kinds, the Scroll Lock latch, the scroll deltas, and the swap at the prompt. After this task the feature works; only the Options row is missing.

**Files:**
- Modify: `saturn/src/input/saturn_keyboard.h:16-52`
- Modify: `saturn/src/input/saturn_keyboard.cxx:29-39,182-248`
- Modify: `saturn/src/input/input.cxx:49-73`
- Modify: `saturn/src/video/console_view.cxx:392-399`
- Modify: `saturn/src/video/console_view.h` (`typeahead_edit` banner)

**Interfaces:**
- Consumes: `keyboard_get_scrolllock()` / `keyboard_set_scrolllock()` from Task 1.
- Produces: `SATURN_KEY_CTRL_UP` and `SATURN_KEY_CTRL_DOWN` enum members.

- [ ] **Step 1: Add the two key kinds**

In `saturn/src/input/saturn_keyboard.h`, add immediately after `SATURN_KEY_CTRL_RIGHT`:

```c
    SATURN_KEY_CTRL_UP,     /* Ctrl+Up: scroll the console up one line */
    SATURN_KEY_CTRL_DOWN,   /* Ctrl+Down: scroll the console down one line */
```

Update the enum banner's Description to mention them alongside CTRL_LEFT/RIGHT:

```c
 |   CTRL_LEFT/RIGHT (word caret), CTRL_UP/DOWN (one-line scroll), DELETE, and the
```

- [ ] **Step 2: Latch Scroll Lock and decode Ctrl+Up/Down**

In `saturn/src/input/saturn_keyboard.cxx`, add to the `KBD_*` defines block:

```c
#define KBD_CODE_SCRLK  0x7E
```

Add `static int scrl_held = 0;` beside the other statics in `saturn_keyboard_poll`, then add this latch block immediately after the `KBD_CODE_INSERT` block from Task 2:

```c
    if (code == KBD_CODE_SCRLK) {
        if (cond & KBD_COND_MAKE)  { if (!scrl_held) { keyboard_set_scrolllock(!keyboard_get_scrolllock()); scrl_held = 1; } }
        if (cond & KBD_COND_BREAK) { scrl_held = 0; }
    }
```

`kbd_map[0x7E]` is 0, so Scroll Lock produces no character and needs no early return.

Replace the two arrow-decode lines so Ctrl selects the scroll variants, mirroring the Left/Right lines directly above them:

```c
    if (code == 137)              { ev.kind = ctrl_down ? SATURN_KEY_CTRL_UP   : SATURN_KEY_UP;    return ev; }
    if (code == 138)              { ev.kind = ctrl_down ? SATURN_KEY_CTRL_DOWN : SATURN_KEY_DOWN;  return ev; }
```

Add ScrollLock to the latched-toggle sentence in the `saturn_keyboard_poll` banner ("Caps, Num, Insert and ScrollLock are latched toggles...").

- [ ] **Step 3: Give the new kinds a scroll delta**

In `saturn/src/input/input.cxx`, replace the body of `scroll_handle_key` (lines 61-73) with:

```c
bool scroll_handle_key(const SaturnKeyEvent &ke) {
    switch (ke.kind) {
        case SATURN_KEY_UP:
        case SATURN_KEY_CTRL_UP:   g_scroll += 1;           return true;
        case SATURN_KEY_DOWN:
        case SATURN_KEY_CTRL_DOWN: g_scroll -= 1;           return true;
        case SATURN_KEY_PAGEUP:   g_scroll += SCROLL_PAGE; return true;
        case SATURN_KEY_PAGEDOWN: g_scroll -= SCROLL_PAGE; return true;
        case SATURN_KEY_HOME:     g_scroll  = SCROLL_ALL;  return true;
        case SATURN_KEY_END:      g_scroll  = 0;           return true;
        case SATURN_KEY_LEFT:
        case SATURN_KEY_RIGHT:                             return true;
        default:                                           return false;
    }
}
```

Both plain and Ctrl variants scroll ±1 because only one of the two pairs ever reaches here — `typeahead_edit` claims the other for history recall (Step 4).

Update the banner Description to say so:

```c
 | Description: A switch over the physical-keyboard nav keys, translating each
 |   into a g_scroll delta or absolute value. Up/Down and Ctrl+Up/Down both scroll
 |   one line: whichever pair ScrollLock has NOT assigned to history recall is the
 |   pair that reaches here. Left/Right are matched but left a no-op: they used to
 |   move the on-screen keyboard cursor and are now consumed here so they don't
 |   fall through and get typed as text.
```

- [ ] **Step 4: Swap history vs scroll at the prompt**

In `saturn/src/video/console_view.cxx`, replace the final dispatch chain (lines 392-399) with:

```c
    bool scrl = keyboard_get_scrolllock();
    bool hist_up   = scrl ? (ke.kind == SATURN_KEY_CTRL_UP)   : (ke.kind == SATURN_KEY_UP);
    bool hist_down = scrl ? (ke.kind == SATURN_KEY_CTRL_DOWN) : (ke.kind == SATURN_KEY_DOWN);

    if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
    else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
    else if (ke.kind == SATURN_KEY_DELETE)    keyboard_delete_forward(&k);
    else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
    else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
    else if (hist_up)                         history_recall(&k, 1);
    else if (hist_down)                       history_recall(&k, 0);
    else                                      scroll_handle_key(ke);
```

In `console_view.cxx`, the `typeahead_edit` banner ends with "Remaining key events type/erase/submit/recall or fall through to scroll handling." Replace that sentence with:

```c
 |   Remaining key events type/erase/submit/recall or fall through to scroll
 |   handling; ScrollLock selects whether plain or Ctrl Up/Down recalls history,
 |   the other pair scrolling one line.
```

The `console_view.h` banner has no such sentence — it ends with "...and recalls history." Append to that clause instead:

```c
 |   completion (with or without a trailing space), and recalls history --
 |   ScrollLock choosing whether plain or Ctrl Up/Down recalls, with the other
 |   pair scrolling the console one line.
```

- [ ] **Step 5: Verify both Saturn targets build**

Run:
```bash
cd saturn && ./compile.bat debug
```
Expected: PASS — both the CD image and `zaturn.netbin` build with no errors.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input/saturn_keyboard.h saturn/src/input/saturn_keyboard.cxx \
        saturn/src/input/input.cxx saturn/src/video/console_view.cxx \
        saturn/src/video/console_view.h
git commit -m "feat(input): Ctrl+Up/Down scroll one line, Scroll Lock swaps

Ctrl+Up/Down scroll the console a line at a time and repeat while held.
Scroll Lock swaps that against plain Up/Down command-history recall."
```

---

### Task 4: Scroll Lock row on the Options page

Restores the page to six rows. This also insures the feature against the scancode risk: `0x7E` is inferred from the same PS/2 set-2 table `kbd_map` follows, and the Saturn keyboard may not report it — with this row, the toggle stays usable either way.

**Files:**
- Modify: `saturn/src/menu/menu_pages.cxx` (`keyboard_controls_page`)
- Modify: `saturn/src/net/netbin_pages.cxx` (`keyboard_controls_page`)

**Interfaces:**
- Consumes: `keyboard_get_scrolllock()` / `keyboard_set_scrolllock()` from Task 1.
- Produces: final row indices — 0 Insert mode, 1 Caps Lock, 2 Num Lock, 3 Scroll Lock, 4 Ok, 5 Cancel, with `N = 6`.

- [ ] **Step 1: Add the row to both copies**

Apply verbatim to **both** `saturn/src/menu/menu_pages.cxx` and `saturn/src/net/netbin_pages.cxx`.

Snapshot and row count:

```c
    int s_ins = keyboard_get_insert(), s_caps = keyboard_get_caps(),
        s_num = keyboard_get_num(), s_scrl = keyboard_get_scrolllock();
    const int N = 6;
```

`back` restore block:

```c
        if (back) {
            keyboard_set_insert(s_ins); keyboard_set_caps(s_caps);
            keyboard_set_num(s_num); keyboard_set_scrolllock(s_scrl);
            break;
        }
```

Toggle chain:

```c
        if      (sel == 0 && toggle) keyboard_set_insert(!keyboard_get_insert());
        else if (sel == 1 && toggle) keyboard_set_caps(!keyboard_get_caps());
        else if (sel == 2 && toggle) keyboard_set_num(!keyboard_get_num());
        else if (sel == 3 && toggle) keyboard_set_scrolllock(!keyboard_get_scrolllock());
        else if (sel == 4 && act) { options_save(); break; }
        else if (sel == 5 && act) {
            keyboard_set_insert(s_ins); keyboard_set_caps(s_caps);
            keyboard_set_num(s_num); keyboard_set_scrolllock(s_scrl); break; }
```

Help lines and row rendering — replace the block written in Task 2 with:

```c
        SRL::Debug::Print(x, y++, "Insert: type-insert, caret arrows.");
        SRL::Debug::Print(x, y++, "ScrLk: Up/Dn scroll, Ctrl=history.");
        y++;
        bool nums = !g_kbd_visible;
        if (nums) SRL::Debug::Print(x, y, "%c 1) Insert mode", sel == 0 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Insert mode", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_insert() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y, "%c 2) Caps Lock", sel == 1 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Caps Lock", sel == 1 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_caps() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y, "%c 3) Num Lock", sel == 2 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Num Lock", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_num() ? "On" : "Off");
        if (nums) SRL::Debug::Print(x, y, "%c 4) Scroll Lock", sel == 3 ? '>' : ' ');
        else      SRL::Debug::Print(x, y, "%c    Scroll Lock", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x + 18, y++, "%s", keyboard_get_scrolllock() ? "On" : "Off");
        y++;
        if (nums) SRL::Debug::Print(x, y++, "%c 5) Ok", sel == 4 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Ok", sel == 4 ? '>' : ' ');
        if (nums) SRL::Debug::Print(x, y++, "%c 6) Cancel", sel == 5 ? '>' : ' ');
        else      SRL::Debug::Print(x, y++, "%c    Cancel", sel == 5 ? '>' : ' ');
```

Both help lines are exactly 34 characters — the proven-safe maximum. Do not lengthen them. The row count is back to four, so the box height is unchanged from the original page.

In both files, add Scroll Lock to the `keyboard_controls_page` banner's Description row list and to its `Dependencies:` line.

- [ ] **Step 2: Verify the two copies did not drift**

Run:
```bash
git grep -c "Scroll Lock" -- saturn/src/menu/menu_pages.cxx saturn/src/net/netbin_pages.cxx
```
Expected: both files report the same count.

- [ ] **Step 3: Verify both Saturn targets build**

Run:
```bash
cd saturn && ./compile.bat debug
```
Expected: PASS — both the CD image and `zaturn.netbin` build with no errors.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/menu/menu_pages.cxx saturn/src/net/netbin_pages.cxx
git commit -m "feat(menu): add Scroll Lock row to the Keyboard Controls page"
```

---

### Task 5: Manual verification on the emulator

Everything below `input.h` pulls in `srl.hpp` and cannot be reached from the host-side gcc tests, so this pass is the only check on the decode and dispatch behavior. It is a required task, not an optional one — in particular the Scroll Lock scancode is unverified until step 4 passes.

**Files:** none — verification only.

- [ ] **Step 1: Launch the built image with a keyboard attached**

Run `saturn/run_with_mednafen.bat`, which loads `BuildDrop/Zaturn (USA) (Netlink Edition).cue`. Confirm a keyboard is mapped in Mednafen's Saturn port settings before starting a game.

- [ ] **Step 2: Insert key drives the Insert mode row live**

Open Options → Keyboard Controls. With the page open, press the physical Insert key. Expected: the **Insert mode** row flips between `On` and `Off` on screen, the same way Caps Lock and Num Lock rows already respond to their keys. This is the reported bug; before Task 2 the row did not move.

- [ ] **Step 3: The merged setting drives both behaviors**

Leave the Options page and start a game. With **Insert mode Off**: typing mid-line overwrites, plain Left/Right cycle typeahead suggestions, and Ctrl+Left/Right move the caret. With **Insert mode On**: typing mid-line inserts, plain Left/Right move the caret, and Ctrl+Left/Right cycle suggestions.

- [ ] **Step 4: Scroll Lock is reported by the keyboard**

Back on the Keyboard Controls page, press the physical Scroll Lock key. Expected: the **Scroll Lock** row flips. **If it does not move, the `0x7E` scancode guess is wrong** — report this rather than working around it; the row still lets you toggle the setting manually, so continue the remaining steps using the menu.

- [ ] **Step 5: Ctrl+Up/Down scrolls one line and repeats**

In a game, generate enough output to fill the console. With Scroll Lock **Off**: Ctrl+Up scrolls up exactly one line per press and repeats smoothly when held; Ctrl+Down scrolls back down; plain Up/Down still recall previous commands.

- [ ] **Step 6: Scroll Lock swaps the pair**

Turn Scroll Lock **On**. Expected: plain Up/Down now scroll one line, and Ctrl+Up/Down recall history. Confirm history recall is still reachable — it must not be lost in either state.

- [ ] **Step 7: Nothing else regressed**

PgUp/PgDn still page, Home/End still jump to top/bottom, and the `^` / `more v` edge markers still appear. Repeat steps 5 and 6 in the **online terminal** (Play Online), which shares `typeahead_edit` — behavior must be identical to the local prompt.

- [ ] **Step 8: Record the outcome**

If every step passed, note in the PR or commit trail that the `0x7E` Scroll Lock scancode is confirmed on Mednafen. If step 4 failed, open a follow-up describing which key produced no response, so the scancode can be re-derived from a raw report dump rather than guessed again.
