# Netbin Direction Rose + Typeahead Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `zaturn.netbin` the two features it can still gain — a bordered keyboard strip with an all-directions compass rose, and working typeahead suggestions from an embedded Zork I dictionary.

**Architecture:** Both features are additive to the existing `NETBIN=1` target. The rose reuses `command_rose.c` unchanged by feeding it a static all-open exits array instead of a live room model, which drops the `room_model`/`command_view`/`command_panel` cluster entirely. Typeahead embeds `ZORK1.Z3` as a `.rodata` blob and a Zork-I-only solution overlay, replacing the `#ifdef NETBIN` early return that currently leaves the trie empty. The two features are independent; they are sequenced in one plan only because both edit the `Makefile` NETBIN source list and `tests/test_netbin_sources.py`.

**Tech Stack:** SH-2 GCC (SaturnRingLib toolchain), GNU make, Python 3 (generators), C (c2x) / C++ (gnu++2b), host `gcc` for tests.

**Specs:**
- `docs/superpowers/specs/2026-08-25-netbin-direction-rose-design.md` (Tasks 1-2)
- `docs/superpowers/specs/2026-08-25-netbin-typeahead-design.md` (Tasks 3-5)

## Global Constraints

- **THE USER RUNS ALL SATURN BUILDS.** Never invoke `compile.bat`, `compile-netbin.bat`, `make all`, or any SH-2 build. Host `gcc` tests and Python you DO run yourself. Where a step needs a real build, prepare the change and hand off with the exact command.
- **Never modify anything under `SaturnRingLib/`.** It is a pinned submodule.
- **The CD build must continue to work unchanged.** `saturn/cd/data/0.bin` is currently 368,723 bytes; after Task 1 it should change only by the ~200 bytes that move between objects.
- **Entry point / load base is exactly `0x06010000`** for the netbin. Non-negotiable loader requirement.
- **`zaturn.netbin` must stay under `NETBIN_MAX_BYTES` = 409,600** (`saturn/post.makefile:10`). The build hard-fails past it.
- **Comment style** (from `CLAUDE.md`): every method, constant and file gets a `/*----------------------*/` header block with `Description / Author: suinevere / Dependencies / Globals / Params / Returns`, `N/A` where a field does not apply. Tests and generated files get a file header only. **No comments inside functions.** Keep prose to a sentence.
- **A `*/` inside a `/*----box----*/` comment body closes the comment early and breaks the build.** Never put one there.
- **Commits are one sentence.** No body, no bullets, no trailers, no mention of Claude/AI/the session.
- **`text_map.h` is C++-only** — its `extern "C" {` at line 49 is unguarded and no `.c` file in the tree includes it. Anything calling `text_print*` must be a `.cxx` translation unit.
- Debug output uses only `%c %s %d %0Nd` — SRL's `Debug::Print` supports nothing else.

---

## File Structure

**Created:**
- `saturn/src/video/rose_draw.h` / `rose_draw.cxx` — the one rose-row draw call, lifted out of `command_view.cxx` so the netbin can draw a rose without linking the command panel. C++ because it calls `text_print`.
- `tools/gen_blob.py` — binary file to C byte-array source. One responsibility: bytes → `.c` text.
- `saturn/src/input/netbin_story.h` / `netbin_story.c` — **generated**; the embedded `ZORK1.Z3`. Never hand-edited.
- `saturn/src/input/typeahead_solution_zork1.c` — **generated**; the single-game overlay.

**Modified:**
- `saturn/src/video/command_rose.h` / `command_rose.c` — gains `cr_dir_word`.
- `saturn/src/video/command_view.h` / `command_view.cxx` — loses the `cv_draw_rose_row` definition and declaration.
- `saturn/src/video/console_view.cxx` — five `#ifndef NETBIN` regions un-gated; two `room_model` calls replaced.
- `saturn/src/main_netbin.cxx` — sets `g_in_game` for the session; `typeahead_malloc` moves to Low Work RAM.
- `saturn/src/net/online.cxx` — the NETBIN typeahead early return becomes the embedded-blob build.
- `saturn/Makefile` — six sources added to the `NETBIN=1` block (three in Task 2, three in Task 5).
- `saturn/tests/test_netbin_sources.py` — `EXPECTED` 21 → 27, `NETBIN_ONLY` 2 → 3.
- `saturn/tests/test_command_rose.c` — `cr_dir_word` coverage.
- `tools/typeahead/gen_all.ps1` — second `gen_solution.py` invocation.

---

## Task 1: Lift the rose draw call out of `command_view`

`console_view.cxx` calls `cv_draw_rose_row`, which lives in `command_view.cxx`. Linking `command_view.o` into the netbin for that one function would cost 10,368 bytes of image and 15,404 bytes of `.bss` for a panel this build never shows. It moves to its own translation unit. `command_rose.c` also gains `cr_dir_word` so `console_view` stops needing `room_model` for direction spellings.

This task touches only the CD build. The netbin is unchanged and still links.

