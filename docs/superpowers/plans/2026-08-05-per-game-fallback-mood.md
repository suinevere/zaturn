# Per-Game Fallback Mood Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After three consecutive rooms whose text classifies as `TC_NEUTRAL`, show the loaded game's authored default mood instead of holding the previous wallpaper.

**Architecture:** A `fallback` column is added to the existing `GAME_GENRE` table in `saturn/src/classify/room_class_data.c` and read through a new accessor. The substitution itself happens in `saturn/src/sound/music.c`, which already owns room-to-room state — the classifier is not touched at all, which is what makes the corpus snapshot a free regression gate.

**Tech Stack:** C89-compatible C (SH-2 cross-compiler via `make`), host `gcc` for tests, Python 3 for the asset-table test.

## Global Constraints

- **Comment style is mandatory.** Every file, function and constant gets the `/*---- | name | Description: | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----*/` block, with `N/A` for fields that do not apply. **Tests and generated files get a FILE header only**, never per-function blocks. In-function `/* */` comments are permitted when they explain a non-obvious decision.
- **Author of record is `suinevere`** in every comment block.
- **Commit messages: one sentence.** No body, no bullets, no trailers. Never mention Claude, AI, or the session.
- **`TEXT_NUM_CATEGORIES` stays 15 and the `TC_*` enum order is frozen.** Category ids index `CATEGORY_POOL` in `music_data.c` and `CATEGORY_IMAGE` in `display.c`; reordering silently repoints every row after the insertion.
- **`test/corpus/blessed.inc` must remain byte-identical.** This is the primary regression gate for the whole plan. The classifier is untouched, so any diff means the fallback leaked into classification.
- **Never run `saturn/compile.bat`, `compile-cd.bat`, `compile-netbin.bat`, or an emulator.** The user runs all Saturn builds. To check a changed unit compiles, cross-compile that unit alone to the scratch directory.
- **The Saturn build auto-discovers sources** (`saturn/Makefile:40-41` globs `find src/`), so no Makefile edit is needed. The NETBIN list at `Makefile:59-77` must stay exactly 19 entries — `saturn/tests/test_netbin_sources.py` asserts it.
- **Do not re-run `tools/gen_room_corpus.py`.** It takes ~20 minutes and its output is committed and reviewed.

---

## File Structure

**Modify:**
- `saturn/src/classify/room_class_data.c` — `GameGenre` gains a `fallback` field; all 29 rows gain a value; new `text_game_fallback` accessor.
- `saturn/src/classify/room_class.h` — declare `text_game_fallback`.
- `saturn/src/sound/music.c` — `MUSIC_FALLBACK_ROOMS`, two statics, one line in `music_set_game`, two in `music_reset`, and the substitution in `music_on_turn`.
- `test/music_category_test.c` — the synthetic room-sequence suite.
- `saturn/tests/test_category_art.py` — assert every fallback names a category with a real picture pool.

**No files are created.** Nothing under `saturn/src/classify/room_class.c` changes.

---

## Task 1: The fallback column and its guard

Data plus the test that keeps it honest. Behaviour does not change in this task — a fallback that nothing reads yet.

