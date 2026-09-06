# Sentence Shape and Word Tabs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive the command panel's sentence slots from each verb's own grammar rows in the story file, and put a four-tab strip (Verb / Noun / Prep / A-Z) above the word list so the player can overrule the guess.

**Architecture:** A new `sentence_shape` module copies each verb's 8-byte syntax rows out of the v3 grammar table when the typeahead trie is built, and answers one question -- given the words picked so far, what kind of word comes next. `command_panel.c` stops walking a fixed slot enum and takes that kind from its caller. `command_view.cxx` asks the shape module, uses the answer to choose a default tab, and dispatches its existing candidate sourcing on the tab rather than on the slot.

**Tech Stack:** C11 (device sources are built by the SH-2 GCC toolchain; every test here is a host build with `gcc`/`cc`), Python 3 + pytest for the test harnesses that compile and run those host builds.

**Spec:** `docs/superpowers/specs/2026-09-06-sentence-shape-and-word-tabs-design.md`

## Global Constraints

- **Never run `saturn/compile.bat` and never run the emulator.** The user runs all device builds. Every task in this plan is verified by a host `gcc` build of the specific sources under test. Where a task needs a device build number (Task 1, Task 9), it stops and asks the user to build.
- **Netbin budget: 274,432 bytes usable, image is 272,688 at `8dbdc6b`, so 1,744 bytes of slack.** `saturn/tests/test_netbin_budget.py` enforces a 300 KiB ceiling and a 32 KiB headroom floor. Task 1 measures before anything is written and Task 9 measures after; Task 9 carries the fallback if it does not fit.
- **HWRAM C heap is roughly 194 KB**, not a megabyte (`saturn/tests/test_hwram_budget.py`). The shape table is about 3.6 KB of it.
- **Comment style (`CLAUDE.md`):** every file, function and constant gets the `/*---- | name | Description: ... | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----*/` header block, using `N/A` for fields that do not apply. Tests and generated files get a file header only. **No comments inside function bodies** except where the existing file already does it -- `command_panel.c` and `command_view.cxx` both carry in-body comments, so matching them there is correct; new files get none.
- **Commit style (`CLAUDE.md`):** one sentence, no body, no bullets, no trailers, and no mention of Claude, AI, or the session. The commit messages in this plan are written to that rule -- use them as given.
- **`command_panel.h` declares `Dependencies: none` and must keep declaring it.** It is pure state logic: no typeahead include, no grammar knowledge, no opinion about where words come from. Every grammar answer arrives as a parameter from `command_view.cxx`.
- **v3 only.** `build_typeahead_from_story` returns immediately unless `story[0] == 3`; the shape module inherits that.

---

### Task 1: Measure the baseline

No code. This task exists because two numbers decide whether later tasks land as designed, and both are properties of a build only the user can run.

**Files:**
- Modify: none

**Interfaces:**
- Consumes: nothing
- Produces: two recorded numbers -- the netbin image size and the HWRAM heap size at the branch point -- quoted in Task 9

- [ ] **Step 1: Ask the user for a clean build**

Post exactly this and wait:

> Before I touch anything I need a baseline from a build, which I do not run. Please run `saturn/compile.bat` (CD build) and `saturn/compile.bat NETBIN=1`, then say done. I need `BuildDrop/zaturn.netbin`'s size and the heap number the budget tests read off the link map.

- [ ] **Step 2: Read both numbers back**

Run:

```bash
python -m pytest saturn/tests/test_netbin_budget.py saturn/tests/test_hwram_budget.py -q
ls -l saturn/BuildDrop/zaturn.netbin
```

Expected: both pass. If `test_the_image_leaves_room_to_grow` already fails, stop and report it -- this plan cannot add bytes to an image that is already over, and Task 9's fallback becomes Task 2's starting assumption instead.

- [ ] **Step 3: Record them**

Write the two numbers into this plan file under this task as a line reading `Baseline: netbin <N> bytes, heap <M> bytes, measured <date>`. No commit -- the plan file is committed once at the end of Task 9 with the closing numbers beside these.

---

### Task 2: A Python oracle for the grammar table

An independent decoder of the v3 verb grammar, written from the story format rather than from the C, so the C decode in Task 3 is checked against the same bytes the console reads instead of against a fixture that could drift. This mirrors `saturn/tests/test_exit_dests.py`, which does exactly this for the exit tables.

**Files:**
- Create: `saturn/tests/test_sentence_shape.py`
- Test: itself (it is a pytest module)

**Interfaces:**
- Consumes: nothing
- Produces: `verb_rows(raw)` -> `{verb_dict_id: [(nobj, prep1, prep2, attr1, attr2), ...]}`, and `dict_entries(raw)` -> `[(text, flags, dict_id), ...]`, both used by Task 3's cross-check

- [ ] **Step 1: Write the oracle and its self-check**

Create `saturn/tests/test_sentence_shape.py`:

```python
#!/usr/bin/env python3
"""Host oracle for the v3 verb grammar table, and the cross-check against
sentence_shape.c's decode of the same bytes.

A v3 story's verb grammar lives at a word table indexed from the static-memory
base by (255 - verb dictionary id). The first byte at that address is the row
count; each row is 8 bytes, of which the first five carry the sentence shape:

    byte 0  number of objects the line takes (0, 1 or 2)
    byte 1  dictionary id of the preposition before object 1, 0 for none
    byte 2  dictionary id of the preposition before object 2, 0 for none
    byte 3  object 1 search/attribute byte
    byte 4  object 2 search/attribute byte

Bytes 1-4 are what typeahead_extract.c already reads, into two flat bags.
Byte 0 -- and the pairing of a preposition with the object it precedes -- is
what this table adds and what the panel's sentence shape is made of.

Reads the shipped stories directly, not a fixture.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"
INPUT_DIR = ROOT / "saturn" / "src" / "input"
DRIVER_SRC = ROOT / "saturn" / "tests" / "dump_shape.c"

FL_VERB = 0x40
FL_PREP = 0x08


def rd16(raw, addr):
    return (raw[addr] << 8) | raw[addr + 1]


def dict_entries(raw):
    """Every dictionary entry as (text, flags, dict_id).

    Text is decoded through the A0 alphabet only, which is all a dictionary
    word needs -- the same decode room_model.c and typeahead_extract.c use.
    """
    a0 = "      abcdefghijklmnopqrstuvwxyz"
    p = rd16(raw, 0x08)
    nsep = raw[p]
    p += 1 + nsep
    elen = raw[p]
    p += 1
    count = rd16(raw, p)
    p += 2
    out = []
    for k in range(count):
        off = p + k * elen
        chars = []
        for half in (0, 2):
            x = rd16(raw, off + half)
            chars.append(a0[(x >> 10) & 31])
            chars.append(a0[(x >> 5) & 31])
            chars.append(a0[x & 31])
        out.append(("".join(chars).strip(), raw[off + 4], raw[off + 5]))
    return out


def verb_rows(raw):
    """{verb dict id: [(nobj, prep1, prep2, attr1, attr2), ...]}.

    Applies the same two guards typeahead_extract.c does -- the entry address
    must land inside static memory, and the row count must be 1..12 -- so a
    verb this refuses is a verb the C refuses too.
    """
    base = rd16(raw, 0x0e)
    out = {}
    for _text, flags, wid in dict_entries(raw):
        if not (flags & FL_VERB) or wid in out:
            continue
        a = rd16(raw, base + 2 * (255 - wid))
        if not (base <= a < len(raw)):
            continue
        n = raw[a]
        if not (1 <= n <= 12):
            continue
        rows = []
        for e in range(n):
            r = raw[a + 1 + e * 8: a + 1 + e * 8 + 8]
            if len(r) < 5:
                break
            rows.append((r[0], r[1], r[2], r[3], r[4]))
        out[wid] = rows
    return out


def all_stories():
    return sorted(Z3_DIR.glob("*.Z3"))


def load(path):
    return path.read_bytes()


def test_zork1_shapes_are_the_six_the_spec_names():
    """The design rests on Zork I having exactly six distinct shapes and 76
    rows with a preposition before the first object. If either number moves,
    the story file moved and the design's evidence needs re-reading."""
    raw = load(Z3_DIR / "ZORK1.Z3")
    rows = verb_rows(raw)
    shapes = {(n, 1 if p1 else 0, 1 if p2 else 0) for rs in rows.values() for (n, p1, p2, _a1, _a2) in rs}
    assert shapes == {(0, 0, 0), (1, 0, 0), (1, 1, 0), (2, 0, 0), (2, 0, 1), (2, 1, 1)}
    assert sum(len(rs) for rs in rows.values()) == 246
    assert sum(1 for rs in rows.values() for (n, p1, _p2, _a1, _a2) in rs if n >= 1 and p1) == 76


def test_every_story_decodes_and_stays_within_the_c_limits():
    """No shipped story exceeds the fixed limits sentence_shape.c is built to
    (184 verbs, 364 rows), and every object count is 0, 1 or 2 -- the three
    the panel knows how to walk."""
    worst_verbs = worst_rows = 0
    for path in all_stories():
        raw = load(path)
        if raw[0] != 3:
            continue
        rows = verb_rows(raw)
        nrows = sum(len(rs) for rs in rows.values())
        worst_verbs = max(worst_verbs, len(rows))
        worst_rows = max(worst_rows, nrows)
        for rs in rows.values():
            for (n, _p1, _p2, _a1, _a2) in rs:
                assert n in (0, 1, 2), f"{path.name}: object count {n}"
    assert worst_verbs <= 184, worst_verbs
    assert worst_rows <= 364, worst_rows
```

- [ ] **Step 2: Run it**

Run: `python -m pytest saturn/tests/test_sentence_shape.py -q`
Expected: 2 passed. If `test_zork1_shapes_are_the_six_the_spec_names` fails, stop -- the spec's evidence is wrong and the design needs revisiting before any C is written.

- [ ] **Step 3: Commit**

```bash
git add saturn/tests/test_sentence_shape.py
git commit -m "Decode the v3 verb grammar table in a host oracle before anything reads it in C: byte 0 of each 8-byte syntax row is the object count and with the two preposition bytes it is the whole sentence shape, which is the part typeahead_extract has always thrown away, and Zork I turns out to hold exactly six distinct shapes across 246 rows of which 76 put a preposition before the first object."
```

---

### Task 3: The shape module's table

