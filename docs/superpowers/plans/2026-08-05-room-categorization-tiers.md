# Room Categorization — Tiers, Spatial Scope and Genre — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat keyword count in `text_classify_room()` with a tiered, sentence-scoped, genre-aware ranking, and prove the change on a golden corpus built from the real room text of all 31 stories in `tools/assets/Z3/`.

**Architecture:** Classification moves out of `saturn/src/sound/music.c` into a new `saturn/src/classify/` module. Scoring becomes a per-tier hit vector compared lexicographically, so a Structure keyword beats any number of Biome keywords. Sentence-level modifiers discard or double a sentence's hits. A per-game genre mask lets one keyword vote differently per game, with runtime inference for unknown games.

**Tech Stack:** C89-compatible C (SH-2 cross-compiler via `make`), host `gcc` for tests, Python 3 for the corpus generator.

## Global Constraints

- **Comment style is mandatory.** Every file, function and constant gets the `/*---- | name | Description: | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----*/` block. Use `N/A` for fields that do not apply. **No comments inside function bodies** except short `/* */` notes explaining a non-obvious decision. Tests get a file header only.
- **Author of record is `suinevere`** in every comment block.
- **Commit after every task.** One sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session.
- **Project layout:** `/src` holds only `main.cxx`; everything else lives in a concern subfolder. `saturn/src/classify/` is the new concern.
- **`TEXT_NUM_CATEGORIES` stays 15 and the `TC_*` enum order is frozen.** Category ids index `CATEGORY_POOL` in `music_data.c` and `CATEGORY_IMAGE` in `display.c`; inserting or reordering silently repoints every row after the insertion.
- **Do not add new keywords in this work.** Tier and genre are assigned to the ~110 keywords that already exist. Adding words changes the snapshot for reasons unrelated to the mechanism under test and makes the diff uninterpretable. Words like `room` and `hallway` from the original tier sketch are deliberately not introduced.
- **Never run `saturn/compile.bat` or the emulator.** The user runs all Saturn builds. To check that a changed unit still compiles, cross-compile that unit alone to the scratch directory. Host `gcc` test builds are expected and encouraged.
- **The Saturn build auto-discovers sources.** `saturn/Makefile:40-41` globs `find src/ -name '*.c'` and `'*.cxx'`, and line 89 puts every `src` subdirectory on the include path. A new `src/classify/` folder needs **no Makefile edit**. The NETBIN list at `Makefile:59-77` is explicit and must **not** gain the new files — `saturn/tests/test_netbin_sources.py` asserts it is exactly 19 entries.

---

## File Structure

**Create:**
- `saturn/src/classify/room_class.h` — the module interface: `TextKeyword`, `KT_*` tiers, `GN_*` genre masks, the classifiers, and the module's lifecycle calls.
- `saturn/src/classify/room_class.c` — the logic: word matching, sentence splitting, tier scoring, genre resolution. Target under 300 lines.
- `saturn/src/classify/room_class_data.c` — the tables: `KW[]`, `EV[]`, `NEG[]`, `POS[]`, `GENRE_KW[]`, `GAME_GENRE[]`. Edited freely; no logic.
- `tools/gen_room_corpus.py` — drives host mojozork over the game library, emits captured room text.
- `tools/wander.txt` — the fixed direction script, checked in.
- `test/room_class_test.c` — snapshot suite + assertion suite + `--bless` mode.
- `test/corpus/rooms.inc` — generated: captured room text.
- `test/corpus/blessed.inc` — generated: agreed verdicts.

**Modify:**
- `saturn/src/sound/music.c` — loses the classifiers; gains a `room_class.h` include and the genre-lock cache flush.
- `saturn/src/sound/music.h` — loses `TextKeyword`, `text_keywords`, `text_events`, `text_classify_room`, `text_scan_event`.
- `saturn/src/sound/music_data.c` — loses `KW[]`, `EV[]` and their accessors; keeps the track pools and `text_game_room_category`.
- `test/music_category_test.c` — build line, and three expectations that legitimately change.
- `test/music_test.c` — build line and include.

---

## Task 1: Move the classifier into `saturn/src/classify/`, behaviour unchanged

This task must not change a single verdict. It is the safe base the snapshot gets blessed against.

**Files:**
- Create: `saturn/src/classify/room_class.h`
- Create: `saturn/src/classify/room_class.c`
- Create: `saturn/src/classify/room_class_data.c`
- Modify: `saturn/src/sound/music.c` (remove lines 66–234 region, add include, forward title/reset)
- Modify: `saturn/src/sound/music.h:69-91,191-201`
- Modify: `saturn/src/sound/music_data.c:14-87,143-150`
- Modify: `test/music_category_test.c:1-10` (build line only)
- Modify: `test/music_test.c` (build line and include only)

**Interfaces:**
- Consumes: nothing.
- Produces: `int text_classify_room(const char*)`, `int text_scan_event(const char*)`, `void room_class_note_title(const char*)`, `void room_class_reset(void)`, `const TextKeyword* text_keywords(int*)`, `const TextKeyword* text_events(int*)`, and `typedef struct { const char* word; unsigned char cat; } TextKeyword` — all declared in `saturn/src/classify/room_class.h`.

- [ ] **Step 1: Confirm the existing tests pass before touching anything**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c \
    saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
```

Expected: prints an OK line and exits 0. If this fails, stop — the baseline is already broken and nothing below is meaningful.

- [ ] **Step 2: Create `saturn/src/classify/room_class.h`**

```c
/*----------------------
 | room_class.h
 | Description: Room and event classification: the keyword row type, the two
 |   keyword-table accessors, and the two classifiers that turn a turn's text
 |   into a TC_* category.
 |
 |   Split out of music.c because the category is not a sound concern -- it
 |   drives the background picture as much as the CD-DA track, and it only lived
 |   beside the engine because music consumed it first. music.c keeps the per-room
 |   memo cache and the playback decisions; everything about reading text lives
 |   here.
 |
 |   Includes music.h for the TC_* ids and TEXT_NUM_CATEGORIES rather than moving
 |   them: the category id is the row index of music_data.c's CATEGORY_POOL and
 |   display.c's CATEGORY_IMAGE, so the enum stays where both of those already
 |   reach it.
 | Author: suinevere
 | Dependencies: music.h (TC_*, TEXT_NUM_CATEGORIES)
 ----------------------*/
#ifndef ROOM_CLASS_H
#define ROOM_CLASS_H

#include "music.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | TextKeyword
 | Description: One keyword -> text-category mapping row, used by both the room
 |   and event tables.
 | Author: suinevere
 ----------------------*/
typedef struct { const char* word; unsigned char cat; } TextKeyword;

/*----------------------
 | table accessors (room_class_data.c)
 | Description: text_keywords returns the room-keyword table
 |   (TC_WILDERNESS..TC_PLACE_LAST) and its length; text_events returns the
 |   event-word table (TC_DANGER/TC_TRIUMPH) and its length.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n);
const TextKeyword* text_events(int* n);

/*----------------------
 | classifiers
 | Description: text_classify_room returns a room's mood from its text -- one of
 |   TC_WILDERNESS..TC_PLACE_LAST, or TC_NEUTRAL when nothing matched.
 |   text_scan_event returns an event category from turn text, or -1.
 | Author: suinevere
 ----------------------*/
int text_classify_room(const char* text);
int text_scan_event(const char* text);

/*----------------------
 | room_class_note_title / room_class_reset
 | Description: note_title records the authoritative room name the interpreter
 |   read off the location object, to be weighted as the title for the next
 |   classification instead of the first printed line; NULL or "" falls back to
 |   that first line. reset clears it, so one game's room cannot leak into the
 |   next.
 | Author: suinevere
 ----------------------*/
void room_class_note_title(const char* title);
void room_class_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* ROOM_CLASS_H */
```

- [ ] **Step 3: Create `saturn/src/classify/room_class.c` by moving code verbatim**

Move these from `saturn/src/sound/music.c` **without editing their bodies**: `lc()` (line 71), `has_word()` (lines 79–92), the `TEXT_TITLE_MAX` / `TEXT_TITLE_WEIGHT` comment block and defines (lines 96–117), `g_room_title` (lines 119–125), `text_room_title()` (lines 151–174), `text_classify_room()` (lines 176–216), `text_scan_event()` (lines 218–234).

The file header and the two new functions:

```c
/*----------------------
 | room_class.c
 | Description: The classification logic: whole-word matching, the room-title
 |   read, the keyword scoring that picks a room's mood, and the event scan. The
 |   tables it reads live in room_class_data.c and are meant to be edited freely.
 | Author: suinevere
 | Dependencies: room_class.h, string.h
 ----------------------*/
#include "room_class.h"
#include <string.h>
```

Then, replacing what was `music_note_room_title` in music.c:

```c
/*----------------------
 | room_class_note_title
 | Description: Records the authoritative room name for the turn about to be
 |   classified, which the interpreter reads off the location object rather than
 |   guessing from printed text.
 |
 |   It exists because the printed text lies on turn one. Zork I opens with its
 |   banner -- "ZORK I: The Great Underground Empire" -- above the room, so the
 |   first-line heuristic read that as the title, handed "underground" the title
 |   weight, and put West of House in a bunker. Any game whose banner names a
 |   place does the same, and so does any turn that prints something before the
 |   room description.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room_title
 | Params: title -- the room name, truncated to TEXT_TITLE_MAX-1; NULL clears it
 | Returns: N/A
 ----------------------*/