**Files:**
- Modify: `saturn/src/classify/room_class_data.c` (the `GameGenre` typedef, the `GAME_GENRE` rows, and a new accessor at the end)
- Modify: `saturn/src/classify/room_class.h` (one declaration beside `text_game_genre`)
- Modify: `saturn/tests/test_category_art.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `unsigned char text_game_fallback(unsigned int release, const char* serial)` — returns the game's `TC_*` default, or `TC_NEUTRAL` (0) when the story is not listed or has no default. Task 2 calls exactly this.

- [ ] **Step 1: Add the failing check to the art test**

Append this to `saturn/tests/test_category_art.py`, inside `main()`, immediately before the `if fails:` block near the end:

```python
    # A fallback naming a category with no pictures would be invisible at
    # runtime -- display_set_dynamic_category resolves it to "no slot" and holds
    # the previous picture, which is exactly the behaviour the fallback exists to
    # replace. Same class of silent failure as the checks above.
    DATA = ROOT / "src" / "classify" / "room_class_data.c"
    if DATA.is_file():
        data = DATA.read_text(encoding="utf-8", errors="replace")
        table = re.search(r"GAME_GENRE\[\]\s*=\s*\{(.*?)\n\};", data, re.S)
        if not table:
            print("FAIL: no GAME_GENRE table in room_class_data.c")
            fails += 1
        else:
            rows = re.findall(r"\{\s*\d+,\s*\"\d+\",\s*[^,]+,\s*(TC_\w+)\s*\}",
                              table.group(1))
            if not rows:
                print("FAIL: GAME_GENRE rows carry no fallback column")
                fails += 1
            bad = []
            for cat in sorted(set(rows)):
                if cat == "TC_NEUTRAL":
                    continue
                img = "IMG_" + cat[len("TC_"):]
                if img not in pool or not pool[img]:
                    bad.append(f"{cat}: no pictures in {img}")
            if bad:
                print("FAIL: fallback categories with no picture pool:")
                for b in bad:
                    print(f"       {b}")
                fails += 1
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd /c/Users/saggl/CLionProjects/zaturn && python saturn/tests/test_category_art.py
```

Expected: `FAIL: GAME_GENRE rows carry no fallback column`, exit 1. The column does not exist yet.

- [ ] **Step 3: Add the field and populate all 29 rows**

In `saturn/src/classify/room_class_data.c`, replace the `GameGenre` typedef and the whole `GAME_GENRE` table with:

```c
typedef struct {
    unsigned short release;
    const char*    serial;
    unsigned char  genre;
    unsigned char  fallback;
} GameGenre;
static const GameGenre GAME_GENRE[] = {
    {   1, "151001", GN_FANTASY, TC_UNDERGROUND },  /* Adventure           */
    {  97, "851218", GN_MODERN , TC_MYSTERY     },  /* Ballyhoo            */
    {  23, "840809", GN_MODERN , TC_NAUTICAL    },  /* Cutthroats          */
    {  27, "831005", GN_MODERN , TC_MYSTERY     },  /* Deadline            */
    {  29, "860820", GN_FANTASY, TC_MAGIC       },  /* Enchanter           */
    {  59, "851108", GN_SCIFI  , TC_SCIFI       },  /* Hitchhiker's Guide  */
    {  37, "861215", GN_MODERN , TC_HOUSE       },  /* Hollywood Hijinx    */
    {  11, "870225", GN_MODERN , TC_NEUTRAL     },  /* Hypochondriac       */
    {  22, "830916", GN_MODERN , TC_DESERT      },  /* Infidel             */
    {  59, "860730", GN_SCIFI  , TC_SCIFI       },  /* Leather Goddesses   */
    { 219, "870912", GN_MODERN , TC_HORROR      },  /* The Lurking Horror  */
    {   9, "861022", GN_MODERN , TC_MYSTERY     },  /* Moonmist            */
    {   2, "840207", GN_FANTASY, TC_UNDERGROUND },  /* Mini-Zork I         */
    {  34, "871124", GN_FANTASY, TC_UNDERGROUND },  /* Mini-Zork I         */
    {   2, "871123", GN_FANTASY, TC_UNDERGROUND },  /* Mini-Zork II        */
    {  26, "870730", GN_MODERN , TC_NAUTICAL    },  /* Plundered Hearts    */
    {  37, "851003", GN_SCIFI  , TC_SCIFI       },  /* Planetfall          */
    {  16, "850603", GN_MODERN , TC_NAUTICAL    },  /* Seastalker          */
    {  15, "851108", GN_FANTASY, TC_MAGIC       },  /* Sorcerer            */
    {  87, "860904", GN_FANTASY, TC_MAGIC       },  /* Spellbreaker        */
    {  17, "821021", GN_SCIFI  , TC_SCIFI       },  /* Starcross           */
    { 107, "870430", GN_SCIFI  , TC_SCIFI       },  /* Stationfall         */
    {  14, "841005", GN_MODERN , TC_MYSTERY     },  /* Suspect             */
    {   8, "840521", GN_SCIFI  , TC_SCIFI       },  /* Suspended           */
    {  69, "850920", GN_FANTASY, TC_TOWN        },  /* Wishbringer         */
    {  22, "840924", GN_MODERN , TC_MYSTERY     },  /* The Witness         */
    {  88, "840726", GN_FANTASY, TC_UNDERGROUND },  /* Zork I              */
    {  48, "840904", GN_FANTASY, TC_UNDERGROUND },  /* Zork II             */
    {  17, "840727", GN_FANTASY, TC_UNDERGROUND },  /* Zork III            */
};
```

Extend that table's existing header block to describe the new column, keeping the note about the Infocom Samplers:

```c
/*----------------------
 | GameGenre / GAME_GENRE
 | Description: The authored genre and default mood of each shipped story, keyed
 |   by Z-header release number and 6-char serial -- the same key SOLUTIONS[] in
 |   typeahead_solution.c and text_game_room_category use.
 |
 |   `fallback` is what the engine shows after a run of rooms whose text says
 |   nothing. TC_NEUTRAL means this game has no default and keeps the older
 |   behaviour of holding the previous picture. Hypochondriac carries TC_NEUTRAL
 |   deliberately: guessing at an obscure game's mood is worse than leaving it.
 |
 |   The two Infocom Samplers are deliberately absent. A sampler carries excerpts
 |   of several games and genuinely changes genre partway through, so inference
 |   describes it better than any single tag could.
 | Author: suinevere
 ----------------------*/