The build half of `sentence_shape.c`: copy the rows out and answer nothing yet.

**Files:**
- Create: `saturn/src/input/sentence_shape.h`, `saturn/src/input/sentence_shape.c`
- Create: `saturn/tests/dump_shape.c` (host driver that prints the C decode for the oracle to compare)
- Modify: `saturn/tests/test_sentence_shape.py` (add the cross-check)

**Interfaces:**
- Consumes: `TrieNode`, `DictionaryWord`, `find_exact_word` from `typeahead.h`
- Produces:
  - `void shape_build(const unsigned char *story, unsigned int len, TrieNode *root);`
  - `void shape_destroy(void);`
  - `int shape_verb_rows(const char *verb, ShapeRow *out, int max);` -- rows for one verb, count returned, 0 when unknown
  - `typedef struct { unsigned char nobj, prep1, prep2, attr1, attr2; } ShapeRow;`
  - `#define SHAPE_ROWS_MAX 12`

- [ ] **Step 1: Write the header**

Create `saturn/src/input/sentence_shape.h`:

```c
/*----------------------
 | sentence_shape.h
 | Description: The sentence shapes a loaded v3 story's verb grammar allows, and
 |   the one question the command panel asks of them: given the words picked so
 |   far, what kind of word comes next. Built from the same story bytes the
 |   typeahead trie is built from and owned for as long as that trie is, because
 |   the online path frees the story the moment the trie exists. Implemented in
 |   sentence_shape.c.
 | Author: suinevere
 | Dependencies: typeahead.h (TrieNode, DictionaryWord), and the same
 |   TYPEAHEAD_MALLOC/FREE allocator the trie uses
 ----------------------*/
#ifndef SENTENCE_SHAPE_H
#define SENTENCE_SHAPE_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SHAPE_ROWS_MAX / SHAPE_PREP_MAX
 | Description: The row count one verb's grammar entry may hold -- the same
 |   1..12 bound typeahead_extract.c applies before it trusts an entry -- and so
 |   the most prepositions one slot can offer, since each row contributes at
 |   most one.
 | Author: suinevere
 ----------------------*/
#define SHAPE_ROWS_MAX 12
#define SHAPE_PREP_MAX 12

/*----------------------
 | ShapeRow
 | Description: One syntax line, bytes 0..4 of the story's 8-byte row: how many
 |   objects it takes, the dictionary id of the preposition before each (0 for
 |   none), and each object's search byte. The last two are carried for the
 |   noun-class ranking a later change may want and are not read today.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char nobj;
    unsigned char prep1;
    unsigned char prep2;
    unsigned char attr1;
    unsigned char attr2;
} ShapeRow;

/*----------------------
 | shape_build
 | Description: Decodes the story's verb grammar into an owned table: every
 |   verb's syntax rows, an index from verb dictionary id to them, and the
 |   canonical word for each preposition id. Copies rather than referencing the
 |   story, which the online path frees as soon as the trie is built. A second
 |   call replaces the first. Silently builds nothing for a story that is not v3
 |   or for a null trie, which leaves every verb unknown and every caller on its
 |   own fallback.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: the module's own table
 | Params: story -- the loaded story bytes; len -- its length; root -- the trie
 |   already built from the same story, used to resolve preposition spellings
 | Returns: N/A
 ----------------------*/
void shape_build(const unsigned char *story, unsigned int len, TrieNode *root);

/*----------------------
 | shape_destroy
 | Description: Frees the table. Safe on a module that never built one, and
 |   safe to call twice. Call beside destroy_typeahead, never after the trie is
 |   gone and this is still being asked questions.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: the module's own table
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void shape_destroy(void);

/*----------------------
 | shape_verb_rows
 | Description: Copies one verb's syntax rows into `out`. The lookup is by
 |   spelling, since that is what the panel's assembled line holds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: the module's own table
 | Params: verb -- the verb's spelling, may be null; out -- receives the rows;
 |   max -- out's capacity
 | Returns: rows written, 0 when the verb is unknown or has no entry
 ----------------------*/
int shape_verb_rows(const char *verb, ShapeRow *out, int max);

#ifdef __cplusplus
}
#endif

#endif // SENTENCE_SHAPE_H
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/input/sentence_shape.c`:

```c
/*----------------------
 | sentence_shape.c
 | Description: The table and lookups described in sentence_shape.h.
 | Author: suinevere
 | Dependencies: sentence_shape.h, typeahead.h, string.h
 ----------------------*/
#include "sentence_shape.h"
#include <string.h>

/*----------------------
 | SH_VERBS_MAX / SH_TOTAL_ROWS_MAX / FL_VERB / FL_PREP
 | Description: The table's fixed bounds -- measured across the twenty shipped
 |   v3 stories at 184 verbs and 364 rows, taken with room above both -- and the
 |   two dictionary flag bits this file reads.
 | Author: suinevere
 ----------------------*/
#define SH_VERBS_MAX      256
#define SH_TOTAL_ROWS_MAX 512
#define FL_VERB 0x40
#define FL_PREP 0x08

/*----------------------
 | ShapeVerb
 | Description: One verb's slice of the row array, keyed by its spelling since
 |   that is what the panel's line holds.
 | Author: suinevere
 ----------------------*/
typedef struct {
    char text[12];
    unsigned short first;
    unsigned char count;
} ShapeVerb;

/*----------------------
 | ShapeTable
 | Description: The whole owned table: the verbs, their rows end to end, and
 |   the canonical word per preposition dictionary id.
 | Author: suinevere
 ----------------------*/
typedef struct {
    ShapeVerb verb[SH_VERBS_MAX];
    ShapeRow  row[SH_TOTAL_ROWS_MAX];
    DictionaryWord *prep[256];
    int nverb;
    int nrow;
} ShapeTable;

/*----------------------
 | g_shape
 | Description: The module's one table, null until shape_build succeeds.
 | Author: suinevere
 ----------------------*/
static ShapeTable *g_shape = 0;

/*----------------------
 | sh_rd16
 | Description: A big-endian word from the story image.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- story bytes; a -- byte offset
 | Returns: the word
 ----------------------*/
static unsigned int sh_rd16(const unsigned char *s, unsigned int a) {
    return ((unsigned int) s[a] << 8) | s[a + 1];
}

/*----------------------
 | sh_dict_word
 | Description: Decodes a dictionary entry's four text bytes through the A0
 |   alphabet, which is all a dictionary word uses, into `out` (at least 8
 |   bytes) with trailing spaces removed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- story bytes; off -- the entry's offset; out -- receives the text
 | Returns: N/A
 ----------------------*/
static void sh_dict_word(const unsigned char *s, unsigned int off, char *out) {
    static const char a0[] = "      abcdefghijklmnopqrstuvwxyz";
    int n = 0, half, k;
    for (half = 0; half < 2; half++) {
        unsigned int x = sh_rd16(s, off + (unsigned int) half * 2);
        int z[3];
        z[0] = (int) ((x >> 10) & 31);
        z[1] = (int) ((x >> 5) & 31);
        z[2] = (int) (x & 31);
        for (k = 0; k < 3; k++) out[n++] = a0[z[k]];
    }
    out[n] = '\0';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
}

void shape_destroy(void) {
    if (g_shape == 0) return;
    TYPEAHEAD_FREE(g_shape);
    g_shape = 0;
}

void shape_build(const unsigned char *story, unsigned int len, TrieNode *root) {
    unsigned int dict_addr, base, p;
    int nsep, entry_len, count, k;

    shape_destroy();
    if (story == 0 || root == 0 || len < 0x40 || story[0] != 3) return;
    g_shape = (ShapeTable *) TYPEAHEAD_MALLOC((unsigned int) sizeof(ShapeTable));
    if (g_shape == 0) return;
    memset(g_shape, 0, sizeof(ShapeTable));

    dict_addr = sh_rd16(story, 0x08);
    base = sh_rd16(story, 0x0e);
    p = dict_addr;
    nsep = story[p];
    p += 1 + (unsigned int) nsep;
    entry_len = story[p];
    p += 1;
    count = (int) sh_rd16(story, p);
    p += 2;

    for (k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * (unsigned int) entry_len;
        char text[12];
        if (off + 6 > len) break;
        if ((story[off + 4] & FL_PREP) == 0) continue;
        sh_dict_word(story, off, text);
        if (text[0] == '\0') continue;
        if (g_shape->prep[story[off + 5]] == 0)
            g_shape->prep[story[off + 5]] = find_exact_word(root, text);
    }

    for (k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * (unsigned int) entry_len;
        unsigned int a;
        int nrows, e;
        char text[12];
        if (off + 6 > len) break;
        if ((story[off + 4] & FL_VERB) == 0) continue;
        if (g_shape->nverb >= SH_VERBS_MAX) break;
        sh_dict_word(story, off, text);
        if (text[0] == '\0') continue;
        a = sh_rd16(story, base + 2 * (255u - story[off + 5]));
        if (a < base || a >= len) continue;
        nrows = story[a];
        if (nrows < 1 || nrows > SHAPE_ROWS_MAX) continue;
        if (g_shape->nrow + nrows > SH_TOTAL_ROWS_MAX) break;
        {
            ShapeVerb *v = &g_shape->verb[g_shape->nverb];
            strncpy(v->text, text, sizeof v->text - 1);
            v->text[sizeof v->text - 1] = '\0';
            v->first = (unsigned short) g_shape->nrow;
            v->count = 0;
            for (e = 0; e < nrows; e++) {
                unsigned int r = a + 1 + (unsigned int) e * 8;
                ShapeRow *dst;
                if (r + 4 >= len) break;
                dst = &g_shape->row[g_shape->nrow++];
                dst->nobj  = story[r];
                dst->prep1 = story[r + 1];
                dst->prep2 = story[r + 2];
                dst->attr1 = story[r + 3];
                dst->attr2 = story[r + 4];
                v->count++;
            }
            if (v->count > 0) g_shape->nverb++;
            else g_shape->nrow = v->first;
        }
    }
}

int shape_verb_rows(const char *verb, ShapeRow *out, int max) {
    int i, n;
    if (g_shape == 0 || verb == 0 || out == 0 || max <= 0) return 0;
    for (i = 0; i < g_shape->nverb; i++) {
        if (strcmp(g_shape->verb[i].text, verb) != 0) continue;
        n = g_shape->verb[i].count;
        if (n > max) n = max;
        for (int j = 0; j < n; j++) out[j] = g_shape->row[g_shape->verb[i].first + j];
        return n;
    }
    return 0;
}
```