void room_class_note_title(const char* title) {
    int i = 0;
    if (title) for (; title[i] && i < TEXT_TITLE_MAX - 1; i++) g_room_title[i] = title[i];
    g_room_title[i] = 0;
}

/*----------------------
 | room_class_reset
 | Description: Clears the module's per-game state, so a room name recorded for
 |   one story cannot be weighted into the next one's first classification.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room_title
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_class_reset(void) {
    g_room_title[0] = 0;
}
```

- [ ] **Step 4: Create `saturn/src/classify/room_class_data.c`**

Move `KW[]` (music_data.c lines 14–73), `EV[]` (lines 75–87), and `text_keywords` / `text_events` (lines 143–150) verbatim, under this header:

```c
/*----------------------
 | room_class_data.c
 | Description: The tunable classification tables: the room keyword -> category
 |   table and the event keyword -> category table, plus the two accessors the
 |   classifier calls to reach them. All of it is data meant to be edited freely;
 |   the logic lives in room_class.c.
 | Author: suinevere
 | Dependencies: room_class.h (TextKeyword, TC_*)
 ----------------------*/
#include "room_class.h"
```

- [ ] **Step 5: Strip the moved code out of the sound module**

In `saturn/src/sound/music.c`: delete the moved functions and globals, add `#include "room_class.h"` beside the existing includes, and replace `music_note_room_title` with a forwarder that keeps the public name:

```c
/*----------------------
 | music_note_room_title
 | Description: Forwards the interpreter's room name to the classifier. Kept on
 |   the music_* surface because saturn_glue.cxx already calls it there and the
 |   interpreter has no reason to know classification moved.
 | Author: suinevere
 | Dependencies: room_class.h (room_class_note_title)
 | Globals: N/A
 | Params: title -- the room name; NULL clears it
 | Returns: N/A
 ----------------------*/
void music_note_room_title(const char* title) {
    room_class_note_title(title);
}
```

In `music_reset()`, add `room_class_reset();` so the title still clears — `test/music_category_test.c:318` pins this.

In `saturn/src/sound/music.h`: delete the `TextKeyword` block (lines 69–75), delete `text_keywords` and `text_events` from the accessor block (lines 87–88), and delete the classifiers block (lines 191–201). Leave `music_category_pool` and `text_game_room_category` where they are.

In `saturn/src/sound/music_data.c`: delete `KW[]`, `EV[]`, `text_keywords`, `text_events`, and change its header's Description and Dependencies to drop the keyword tables.

- [ ] **Step 6: Update the two host tests' build lines and includes**

`test/music_category_test.c` header comment, and `test/music_test.c` — replace the documented gcc line with:

```
   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mct \
       test/music_category_test.c saturn/src/sound/music.c \
       saturn/src/sound/music_data.c saturn/src/classify/room_class.c \
       saturn/src/classify/room_class_data.c && /tmp/mct
```

Add `#include "classify/room_class.h"` to both test files, after the existing `#include "sound/music.h"`.

- [ ] **Step 7: Run both host tests — every verdict must be identical**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mct \
    test/music_category_test.c saturn/src/sound/music.c \
    saturn/src/sound/music_data.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/mct
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mt \
    test/music_test.c saturn/src/sound/music.c \
    saturn/src/sound/music_data.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/mt
```

Expected: both PASS with zero failures. A single changed verdict here means the move was not verbatim — find it before continuing.

- [ ] **Step 8: Check the Saturn units still compile**

Cross-compile the changed units alone to scratch. Do **not** run `compile.bat`.

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/85376027-4922-4225-8ab7-2f9ed45c05ac/scratchpad"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
CDIR="../SaturnRingLib/Compiler"
"$CDIR/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound -I src/classify \
    -o "$SP/room_class.o" src/classify/room_class.c
"$CDIR/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound -I src/classify \
    -o "$SP/room_class_data.o" src/classify/room_class_data.c
```

Expected: no output, two `.o` files produced.

- [ ] **Step 9: Confirm the netbin gate still passes**

```bash
cd /c/Users/saggl/CLionProjects/zaturn && python saturn/tests/test_netbin_sources.py
```

Expected: PASS. The new files must not appear in the NETBIN list.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/classify saturn/src/sound/music.c saturn/src/sound/music.h \
        saturn/src/sound/music_data.c test/music_category_test.c test/music_test.c
git commit -m "classify: move room and event classification out of the sound module unchanged"
```

---

## Task 2: Corpus generator

**REVISED 2026-08-05 after the first attempt.** The runtime-only capture reached
119 rooms across 24 games and left 7 stories barren behind a `KNOWN_BARREN`
allowlist — including `SEASTLKR`, which the spec names as a genre assertion
target. A probe established that decoding the story files directly is both
feasible and far better: Z-string decoding of the v3 object table yields room
titles and, via a per-game description property, the real room prose. Every one
of the barren games carries prose statically (Sorcerer 79 objects, Seastalker 51,
Moonmist 27, Witness 17, Suspended 16).

**Static extraction is now the primary source; the runtime capture stays as a
complement** for rooms whose description is computed by a routine rather than
stored as a string — the one class static decoding cannot reach.

**Files:**
- Create: `tools/zstory.py` — v3 story decoding: Z-strings, abbreviations, the
  object table, property lookup. Pure decoding, no corpus policy.
- Create: `tools/gen_room_corpus.py` — static extraction + runtime capture, unioned.
- Create: `tools/wander.txt`
- Create: `test/corpus/rooms.inc` (generated output, committed)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `test/corpus/rooms.inc`, defining `static const CorpusRoom CORPUS[]` with fields `{ unsigned short release; const char* serial; const char* title; const char* text; }` and `#define CORPUS_N <count>`. Task 3 includes this file and relies on exactly those names.

- [ ] **Step 0: Build the static extractor**

A verified working Z-string decoder is at
`.superpowers/sdd/2026-08-05-room-categorization-tiers/zstring-probe-reference.py`.
It correctly decodes Zork I's 250 objects and their names. Use it as the
starting point for `tools/zstory.py` rather than rediscovering the format.

The extraction, per story:

1. Decode the abbreviation table (header `0x18`) and the object table
   (header `0x0A`: 31 property-default words, then 9-byte v3 object entries of
   attributes/parent/sibling/child/property-address).
2. **Detect the description property per game** — it is not a constant (Zork I
   11, Starcross 13, Seastalker 18). Score each property number by how many
   objects have a 2-byte value that, read as a packed address, decodes to
   prose; the highest-scoring number wins. Print the detected number per game
   so a bad detection is visible rather than silent.
3. **Identify rooms.** A room is an object carrying the description property
   plus at least one direction property. Direction-property numbering differs
   per game, so derive it rather than hardcoding: the direction properties are
   the high-numbered ones shared by objects that other objects sit inside.
   Whatever rule you use, validate it — Zork I must yield `West of House`,
   `Kitchen`, `Cellar`, `Behind House` and `Forest Path` as rooms and must not
   yield `mouse hole` or `magic boat`.
4. **Skip routine-described rooms.** When the description property holds a
   routine address rather than a string, decoding produces garbage. Detect it
   (the decode fails, or the result fails a printable-prose check) and skip the
   room rather than admitting noise. **Count the skips and print them per
   game** — this is the coverage gap and it must be visible.

- [ ] **Step 1: Create `tools/wander.txt`**

A fixed, checked-in direction script. No randomness — the corpus must regenerate byte-identical or the snapshot suite is worthless.

```
n
e
s
w
ne
se
sw
nw
u
d
in
out
n
n
e
e
s
s
w
w
u
u
d
d
ne
ne
se
se
sw
sw
nw
nw
in
in
out
out
n
e
u
w
s
d
```

- [ ] **Step 2: Write the generator**