```

- [ ] **Step 4: Add the accessor**

At the end of `saturn/src/classify/room_class_data.c`, after `text_game_genre`:

```c
/*----------------------
 | text_game_fallback
 | Description: Looks up a story's default mood by release and 6-char serial.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_GENRE
 | Params: release -- Z-machine release number; serial -- the 6-char game serial
 | Returns: the TC_* default, or TC_NEUTRAL when the story is not listed
 ----------------------*/
unsigned char text_game_fallback(unsigned int release, const char* serial) {
    int i;
    if (!serial) return TC_NEUTRAL;
    for (i = 0; i < (int)(sizeof GAME_GENRE / sizeof GAME_GENRE[0]); i++)
        if (GAME_GENRE[i].release == release &&
            memcmp(GAME_GENRE[i].serial, serial, 6) == 0)
            return GAME_GENRE[i].fallback;
    return TC_NEUTRAL;
}
```

And declare it in `saturn/src/classify/room_class.h`, directly below `text_game_genre`:

```c
unsigned char       text_game_fallback(unsigned int release, const char* serial);
```

Extend that declaration block's Description to mention it: `game_fallback returns a story's default mood, or TC_NEUTRAL when it is not listed or has none.`

- [ ] **Step 5: Run the art test — it must now pass**

```bash
cd /c/Users/saggl/CLionProjects/zaturn && python saturn/tests/test_category_art.py
```

Expected: `test_category_art: OK (...)`, exit 0. Every non-neutral fallback resolves to a pool with pictures in it.

- [ ] **Step 6: Prove the classifier did not move**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/rct \
    test/room_class_test.c saturn/src/classify/room_class.c \
    saturn/src/classify/room_class_data.c && /tmp/rct
git diff --stat -- test/corpus/blessed.inc
```

Expected: `SNAPSHOT: OK (1024 rooms unchanged)`, `ASSERTIONS: OK`, exit 0, and the `git diff` prints **nothing**. A modified `blessed.inc` here means something reached the classifier that should not have.

- [ ] **Step 7: Cross-compile the changed unit**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/scratch"
mkdir -p "$SP"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
"../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound \
    -I src/classify -o "$SP/room_class_data.o" src/classify/room_class_data.c
```

Expected: no output, one `.o` produced.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/classify/room_class_data.c saturn/src/classify/room_class.h \
        saturn/tests/test_category_art.py
git commit -m "classify: give each shipped story a default mood for rooms whose text says nothing"
```

---

## Task 2: The engine substitution

**Files:**
- Modify: `saturn/src/sound/music.c` — the engine-state block near line 107, `music_set_game` (~line 383), `music_reset` (~line 505), and `music_on_turn`'s `room_changed` branch (~line 765)
- Modify: `test/music_category_test.c`

**Interfaces:**
- Consumes: `unsigned char text_game_fallback(unsigned int release, const char* serial)` from Task 1.
- Produces: nothing further.

- [ ] **Step 1: Write the failing sequence tests**

Add this block to `test/music_category_test.c` inside `main()`, immediately before the final failure-count print. Ensure `#include <string.h>` is present at the top of the file.

The `music_tick()` calls in the helper are load-bearing: only the very first category of a session is announced immediately from `music_on_turn`. Every later change goes through the debounce and does not reach a subscriber until a tick commits it. A version of this test without the ticks silently records zero announcements and passes for the wrong reason.