**Files:**
- Create: `saturn/src/video/rose_draw.h`, `saturn/src/video/rose_draw.cxx`
- Modify: `saturn/src/video/command_rose.h`, `saturn/src/video/command_rose.c`
- Modify: `saturn/src/video/command_view.h:48-60`, `saturn/src/video/command_view.cxx:671-703,949`
- Modify: `saturn/src/video/console_view.cxx:732`
- Test: `saturn/tests/test_command_rose.c`

**Interfaces:**
- Consumes: `cr_row`, `cr_dir_cell` (`command_rose.h`, unchanged); `text_print`, `text_print_hl` (`text_map.h`); `CV_TRAVEL_X` (`command_view.h:29`).
- Produces:
  - `const char *cr_dir_word(int dir)` — direction index → canonical word, `""` out of range.
  - `void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel)` — same signature and C++ linkage as today, new home.

- [ ] **Step 1: Write the failing test for `cr_dir_word`**

Append to `saturn/tests/test_command_rose.c`, before `main`:

```c
static void test_dir_words(void) {
    assert(strcmp(cr_dir_word(RM_N),    "north") == 0);
    assert(strcmp(cr_dir_word(RM_E),    "east")  == 0);
    assert(strcmp(cr_dir_word(RM_W),    "west")  == 0);
    assert(strcmp(cr_dir_word(RM_S),    "south") == 0);
    assert(strcmp(cr_dir_word(RM_NE),   "ne")    == 0);
    assert(strcmp(cr_dir_word(RM_NW),   "nw")    == 0);
    assert(strcmp(cr_dir_word(RM_SE),   "se")    == 0);
    assert(strcmp(cr_dir_word(RM_SW),   "sw")    == 0);
    assert(strcmp(cr_dir_word(RM_UP),   "up")    == 0);
    assert(strcmp(cr_dir_word(RM_DOWN), "down")  == 0);
    assert(strcmp(cr_dir_word(RM_IN),   "in")    == 0);
    assert(strcmp(cr_dir_word(RM_OUT),  "out")   == 0);
    assert(strcmp(cr_dir_word(-1), "") == 0);
    assert(strcmp(cr_dir_word(RM_DIR_N), "") == 0);
}
```

Add `test_dir_words();` to `main`'s call list.

- [ ] **Step 2: Run the test and confirm it fails**

```bash
gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
    saturn/tests/test_command_rose.c saturn/src/video/command_rose.c && /tmp/tcr.exe
```

Expected: compile error, `implicit declaration of function 'cr_dir_word'`.

- [ ] **Step 3: Declare `cr_dir_word` in `command_rose.h`**

Inside the existing `extern "C"` block, after `int cr_dir_row(int dir);`:

```c
/*----------------------
 | cr_dir_word
 | Description: The canonical spelling of a direction as Infocom's parsers hold
 |   it, so a rose selection can be submitted as a typed command. Duplicates
 |   room_model.c's own table deliberately: this one carries no story dependency,
 |   so a build with no interpreter can still name a direction.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dir -- one of the RM_* direction indices
 | Returns: the word, or "" when dir is out of range
 ----------------------*/
const char *cr_dir_word(int dir);
```

- [ ] **Step 4: Implement `cr_dir_word` in `command_rose.c`**

Append to `saturn/src/video/command_rose.c`:

```c
/*----------------------
 | CR_DIR_WORD
 | Description: The direction spellings cr_dir_word returns, in RM_* index order.
 | Author: suinevere
 ----------------------*/
static const char *CR_DIR_WORD[RM_DIR_N] = {
    "north", "east", "west", "south", "ne", "nw", "se", "sw",
    "up", "down", "in", "out"
};

const char *cr_dir_word(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return "";
    return CR_DIR_WORD[dir];
}
```

- [ ] **Step 5: Run the test and confirm it passes**

```bash
gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
    saturn/tests/test_command_rose.c saturn/src/video/command_rose.c && /tmp/tcr.exe
```

Expected: PASS, no output, exit 0.

- [ ] **Step 6: Create `saturn/src/video/rose_draw.h`**

```c
/*----------------------
 | rose_draw.h
 | Description: The one call that paints a compass-rose row through the text map,
 |   split out of command_view so a build can draw a rose without linking the
 |   command panel. C++ linkage because the body calls text_map's text_print,
 |   which is a C++ inline outside that header's extern "C" block.
 | Author: suinevere
 | Dependencies: command_rose.h (the row composition it draws)
 ----------------------*/
#ifndef ROSE_DRAW_H
#define ROSE_DRAW_H

/*----------------------
 | cv_draw_rose_row
 | Description: Draws one composed rose row at a text-map cell row, overprinting
 |   the selected direction's label in reverse video.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h, command_view.h (CV_TRAVEL_X)
 | Globals: N/A
 | Params: row -- rose row 0..CR_ROWS-1; exits -- RM_DIR_N exit states;
 |   y -- text-map cell row; sel -- selected RM_* direction, or negative for none
 | Returns: N/A
 ----------------------*/
void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel);

#endif /* ROSE_DRAW_H */
```

