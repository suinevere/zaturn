# Classification Vocabulary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match plural keywords, and add the fourteen place-words that appear repeatedly in rooms currently classified as nothing.

**Architecture:** A new `has_word_infl()` carries a trailing-only `s`/`es` relaxation and is used by `text_classify_room` alone; `has_word_n` and `has_word` keep literal semantics so `text_scan_event` and the genre markers are unaffected. The redundant `trees` row is deleted and three compound words get explicit rows, which removes every derived-form collision and means no precedence machinery is needed.

**Tech Stack:** C89-compatible C (SH-2 cross-compiler via `make`), host `gcc` for tests.

## Global Constraints

- **Comment style is mandatory.** Every file, function and constant gets the `/*---- | name | Description: | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----*/` block, with `N/A` for fields that do not apply. **Tests and generated files get a FILE header only.** In-function `/* */` comments are permitted when they explain a non-obvious decision.
- **Author of record is `suinevere`** in every comment block.
- **Commit messages: one sentence.** No body, no bullets, no trailers. Never mention Claude, AI, or the session.
- **`TEXT_NUM_CATEGORIES` stays 15 and the `TC_*` enum order is frozen.**
- **`text_scan_event` and the genre markers must not inflect.** Both call `has_word`; leave `has_word` and `has_word_n` alone.
- **Blessing requires a rebuild.** `blessed.inc` is `#include`d into the test binary, so `--bless > blessed.inc` followed by running the *old* binary reports a false failure. Always rebuild between blessing and re-running.
- **Never run `saturn/compile.bat`, `compile-cd.bat`, `compile-netbin.bat`, or an emulator.** The user runs all Saturn builds. Cross-compile a changed unit alone to the scratch directory instead.
- **Do not re-run `tools/gen_room_corpus.py`.** It takes ~20 minutes and its output is committed.

---

## File Structure

**Modify:**
- `saturn/src/classify/room_class.c` — add `at_bound()` and `has_word_infl()`; point `text_classify_room`'s two keyword passes at the latter.
- `saturn/src/classify/room_class_data.c` — delete the `trees` row; add three compound rows (Task 1) and fourteen place-word rows (Task 2).
- `test/room_class_test.c` — assertions for both tasks.
- `test/corpus/blessed.inc` — re-blessed once per task.
- `docs/superpowers/specs/2026-08-06-classification-vocabulary-design.md` — status line, Task 2 only.

**No files are created.**

---

## Task 1: Inflection, the `trees` deletion, and the compound rows

**Files:**
- Modify: `saturn/src/classify/room_class.c` (after `has_word` at ~line 59; call sites at ~line 389 and ~line 398)
- Modify: `saturn/src/classify/room_class_data.c` (the `trees` row at line 23; new rows beside their bases)
- Modify: `test/room_class_test.c`
- Modify: `test/corpus/blessed.inc`

**Interfaces:**
- Consumes: nothing.
- Produces: `static int has_word_infl(const char* text, int len, const char* word)` in `room_class.c`. Task 2 adds data only and does not call it directly.

- [ ] **Step 1: Write the failing assertions**

Add to `assertions()` in `test/room_class_test.c`, before the `if (!fails) printf("ASSERTIONS: OK\n");` line:

```c
    /* ---- keyword inflection ----
       The table already held "passage"; the matcher just could not see the
       plural. 34 corpus rooms say "passages" and classified as nothing. */
    room_class_reset();
    CHECK(classify("Junction", "Two passages lead away from here.") == TC_UNDERGROUND);

    /* The relaxation is TRAILING ONLY, and this is the assertion that keeps it
       that way. "mineral" begins with the keyword "mine"; if the rule ever
       degenerated into prefix or substring matching, this room would read as
       underground and so would every pair the table distinguishes on purpose.

       The spec proposed "caverns must not match cave" for this. That test cannot
       fail: "cavern" is itself a keyword and votes TC_UNDERGROUND, the same as
       "cave", so it passes whether the rule is correct or broken. "mine" and
       "mineral" is the version with teeth -- "mineral" is not a keyword at all,
       so a match can only come from the boundary rule going wrong. */
    room_class_reset();
    CHECK(classify("Nowhere", "A mineral seam runs through it.") == TC_NEUTRAL);

    /* A hallway is not a hall. "hall" votes TOWN for public buildings; every
       hallway in the library is a domestic interior. Its explicit row must win,
       and it also proves the relaxation did not match "hall" inside "hallway" --
       if both scored, the Structure-tier tie would fall to TOWN on enum order. */
    room_class_reset();
    CHECK(classify("Upstairs Hallway", "A hallway runs the length of the floor.")
          == TC_HOUSE);

    /* Events keep literal matching: the relaxation must not reach EV[]. */
    CHECK(text_scan_event("A pile of jewels lies here.") == -1);
```