```c
    /* ---- per-game fallback mood ----
       A run of rooms whose text says nothing used to hold the previous picture
       forever. After MUSIC_FALLBACK_ROOMS of them the engine now shows the
       game's authored default instead. These cases cannot come from the room
       corpus: it is a static list with no traversal order, so run length is only
       expressible as a scripted sequence. */
    {
        const char *NOWHERE = "Nowhere\nThere is nothing in particular here.";
        const char *CELLAR  = "Cellar\nA dark cellar with a crawlway south.";

        music_reset();
        music_set_backend(play);
        music_set_isplaying(isplaying);
        music_set_category_fn(rec_cat);
        music_set_debounce_frames(0);
        music_set_game(219, "870912");         /* The Lurking Horror -> HORROR */

        /* Two unclassified rooms must NOT trigger it: holding the previous
           picture across a short gap is deliberate, not a bug. */
        g_ncat = 0;
        enter(1, NOWHERE);
        enter(2, NOWHERE);
        CHECK(g_ncat == 0);

        /* The third does -- and it is a real mood change, so the track moves
           with the picture. A fallback that changed the wallpaper while the
           music stayed put would put the two subscribers in disagreement, which
           is the thing music_set_category_fn exists to prevent. */
        g_last_track = 0;
        enter(3, NOWHERE);
        CHECK(g_ncat == 1);
        CHECK(g_cats[0] == TC_HORROR);
        CHECK(g_last_track != 0);

        /* And it holds for the rest of the run -- one change, not seven. */
        g_ncat = 0;
        for (i = 4; i <= 10; i++) enter((unsigned) i, NOWHERE);
        CHECK(g_ncat == 0);

        /* A classified room takes back control immediately and resets the run,
           so three more unclassified rooms are needed before it fires again. */
        g_ncat = 0;
        enter(11, CELLAR);
        CHECK(g_ncat == 1);
        CHECK(g_cats[0] == TC_UNDERGROUND);
        g_ncat = 0;
        enter(12, NOWHERE);
        enter(13, NOWHERE);
        CHECK(g_ncat == 0);
        enter(14, NOWHERE);
        CHECK(g_ncat == 1);
        CHECK(g_cats[0] == TC_HORROR);

        /* A game whose column is TC_NEUTRAL never falls back, however long the
           run -- that is how "this game has no default" is expressed. */
        music_reset();
        music_set_game(11, "870225");          /* Hypochondriac -> none */
        g_ncat = 0;
        for (i = 1; i <= 8; i++) enter((unsigned) i, NOWHERE);
        CHECK(g_ncat == 0);

        /* An unlisted game takes the same path for a different reason. */
        music_reset();
        music_set_game(0, "000000");
        g_ncat = 0;
        for (i = 1; i <= 8; i++) enter((unsigned) i, NOWHERE);
        CHECK(g_ncat == 0);

        /* music_reset clears the run, so one game's dead zone cannot carry into
           the next game's opening rooms. */
        music_set_game(219, "870912");
        enter(1, NOWHERE);
        enter(2, NOWHERE);
        music_reset();
        music_set_backend(play);
        music_set_isplaying(isplaying);
        music_set_category_fn(rec_cat);
        music_set_debounce_frames(0);
        music_set_game(219, "870912");
        g_ncat = 0;
        enter(1, NOWHERE);
        CHECK(g_ncat == 0);
    }
```

Add this helper above `main()`, beside the other statics:

```c
static void enter(unsigned int room, const char *text) {
    music_note_output(text, (unsigned int) strlen(text));
    music_on_turn(room);
    for (int t = 0; t < 4; t++) music_tick();
}
```

- [ ] **Step 2: Run to verify the new cases fail**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mct \
    test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c \
    saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c && /tmp/mct
```

Expected: FAIL lines from the new block — the third-room case reports `g_ncat == 0` because nothing falls back yet. Cases that assert `g_ncat == 0` will pass already; that is fine and expected.

- [ ] **Step 3: Add the constant and state to `music.c`**

Beside `g_genre_was_locked` in the engine-state block:

```c
/*----------------------
 | MUSIC_FALLBACK_ROOMS
 | Description: How many rooms in a row must classify as nothing before the
 |   engine gives up on the text and shows the game's authored default mood.
 |
 |   Three, matching MUSIC_ROTATE_ROOMS -- both answer the same shape of question
 |   and letting them disagree would be noise. It is also what keeps the older
 |   behaviour worth keeping: holding the previous picture across a corridor or a
 |   closet reads as continuity, and a threshold of one would turn every isolated
 |   featureless room into two wallpaper changes where there are currently none.
 | Author: suinevere
 ----------------------*/