- [ ] **Step 7: Create `saturn/src/video/rose_draw.cxx` with the body moved verbatim**

Cut lines 671-703 of `saturn/src/video/command_view.cxx` (the `cv_draw_rose_row` header block and its definition) and paste the body here. The code is unchanged:

```cpp
/*----------------------
 | rose_draw.cxx
 | Description: The rose-row draw call described in rose_draw.h, moved here from
 |   command_view.cxx so console_view can draw a rose without pulling the command
 |   panel's 10 KB of image and 15 KB of .bss into a build that never shows it.
 | Author: suinevere
 | Dependencies: rose_draw.h, command_rose.h, command_view.h, text_map.h
 ----------------------*/
#include "rose_draw.h"
#include "command_rose.h"
#include "command_view.h"
#include "text_map.h"

void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel) {
    char buf[CR_COLS + 1];
    int srow, scol, slen;
    cr_row(exits, row, buf);
    text_print(CV_TRAVEL_X, y, buf);
    if (sel < 0 || !cr_dir_cell(sel, &srow, &scol, &slen) || srow != row) return;
    if (buf[scol] == ' ') return;
    {
        char label[5];
        int i;
        for (i = 0; i < slen && i < (int) sizeof label - 1; i++) label[i] = buf[scol + i];
        label[i] = '\0';
        text_print_hl(CV_TRAVEL_X + scol, y, label);
    }
}
```

The original's in-function comment about blank highlights is dropped per `CLAUDE.md` ("No comments inside functions"); its content is already carried by the `Description` line.

- [ ] **Step 8: Remove the declaration from `command_view.h`**

Delete the `cv_draw_rose_row` header block and declaration at `command_view.h:48-60`. Leave `CV_TRAVEL_X` and `CV_STRIP_ROWS` in place — both remain header-only constants other files use.

- [ ] **Step 9: Add the include to both call sites**

In `saturn/src/video/command_view.cxx`, after `#include "command_rose.h"` (line 26):

```cpp
#include "rose_draw.h"
```

In `saturn/src/video/console_view.cxx`, after `#include "command_rose.h"` (line 19):

```cpp
#include "rose_draw.h"
```

Neither call site's code changes — `command_view.cxx:949` and `console_view.cxx:732` still call `cv_draw_rose_row(row, exits, y, sel)` with the same arguments.

- [ ] **Step 10: Re-run the host rose test**

```bash
gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
    saturn/tests/test_command_rose.c saturn/src/video/command_rose.c && /tmp/tcr.exe
```

Expected: PASS. `rose_draw.cxx` is not in this test's link set — it needs the Saturn text map — so this only confirms `command_rose.c` still builds standalone.

- [ ] **Step 11: Hand off the CD build**

The makefile's CD `SOURCES` globs `src/**/*.cxx`, so `rose_draw.cxx` is picked up with no makefile edit. Ask the user to run:

```
cd saturn && ./compile.bat
```

Expected: builds clean; the in-game rose still shows room-accurate exits; `cd/data/0.bin` differs from 368,723 by roughly ±200 bytes.

- [ ] **Step 12: Commit**

```bash
git add saturn/src/video/rose_draw.h saturn/src/video/rose_draw.cxx \
        saturn/src/video/command_rose.h saturn/src/video/command_rose.c \
        saturn/src/video/command_view.h saturn/src/video/command_view.cxx \
        saturn/src/video/console_view.cxx saturn/tests/test_command_rose.c
git commit -m "Move the rose-row draw call into its own unit and give the rose module its own direction-word table, so a build can draw a compass rose without linking the command panel or the room model."
```

---

## Task 2: The netbin's all-directions rose

Un-gate `console_view`'s five `#ifndef NETBIN` regions, feed the rose a static all-open exits array instead of a room model, and turn on the strip layout for the session. Ends with a netbin that links and shows the rose.

**Files:**
- Modify: `saturn/src/video/console_view.cxx:415-531,547-550,558-570,677-764,806-808`
- Modify: `saturn/src/main_netbin.cxx:270-292`
- Modify: `saturn/Makefile:59-79`
- Test: `saturn/tests/test_netbin_sources.py`

**Interfaces:**
- Consumes: `cr_dir_word` and `cv_draw_rose_row` from Task 1; `cr_enter`, `cr_move`, `cr_dir_row` (`command_rose.h`); `game_kb_char_at`, `game_kb_move` (`game_kb.h`); `g_in_game` (`app_state.h:149`); `CV_STRIP_ROWS` (`command_view.h:32`).
- Produces: nothing new for later tasks.

- [ ] **Step 1: Replace `kb_exits` with the all-open table**

Replace `saturn/src/video/console_view.cxx:444-460` in full:

```cpp
/*----------------------
 | KB_EXITS_ALL / kb_exits
 | Description: The exit states the keyboard's rose draws. With an interpreter in
 |   hand that is the current room's own exits, flattened to conditional on Hard so
 |   the rose gives no more away than the panel's. With the game on a remote server
 |   there is no object tree to read and rooms share names with different exits, so
 |   every direction is offered and the server refuses the ones that do not exist.
 | Author: suinevere
 | Dependencies: room_model.h (RM_* constants; no link edge under NETBIN)
 | Globals: g_difficulty
 | Params: flat -- RM_DIR_N scratch the flattened copy is built in
 | Returns: the exit states to draw
 ----------------------*/
#ifdef NETBIN
static const unsigned char KB_EXITS_ALL[RM_DIR_N] = {
    RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN,
    RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN,
    RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN
};
static const unsigned char *kb_exits(unsigned char *flat) {
    (void) flat;
    return KB_EXITS_ALL;
}
#else
static const unsigned char *kb_exits(unsigned char *flat) {
    const RoomModel *m = room_model_get();
    if (g_difficulty != DIFF_HARD) return m->exits;
    for (int i = 0; i < RM_DIR_N; i++)
        flat[i] = (m->exits[i] == RM_EXIT_OPEN) ? RM_EXIT_MAYBE : m->exits[i];
    return flat;
}
#endif
```

- [ ] **Step 2: Replace the direction-word call**

At `saturn/src/video/console_view.cxx:528`, inside `game_kb_travel`:

```cpp
    keyboard_load_line(&k, cr_dir_word(d));
```

was `room_model_dir_word(d)`. Update that function's `Dependencies:` line to name `command_rose.h` instead of `room_model.h`.

- [ ] **Step 3: Remove the five NETBIN gates**

Delete these lines from `saturn/src/video/console_view.cxx`, leaving the code they wrapped in place. Work bottom-up so earlier line numbers stay valid:

| Delete | Was |
| --- | --- |
| 806 and 808 | `#ifndef NETBIN` / `#endif` around the `render_game_keyboard` dispatch |
| 677 and 764 | `#ifndef NETBIN` / `#endif` around `render_game_keyboard` |
| 558 and 570 | `#ifndef NETBIN` / `#endif` around the face-button dispatch |
| 547 and 550 | `#ifndef NETBIN` / `#endif` around the d-pad dispatch |
| 415 and 531 | `#ifndef NETBIN` / `#endif` around `kb_rose_row` … `game_kb_travel` |

Do **not** delete the `#ifdef NETBIN` / `#else` / `#endif` added in Step 1 — that one stays.

- [ ] **Step 4: Set `g_in_game` for the session**

In `saturn/src/main_netbin.cxx`, after the existing `g_menu_backing_depth = 0;` post-setjmp reset (~line 277), add:

```cpp
    g_in_game = false;
```

and change the main loop to bracket the session:

```cpp
    bool auto_dial = valid_dialnum(g_dialnum);
    for (;;) {
        menu_clear();
        if (!auto_dial) netbin_dial_page();
        auto_dial = false;
        g_in_game = true;
        online_mode();
        g_in_game = false;
    }
```

The post-setjmp clear is what makes the reboot longjmp safe: it skips the `g_in_game = false` after `online_mode()`, and without the reset the dial page would render with the strip layout. This mirrors the `g_menu_backing_depth` reset directly above it and the same pattern in `main.cxx`.

Update `main_netbin.cxx`'s `main` header block `Globals:` line to include `g_in_game`.

- [ ] **Step 5: Add the two sources to the netbin object list**

In `saturn/Makefile`, inside the `ifeq ($(strip $(NETBIN)),1)` block, add to `SOURCES` after `src/video/glyph_invert.c \`:

```
          src/video/command_rose.c \
          src/video/rose_draw.cxx \
          src/input/game_kb.c \
```

None of the three is netbin-only — the CD build compiles all three already — so `NETBIN_ONLY_SOURCES` does not change.

- [ ] **Step 6: Update the source-list gate**

In `saturn/tests/test_netbin_sources.py`, add to `EXPECTED`:

```python
    "src/video/command_rose.c",
    "src/video/rose_draw.cxx",
    "src/input/game_kb.c",
```

Update the module docstring's "exactly 21 objects" to "exactly 24 objects".

- [ ] **Step 7: Run the source-list test**

```bash
cd saturn && python3 tests/test_netbin_sources.py
```

Expected: exits 0 with no assertion failure.

- [ ] **Step 8: Hand off the netbin build**

Ask the user to run:

```
cd saturn && ./compile-netbin.bat
```

Expected: links clean with no undefined `cr_*`, `game_kb_*`, `cv_draw_rose_row` or `room_model_*` symbols, and `post.makefile` reports `zaturn.netbin: <size> bytes (limit 409600)` at roughly 129 KB.

If the link reports an undefined symbol, the cause is a sixth call into the dropped cluster that this plan did not find; get its name from the error and check `sh2eb-elf-nm -u src/video/console_view.o` for what else it needs.

- [ ] **Step 9: Hand off the hardware check**

On hardware or in the loader: dial, connect, confirm the strip renders with all twelve directions, the d-pad crosses left from the keyboard grid onto the rose and back, and selecting a direction submits its word. Watch the console row budget — the strip costs five more rows than the four-row grid it replaces, which is the top risk in the spec.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/video/console_view.cxx saturn/src/main_netbin.cxx \
        saturn/Makefile saturn/tests/test_netbin_sources.py
git commit -m "Give the netbin terminal the bordered keyboard strip and a compass rose that offers all twelve directions, since a server-driven session has no object tree to read and rooms sharing a name with different exits would make a guessed rose confidently wrong."
```