- [ ] **Step 2: Run and confirm they fail**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: the `passages` and `hallway` assertions FAIL. The `mineral` and `jewels` assertions PASS already — that is correct and expected; they are guards against the change going wrong, not drivers of it.

- [ ] **Step 3: Add the boundary helper and the relaxed matcher**

In `saturn/src/classify/room_class.c`, immediately after `has_word` (~line 59):

```c
/*----------------------
 | at_bound
 | Description: True when offset q ends a word -- past the end of the searched
 |   span, or sitting on a non-letter.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- haystack; len -- searched length; q -- offset to test
 | Returns: 1 when q is a word boundary, 0 otherwise
 ----------------------*/
static int at_bound(const char* text, int len, int q) {
    char c;
    if (q >= len) return 1;
    c = text[q];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

/*----------------------
 | has_word_infl
 | Description: Whole-word search that also accepts a plural: the word, the word
 |   plus "s", or the word plus "es".
 |
 |   The leading boundary stays strict and only the trailing one relaxes, which
 |   is the whole safety property. "caverns" matches "cavern" and never "cave";
 |   "mineral" matches neither "mine" nor anything else. A rule that relaxed both
 |   ends would silently collapse pairs the keyword table separates on purpose.
 |
 |   One pass, not one pass per suffix -- the alternative of re-scanning the text
 |   for word+"s" and again for word+"es" would triple a loop that already runs
 |   ~130 keywords over every sentence of the room text.
 |
 |   Deliberately NOT used by text_scan_event or the genre markers, which keep
 |   has_word: events fire on every turn with first-match-wins semantics, and
 |   inflecting them is a different decision from inflecting room keywords.
 | Author: suinevere
 | Dependencies: string.h, at_bound, lc
 | Globals: N/A
 | Params: text -- haystack; len -- how much of it to search; word -- lowercase needle
 | Returns: 1 on a whole-word or plural match, 0 otherwise
 ----------------------*/
static int has_word_infl(const char* text, int len, const char* word) {
    int wl = (int) strlen(word), p, i, e;
    for (p = 0; p + wl <= len; p++) {
        i = 0;
        while (i < wl && lc(text[p + i]) == word[i]) i++;
        if (i != wl) continue;
        {
            char before = (p == 0) ? ' ' : text[p - 1];
            if ((before >= 'a' && before <= 'z') ||
                (before >= 'A' && before <= 'Z')) continue;
        }
        e = p + wl;
        if (at_bound(text, len, e)) return 1;
        if (lc(text[e]) == 's' && at_bound(text, len, e + 1)) return 1;
        if (lc(text[e]) == 'e' && e + 1 < len && lc(text[e + 1]) == 's' &&
            at_bound(text, len, e + 2)) return 1;
    }
    return 0;
}
```

Reading `text[e]` is safe: `at_bound` already returned 0 for that offset, which it only does when `e < len`.

- [ ] **Step 4: Point the two keyword passes at it**

In `text_classify_room`, the sentence pass (~line 389):

```c
                        if (KW_VOTES(kw[i]) && has_word_infl(text + start, len, kw[i].word))
```

and the title pass (~line 398):

```c
        if (KW_VOTES(kw[i]) && has_word_infl(title, (int) strlen(title), kw[i].word))
```

Leave `genre_accumulate`'s `has_word` (~line 318) and `text_scan_event`'s `has_word` (~line 426) exactly as they are.

- [ ] **Step 5: Delete the redundant row and add the compounds**

In `saturn/src/classify/room_class_data.c`, line 23 currently begins:

```c
    {"trees",TC_WILDERNESS,KT_FEATURE,GN_ANY},{"woods",TC_WILDERNESS,KT_BIOME,GN_ANY},
```

Drop the `trees` entry, keeping `woods`. `tree` plus the relaxation now covers it, and leaving both would let one surface form score twice.