```python
#!/usr/bin/env python3
"""Capture real room text from every story in tools/assets/Z3 into a C fixture.

The classifier's job is judgement, and the only way to know a change improved it
is to run it over the prose the games actually print. This drives host mojozork
twice per game -- once through its winning walkthrough where one exists, once
through a fixed wander script -- and keeps whatever rooms either pass reached.

It never decides what the RIGHT category is. That belongs to blessed.inc, which
room_class_test writes; this file only records what the games say.

Determinism is a hard requirement: the wander script is fixed and checked in, and
the junk filter is a phrase list rather than a heuristic, so a regeneration with
an unchanged game library produces a byte-identical rooms.inc. If that ever stops
holding, the snapshot suite is broken even while it is passing.

Run: python tools/gen_room_corpus.py
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
Z3 = ROOT / "tools" / "assets" / "Z3"
SOLUTIONS = ROOT / "tools" / "typeahead" / "solutions"
WANDER = ROOT / "tools" / "wander.txt"
OUT = ROOT / "test" / "corpus" / "rooms.inc"

# Turn output whose first line matches one of these is not a room. The wander
# pass walks into walls constantly, so this is load-bearing rather than tidy.
JUNK = (
    "you can't go that way", "it is pitch black", "i don't understand",
    "that's not a verb", "you have died", "you can't see", "i beg your pardon",
    "there is a wall", "that sentence isn't one i recognize",
)

TITLE_MAX = 64          # matches TEXT_TITLE_MAX in room_class.c
MIN_BODY = 20           # a room prints prose, not a two-word acknowledgement


def build_mojozork(tmp):
    """Compile the host interpreter once; returns the binary path."""
    exe = tmp / "mojozork"
    subprocess.run(["gcc", "-O2", "-o", str(exe), str(ROOT / "saturn" / "mojozork.c")],
                   check=True)
    return exe


def story_header(path):
    """The Z-header's release number (0x02, big-endian) and 6-char serial (0x12)."""
    raw = path.read_bytes()
    release = (raw[0x02] << 8) | raw[0x03]
    serial = raw[0x12:0x18].decode("ascii", "replace")
    return release, serial


def run(exe, story, commands):
    """Feed commands to the interpreter and return its turn chunks.

    mojozork echoes '>' before each turn's output, so splitting on it gives one
    chunk per command. Chunk 0 is the banner plus the opening room and is
    discarded wholesale -- 'look' is issued as the second command so the opening
    room comes back as a clean chunk of its own.
    """
    script = "verbose\nlook\n" + "\n".join(commands) + "\n"
    proc = subprocess.run([str(exe), str(story)], input=script, capture_output=True,
                          text=True, errors="replace", timeout=120)
    return proc.stdout.split("\n>")[1:]


def rooms_from(chunks):
    """Every chunk that looks like a room entry, as (title, body)."""
    found = []
    for chunk in chunks:
        lines = [ln.rstrip() for ln in chunk.strip("\n").split("\n")]
        lines = [ln for ln in lines if ln.strip()]
        if len(lines) < 2:
            continue
        title = lines[0].strip()
        if not title or len(title) > TITLE_MAX:
            continue
        # A room title is a bare noun phrase. Anything ending in sentence
        # punctuation is the interpreter talking, not a place.
        if title[-1] in ".!?,;:":
            continue
        if title.lower() in [j for j in JUNK]:
            continue
        body = " ".join(lines[1:]).strip()
        if len(body) < MIN_BODY:
            continue
        if any(body.lower().startswith(j) for j in JUNK):
            continue
        found.append((title, body))
    return found


def c_string(s):
    """A C string literal for s, with the escapes C actually needs."""
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + s + '"'


def main():
    if not Z3.is_dir():
        print(f"FAIL: no game library at {Z3} -- run tools/assets/games.bat first")
        return 1

    stories = sorted(Z3.glob("*.Z3"))
    if not stories:
        print(f"FAIL: no .Z3 files under {Z3}")
        return 1

    wander = [ln.strip() for ln in WANDER.read_text().splitlines() if ln.strip()]
    rows = []
    barren = []

    with tempfile.TemporaryDirectory() as td:
        exe = build_mojozork(pathlib.Path(td))
        for story in stories:
            stem = story.stem.upper()
            release, serial = story_header(story)
            seen = {}
            passes = []

            win = SOLUTIONS / f"{stem}.WIN"
            if win.is_file() and win.stat().st_size > 0:
                cmds = [ln.strip() for ln in win.read_text(errors="replace").splitlines()
                        if ln.strip() and not ln.strip().startswith("#")]
                for title, body in rooms_from(run(exe, story, cmds)):
                    seen.setdefault(title, body)
                passes.append("solution")

            for title, body in rooms_from(run(exe, story, wander)):
                seen.setdefault(title, body)
            passes.append("wander")

            print(f"  {stem:10s} {len(seen):4d} rooms  ({', '.join(passes)})")
            if not seen:
                barren.append(stem)
            for title in sorted(seen):
                rows.append((release, serial, title, seen[title]))

    if barren:
        print(f"FAIL: produced no rooms at all: {', '.join(barren)}")
        return 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("/*----------------------\n")
        f.write(" | rooms.inc\n")
        f.write(" | Description: GENERATED FILE -- do not edit by hand; produced by\n")
        f.write(" |   tools/gen_room_corpus.py. Real room text captured from every story\n")
        f.write(" |   in tools/assets/Z3, used by test/room_class_test.c. Carries no\n")
        f.write(" |   expected categories: those live in blessed.inc.\n")
        f.write(" | Author: suinevere\n")
        f.write(" ----------------------*/\n")
        f.write(f"#define CORPUS_N {len(rows)}\n")
        f.write("static const CorpusRoom CORPUS[CORPUS_N] = {\n")
        for release, serial, title, body in rows:
            f.write(f"    {{ {release}, {c_string(serial)}, {c_string(title)},\n")
            f.write(f"      {c_string(body)} }},\n")
        f.write("};\n")

    print(f"gen_room_corpus: {len(rows)} rooms from {len(stories)} stories -> {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Run the generator**

```bash
cd /c/Users/saggl/CLionProjects/zaturn && python tools/gen_room_corpus.py
```

Expected: a per-game line for each of the 31 stories showing the detected
description property, the static room count, the runtime room count, and the
number of routine-described rooms skipped. `LURKING` must show a solution pass —
if it shows wander only, the `LURKING.WIN` rename did not land.

**No story may be barren, and there is no allowlist.** The first attempt carried
a `KNOWN_BARREN` list of seven; static extraction is what removes the need for
it. If a story still yields nothing, that is a real extraction failure to fix,
not a fact to record — the two Infocom Samplers are the only stories permitted
to yield no *rooms*, and even they must decode objects successfully.

The total should be in the many hundreds. If it is not, the room-identification
rule in Step 0 is too narrow — check it against Zork I, whose room count alone
should be near a hundred.

- [ ] **Step 4: Verify determinism**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
cp test/corpus/rooms.inc /tmp/rooms-first.inc
python tools/gen_room_corpus.py >/dev/null
diff /tmp/rooms-first.inc test/corpus/rooms.inc && echo "DETERMINISTIC"
```

Expected: prints `DETERMINISTIC` with no diff. If it differs, the generator is not fit for purpose — fix it before Task 3, because a non-deterministic corpus makes every later snapshot diff meaningless.

- [ ] **Step 5: Spot-check the captured text**

```bash
cd /c/Users/saggl/CLionProjects/zaturn && grep -c "^    {" test/corpus/rooms.inc && grep -A1 '"West of House"' test/corpus/rooms.inc | head -4
```

Expected: a room count in the hundreds, and West of House's real description text — not a truncated or escaped mess.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_room_corpus.py tools/wander.txt test/corpus/rooms.inc
git commit -m "test: capture real room text from every story into a committed corpus fixture"
```

---

## Task 3: Snapshot harness, blessed against the unchanged classifier

**This is the sequencing gate.** The snapshot records what the classifier does *today*. Blessing after any scoring change would record the new behaviour as the baseline, and the suite would pass on day one no matter what the rewrite did.

**Files:**
- Create: `test/room_class_test.c`
- Create: `test/corpus/blessed.inc` (generated by `--bless`, committed)

**Interfaces:**
- Consumes: `text_classify_room`, `room_class_note_title`, `room_class_reset` from Task 1; `CORPUS`, `CORPUS_N`, `CorpusRoom` from Task 2.
- Produces: `test/corpus/blessed.inc`, defining `static const unsigned char BLESSED[CORPUS_N]` — one `TC_*` id per corpus row, in corpus order.

- [ ] **Step 1: Write the test harness**

```c
/* Golden-corpus tests for room classification.

   Two suites over one corpus. The SNAPSHOT compares every captured room against
   the verdict recorded in blessed.inc and fails on any difference, so a keyword
   or tier edit shows its blast radius across the whole game library instead of
   only where someone thought to look. The ASSERTIONS pin the specific rooms this
   work exists to fix.

   Re-blessing is deliberate and reviewable:
       ./rct --bless > test/corpus/blessed.inc
   Read the diff. Do not bless to make a red suite green.

   gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
       test/room_class_test.c saturn/src/classify/room_class.c \
       saturn/src/classify/room_class_data.c && /tmp/rct */
#include <stdio.h>
#include <string.h>
#include "classify/room_class.h"

typedef struct {
    unsigned short release;
    const char*    serial;
    const char*    title;
    const char*    text;
} CorpusRoom;

#include "corpus/rooms.inc"

static const char* CAT_NAME[TEXT_NUM_CATEGORIES] = {
    "NEUTRAL", "WILDERNESS", "UNDERGROUND", "WATER", "NAUTICAL", "TOWN",
    "DUNGEON", "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
    "DANGER", "TRIUMPH"
};

static int classify_row(const CorpusRoom* r) {
    room_class_reset();
    room_class_note_title(r->title);
    return text_classify_room(r->text);
}
/* Task 6 replaces the reset in this helper with a room_class_set_game call, so
   each corpus room is judged under its own story's genre. Until then reset is
   what keeps one row's title from leaking into the next. */

static int bless(void) {
    printf("/*----------------------\n");
    printf(" | blessed.inc\n");
    printf(" | Description: GENERATED FILE -- do not edit by hand; produced by\n");
    printf(" |   room_class_test --bless. The agreed category for every row of\n");
    printf(" |   rooms.inc, in corpus order. Regenerating it is how a deliberate\n");
    printf(" |   change of judgement is recorded; the diff is the review.\n");
    printf(" | Author: suinevere\n");
    printf(" ----------------------*/\n");
    printf("static const unsigned char BLESSED[CORPUS_N] = {\n");
    for (int i = 0; i < CORPUS_N; i++)
        printf("    %d,   /* %s: %s */\n", classify_row(&CORPUS[i]),
               CORPUS[i].serial, CORPUS[i].title);
    printf("};\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--bless") == 0) return bless();
    return run_suites();
}
```

Note `run_suites()` is defined in Step 3. For this step, temporarily define it as `static int run_suites(void) { return 0; }` above `main` so `--bless` can run.

- [ ] **Step 2: Build and bless against the UNCHANGED classifier**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c
/tmp/rct --bless > test/corpus/blessed.inc
wc -l test/corpus/blessed.inc
```