- [ ] **Step 3: Write the host driver**

Create `saturn/tests/dump_shape.c`:

```c
/*----------------------
 | dump_shape.c
 | Description: Host driver that prints sentence_shape.c's decode of a story's
 |   verb grammar, one line per row as "verb nobj prep1 prep2", for the Python
 |   oracle in test_sentence_shape.py to compare against its own decode.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tds.exe saturn/tests/dump_shape.c \
 |          saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c \
 |          saturn/src/input/typeahead_extract.c \
 |          && /tmp/tds.exe saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "../src/input/sentence_shape.h"
#include "../src/input/typeahead_extract.h"
#include <stdio.h>
#include <stdlib.h>

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void typeahead_free(void* ptr) { free(ptr); }

int main(int argc, char **argv) {
    FILE *f;
    long len;
    unsigned char *story;
    TrieNode *root;
    if (argc < 2) return 2;
    f = fopen(argv[1], "rb");
    if (f == NULL) return 2;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) len);
    if (story == NULL || fread(story, 1, (size_t) len, f) != (size_t) len) return 2;
    fclose(f);

    root = create_trie_node();
    build_typeahead_from_story(root, story, (unsigned int) len);
    shape_build(story, (unsigned int) len, root);

    for (int i = 0; i < 256; i++) {
        (void) i;
    }
    {
        /* The driver has no list of verbs of its own, so it re-reads the
           dictionary the same way and asks the module about each word. */
        unsigned int p = ((unsigned int) story[0x08] << 8) | story[0x09];
        int nsep = story[p];
        int elen, count;
        p += 1 + (unsigned int) nsep;
        elen = story[p];
        p += 1;
        count = (int) (((unsigned int) story[p] << 8) | story[p + 1]);
        p += 2;
        for (int k = 0; k < count; k++) {
            unsigned int off = p + (unsigned int) k * (unsigned int) elen;
            char text[12];
            ShapeRow rows[SHAPE_ROWS_MAX];
            int n, j;
            if ((story[off + 4] & 0x40) == 0) continue;
            {
                static const char a0[] = "      abcdefghijklmnopqrstuvwxyz";
                int w = 0;
                for (int half = 0; half < 2; half++) {
                    unsigned int x = ((unsigned int) story[off + half * 2] << 8) | story[off + half * 2 + 1];
                    text[w++] = a0[(x >> 10) & 31];
                    text[w++] = a0[(x >> 5) & 31];
                    text[w++] = a0[x & 31];
                }
                text[w] = '\0';
                while (w > 0 && text[w - 1] == ' ') text[--w] = '\0';
            }
            n = shape_verb_rows(text, rows, SHAPE_ROWS_MAX);
            for (j = 0; j < n; j++)
                printf("%s %d %d %d\n", text, rows[j].nobj, rows[j].prep1, rows[j].prep2);
        }
    }
    shape_destroy();
    return 0;
}
```

- [ ] **Step 4: Add the cross-check to the Python module**

Append to `saturn/tests/test_sentence_shape.py`:

```python
_BIN = None


def shape_binary():
    """Compile dump_shape.c once per session, or skip if there is no compiler.

    Skipping rather than failing is deliberate and matches test_exit_dests.py:
    a machine with no host compiler is not a machine where this decode
    regressed.
    """
    global _BIN
    if _BIN is not None:
        return _BIN
    cc = shutil.which("gcc") or shutil.which("cc")
    if cc is None:
        pytest.skip("no host compiler")
    out_dir = pathlib.Path(tempfile.gettempdir()) / "zaturn_test_sentence_shape"
    out_dir.mkdir(exist_ok=True)
    exe = out_dir / ("dump_shape.exe" if os.name == "nt" else "dump_shape")
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-o", str(exe),
           str(DRIVER_SRC),
           str(INPUT_DIR / "sentence_shape.c"),
           str(INPUT_DIR / "typeahead.c"),
           str(INPUT_DIR / "typeahead_extract.c")]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    _BIN = exe
    return exe


def c_rows(exe, story_path):
    """{verb text: [(nobj, prep1, prep2), ...]} as sentence_shape.c sees it."""
    result = subprocess.run([str(exe), str(story_path)], capture_output=True,
                            text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    out = {}
    for line in result.stdout.splitlines():
        text, nobj, p1, p2 = line.split()
        out.setdefault(text, []).append((int(nobj), int(p1), int(p2)))
    return out


@pytest.mark.parametrize("path", all_stories(), ids=lambda p: p.name)
def test_c_decode_matches_the_oracle(path):
    """Every verb the oracle finds rows for, the C finds the same rows for, in
    the same order. Verbs the oracle skipped are not asserted against: the C
    applies the same guards and skipping is what both are meant to do."""
    raw = load(path)
    if raw[0] != 3:
        pytest.skip("not v3")
    exe = shape_binary()
    got = c_rows(exe, path)
    by_id = verb_rows(raw)
    id_to_text = {}
    for text, flags, wid in dict_entries(raw):
        if (flags & FL_VERB) and wid not in id_to_text:
            id_to_text[wid] = text
    for wid, rows in by_id.items():
        text = id_to_text.get(wid)
        if text is None:
            continue
        expect = [(n, p1, p2) for (n, p1, p2, _a1, _a2) in rows]
        assert got.get(text) == expect, f"{path.name}: {text}"
```

- [ ] **Step 5: Run it and watch it pass**

Run: `python -m pytest saturn/tests/test_sentence_shape.py -q`
Expected: the two Task 2 tests plus one per shipped story, all passing. A mismatch on one story names the story and the verb.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input/sentence_shape.c saturn/src/input/sentence_shape.h saturn/tests/dump_shape.c saturn/tests/test_sentence_shape.py
git commit -m "Keep every verb's syntax rows in a table of their own, copied out of the story rather than pointed at it, because the online path frees the story bytes the moment the trie is built and a shape read at slot time would work in the CD and netbin builds and silently offer nothing in the third."
```

---

### Task 4: `shape_next`, the matching rule

The question the panel actually asks.

**Files:**
- Modify: `saturn/src/input/sentence_shape.h`, `saturn/src/input/sentence_shape.c`
- Create: `saturn/tests/test_shape_next.c`
- Modify: `saturn/tests/test_sentence_shape.py` (compile and run it)

**Interfaces:**
- Consumes: `ShapeRow`, `shape_verb_rows` from Task 3
- Produces:
  - `enum { SHAPE_PREP, SHAPE_NOUN, SHAPE_END, SHAPE_FREE };`
  - `typedef struct { int kind; DictionaryWord *prep[SHAPE_PREP_MAX]; int nprep; } ShapeSlot;`
  - `void shape_next(const char *const *picked, int npicked, ShapeSlot *out);`

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_shape_next.c`:

```c
/*----------------------
 | test_shape_next.c
 | Description: Host test for the sentence-shape matching rule, against Zork I's
 |   own grammar: a bare verb's first slot, a preposition before the first
 |   object, the second object's preposition arriving only in its own slot, the
 |   end of a sentence, and the free fallback for a verb the grammar does not
 |   describe.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tsn.exe saturn/tests/test_shape_next.c \
 |          saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c \
 |          saturn/src/input/typeahead_extract.c \
 |          && /tmp/tsn.exe saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "../src/input/sentence_shape.h"
#include "../src/input/typeahead_extract.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void typeahead_free(void* ptr) { free(ptr); }

static int offers(const ShapeSlot *s, const char *word) {
    for (int i = 0; i < s->nprep; i++)
        if (s->prep[i] != NULL && strcmp(s->prep[i]->text, word) == 0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    FILE *f;
    long len;
    unsigned char *story;
    TrieNode *root;
    ShapeSlot s;
    const char *line[4];

    assert(argc >= 2);
    f = fopen(argv[1], "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) len);
    assert(story != NULL);
    assert(fread(story, 1, (size_t) len, f) == (size_t) len);
    fclose(f);

    root = create_trie_node();
    build_typeahead_from_story(root, story, (unsigned int) len);
    shape_build(story, (unsigned int) len, root);

    /* "look" takes a preposition before its object, which the fixed chain could
       never place. */
    line[0] = "look";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_PREP);
    assert(offers(&s, "at"));

    line[1] = "at";
    shape_next(line, 2, &s);
    assert(s.kind == SHAPE_NOUN);

    line[2] = "lamp";
    shape_next(line, 3, &s);
    assert(s.kind == SHAPE_END);

    /* "put" takes its preposition before the SECOND object, not the first. */
    line[0] = "put";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_NOUN);
    line[1] = "lamp";
    shape_next(line, 2, &s);
    assert(s.kind == SHAPE_PREP);
    assert(offers(&s, "in"));
    line[2] = "in";
    shape_next(line, 3, &s);
    assert(s.kind == SHAPE_NOUN);
    line[3] = "case";
    shape_next(line, 4, &s);
    assert(s.kind == SHAPE_END);

    /* A word with no grammar entry leaves the caller on its own chain. */
    line[0] = "zzzzzz";
    shape_next(line, 1, &s);
    assert(s.kind == SHAPE_FREE);

    /* No verb picked yet is the verb slot, which is not this module's question. */
    shape_next(line, 0, &s);
    assert(s.kind == SHAPE_FREE);

    shape_destroy();
    printf("test_shape_next: ok\n");
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run:

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tsn.exe saturn/tests/test_shape_next.c \
  saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c \
  saturn/src/input/typeahead_extract.c
```

Expected: FAIL to compile -- `unknown type name 'ShapeSlot'`, `implicit declaration of function 'shape_next'`.

- [ ] **Step 3: Add the declarations to the header**

Insert into `saturn/src/input/sentence_shape.h`, above `shape_build`:

```c
/*----------------------
 | SHAPE_PREP / SHAPE_NOUN / SHAPE_END / SHAPE_FREE
 | Description: What the next slot wants: a preposition, an object, nothing
 |   because the sentence is complete, or -- SHAPE_FREE -- no answer at all,
 |   which is what a verb outside the grammar and a sentence taken off it both
 |   give, and which means the caller keeps its own fallback chain.
 | Author: suinevere
 ----------------------*/
enum { SHAPE_PREP = 0, SHAPE_NOUN, SHAPE_END, SHAPE_FREE };

/*----------------------
 | ShapeSlot
 | Description: One answer from shape_next: the kind, and when the kind is
 |   SHAPE_PREP the prepositions the surviving rows allow here, in the trie's
 |   own order.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int kind;
    DictionaryWord *prep[SHAPE_PREP_MAX];
    int nprep;
} ShapeSlot;

/*----------------------
 | shape_next
 | Description: Matches the words picked so far against the first word's syntax
 |   rows, position by position, dropping every row that disagrees, and reports
 |   what the survivors want next. Survivors that disagree with each other --
 |   one wanting a preposition where another wants an object -- give SHAPE_PREP
 |   with the union, since a preposition is the pick that tells them apart and
 |   an object is still reachable through the panel's noun tab.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: the module's own table
 | Params: picked -- the words picked so far, first being the verb; npicked --
 |   how many; out -- receives the answer, never left unwritten
 | Returns: N/A
 ----------------------*/
void shape_next(const char *const *picked, int npicked, ShapeSlot *out);
```

- [ ] **Step 4: Implement it**

Append to `saturn/src/input/sentence_shape.c`:

```c
/*----------------------
 | sh_is_prep
 | Description: Whether `word` is the canonical spelling of preposition id `id`.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shape
 | Params: id -- a preposition dictionary id; word -- the word picked
 | Returns: 1 on a match, 0 otherwise
 ----------------------*/
static int sh_is_prep(unsigned char id, const char *word) {
    DictionaryWord *w;
    if (id == 0 || g_shape == 0) return 0;
    w = g_shape->prep[id];
    return (w != 0 && strcmp(w->text, word) == 0);
}

/*----------------------
 | sh_add_prep
 | Description: Adds preposition id `id`'s canonical word to the answer, once.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_shape
 | Params: out -- the answer being built; id -- the preposition id
 | Returns: N/A
 ----------------------*/
static void sh_add_prep(ShapeSlot *out, unsigned char id) {
    DictionaryWord *w;
    int i;
    if (id == 0 || g_shape == 0 || out->nprep >= SHAPE_PREP_MAX) return;
    w = g_shape->prep[id];
    if (w == 0) return;
    for (i = 0; i < out->nprep; i++) if (out->prep[i] == w) return;
    out->prep[out->nprep++] = w;
}

void shape_next(const char *const *picked, int npicked, ShapeSlot *out) {
    ShapeRow rows[SHAPE_ROWS_MAX];
    int nrows, i, want_prep = 0, want_noun = 0, want_end = 0;

    out->kind = SHAPE_FREE;
    out->nprep = 0;
    if (picked == 0 || npicked < 1 || out == 0) return;
    nrows = shape_verb_rows(picked[0], rows, SHAPE_ROWS_MAX);
    if (nrows == 0) return;

    for (i = 0; i < nrows; i++) {
        const ShapeRow *r = &rows[i];
        int at = 1, objs = 0, alive = 1;
        while (alive && at < npicked) {
            if (objs == 0 && r->nobj >= 1 && r->prep1 != 0 && sh_is_prep(r->prep1, picked[at])) {
                at++;
            } else if (objs == 0 && r->nobj >= 1) {
                objs = 1; at++;
            } else if (objs == 1 && r->nobj >= 2 && r->prep2 != 0 && sh_is_prep(r->prep2, picked[at])) {
                at++;
            } else if (objs == 1 && r->nobj >= 2) {
                objs = 2; at++;
            } else {
                alive = 0;
            }
        }
        if (!alive) continue;
        if (objs == 0 && r->nobj >= 1) {
            if (r->prep1 != 0) { want_prep = 1; sh_add_prep(out, r->prep1); }
            else want_noun = 1;
        } else if (objs == 1 && r->nobj >= 2) {
            if (r->prep2 != 0) { want_prep = 1; sh_add_prep(out, r->prep2); }
            else want_noun = 1;
        } else {
            want_end = 1;
        }
    }

    if (want_prep) out->kind = SHAPE_PREP;
    else if (want_noun) out->kind = SHAPE_NOUN;
    else if (want_end) out->kind = SHAPE_END;
    else out->kind = SHAPE_FREE;
}
```

Note on the loop: a row whose preposition the player did not pick still consumes the position as an object, which is what lets `look lamp` match the `(1,1,0)` row's object slot as well as `look at lamp` -- the story's parser accepts both and so must this.

- [ ] **Step 5: Run the test to verify it passes**

Run:

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tsn.exe saturn/tests/test_shape_next.c \
  saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c \
  saturn/src/input/typeahead_extract.c && /tmp/tsn.exe saturn/cd/data/Z3/ZORK1.Z3
```

Expected: `test_shape_next: ok`

- [ ] **Step 6: Wire it into pytest**

Append to `saturn/tests/test_sentence_shape.py`:

```python
def test_shape_next_against_zork1():
    """Builds and runs test_shape_next.c so the C matching rule is exercised by
    `test.bat` rather than only by hand."""
    cc = shutil.which("gcc") or shutil.which("cc")
    if cc is None:
        pytest.skip("no host compiler")
    out_dir = pathlib.Path(tempfile.gettempdir()) / "zaturn_test_sentence_shape"
    out_dir.mkdir(exist_ok=True)
    exe = out_dir / ("shape_next.exe" if os.name == "nt" else "shape_next")
    src = ROOT / "saturn" / "tests" / "test_shape_next.c"
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-o", str(exe), str(src),
           str(INPUT_DIR / "sentence_shape.c"),
           str(INPUT_DIR / "typeahead.c"),
           str(INPUT_DIR / "typeahead_extract.c")]
    build = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(exe), str(Z3_DIR / "ZORK1.Z3")],
                         capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "ok" in run.stdout
```

- [ ] **Step 7: Run the suite and commit**

Run: `python -m pytest saturn/tests/test_sentence_shape.py -q`
Expected: all pass.

```bash
git add saturn/src/input/sentence_shape.c saturn/src/input/sentence_shape.h saturn/tests/test_shape_next.c saturn/tests/test_sentence_shape.py
git commit -m "Answer what the next word in a sentence should be by matching the words already picked against the verb's own syntax rows and dropping the ones that disagree, which is what puts 'at' between 'look' and its object and keeps 'in' out of every slot but the second object's."
```

---

### Task 5: The panel takes the slot kind from its caller

`command_panel.c` stops deciding the chain. It keeps no grammar and gains no include -- the header still declares `Dependencies: none`.

**Files:**
- Modify: `saturn/src/input/command_panel.h`, `saturn/src/input/command_panel.c`
- Modify: `saturn/tests/test_command_panel.c`
- Modify: `saturn/src/video/command_view.cxx` (call sites only, to keep the tree compiling; the grammar wiring is Task 6)

**Interfaces:**
- Consumes: nothing from earlier tasks (deliberately -- the panel does not know the shape module exists)
- Produces:
  - `enum { CP_SLOT_VERB = 0, CP_SLOT_NOUN, CP_SLOT_PREP, CP_SLOT_DONE };` (CP_SLOT_NOUN2 is gone; a second noun is CP_SLOT_NOUN again)
  - `#define CP_SLOT_POS_MAX 5`
  - `void cp_pick(CommandPanel *p, const char *word, int next_slot);`
  - `void cp_set_slot(CommandPanel *p, int slot);`
  - `int cp_word_count(const CommandPanel *p);`

- [ ] **Step 1: Write the failing test**

Replace the sentence-building section of `saturn/tests/test_command_panel.c` (the two blocks commented `Two-slot command` and `Four-slot command`) with:

```c
    /* Two-slot command: the caller says the sentence ends after the noun. */
    cp_reset(&p);
    cp_pick(&p, "take", CP_SLOT_NOUN);
    assert(p.slot == CP_SLOT_NOUN);
    assert(strcmp(p.line, "take") == 0);
    assert(cp_word_count(&p) == 1);
    cp_pick(&p, "lamp", CP_SLOT_DONE);
    assert(p.slot == CP_SLOT_DONE);
    assert(strcmp(p.line, "take lamp") == 0);
    assert(p.submitted == 1);

    /* A preposition before the FIRST object, which the old fixed chain could
       not express at all. */
    cp_reset(&p);
    cp_pick(&p, "look", CP_SLOT_PREP);
    assert(p.slot == CP_SLOT_PREP);
    cp_pick(&p, "at", CP_SLOT_NOUN);
    assert(p.slot == CP_SLOT_NOUN);
    cp_pick(&p, "lamp", CP_SLOT_DONE);
    assert(strcmp(p.line, "look at lamp") == 0);
    assert(p.submitted == 1);

    /* Five positions: verb, prep, noun, prep, noun. The second noun must not
       inherit the first noun's remembered row, so the places are kept by
       position and there are five of them. */
    cp_reset(&p);
    cp_pick(&p, "dig", CP_SLOT_PREP);
    cp_pick(&p, "in", CP_SLOT_NOUN);
    cp_pick(&p, "sand", CP_SLOT_PREP);
    cp_pick(&p, "with", CP_SLOT_NOUN);
    assert(cp_word_count(&p) == 4);
    cp_pick(&p, "shovel", CP_SLOT_DONE);
    assert(strcmp(p.line, "dig in sand with shovel") == 0);
    assert(p.submitted == 1);

    /* Back leaves the slot for the caller to set, and cp_set_slot is how it
       does: the panel has no grammar to re-derive one from. */
    cp_reset(&p);
    cp_pick(&p, "put", CP_SLOT_NOUN);
    cp_pick(&p, "lamp", CP_SLOT_PREP);
    cp_back(&p);
    assert(strcmp(p.line, "put") == 0);
    assert(cp_word_count(&p) == 1);
    cp_set_slot(&p, CP_SLOT_NOUN);
    assert(p.slot == CP_SLOT_NOUN);
```

- [ ] **Step 2: Run it to verify it fails**

Run:

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c \
  saturn/src/input/command_panel.c