Add `pathway` beside `path` in the wilderness block:

```c
    {"path",TC_WILDERNESS,KT_FEATURE,GN_ANY},{"pathway",TC_WILDERNESS,KT_FEATURE,GN_ANY},
```

Add `passageway` beside `passage` in the underground block:

```c
    {"passage",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"passageway",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
```

Add `hallway` to the house block, with the reason it is not derived from `hall`:

```c
    /* Not an inflection of "hall": that votes TC_TOWN for public buildings and
       settlements, while every hallway in the library is a domestic interior --
       Cutthroats' inn landing, Deadline's and Moonmist's mansions, Infidel's
       pyramid. "way" is not a general suffix precisely so this can differ. */
    {"hallway",TC_HOUSE,KT_STRUCTURE,GN_ANY},
```

- [ ] **Step 6: Run — assertions pass, snapshot moves**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `ASSERTIONS: OK`, and a `SNAPSHOT:` block listing roughly 44 changed rooms.

- [ ] **Step 7: Read every changed room before blessing**

This is the deliverable of the task. Every line should be explainable as *"the table already had this word; now the plural or compound matches"*. Expect `passages` to dominate. A room that moved for any other reason is a signal the relaxation is matching something it should not — investigate it rather than blessing it.

- [ ] **Step 8: Bless, REBUILD, and re-run**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
/tmp/rct --bless > test/corpus/blessed.inc
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

The rebuild is not optional. `blessed.inc` is `#include`d into the binary, so running the old one after blessing compares against the table compiled in before, and reports a failure that is not real.

Expected: `SNAPSHOT: OK (1024 rooms unchanged)`, `ASSERTIONS: OK`, exit 0.

- [ ] **Step 9: Run the rest of the host suite**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
P="-I saturn/src -I saturn/src/sound -I saturn/src/classify"
S="saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c"
M="saturn/src/sound/music.c saturn/src/sound/music_data.c"
gcc -O2 $P -o /tmp/mct test/music_category_test.c $M $S && /tmp/mct | tail -1
gcc -O2 $P -o /tmp/mt  test/music_test.c        $M $S && /tmp/mt  | tail -1
python saturn/tests/test_category_art.py
python saturn/tests/test_netbin_sources.py
```

Expected: all green. `music_category_test` pins several classifier verdicts; if the relaxation moved one, that is a real finding to investigate, not an expectation to edit.

- [ ] **Step 10: Cross-compile the changed units**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/scratch"
mkdir -p "$SP"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
C="../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc"
$C -c -I src -I src/sound -I src/classify -o "$SP/room_class.o" src/classify/room_class.c
$C -c -I src -I src/sound -I src/classify -o "$SP/room_class_data.o" src/classify/room_class_data.c
```

Expected: no output, two `.o` files.

- [ ] **Step 11: Commit**

```bash
git add saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c \
        test/room_class_test.c test/corpus/blessed.inc
git commit -m "classify: match plural keywords and give hallway its own meaning"
```

---

## Task 2: The place-words

**Files:**
- Modify: `saturn/src/classify/room_class_data.c` (fourteen rows across the existing category blocks)
- Modify: `test/room_class_test.c`
- Modify: `test/corpus/blessed.inc`
- Modify: `docs/superpowers/specs/2026-08-06-classification-vocabulary-design.md` (status line)

**Interfaces:**
- Consumes: `has_word_infl` from Task 1 — the new words inflect automatically, so `tents`, `closets` and `canyons` match without extra rows.
- Produces: nothing.

- [ ] **Step 1: Write the failing assertion**

Add to `assertions()` in `test/room_class_test.c`, after the Task 1 block:

```c
    /* ---- place-words ----
       "tent" gives TC_DESERT its first Structure-tier word. Infidel's camp rooms
       previously lost to whatever biome word sat nearby, because DESERT could
       only ever field Biome and Feature entries. */
    room_class_reset();
    CHECK(classify("Camp", "Your tent stands in a clearing.") == TC_DESERT);
```

- [ ] **Step 2: Run and confirm it fails**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: that assertion FAILS, reporting `TC_WILDERNESS` — `clearing` is a Biome word and nothing yet outranks it.

- [ ] **Step 3: Add the fourteen rows**