Expected: line count is the corpus row count plus 9. **Do not proceed if any scoring change has already been made** — check `git log --oneline -3` shows only the Task 1 move and the Task 2 corpus.

- [ ] **Step 3: Replace the stub with the real snapshot suite**

```c
static int snapshot(void) {
    int fails = 0;
    for (int i = 0; i < CORPUS_N; i++) {
        int got = classify_row(&CORPUS[i]);
        if (got != BLESSED[i]) {
            printf("  %-8s %-40s %s -> %s\n", CORPUS[i].serial, CORPUS[i].title,
                   CAT_NAME[BLESSED[i]], CAT_NAME[got]);
            fails++;
        }
    }
    if (fails)
        printf("SNAPSHOT: %d of %d rooms changed verdict.\n"
               "  Review each line above. If every change is intended:\n"
               "    /tmp/rct --bless > test/corpus/blessed.inc\n", fails, CORPUS_N);
    else
        printf("SNAPSHOT: OK (%d rooms unchanged)\n", CORPUS_N);
    return fails;
}

static int run_suites(void) {
    int fails = snapshot();
    return fails ? 1 : 0;
}
```

Add `#include "corpus/blessed.inc"` immediately after the `rooms.inc` include.

- [ ] **Step 4: Run — must be green against the unchanged classifier**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `SNAPSHOT: OK (N rooms unchanged)`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add test/room_class_test.c test/corpus/blessed.inc
git commit -m "test: bless the room-classification snapshot against current behaviour"
```

---

## Task 4: Keyword tiers and dominant scoring

**Files:**
- Modify: `saturn/src/classify/room_class.h` (add `KT_*`, extend `TextKeyword`)
- Modify: `saturn/src/classify/room_class_data.c` (tier every `KW[]` row)
- Modify: `saturn/src/classify/room_class.c` (`text_classify_room`)
- Modify: `test/room_class_test.c` (assertion suite)
- Modify: `test/corpus/blessed.inc` (re-blessed after review)

**Interfaces:**
- Consumes: everything from Tasks 1–3.
- Produces: `KT_STRUCTURE`/`KT_BIOME`/`KT_FEATURE`, `KT_NUM_TIERS`, and a `TextKeyword` with a `tier` field. Task 6 adds a `genre` field to the same struct.

- [ ] **Step 1: Write the failing assertion suite**

Add to `test/room_class_test.c` above `run_suites`:

```c
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

/* Deliberately does NOT call room_class_reset: from Task 6 that clears the
   resolved genre, which would undo the room_class_set_game the genre assertions
   make just before calling here. note_title always overwrites, so there is
   nothing a reset would add. */
static int classify(const char* title, const char* text) {
    room_class_note_title(title);
    return text_classify_room(text);
}

static int assertions(void) {
    int fails = 0;

    /* snapshot() runs first and leaves whatever genre the last corpus row
       resolved. Clear it so these cases start from a known state. */
    room_class_reset();

    /* A lake in a cave is a cave. Structure outranks Biome outranks Feature, so
       no count of scenery can overturn the thing the player is standing in. */
    CHECK(classify("Cave",
        "You are in a damp cave. A lake stretches away below, and a forest is "
        "visible far above through a crack.") == TC_UNDERGROUND);

    /* Features cannot outvote a Biome however many of them there are -- this is
       the case additive weights got wrong at four features and up. */
    CHECK(classify("Clearing",
        "A tree leans over a boulder beside a still pool. A rug of moss covers "
        "the ground and a desk rots against a stump.") == TC_WILDERNESS);

    /* The title can no longer promote a Feature past a Structure. Under flat
       counting a weighted title word simply won; now the tier decides first. */
    CHECK(classify("Forest Path", "A cave.") == TC_UNDERGROUND);

    if (!fails) printf("ASSERTIONS: OK\n");
    return fails;
}
```

Change `run_suites` to `int fails = snapshot() + assertions();`.

- [ ] **Step 2: Run to verify the assertions fail**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `SNAPSHOT: OK` but three `FAIL line ...` lines from `assertions`, exit 1.

- [ ] **Step 3: Add tiers to the interface**

In `room_class.h`, above `TextKeyword`:

```c
/*----------------------
 | KT_* / KT_NUM_TIERS
 | Description: How reliably a keyword names the room itself. Structure is what
 |   the player is standing in (cave, house, ship); Biome is the general natural
 |   area (forest, desert, sea); Feature is a thing that sits inside one of those
 |   (tree, boulder, rug).
 |
 |   The tier is a property of the WORD, not of the category it votes for -- both
 |   "cave" and "house" are Structure though they vote for different moods. It is
 |   compared before any count, so one Structure hit beats any number of Biome
 |   hits: a lake in a cave is a cave, by construction rather than by tuning a
 |   weight until it happens to come out right.
 | Author: suinevere
 ----------------------*/
enum { KT_STRUCTURE = 0, KT_BIOME = 1, KT_FEATURE = 2 };
#define KT_NUM_TIERS 3
```

And extend the row:

```c
typedef struct {
    const char*   word;
    unsigned char cat;
    unsigned char tier;
} TextKeyword;
```

- [ ] **Step 4: Tier every keyword**

In `room_class_data.c`, add a tier to each `KW[]` row. Structure is anything the player occupies; Biome is an outdoor expanse; Feature is contents. `EV[]` rows all take `KT_FEATURE` — events are never tiered, and the field is unused there.

```c
static const TextKeyword KW[] = {
    {"forest",TC_WILDERNESS,KT_BIOME},{"tree",TC_WILDERNESS,KT_FEATURE},
    {"trees",TC_WILDERNESS,KT_FEATURE},{"woods",TC_WILDERNESS,KT_BIOME},
    {"grove",TC_WILDERNESS,KT_BIOME},{"meadow",TC_WILDERNESS,KT_BIOME},
    {"field",TC_WILDERNESS,KT_BIOME},{"clearing",TC_WILDERNESS,KT_BIOME},
    {"path",TC_WILDERNESS,KT_FEATURE},{"hill",TC_WILDERNESS,KT_BIOME},
    {"mountain",TC_WILDERNESS,KT_BIOME},{"garden",TC_WILDERNESS,KT_BIOME},

    {"cave",TC_UNDERGROUND,KT_STRUCTURE},{"cavern",TC_UNDERGROUND,KT_STRUCTURE},
    {"tunnel",TC_UNDERGROUND,KT_STRUCTURE},{"underground",TC_UNDERGROUND,KT_BIOME},
    {"cellar",TC_UNDERGROUND,KT_STRUCTURE},{"mine",TC_UNDERGROUND,KT_STRUCTURE},
    {"passage",TC_UNDERGROUND,KT_STRUCTURE},{"grotto",TC_UNDERGROUND,KT_STRUCTURE},
    {"crawlway",TC_UNDERGROUND,KT_STRUCTURE},

    {"river",TC_WATER,KT_BIOME},{"stream",TC_WATER,KT_BIOME},
    {"lake",TC_WATER,KT_BIOME},{"pool",TC_WATER,KT_FEATURE},
    {"water",TC_WATER,KT_FEATURE},{"waterfall",TC_WATER,KT_FEATURE},
    {"shore",TC_WATER,KT_BIOME},{"bank",TC_WATER,KT_FEATURE},
    {"underwater",TC_WATER,KT_BIOME},{"flooded",TC_WATER,KT_FEATURE},

    {"ship",TC_NAUTICAL,KT_STRUCTURE},{"boat",TC_NAUTICAL,KT_STRUCTURE},
    {"deck",TC_NAUTICAL,KT_STRUCTURE},{"cabin",TC_NAUTICAL,KT_STRUCTURE},
    {"hull",TC_NAUTICAL,KT_STRUCTURE},{"sea",TC_NAUTICAL,KT_BIOME},
    {"ocean",TC_NAUTICAL,KT_BIOME},{"dock",TC_NAUTICAL,KT_STRUCTURE},
    {"harbor",TC_NAUTICAL,KT_BIOME},{"sail",TC_NAUTICAL,KT_FEATURE},
    {"mast",TC_NAUTICAL,KT_FEATURE},{"submarine",TC_NAUTICAL,KT_STRUCTURE},

    {"house",TC_HOUSE,KT_STRUCTURE},{"kitchen",TC_HOUSE,KT_STRUCTURE},
    {"parlor",TC_HOUSE,KT_STRUCTURE},{"bedroom",TC_HOUSE,KT_STRUCTURE},
    {"attic",TC_HOUSE,KT_STRUCTURE},{"cottage",TC_HOUSE,KT_STRUCTURE},
    {"farmhouse",TC_HOUSE,KT_STRUCTURE},{"porch",TC_HOUSE,KT_STRUCTURE},

    {"town",TC_TOWN,KT_BIOME},{"village",TC_TOWN,KT_BIOME},
    {"street",TC_TOWN,KT_BIOME},{"building",TC_TOWN,KT_STRUCTURE},
    {"hall",TC_TOWN,KT_STRUCTURE},{"office",TC_TOWN,KT_STRUCTURE},
    {"stairs",TC_TOWN,KT_FEATURE},{"square",TC_TOWN,KT_BIOME},
    {"market",TC_TOWN,KT_BIOME},{"shop",TC_TOWN,KT_STRUCTURE},
    {"inn",TC_TOWN,KT_STRUCTURE},{"tavern",TC_TOWN,KT_STRUCTURE},

    {"temple",TC_DUNGEON,KT_STRUCTURE},{"tomb",TC_DUNGEON,KT_STRUCTURE},
    {"crypt",TC_DUNGEON,KT_STRUCTURE},{"ruin",TC_DUNGEON,KT_STRUCTURE},
    {"altar",TC_DUNGEON,KT_FEATURE},{"ancient",TC_DUNGEON,KT_FEATURE},
    {"chamber",TC_DUNGEON,KT_STRUCTURE},{"dungeon",TC_DUNGEON,KT_STRUCTURE},
    {"catacomb",TC_DUNGEON,KT_STRUCTURE},{"vault",TC_DUNGEON,KT_STRUCTURE},

    {"desert",TC_DESERT,KT_BIOME},{"sand",TC_DESERT,KT_FEATURE},
    {"dune",TC_DESERT,KT_BIOME},{"oasis",TC_DESERT,KT_BIOME},
    {"wasteland",TC_DESERT,KT_BIOME},

    {"spell",TC_MAGIC,KT_FEATURE},{"magic",TC_MAGIC,KT_FEATURE},
    {"enchant",TC_MAGIC,KT_FEATURE},{"wizard",TC_MAGIC,KT_FEATURE},
    {"scroll",TC_MAGIC,KT_FEATURE},{"rune",TC_MAGIC,KT_FEATURE},
    {"mystic",TC_MAGIC,KT_FEATURE},{"sorcerer",TC_MAGIC,KT_FEATURE},

    {"console",TC_SCIFI,KT_FEATURE},{"computer",TC_SCIFI,KT_FEATURE},
    {"airlock",TC_SCIFI,KT_STRUCTURE},{"panel",TC_SCIFI,KT_FEATURE},
    {"robot",TC_SCIFI,KT_FEATURE},{"laboratory",TC_SCIFI,KT_STRUCTURE},
    {"reactor",TC_SCIFI,KT_STRUCTURE},{"corridor",TC_SCIFI,KT_STRUCTURE},
    {"module",TC_SCIFI,KT_STRUCTURE},{"cockpit",TC_SCIFI,KT_STRUCTURE},

    {"corpse",TC_HORROR,KT_FEATURE},{"rotting",TC_HORROR,KT_FEATURE},
    {"stench",TC_HORROR,KT_FEATURE},{"shadow",TC_HORROR,KT_FEATURE},
    {"eerie",TC_HORROR,KT_FEATURE},{"decay",TC_HORROR,KT_FEATURE},
    {"skeleton",TC_HORROR,KT_FEATURE},

    {"body",TC_MYSTERY,KT_FEATURE},{"clue",TC_MYSTERY,KT_FEATURE},
    {"murder",TC_MYSTERY,KT_FEATURE},{"evidence",TC_MYSTERY,KT_FEATURE},
    {"study",TC_MYSTERY,KT_STRUCTURE},{"library",TC_MYSTERY,KT_STRUCTURE},
    {"detective",TC_MYSTERY,KT_FEATURE},{"locked",TC_MYSTERY,KT_FEATURE},
};
```

Keep the existing `/* A house is not a town... */` and `/* Left in TC_TOWN... */` comment blocks where they sit.

- [ ] **Step 5: Replace the scoring in `text_classify_room`**

```c
/*----------------------
 | tier_wins
 | Description: True when hit vector `a` outranks `b`. Comparison is
 |   lexicographic over the tiers, so Structure decides before Biome, which
 |   decides before Feature: the better tier wins outright at any count, and a
 |   tie at one tier falls to the next tier down. Equal vectors lose, which is
 |   what leaves an exact tie to the caller's enum order.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- per-tier hit counts, KT_NUM_TIERS entries each
 | Returns: 1 when a outranks b, 0 otherwise
 ----------------------*/
