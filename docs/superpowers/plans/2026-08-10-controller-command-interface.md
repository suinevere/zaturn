# Controller Command Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the gamepad a Shadowgate-style command panel that assembles commands from the running room's decoded exits, contents and grammar, instead of spelling them one letter at a time.

**Architecture:** A new pure-C `room_model` decodes the live story image each prompt (current room from global 0, exits from the room object's direction properties, contents from the object tree). A pure-C `command_panel` turns picks into a command string. A `command_view` draws a three-module strip, using inverted glyphs generated on demand into font 0's unused control-code tiles. `saturn_readline` dispatches to the panel or the existing keyboard editor. The panel's only output is `KeyboardState.input` plus `keyboard_submit`, so every downstream path is untouched.

**Tech Stack:** C11 for the testable modules, C++ (SRL/SGL) for rendering and glue, gcc host tests, SH-2 cross-compiler for syntax checking.

**Spec:** `docs/superpowers/specs/2026-08-10-controller-command-interface-design.md`

## Global Constraints

- **Never run `compile.bat`, `compile-cd.bat`, or the emulator.** The Saturn build is the user's. Verify `.cxx`/`.h` changes with `sh saturn/syntax-check.sh <files>` (writes no objects) and hand the tree back.
- **Host tests are gcc, run from the repo root**, binaries to `/tmp/<name>.exe`, following `saturn/tests/test_room_genre.c`.
- **New `.c` files are plain C11** — they are compiled by the SH-2 C compiler and by host gcc. No C++ in a `.c` file.
- **No comments inside functions.** Every file, function and constant gets a header block in the house format (see any file in `saturn/src/`). Tests get a file header only.
- **Header blocks go in the `.c` as well as the `.h`** — on the definition, not only the declaration. `saturn/src/input/keyboard.c` and `saturn/src/classify/room_class.c` both carry a full block above every non-trivial definition even though their headers already document the same function. Several code blocks in the tasks below show blocks only on the static helpers; that is an omission in this plan, not the house style. Add them on the public definitions too — `Description` / `Author` / `Dependencies` / `Globals` / `Params` / `Returns`, with `N/A` where a field does not apply.
- **Never write VDP2 VRAM outside the `OnAfterSync` flush.** `text_map` exists because VDP2 re-reads a cell's pattern name on every scanline, so a store landing while the beam is inside a row tears. That applies to font-tile writes as much as to map writes: compose in RAM, write in vblank.
- **No dynamic allocation** in `room_model` or `command_panel`. High Work RAM is already carrying the story image plus a 115-200 KB typeahead trie; these modules use fixed arrays.
- **Screen is 40 columns.** Module widths are travel 13, words 15, commands 8, with single `|` dividers and one border column each side: `1 + 13 + 1 + 15 + 1 + 8 + 1 = 40`.
- **Commit after every task.** One sentence, no body, no trailers. Never mention AI, Claude, or the session.
- Author of record in every header block is `suinevere`.

## File Structure

| File | Responsibility |
|---|---|
| `saturn/src/video/glyph_invert.c/.h` | **New.** Pure C: the 4bpp tile inversion transform and the character→scratch-slot cache. No VRAM, no SRL. |
| `saturn/src/video/text_map.h/.cxx` | **Modify.** Add `text_print_hl`; write inverted tiles to VRAM in the existing flush path. |
| `saturn/src/menu/options.cxx` | **Modify.** `text_set_color` also writes CRAM entry 2; MOJOOPTS gains a versioned gameplay block. |
| `saturn/src/engine/room_model.c/.h` | **New.** Pure C: direction-property map, per-room exits, contents, carried items, dictionary lookup. |
| `saturn/src/input/command_panel.c/.h` | **New.** Pure C: focus, slot progression, word-page fill, sentence assembly. |
| `saturn/src/video/command_rose.c/.h` | **New.** Pure C: compose one rose row from an exit-state array. |
| `saturn/src/video/command_view.h/.cxx` | **New.** Draw the strip; `command_edit` reads the pad and drives the panel. |
| `saturn/src/engine/saturn_glue.cxx` | **Modify.** Refresh the room model per prompt; dispatch to panel or keyboard. |
| `saturn/src/video/console_view.cxx` | **Modify.** `console_height` third case. |
| `saturn/src/input/input.h/.cxx` | **Modify.** Toggle-button tap detection. |
| `saturn/src/engine/app_state.h/.cxx` | **Modify.** `g_cmd_iface`, `g_cmd_mode`, `g_toggle_btn`. |

---

### Task 1: Inverted glyph slots

Pure-C tile transform and slot cache, plus the `text_map` entry point that draws with them.

**Files:**
- Create: `saturn/src/video/glyph_invert.h`, `saturn/src/video/glyph_invert.c`
- Create: `saturn/tests/test_glyph_invert.c`
- Modify: `saturn/src/video/text_map.h`, `saturn/src/video/text_map.cxx`
- Modify: `saturn/src/menu/options.cxx:56` (`text_set_color`)

**Interfaces:**
- Consumes: nothing.
- Produces: `void text_print_hl(int x, int y, const char *s)` — prints `s` at (x,y) in reverse video. `int gi_slot_for(char c, int *is_new)`, `void gi_invert_tile(const unsigned char *src, unsigned char *dst)`, `void gi_begin_frame(void)`, `void gi_reset(void)`, `GI_SLOT_N`, `GI_TILE_BYTES`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_glyph_invert.c`:

```c
/*----------------------
 | test_glyph_invert.c
 | Description: Host test for the inverted-glyph transform and its scratch-slot
 |   cache. The transform maps a 4bpp font tile's background (pixel value 0) to
 |   the ink colour and its ink (value 1) to CRAM entry 2, producing reverse
 |   video. The cache hands each character one of the 32 control-code tile slots
 |   font 0 already owns, reusing a slot across frames so a steady selection
 |   costs no VRAM writes. No SRL or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/video/glyph_invert.h and glyph_invert.c, assert.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tgi.exe \
 |          saturn/tests/test_glyph_invert.c saturn/src/video/glyph_invert.c \
 |          && /tmp/tgi.exe
 ----------------------*/
#include "../src/video/glyph_invert.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    unsigned char src[GI_TILE_BYTES], dst[GI_TILE_BYTES];
    for (int i = 0; i < GI_TILE_BYTES; i++) src[i] = 0x01;
    gi_invert_tile(src, dst);
    for (int i = 0; i < GI_TILE_BYTES; i++) assert(dst[i] == 0x12);

    for (int i = 0; i < GI_TILE_BYTES; i++) src[i] = 0x10;
    gi_invert_tile(src, dst);
    for (int i = 0; i < GI_TILE_BYTES; i++) assert(dst[i] == 0x21);

    gi_reset();
    gi_begin_frame();
    int is_new = 0;
    int a = gi_slot_for('A', &is_new);
    assert(a >= 0 && a < GI_SLOT_N && is_new == 1);
    int a2 = gi_slot_for('A', &is_new);
    assert(a2 == a && is_new == 0);

    int b = gi_slot_for('B', &is_new);
    assert(b != a && is_new == 1);

    gi_begin_frame();
    int a3 = gi_slot_for('A', &is_new);
    assert(a3 == a && is_new == 0);

    gi_reset();
    gi_begin_frame();
    for (int i = 0; i < GI_SLOT_N; i++) {
        int s = gi_slot_for((char) ('a' + i), &is_new);
        assert(s >= 0 && is_new == 1);
    }
    assert(gi_slot_for('!', &is_new) == -1);

    printf("test_glyph_invert ok\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tgi.exe \
  saturn/tests/test_glyph_invert.c saturn/src/video/glyph_invert.c && /tmp/tgi.exe
```

Expected: FAIL — `glyph_invert.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/video/glyph_invert.h`:

```c
/*----------------------
 | glyph_invert.h
 | Description: Reverse-video glyph support: the 4bpp tile transform that turns
 |   a font tile into its inverse, and the cache that lends each character one of
 |   the 32 tile slots font 0 already owns at character codes 0x00..0x1F (control
 |   codes this program never prints). Pure logic -- no VRAM, no SRL; text_map
 |   owns the writes. Implemented in glyph_invert.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef GLYPH_INVERT_H
#define GLYPH_INVERT_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | GI_SLOT_N / GI_TILE_BYTES
 | Description: How many scratch slots exist (character codes 0x00..0x1F) and
 |   the byte size of one 8x8 4bpp tile.
 | Author: suinevere
 ----------------------*/
#define GI_SLOT_N     32
#define GI_TILE_BYTES 32

/*----------------------
 | gi_invert_tile
 | Description: Writes the reverse-video form of one 8x8 4bpp tile: pixel value
 |   0 (the transparent background) becomes 1 (the ink colour) and value 1
 |   becomes 2 (the highlight letter colour), so the cell paints a solid block
 |   with the letter punched out. Other values pass through untouched.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- GI_TILE_BYTES source tile; dst -- GI_TILE_BYTES destination
 | Returns: N/A
 ----------------------*/
void gi_invert_tile(const unsigned char *src, unsigned char *dst);

/*----------------------
 | gi_begin_frame
 | Description: Opens a new allocation generation. Slots claimed in an earlier
 |   generation become reusable; slots re-requested this generation keep their
 |   character and need no tile rewrite.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_gen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void gi_begin_frame(void);

/*----------------------
 | gi_slot_for
 | Description: The scratch slot holding `c`'s inverted glyph, claiming one if
 |   it does not already hold it. Sets *is_new when the caller must write the
 |   tile; clears it when the slot already carries that character.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot, g_gen
 | Params: c -- character to invert; is_new -- (out) 1 if the tile must be written
 | Returns: the slot index in 0..GI_SLOT_N-1, or -1 when every slot is spoken for
 |   this generation
 ----------------------*/
int gi_slot_for(char c, int *is_new);

/*----------------------
 | gi_reset
 | Description: Forgets every slot assignment, so the next generation starts
 |   from an empty cache. Used at init and by tests.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_slot, g_gen
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void gi_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* GLYPH_INVERT_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/video/glyph_invert.c`:

```c
/*----------------------
 | glyph_invert.c
 | Description: The reverse-video tile transform and the scratch-slot cache
 |   described in glyph_invert.h.
 | Author: suinevere
 | Dependencies: glyph_invert.h
 ----------------------*/
#include "glyph_invert.h"

/*----------------------
 | GiSlot / g_slot / g_gen
 | Description: One scratch slot's occupant and the generation it was last
 |   asked for, plus the current generation. A slot whose gen is not the current
 |   one is free to reclaim; matching it means the slot is in use this frame and
 |   must not be handed to another character.
 | Author: suinevere
 ----------------------*/
typedef struct { char ch; unsigned int gen; int used; } GiSlot;
static GiSlot g_slot[GI_SLOT_N];
static unsigned int g_gen = 1;

void gi_invert_tile(const unsigned char *src, unsigned char *dst) {
    int i;
    for (i = 0; i < GI_TILE_BYTES; i++) {
        unsigned char hi = (unsigned char) ((src[i] >> 4) & 0x0f);
        unsigned char lo = (unsigned char) (src[i] & 0x0f);
        if (hi == 0) hi = 1; else if (hi == 1) hi = 2;
        if (lo == 0) lo = 1; else if (lo == 1) lo = 2;
        dst[i] = (unsigned char) ((hi << 4) | lo);
    }
}

void gi_begin_frame(void) {
    g_gen++;
}

int gi_slot_for(char c, int *is_new) {
    int i;
    for (i = 0; i < GI_SLOT_N; i++) {
        if (g_slot[i].used && g_slot[i].ch == c) {
            g_slot[i].gen = g_gen;
            if (is_new) *is_new = 0;
            return i;
        }
    }
    for (i = 0; i < GI_SLOT_N; i++) {
        if (!g_slot[i].used || g_slot[i].gen != g_gen) {
            g_slot[i].used = 1;
            g_slot[i].ch   = c;
            g_slot[i].gen  = g_gen;
            if (is_new) *is_new = 1;
            return i;
        }
    }
    if (is_new) *is_new = 0;
    return -1;
}

void gi_reset(void) {
    int i;
    for (i = 0; i < GI_SLOT_N; i++) { g_slot[i].used = 0; g_slot[i].ch = 0; g_slot[i].gen = 0; }
    g_gen = 1;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tgi.exe \
  saturn/tests/test_glyph_invert.c saturn/src/video/glyph_invert.c && /tmp/tgi.exe
```

Expected: PASS — `test_glyph_invert ok`.

- [ ] **Step 6: Add `text_print_hl` to the header**

In `saturn/src/video/text_map.h`, inside the `extern "C"` block, after `text_print_str`:

```c
/*----------------------
 | text_print_hl
 | Description: Writes one unformatted string at (x, y) in reverse video, by
 |   resolving each character to an inverted-glyph scratch slot (glyph_invert.h)
 |   and baking that slot's character code into the pattern name. A character
 |   that cannot be given a slot -- more than GI_SLOT_N distinct ones on screen
 |   at once -- is drawn normally rather than dropped.
 | Author: suinevere
 | Dependencies: glyph_invert.h
 | Globals: g_shadow
 | Params: x, y -- cell position; s -- the string
 | Returns: N/A
 ----------------------*/
void text_print_hl(int x, int y, const char *s);
```

- [ ] **Step 7: Implement `text_print_hl` and the tile write**

In `saturn/src/video/text_map.cxx`, add `#include "glyph_invert.h"` beside the existing includes, then after `text_print_str`:

```c
/*----------------------
 | TEXT_FONT_TILES / hl_tile
 | Description: Where font 0's tiles live, and the address of one character
 |   code's tile within it. install_block_glyph (console_view.cxx) writes the
 |   same region for the block cursor; the scratch slots are the low 32 codes of
 |   that same 128-tile block.
 | Author: suinevere
 ----------------------*/
#define TEXT_FONT_TILES (VDP2_VRAM_B1 + 0x18000)

static volatile unsigned char *hl_tile(int code)
{
    return (volatile unsigned char *) (TEXT_FONT_TILES + (code + TEXT_FONT_BANK) * 0x20);
}

extern "C" void text_print_hl(int x, int y, const char *s)
{
    if (y < 0 || y >= TEXT_ROWS || x >= TEXT_COLS || s == nullptr) return;
    if (x < 0) x = 0;

    uint16_t *row = g_shadow[y];

    for (int c = x; c < TEXT_COLS && *s != '\0'; c++)
    {
        char ch = *s++;
        int is_new = 0;
        int slot = gi_slot_for(ch, &is_new);
        int code = (slot < 0) ? (int) (unsigned char) ch : slot;

        if (slot >= 0 && is_new)
        {
            unsigned char src[GI_TILE_BYTES], dst[GI_TILE_BYTES];
            volatile unsigned char *from = hl_tile((int) (unsigned char) ch);
            volatile unsigned char *to   = hl_tile(slot);
            for (int i = 0; i < GI_TILE_BYTES; i++) src[i] = from[i];
            gi_invert_tile(src, dst);
            for (int i = 0; i < GI_TILE_BYTES; i++) to[i] = dst[i];
        }

        uint16_t word = (uint16_t)((uint16_t) code + TEXT_FONT_BANK) | TEXT_COLOR_BANK;
        if (row[c] != word)
        {
            row[c] = word;
            mark_dirty(y);
        }
    }
}
```

In the same file, call `gi_begin_frame()` at the top of `flush_hook`, so a generation spans exactly one displayed frame:

```c
static void flush_hook(void)
{
    gi_begin_frame();
    text_flush();
}
```

- [ ] **Step 8: Write CRAM entry 2**

In `saturn/src/menu/options.cxx`, extend `text_set_color` (line 56):

```c
void text_set_color(unsigned short rgb555) {
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    cram[1]  = rgb555;   // glyph foreground
    cram[2]  = 0;        // reverse-video letter, punched out of the ink block
    cram[15] = rgb555;   // install_block_glyph()'s cursor tile
}
```

Update that function's header block's Description to name entry 2 alongside 1 and 15.

- [ ] **Step 9: Syntax-check the Saturn units**

```bash
sh saturn/syntax-check.sh src/video/text_map.cxx src/menu/options.cxx
```

Expected: `syntax-check: DEBUG build` then `syntax-check: release build`, no errors.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/video/glyph_invert.h saturn/src/video/glyph_invert.c \
        saturn/tests/test_glyph_invert.c saturn/src/video/text_map.h \
        saturn/src/video/text_map.cxx saturn/src/menu/options.cxx
git commit -m "Draw reverse-video text by inverting glyphs into font zero's unused control-code tiles."
```

---

### Task 2: Room model — direction property map

Decode which property number each direction word owns, and gate on the result looking like a ZILCH layout.

**Files:**
- Create: `saturn/src/engine/room_model.h`, `saturn/src/engine/room_model.c`
- Create: `saturn/tests/test_room_model.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `int room_model_bind(const unsigned char *story, unsigned int len)`, `int room_model_available(void)`, `int room_model_dir_prop(int dir)`, `const char *room_model_dir_word(int dir)`, the `RM_*` enums and `RoomModel` struct.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_room_model.c`:

```c
/*----------------------
 | test_room_model.c
 | Description: Host test for the room model's static decode against the shipped
 |   Zork I image. Covers the direction-word to property-number map, the
 |   contiguous-run sanity gate that rejects a non-ZILCH story, and the
 |   dictionary lookup the verb filter depends on. Reads saturn/zork1.dat
 |   directly; no SRL or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/engine/room_model.h and room_model.c, assert.h, stdio.h,
 |   stdlib.h, saturn/zork1.dat
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
 |          saturn/tests/test_room_model.c saturn/src/engine/room_model.c \
 |          && /tmp/trm.exe
 ----------------------*/
#include "../src/engine/room_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char *g_story;
static unsigned int   g_len;

static void load_story(void) {
    FILE *f = fopen("saturn/zork1.dat", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_story = (unsigned char *) malloc((size_t) n);
    assert(g_story != NULL);
    assert(fread(g_story, 1, (size_t) n, f) == (size_t) n);
    fclose(f);
    g_len = (unsigned int) n;
}

int main(void) {
    load_story();

    assert(room_model_bind(g_story, g_len) == 1);
    assert(room_model_available() == 1);

    assert(room_model_dir_prop(RM_N)    == 31);
    assert(room_model_dir_prop(RM_E)    == 30);
    assert(room_model_dir_prop(RM_W)    == 29);
    assert(room_model_dir_prop(RM_S)    == 28);
    assert(room_model_dir_prop(RM_NE)   == 27);
    assert(room_model_dir_prop(RM_NW)   == 26);
    assert(room_model_dir_prop(RM_SE)   == 25);
    assert(room_model_dir_prop(RM_SW)   == 24);
    assert(room_model_dir_prop(RM_UP)   == 23);
    assert(room_model_dir_prop(RM_DOWN) == 22);
    assert(room_model_dir_prop(RM_IN)   == 21);
    assert(room_model_dir_prop(RM_OUT)  == 20);

    assert(room_model_has_word("open")    == 1);
    assert(room_model_has_word("mailbox") == 1);
    assert(room_model_has_word("photosynthesis") == 0);

    /* A story whose header points nowhere sane must report unavailable rather
       than decode garbage. */
    unsigned char junk[64];
    for (int i = 0; i < 64; i++) junk[i] = 0;
    assert(room_model_bind(junk, sizeof junk) == 0);
    assert(room_model_available() == 0);

    printf("test_room_model ok\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
  saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: FAIL — `room_model.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/engine/room_model.h`:

```c
/*----------------------
 | room_model.h
 | Description: A model of the room the player is standing in, decoded from the
 |   live story image: which directions lead somewhere, what objects are here,
 |   and what is being carried. Exits come from the room object's direction
 |   properties, whose numbers are recovered from the dictionary's direction
 |   entries rather than hardcoded, so the decode works for any ZILCH-compiled
 |   v3 story and reports itself unavailable for anything else. Pure C -- no SRL,
 |   no console, no trie. Implemented in room_model.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef ROOM_MODEL_H
#define ROOM_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | RM_DIR_N / RM_HERE_MAX / RM_CARRIED_MAX
 | Description: The twelve directions the panel offers, and the fixed capacities
 |   for a room's visible objects and the player's carried items. Fixed rather
 |   than grown because the C heap is already carrying the story image and the
 |   typeahead trie.
 | Author: suinevere
 ----------------------*/
#define RM_DIR_N        12
#define RM_HERE_MAX     24
#define RM_CARRIED_MAX  16

/*----------------------
 | RM_N .. RM_OUT
 | Description: Direction indices, in the order the compass rose reads them.
 | Author: suinevere
 ----------------------*/
enum { RM_N = 0, RM_E, RM_W, RM_S, RM_NE, RM_NW, RM_SE, RM_SW,
       RM_UP, RM_DOWN, RM_IN, RM_OUT };

/*----------------------
 | RM_EXIT_NONE .. RM_EXIT_MAYBE
 | Description: What the room object says about a direction. NONE means the
 |   property is absent (no exit at all) or is the two-byte refusal-message form;
 |   OPEN is the one-byte unconditional form; MAYBE is any longer form --
 |   conditional, door or routine -- which cannot be resolved without running
 |   story code.
 | Author: suinevere
 ----------------------*/
enum { RM_EXIT_NONE = 0, RM_EXIT_BLOCKED, RM_EXIT_OPEN, RM_EXIT_MAYBE };

/*----------------------
 | RoomModel
 | Description: One prompt's snapshot: the room object, each direction's state
 |   and destination, the objects present, and the objects carried.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short room;
    unsigned char  exits[RM_DIR_N];
    unsigned short dest[RM_DIR_N];
    unsigned short here[RM_HERE_MAX];
    int            nhere;
    unsigned short carried[RM_CARRIED_MAX];
    int            ncarried;
} RoomModel;

/*----------------------
 | room_model_bind
 | Description: Reads the story header, recovers the direction-word to
 |   property-number map from the dictionary, and gates on the recovered set
 |   being a contiguous run ending at 31 -- ZILCH allocates direction properties
 |   first, from the top down, so anything else means the story was not built
 |   that way and nothing here can be trusted. Call once per story load.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_len, g_obj, g_glob, g_dict, g_prop, g_available
 | Params: story -- the live story image; len -- its length in bytes
 | Returns: 1 if the model is available for this story, 0 otherwise
 ----------------------*/
int room_model_bind(const unsigned char *story, unsigned int len);

/*----------------------
 | room_model_available
 | Description: Whether the last bind produced a usable model.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_available
 | Params: N/A
 | Returns: 1 when available, 0 otherwise
 ----------------------*/
int room_model_available(void);

/*----------------------
 | room_model_dir_prop / room_model_dir_word
 | Description: The property number recovered for a direction (0 when the story
 |   has no such direction), and that direction's canonical word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prop
 | Params: dir -- one of the RM_* direction indices
 | Returns: the property number, or the word
 ----------------------*/
int room_model_dir_prop(int dir);
const char *room_model_dir_word(int dir);

/*----------------------
 | room_model_has_word
 | Description: Whether the story's dictionary accepts `text`, comparing only
 |   the first six characters because a v3 entry holds four text bytes, which is
 |   six Z-characters -- the parser cannot tell longer words apart either. Exists
 |   so the verb filter works on Hard, where no typeahead trie is built at all.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_dict
 | Params: text -- the word to look for
 | Returns: 1 if present, 0 otherwise
 ----------------------*/
int room_model_has_word(const char *text);

#ifdef __cplusplus
}
#endif
#endif /* ROOM_MODEL_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/engine/room_model.c`:

```c
/*----------------------
 | room_model.c
 | Description: The story decode described in room_model.h.
 | Author: suinevere
 | Dependencies: room_model.h
 ----------------------*/
#include "room_model.h"

/*----------------------
 | FL_DIR / PROP_MAX / RM_DIR_WORD
 | Description: The v3 dictionary's direction flag bit, the highest v3 property
 |   number, and the canonical spelling of each direction as Infocom's parsers
 |   hold it.
 | Author: suinevere
 ----------------------*/
#define FL_DIR   0x10
#define PROP_MAX 31

static const char *RM_DIR_WORD[RM_DIR_N] = {
    "north", "east", "west", "south", "ne", "nw", "se", "sw",
    "up", "down", "in", "out"
};

/*----------------------
 | g_story .. g_available
 | Description: The bound image and the header addresses read out of it, the
 |   per-direction property numbers, and whether the decode passed its gate.
 | Author: suinevere
 ----------------------*/
static const unsigned char *g_story;
static unsigned int g_len;
static unsigned int g_dict, g_obj, g_glob;
static int g_prop[RM_DIR_N];
static int g_available;

/*----------------------
 | rd16
 | Description: Reads a big-endian 16-bit word out of the bound image.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story
 | Params: a -- byte offset
 | Returns: the word
 ----------------------*/
static unsigned int rd16(unsigned int a) {
    return (unsigned int) ((g_story[a] << 8) | g_story[a + 1]);
}

/*----------------------
 | dict_entry_len / dict_count / dict_first
 | Description: The dictionary's entry size, entry count, and the offset of its
 |   first entry, all read from the header's dictionary block.
 | Author: suinevere
 | Dependencies: rd16
 | Globals: g_story, g_dict
 | Params: N/A
 | Returns: the respective value
 ----------------------*/
static unsigned int dict_first(void) {
    return g_dict + 1u + g_story[g_dict] + 3u;
}
static unsigned int dict_entry_len(void) {
    return g_story[g_dict + 1u + g_story[g_dict]];
}
static unsigned int dict_count(void) {
    return rd16(g_dict + 2u + g_story[g_dict]);
}

/*----------------------
 | decode_word
 | Description: Decodes an entry's four text bytes into up to six lowercase
 |   letters. Only the A0 alphabet is decoded; shift and abbreviation codes are
 |   rendered as spaces, which is enough because every word compared here is
 |   plain lowercase.
 | Author: suinevere
 | Dependencies: rd16
 | Globals: g_story
 | Params: off -- entry offset; out -- receives 7 bytes (6 chars + NUL)
 | Returns: N/A
 ----------------------*/
static void decode_word(unsigned int off, char *out) {
    static const char A0[32] =
        "      abcdefghijklmnopqrstuvwxyz";
    int p = 0, k;
    for (k = 0; k < 4; k += 2) {
        unsigned int x = rd16(off + (unsigned int) k);
        out[p++] = A0[(x >> 10) & 31];
        out[p++] = A0[(x >> 5) & 31];
        out[p++] = A0[x & 31];
    }
    out[6] = '\0';
    while (p > 0 && out[p - 1] == ' ') out[--p] = '\0';
}

/*----------------------
 | dir_prop_of
 | Description: The direction property number carried by a dictionary entry: of
 |   its data bytes, the unique one in 1..PROP_MAX. A word that is also an
 |   adjective or preposition carries that class's value too, which is why the
 |   byte is found by range rather than by offset. Zero or two candidates is a
 |   decode failure.
 | Author: suinevere
 | Dependencies: dict_entry_len
 | Globals: g_story
 | Params: off -- entry offset
 | Returns: the property number, or 0
 ----------------------*/
static int dir_prop_of(unsigned int off) {
    unsigned int elen = dict_entry_len();
    int found = 0, prop = 0;
    unsigned int i;
    for (i = 5; i < elen; i++) {
        unsigned char b = g_story[off + i];
        if (b >= 1 && b <= PROP_MAX) { prop = (int) b; found++; }
    }
    return (found == 1) ? prop : 0;
}

int room_model_bind(const unsigned char *story, unsigned int len) {
    int seen[PROP_MAX + 1];
    int i, top, run, cardinals;
    unsigned int n, k, elen, first;

    g_available = 0;
    for (i = 0; i < RM_DIR_N; i++) g_prop[i] = 0;
    g_story = story; g_len = len;
    if (story == 0 || len < 64u) return 0;

    g_dict = rd16(0x08);
    g_obj  = rd16(0x0a);
    g_glob = rd16(0x0c);
    if (g_dict == 0 || g_obj == 0 || g_glob == 0) return 0;
    if (g_dict + 4u >= len || g_obj + 64u >= len || g_glob + 2u >= len) return 0;
    if (g_dict + (unsigned int) g_story[g_dict] + 4u > len) return 0;

    for (i = 0; i <= PROP_MAX; i++) seen[i] = 0;

    elen  = dict_entry_len();
    n     = dict_count();
    first = dict_first();
    if (elen < 6u || elen > 16u || n == 0u || first + n * elen > len) return 0;

    for (k = 0; k < n; k++) {
        unsigned int off = first + k * elen;
        char text[8];
        int prop;
        if (!(g_story[off + 4] & FL_DIR)) continue;
        prop = dir_prop_of(off);
        if (prop == 0) continue;
        seen[prop] = 1;
        decode_word(off, text);
        for (i = 0; i < RM_DIR_N; i++) {
            const char *w = RM_DIR_WORD[i];
            int j = 0;
            while (w[j] && j < 6 && w[j] == text[j]) j++;
            if ((w[j] == '\0' || j == 6) && text[j] == '\0') g_prop[i] = prop;
        }
    }

    top = 0;
    for (i = PROP_MAX; i >= 1; i--) { if (seen[i]) { top = i; break; } }
    if (top != PROP_MAX) return 0;
    run = 0;
    for (i = PROP_MAX; i >= 1 && seen[i]; i--) run++;
    for (; i >= 1; i--) { if (seen[i]) return 0; }
    if (run < 4) return 0;

    cardinals = (g_prop[RM_N] != 0) + (g_prop[RM_S] != 0)
              + (g_prop[RM_E] != 0) + (g_prop[RM_W] != 0);
    if (cardinals < 4) return 0;

    g_available = 1;
    return 1;
}

int room_model_available(void) { return g_available; }

int room_model_dir_prop(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return 0;
    return g_prop[dir];
}

const char *room_model_dir_word(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return "";
    return RM_DIR_WORD[dir];
}

int room_model_has_word(const char *text) {
    unsigned int elen, n, k, first;
    if (!g_available || text == 0) return 0;
    elen = dict_entry_len(); n = dict_count(); first = dict_first();
    for (k = 0; k < n; k++) {
        char w[8];
        int j = 0;
        decode_word(first + k * elen, w);
        while (j < 6 && text[j] && w[j] && text[j] == w[j]) j++;
        if (w[j] == '\0' && (text[j] == '\0' || j == 6)) return 1;
    }
    return 0;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
  saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: PASS — `test_room_model ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/room_model.h saturn/src/engine/room_model.c \
        saturn/tests/test_room_model.c
git commit -m "Recover each direction's property number from the story dictionary and gate on a ZILCH layout."
```

---

### Task 3: Room model — exits

Walk the room object's property table and classify each direction.

**Files:**
- Modify: `saturn/src/engine/room_model.h`, `saturn/src/engine/room_model.c`
- Modify: `saturn/tests/test_room_model.c`

**Interfaces:**
- Consumes: Task 2's `room_model_bind`, `g_prop`, `RM_EXIT_*`, `RoomModel`.
- Produces: `void room_model_refresh_room(unsigned short room)`, `const RoomModel *room_model_get(void)`.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_room_model.c`, insert before the junk-header block:

```c
    /* Object 81 is "North of House": north, east, west, southeast and southwest
       are one-byte unconditional exits, and south is the two-byte form that only
       prints the boarded-windows refusal. */
    room_model_bind(g_story, g_len);
    room_model_refresh_room(81);
    {
        const RoomModel *m = room_model_get();
        assert(m->room == 81);
        assert(m->exits[RM_N]  == RM_EXIT_OPEN && m->dest[RM_N]  == 75);
        assert(m->exits[RM_E]  == RM_EXIT_OPEN && m->dest[RM_E]  == 79);
        assert(m->exits[RM_W]  == RM_EXIT_OPEN && m->dest[RM_W]  == 180);
        assert(m->exits[RM_SE] == RM_EXIT_OPEN && m->dest[RM_SE] == 79);
        assert(m->exits[RM_SW] == RM_EXIT_OPEN && m->dest[RM_SW] == 180);
        assert(m->exits[RM_S]  == RM_EXIT_BLOCKED);
        assert(m->exits[RM_NE] == RM_EXIT_NONE);
        assert(m->exits[RM_NW] == RM_EXIT_NONE);
        assert(m->exits[RM_UP] == RM_EXIT_NONE);
        assert(m->exits[RM_IN] == RM_EXIT_NONE);
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
  saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: FAIL — implicit declaration of `room_model_refresh_room` / `room_model_get`.

- [ ] **Step 3: Declare the new entry points**

In `saturn/src/engine/room_model.h`, before the closing `extern "C"`:

```c
/*----------------------
 | room_model_refresh_room
 | Description: Rebuilds the snapshot for a given room object -- the entry the
 |   host tests drive, and what room_model_refresh calls once it has read the
 |   room out of global 0.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_model
 | Params: room -- the room object number
 | Returns: N/A
 ----------------------*/
void room_model_refresh_room(unsigned short room);

/*----------------------
 | room_model_get
 | Description: The current snapshot. Valid but empty before the first refresh
 |   and whenever the model is unavailable.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_model
 | Params: N/A
 | Returns: the snapshot, never NULL
 ----------------------*/
const RoomModel *room_model_get(void);
```

- [ ] **Step 4: Implement the property walk**

Append to `saturn/src/engine/room_model.c`:

```c
/*----------------------
 | g_model
 | Description: The snapshot the last refresh produced.
 | Author: suinevere
 ----------------------*/
static RoomModel g_model;

/*----------------------
 | obj_entry / obj_props
 | Description: A v3 object's 9-byte table entry (four attribute bytes, then
 |   parent, sibling and child, then the two-byte property-table address), and
 |   the address of that object's first property -- past the short name, whose
 |   length in words is the property table's first byte.
 | Author: suinevere
 | Dependencies: rd16
 | Globals: g_obj, g_story
 | Params: id -- object number, 1-based
 | Returns: the respective offset
 ----------------------*/
static unsigned int obj_entry(unsigned short id) {
    return g_obj + 62u + ((unsigned int) id - 1u) * 9u;
}

/*----------------------
 | obj_valid
 | Description: Whether an object number's whole 9-byte table entry lies inside
 |   the image. Every walk of the object tree follows numbers read out of the
 |   story, so none of them can be trusted to name a real object.
 | Author: suinevere
 | Dependencies: obj_entry
 | Globals: g_available, g_len
 | Params: id -- object number, 1-based
 | Returns: 1 when the entry is readable, 0 otherwise
 ----------------------*/
static int obj_valid(unsigned short id) {
    if (!g_available || id == 0) return 0;
    return obj_entry(id) + 9u <= g_len;
}

static unsigned int obj_props(unsigned short id) {
    unsigned int t;
    if (!obj_valid(id)) return 0u;
    t = rd16(obj_entry(id) + 7u);
    if (t == 0u || t + 1u >= g_len) return 0u;
    return t + 1u + 2u * g_story[t];
}

/*----------------------
 | dir_of_prop
 | Description: Which direction index a property number belongs to, or -1 when
 |   it is not a direction property in this story.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_prop
 | Params: prop -- property number
 | Returns: the RM_* index, or -1
 ----------------------*/
static int dir_of_prop(int prop) {
    int i;
    for (i = 0; i < RM_DIR_N; i++) if (g_prop[i] == prop) return i;
    return -1;
}

void room_model_refresh_room(unsigned short room) {
    unsigned int a;
    int i;

    for (i = 0; i < RM_DIR_N; i++) { g_model.exits[i] = RM_EXIT_NONE; g_model.dest[i] = 0; }
    g_model.nhere = 0;
    g_model.ncarried = 0;
    g_model.room = room;
    if (!g_available || room == 0) return;
    if (obj_entry(room) + 9u > g_len) return;

    a = obj_props(room);
    if (a == 0u) return;
    while (a < g_len && g_story[a] != 0) {
        int size = (int) g_story[a];
        int prop = size & 31;
        int plen = (size >> 5) + 1;
        int dir;
        if (a + 1u + (unsigned int) plen > g_len) break;
        dir = dir_of_prop(prop);
        if (dir >= 0) {
            if (plen == 1) {
                g_model.exits[dir] = RM_EXIT_OPEN;
                g_model.dest[dir]  = g_story[a + 1u];
            } else if (plen == 2) {
                g_model.exits[dir] = RM_EXIT_BLOCKED;
            } else {
                g_model.exits[dir] = RM_EXIT_MAYBE;
            }
        }
        a += 1u + (unsigned int) plen;
    }
}

const RoomModel *room_model_get(void) { return &g_model; }
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
  saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: PASS — `test_room_model ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/room_model.h saturn/src/engine/room_model.c \
        saturn/tests/test_room_model.c
git commit -m "Classify a room's exits from its direction properties by data length."
```

---

### Task 4: Room model — contents, carried items, live refresh

Walk the object tree for what is here and what is held, and add the entry that reads global 0.

**Files:**
- Modify: `saturn/src/engine/room_model.h`, `saturn/src/engine/room_model.c`
- Modify: `saturn/tests/test_room_model.c`

**Interfaces:**
- Consumes: Task 3's `room_model_refresh_room`, `room_model_get`.
- Produces: `void room_model_refresh(void)`, `unsigned short room_model_player(void)`.

**Deliberately not produced: object short names.** The only Z-string decoder in
the tree, `typeahead_extract.c`'s `decode_at`, reads a file-static story pointer
that `build_typeahead_from_story` sets — and Hard builds no trie at all, so that
pointer may never have been set. `room_model` therefore reports *which objects
are present* and leaves the wording to the trie's on-screen vocabulary, or to
`room_model_has_word` on Hard. Do not add a second Z-string decoder here.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_room_model.c`, after the object-81 block:

```c
    /* Object 180 is "West of House". Its children are the door (181) and the
       mailbox (160), read straight from the object tree -- so they are known to
       be here without a word of text having been printed. */
    room_model_refresh_room(180);
    {
        const RoomModel *m = room_model_get();
        int saw_door = 0, saw_box = 0, i;
        assert(m->nhere == 2);
        for (i = 0; i < m->nhere; i++) {
            if (m->here[i] == 181) saw_door = 1;
            if (m->here[i] == 160) saw_box  = 1;
        }
        assert(saw_door == 1 && saw_box == 1);
    }

    /* Object 81 is "North of House" and holds nothing. */
    room_model_refresh_room(81);
    assert(room_model_get()->nhere == 0);

    /* The player is unknown until a room change lets the model intersect two
       rooms' child sets; nothing above depended on knowing it. */
    assert(room_model_player() == 0);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
  saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: FAIL — implicit declaration of `room_model_player`, and `nhere` still 0.

- [ ] **Step 3: Declare the new entry points**

In `saturn/src/engine/room_model.h`, before the closing `extern "C"`:

```c
/*----------------------
 | room_model_refresh
 | Description: Reads the current room out of global 0 -- which the v3
 |   specification defines as the room the status line names -- and rebuilds the
 |   snapshot for it. Call once per prompt.
 | Author: suinevere
 | Dependencies: room_model_refresh_room
 | Globals: g_glob, g_model
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_model_refresh(void);

/*----------------------
 | room_model_player
 | Description: The object the player inhabits, or 0 while it is still unknown.
 |   There is no specified way to find it, so it is identified by intersecting
 |   the child sets of two consecutive rooms -- only the player follows the
 |   player -- which converges on the first room change. Until then carried items
 |   are simply absent.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_player
 | Params: N/A
 | Returns: the object number, or 0
 ----------------------*/
unsigned short room_model_player(void);
```

- [ ] **Step 4: Implement the tree walk and the player heuristic**

Declare these statics **beside the existing ones near the top of the file**, not
at the bottom — `room_model_bind` clears them and `room_model_refresh_room`
updates them, and both are defined earlier in the file:

```c
/*----------------------
 | g_player / g_prev_room / g_prev_kids / g_prev_n
 | Description: The identified player object, and the previous room with its
 |   child set, kept so a room change can be intersected.
 | Author: suinevere
 ----------------------*/
static unsigned short g_player;
static unsigned short g_prev_room;
static unsigned short g_prev_kids[RM_HERE_MAX];
static int g_prev_n;

/*----------------------
 | obj_child / obj_sibling
 | Description: An object's first child and next sibling, from bytes 6 and 5 of
 |   its table entry.
 | Author: suinevere
 | Dependencies: obj_entry
 | Globals: g_story
 | Params: id -- object number
 | Returns: the related object number, or 0
 ----------------------*/
static unsigned short obj_child(unsigned short id) {
    if (!obj_valid(id)) return 0;
    return (unsigned short) g_story[obj_entry(id) + 6u];
}
static unsigned short obj_sibling(unsigned short id) {
    if (!obj_valid(id)) return 0;
    return (unsigned short) g_story[obj_entry(id) + 5u];
}

/*----------------------
 | collect_children
 | Description: Fills `out` with an object's children, up to `max`, returning
 |   how many were written. Bounded by max rather than by the chain so a corrupt
 |   sibling link cannot spin.
 | Author: suinevere
 | Dependencies: obj_child, obj_sibling
 | Globals: N/A
 | Params: parent -- the object to walk; out -- destination; max -- its capacity
 | Returns: the count written
 ----------------------*/
static int collect_children(unsigned short parent, unsigned short *out, int max) {
    unsigned short c = obj_child(parent);
    int n = 0;
    while (c != 0 && n < max) { out[n++] = c; c = obj_sibling(c); }
    return n;
}

unsigned short room_model_player(void) { return g_player; }

void room_model_refresh(void) {
    unsigned short room;
    if (!g_available) return;
    room = (unsigned short) rd16(g_glob);
    room_model_refresh_room(room);
}
```

Then extend `room_model_refresh_room`, immediately before its closing brace, so contents and carried items are filled and the player heuristic advances:

```c
    g_model.nhere = collect_children(room, g_model.here, RM_HERE_MAX);

    if (g_player == 0 && g_prev_room != 0 && g_prev_room != room) {
        int i, j, cand = 0, ncand = 0;
        for (i = 0; i < g_prev_n; i++)
            for (j = 0; j < g_model.nhere; j++)
                if (g_prev_kids[i] == g_model.here[j]) { cand = g_prev_kids[i]; ncand++; }
        if (ncand == 1) g_player = (unsigned short) cand;
    }
    g_prev_room = room;
    g_prev_n = g_model.nhere;
    { int i; for (i = 0; i < g_model.nhere; i++) g_prev_kids[i] = g_model.here[i]; }

    if (g_player != 0)
        g_model.ncarried = collect_children(g_player, g_model.carried, RM_CARRIED_MAX);
```

Also reset `g_player`, `g_prev_room` and `g_prev_n` to 0 at the top of `room_model_bind`, so switching games does not carry a stale player across.

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe \
  saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: PASS — `test_room_model ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine/room_model.h saturn/src/engine/room_model.c \
        saturn/tests/test_room_model.c
git commit -m "Read a room's contents and the player's carried items from the object tree."
```

---

### Task 5: Command panel — focus, slots, sentence assembly

**Files:**
- Create: `saturn/src/input/command_panel.h`, `saturn/src/input/command_panel.c`
- Create: `saturn/tests/test_command_panel.c`

**Interfaces:**
- Consumes: nothing (deliberately — the panel takes candidate lists from its caller).
- Produces: `CommandPanel`, `cp_reset`, `cp_focus`, `cp_move`, `cp_pick`, `cp_back`, and the `CP_BOX_*` / `CP_SLOT_*` enums.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_command_panel.c`:

```c
/*----------------------
 | test_command_panel.c
 | Description: Host test for the command panel's state machine: focus movement
 |   across the three modules, slot progression as a sentence fills, the
 |   preposition slot opening only when the caller says the grammar wants one,
 |   and Back unwinding a word at a time. Asserts the assembled command string,
 |   which is the panel's only output. No SRL or Saturn code is involved.
 | Author: suinevere
 | Dependencies: ../src/input/command_panel.h and command_panel.c, assert.h,
 |   string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
 |          saturn/tests/test_command_panel.c saturn/src/input/command_panel.c \
 |          && /tmp/tcp.exe
 ----------------------*/
#include "../src/input/command_panel.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    CommandPanel p;

    cp_reset(&p);
    assert(p.box == CP_BOX_WORD);
    assert(p.slot == CP_SLOT_VERB);
    assert(p.line_len == 0);

    cp_focus(&p, -1);
    assert(p.box == CP_BOX_TRAVEL);
    cp_focus(&p, -1);
    assert(p.box == CP_BOX_TRAVEL);
    cp_focus(&p, +1);
    cp_focus(&p, +1);
    assert(p.box == CP_BOX_CMD);
    cp_focus(&p, +1);
    assert(p.box == CP_BOX_CMD);

    /* Two-slot command: verb then noun, no preposition wanted. */
    cp_reset(&p);
    cp_pick(&p, "take", 0);
    assert(p.slot == CP_SLOT_NOUN);
    assert(strcmp(p.line, "take") == 0);
    cp_pick(&p, "lamp", 0);
    assert(p.slot == CP_SLOT_DONE);
    assert(strcmp(p.line, "take lamp") == 0);
    assert(p.submitted == 1);

    /* Four-slot command: the caller reports the grammar wants a preposition. */
    cp_reset(&p);
    cp_pick(&p, "put", 0);
    cp_pick(&p, "coffin", 1);
    assert(p.slot == CP_SLOT_PREP);
    assert(p.submitted == 0);
    cp_pick(&p, "in", 0);
    assert(p.slot == CP_SLOT_NOUN2);
    cp_pick(&p, "boat", 0);
    assert(strcmp(p.line, "put coffin in boat") == 0);
    assert(p.submitted == 1);

    /* Back unwinds one word and one slot at a time. */
    cp_reset(&p);
    cp_pick(&p, "put", 0);
    cp_pick(&p, "coffin", 1);
    cp_back(&p);
    assert(strcmp(p.line, "put") == 0);
    assert(p.slot == CP_SLOT_NOUN);
    cp_back(&p);
    assert(p.line_len == 0);
    assert(p.slot == CP_SLOT_VERB);
    assert(p.box == CP_BOX_WORD);
    cp_back(&p);
    assert(p.box == CP_BOX_TRAVEL);

    /* Travel submits a whole command in one pick, whatever slot was showing. */
    cp_reset(&p);
    cp_focus(&p, -1);
    assert(p.box == CP_BOX_TRAVEL);
    cp_pick(&p, "north", 0);
    assert(strcmp(p.line, "north") == 0);
    assert(p.slot == CP_SLOT_DONE);
    assert(p.submitted == 1);

    /* The cursor is clamped to the module it is walking, never wrapped. */
    cp_reset(&p);
    cp_move(&p, -1, 10);
    assert(p.cursor == 0);
    cp_move(&p, 4, 10);
    assert(p.cursor == 4);
    cp_move(&p, 99, 10);
    assert(p.cursor == 9);
    cp_move(&p, 1, 0);
    assert(p.cursor == 0);

    printf("test_command_panel ok\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
  saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: FAIL — `command_panel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/input/command_panel.h`:

```c
/*----------------------
 | command_panel.h
 | Description: The command panel's state: which of the three modules has focus,
 |   which sentence slot is being filled, where the cursor sits, which page of
 |   the word list is showing, and the command assembled so far. Pure logic --
 |   no rendering, no device polling, and no opinion about where candidate words
 |   come from; the caller supplies them already ordered. Implemented in
 |   command_panel.c.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef COMMAND_PANEL_H
#define COMMAND_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CP_WORD_COLS / CP_WORD_ROWS / CP_WORD_CELLS / CP_WORD_MAX / CP_LINE_MAX
 | Description: The word module's two columns over five rows, the cell count
 |   they make, the display width of one word (six characters plus its NUL --
 |   six is what a v3 dictionary entry distinguishes), and the assembled
 |   command's capacity, matching KB_INPUT_MAX.
 | Author: suinevere
 ----------------------*/
#define CP_WORD_COLS  2
#define CP_WORD_ROWS  5
#define CP_WORD_CELLS (CP_WORD_COLS * CP_WORD_ROWS)
#define CP_WORD_MAX   7
#define CP_LINE_MAX   64

/*----------------------
 | CP_BOX_TRAVEL / CP_BOX_WORD / CP_BOX_CMD / CP_BOX_N
 | Description: The three modules, left to right.
 | Author: suinevere
 ----------------------*/
enum { CP_BOX_TRAVEL = 0, CP_BOX_WORD, CP_BOX_CMD, CP_BOX_N };

/*----------------------
 | CP_SLOT_VERB .. CP_SLOT_DONE
 | Description: The sentence slots, in the order they fill. DONE means the
 |   command is complete and has been marked for submission.
 | Author: suinevere
 ----------------------*/
enum { CP_SLOT_VERB = 0, CP_SLOT_NOUN, CP_SLOT_PREP, CP_SLOT_NOUN2, CP_SLOT_DONE };

/*----------------------
 | CommandPanel
 | Description: One prompt's panel state.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int  box;
    int  slot;
    int  cursor;
    int  page;
    char line[CP_LINE_MAX];
    int  line_len;
    int  submitted;
} CommandPanel;

/*----------------------
 | cp_reset
 | Description: Clears the assembled command and returns focus to the word
 |   module at the verb slot, page zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state to clear
 | Returns: N/A
 ----------------------*/
void cp_reset(CommandPanel *p);

/*----------------------
 | cp_focus
 | Description: Moves focus one module left (-1) or right (+1), clamped at the
 |   ends rather than wrapping, and resets the cursor and page for the module
 |   arrived at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dir -- -1 or +1
 | Returns: N/A
 ----------------------*/
void cp_focus(CommandPanel *p, int dir);

/*----------------------
 | cp_move
 | Description: Steps the cursor within the focused module by `d`, clamped to
 |   0..count-1.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; d -- signed step; count -- entries in the module
 | Returns: N/A
 ----------------------*/
void cp_move(CommandPanel *p, int d, int count);

/*----------------------
 | cp_pick
 | Description: Appends `word` to the command, space-separated, and advances the
 |   slot. wants_prep is consulted only when leaving the noun slot: set, the
 |   preposition slot opens; clear, the command is complete. A pick made from the
 |   travel module completes immediately, since a direction is a whole command.
 |   Marks `submitted` when the command is complete.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the word picked; wants_prep -- 1 when the
 |   story's grammar says this verb takes a preposition
 | Returns: N/A
 ----------------------*/
void cp_pick(CommandPanel *p, const char *word, int wants_prep);

/*----------------------
 | cp_back
 | Description: Removes the last word from the command and steps the slot back
 |   one. From an empty command at the verb slot, moves focus to the travel
 |   module instead.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: N/A
 ----------------------*/
void cp_back(CommandPanel *p);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_PANEL_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/input/command_panel.c`:

```c
/*----------------------
 | command_panel.c
 | Description: The panel state machine described in command_panel.h.
 | Author: suinevere
 | Dependencies: command_panel.h
 ----------------------*/
#include "command_panel.h"

void cp_reset(CommandPanel *p) {
    p->box = CP_BOX_WORD;
    p->slot = CP_SLOT_VERB;
    p->cursor = 0;
    p->page = 0;
    p->line[0] = '\0';
    p->line_len = 0;
    p->submitted = 0;
}

void cp_focus(CommandPanel *p, int dir) {
    int b = p->box + dir;
    if (b < 0) b = 0;
    if (b >= CP_BOX_N) b = CP_BOX_N - 1;
    if (b != p->box) { p->box = b; p->cursor = 0; p->page = 0; }
}

void cp_move(CommandPanel *p, int d, int count) {
    int c = p->cursor + d;
    if (count <= 0) { p->cursor = 0; return; }
    if (c < 0) c = 0;
    if (c >= count) c = count - 1;
    p->cursor = c;
}

void cp_pick(CommandPanel *p, const char *word, int wants_prep) {
    int i = 0;
    if (word == 0 || word[0] == '\0') return;
    if (p->line_len > 0 && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = ' ';
    while (word[i] && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = word[i++];
    p->line[p->line_len] = '\0';

    if (p->box == CP_BOX_TRAVEL) { p->slot = CP_SLOT_DONE; p->submitted = 1; return; }

    switch (p->slot) {
        case CP_SLOT_VERB:  p->slot = CP_SLOT_NOUN; break;
        case CP_SLOT_NOUN:  p->slot = wants_prep ? CP_SLOT_PREP : CP_SLOT_DONE; break;
        case CP_SLOT_PREP:  p->slot = CP_SLOT_NOUN2; break;
        case CP_SLOT_NOUN2: p->slot = CP_SLOT_DONE; break;
        default: break;
    }
    p->cursor = 0;
    p->page = 0;
    if (p->slot == CP_SLOT_DONE) p->submitted = 1;
}

void cp_back(CommandPanel *p) {
    int i;
    if (p->line_len == 0) {
        if (p->box == CP_BOX_WORD) p->box = CP_BOX_TRAVEL;
        return;
    }
    for (i = p->line_len - 1; i >= 0; i--) if (p->line[i] == ' ') break;
    p->line_len = (i < 0) ? 0 : i;
    p->line[p->line_len] = '\0';
    if (p->slot > CP_SLOT_VERB) p->slot--;
    if (p->slot > CP_SLOT_NOUN && p->line_len == 0) p->slot = CP_SLOT_VERB;
    p->cursor = 0;
    p->page = 0;
    p->submitted = 0;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
  saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: PASS — `test_command_panel ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input/command_panel.h saturn/src/input/command_panel.c \
        saturn/tests/test_command_panel.c
git commit -m "Assemble commands from panel picks with grammar-gated preposition slots."
```

---

### Task 6: Command panel — word page fill

Ten cells, filled row-major, with `v more` claiming the last only when a further candidate exists.

**Files:**
- Modify: `saturn/src/input/command_panel.h`, `saturn/src/input/command_panel.c`
- Modify: `saturn/tests/test_command_panel.c`

**Interfaces:**
- Consumes: Task 5's `CP_WORD_CELLS`.
- Produces: `CommandWords`, `void cp_fill(const char *const *cands, int ncand, int page, CommandWords *out)`, `int cp_pages(int ncand)`.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_command_panel.c`, before the final `printf`:

```c
    {
        static const char *c[32];
        CommandWords w;
        int i;
        for (i = 0; i < 32; i++) c[i] = "word";

        /* Nine fits with a cell to spare and shows no marker. */
        cp_fill(c, 9, 0, &w);
        assert(w.n == 9 && w.more == 0);

        /* Ten fills every cell exactly and still shows no marker. */
        cp_fill(c, 10, 0, &w);
        assert(w.n == 10 && w.more == 0);

        /* Eleven cannot fit, so the last cell becomes the marker. */
        cp_fill(c, 11, 0, &w);
        assert(w.n == 9 && w.more == 1);
        cp_fill(c, 11, 1, &w);
        assert(w.n == 2 && w.more == 0);

        assert(cp_pages(9)  == 1);
        assert(cp_pages(10) == 1);
        assert(cp_pages(11) == 2);
        assert(cp_pages(19) == 3);
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
  saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: FAIL — unknown type name `CommandWords`.

- [ ] **Step 3: Declare the fill interface**

In `saturn/src/input/command_panel.h`, after the `CommandPanel` struct:

```c
/*----------------------
 | CommandWords
 | Description: One page of the word module: the words to draw in cell order and
 |   whether the last cell should instead read "v more".
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char *word[CP_WORD_CELLS];
    int         n;
    int         more;
} CommandWords;

/*----------------------
 | cp_fill
 | Description: Fills one page of the word module from an ordered candidate
 |   list. A list that fits uses every cell; one that does not gives its last
 |   cell to the "v more" marker and pages by CP_WORD_CELLS - 1, so no candidate
 |   is skipped by the marker.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: cands -- ordered candidates; ncand -- how many; page -- zero-based
 |   page; out -- receives the page
 | Returns: N/A
 ----------------------*/
void cp_fill(const char *const *cands, int ncand, int page, CommandWords *out);

/*----------------------
 | cp_pages
 | Description: How many pages a candidate list occupies.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ncand -- candidate count
 | Returns: the page count, at least 1
 ----------------------*/
int cp_pages(int ncand);
```

- [ ] **Step 4: Implement the fill**

Append to `saturn/src/input/command_panel.c`:

```c
int cp_pages(int ncand) {
    int stride;
    if (ncand <= CP_WORD_CELLS) return 1;
    stride = CP_WORD_CELLS - 1;
    return (ncand + stride - 1) / stride;
}

void cp_fill(const char *const *cands, int ncand, int page, CommandWords *out) {
    int stride, start, room, i;

    out->n = 0;
    out->more = 0;
    for (i = 0; i < CP_WORD_CELLS; i++) out->word[i] = 0;
    if (cands == 0 || ncand <= 0) return;

    if (ncand <= CP_WORD_CELLS) {
        for (i = 0; i < ncand; i++) out->word[i] = cands[i];
        out->n = ncand;
        return;
    }

    stride = CP_WORD_CELLS - 1;
    if (page < 0) page = 0;
    start = page * stride;
    if (start >= ncand) start = (cp_pages(ncand) - 1) * stride;

    room = ncand - start;
    if (room > stride) { room = stride; out->more = 1; }
    for (i = 0; i < room; i++) out->word[i] = cands[start + i];
    out->n = room;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
  saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: PASS — `test_command_panel ok`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input/command_panel.h saturn/src/input/command_panel.c \
        saturn/tests/test_command_panel.c
git commit -m "Page the word module ten cells at a time, spending the last cell on a marker only when needed."
```

---

### Task 7: Compass rose composition and the panel view

The rose rows are a pure function so they can be tested; the drawing around them is not.

**Files:**
- Create: `saturn/src/video/command_rose.h`, `saturn/src/video/command_rose.c`
- Create: `saturn/tests/test_command_rose.c`
- Create: `saturn/src/video/command_view.h`, `saturn/src/video/command_view.cxx`

**Interfaces:**
- Consumes: Task 3's `RM_EXIT_*` and `RM_*` direction indices; Task 6's `CommandWords`; Task 1's `text_print_hl`.
- Produces: `void cr_row(const unsigned char *exits, int row, char *out)` (writes 13 chars + NUL), `CR_ROWS`, `CR_COLS`, and `void render_command_panel(const CommandPanel &p, const RoomModel &m, const CommandWords &w)`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_command_rose.c`:

```c
/*----------------------
 | test_command_rose.c
 | Description: Host test for the compass rose's row composition. The rose is
 |   drawn from the decoded exit states alone, so an absent exit erases both its
 |   label and its spoke, a conditional one lowercases it, and the in and out
 |   words take the vertical spokes when they are available. Asserts the exact
 |   13-column rows, which is what keeps the module inside its 40-column strip.
 | Author: suinevere
 | Dependencies: ../src/video/command_rose.h and command_rose.c,
 |   ../src/engine/room_model.h, assert.h, string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
 |          saturn/tests/test_command_rose.c saturn/src/video/command_rose.c \
 |          && /tmp/tcr.exe
 |   The -I saturn/src/engine is needed because command_rose.c includes
 |   "room_model.h" unqualified, which the real build resolves through
 |   makefile:34's -I for every src subdirectory.
 ----------------------*/
#include "../src/video/command_rose.h"
#include "../src/engine/room_model.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    unsigned char e[RM_DIR_N];
    char row[CR_COLS + 1];
    int i;

    /* Nothing available: every row is blank but the centre marker. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    cr_row(e, 0, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "      +      ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "             ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "             ") == 0);

    /* North of House: n, e, w, se and sw open; s blocked; nothing else. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    e[RM_N] = e[RM_E] = e[RM_W] = e[RM_SE] = e[RM_SW] = RM_EXIT_OPEN;
    e[RM_S] = RM_EXIT_BLOCKED;
    cr_row(e, 0, row); assert(strcmp(row, "      N      ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "      |      ") == 0);
    cr_row(e, 2, row); assert(strcmp(row, "W --  +  -- E") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "   /     \\   ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "SW         SE") == 0);

    /* Up and down flank the poles; in and out take the vertical spokes. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    e[RM_N] = e[RM_S] = e[RM_UP] = e[RM_DOWN] = RM_EXIT_OPEN;
    e[RM_IN] = e[RM_OUT] = RM_EXIT_OPEN;
    cr_row(e, 0, row); assert(strcmp(row, "   ^  N  ^   ") == 0);
    cr_row(e, 1, row); assert(strcmp(row, "     IN      ") == 0);
    cr_row(e, 3, row); assert(strcmp(row, "     OUT     ") == 0);
    cr_row(e, 4, row); assert(strcmp(row, "   v  S  v   ") == 0);

    /* A conditional exit is lowercased rather than promised. */
    for (i = 0; i < RM_DIR_N; i++) e[i] = RM_EXIT_NONE;
    e[RM_NE] = RM_EXIT_MAYBE;
    cr_row(e, 0, row); assert(strcmp(row, "           ne") == 0);

    printf("test_command_rose ok\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
  saturn/tests/test_command_rose.c saturn/src/video/command_rose.c && /tmp/tcr.exe
```

Expected: FAIL — `command_rose.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/video/command_rose.h`:

```c
/*----------------------
 | command_rose.h
 | Description: Composition of the travel module's compass rose from decoded
 |   exit states. Five rows of thirteen columns, drawn so that an unavailable
 |   direction erases its own spoke as well as its label -- the rose is a map of
 |   the room rather than a menu of twelve buttons. Pure string building; the
 |   view prints what this returns and overprints the up and down markers in
 |   reverse video. Implemented in command_rose.c.
 | Author: suinevere
 | Dependencies: room_model.h (the RM_* direction indices and exit states)
 ----------------------*/
#ifndef COMMAND_ROSE_H
#define COMMAND_ROSE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CR_ROWS / CR_COLS / CR_UP_L / CR_UP_R
 | Description: The rose's shape, and the two inner columns the up and down
 |   markers occupy -- the view needs them to overprint those cells highlighted.
 | Author: suinevere
 ----------------------*/
#define CR_ROWS  5
#define CR_COLS 13
#define CR_UP_L  3
#define CR_UP_R  9

/*----------------------
 | cr_row
 | Description: Composes one rose row into `out` as exactly CR_COLS characters
 |   plus a NUL. An open direction prints uppercase, a conditional or
 |   undecodable one lowercase, and an absent or message-only one prints spaces
 |   along with its spoke. Available in and out replace the vertical spokes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: exits -- RM_DIR_N exit states; row -- 0..CR_ROWS-1; out -- receives
 |   CR_COLS + 1 bytes
 | Returns: N/A
 ----------------------*/
void cr_row(const unsigned char *exits, int row, char *out);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_ROSE_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/video/command_rose.c`:

```c
/*----------------------
 | command_rose.c
 | Description: The rose composition described in command_rose.h.
 | Author: suinevere
 | Dependencies: command_rose.h, room_model.h
 ----------------------*/
#include "command_rose.h"
#include "room_model.h"

/*----------------------
 | shown / put
 | Description: Whether a direction should appear at all (open or conditional),
 |   and writing a label into the row at a column, uppercased when the exit is
 |   decoded open and left lowercase when it is only possible.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- an RM_EXIT_* state; out -- the row; col -- inner column;
 |   text -- the label
 | Returns: shown returns 1 when the direction appears
 ----------------------*/
static int shown(unsigned char st) {
    return st == RM_EXIT_OPEN || st == RM_EXIT_MAYBE;
}

static void put(char *out, int col, const char *text, unsigned char st) {
    int i;
    if (!shown(st)) return;
    for (i = 0; text[i] && col + i < CR_COLS; i++) {
        char c = text[i];
        if (st == RM_EXIT_OPEN && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[col + i] = c;
    }
}

void cr_row(const unsigned char *exits, int row, char *out) {
    int i;
    for (i = 0; i < CR_COLS; i++) out[i] = ' ';
    out[CR_COLS] = '\0';

    switch (row) {
        case 0:
            put(out, 0,  "nw", exits[RM_NW]);
            put(out, 11, "ne", exits[RM_NE]);
            put(out, 6,  "n",  exits[RM_N]);
            if (shown(exits[RM_UP])) { out[CR_UP_L] = '^'; out[CR_UP_R] = '^'; }
            break;
        case 1:
            if (shown(exits[RM_NW])) out[3] = '\\';
            if (shown(exits[RM_NE])) out[9] = '/';
            if (shown(exits[RM_IN]))      put(out, 5, "in", exits[RM_IN]);
            else if (shown(exits[RM_N]))  out[6] = '|';
            break;
        case 2:
            put(out, 0,  "w", exits[RM_W]);
            put(out, 12, "e", exits[RM_E]);
            if (shown(exits[RM_W])) { out[2] = '-'; out[3] = '-'; }
            if (shown(exits[RM_E])) { out[9] = '-'; out[10] = '-'; }
            out[6] = '+';
            break;
        case 3:
            if (shown(exits[RM_SW])) out[3] = '/';
            if (shown(exits[RM_SE])) out[9] = '\\';
            if (shown(exits[RM_OUT]))     put(out, 5, "out", exits[RM_OUT]);
            else if (shown(exits[RM_S]))  out[6] = '|';
            break;
        case 4:
            put(out, 0,  "sw", exits[RM_SW]);
            put(out, 11, "se", exits[RM_SE]);
            put(out, 6,  "s",  exits[RM_S]);
            if (shown(exits[RM_DOWN])) { out[CR_UP_L] = 'v'; out[CR_UP_R] = 'v'; }
            break;
        default:
            break;
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe \
  saturn/tests/test_command_rose.c saturn/src/video/command_rose.c && /tmp/tcr.exe
```

Expected: PASS — `test_command_rose ok`.

- [ ] **Step 6: Write the view header**

Create `saturn/src/video/command_view.h`:

```c
/*----------------------
 | command_view.h
 | Description: The command panel's rendering and its pad-driven editor -- the
 |   command-mode counterparts of render_keyboard and typeahead_edit. Draws the
 |   three-module strip below the input line, and turns pad input into panel
 |   picks that end up in the same KeyboardState the on-screen keyboard fills.
 | Author: suinevere
 | Dependencies: command_panel.h, room_model.h, keyboard.h, typeahead.h, SRL
 ----------------------*/
#ifndef COMMAND_VIEW_H
#define COMMAND_VIEW_H

#include "command_panel.h"
#include "keyboard.h"
#include "room_model.h"
#include "saturn_keyboard.h"
#include "typeahead.h"

/*----------------------
 | CV_TRAVEL_X / CV_WORD_X / CV_CMD_X / CV_STRIP_ROWS
 | Description: The inner starting column of each module and the strip's content
 |   height. The strip is 1 + 13 + 1 + 15 + 1 + 8 + 1 = 40 columns, and seven
 |   rows: a blank under the top border, five of content, a blank above the
 |   bottom one.
 | Author: suinevere
 ----------------------*/
#define CV_TRAVEL_X    1
#define CV_WORD_X     15
#define CV_CMD_X      31
#define CV_STRIP_ROWS  7

/*----------------------
 | render_command_panel
 | Description: Draws the input line, the strip's borders and dividers, the
 |   compass rose, the word page, and the fixed command list, highlighting the
 |   focused module's selected entry and its border hint in reverse video.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h, console_view.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot; w -- the current word page
 | Returns: N/A
 ----------------------*/
void render_command_panel(const CommandPanel &p, const RoomModel &m, const CommandWords &w);

/*----------------------
 | command_edit
 | Description: One frame of command-mode input: L/R move focus, the D-pad walks
 |   the focused module or acts as the literal compass in travel, Accept picks,
 |   Back unwinds. A completed command is copied into `k` and submitted, so it
 |   leaves through the same path a typed one does.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: g_pad
 | Params: k -- keyboard state the command is written into; p -- panel state;
 |   m -- the room snapshot; root -- the typeahead trie for ranking, may be null;
 |   ke -- the decoded key event, consumed as handled; w -- (out) the word page
 |   the renderer should draw
 | Returns: N/A
 ----------------------*/
void command_edit(KeyboardState &k, CommandPanel &p, const RoomModel &m,
                  TrieNode *root, SaturnKeyEvent &ke, CommandWords &w);

#endif /* COMMAND_VIEW_H */
```

- [ ] **Step 7: Write the view implementation**

Create `saturn/src/video/command_view.cxx` with `render_command_panel` and `command_edit`. Draw with `text_print` and `text_print_hl` from `text_map.h`; the strip's top border is `+-------------+---------------+--------+`, its bottom border is `+---L/R box---+-A=pick B=bck--+-Z=kbd--+`, and rows carry `|` at columns 0, 14, 30 and 39.

The command module's five entries are fixed:

```c
/*----------------------
 | CV_CMD_ROW
 | Description: The fixed command module's entries, in display order. Every one
 |   routes to a mechanism that already exists, so none of them needs a new path
 |   to the interpreter.
 | Author: suinevere
 ----------------------*/
static const char *CV_CMD_ROW[5] = { "invent", "look", "save", "load", "quit" };
```

Until Task 9 gives it an overlay, `invent` submits the `inventory` command like
the other four — the module is complete and usable at the end of this task, it
just has no box to open yet.

The verb core, filtered against the story before display:

```c
/*----------------------
 | CV_VERB_CORE
 | Description: The curated verbs offered before the story's own, in likelihood
 |   order. Each is dropped unless the loaded story's dictionary accepts it, so a
 |   game that does not define "attack" never offers it.
 | Author: suinevere
 ----------------------*/
static const char *CV_VERB_CORE[16] = {
    "look", "take", "open", "read", "drop", "close", "push", "pull",
    "move", "attack", "climb", "enter", "throw", "turn", "eat", "drink"
};
```

`command_edit` reads the pad through the existing helpers in `input.h` — `pad_fired`, `face_button(FA_ACCEPT)`, `face_button(FA_BACK)` — and:

- `pad_fired(Button::L)` / `pad_fired(Button::R)` call `cp_focus(&p, -1)` / `cp_focus(&p, +1)`.
- In `CP_BOX_TRAVEL`, each D-pad direction maps to its `RM_*` index and calls `cp_pick(&p, room_model_dir_word(dir), 0)` directly; `Up`+`Right` together select `RM_NE` and so on for the diagonals.
- In `CP_BOX_WORD` and `CP_BOX_CMD`, `Up`/`Down` step by the column count and `Left`/`Right` by one, through `cp_move`.
- Accept calls `cp_pick` with `wants_prep` set when `root` is non-null and the picked verb has a `TYPE_PREP` transition in the trie (walk `next_words` looking for a target whose `type == TYPE_PREP`); `wants_prep` is 0 when `root` is null.
- Back calls `cp_back`.
- On `p.submitted`, copy `p.line` into `k.input`, set `k.input_len`, `k.cursor` and `k.submitted`, then `cp_reset(&p)`.

Candidate sourcing for the word module, by slot:

- `CP_SLOT_VERB` — `CV_VERB_CORE` entries passing `room_model_has_word` (or `find_exact_word(root, ...)` when the model is unavailable), then the trie's remaining verbs.
- `CP_SLOT_NOUN` / `CP_SLOT_NOUN2` — object names from `m.here` and `m.carried`, then on-screen vocabulary from the trie.
- `CP_SLOT_PREP` — trie words with `type == TYPE_PREP`.

Truncate every candidate to six characters when writing it into `CommandWords`.

**Ordering follows `g_difficulty`**, and only the ordering does — the same
candidates appear at every setting:

| `g_difficulty` | word order | rose |
|---|---|---|
| `DIFF_EASY` | solution-overlay links first (a `NextWordLink` with `solution == 1`), then the rest | as decoded, plus the walkthrough's next direction marked |
| `DIFF_MEDIUM` | trie weight, which already folds in grammar and the on-screen boost | as decoded |
| `DIFF_HARD` | alphabetical — there is no trie to rank with | every direction lowercase, so no exit is revealed |

Hard is not a special case in the sourcing code: `ensure_typeahead` returns
early there, so `root` arrives null and the alphabetical path is simply what
remains. The rose flattening on Hard *is* deliberate and must be written — those
decoded exits are exactly the guidance Hard exists to withhold.

- [ ] **Step 8: Syntax-check the Saturn units**

```bash
sh saturn/syntax-check.sh src/video/command_view.cxx
```

Expected: both configurations clean.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/command_rose.h saturn/src/video/command_rose.c \
        saturn/tests/test_command_rose.c saturn/src/video/command_view.h \
        saturn/src/video/command_view.cxx
git commit -m "Draw the command strip and compose its compass rose from decoded exits."
```

---

### Task 8: Dispatch, toggle button, and persistence

Wire the panel into the prompt, give it a button, and remember the preference.

**Files:**
- Modify: `saturn/src/engine/app_state.h`, `saturn/src/engine/app_state.cxx`
- Modify: `saturn/src/input/input.h`, `saturn/src/input/input.cxx`
- Modify: `saturn/src/engine/saturn_glue.cxx`
- Modify: `saturn/src/video/console_view.cxx:60-63`
- Modify: `saturn/src/menu/options.cxx` (`options_load`, `options_save`)
- Modify: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: Tasks 1-7 in full.
- Produces: `g_cmd_iface`, `g_cmd_mode`, `g_toggle_btn`, `bool mode_toggle_fired(void)`.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_display.c`, beside the existing `test_five_is_not_a_display_sentinel`, add the same guard for the new gameplay sentinel:

```c
/* MOJOOPTS' gameplay block grew a second byte and took a new sentinel to say so.
   Seven has to be as safe as five was: a blob written before the interface byte
   existed has its display sentinel where the new gameplay sentinel now sits, so
   seven must never be a value display_encode can write. */
static void test_seven_is_not_a_display_sentinel(void) {
    DisplayState d;
    uint8_t blob[DISP_BLOB_BYTES];
    int i;
    display_defaults(&d);
    for (i = 0; i < DISP_PAL_N; i++) {
        d.palette = i;
        display_encode(&d, blob);
        assert(blob[0] != 7);
        assert(blob[0] != 5);
    }
    printf("  seven is not a display sentinel: ok\n");
}
```

Register the call in `main()` alongside the existing sentinel test.

- [ ] **Step 2: Run test to verify it fails**

Use the build line already in `saturn/tests/test_display.c`'s header. Expected: PASS. This is a guard, not a red test — it fails only if `display_encode` is ever changed to emit 5 or 7, which would make an old MOJOOPTS blob decode as a gameplay block. Run it now so the guard is known-good before the writer changes.

- [ ] **Step 3: Add the globals**

In `saturn/src/engine/app_state.h`, after `g_verb_pending`:

```c
/* Which input interface a gamepad gets (IFACE_KEYBOARD / IFACE_PANEL). */
enum { IFACE_KEYBOARD = 0, IFACE_PANEL = 1 };

// Interface a gamepad starts a game in; persisted in MOJOOPTS and set on the
// Options > Gameplay page. Defaults to IFACE_PANEL.
extern int g_cmd_iface;

// Interface in use right now, seeded from g_cmd_iface when a game starts and
// flipped by the toggle button. Not persisted -- a tap is for this session.
extern int g_cmd_mode;

// Which shift button carries the interface toggle: 0 = Z, 1 = Y. Persisted in
// MOJOOPTS and set on the Options > Controller > Configure page.
extern int g_toggle_btn;
```

Define them in `saturn/src/engine/app_state.cxx` with header blocks matching the neighbours: `int g_cmd_iface = IFACE_PANEL; int g_cmd_mode = IFACE_PANEL; int g_toggle_btn = 0;`

- [ ] **Step 4: Add tap detection**

In `saturn/src/input/input.cxx`, beside `caps_combo_fired`:

```c
/*----------------------
 | mode_toggle_fired
 | Description: Reports a tap of the toggle button -- pressed and released with
 |   no direction or shoulder held in between. Y and Z do nothing on their own
 |   today, they only shift the chord slots, so a tap is free to claim; a press
 |   that fires a chord is marked spent and never reports.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pad, g_toggle_btn
 | Params: N/A
 | Returns: true on the frame a clean tap completes
 ----------------------*/
bool mode_toggle_fired(void) {
    static bool was = false;
    static bool spent = false;
    Button b = (g_toggle_btn == 1) ? Button::Y : Button::Z;
    bool now = g_pad->IsHeld(b);
    bool other = g_pad->IsHeld(Button::Up) || g_pad->IsHeld(Button::Down) ||
                 g_pad->IsHeld(Button::Left) || g_pad->IsHeld(Button::Right) ||
                 g_pad->IsHeld(Button::L) || g_pad->IsHeld(Button::R);
    bool fired = false;
    if (now && other) spent = true;
    if (was && !now) { fired = !spent; spent = false; }
    was = now;
    return fired;
}
```

Declare it in `saturn/src/input/input.h` with a matching header block.

- [ ] **Step 5: Give the panel its console rows**

In `saturn/src/video/console_view.cxx`, replace `console_height` (lines 60-63):

```c
int console_height(void) {
    int avail = SCREEN_ROWS - TOP_MARGIN;
    if (!g_kbd_visible) return avail - 1;
    if (g_cmd_mode == IFACE_PANEL) return avail - (1 + CV_STRIP_ROWS + 2);
    return avail - (1 + KB_ROWS + 1);
}
```

Add `#include "app_state.h"` and `#include "command_view.h"` if they are not already included, and update the function's header block to name the third case.

- [ ] **Step 6: Dispatch from the prompt**

In `saturn/src/engine/saturn_glue.cxx`, add `#include "room_model.h"` and `#include "command_view.h"`, and:

- In `ensure_typeahead`, after the trie is built, call `room_model_bind(story, len)`.
- In `saturn_readline`, immediately after `typeahead_scan_screen(g_typeahead_root)` at line 336, call `room_model_refresh()`.
- Declare `static CommandPanel cpanel;` beside `static KeyboardState k;` and `cp_reset(&cpanel)` where `k` is cleared.
- Replace the editor call at lines 426-431 with:

```c
        if (g_kbd_visible && mode_toggle_fired())
            g_cmd_mode = (g_cmd_mode == IFACE_PANEL) ? IFACE_KEYBOARD : IFACE_PANEL;

        if (g_kbd_visible && g_cmd_mode == IFACE_PANEL) {
            CommandWords cw;
            command_edit(k, cpanel, *room_model_get(), g_typeahead_root, ke, cw);
            pad_scroll_update();
            render_console();
            render_command_panel(cpanel, *room_model_get(), cw);
        } else {
            DictionaryWord* selected; int cw_len;
            typeahead_edit(k, g_typeahead_root, sug_index, sug_last, ke, pad, selected, cw_len);
            pad_scroll_update();
            render_console();
            render_keyboard(k, selected, cw_len);
        }
```

- In `main.cxx`, where a game starts, set `g_cmd_mode = g_cmd_iface;`.

- [ ] **Step 7: Persist the preference**

In `saturn/src/menu/options.cxx`, in `options_save`, replace the two gameplay bytes with three:

```c
    buf[n++] = 7;                                 // gameplay-block sentinel, v2
    buf[n++] = (uint8_t) g_verbosity;             // VERB_*
    buf[n++] = (uint8_t) ((g_cmd_iface & 1) | ((g_toggle_btn & 1) << 1));
```

In `options_load`, accept either form:

```c
    int gp = s + 3, dsp = gp;
    if (gp + 1 < (int) sizeof(buf) && buf[gp] == 5) {
        if (buf[gp + 1] <= VERB_VERBOSE) g_verbosity = buf[gp + 1];
        dsp = gp + 2;
    } else if (gp + 2 < (int) sizeof(buf) && buf[gp] == 7) {
        if (buf[gp + 1] <= VERB_VERBOSE) g_verbosity = buf[gp + 1];
        g_cmd_iface  = buf[gp + 2] & 1;
        g_toggle_btn = (buf[gp + 2] >> 1) & 1;
        dsp = gp + 3;
    }
```

Update both functions' header blocks to describe the new block, and extend the existing sentinel comment to say why 7 was chosen.

- [ ] **Step 8: Add the option rows**

On the Options > Gameplay page in `saturn/src/menu/options.cxx`, add a row cycling `g_cmd_iface` between "Keyboard" and "Command Panel", following the neighbouring `g_verbosity` row's pattern. On the Options > Controller > Configure page in `saturn/src/menu/menu_pages.cxx`, add a row cycling `g_toggle_btn` between "Z" and "Y", labelled with what it toggles.

- [ ] **Step 9: Run every host test**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tgi.exe saturn/tests/test_glyph_invert.c saturn/src/video/glyph_invert.c && /tmp/tgi.exe
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
gcc -std=c11 -Wall -Wextra -I saturn/src/engine -o /tmp/tcr.exe saturn/tests/test_command_rose.c saturn/src/video/command_rose.c && /tmp/tcr.exe
```

Then `test_display.c` using its own header build line. Expected: all print `ok`.

- [ ] **Step 10: Syntax-check every touched Saturn unit**

```bash
sh saturn/syntax-check.sh src/engine/saturn_glue.cxx src/video/console_view.cxx \
   src/menu/options.cxx src/menu/menu_pages.cxx src/input/input.cxx \
   src/video/command_view.cxx src/main.cxx
NETBIN=1 sh saturn/syntax-check.sh src/main_netbin.cxx src/net/online.cxx
```

Expected: both configurations clean. The netbin check matters because the panel must not reach that build.

- [ ] **Step 11: Commit**

```bash
git add saturn/src/engine/app_state.h saturn/src/engine/app_state.cxx \
        saturn/src/input/input.h saturn/src/input/input.cxx \
        saturn/src/engine/saturn_glue.cxx saturn/src/video/console_view.cxx \
        saturn/src/menu/options.cxx saturn/src/menu/menu_pages.cxx \
        saturn/src/main.cxx saturn/tests/test_display.c
git commit -m "Swap between the command panel and the on-screen keyboard on a shift-button tap, and remember the choice."
```

- [ ] **Step 12: Hand back for a hardware run**

The Saturn build and the emulator are the user's. Report which units changed and that both syntax-check configurations are clean, and ask for a `./compile.bat` run. On hardware, confirm: the strip draws at 40 columns with no torn rows; the rose matches the room; a tap swaps interfaces; a picked command echoes and reaches the parser; `save`, `load` and `quit` behave exactly as their F-keys do.

---

### Task 9: Inventory overlay

`invent` opens a smaller box across all three modules, listing what is carried.

**Files:**
- Modify: `saturn/src/input/command_panel.h`, `saturn/src/input/command_panel.c`
- Modify: `saturn/tests/test_command_panel.c`
- Modify: `saturn/src/video/command_view.cxx`

**Interfaces:**
- Consumes: Task 5's `CommandPanel` and `CP_SLOT_*`; Task 4's `RoomModel.carried`.
- Produces: `void cp_overlay_open(CommandPanel *p)`, `void cp_overlay_close(CommandPanel *p)`, `int cp_overlay_takes_noun(const CommandPanel *p)`, and the `overlay` field on `CommandPanel`.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_command_panel.c`, before the final `printf`:

```c
    /* The overlay fills a noun slot, but is a viewer only when a verb is what
       the panel is waiting for -- picking a held object cannot start a
       sentence. */
    cp_reset(&p);
    cp_overlay_open(&p);
    assert(p.overlay == 1);
    assert(cp_overlay_takes_noun(&p) == 0);
    cp_overlay_close(&p);
    assert(p.overlay == 0);
    assert(p.line_len == 0);

    cp_pick(&p, "take", 0);
    assert(p.slot == CP_SLOT_NOUN);
    cp_overlay_open(&p);
    assert(cp_overlay_takes_noun(&p) == 1);
    cp_pick(&p, "lamp", 0);
    assert(strcmp(p.line, "take lamp") == 0);
    assert(p.overlay == 0);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
  saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: FAIL — `CommandPanel` has no member `overlay`.

- [ ] **Step 3: Add the field and declarations**

In `saturn/src/input/command_panel.h`, add `int overlay;` to `CommandPanel` (documented in its header block as "1 while the inventory overlay is up"), and declare:

```c
/*----------------------
 | cp_overlay_open / cp_overlay_close / cp_overlay_takes_noun
 | Description: Raises and lowers the inventory overlay, and reports whether a
 |   pick made from it would land somewhere -- true only while the panel is
 |   waiting for a noun. With a verb slot active the overlay is a viewer, since
 |   a held object cannot start a sentence.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: cp_overlay_takes_noun returns 1 when a pick would fill a slot
 ----------------------*/
void cp_overlay_open(CommandPanel *p);
void cp_overlay_close(CommandPanel *p);
int  cp_overlay_takes_noun(const CommandPanel *p);
```

- [ ] **Step 4: Implement**

In `saturn/src/input/command_panel.c`, set `p->overlay = 0` in `cp_reset`, close the overlay at the end of `cp_pick`, and add:

```c
void cp_overlay_open(CommandPanel *p) { p->overlay = 1; p->cursor = 0; }

void cp_overlay_close(CommandPanel *p) { p->overlay = 0; p->cursor = 0; }

int cp_overlay_takes_noun(const CommandPanel *p) {
    return p->overlay && (p->slot == CP_SLOT_NOUN || p->slot == CP_SLOT_NOUN2);
}
```

Guard the top of `cp_pick` so a pick made from a viewer-only overlay is dropped rather than appended:

```c
    if (p->overlay && !cp_overlay_takes_noun(p)) { cp_overlay_close(p); return; }
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe \
  saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: PASS — `test_command_panel ok`.

- [ ] **Step 6: Draw and drive the overlay**

In `saturn/src/video/command_view.cxx`, when `p.overlay` is set, draw a bordered box 34 columns wide starting at column 2, over the strip's rows, listing `m.carried` one per row with the selected entry in reverse video. While it is up, the D-pad walks the list, Accept calls `cp_pick`, and Back calls `cp_overlay_close`. Selecting `invent` in the command module calls `cp_overlay_open`.

When `room_model_available()` is false there is no carried set to show, so `invent` submits the `inventory` command through `cp_pick` instead of opening anything.

- [ ] **Step 7: Syntax-check and commit**

```bash
sh saturn/syntax-check.sh src/video/command_view.cxx
git add saturn/src/input/command_panel.h saturn/src/input/command_panel.c \
        saturn/tests/test_command_panel.c saturn/src/video/command_view.cxx
git commit -m "Open the carried set as an overlay that fills a noun slot and views only when a verb is wanted."
```

---

### Task 10: Dictionary enumeration and Hard ordering

Added after Task 7 surfaced that the noun column has no source on Hard, where
no trie is built. Without this the column is permanently blank at that setting.

**Files:**
- Modify: `saturn/src/engine/room_model.h`, `saturn/src/engine/room_model.c`
- Modify: `saturn/tests/test_room_model.c`
- Modify: `saturn/src/video/command_view.cxx`

**Interfaces:**
- Consumes: Task 2's dictionary walk (`dict_first`, `dict_entry_len`, `dict_count`, `decode_word`, the `FL_*` flag bits).
- Produces: `int room_model_dict_count(void)` and `int room_model_dict_word(int index, char *out, int max, unsigned char *flags_out)`.

- [ ] **Step 1: Write the failing test**

In `saturn/tests/test_room_model.c`, before the junk-header block:

```c
    room_model_bind(g_story, g_len);
    {
        int n = room_model_dict_count();
        char w[8];
        unsigned char fl;
        int found_lamp = 0, found_open = 0, i;
        assert(n == 697);
        for (i = 0; i < n; i++) {
            assert(room_model_dict_word(i, w, (int) sizeof w, &fl) == 1);
            if (strcmp(w, "lamp") == 0 && (fl & 0x80) != 0) found_lamp = 1;
            if (strcmp(w, "open") == 0 && (fl & 0x40) != 0) found_open = 1;
        }
        assert(found_lamp == 1);
        assert(found_open == 1);
        assert(room_model_dict_word(-1, w, (int) sizeof w, &fl) == 0);
        assert(room_model_dict_word(n, w, (int) sizeof w, &fl) == 0);
    }
```

Add `#include <string.h>` to the test if it is not already present.

- [ ] **Step 2: Run test to verify it fails**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: FAIL — implicit declaration of `room_model_dict_count`.

- [ ] **Step 3: Declare the enumerator**

In `saturn/src/engine/room_model.h`, before the closing `extern "C"`:

```c
/*----------------------
 | room_model_dict_count / room_model_dict_word
 | Description: Enumerate the story's own dictionary. count is how many entries
 |   it holds; word copies entry `index`'s text (six characters at most, which
 |   is all a v3 entry distinguishes) and its part-of-speech flag byte. This is
 |   the vocabulary source on Hard, where no typeahead trie is built at all and
 |   the panel would otherwise have no words to offer.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_story, g_dict, g_available
 | Params: index -- entry index; out -- receives the text; max -- its capacity;
 |   flags_out -- receives the flag byte, may be null
 | Returns: count returns the entry count (0 when unavailable); word returns 1
 |   on success, 0 when unavailable or the index is out of range
 ----------------------*/
int room_model_dict_count(void);
int room_model_dict_word(int index, char *out, int max, unsigned char *flags_out);
```

- [ ] **Step 4: Implement**

Append to `saturn/src/engine/room_model.c`, with full header blocks on both definitions:

```c
int room_model_dict_count(void) {
    if (!g_available) return 0;
    return (int) dict_count();
}

int room_model_dict_word(int index, char *out, int max, unsigned char *flags_out) {
    unsigned int off;
    char w[8];
    int i;
    if (max > 0) out[0] = '\0';
    if (!g_available || out == 0 || max <= 0) return 0;
    if (index < 0 || index >= (int) dict_count()) return 0;
    off = dict_first() + (unsigned int) index * dict_entry_len();
    decode_word(off, w);
    for (i = 0; i < max - 1 && w[i]; i++) out[i] = w[i];
    out[i] = '\0';
    if (flags_out != 0) *flags_out = g_story[off + 4];
    return 1;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/trm.exe saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm.exe
```

Expected: PASS — `test_room_model ok`.

- [ ] **Step 6: Source Hard's words from the enumerator**

In `saturn/src/video/command_view.cxx`, where candidates are gathered:

- **Verbs, every difficulty:** `CV_VERB_CORE` entries that pass `room_model_has_word` lead the list, in the order they are declared. Only what follows them changes with difficulty — trie-ranked on Easy and Medium, dictionary order on Hard. Do not sort the core alphabetically at any setting.
- **Nouns on Hard** (`root == nullptr`): walk `room_model_dict_count()` / `room_model_dict_word()`, keep entries whose flag byte has `0x80` (noun) set, and prefer those naming objects the room model reports present before the rest of the dictionary.
- Truncate every candidate to six characters, as elsewhere.

- [ ] **Step 7: Syntax-check and commit**

```bash
sh saturn/syntax-check.sh src/video/command_view.cxx
git add saturn/src/engine/room_model.h saturn/src/engine/room_model.c \
        saturn/tests/test_room_model.c saturn/src/video/command_view.cxx
git commit -m "Enumerate the story dictionary so the panel still offers words with no trie built."
```

---

### Task 11: Full-length word recovery for display

Added after Task 9 surfaced that object synonyms are the dictionary's truncated
six-character forms, so the panel displays `mailbo` where a player expects
`mailbox`.

**Files:**
- Modify: `saturn/src/engine/room_model.h`, `saturn/src/engine/room_model.c`
- Modify: `saturn/tests/test_room_model.c`
- Modify: `saturn/src/video/command_view.cxx`

**The precedent to follow, not reinvent.** `typeahead_extract.c` already solves
this for the trie — see its "full-word recovery" pass, which replaces a
six-character dictionary form with a longer object-name token sharing the same
first six characters. Read that code before writing any of this. The reason it
cannot simply be called is the one recorded in the spec: its decoder reads a
file-static story pointer that the trie builder sets, and Hard never builds a
trie. The *approach* transfers; the code does not.

**What this task adds:** a decode of object short names in `room_model`,
used only to find a longer spelling for a truncated dictionary word. The
submitted word must remain the dictionary form — the parser distinguishes six
characters and nothing more, so the recovered spelling is for display only.

- Decode an object's short name (the text at its property table, whose length
  in 2-byte words is that table's first byte). This needs the A0/A1/A2 shift
  alphabets, the abbreviation table at header offset 0x18, and the 10-bit ZSCII
  escape — all of which `typeahead_extract.c`'s decoder already handles.
- For a truncated word, prefer a token from the object's short name whose first
  six characters match it. `mailbo` + short name "small mailbox" -> `mailbox`.
- Where no longer form exists, keep the six-character word unchanged.
- Six characters remains the display column width, so a recovered word longer
  than six is still truncated when drawn — the recovery matters for the ones
  that fit, and for a later widening of the column if that ever happens.

**Bounds:** every address here is derived from story bytes. This module has
produced three out-of-bounds findings already; bound the short-name read
against `g_len` before decoding, and cap the decode at its output buffer.

**Tests:** assert against the real `saturn/zork1.dat` that object 160's word
recovers to `mailbox` from `mailbo`, and that an object whose short name offers
no longer match keeps its dictionary form. Assert a malformed short-name
pointer is refused rather than decoded.

---

## Notes for the implementer

**Why `room_model` refuses rather than guesses.** The direction-property convention is ZILCH's, not the Z-machine specification's. An Inform-compiled v3 story has no direction properties at all, so `room_model_bind` returns 0 and everything downstream takes the degraded path: verbs filter against the trie, nouns come from on-screen vocabulary, and the rose renders every direction lowercase and fully pressable. That path is also what online play uses, since `online.cxx` frees the story bytes after building its trie and the game state lives on the server. Never let an undecodable story hide a direction — a hidden exit is an unwinnable game.

**Why the panel writes into `KeyboardState`.** Everything downstream of the prompt — echo, history, `g_output_start`, the reboot and quit intercepts, the F-key save and restore shortcuts, the trailing `\n` the interpreter expects — is reached by exactly one path. The panel is an alternative way to fill the input line, not a second way to reach the parser.

**Six characters is not a compromise.** A v3 dictionary entry holds four text bytes, which is six Z-characters. `mailbo` and `mailbox` are the same word to the parser, so truncating a display column to six loses no distinction the game can make.