---

## Task 3: The blob generator

A binary-to-C-array generator, needed because the netbin has no CD to read a story from. Written fresh: the `netbin-build` branch that carried the original is unreachable in this repository.

**Files:**
- Create: `tools/gen_blob.py`
- Test: `saturn/tests/test_gen_blob.py`

**Interfaces:**
- Consumes: nothing.
- Produces: a CLI, `python3 tools/gen_blob.py OUT.c NAME=PATH [NAME=PATH ...]`, emitting for each `NAME` a `const unsigned char netbin_<NAME>_bytes[]` and `const unsigned int netbin_<NAME>_len`, wrapped in `#ifdef NETBIN`.

- [ ] **Step 1: Write the failing round-trip test**

Create `saturn/tests/test_gen_blob.py`:

```python
#!/usr/bin/env python3
"""Host test that tools/gen_blob.py round-trips bytes exactly: every byte of the
input appears in the generated array, in order, and the emitted length matches.
Parses the generated C rather than compiling it, so the test needs no toolchain."""
import pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "tools" / "gen_blob.py"


def test_round_trip():
    payload = bytes(range(256)) * 3 + b"\x00\xff\x7f"
    with tempfile.TemporaryDirectory() as td:
        src = pathlib.Path(td) / "payload.bin"
        src.write_bytes(payload)
        out = pathlib.Path(td) / "blob.c"
        subprocess.run([sys.executable, str(GEN), str(out), f"story={src}"],
                       check=True)
        text = out.read_text(encoding="utf-8")

    body = re.search(r"netbin_story_bytes\[\d+\] = \{(.*?)\};", text, re.S)
    assert body, "no netbin_story_bytes array emitted"
    got = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body.group(1)))
    assert got == payload, f"round-trip mismatch: {len(got)} vs {len(payload)} bytes"

    declared = re.search(r"netbin_story_len = (\d+)u;", text)
    assert declared, "no netbin_story_len emitted"
    assert int(declared.group(1)) == len(payload)
    assert "#ifdef NETBIN" in text, "arrays must be NETBIN-guarded"

    # The accessors are generated, not hand-appended: this file is regenerated
    # whenever the payload changes, and anything added by hand would be lost.
    assert "netbin_story_data(void) { return netbin_story_bytes; }" in text
    assert "netbin_story_size(void) { return netbin_story_len; }" in text
    assert "netbin_story_data(void) { return 0; }" in text, "no non-NETBIN stub"
    assert "netbin_story_size(void) { return 0u; }" in text, "no non-NETBIN stub"


if __name__ == "__main__":
    test_round_trip()
    print("ok")
```

- [ ] **Step 2: Run it and confirm it fails**

```bash
cd saturn && python3 tests/test_gen_blob.py
```

Expected: fails — `tools/gen_blob.py` does not exist, so `subprocess.run` raises `FileNotFoundError`.

- [ ] **Step 3: Write the generator**

Create `tools/gen_blob.py`:

```python
#!/usr/bin/env python3
"""Convert binary files into a single C source of byte arrays.

Emits arrays guarded by #ifdef NETBIN so the generated file compiles to an
empty object in the CD build, which globs every src/*.c unconditionally.

Usage:
    python3 tools/gen_blob.py OUT.c NAME=PATH [NAME=PATH ...]
"""
import sys


def emit_array(name, data):
    out = [f"const unsigned char netbin_{name}_bytes[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        out.append("    " + "".join(f"0x{b:02x}," for b in data[i:i + 16]))
    out.append("};")
    out.append(f"const unsigned int netbin_{name}_len = {len(data)}u;")
    out.append("")
    out.append(f"const unsigned char *netbin_{name}_data(void)"
               f" {{ return netbin_{name}_bytes; }}")
    out.append(f"unsigned int         netbin_{name}_size(void)"
               f" {{ return netbin_{name}_len; }}")
    return "\n".join(out)


def emit_stub(name):
    return "\n".join([
        f"const unsigned char *netbin_{name}_data(void) {{ return 0; }}",
        f"unsigned int         netbin_{name}_size(void) {{ return 0u; }}",
    ])


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    out_path, specs = argv[1], argv[2:]

    names = []
    for spec in specs:
        if "=" not in spec:
            sys.stderr.write(f"gen_blob: bad spec {spec!r}, want NAME=PATH\n")
            return 2
        names.append(spec.split("=", 1))

    parts = [
        "/* GENERATED by tools/gen_blob.py -- do not edit by hand. */",
        '#include "netbin_story.h"',
        "",
        "#ifdef NETBIN",
        "",
    ]
    for name, path in names:
        with open(path, "rb") as f:
            data = f.read()
        parts.append(emit_array(name, data))
        parts.append("")
    parts.append("#else")
    parts.append("")
    for name, _ in names:
        parts.append(emit_stub(name))
        parts.append("")
    parts.append("#endif /* NETBIN */")

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Run the test and confirm it passes**

```bash
cd saturn && python3 tests/test_gen_blob.py
```

Expected: prints `ok`, exits 0.

- [ ] **Step 5: Commit**

```bash
git add tools/gen_blob.py saturn/tests/test_gen_blob.py
git commit -m "Add a generator that turns a binary file into a NETBIN-guarded C byte array, with a host test that the bytes survive the trip unchanged."
```

---

## Task 4: Generate the embedded story and the single-game overlay

**Files:**
- Create: `saturn/src/input/netbin_story.h`
- Create (generated): `saturn/src/input/netbin_story.c`, `saturn/src/input/typeahead_solution_zork1.c`
- Modify: `tools/typeahead/gen_all.ps1`

**Interfaces:**
- Consumes: `tools/gen_blob.py` from Task 3.
- Produces:
  - `const unsigned char *netbin_story_data(void)` — the embedded story, `NULL` without `NETBIN`.
  - `unsigned int netbin_story_size(void)` — its length, `0` without `NETBIN`.
  - `int apply_solution_overlay(TrieNode *root, const unsigned char *story, unsigned int len)` — same signature as `typeahead_solution.c`'s, Zork I only. The two are never linked together.

- [ ] **Step 1: Write the accessor header**

Create `saturn/src/input/netbin_story.h`:

```c
/*----------------------
 | netbin_story.h
 | Description: The story image embedded in the netbin, which has no CD to read
 |   one from. It exists only so the typeahead layer has a dictionary and grammar
 |   to build a trie from -- no interpreter runs here, and the bytes are never
 |   executed or written. Returns NULL/0 in the CD build.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef NETBIN_STORY_H
#define NETBIN_STORY_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | netbin_story_data / netbin_story_size
 | Description: The embedded story bytes and their length. The pointer addresses
 |   .rodata directly, so callers must treat it as read-only and must not free it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: netbin_story_bytes, netbin_story_len (generated)
 | Params: N/A
 | Returns: the bytes and length, or NULL and 0 when NETBIN is not defined
 ----------------------*/
const unsigned char *netbin_story_data(void);
unsigned int         netbin_story_size(void);

#ifdef __cplusplus
}
#endif

#endif /* NETBIN_STORY_H */
```

- [ ] **Step 2: Generate the story blob**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
python3 tools/gen_blob.py saturn/src/input/netbin_story.c \
    story=saturn/cd/data/Z3/ZORK1.Z3
```

- [ ] **Step 3: Verify the blob length matches the source file**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
grep -o "netbin_story_len = [0-9]*u" saturn/src/input/netbin_story.c
stat -c%s saturn/cd/data/Z3/ZORK1.Z3
```

Expected: both report `84876`.

- [ ] **Step 4: Generate the Zork-I-only overlay**

```bash
cd /c/Users/saggl/CLionProjects/zaturn/tools/typeahead
python3 gen_solution.py \
    --game ../../saturn/cd/data/Z3/ZORK1.Z3:./solutions/ZORK1.WIN \
    --out  ../../saturn/src/input/typeahead_solution_zork1.c
```

Expected output line: `release 88 serial '840726' -> 109 word boosts, 149 transitions`.

- [ ] **Step 5: Add the second invocation to `gen_all.ps1`**

At the end of `tools/typeahead/gen_all.ps1`, after the existing combined-file generation:

```powershell
# The netbin links a Zork-I-only copy instead of the 25-game table: it serves one
# game, and the full table is 64.8 KB against this slice's 3.8 KB. Same symbol,
# never linked together -- the netbin's source list is explicit and the CD build's
# glob filters the netbin-only sources out.
python gen_solution.py `
    --game ../../saturn/cd/data/Z3/ZORK1.Z3:./solutions/ZORK1.WIN `
    --out  ../../saturn/src/input/typeahead_solution_zork1.c
```

Extend the file's header comment ("Why one file, not one-per-game") with a sentence noting the netbin exception.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input/netbin_story.h saturn/src/input/netbin_story.c \
        saturn/src/input/typeahead_solution_zork1.c tools/typeahead/gen_all.ps1
git commit -m "Embed ZORK1.Z3 and a Zork-I-only solution overlay for the netbin, and teach the overlay generator to emit that single-game copy alongside the disc build's 25-game table."
```

---

## Task 5: Build the netbin's trie from the embedded story