static int tier_wins(const unsigned short* a, const unsigned short* b) {
    for (int t = 0; t < KT_NUM_TIERS; t++)
        if (a[t] != b[t]) return a[t] > b[t];
    return 0;
}

/*----------------------
 | text_classify_room
 | Description: Scores room text against the keyword table and returns the
 |   winning category, or TC_NEUTRAL when nothing matched. Hits are counted per
 |   tier rather than summed, and the tiers are compared in order, so a keyword
 |   that names the room the player is in beats any amount of scenery mentioned
 |   inside it. Title words count for TEXT_TITLE_WEIGHT extra WITHIN their tier,
 |   so a weighted title can break a tie but can no longer promote a Feature past
 |   a Structure.
 | Author: suinevere
 | Dependencies: room_class.h (text_keywords, TC_*, KT_*)
 | Globals: g_room_title
 | Params: text -- the turn's text, opening with the room title (NULL -> NEUTRAL)
 | Returns: the winning TC_* category
 ----------------------*/
int text_classify_room(const char* text) {
    if (!text) return TC_NEUTRAL;
    char firstline[TEXT_TITLE_MAX];
    const char* title;
    unsigned short hits[TEXT_NUM_CATEGORIES][KT_NUM_TIERS];
    int c, t, i;

    for (c = 0; c < TEXT_NUM_CATEGORIES; c++)
        for (t = 0; t < KT_NUM_TIERS; t++) hits[c][t] = 0;

    if (g_room_title[0] != 0) {
        title = g_room_title;
    } else {
        text_room_title(text, firstline);
        title = firstline;
    }

    int nk = 0; const TextKeyword* kw = text_keywords(&nk);
    for (i = 0; i < nk; i++) {
        if (has_word(text,  kw[i].word)) hits[kw[i].cat][kw[i].tier]++;
        if (has_word(title, kw[i].word)) hits[kw[i].cat][kw[i].tier] += TEXT_TITLE_WEIGHT;
    }

    /* Starts past TC_NEUTRAL on purpose: it is the nothing-matched answer, not
       something a keyword can vote for. An exact vector tie falls to the lowest
       id, which is what the strict tier_wins gives. */
    int best = TC_NEUTRAL;
    unsigned short none[KT_NUM_TIERS] = {0, 0, 0};
    const unsigned short* bestv = none;
    for (c = TC_WILDERNESS; c <= TC_PLACE_LAST; c++)
        if (tier_wins(hits[c], bestv)) { bestv = hits[c]; best = c; }
    return best;
}
```

- [ ] **Step 6: Run — assertions pass, snapshot shows a reviewable diff**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `ASSERTIONS: OK`, and a `SNAPSHOT:` block listing rooms that changed verdict.

- [ ] **Step 7: Review every snapshot line before re-blessing**

Read each `old -> new` line and decide whether the new verdict is better. Expect to see rooms that mention distant scenery move to the structure they are actually in. **If a change looks wrong, fix the tier assignment rather than blessing it.** This review is the deliverable of the task; blessing without reading it discards the entire value of the corpus.

- [ ] **Step 8: Re-bless and confirm green**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
/tmp/rct --bless > test/corpus/blessed.inc
/tmp/rct
```

Expected: `SNAPSHOT: OK`, `ASSERTIONS: OK`, exit 0.

- [ ] **Step 9: Check the Saturn unit still compiles**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/85376027-4922-4225-8ab7-2f9ed45c05ac/scratchpad"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
"../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound \
    -I src/classify -o "$SP/room_class.o" src/classify/room_class.c
```

Expected: no output.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/classify test/room_class_test.c test/corpus/blessed.inc
git commit -m "classify: rank keywords by tier so structure outranks biome outranks feature"
```

---

## Task 5: Sentence-scoped spatial modifiers

**Files:**
- Modify: `saturn/src/classify/room_class.c` (bounded matching, sentence walk)
- Modify: `saturn/src/classify/room_class_data.c` (`NEG[]`, `POS[]` + accessors)
- Modify: `saturn/src/classify/room_class.h` (declare the accessors)
- Modify: `test/room_class_test.c`, `test/corpus/blessed.inc`

**Interfaces:**
- Consumes: `KT_*`, `TextKeyword` with `tier`, `tier_wins` from Task 4.
- Produces: `const char* const* text_neg_phrases(int* n)` and `const char* const* text_pos_phrases(int* n)`.

- [ ] **Step 1: Write the failing assertions**

Add to `assertions()` in `test/room_class_test.c`:

```c
    /* A landmark named as distant does not get to describe the room. The whole
       sentence is discarded, because "far off" attaches to the landmark and not
       to any particular word next to it. */
    CHECK(classify("Ledge",
        "You are on a narrow ledge. Far off, a forest covers the valley floor.")
        == TC_NEUTRAL);

    /* ...and the same sentence without the modifier still votes. */
    CHECK(classify("Ledge",
        "You are on a narrow ledge. A forest covers the valley floor.")
        == TC_WILDERNESS);

    /* A positive phrase doubles its sentence's hits, which breaks a tie WITHIN a
       tier -- it cannot promote a Feature past a Structure. */
    CHECK(classify("Junction",
        "A tunnel leads north. You are in a large cavern.") == TC_UNDERGROUND);
    CHECK(classify("Junction",
        "You are in a room with a rug. A cave opens to the north.")
        == TC_UNDERGROUND);
```

- [ ] **Step 2: Run to verify they fail**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: the first assertion fails (the forest currently wins), exit 1.

- [ ] **Step 3: Add the phrase tables**

In `room_class_data.c`:

```c
/*----------------------
 | NEG / POS
 | Description: Spatial modifiers, lowercase. A sentence containing a NEG phrase
 |   describes something the player can see but is not in, so its keyword hits
 |   are discarded outright. A sentence containing a POS phrase names the room
 |   itself, so its hits count double.
 |
 |   Both lists are deliberately short. "you can see" is NOT here despite being a
 |   distance idiom: it introduces genuinely present objects far more often than
 |   distant ones, and discarding those sentences would cost more than the
 |   occasional distant landmark it catches.
 | Author: suinevere
 ----------------------*/
static const char* const NEG[] = {
    "in the distance", "far off", "through the window", "painted on",
    "on the horizon", "beyond the", "you can hear",
};
static const char* const POS[] = {
    "you are in", "you are standing", "you stand", "this is a",
    "you find yourself", "you are on",
};

/*----------------------
 | text_neg_phrases / text_pos_phrases
 | Description: Hand back the negative / positive spatial-modifier tables and
 |   their lengths.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: NEG, POS
 | Params: n -- receives the table length
 | Returns: the table
 ----------------------*/
const char* const* text_neg_phrases(int* n) { *n = (int)(sizeof NEG / sizeof NEG[0]); return NEG; }
const char* const* text_pos_phrases(int* n) { *n = (int)(sizeof POS / sizeof POS[0]); return POS; }
```

Declare both in `room_class.h` beside `text_keywords`.

- [ ] **Step 4: Add bounded matching helpers to `room_class.c`**

`has_word` gains a length-bounded sibling so a sentence can be scored in place. Copying each sentence to a stack buffer would cost `MUSIC_TEXT_MAX` bytes of Saturn stack for no benefit.

```c
/*----------------------
 | has_word_n
 | Description: Case-insensitive whole-word search within the first `len` bytes
 |   of `text`, so "cave" does not match "caverns". The bounded form exists so a
 |   single sentence can be scored without copying it out of the turn buffer.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- haystack; len -- how much of it to search; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
static int has_word_n(const char* text, int len, const char* word) {
    int wl = (int) strlen(word), p, i;
    for (p = 0; p + wl <= len; p++) {
        i = 0;
        while (i < wl && lc(text[p + i]) == word[i]) i++;
        if (i == wl) {
            char before = (p == 0) ? ' ' : text[p - 1];
            char after  = (p + wl < len) ? text[p + wl] : ' ';
            int lb = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z'));
            int la = !((after  >= 'a' && after  <= 'z') || (after  >= 'A' && after  <= 'Z'));
            if (lb && la) return 1;
        }
    }
    return 0;
}

/*----------------------
 | has_phrase_n
 | Description: Case-insensitive substring search within the first `len` bytes of
 |   `text`. Not word-bounded, because the modifier phrases contain spaces and
 |   are matched as written.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: text -- haystack; len -- how much of it to search; phrase -- lowercase needle
 | Returns: 1 on a match, 0 otherwise
 ----------------------*/
static int has_phrase_n(const char* text, int len, const char* phrase) {
    int pl = (int) strlen(phrase), p, i;
    for (p = 0; p + pl <= len; p++) {
        i = 0;
        while (i < pl && lc(text[p + i]) == phrase[i]) i++;
        if (i == pl) return 1;
    }
    return 0;
}

/*----------------------
 | sentence_weight
 | Description: What one sentence's keyword hits are worth: 0 when it names
 |   something distant, 2 when it names the room the player is in, 1 otherwise.
 | Author: suinevere
 | Dependencies: room_class.h (text_neg_phrases, text_pos_phrases)
 | Globals: N/A
 | Params: s -- the sentence; len -- its length
 | Returns: the multiplier
 ----------------------*/
static int sentence_weight(const char* s, int len) {
    int n = 0, i;
    const char* const* p = text_neg_phrases(&n);
    for (i = 0; i < n; i++) if (has_phrase_n(s, len, p[i])) return 0;
    p = text_pos_phrases(&n);
    for (i = 0; i < n; i++) if (has_phrase_n(s, len, p[i])) return 2;
    return 1;
}
```

Redefine the original as `static int has_word(const char* text, const char* word) { return has_word_n(text, (int) strlen(text), word); }` so `text_scan_event` is untouched.

- [ ] **Step 5: Walk sentences in `text_classify_room`**

Replace the single `for (i = 0; i < nk; i++)` text pass with a sentence walk. The title pass is unchanged.

```c
    int nk = 0; const TextKeyword* kw = text_keywords(&nk);
    int start = 0, pos = 0;
    for (;;) {
        char ch = text[pos];
        if (ch == 0 || ch == '.' || ch == '!' || ch == '?') {
            int len = pos - start;
            if (len > 0) {
                int w = sentence_weight(text + start, len);
                if (w > 0)
                    for (i = 0; i < nk; i++)
                        if (has_word_n(text + start, len, kw[i].word))
                            hits[kw[i].cat][kw[i].tier] += (unsigned short) w;
            }
            if (ch == 0) break;
            start = pos + 1;
        }
        pos++;
    }
    for (i = 0; i < nk; i++)
        if (has_word(title, kw[i].word)) hits[kw[i].cat][kw[i].tier] += TEXT_TITLE_WEIGHT;
```

Update the function's comment block to name the sentence scoping in its Description.

- [ ] **Step 6: Run, review the snapshot diff, re-bless**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

Expected: `ASSERTIONS: OK` and a snapshot diff. Read every line. Then:

```bash
/tmp/rct --bless > test/corpus/blessed.inc && /tmp/rct
```

Expected: both suites OK, exit 0.

- [ ] **Step 7: Check the Saturn unit compiles and commit**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/85376027-4922-4225-8ab7-2f9ed45c05ac/scratchpad"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
"../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound \
    -I src/classify -o "$SP/room_class.o" src/classify/room_class.c
cd /c/Users/saggl/CLionProjects/zaturn
git add saturn/src/classify test/room_class_test.c test/corpus/blessed.inc
git commit -m "classify: score each sentence separately so distant scenery cannot describe the room"
```

---

## Task 6: Genre masks, per-game table, inference and cache flush

**Files:**
- Modify: `saturn/src/classify/room_class.h` (`GN_*`, `genre` field, lifecycle calls)
- Modify: `saturn/src/classify/room_class_data.c` (split ambiguous keywords, `GENRE_KW[]`, `GAME_GENRE[]`)
- Modify: `saturn/src/classify/room_class.c` (genre gate, inference, lock)
- Modify: `saturn/src/sound/music.c` (`music_set_game` forwards; cache flush on lock)
- Modify: `test/room_class_test.c`, `test/corpus/blessed.inc`

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: `GN_FANTASY`/`GN_SCIFI`/`GN_MODERN`/`GN_ANY`, `void room_class_set_game(unsigned int release, const char* serial)`, `int room_class_genre_locked(void)`.

- [ ] **Step 1: Write the failing assertions**

```c
    /* The bug this work exists for. The same word, two games, two answers. */
    room_class_set_game(17, "821021");            /* Starcross -- sci-fi */
    CHECK(classify("Airlock",
        "You are in the airlock of the ship. The hull curves away above you.")
        == TC_SCIFI);

    room_class_set_game(88, "840726");            /* Zork I -- fantasy */
    CHECK(classify("Dock",
        "A ship is moored here. Its hull is sound and the deck is clear.")
        == TC_NAUTICAL);

    /* An unknown game abstains on ambiguous words rather than guessing wrong.
       Nothing else in this text votes, so the answer must be NEUTRAL. */
    room_class_set_game(0, "000000");
    CHECK(classify("Somewhere",
        "A ship is here. Its hull is sound and the deck is clear.")
        == TC_NEUTRAL);

    /* ...and once markers resolve the genre, the same words vote again. */
    room_class_set_game(0, "000000");
    CHECK(room_class_genre_locked() == 0);
    classify("Bay", "An airlock leads out. A reactor hums. The computer is dark.");
    classify("Bay", "The airlock is sealed. A robot waits by the console.");
    classify("Bay", "A panel glows beside the airlock.");
    CHECK(room_class_genre_locked() == 1);
    CHECK(classify("Hold", "The ship's hull creaks.") == TC_SCIFI);

    /* Seastalker is undersea but not a space game, so the same words resolve the
       other way. This is the assertion that stops the sci-fi fix from simply
       moving every ship to SCIFI and calling it done. */
    room_class_set_game(16, "850603");            /* Seastalker -- modern */
    CHECK(classify("Sub Bay",
        "You are in the cabin of the submarine. The hull is scarred.")
        == TC_NAUTICAL);

    /* Infidel's desert, where Feature-tier objects are strewn through every
       description and must not overturn the Biome the room actually is. */
    room_class_set_game(22, "830916");            /* Infidel -- modern */
    CHECK(classify("Desert",
        "You stand in the desert. A boulder casts a shadow and a scroll lies "
        "half-buried beside a rotting sack.") == TC_DESERT);
```

- [ ] **Step 2: Run to verify they fail**

Expected: the Starcross and abstention assertions fail, exit 1.

- [ ] **Step 3: Add the genre interface**

In `room_class.h`, above `TextKeyword`:

```c
/*----------------------
 | GN_* / GN_ANY
 | Description: Which genres a keyword votes in, as a bitmask. Most words mean
 |   the same thing everywhere and carry GN_ANY; a handful do not, and get one
 |   row per reading. "ship" is TC_NAUTICAL in a fantasy or period game and
 |   TC_SCIFI in a space one, which no single-answer table could express.
 |
 |   While a game's genre is unresolved, a row whose mask is not GN_ANY does not
 |   vote at all. Abstaining leaves the room to its other evidence; guessing puts
 |   sailing-ship art on a starship.
 | Author: suinevere
 ----------------------*/
#define GN_FANTASY 0x01
#define GN_SCIFI   0x02
#define GN_MODERN  0x04
#define GN_ANY     0xFF