#define MUSIC_FALLBACK_ROOMS 3

/*----------------------
 | g_neutral_rooms / g_fallback_cat
 | Description: g_neutral_rooms counts consecutive rooms whose text classified as
 |   nothing; g_fallback_cat is the loaded game's authored default mood, or
 |   TC_NEUTRAL when it has none and the older hold-the-previous-picture
 |   behaviour should stand.
 | Author: suinevere
 ----------------------*/
static int           g_neutral_rooms = 0;
static unsigned char g_fallback_cat = TC_NEUTRAL;
```

- [ ] **Step 4: Wire `music_set_game` and `music_reset`**

In `music_set_game`, after the existing `g_genre_was_locked` line:

```c
    g_fallback_cat = text_game_fallback(g_release, g_serial);
    g_neutral_rooms = 0;
```

In `music_reset`, beside the existing `g_genre_was_locked = 0;`:

```c
    g_neutral_rooms = 0;
    g_fallback_cat = TC_NEUTRAL;
```

> **Corrected during implementation.** Wiping `g_fallback_cat` to `TC_NEUTRAL`
> here is wrong -- it undoes what `music_set_game` just derived, every time
> `music_reset` runs after it. The shipped `music_reset` re-derives
> `g_fallback_cat` (and `g_genre_was_locked`) from `g_release`/`g_serial`
> instead of wiping them. See the design doc's Architecture section for the
> corrected control flow.

- [ ] **Step 5: Add the substitution in `music_on_turn`**

Inside the `if (room_changed) {` branch, after the whole `if (base < 0) { ... }` block closes and **before** `g_cur_room = room; g_have_room = 1; ...`:

```c
        /* The cache above stored the real verdict, not this substitution, so a
           revisit mid-run behaves exactly like the first visit and the room
           corpus keeps meaning "what the text says". */
        if (base != TC_NEUTRAL) {
            g_neutral_rooms = 0;
        } else if (++g_neutral_rooms >= MUSIC_FALLBACK_ROOMS &&
                   g_fallback_cat != TC_NEUTRAL) {
            base = g_fallback_cat;
        }
```

Extend `music_on_turn`'s header block Description with one sentence: `After MUSIC_FALLBACK_ROOMS rooms that classify as nothing, the game's authored default mood stands in for the text.` Add `g_neutral_rooms, g_fallback_cat` to its Globals line.

- [ ] **Step 6: Run the tests — all must pass**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -I saturn/src/sound -I saturn/src/classify -o /tmp/mct \
    test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c \
    saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c && /tmp/mct
```

Expected: `ALL PASS`, exit 0.

- [ ] **Step 7: Run the whole host suite, including the corpus invariant**

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
git diff --stat -- test/corpus/blessed.inc
```

Expected: all five green, and `git diff` on `blessed.inc` prints **nothing**. That silence is the point of the whole design — the classifier was never touched.

- [ ] **Step 8: Cross-compile `music.c`**

```bash
SP="C:/Users/saggl/AppData/Local/Temp/claude/C--Users-saggl-CLionProjects-zaturn/scratch"
mkdir -p "$SP"
cd /c/Users/saggl/CLionProjects/zaturn/saturn
"../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-gcc" -c -I src -I src/sound \
    -I src/classify -o "$SP/music.o" src/sound/music.c
```

Expected: no output, one `.o` produced.

- [ ] **Step 9: Mark the spec implemented and commit**

Change line 4 of `docs/superpowers/specs/2026-08-05-per-game-fallback-mood-design.md` to `**Status:** Implemented 2026-08-05`.

```bash
git add saturn/src/sound/music.c test/music_category_test.c \
        docs/superpowers/specs/2026-08-05-per-game-fallback-mood-design.md
git commit -m "music: show a game's default mood after three rooms whose text says nothing"
```

- [ ] **Step 10: Hand back for a Saturn build**

Report that every host test is green, the corpus is byte-identical, and both changed units cross-compile. Ask the user to run `saturn/compile.bat` and check in play whether three rooms of hysteresis feels right — `MUSIC_FALLBACK_ROOMS` is the dial if it does not. That judgement needs real hardware and cannot be made from the host suite.