Replace the early return that leaves the trie empty, and move the trie allocator off High Work RAM now that it holds 77 KB instead of one empty root node.

**Files:**
- Modify: `saturn/src/net/online.cxx:208-215`
- Modify: `saturn/src/main_netbin.cxx:61-85`
- Modify: `saturn/Makefile:59-79`
- Test: `saturn/tests/test_netbin_typeahead.c`, `saturn/tests/test_netbin_sources.py`

**Interfaces:**
- Consumes: `netbin_story_data`, `netbin_story_size` (Task 4); `build_typeahead_from_story` (`typeahead_extract.h`); `apply_solution_overlay` (`typeahead_solution.h`); `typeahead_add_abbreviations`, `create_trie_node` (`typeahead.h`).
- Produces: nothing for later tasks.

- [ ] **Step 1: Write the failing host test**

Create `saturn/tests/test_netbin_typeahead.c`:

```c
/*----------------------
 | test_netbin_typeahead.c
 | Description: Host test that the story the netbin embeds yields a usable trie:
 |   the solution overlay recognises it, and a prefix predicts a real Zork I word.
 |   Reads the story from the disc copy rather than the generated array so the test
 |   needs no NETBIN compile; Task 4 already pins the two to the same bytes.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -o /tmp/tnt.exe saturn/tests/test_netbin_typeahead.c \
 |          saturn/src/input/typeahead.c saturn/src/input/typeahead_extract.c \
 |          saturn/src/input/typeahead_solution_zork1.c \
 |          -I saturn/src/input && /tmp/tnt.exe saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *typeahead_malloc(unsigned int n) { return malloc(n); }
void  typeahead_free(void *p) { free(p); }

int main(int argc, char **argv) {
    FILE *f;
    long n;
    unsigned char *story;
    TrieNode *root;
    DictionaryWord *out[8];
    int got, i, found_lamp = 0;

    assert(argc == 2);
    f = fopen(argv[1], "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) n);
    assert(fread(story, 1, (size_t) n, f) == (size_t) n);
    fclose(f);

    root = create_trie_node();
    build_typeahead_from_story(root, story, (unsigned int) n);
    assert(apply_solution_overlay(root, story, (unsigned int) n) == 1);
    typeahead_add_abbreviations(root);

    assert(find_exact_word(root, "lamp") != NULL);
    assert(find_exact_word(root, "north") != NULL);

    got = predict_candidates(root, NULL, "lam", out, 8, 0);
    assert(got > 0);
    for (i = 0; i < got; i++)
        if (strcmp(out[i]->text, "lamp") == 0) found_lamp = 1;
    assert(found_lamp);

    printf("ok\n");
    return 0;
}
```

- [ ] **Step 2: Run it and confirm it fails**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -std=c11 -Wall -o /tmp/tnt.exe saturn/tests/test_netbin_typeahead.c \
    saturn/src/input/typeahead.c saturn/src/input/typeahead_extract.c \
    saturn/src/input/typeahead_solution_zork1.c \
    -I saturn/src/input && /tmp/tnt.exe saturn/cd/data/Z3/ZORK1.Z3
```

Expected on a tree without Task 4: link error, no `apply_solution_overlay`. If Task 4 is already applied this test passes immediately — that is fine, it is a characterisation test for the generated data, and Steps 3-5 are the behaviour change.

- [ ] **Step 3: Replace the NETBIN early return**

In `saturn/src/net/online.cxx`, replace lines 208-215 (the `#ifdef NETBIN` block ending in `return;`) with:

```cpp
#ifdef NETBIN
    // The story is a .rodata blob, and both builders take a const pointer, so it
    // is read in place -- no allocation, and nothing to free afterward.
    build_typeahead_from_story(g_online_ta, netbin_story_data(), netbin_story_size());
    apply_solution_overlay(g_online_ta, netbin_story_data(), netbin_story_size());
    typeahead_add_abbreviations(g_online_ta);
    return;
#else
```

Add `#include "netbin_story.h"` to the file's include list. Update the `ensure_online_typeahead` header block: the `Description` no longer says the rebuild is skipped, and `Dependencies` gains `netbin_story.h`.

- [ ] **Step 4: Move the trie allocator to Low Work RAM**

In `saturn/src/main_netbin.cxx`, replace the bodies at lines 78-85:

```cpp
extern "C" void *typeahead_malloc(unsigned int size) {
    return SRL::Memory::LowWorkRam::Malloc((uint32_t) size);
}

extern "C" void typeahead_free(void *ptr) {
    if (ptr != nullptr) SRL::Memory::LowWorkRam::Free(ptr);
}
```

Replace the header block's High-Work-RAM rationale, which is now false, with:

```
 |   Low Work RAM, matching engine/saturn_glue.cxx's choice for the CD build: the
 |   Zork I trie is 4,722 allocations totalling about 77 KB, which does not belong
 |   in the same High Work RAM heap the image and .bss already share. LWRAM is
 |   otherwise unclaimed in this build.
```