In `saturn/src/classify/room_class_data.c`, add each to the block for the category it votes for, so the table stays grouped by mood.

Wilderness block:

```c
    {"canyon",TC_WILDERNESS,KT_BIOME,GN_ANY},{"volcano",TC_WILDERNESS,KT_BIOME,GN_ANY},
    {"cliff",TC_WILDERNESS,KT_BIOME,GN_ANY},{"ledge",TC_WILDERNESS,KT_FEATURE,GN_ANY},
```

Underground block:

```c
    {"maze",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},{"shaft",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"pit",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},{"alcove",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
```

Nautical block:

```c
    {"wharf",TC_NAUTICAL,KT_STRUCTURE,GN_ANY},
```

House block:

```c
    {"ballroom",TC_HOUSE,KT_STRUCTURE,GN_ANY},{"closet",TC_HOUSE,KT_STRUCTURE,GN_ANY},
    {"fireplace",TC_HOUSE,KT_FEATURE,GN_ANY},
```

Town block:

```c
    {"tower",TC_TOWN,KT_STRUCTURE,GN_ANY},
```

Desert block, with the note about what it unlocks:

```c
    /* TC_DESERT's first Structure-tier word. Without it the category could only
       field Biome and Feature entries, so any indoor word in an Infidel camp
       room outranked the camp itself. */
    {"tent",TC_DESERT,KT_STRUCTURE,GN_ANY},
```

`fireplace` is Feature rather than Structure on purpose — it is an object in a
room, not the room. Feature is enough to rescue a room that scores nothing else.

- [ ] **Step 4: Run — assertion passes, snapshot moves**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `ASSERTIONS: OK` and roughly 103 changed rooms.

- [ ] **Step 5: Read every changed room before blessing**

The bulk should be `TC_NEUTRAL → something`, which is the whole point. Watch for two things specifically:

- A room moving *between* two real categories rather than out of neutral. That means a new word outranked an existing one, which may be right or may be a mis-tiering — judge it against the room's text.
- `pit` dragging rooms wrongly. It is short and common in prose. The spec accepts dropping it if the diff shows it misbehaving; it is worth only 6 rooms.

- [ ] **Step 6: Bless, REBUILD, re-run**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
/tmp/rct --bless > test/corpus/blessed.inc
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `SNAPSHOT: OK (1024 rooms unchanged)`, `ASSERTIONS: OK`, exit 0. The rebuild is again not optional.

- [ ] **Step 7: Run the whole host suite**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
P="-I saturn/src -I saturn/src/sound -I saturn/src/classify"
S="saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c"
M="saturn/src/sound/music.c saturn/src/sound/music_data.c"
gcc -O2 $P -o /tmp/mct test/music_category_test.c $M $S && /tmp/mct | tail -1
gcc -O2 $P -o /tmp/mt  test/music_test.c        $M $S && /tmp/mt  | tail -1
gcc -O2 $P -o /tmp/rct test/room_class_test.c   $S    && /tmp/rct
python saturn/tests/test_category_art.py
python saturn/tests/test_netbin_sources.py
```

Expected: all five green.

- [ ] **Step 8: Cross-compile and report the new NEUTRAL count**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/scratch"
mkdir -p "$SP"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
"../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound \
    -I src/classify -o "$SP/room_class_data.o" src/classify/room_class_data.c
cd /c/Users/saggl/CLionProjects/zaturn
grep -c '^    0,' test/corpus/blessed.inc
```

The final count is how many rooms still classify as nothing, against 295 before this work. Record it in your report — it is the headline number for the whole change.

- [ ] **Step 9: Mark the spec implemented and commit**

Change line 4 of `docs/superpowers/specs/2026-08-06-classification-vocabulary-design.md` to `**Status:** Implemented 2026-08-06`.

```bash
git add saturn/src/classify/room_class_data.c test/room_class_test.c \
        test/corpus/blessed.inc \
        docs/superpowers/specs/2026-08-06-classification-vocabulary-design.md
git commit -m "classify: add the place-words that left a hundred rooms unclassified"
```

- [ ] **Step 10: Hand back for a Saturn build**

Report the before/after NEUTRAL counts, the host suite state, and that both changed units cross-compile. Ask the user to run `saturn/compile.bat` and check in play — in particular whether rooms that now classify are getting sensible art, since the corpus can prove the verdict changed but not that it looks right.