```

Expected: FAIL -- `implicit declaration of function 'cp_word_count'` and `'cp_set_slot'`, and `CP_SLOT_NOUN2` still exists.

- [ ] **Step 3: Change the header**

In `saturn/src/input/command_panel.h`:

Replace the slot enum and its comment block with:

```c
/*----------------------
 | CP_SLOT_VERB .. CP_SLOT_DONE / CP_SLOT_POS_MAX
 | Description: What the panel is waiting for, not where in a fixed chain it
 |   is: a verb, a preposition, an object, or DONE, meaning the command is
 |   complete and marked for submission. A sentence can want two prepositions
 |   and two objects, so the same value can come round twice and the chain is
 |   the caller's to supply. CP_SLOT_POS_MAX is the most words one command can
 |   be built from -- verb, prep, noun, prep, noun -- and so how many places the
 |   per-position cursor memory holds.
 | Author: suinevere
 ----------------------*/
enum { CP_SLOT_VERB = 0, CP_SLOT_NOUN, CP_SLOT_PREP, CP_SLOT_DONE };
#define CP_SLOT_POS_MAX 5
```

In the `CommandPanel` struct, replace the two remembered-place arrays:

```c
    /* Where the cursor was left at each word POSITION, not each slot kind: a
       sentence can hold two objects, and the second must not open on the row
       the first was left at. Index is the word count when that place was left,
       0..CP_SLOT_POS_MAX-1. */
    int  slot_cursor[CP_SLOT_POS_MAX];
    int  slot_top[CP_SLOT_POS_MAX];
```

Replace `cp_pick`'s comment block and declaration:

```c
/*----------------------
 | cp_pick
 | Description: Appends `word` to the command, space-separated, and moves to
 |   `next_slot`, which the caller has already resolved from the story's grammar
 |   -- this file holds no chain of its own, since only the caller can know
 |   whether a preposition belongs before the next object or the sentence is
 |   finished. CP_SLOT_DONE marks the command submitted. A pick made from the
 |   travel module completes immediately whatever is passed, since a direction
 |   is a whole command. A pick made from the inventory overlay hands focus back
 |   to the word module on its way out, since the cursor it leaves behind is a
 |   word cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; word -- the word picked, may be null or empty when
 |   the overlay had nothing to offer; next_slot -- the slot to open after this
 |   word, one of CP_SLOT_*
 | Returns: N/A
 ----------------------*/
void cp_pick(CommandPanel *p, const char *word, int next_slot);

/*----------------------
 | cp_set_slot
 | Description: Points the panel at `slot`, keeping the per-position cursor
 |   memory -- the caller's way of saying what the sentence wants next after a
 |   change this file cannot resolve on its own, which is every change that is
 |   not a pick: a Back, a recalled line, a tab override that took the sentence
 |   off grammar. Out-of-range values are ignored rather than stored.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; slot -- one of CP_SLOT_*
 | Returns: N/A
 ----------------------*/
void cp_set_slot(CommandPanel *p, int slot);

/*----------------------
 | cp_word_count
 | Description: How many space-separated words the command holds, which is the
 |   position the next pick will fill and the index its place is remembered at.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state
 | Returns: the word count, 0 for an empty line
 ----------------------*/
int cp_word_count(const CommandPanel *p);
```

- [ ] **Step 4: Change the implementation**

In `saturn/src/input/command_panel.c`:

Replace `slot_remember` / `slot_restore` bodies so they key on position:

```c
static void slot_remember(CommandPanel *p) {
    int pos = cp_word_count(p);
    if (pos >= 0 && pos < CP_SLOT_POS_MAX) {
        p->slot_cursor[pos] = p->cursor;
        p->slot_top[pos]    = p->top;
    }
}