typedef struct {
    const char*   word;
    unsigned char cat;
    unsigned char tier;
    unsigned char genre;
} TextKeyword;
```

And beside `room_class_reset`:

```c
/*----------------------
 | room_class_set_game / room_class_genre_locked
 | Description: set_game identifies the loaded story so its authored genre can be
 |   looked up; an unlisted game starts unresolved and infers instead.
 |   genre_locked reports whether the genre has settled, which is the signal to
 |   discard any category memoized while ambiguous words were still abstaining.
 | Author: suinevere
 ----------------------*/
void room_class_set_game(unsigned int release, const char* serial);
int  room_class_genre_locked(void);
```

- [ ] **Step 4: Split the ambiguous keywords and add the genre tables**

In `room_class_data.c`, add `GN_ANY` as the fourth field of every existing `KW[]` and `EV[]` row, then replace the nautical and sci-fi blocks' ambiguous words with per-genre pairs:

```c
    /* The words that mean two different things. Each reading is its own row, so
       the table stays declarative and the classifier needs no special cases. */
    {"ship",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"ship",TC_SCIFI,   KT_STRUCTURE,GN_SCIFI},
    {"deck",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"deck",TC_SCIFI,   KT_STRUCTURE,GN_SCIFI},
    {"cabin",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"cabin",TC_SCIFI,   KT_STRUCTURE,GN_SCIFI},
    {"hull",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"hull",TC_SCIFI,   KT_STRUCTURE,GN_SCIFI},
    {"hall",TC_TOWN,    KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"hall",TC_SCIFI,   KT_STRUCTURE,GN_SCIFI},
    {"study",TC_MYSTERY,KT_STRUCTURE,GN_MODERN},
    {"study",TC_HOUSE,  KT_STRUCTURE,GN_FANTASY},
    {"panel",TC_SCIFI,  KT_FEATURE,  GN_SCIFI|GN_MODERN},
    {"chamber",TC_DUNGEON,KT_STRUCTURE,GN_FANTASY},
    {"chamber",TC_SCIFI,  KT_STRUCTURE,GN_SCIFI},
    {"module",TC_SCIFI,   KT_STRUCTURE,GN_SCIFI},
```

Remove the single-reading rows those replace. Then:

```c
/*----------------------
 | GENRE_KW
 | Description: Marker words that identify a game's genre, for a story with no
 |   GAME_GENRE row. Deliberately narrow: a marker only earns its place if it is
 |   near-impossible in the other genres.
 | Author: suinevere
 ----------------------*/
static const GenreKeyword GENRE_KW[] = {
    {"airlock",GN_SCIFI},{"hyperspace",GN_SCIFI},{"android",GN_SCIFI},
    {"spacesuit",GN_SCIFI},{"reactor",GN_SCIFI},{"robot",GN_SCIFI},
    {"spell",GN_FANTASY},{"elf",GN_FANTASY},{"troll",GN_FANTASY},
    {"wizard",GN_FANTASY},{"sorcerer",GN_FANTASY},{"scroll",GN_FANTASY},
    {"telephone",GN_MODERN},{"elevator",GN_MODERN},{"automobile",GN_MODERN},
    {"cigarette",GN_MODERN},{"newspaper",GN_MODERN},
};

/*----------------------
 | GAME_GENRE
 | Description: The authored genre of each shipped story, keyed by Z-header
 |   release number and 6-char serial -- the same key SOLUTIONS[] and
 |   text_game_room_category use.
 |
 |   The two Infocom Samplers are deliberately absent. A sampler carries excerpts
 |   of several games and genuinely changes genre partway through, so inference
 |   describes it better than any single tag could.
 | Author: suinevere
 ----------------------*/
typedef struct { unsigned short release; const char* serial; unsigned char genre; } GameGenre;
static const GameGenre GAME_GENRE[] = {
    {   1, "151001", GN_FANTASY },  /* Adventure           */
    {  97, "851218", GN_MODERN  },  /* Ballyhoo            */
    {  23, "840809", GN_MODERN  },  /* Cutthroats          */
    {  27, "831005", GN_MODERN  },  /* Deadline            */
    {  29, "860820", GN_FANTASY },  /* Enchanter           */
    {  59, "851108", GN_SCIFI   },  /* Hitchhiker's Guide  */
    {  37, "861215", GN_MODERN  },  /* Hollywood Hijinx    */
    {  11, "870225", GN_MODERN  },  /* Hypochondriac       */
    {  22, "830916", GN_MODERN  },  /* Infidel             */
    {  59, "860730", GN_SCIFI   },  /* Leather Goddesses   */
    { 219, "870912", GN_MODERN  },  /* The Lurking Horror  */
    {   9, "861022", GN_MODERN  },  /* Moonmist            */
    {   2, "840207", GN_FANTASY },  /* Mini-Zork I         */
    {  34, "871124", GN_FANTASY },  /* Mini-Zork I         */
    {   2, "871123", GN_FANTASY },  /* Mini-Zork II        */
    {  26, "870730", GN_MODERN  },  /* Plundered Hearts    */
    {  37, "851003", GN_SCIFI   },  /* Planetfall          */
    {  16, "850603", GN_MODERN  },  /* Seastalker          */
    {  15, "851108", GN_FANTASY },  /* Sorcerer            */
    {  87, "860904", GN_FANTASY },  /* Spellbreaker        */
    {  17, "821021", GN_SCIFI   },  /* Starcross           */
    { 107, "870430", GN_SCIFI   },  /* Stationfall         */
    {  14, "841005", GN_MODERN  },  /* Suspect             */
    {   8, "840521", GN_SCIFI   },  /* Suspended           */
    {  69, "850920", GN_FANTASY },  /* Wishbringer         */
    {  22, "840924", GN_MODERN  },  /* The Witness         */
    {  88, "840726", GN_FANTASY },  /* Zork I              */
    {  48, "840904", GN_FANTASY },  /* Zork II             */
    {  17, "840727", GN_FANTASY },  /* Zork III            */
};

/*----------------------
 | text_genre_keywords / text_game_genre
 | Description: genre_keywords hands back the marker table and its length;
 |   game_genre returns a story's authored genre mask, or 0 when it is not
 |   listed and inference should run instead.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GENRE_KW, GAME_GENRE
 | Params: n -- receives the marker count; release/serial -- identify the story
 | Returns: the table / the genre mask (0 = unlisted)
 ----------------------*/
const GenreKeyword* text_genre_keywords(int* n) {
    *n = (int)(sizeof GENRE_KW / sizeof GENRE_KW[0]); return GENRE_KW;
}
unsigned char text_game_genre(unsigned int release, const char* serial) {
    int i;
    if (!serial) return 0;
    for (i = 0; i < (int)(sizeof GAME_GENRE / sizeof GAME_GENRE[0]); i++)
        if (GAME_GENRE[i].release == release &&
            memcmp(GAME_GENRE[i].serial, serial, 6) == 0)
            return GAME_GENRE[i].genre;
    return 0;
}
```

Add `#include <string.h>` to `room_class_data.c`. `GameGenre` is local to that file, but `GenreKeyword` crosses the boundary — define it **only** in `room_class.h` (a second `typedef` in the data file is a redefinition error):

```c
/*----------------------
 | GenreKeyword
 | Description: One marker word and the single genre it identifies. Separate from
 |   TextKeyword because a marker votes for a genre, not for a mood.
 | Author: suinevere
 ----------------------*/
typedef struct { const char* word; unsigned char genre; } GenreKeyword;

const GenreKeyword* text_genre_keywords(int* n);
unsigned char       text_game_genre(unsigned int release, const char* serial);
```

- [ ] **Step 5: Gate keyword voting and infer the genre in `room_class.c`**