- [ ] **Step 5: Add the three sources to the netbin object list**

In `saturn/Makefile`, inside the `NETBIN` block, add to `SOURCES`:

```
          src/input/typeahead_extract.c \
          src/input/typeahead_solution_zork1.c \
          src/input/netbin_story.c \
```

Add **one** file to `NETBIN_ONLY_SOURCES` so the CD build's glob excludes it:

```
NETBIN_ONLY_SOURCES = src/main_netbin.cxx src/net/netbin_pages.cxx \
                      src/input/typeahead_solution_zork1.c
```

Only `typeahead_solution_zork1.c` is netbin-only, and it must be: `typeahead_solution.c:155`
already defines `apply_solution_overlay`, so letting the CD glob pick up the Zork-I copy
would hand the linker a duplicate definition.

`netbin_story.c` is **not** netbin-only. Nothing else in the tree defines
`netbin_story_data`/`netbin_story_size`, and its byte array lives inside `#ifdef NETBIN`,
so in the CD build it compiles to just the two `return 0;` stubs — a near-empty object.
That is exactly what the spec specifies ("the file compiles to an empty object in the CD
build's find-globbed source list"), and it is what makes the generated stubs meaningful
rather than dead code.

`typeahead_extract.c` is **not** netbin-only either — the CD build already compiles it.

- [ ] **Step 6: Update the source-list gate**

In `saturn/tests/test_netbin_sources.py`, add to `EXPECTED`:

```python
    "src/input/typeahead_extract.c",
    "src/input/typeahead_solution_zork1.c",
    "src/input/netbin_story.c",
```

and to `NETBIN_ONLY`:

```python
NETBIN_ONLY = {"src/main_netbin.cxx", "src/net/netbin_pages.cxx",
               "src/input/typeahead_solution_zork1.c"}
```

Update the docstring's object count from "24" (set in Task 2) to "27".

- [ ] **Step 7: Run both host tests**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -std=c11 -Wall -o /tmp/tnt.exe saturn/tests/test_netbin_typeahead.c \
    saturn/src/input/typeahead.c saturn/src/input/typeahead_extract.c \
    saturn/src/input/typeahead_solution_zork1.c \
    -I saturn/src/input && /tmp/tnt.exe saturn/cd/data/Z3/ZORK1.Z3
cd saturn && python3 tests/test_netbin_sources.py
```

Expected: `ok` from the first, exit 0 from the second.

- [ ] **Step 8: Hand off both builds**

Ask the user to run:

```
cd saturn && ./compile.bat
```

which builds the netbin first and the CD image second. Expected: `zaturn.netbin: <size> bytes (limit 409600)` at roughly 220 KB, and a CD build that still works — `netbin_story.c` compiles to an empty object there, and `typeahead_solution_zork1.c` is not in its source list at all.

- [ ] **Step 9: Hand off the hardware check**

Dial, connect, type a prefix at the multizork prompt, confirm the suggestion row appears and cycles. This is the only check that exercises the Low Work RAM allocation, which the spec flags as unproven in this build — if allocation fails, the trie will be empty and suggestions silently absent rather than crashing, so check for suggestions specifically rather than for a clean boot.

Also confirm the served story matches the embedded one: if the multizork server runs a multiplayer-modified Zork I, its release and serial will differ from 88 / `840726` and the overlay will silently no-op, leaving grammar-only suggestions.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/net/online.cxx saturn/src/main_netbin.cxx saturn/Makefile \
        saturn/tests/test_netbin_sources.py saturn/tests/test_netbin_typeahead.c
git commit -m "Build the netbin's typeahead trie from the embedded story instead of leaving it empty, and move the trie allocator to Low Work RAM now that it holds 77 KB rather than a single root node."
```

---

## Self-Review Notes

**Spec coverage.** Rose spec: `cv_draw_rose_row` relocation (Task 1), `cr_dir_word` (Task 1), all-open `kb_exits` (Task 2), five gates (Task 2), `g_in_game` (Task 2), makefile and test (Task 2) — complete. The spec proposed `command_rose.c` as the relocation target; this plan uses a new `rose_draw.cxx` instead, because `text_map.h`'s `extern "C" {` at line 49 is unguarded and no `.c` file in the tree includes it, so a plain-C unit cannot call `text_print`. Update the spec's Architecture section to match.

Typeahead spec: `gen_blob.py` (Task 3), `netbin_story` (Task 4), single-game overlay (Task 4), `gen_all.ps1` (Task 4), `online.cxx` rewire (Task 5), LWRAM move (Task 5), makefile and test (Task 5) — complete. The spec's `online.cxx:212` and 07-25 spec comment corrections are folded into Task 5 Step 3's header-block rewrite.

**Known deviation from the spec's size table.** The rose spec costs the relocation at ~200 B inside `command_rose.o`; with a separate `rose_draw.cxx` it is ~200 B in a new object instead. The total is unchanged.