static void slot_restore(CommandPanel *p) {
    int pos = cp_word_count(p);
    if (p->slot == CP_SLOT_DONE) return;
    if (pos < 0 || pos >= CP_SLOT_POS_MAX) return;
    p->cursor = p->slot_cursor[pos];
    p->top    = p->slot_top[pos];
    if (p->cursor < 0 || p->cursor >= CP_WORD_CELLS) p->cursor = 0;
    if (p->top < 0) p->top = 0;
}
```

Add `cp_word_count` above them:

```c
int cp_word_count(const CommandPanel *p) {
    int i, n = 0, in_word = 0;
    for (i = 0; i < p->line_len; i++) {
        if (p->line[i] == ' ') in_word = 0;
        else if (!in_word) { in_word = 1; n++; }
    }
    return n;
}
```

Replace `cp_pick`'s slot switch with the caller's answer -- note `slot_remember` runs before the word is appended, because the position it records is the one just left:

```c
void cp_pick(CommandPanel *p, const char *word, int next_slot) {
    int i = 0;
    int empty = (word == 0 || word[0] == '\0');
    if (p->overlay && (!cp_overlay_takes_noun(p) || empty)) { cp_overlay_close(p); return; }
    if (empty) return;
    slot_remember(p);
    if (p->line_len > 0 && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = ' ';
    while (word[i] && p->line_len < CP_LINE_MAX - 1) p->line[p->line_len++] = word[i++];
    p->line[p->line_len] = '\0';

    if (p->box == CP_BOX_TRAVEL) { p->slot = CP_SLOT_DONE; p->submitted = 1; p->overlay = 0; return; }

    p->slot = (next_slot >= CP_SLOT_VERB && next_slot <= CP_SLOT_DONE)
            ? next_slot : CP_SLOT_DONE;
    slot_restore(p);
    if (p->overlay) p->box = CP_BOX_WORD;
    p->overlay = 0;
    if (p->slot == CP_SLOT_DONE) p->submitted = 1;
}
```

Add `cp_set_slot` beneath it:

```c
void cp_set_slot(CommandPanel *p, int slot) {
    if (slot < CP_SLOT_VERB || slot > CP_SLOT_DONE) return;
    p->slot = slot;
    slot_restore(p);
}
```

Replace `cp_load_line`'s slot derivation with the only one this file can honestly make:

```c
    p->slot = (words == 0) ? CP_SLOT_VERB : CP_SLOT_NOUN;
```

and replace that function's comment sentence about the chain with: `The slot is VERB on an empty line and NOUN otherwise; a caller that knows the story's grammar corrects it with cp_set_slot, and one that does not is no worse off than the fixed chain left it.`

Replace `cp_back`'s slot step with the same rule:

```c
    slot_remember(p);
    p->slot = (p->line_len == 0) ? CP_SLOT_VERB : CP_SLOT_NOUN;
    slot_restore(p);
```

In `cp_init`, size the clearing loop by `CP_SLOT_POS_MAX` instead of `CP_SLOT_DONE`.

In `cp_overlay_takes_noun`, drop the `CP_SLOT_NOUN2` arm: `return p->overlay && p->slot == CP_SLOT_NOUN;`

- [ ] **Step 5: Keep command_view.cxx compiling**

In `saturn/src/video/command_view.cxx`, three call sites change mechanically. This is a holding change; Task 6 replaces it with the real wiring.

- line ~701: `} else if (p.slot == CP_SLOT_NOUN || p.slot == CP_SLOT_NOUN2) {` becomes `} else if (p.slot == CP_SLOT_NOUN) {`
- in `cv_word_accept`: `cp_pick(&p, submit, wants_prep);` becomes `cp_pick(&p, submit, wants_prep ? CP_SLOT_PREP : (p.slot == CP_SLOT_VERB ? CP_SLOT_NOUN : CP_SLOT_DONE));`
- line ~1402: `cp_pick(&p, has ? submit : 0, has ? cv_verb_wants_prep(p, root, word) : 0);` becomes `cp_pick(&p, has ? submit : 0, has ? (cv_verb_wants_prep(p, root, word) ? CP_SLOT_PREP : CP_SLOT_DONE) : CP_SLOT_DONE);`

- [ ] **Step 6: Run the test to verify it passes**

Run:

```bash
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c \
  saturn/src/input/command_panel.c && /tmp/tcp.exe
```

Expected: PASS, no warnings.

- [ ] **Step 7: Check nothing else referenced the old names**

Run: `grep -rn "CP_SLOT_NOUN2\|wants_prep" --include=*.c --include=*.cxx --include=*.h saturn/src saturn/tests test`
Expected: only `cv_verb_wants_prep` in `command_view.cxx`, which Task 6 deletes. Any other hit is a call site this task missed.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/input/command_panel.c saturn/src/input/command_panel.h saturn/tests/test_command_panel.c saturn/src/video/command_view.cxx
git commit -m "Let the caller say what the panel is waiting for instead of walking a fixed verb-noun-prep-noun chain: a sentence can want a preposition before either object and can run to five words, so the slot becomes a kind that may come round twice and the cursor's remembered place moves from the slot to the word position, which is what stops a second object opening on the row the first was left at."
```

---

### Task 6: The view asks the shape module

The grammar answer now comes from the story rather than from a guess about it.

**Files:**
- Modify: `saturn/src/video/command_view.cxx`
- Modify: `saturn/src/engine/saturn_glue.cxx`, `saturn/src/net/online.cxx`
- Modify: `saturn/makefile`, `saturn/tests/test_netbin_sources.py`

**Interfaces:**
- Consumes: `shape_build`, `shape_destroy`, `shape_next`, `ShapeSlot`, `SHAPE_*` from Task 4; `cp_set_slot`, `cp_word_count` from Task 5
- Produces: `static int cv_next_slot(const CommandPanel &p, const char *picking);` -- the CP_SLOT_* the next pick should open, used by every pick site in this file

- [ ] **Step 1: Build the shape table beside the trie**

In `saturn/src/engine/saturn_glue.cxx`, add `#include "../input/sentence_shape.h"` beside the typeahead include, and in `ensure_typeahead`:

- after `destroy_typeahead(...)`, add `shape_destroy();`
- after `typeahead_add_abbreviations(g_typeahead_root);`, add `shape_build(story, len, g_typeahead_root);`

In `saturn/src/net/online.cxx`, the same include, and in `ensure_online_typeahead`:

- after each `destroy_typeahead(...)`, add `shape_destroy();`
- in the NETBIN branch, after `typeahead_add_abbreviations(g_online_ta);`, add `shape_build(netbin_story_data(), netbin_story_size(), g_online_ta);`
- in the CD branch, after `typeahead_add_abbreviations(g_online_ta);`, add `shape_build(story, len, g_online_ta);` -- **before** the `SRL::Memory::HighWorkRam::Free(story)` that follows, which is the whole reason the table is a copy

- [ ] **Step 2: Add the source to both builds**

In `saturn/makefile`, add `          src/input/sentence_shape.c \` to the NETBIN `SOURCES` list, directly after the `src/input/typeahead_extract.c` line. The CD build's `SOURCES` is a `find` glob over `src/` and needs no edit.

In `saturn/tests/test_netbin_sources.py`, add `"src/input/sentence_shape.c",` to `EXPECTED`, after `"src/input/typeahead_extract.c",`.

- [ ] **Step 3: Replace the guess with the answer**

In `saturn/src/video/command_view.cxx`:

Add `#include "../input/sentence_shape.h"` beside the typeahead include.

Delete `cv_verb_wants_prep` entirely -- its comment block and its body.

Add in its place:

```c
/*----------------------
 | cv_next_slot
 | Description: The slot the next pick should open, read off the story's own
 |   grammar: the sentence so far plus the word about to be picked, matched
 |   against the verb's syntax rows. SHAPE_FREE -- a verb outside the grammar, a
 |   sentence an override took off it, or Hard, where no trie and no shape table
 |   are built at all -- falls back to the chain this file used to hardcode, so
 |   a story whose grammar cannot be read is no worse served than before.
 | Author: suinevere
 | Dependencies: sentence_shape.h, command_panel.h
 | Globals: N/A
 | Params: p -- panel state; picking -- the word about to be picked
 | Returns: one of CP_SLOT_VERB..CP_SLOT_DONE
 ----------------------*/
static int cv_next_slot(const CommandPanel &p, const char *picking) {
    const char *words[CP_SLOT_POS_MAX];
    char buf[CP_SLOT_POS_MAX][CP_WORD_MAX + 1];
    ShapeSlot s;
    int n = 0, i = 0, len = 0;

    while (i < p.line_len && n < CP_SLOT_POS_MAX - 1) {
        if (p.line[i] == ' ') {
            if (len > 0) { buf[n][len] = '\0'; words[n] = buf[n]; n++; len = 0; }
        } else if (len < CP_WORD_MAX) {
            buf[n][len++] = p.line[i];
        }
        i++;
    }
    if (len > 0 && n < CP_SLOT_POS_MAX - 1) { buf[n][len] = '\0'; words[n] = buf[n]; n++; }
    if (picking != 0 && picking[0] != '\0' && n < CP_SLOT_POS_MAX) words[n++] = picking;

    shape_next(words, n, &s);
    if (s.kind == SHAPE_PREP) return CP_SLOT_PREP;
    if (s.kind == SHAPE_NOUN) return CP_SLOT_NOUN;
    if (s.kind == SHAPE_END)  return CP_SLOT_DONE;
    return (n <= 1) ? CP_SLOT_NOUN : CP_SLOT_DONE;
}
```

In `cv_word_accept`, replace the two `wants_prep` lines with:

```c
    next = cv_next_slot(p, w.word[p.cursor]);
    cv_submit_form(w.word[p.cursor], m, submit, (int) sizeof submit);
    cp_pick(&p, submit, next);
```

and change the local declaration `int wants_prep;` to `int next;`.

At the overlay pick site (~line 1402), replace the holding call from Task 5 with:

```c
    cp_pick(&p, has ? submit : 0, has ? cv_next_slot(p, word) : CP_SLOT_DONE);
```

At the two places a line changes without a pick, correct the slot afterwards. After the `cp_load_line(&p, h);` calls in the two `chord_fired(CA_RECALL, ...)` branches, and after the `cp_back(&p)` call, add:

```c
        cp_set_slot(&p, cv_next_slot(p, 0));
```

- [ ] **Step 4: Verify the panel and shape sources still build clean together**

Run:

```bash
gcc -std=c11 -Wall -Wextra -c -o /tmp/ss.o saturn/src/input/sentence_shape.c
gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe
gcc -std=c11 -Wall -Wextra -o /tmp/tsn.exe saturn/tests/test_shape_next.c saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c saturn/src/input/typeahead_extract.c && /tmp/tsn.exe saturn/cd/data/Z3/ZORK1.Z3
python -m pytest saturn/tests/test_netbin_sources.py saturn/tests/test_sentence_shape.py -q
```

Expected: all pass. `command_view.cxx` itself is a device source with SRL includes and is not host-buildable; it is compiled by the user's build in Task 9.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/command_view.cxx saturn/src/engine/saturn_glue.cxx saturn/src/net/online.cxx saturn/makefile saturn/tests/test_netbin_sources.py
git commit -m "Ask the story which word comes next instead of asking whether the verb has a preposition anywhere: the old question opened a preposition slot for every verb that took one in any of its syntax lines and could never put one before the first object, and the new one reads the line the player is actually building."
```

---

### Task 7: The tab strip

**Files:**
- Modify: `saturn/src/input/command_panel.h`, `saturn/src/input/command_panel.c`
- Modify: `saturn/tests/test_command_panel.c`
- Modify: `saturn/src/video/command_view.cxx`

**Interfaces:**
- Consumes: `cp_word_count`, `CP_SLOT_*` from Task 5
- Produces:
  - `enum { CP_TAB_VERB = 0, CP_TAB_NOUN, CP_TAB_PREP, CP_TAB_AZ, CP_TAB_N };`
  - `enum { CP_ZONE_LIST = 0, CP_ZONE_TABS, CP_ZONE_LETTERS };`
  - `int cp_tab_for_slot(int slot);`
  - `int cp_tab_move(CommandPanel *p, int dx);` -- returns -1/+1 when the move leaves the strip sideways, 0 when it stayed
  - fields `int zone; int tab; int tab_override;` on `CommandPanel`

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_command_panel.c`, before the final `printf`/`return`:

```c
    /* The strip is entered by pressing up at the top of an unscrolled list --
       the one press that did nothing at all before. */
    cp_reset(&p);
    cp_set_slot(&p, CP_SLOT_VERB);
    p.box = CP_BOX_WORD;
    p.zone = CP_ZONE_LIST;
    p.cursor = 0;
    p.top = 0;
    cp_word_move(&p, 0, -1, 20, CP_WORD_ROWS);
    assert(p.zone == CP_ZONE_TABS);
    assert(p.tab == CP_TAB_VERB);

    /* Along the strip, and off its right end into the module beyond. */
    assert(cp_tab_move(&p, +1) == 0);
    assert(p.tab == CP_TAB_NOUN);
    assert(cp_tab_move(&p, +1) == 0);
    assert(cp_tab_move(&p, +1) == 0);
    assert(p.tab == CP_TAB_AZ);
    assert(cp_tab_move(&p, +1) == 1);
    assert(cp_tab_move(&p, -1) == 0);
    assert(p.tab == CP_TAB_PREP);
    while (cp_tab_move(&p, -1) == 0) { }
    assert(p.tab == CP_TAB_VERB);

    /* Down leaves the strip for the list. */
    cp_word_move(&p, 0, +1, 20, CP_WORD_ROWS);
    assert(p.zone == CP_ZONE_LIST);

    /* Up at a scrolled list still scrolls -- the strip is only reachable from
       the top. */
    p.top = 1;
    p.cursor = 0;
    cp_word_move(&p, 0, -1, 20, CP_WORD_ROWS);
    assert(p.zone == CP_ZONE_LIST);
    assert(p.top == 0);

    /* An override lasts exactly one pick. */
    cp_reset(&p);
    p.zone = CP_ZONE_TABS;
    p.tab = CP_TAB_PREP;
    p.tab_override = 1;
    cp_pick(&p, "look", CP_SLOT_NOUN);
    assert(p.tab_override == 0);
    assert(p.tab == cp_tab_for_slot(CP_SLOT_NOUN));
    assert(p.zone == CP_ZONE_LIST);

    /* The default tab follows the slot when nothing is overriding it. */
    assert(cp_tab_for_slot(CP_SLOT_VERB) == CP_TAB_VERB);
    assert(cp_tab_for_slot(CP_SLOT_NOUN) == CP_TAB_NOUN);
    assert(cp_tab_for_slot(CP_SLOT_PREP) == CP_TAB_PREP);
```

- [ ] **Step 2: Run it to verify it fails**

Run: `gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c saturn/src/input/command_panel.c`
Expected: FAIL -- no `zone`, no `tab`, `cp_word_move` takes 4 arguments not 5.

- [ ] **Step 3: Add the state to the header**

In `saturn/src/input/command_panel.h`, add above `CommandPanel`:

```c
/*----------------------
 | CP_TAB_VERB .. CP_TAB_N
 | Description: The four word lists the module can show, left to right along the
 |   strip: the verbs, the objects, the prepositions, and the whole dictionary
 |   by first letter. The slot picks one by default; the player may pick another.
 | Author: suinevere
 ----------------------*/
enum { CP_TAB_VERB = 0, CP_TAB_NOUN, CP_TAB_PREP, CP_TAB_AZ, CP_TAB_N };

/*----------------------
 | CP_ZONE_LIST / CP_ZONE_TABS / CP_ZONE_LETTERS
 | Description: Which part of the word module the cursor is in, and so what its
 |   index means: a cell of the word grid, a tab of the strip above it, or a
 |   letter of the alphabet grid the A-Z tab opens.
 | Author: suinevere
 ----------------------*/
enum { CP_ZONE_LIST = 0, CP_ZONE_TABS, CP_ZONE_LETTERS };
```

Add to the `CommandPanel` struct:

```c
    int  zone;          /* CP_ZONE_* -- what the cursor is indexing */
    int  tab;           /* CP_TAB_* -- which list is showing */
    int  tab_override;  /* 1 while the player's tab outranks the slot's */
```

Declare the two new calls, and change `cp_word_move`'s signature and comment to take the visible row count:

```c
/*----------------------
 | cp_tab_for_slot
 | Description: The list a slot asks for when nothing is overriding it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- one of CP_SLOT_*
 | Returns: one of CP_TAB_*; the verb tab for DONE, which shows no list
 ----------------------*/
int cp_tab_for_slot(int slot);

/*----------------------
 | cp_tab_move
 | Description: Steps along the tab strip, marking the choice as the player's.
 |   A step off either end is the module's edge, reported the way cp_word_move
 |   reports the list's column edges so focus crosses by the same rule.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dx -- -1 or +1
 | Returns: -1 or +1 when the step left the strip sideways, 0 when it did not
 ----------------------*/
int cp_tab_move(CommandPanel *p, int dx);
```

- [ ] **Step 4: Implement**

In `saturn/src/input/command_panel.c`:

Add the two functions:

```c
int cp_tab_for_slot(int slot) {
    if (slot == CP_SLOT_NOUN) return CP_TAB_NOUN;
    if (slot == CP_SLOT_PREP) return CP_TAB_PREP;
    return CP_TAB_VERB;
}

int cp_tab_move(CommandPanel *p, int dx) {
    int t = p->tab + dx;
    if (t < 0) return -1;
    if (t >= CP_TAB_N) return 1;
    p->tab = t;
    p->tab_override = 1;
    p->cursor = 0;
    p->top = 0;
    return 0;
}
```

Give `cp_word_move` the row count and the strip exit. Replace its signature and the `dy` branch:

```c
int cp_word_move(CommandPanel *p, int dx, int dy, int ncand, int rows_visible) {
    int row = p->cursor / CP_WORD_COLS;
    int col = p->cursor % CP_WORD_COLS;
    int vis;

    if (p->zone == CP_ZONE_TABS) {
        if (dx != 0) return cp_tab_move(p, dx);
        if (dy > 0) p->zone = CP_ZONE_LIST;
        return 0;
    }
    ...
    if (dy != 0) {
        int nr = row + dy;
        vis = cp_word_rows(ncand) - p->top;
        if (vis > rows_visible) vis = rows_visible;
        if (nr < 0) {
            if (p->top > 0) p->top--;
            else p->zone = CP_ZONE_TABS;
        } else if (nr >= vis) {
            if (p->top < cp_top_max(ncand, rows_visible)) p->top++;
        } else {
            p->cursor = nr * CP_WORD_COLS + col;
        }
        cp_clamp_cursor(p, ncand);
    }
    return 0;
}
```

Thread `rows_visible` through the three functions that assumed the macro -- `cp_top_max(int ncand, int rows_visible)`, `cp_clamp(CommandPanel *p, int ncand, int rows_visible)` and `cp_fill(const char *const *cands, int ncand, int top, int rows_visible, CommandWords *out)` -- replacing every `CP_WORD_ROWS` in their bodies with `rows_visible` and every `CP_WORD_CELLS` bound in `cp_fill` and `cp_clamp_cursor` with `rows_visible * CP_WORD_COLS`. Update their header comment blocks to name the new parameter. `cp_word_enter` gains the same parameter and passes it on.

In `cp_pick`, release the override and follow the slot, immediately before `slot_restore(p)`:

```c
    p->tab_override = 0;
    p->tab = cp_tab_for_slot(p->slot);
    p->zone = CP_ZONE_LIST;
```

In `cp_init`, clear the three new fields: `p->zone = CP_ZONE_LIST; p->tab = CP_TAB_VERB; p->tab_override = 0;`

In `cp_set_slot`, follow the slot only when the player is not overriding: `if (!p->tab_override) p->tab = cp_tab_for_slot(slot);`

- [ ] **Step 5: Update the view's call sites**

In `saturn/src/video/command_view.cxx`, every `cp_word_move`, `cp_fill`, `cp_clamp` and `cp_word_enter` call gains the visible row count. Until Task 8 introduces A-Z, that count is always `CP_WORD_ROWS`. Add above them:

```c
/*----------------------
 | cv_list_rows
 | Description: How many rows of words the module is showing: all five, or the
 |   three left under the A-Z tab's two rows of letters.
 | Author: suinevere
 | Dependencies: command_panel.h
 | Globals: N/A
 | Params: p -- panel state
 | Returns: the row count
 ----------------------*/
static int cv_list_rows(const CommandPanel &p) {
    return (p.tab == CP_TAB_AZ) ? (CP_WORD_ROWS - 2) : CP_WORD_ROWS;
}
```

and pass `cv_list_rows(p)` at each site.

In `cv_word_dpad`, let the strip take the press:

```c
static void cv_word_dpad(CommandPanel &p, const unsigned char *exits, int ncand) {
    int dx = (pad_fired(Button::Right) ? 1 : 0) - (pad_fired(Button::Left) ? 1 : 0);
    int dy = (pad_fired(Button::Down)  ? 1 : 0) - (pad_fired(Button::Up)   ? 1 : 0);
    int row = (p.zone == CP_ZONE_TABS) ? 0 : p.cursor / CP_WORD_COLS;
    int edge;

    if (dx == 0 && dy == 0) return;
    edge = cp_word_move(&p, dx, dy, ncand, cv_list_rows(p));
    if (edge < 0)      cv_enter_travel(p, exits, row + CV_LIST_ROW0);
    else if (edge > 0) { p.box = CP_BOX_CMD; p.cursor = row; }
}
```

Draw the strip. Add:

```c
/*----------------------
 | CV_TAB_LABEL
 | Description: The strip's four labels, in CP_TAB_* order, sized to the two
 |   seven-column fields the word rows occupy.
 | Author: suinevere
 ----------------------*/
static const char *const CV_TAB_LABEL[CP_TAB_N] = { "Vb", "Nn", "Pr", "AZ" };

/*----------------------
 | cv_draw_tab_row
 | Description: Draws the tab strip on the word module's own top row: the
 |   showing tab at full brightness, the rest dim, and a bracket around the one
 |   the cursor is on while the strip holds focus.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h
 | Globals: N/A
 | Params: p -- panel state; y -- the text row to draw on
 | Returns: N/A
 ----------------------*/
static void cv_draw_tab_row(const CommandPanel &p, int y) {
    int t;
    for (t = 0; t < CP_TAB_N; t++) {
        int x = CV_WORD_X + 1 + t * 4;
        char field[4];
        int on = (p.tab == t);
        field[0] = (p.box == CP_BOX_WORD && p.zone == CP_ZONE_TABS && on) ? '[' : ' ';
        field[1] = CV_TAB_LABEL[t][0];
        field[2] = CV_TAB_LABEL[t][1];
        field[3] = '\0';
        if (on) text_print(x, y, field);
        else text_print_dim(x, y, field);
    }
}
```

and call it from the row loop, on the row above the list:

```c
            if (inner == -1) cv_draw_tab_row(p, y);
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe`
Expected: PASS.

- [ ] **Step 7: Check every caller was threaded**

Run: `grep -rn "cp_word_move\|cp_fill(\|cp_clamp(\|cp_word_enter" --include=*.c --include=*.cxx saturn/src saturn/tests`
Expected: every call passes five, five, three and four arguments respectively. A four-argument `cp_word_move` left anywhere is a missed site.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/input/command_panel.c saturn/src/input/command_panel.h saturn/tests/test_command_panel.c saturn/src/video/command_view.cxx
git commit -m "Put a four-tab strip on the word module's own top row so the player can overrule the list the grammar chose: it is reached by pressing up at the top of an unscrolled list, which is the one press in that module that did nothing at all, it hands focus sideways at its ends by the same rule the word cells do, and the override lasts exactly one pick so the panel goes back to guessing without being told to."
```

---

### Task 8: The A-Z tab

**Files:**
- Modify: `saturn/src/input/command_panel.h`, `saturn/src/input/command_panel.c`
- Modify: `saturn/tests/test_command_panel.c`
- Modify: `saturn/src/video/command_view.cxx`

**Interfaces:**
- Consumes: `CP_ZONE_LETTERS`, `CP_TAB_AZ`, `cv_list_rows` from Task 7
- Produces:
  - field `int letter;` on `CommandPanel` (0..25)
  - `int cp_letter_move(CommandPanel *p, int dx, int dy);`
  - `static int cv_build_letter_cands(TrieNode *root, int letter, const char **out);` in the view

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_command_panel.c`:

```c
    /* A-Z: the letter grid sits between the strip and a three-row list, and the
       cursor walks all three zones without leaving the module. */
    cp_reset(&p);
    p.box = CP_BOX_WORD;
    p.zone = CP_ZONE_TABS;
    p.tab = CP_TAB_AZ;
    p.letter = 0;
    cp_word_move(&p, 0, +1, 40, CP_WORD_ROWS - 2);
    assert(p.zone == CP_ZONE_LETTERS);
    assert(p.letter == 0);

    assert(cp_letter_move(&p, +1, 0) == 0);
    assert(p.letter == 1);
    assert(cp_letter_move(&p, 0, +1) == 0);
    assert(p.letter == 14);
    assert(cp_letter_move(&p, 0, -1) == 0);
    assert(p.letter == 1);
    assert(cp_letter_move(&p, 0, -1) == 0);
    assert(p.zone == CP_ZONE_TABS);

    p.zone = CP_ZONE_LETTERS;
    p.letter = 25;
    assert(cp_letter_move(&p, +1, 0) == 1);
    assert(p.letter == 25);
    p.letter = 0;
    assert(cp_letter_move(&p, -1, 0) == -1);

    p.zone = CP_ZONE_LETTERS;
    p.letter = 20;
    assert(cp_letter_move(&p, 0, +1) == 0);
    assert(p.zone == CP_ZONE_LIST);

    /* Three visible rows, not five: the window must not offer a fourth. */
    {
        CommandWords w;
        const char *cands[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };
        cp_fill(cands, 8, 0, CP_WORD_ROWS - 2, &w);
        assert(w.n == 6);
    }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c saturn/src/input/command_panel.c`
Expected: FAIL -- no `letter` field, no `cp_letter_move`.

- [ ] **Step 3: Implement the letter zone**

In `saturn/src/input/command_panel.h`, add to `CommandPanel`:

```c
    int  letter;        /* 0..25, the A-Z grid's cursor */
```

and declare:

```c
/*----------------------
 | cp_letter_move
 | Description: Walks the A-Z tab's two rows of thirteen letters. Up off the top
 |   row returns to the tab strip and down off the bottom row enters the word
 |   list, so the three zones are one column the cursor walks; a step off either
 |   side is the module's edge, reported as cp_word_move reports it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- panel state; dx -- -1, 0 or +1; dy -- -1, 0 or +1
 | Returns: -1 or +1 when the step left the module sideways, 0 otherwise
 ----------------------*/
int cp_letter_move(CommandPanel *p, int dx, int dy);
```

In `saturn/src/input/command_panel.c`:

```c
/*----------------------
 | CP_LETTER_COLS
 | Description: The alphabet's thirteen columns over two rows, which is what
 |   fits the word module's fourteen.
 | Author: suinevere
 ----------------------*/
#define CP_LETTER_COLS 13

int cp_letter_move(CommandPanel *p, int dx, int dy) {
    int row = p->letter / CP_LETTER_COLS;
    int col = p->letter % CP_LETTER_COLS;
    if (dx != 0) {
        int nc = col + dx;
        if (nc < 0) return -1;
        if (nc >= CP_LETTER_COLS) return 1;
        p->letter = row * CP_LETTER_COLS + nc;
        return 0;
    }
    if (dy < 0) {
        if (row == 0) { p->zone = CP_ZONE_TABS; return 0; }
        p->letter = col;
        return 0;
    }
    if (dy > 0) {
        if (row == 1) { p->zone = CP_ZONE_LIST; p->cursor = 0; p->top = 0; return 0; }
        p->letter = CP_LETTER_COLS + col;
        return 0;
    }
    return 0;
}
```

In `cp_word_move`, route the letter zone and let the strip open it:

```c
    if (p->zone == CP_ZONE_LETTERS) return cp_letter_move(p, dx, dy);
    if (p->zone == CP_ZONE_TABS) {
        if (dx != 0) return cp_tab_move(p, dx);
        if (dy > 0) p->zone = (p->tab == CP_TAB_AZ) ? CP_ZONE_LETTERS : CP_ZONE_LIST;
        return 0;
    }
```

and in the list's `nr < 0` branch, go back to the letters rather than the strip when they are up:

```c
            if (p->top > 0) p->top--;
            else p->zone = (p->tab == CP_TAB_AZ) ? CP_ZONE_LETTERS : CP_ZONE_TABS;
```

In `cp_init`, clear `p->letter = 0;`. In `cp_tab_move`, reset it: `p->letter = 0;`

- [ ] **Step 4: Source and draw the letter list**

In `saturn/src/video/command_view.cxx`, add the sourcing:

```c
/*----------------------
 | cv_build_letter_cands
 | Description: Sources the A-Z tab: every word in the trie beginning with
 |   `letter`, whatever its part of speech -- which is what makes this tab the
 |   way to reach a word the extractor typed wrong or could not type at all.
 |   Trie-weight ranked, like every other list here.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: g_cv_rest, g_cv_restwt
 | Params: root -- typeahead trie, may be null; letter -- 0..25 for 'a'..'z';
 |   out -- receives candidates
 | Returns: candidate count
 ----------------------*/
static int cv_build_letter_cands(TrieNode *root, int letter, const char **out) {
    char prefix[2];
    DictionaryWord *hot[CV_PRED_MAX];
    int n = 0, i, nh;
    if (root == 0 || letter < 0 || letter > 25) return 0;
    prefix[0] = (char) ('a' + letter);
    prefix[1] = '\0';
    nh = predict_candidates(root, 0, prefix, hot, CV_PRED_MAX, 0);
    for (i = 0; i < nh; i++) n = cv_add_cand(out, n, hot[i]->text);
    return n;
}
```

In `cv_refill_words`, dispatch on tab rather than slot -- replacing the `p.slot ==` chain:

```c
        } else if (p.tab == CP_TAB_VERB) {
            g_cv_ncand = cv_build_verb_cands(root, g_cv_cand, &core_n);
        } else if (p.tab == CP_TAB_NOUN) {
            prev = cv_last_word(p, root);
            g_cv_ncand = cv_build_noun_cands(root, prev, g_cv_cand, &core_n);
        } else if (p.tab == CP_TAB_PREP) {
            g_cv_ncand = cv_build_prep_cands(root, g_cv_cand);
        } else if (p.tab == CP_TAB_AZ) {
            g_cv_ncand = cv_build_letter_cands(root, p.letter, g_cv_cand);
            core_n = g_cv_ncand;
        }
```

In `cv_cache_stale`, add the tab and letter to the key: add `static int last_tab = -1, last_letter = -1;`, extend `same` with `&& p.tab == last_tab && p.letter == last_letter`, and store both beside the others.

Draw the letters. Add:

```c
/*----------------------
 | cv_draw_letter_row
 | Description: Draws one of the A-Z grid's two rows of thirteen letters, dim,
 |   with the letter under the cursor at full brightness while the letter zone
 |   holds focus.
 | Author: suinevere
 | Dependencies: command_panel.h, text_map.h
 | Globals: N/A
 | Params: row -- 0 for A-M, 1 for N-Z; p -- panel state; y -- the text row
 | Returns: N/A
 ----------------------*/
static void cv_draw_letter_row(int row, const CommandPanel &p, int y) {
    char field[2];
    int i;
    field[1] = '\0';
    for (i = 0; i < 13; i++) {
        int idx = row * 13 + i;
        int x = CV_WORD_X + 1 + i;
        field[0] = (char) ('A' + idx);
        if (p.box == CP_BOX_WORD && p.zone == CP_ZONE_LETTERS && p.letter == idx)
            text_print(x, y, field);
        else
            text_print_dim(x, y, field);
    }
}
```

and in the row loop, put the letters above a shortened list:

```c
            if (inner == -1) cv_draw_tab_row(p, y);
            else if (p.tab == CP_TAB_AZ && inner >= 0 && inner < 2) cv_draw_letter_row(inner, p, y);
            else if (inner >= (p.tab == CP_TAB_AZ ? 2 : 0) && inner < CP_WORD_ROWS)
                cv_draw_word_row(inner - (p.tab == CP_TAB_AZ ? 2 : 0), p, w, y);
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `gcc -std=c11 -Wall -Wextra -o /tmp/tcp.exe saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp.exe`
Expected: PASS.

- [ ] **Step 6: Run the whole host suite**

Run: `python -m pytest saturn/tests tools/tests -q`
Expected: no new failures against the run recorded in Task 1. Anything that changes is a regression this task caused.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/input/command_panel.c saturn/src/input/command_panel.h saturn/tests/test_command_panel.c saturn/src/video/command_view.cxx
git commit -m "Give the fourth tab two rows of letters over a three-row list, filtering live as the cursor crosses the alphabet, and let it offer the whole dictionary rather than the slot's part of speech, since a tab that only ever shows what the extractor already typed correctly is no escape from the times it did not."
```

---

### Task 9: Measure the cost and close it out

**Files:**
- Modify: `docs/superpowers/plans/2026-09-06-sentence-shape-and-word-tabs.md` (the baseline and closing numbers)
- Possibly modify: `saturn/makefile`, `saturn/src/input/sentence_shape.c` (only if the netbin does not fit)

**Interfaces:**
- Consumes: everything
- Produces: a measured verdict on the netbin budget

- [ ] **Step 1: Ask the user to build both images**

Post exactly this and wait:

> Both builds please -- `saturn/compile.bat` and `saturn/compile.bat NETBIN=1`. This is the measurement the plan's last decision rests on: there were 1,744 bytes of netbin slack before any of this, and I need to know what is left.

- [ ] **Step 2: Measure**

Run: `python -m pytest saturn/tests/test_netbin_budget.py saturn/tests/test_hwram_budget.py -q`

- [ ] **Step 3: If the netbin no longer fits, take the fallback**

Only if `test_the_image_leaves_room_to_grow` fails. The netbin keeps the fixed chain and the tab strip, and gives up the shape table -- which is the whole of the new persistent data and most of the new code:

- in `saturn/makefile`, move `src/input/sentence_shape.c` out of the NETBIN `SOURCES` list, and out of `EXPECTED` in `saturn/tests/test_netbin_sources.py`
- in `saturn/src/net/online.cxx`, wrap the `shape_build` and `shape_destroy` calls in `#ifndef NETBIN`
- in `saturn/src/video/command_view.cxx`, guard `cv_next_slot`'s body so the NETBIN build returns the fallback directly:

```c
#ifdef NETBIN
    (void) picking;
    return (cp_word_count(&p) == 0) ? CP_SLOT_NOUN : CP_SLOT_DONE;
#else
```

with `#endif` before the closing brace. Re-run Step 2 and report both numbers either way.

- [ ] **Step 4: Ask the user to play it**

Post exactly this and wait:

> Ready for a look on your side. Worth trying: `look at` something (which the picker could not build before at all), `put X in Y` (whose preposition should now appear only in the second object's slot), the tab strip by pressing up at the top of the word list, and the A-Z tab on a word the picker was hiding.

- [ ] **Step 5: Record the numbers and commit**

Add the closing measurement beside Task 1's baseline in this plan file, then:

```bash
git add docs/superpowers/plans/2026-09-06-sentence-shape-and-word-tabs.md saturn/makefile saturn/tests/test_netbin_sources.py saturn/src/net/online.cxx saturn/src/video/command_view.cxx
git commit -m "Record what the sentence-shape table and the tab strip actually cost the two images, measured on a build rather than estimated from the source."
```

---

## Self-Review

**Spec coverage.** Section 1 (shape module, copy-not-reference, data, interface, matching rule, three `SHAPE_FREE` paths) -> Tasks 3, 4, 6. Section 2 (slot kind, five positions, `cp_load_line`/`cp_back` re-derivation, `cv_verb_wants_prep` deleted) -> Tasks 5, 6. Section 3 (tab strip, dispatch inversion, A-Z, parameterised row count, cache key) -> Tasks 7, 8. Testing section -> Tasks 2, 3, 4, 5, 7, 8. Risks -> Tasks 1 and 9.

One spec item is deliberately deferred rather than dropped: `ShapeRow.attr1`/`attr2` are copied and not read. The spec's own comment says they are carried for a later noun-class ranking; no task reads them and none should.

**Placeholder scan.** No TBDs. Every code step carries the code. The one step that names a condition rather than code -- Task 9 Step 3 -- is a decision gated on a measurement that does not exist until then, and it carries the exact edits for the branch it may take.

**Type consistency.** `ShapeRow`, `ShapeSlot`, `SHAPE_ROWS_MAX`, `SHAPE_PREP_MAX`, `shape_build`, `shape_destroy`, `shape_verb_rows`, `shape_next` are spelled the same in Tasks 3, 4, 6 and in both host drivers. `cp_pick(p, word, next_slot)`, `cp_set_slot`, `cp_word_count`, `cp_tab_for_slot`, `cp_tab_move`, `cp_letter_move`, `CP_SLOT_POS_MAX`, `CP_TAB_*`, `CP_ZONE_*` are spelled the same in Tasks 5, 7, 8 and in the view. `cp_word_move` takes five arguments from Task 7 onward and every call site in Tasks 7 and 8 passes five.