```c
/*----------------------
 | GENRE_LOCK_HITS / GENRE_LOCK_MARGIN / GENRE_LOCK_ROOMS
 | Description: When an inferred genre is settled enough to act on: the leader
 |   needs this many marker hits, this much of a lead over every other genre, and
 |   this many classified rooms behind it. Three conditions rather than one
 |   because a single sci-fi word in a fantasy game's opening room is common and
 |   locking on it would be worse than never locking at all.
 | Author: suinevere
 ----------------------*/
#define GENRE_LOCK_HITS   3
#define GENRE_LOCK_MARGIN 2
#define GENRE_LOCK_ROOMS  3

/*----------------------
 | genre state (g_genre .. g_genre_rooms)
 | Description: g_genre is the resolved mask (0 = unresolved); g_genre_locked is
 |   1 once it can be acted on and never clears within a session; g_genre_hits
 |   counts markers per genre for inference; g_genre_rooms counts classified
 |   rooms so a lock cannot happen on the strength of one.
 | Author: suinevere
 ----------------------*/
static unsigned char g_genre = 0;
static int g_genre_locked = 0;
static int g_genre_hits[3] = {0, 0, 0};
static int g_genre_rooms = 0;

/*----------------------
 | genre_slot
 | Description: The g_genre_hits index for a single-bit genre mask.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: mask -- GN_FANTASY, GN_SCIFI or GN_MODERN
 | Returns: 0..2, or -1 for anything else
 ----------------------*/
static int genre_slot(unsigned char mask) {
    if (mask == GN_FANTASY) return 0;
    if (mask == GN_SCIFI)   return 1;
    if (mask == GN_MODERN)  return 2;
    return -1;
}

/*----------------------
 | genre_accumulate
 | Description: Counts this room's genre markers and locks the inferred genre
 |   once the three conditions above are all met. A no-op once locked, and for a
 |   game whose genre was authored.
 | Author: suinevere
 | Dependencies: room_class.h (text_genre_keywords)
 | Globals: g_genre, g_genre_locked, g_genre_hits, g_genre_rooms
 | Params: text -- the room's text
 | Returns: N/A
 ----------------------*/
static void genre_accumulate(const char* text) {
    static const unsigned char MASK[3] = { GN_FANTASY, GN_SCIFI, GN_MODERN };
    int n = 0, i, lead = 0, second = 0, best = -1;
    if (g_genre_locked) return;
    const GenreKeyword* gk = text_genre_keywords(&n);
    for (i = 0; i < n; i++) {
        int s = genre_slot(gk[i].genre);
        if (s >= 0 && has_word(text, gk[i].word)) g_genre_hits[s]++;
    }
    g_genre_rooms++;
    for (i = 0; i < 3; i++) {
        if (g_genre_hits[i] > lead) { second = lead; lead = g_genre_hits[i]; best = i; }
        else if (g_genre_hits[i] > second) { second = g_genre_hits[i]; }
    }
    if (best >= 0 && g_genre_rooms >= GENRE_LOCK_ROOMS &&
        lead >= GENRE_LOCK_HITS && lead - second >= GENRE_LOCK_MARGIN) {
        g_genre = MASK[best];
        g_genre_locked = 1;
    }
}

/*----------------------
 | room_class_set_game
 | Description: Looks up the story's authored genre. A listed game resolves at
 |   load and never infers; an unlisted one starts unresolved with its counters
 |   cleared.
 | Author: suinevere
 | Dependencies: room_class.h (text_game_genre)
 | Globals: g_genre, g_genre_locked, g_genre_hits, g_genre_rooms
 | Params: release -- Z-machine release; serial -- 6-char game serial
 | Returns: N/A
 ----------------------*/
void room_class_set_game(unsigned int release, const char* serial) {
    int i;
    g_genre = text_game_genre(release, serial);
    g_genre_locked = g_genre ? 1 : 0;
    for (i = 0; i < 3; i++) g_genre_hits[i] = 0;
    g_genre_rooms = 0;
}

/*----------------------
 | room_class_genre_locked
 | Description: 1 once the genre can be acted on, which is the signal to discard
 |   any category memoized while ambiguous words were abstaining.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_genre_locked
 | Params: N/A
 | Returns: 1 when locked, 0 otherwise
 ----------------------*/
int room_class_genre_locked(void) { return g_genre_locked; }
```

Extend `room_class_reset` so a new story starts with no inherited genre:

```c
void room_class_reset(void) {
    int i;
    g_room_title[0] = 0;
    g_genre = 0;
    g_genre_locked = 0;
    g_genre_rooms = 0;
    for (i = 0; i < 3; i++) g_genre_hits[i] = 0;
}
```

Then gate every keyword hit. Place this macro in `room_class.c` immediately above `text_classify_room`:

```c
/*----------------------
 | KW_VOTES
 | Description: Whether a keyword row is allowed to vote right now: always when
 |   it means the same thing in every genre, otherwise only once the genre is
 |   resolved and matches. An ambiguous row abstains while unresolved, because a
 |   wrong confident vote costs more than no vote at all.
 | Author: suinevere
 ----------------------*/
#define KW_VOTES(k) ((k).genre == GN_ANY || (g_genre && ((k).genre & g_genre)))
```

Apply it in both passes — `if (KW_VOTES(kw[i]) && has_word_n(text + start, len, kw[i].word))` and `if (KW_VOTES(kw[i]) && has_word(title, kw[i].word))` — and call `genre_accumulate(text);` as the first statement after the NULL check in `text_classify_room`.

- [ ] **Step 6: Flush the room cache when an inferred genre locks**

In `saturn/src/sound/music.c`, `music_set_game` gains `room_class_set_game(release, serial);`, and `music_on_turn`'s classification branch becomes:

```c
        int base = text_game_room_category(g_release, g_serial, room);
        if (base < 0) {
            unsigned char cached = (room < 256) ? g_room_cache[room] : 0;
            if (cached) base = cached - 1;
            else { base = text_classify_room(g_turn_text); if (room < 256) g_room_cache[room] = (unsigned char)(base + 1); }
            /* Everything memoized before the genre settled was decided with the
               ambiguous words abstaining, so it is all suspect. Drop the lot and
               redo this room; the target comparison below then announces the new
               mood on its own. */
            if (!g_genre_was_locked && room_class_genre_locked()) {
                g_genre_was_locked = 1;
                for (int i = 0; i < 256; i++) g_room_cache[i] = 0;
                base = text_classify_room(g_turn_text);
                if (room < 256) g_room_cache[room] = (unsigned char)(base + 1);
            }
        }
```

Declare `static int g_genre_was_locked = 0;` in the engine state block, documented in that block's comment, and clear it in `music_reset()`.

- [ ] **Step 7: Run, review the snapshot diff, re-bless**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
```

The corpus rows carry a release and serial but `classify_row` does not yet use them. Update it so each room is classified under its own game's genre — this is what makes the Starcross rooms in the corpus resolve correctly:

```c
static int classify_row(const CorpusRoom* r) {
    room_class_reset();
    room_class_set_game(r->release, r->serial);
    room_class_note_title(r->title);
    return text_classify_room(r->text);
}
```

Then re-run, read every snapshot line, and re-bless:

```bash
/tmp/rct --bless > test/corpus/blessed.inc && /tmp/rct
```

Expected: both suites OK. Sci-fi ship interiors should have moved from `NAUTICAL` to `SCIFI` in the diff — that is the headline result of this whole plan.

- [ ] **Step 8: Check both Saturn units compile and commit**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/85376027-4922-4225-8ab7-2f9ed45c05ac/scratchpad"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
C="../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc"
$C -c -I src -I src/sound -I src/classify -o "$SP/room_class.o" src/classify/room_class.c
$C -c -I src -I src/sound -I src/classify -o "$SP/room_class_data.o" src/classify/room_class_data.c
$C -c -I src -I src/sound -I src/classify -o "$SP/music.o" src/sound/music.c
cd /c/Users/saggl/CLionProjects/zaturn
git add saturn/src/classify saturn/src/sound/music.c test/room_class_test.c test/corpus/blessed.inc
git commit -m "classify: resolve ambiguous keywords by game genre so a ship in space is not nautical"
```

---

## Task 7: Reconcile the existing tests and close out

**Files:**
- Modify: `test/music_category_test.c:265-277,311-315`
- Modify: `docs/superpowers/specs/2026-08-04-room-categorization-tiers-design.md:4`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing new.

- [ ] **Step 1: Run the existing category test and see what moved**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mct \
    test/music_category_test.c saturn/src/sound/music.c \
    saturn/src/sound/music_data.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/mct
```

Expected: three failures, at roughly lines 272, 277 and 313. All three are cases where a weighted title word could previously beat a higher-tier description word. Any *fourth* failure is unplanned — investigate it rather than editing the expectation.

- [ ] **Step 2: Update the three expectations and their comments**

At `test/music_category_test.c:265-274`, the truncation case. `"...Forest\nA cave."` — `forest` is Biome, `cave` is Structure, so the answer is no longer decided by enum order:

```c
    /* A title longer than TEXT_TITLE_MAX is cut, and a keyword past the cut gets
       no title bonus -- it still counts, but only as ordinary description text.
       "Forest" sits past character 64, so it scores once as Biome while "cave"
       scores once as Structure, and Structure decides before any count. Pinned
       because it is the visible edge of the truncation: no room title in these
       games is anywhere near this long, and if one ever is, this is what it will
       do rather than crash or read past the buffer. */
    CHECK(text_classify_room(
        "A Room With A Very Long Name Indeed That Runs Well Past Any Sensible "
        "Title Buffer And Then Says Forest\nA cave.") == TC_UNDERGROUND);
    /* The same keyword inside the cut still cannot win, which is the point of
       the tiers: a title names the room, but "cave" names a room harder than
       "forest" does. The title weight breaks ties within a tier, not across. */
    CHECK(text_classify_room("Forest Path\nA cave.") == TC_UNDERGROUND);
```

At `test/music_category_test.c:311-315`:

```c
        /* The title weight lives inside a tier. "Cellar" is Structure and
           "forest" is Biome, so the supplied name wins here where under flat
           counting two agreeing description words beat it. This is the
           deliberate weakening described in the tier design: a title that names
           a real place should not lose to the scenery visible from it. */
        room_class_note_title("Cellar");
        CHECK(text_classify_room("Forest\nThis is a forest, with trees all around.")
              == TC_UNDERGROUND);
```

- [ ] **Step 3: Run the full host suite**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mct \
    test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c \
    saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c && /tmp/mct
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mt \
    test/music_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c \
    saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c && /tmp/mt
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
python saturn/tests/test_category_art.py
python saturn/tests/test_netbin_sources.py
```

Expected: all five green.

- [ ] **Step 4: Mark the spec implemented**

Change line 4 of `docs/superpowers/specs/2026-08-04-room-categorization-tiers-design.md` to `**Status:** Implemented 2026-08-05`.

- [ ] **Step 5: Commit**

```bash
git add test/music_category_test.c docs/superpowers/specs/2026-08-04-room-categorization-tiers-design.md
git commit -m "test: retune the three category expectations that tiering deliberately changes"
```

- [ ] **Step 6: Hand back for a Saturn build**

Report to the user that every host test is green and the changed units cross-compile, and ask them to run `saturn/compile.bat` and check the in-game wallpaper on a sci-fi story. The corpus proves the verdicts; only a real build proves the ROM still links and fits.
