# Dynamic Background Art Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the background picture follow the same mood category the music engine already derives from on-screen text, selected by a new "Dynamic" entry at the top of the Display menu's Palette row, cross-fading picture and music together in game.

**Architecture:** The music engine already owns `g_active_cat`, the category currently sounding. Rather than give the display its own classifier and timer, the engine gains two optional callbacks — one announcing a category change, one driving a fade ramp — and the Saturn client subscribes. `music.c` stays platform-independent and host-testable. The fade runs as per-frame phases around the engine's existing debounced commit, never as a blocking loop.

**Tech Stack:** C (engine, display model, host unit tests via `gcc`), C++ (SRL VDP2/CD-DA shims), Python (repo guard tests), `saturn/syntax-check.sh` for SH-2 type-checking.

**Spec:** `docs/superpowers/specs/2026-07-30-dynamic-background-art-design.md`

## Global Constraints

- **Never run `saturn/compile.bat` or the emulator.** The user runs all real builds. Verify Saturn sources with `sh saturn/syntax-check.sh <paths relative to saturn/>`, which type-checks against real SRL/SGL headers in both DEBUG and release configurations without producing artifacts.
- Host unit tests are pure C. Tests include `"sound/music.h"`, so the include flag is **`-I saturn/src`**, not `-I saturn/src/sound`.
- Baseline (verified green before this plan): `test/music_test.c`, `test/music_mix_test.c`, `saturn/tests/test_display.c` all pass.
- `SRL::Debug::Print` supports only `%d`, `%s`, `%c` — no `%x`, no width/precision flags.
- 14 categories, enum order fixed: NEUTRAL, WILDERNESS, UNDERGROUND, WATER, NAUTICAL, TOWN, DUNGEON, DESERT, MAGIC, SCIFI, HORROR, MYSTERY, DANGER, TRIUMPH.
- `MUSIC_DYN_LOOPS` stays 3. The keyword classifier and event scan stay. No room-title extraction.
- Every new/changed function gets a doc comment in the file's existing `/*---- | name | Description: ... | Author: suinevere ----*/` house style.
- `saturn/src/video/display.c` is on the **NETBIN** source list (`saturn/tests/test_netbin_sources.py` asserts exactly 18 objects). Do **not** add new source files that display.c depends on — the category→picture table goes *inside* display.c for this reason.

---

### Task 1: Rename the text-side category symbols

Mechanical rename. The category is no longer music-specific, so the symbols that derive it from text become `text_*`. The symbols that map a category onto *CD tracks* (`music_category_pool`, `music_category_track`) keep their names — they really are music-side.

**Files:**
- Modify: `saturn/src/sound/music.h`, `saturn/src/sound/music.c`, `saturn/src/sound/music_data.c`
- Modify: `test/music_test.c`, `test/music_mix_test.c`, `saturn/tests/test_music_pause.c`

**Interfaces:**
- Produces: `TEXT_NUM_CATEGORIES`, `TC_NEUTRAL`..`TC_TRIUMPH`, `TextKeyword`, `text_keywords()`, `text_events()`, `text_classify_room()`, `text_scan_event()`, `text_game_room_category()`. Every later task uses `TC_*` and `TEXT_NUM_CATEGORIES`.
- Unchanged: `music_category_pool`, `music_category_track`, `MUSIC_DYN_LOOPS`, `MUSIC_TRACK_MIN/MAX`, `MUSIC_DEBOUNCE_FRAMES`.

- [ ] **Step 1: Confirm the baseline is green**

```bash
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c     saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mt
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mmt
```

Expected: `ALL PASS` twice. If not, stop — the rename must start from green.

- [ ] **Step 2: Apply the rename**

```bash
for f in saturn/src/sound/music.h saturn/src/sound/music.c saturn/src/sound/music_data.c \
         test/music_test.c test/music_mix_test.c saturn/tests/test_music_pause.c; do
  sed -i \
    -e 's/\bMUSIC_NUM_CATEGORIES\b/TEXT_NUM_CATEGORIES/g' \
    -e 's/\bMC_\([A-Z]\)/TC_\1/g' \
    -e 's/\bMusicKeyword\b/TextKeyword/g' \
    -e 's/\bmusic_classify_room\b/text_classify_room/g' \
    -e 's/\bmusic_scan_event\b/text_scan_event/g' \
    -e 's/\bmusic_keywords\b/text_keywords/g' \
    -e 's/\bmusic_events\b/text_events/g' \
    -e 's/\bmusic_game_room_category\b/text_game_room_category/g' \
    "$f"
done
```

`MUSIC_DYN_LOOPS`, `MUSIC_TRACK_MIN`, `MUSIC_TRACK_MAX` and `MUSIC_DEBOUNCE_FRAMES` are untouched — the patterns are anchored with `\b` and none of them match.

- [ ] **Step 3: Fix the prose in the doc comments by hand**

`sed` renames identifiers, not English. Read `saturn/src/sound/music.h` and `music_data.c` and update the surrounding descriptions so they read correctly — in particular the `music.h` header block and the `TEXT_NUM_CATEGORIES / TC_* / MIX_*` block, which describe "mood categories" as a music concept. They now describe a *text* category that drives both music and background art. Mention that second consumer.

- [ ] **Step 4: Verify**

```bash
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c     saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mt
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mmt
grep -rn "MC_[A-Z]\|MUSIC_NUM_CATEGORIES\|music_classify_room\|music_scan_event\|music_keywords\|music_events\|music_game_room_category\|MusicKeyword" \
  saturn/src test --include=*.c --include=*.h --include=*.cxx
```

Expected: `ALL PASS` twice, and the `grep` prints nothing.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/sound/music.h saturn/src/sound/music.c saturn/src/sound/music_data.c \
        test/music_test.c test/music_mix_test.c saturn/tests/test_music_pause.c
git commit -m "refactor: rename text-side category symbols music_* -> text_*

The mood category is derived from on-screen text and is about to drive the
background picture as well as the CD-DA track, so the symbols that produce it
are no longer music-specific. music_category_pool/music_category_track keep
their names: they map a category onto CD tracks, which is genuinely music-side.

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 2: Category-change callback in the engine

The engine announces every change of `g_active_cat` through an optional callback. With none installed, behaviour is byte-for-byte what it is today — which is what the existing tests assume.

**Files:**
- Modify: `saturn/src/sound/music.h` (declaration), `saturn/src/sound/music.c` (state + call sites)
- Create: `test/music_category_test.c`

**Interfaces:**
- Consumes: `TC_*`, `TEXT_NUM_CATEGORIES` (Task 1).
- Produces: `void music_set_category_fn(void (*fn)(int cat));` — Task 8 installs the Saturn handler.

- [ ] **Step 1: Write the failing test**

Create `test/music_category_test.c`:

```c
/* Host tests for the engine's category-change announcement and the 1.5s settle.
   gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c \
       saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct */
#include <stdio.h>
#include "sound/music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

static int g_cats[32], g_ncat;
static void rec_cat(int c) { if (g_ncat < 32) g_cats[g_ncat++] = c; }
static void play(int t, int loop) { (void) t; (void) loop; }
static int  isplaying(void) { return 1; }

int main(void) {
    int fails = 0;
    music_set_backend(play);
    music_set_isplaying(isplaying);
    music_set_category_fn(rec_cat);
    music_set_game(0, "000000");

    /* The pools these tests lean on must be non-empty, or the engine takes the
       "nothing playing yet" branch every turn and the debounce is never armed. */
    CHECK(music_category_track(TC_UNDERGROUND) != 0);
    CHECK(music_category_track(TC_WILDERNESS)  != 0);

    /* --- default settle is 90 frames (1.5s @ 60Hz) ---
       Runs FIRST, before any music_set_debounce_frames call, because that
       override is sticky across music_reset(). */
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(1);                       /* NEUTRAL, plays at once */
    g_ncat = 0;
    music_note_output("A damp cave with a tunnel.", 26);
    music_on_turn(2);                       /* UNDERGROUND, armed */
    for (int i = 0; i < 89; i++) music_tick();
    CHECK(g_ncat == 0);                     /* not yet */
    music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);

    music_set_debounce_frames(3);           /* short settle for the rest */

    /* --- reset announces the neutral default --- */
    g_ncat = 0;
    music_reset();
    CHECK(g_ncat == 1 && g_cats[0] == TC_NEUTRAL);

    /* --- the first room of a session commits immediately --- */
    g_ncat = 0;
    music_note_output("You are in a dark cave with a tunnel.", 37);
    music_on_turn(10);
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);

    /* --- a later room is debounced: nothing announced until it elapses --- */
    g_ncat = 0;
    music_note_output("A forest clearing among tall trees.", 35);
    music_on_turn(11);
    CHECK(g_ncat == 0);
    music_tick(); music_tick();
    CHECK(g_ncat == 0);
    music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_WILDERNESS);

    /* --- standing still announces nothing more --- */
    g_ncat = 0;
    music_note_output("The forest is quiet.", 20);
    music_on_turn(11);
    music_tick(); music_tick(); music_tick(); music_tick();
    CHECK(g_ncat == 0);

    /* --- same category, NEW room: the settle restarts (Task 3) --- */
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(20);                      /* NEUTRAL now sounding */
    g_ncat = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(21);                      /* UNDERGROUND armed, 3 frames */
    music_tick(); music_tick();             /* 2 of 3 elapsed */
    music_note_output("A dark tunnel.", 14);
    music_on_turn(22);                      /* same category, new room */
    music_tick();
    CHECK(g_ncat == 0);                     /* would have fired without the restart */
    music_tick(); music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
```

Expected: fails to link — `music_set_category_fn` is undefined.

- [ ] **Step 3: Declare it in `music.h`**

In `saturn/src/sound/music.h`, inside the engine block, after `music_set_debounce_frames`:

```c
void music_set_category_fn(void (*fn)(int cat));       /* announce category changes */
```

Extend that block's `Description:` to mention it: the engine publishes the active
text category so a second consumer — the background art — can follow the same
change the track does, on the same event rather than a timer of its own.

- [ ] **Step 4: Implement in `music.c`**

Add near the other callback state, after `music_set_debounce_frames`:

```c
/*----------------------
 | g_cat_fn / music_set_category_fn / notify_cat
 | Description: The category-change subscriber and the one place that calls it.
 |   The engine's active category is the only thing that decides which track
 |   sounds, and it is now also what decides which picture shows; publishing it
 |   here rather than letting the client re-derive it from the same text keeps
 |   the two on one event, so a picture cannot end up describing a mood the music
 |   has already left. Optional: with nothing installed the engine behaves exactly
 |   as it did before, which is what the pre-existing host tests assume.
 | Author: suinevere
 ----------------------*/
static void (*g_cat_fn)(int) = 0;
void music_set_category_fn(void (*fn)(int cat)) { g_cat_fn = fn; }
static void notify_cat(int cat) { if (g_cat_fn) g_cat_fn(cat); }
```

Then add the call at each of the four sites where `g_active_cat` changes.

In `music_reset`, immediately before the closing `if (g_play) g_play(0, 0);`:

```c
    notify_cat(TC_NEUTRAL);   // back to the default picture, not the last room's
```

In `music_start_menu`, replace `if (g_active_cat < 0) g_active_cat = TC_NEUTRAL;` with:

```c
            if (g_active_cat < 0) { g_active_cat = TC_NEUTRAL; notify_cat(TC_NEUTRAL); }
```

In `music_tick`'s pending-commit branch, after `g_active_cat = g_pending_cat;` and
the `g_pending_*` clears, before `play_dyn(t, 1);`:

```c
            notify_cat(g_active_cat);
```

In `music_on_turn`'s immediate branch, replace
`g_active_cat = target; play_dyn(pick_prefer_long(target), 1);` with:

```c
        g_active_cat = target; notify_cat(target); play_dyn(pick_prefer_long(target), 1);
```

- [ ] **Step 5: Run the test — the settle assertions still fail**

```bash
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
```

Expected: it links and most checks pass, but the two 90-frame assertions and the
restart-on-room-change assertions FAIL — those are Task 3. Confirm the failures
are exactly those and no others.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/music.h saturn/src/sound/music.c test/music_category_test.c
git commit -m "feat(music): publish text-category changes to an optional subscriber

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 3: 1.5s settle, restarting on every room change

**Files:**
- Modify: `saturn/src/sound/music.c` (`MUSIC_DEBOUNCE_FRAMES`, `music_on_turn`)

**Interfaces:**
- Consumes: the test from Task 2.
- Produces: no new symbols; `MUSIC_DEBOUNCE_FRAMES` becomes 90.

- [ ] **Step 1: Change the constant**

In `saturn/src/sound/music.c`, change:

```c
#define MUSIC_DEBOUNCE_FRAMES 180
```

to:

```c
/* 90 frames = 1.5s at 60Hz (1.8s on a 50Hz PAL machine, accepted: the frame
   counter is the engine's only clock). This is the "stopped long enough to
   mean it" threshold, and it now gates the background picture as well as the
   track. */
#define MUSIC_DEBOUNCE_FRAMES 90
```

Update the mix-state doc block above it, which says "~3s @ 60fps".

- [ ] **Step 2: Restart the settle on any room change**

In `music_on_turn`, capture whether the room moved. Change:

```c
    int event_cat = text_scan_event(g_turn_text);
    if (!g_have_room || room != g_cur_room) {
```

to:

```c
    int event_cat = text_scan_event(g_turn_text);
    int room_changed = (!g_have_room || room != g_cur_room);
    if (room_changed) {
```

Then change the pending-arm tail from:

```c
    } else if (target != g_pending_cat) {
        g_pending_cat = target;
        g_pending_track = pick_prefer_long(target);
        g_pending_frames = g_debounce_frames;
    }
```

to:

```c
    } else if (target != g_pending_cat) {
        g_pending_cat = target;
        g_pending_track = pick_prefer_long(target);
        g_pending_frames = g_debounce_frames;
    } else if (room_changed) {
        /* Same target, but they moved again. The rule is "stopped in one room
           for 1.5s", not "1.5s since the mood first changed" -- without this,
           walking a corridor of same-mood rooms commits shortly after arriving
           in one of them rather than after settling in it. */
        g_pending_frames = g_debounce_frames;
    }
```

Update `music_on_turn`'s doc comment: the countdown restarts on any room change while a switch is pending, not only when the target category changes.

- [ ] **Step 3: Run the tests**

```bash
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mmt
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c     saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mt
```

Expected: `ALL PASS` three times. If `music_mix_test` fails, it is asserting the
old 180 default — read the failing line and update it to 90 only if it is
asserting the constant, not if it is asserting behaviour.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/sound/music.c test/music_mix_test.c
git commit -m "feat(music): 1.5s settle, restarting on every room change

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 4: Fade phases around the commit

The commit issues a new `PlaySingle`, so the audio must already be down when it happens. The fade therefore *brackets* the commit rather than running alongside it, and the engine — which owns the commit — owns the phase. One counter drives picture and audio, so they cannot drift.

**Files:**
- Modify: `saturn/src/sound/music.h`, `saturn/src/sound/music.c`
- Modify: `test/music_category_test.c` (append)

**Interfaces:**
- Consumes: `notify_cat` (Task 2).
- Produces: `void music_set_fade_fn(void (*fn)(int level));` (level 0..255, 255 = normal) and `void music_set_fade_frames(int n);` (0 = instant, the default). Task 8 installs both.

- [ ] **Step 1: Write the failing test**

Append to `test/music_category_test.c`, before the final `printf`:

```c
    /* --- fade phases --- */
    {
        static int lv[512]; static int nlv;
        extern void music_set_fade_fn(void (*fn)(int));
        extern void music_set_fade_frames(int n);
        void rec_lv(int l);   /* forward: defined below main via a file-scope fn */
        (void) lv; (void) nlv;
    }
```

That sketch will not compile — write it as file scope instead. Add these above
`main()`:

```c
static int g_lv[512], g_nlv;
static void rec_lv(int l) { if (g_nlv < 512) g_lv[g_nlv++] = l; }
```

and append this block inside `main()` before the final `printf`:

```c
    /* --- fade off (the default) is byte-for-byte the old instant commit --- */
    music_set_fade_fn(rec_lv);
    music_set_fade_frames(0);
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(30);
    g_ncat = 0; g_nlv = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(31);
    music_tick(); music_tick(); music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);
    CHECK(g_nlv == 0);                       /* no fade callback at all */

    /* --- with a fade, the commit lands at the bottom, exactly once --- */
    music_set_fade_frames(4);
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(40);
    g_ncat = 0; g_nlv = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(41);
    music_tick(); music_tick(); music_tick();   /* settle elapses */
    CHECK(g_ncat == 0);                         /* not committed yet: fading out */
    CHECK(g_nlv > 0);                           /* ramp has started */
    CHECK(g_lv[g_nlv - 1] < 255);               /* and it is heading down */
    for (int i = 0; i < 4 && g_ncat == 0; i++) music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);
    CHECK(g_lv[g_nlv - 1] == 0);                /* committed at the bottom */
    for (int i = 0; i < 8; i++) music_tick();
    CHECK(g_lv[g_nlv - 1] == 255);              /* and came back up */
    CHECK(g_ncat == 1);                         /* still exactly one commit */

    /* --- a reset mid-fade restores full brightness, or the screen stays dim --- */
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(50);
    music_note_output("A damp cave.", 12);
    music_on_turn(51);
    music_tick(); music_tick(); music_tick(); music_tick();
    g_nlv = 0;
    music_reset();
    CHECK(g_nlv >= 1 && g_lv[g_nlv - 1] == 255);

    music_set_fade_frames(0);   /* leave the engine as we found it */
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
```

Expected: fails to link — `music_set_fade_fn` / `music_set_fade_frames` undefined.

- [ ] **Step 3: Declare in `music.h`**

In the engine block, after `music_set_category_fn`:

```c
void music_set_fade_fn(void (*fn)(int level));         /* 0 = black/silent, 255 = normal */
void music_set_fade_frames(int n);                     /* ramp length; 0 = instant commit */
```

- [ ] **Step 4: Implement the phases in `music.c`**

Add above `music_tick`:

```c
/*----------------------
 | fade state (MP_* / g_phase / g_fade_*)
 | Description: The transition around a Dynamic commit. A commit issues a fresh
 |   PlaySingle, so the audio has to already be down when it happens and come up
 |   after -- the ramp brackets the commit rather than running beside it, which is
 |   why the engine owns the phase instead of the client fading on notification.
 |   One counter drives both the picture and the volume, so they cannot drift
 |   apart. g_fade_frames of 0 is the default and skips the phases entirely,
 |   reproducing the instant commit the pre-existing tests assert.
 |
 |   These are ticked one step per music_tick, never looped: title.cxx's fades are
 |   blocking `for i ... Synchronize()` ramps, and running one of those from a
 |   commit would stall the interpreter for the whole fade every time a room's
 |   mood changed.
 | Author: suinevere
 ----------------------*/
enum { MP_IDLE = 0, MP_FADE_OUT, MP_FADE_IN };
static int  g_phase = MP_IDLE;
static int  g_fade_frames = 0;
static int  g_fade_i = 0;
static void (*g_fade_fn)(int) = 0;

void music_set_fade_fn(void (*fn)(int level)) { g_fade_fn = fn; }
void music_set_fade_frames(int n) { g_fade_frames = (n < 0) ? 0 : n; }

static void fade_emit(int level) { if (g_fade_fn) g_fade_fn(level); }

/*----------------------
 | commit_pending
 | Description: Takes the pending category, announces it, and starts its track.
 |   Shared by the instant path and the bottom of a fade so the two cannot drift.
 | Author: suinevere
 ----------------------*/
static void commit_pending(void) {
    g_active_cat = g_pending_cat;
    int t = g_pending_track;
    g_pending_cat = -1; g_pending_track = 0;
    notify_cat(g_active_cat);
    play_dyn(t, 1);
}
```

Replace `music_tick`'s pending block. It currently reads:

```c
    if (g_pending_cat >= 0) {
        if (g_pending_frames > 0) g_pending_frames--;
        if (g_pending_frames <= 0) {
            g_active_cat = g_pending_cat;
            int t = g_pending_track;
            g_pending_cat = -1; g_pending_track = 0;
            notify_cat(g_active_cat);
            play_dyn(t, 1);
        }
        return;
    }
```

Replace it with:

```c
    if (g_phase == MP_FADE_OUT) {
        if (g_fade_i > 0) g_fade_i--;
        fade_emit((255 * g_fade_i) / g_fade_frames);
        if (g_fade_i <= 0) {
            commit_pending();               // swap at the bottom, where it is inaudible
            g_phase = MP_FADE_IN;
        }
        return;
    }
    if (g_phase == MP_FADE_IN) {
        if (g_fade_i < g_fade_frames) g_fade_i++;
        fade_emit((255 * g_fade_i) / g_fade_frames);
        if (g_fade_i >= g_fade_frames) g_phase = MP_IDLE;
        return;
    }
    if (g_pending_cat >= 0) {
        if (g_pending_frames > 0) g_pending_frames--;
        if (g_pending_frames <= 0) {
            if (g_fade_frames > 0) { g_phase = MP_FADE_OUT; g_fade_i = g_fade_frames; }
            else                   commit_pending();
        }
        return;
    }
```

In `music_reset`, add to the state clears (before `notify_cat(TC_NEUTRAL);`):

```c
    if (g_phase != MP_IDLE) fade_emit(255);   // a reset mid-ramp must not leave the screen dim
    g_phase = MP_IDLE; g_fade_i = 0;
```

Update `music_tick`'s doc comment to describe the three phases.

- [ ] **Step 5: Run the tests**

```bash
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mmt
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c     saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mt
```

Expected: `ALL PASS` three times.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound/music.h saturn/src/sound/music.c test/music_category_test.c
git commit -m "feat(music): fade phases bracketing the Dynamic commit

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 5: Category → picture table

Keyed by filename, not slot number: slots index the disc's TGA scan order, so a slot-keyed table would silently repoint if art were added, removed or reordered — the same reason the save blob already stores names.

The table lives **inside `display.c`**, not a new file. `display.c` is on the NETBIN source list, which `saturn/tests/test_netbin_sources.py` pins to exactly 18 objects; a new source file that display.c depends on would break the netbin link and that test. `display.c` gains an `#include "sound/music.h"` for `TC_*`, which is fine — `options.cxx`, also on the netbin list, already includes it.

**Files:**
- Modify: `saturn/src/video/display.h`, `saturn/src/video/display.c`
- Create: `saturn/tests/test_category_art.py`
- Modify: `saturn/tests/test_display.c` (append)

**Interfaces:**
- Consumes: `TC_*`, `TEXT_NUM_CATEGORIES` (Task 1).
- Produces: `const char *display_category_image(int cat);` — filename, or `NULL` for "keep the current picture". Task 6 consumes it.

- [ ] **Step 1: Write the failing tests**

Append to `saturn/tests/test_display.c`, and add a call to it from that file's `main()`:

```c
static void test_category_art(void) {
    int cat, named = 0;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        const char *f = display_category_image(cat);
        if (f == NULL) continue;
        named++;
        assert(f[0] != '\0');
        assert(strlen(f) <= DISP_IMAGE_NAME_MAX - 1);   /* fits the save blob's field */
    }
    /* Twelve places carry art; the two turn-text events deliberately do not. */
    assert(named == 12);
    assert(display_category_image(TC_DANGER)  == NULL);
    assert(display_category_image(TC_TRIUMPH) == NULL);
    assert(display_category_image(TC_NEUTRAL) != NULL);   /* the seeded default */

    /* Out of range is "keep current", never a stray pointer. */
    assert(display_category_image(-1) == NULL);
    assert(display_category_image(TEXT_NUM_CATEGORIES) == NULL);
}
```

Create `saturn/tests/test_category_art.py`:

```python
#!/usr/bin/env python3
"""Every picture the category table names must actually be on the disc.

A name with no file behind it is invisible at runtime -- display.c resolves it
to "no slot", the wallpaper simply holds on the previous picture, and that room
mood silently never gets its art. This is the same class of failure
test_lwram_splash_budget.py guards: nothing about it shows on screen.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "video" / "display.c"
TGA = ROOT / "cd" / "data" / "TGA"


def main():
    text = SRC.read_text(encoding="utf-8", errors="replace")
    block = re.search(r"CATEGORY_IMAGE\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not block:
        print("FAIL: no CATEGORY_IMAGE table in display.c")
        return 1

    names = re.findall(r'"([^"]+)"', block.group(1))
    if not names:
        print("FAIL: CATEGORY_IMAGE names no pictures at all")
        return 1

    present = {p.name for p in TGA.iterdir() if p.suffix.upper() == ".TGA"}
    missing = sorted(n for n in set(names) if n not in present)
    if missing:
        print(f"FAIL: named in display.c but not in cd/data/TGA: {', '.join(missing)}")
        print(f"      present: {', '.join(sorted(present))}")
        return 1

    print(f"test_category_art: OK ({len(set(names))} pictures, {len(names)} assignments)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run both to confirm they fail**

```bash
gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
python saturn/tests/test_category_art.py
```

Expected: the C test fails to compile (`display_category_image` undeclared,
`TC_*` unknown); the Python test prints `FAIL: no CATEGORY_IMAGE table`.

- [ ] **Step 3: Declare in `display.h`**

Add, inside the `extern "C"` block, after the `display_image_file / display_image_label` block:

```c
/*----------------------
 | display_category_image
 | Description: The picture a text category shows, as an on-disc filename, or NULL
 |   for "keep whatever is showing". Keyed by name rather than slot because slots
 |   index the disc's TGA scan order, so adding, removing or reordering art would
 |   repoint a slot-keyed table without any visible sign. NULL is returned for the
 |   two turn-text event categories (TC_DANGER, TC_TRIUMPH) -- those are moments,
 |   not places, so the music shifts for them while the wallpaper holds on the
 |   room's own picture instead of flicking away and back.
 | Author: suinevere
 ----------------------*/
const char *display_category_image(int cat);
```

- [ ] **Step 4: Implement in `display.c`**

At the top of `saturn/src/video/display.c`, after `#include "display.h"`:

```c
#include "sound/music.h"   /* TC_* / TEXT_NUM_CATEGORIES: the categories the art follows */
```

Add the table and accessor near the image helpers:

```c
/*----------------------
 | CATEGORY_IMAGE
 | Description: Which shipped picture each text category shows. Twelve places map
 |   onto the eight bitmaps in cd/data/TGA (several categories share one -- there
 |   is no desert or nautical painting, and the nearest cliff reads better than a
 |   wrong one). The two event categories are NULL on purpose; see
 |   display_category_image. Indexed by TC_*, so the row order is the enum order
 |   in music.h and must stay that way -- saturn/tests/test_category_art.py checks
 |   the names exist on the disc, and test_display.c checks the shape.
 | Author: suinevere
 ----------------------*/
static const char *const CATEGORY_IMAGE[TEXT_NUM_CATEGORIES] = {
    /* TC_NEUTRAL     */ "TYPEWRTR.TGA",
    /* TC_WILDERNESS  */ "FOREST.TGA",
    /* TC_UNDERGROUND */ "BUNKER.TGA",
    /* TC_WATER       */ "CLIFF.TGA",
    /* TC_NAUTICAL    */ "CLIFF.TGA",
    /* TC_TOWN        */ "HOUSE.TGA",
    /* TC_DUNGEON     */ "ANCIENT.TGA",
    /* TC_DESERT      */ "CLIFF.TGA",
    /* TC_MAGIC       */ "CASTLE.TGA",
    /* TC_SCIFI       */ "COMPUTER.TGA",
    /* TC_HORROR      */ "BUNKER.TGA",
    /* TC_MYSTERY     */ "HOUSE.TGA",
    /* TC_DANGER      */ 0,
    /* TC_TRIUMPH     */ 0
};

const char *display_category_image(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    return CATEGORY_IMAGE[cat];
}
```

- [ ] **Step 5: Run both tests**

```bash
gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
python saturn/tests/test_category_art.py
```

Expected: `test_display: OK` and `test_category_art: OK (8 pictures, 12 assignments)`.

Note the added `-I saturn/src` on the display test — `display.c` now includes
`"sound/music.h"`. Update the build comment at the top of `test_display.c` to match.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/display.h saturn/src/video/display.c \
        saturn/tests/test_display.c saturn/tests/test_category_art.py
git commit -m "feat(display): category -> picture table, keyed by filename

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 6: The Dynamic palette

Insert Dynamic at palette index 0 and shift the colour presets up one. Index 0 always exists so every `display_preset_*` accessor stays a single unconditional expression; `display_cycle_palette` skips it when the disc has no art.

**Files:**
- Modify: `saturn/src/video/display.h`, `saturn/src/video/display.c`
- Modify: `saturn/tests/test_display.c` (append)

**Interfaces:**
- Consumes: `display_category_image` (Task 5).
- Produces: `DISP_PAL_DYNAMIC`, `void display_set_dynamic_category(int cat);`, `int display_dynamic_slot(void);`. Tasks 7 and 8 consume all three.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_display.c` and call it from `main()`:

```c
static const char *const IMGS[] = { "ANCIENT.TGA", "BUNKER.TGA", "CASTLE.TGA",
                                    "CLIFF.TGA", "COMPUTER.TGA", "FOREST.TGA",
                                    "HOUSE.TGA", "TYPEWRTR.TGA" };

static void test_dynamic_palette(void) {
    DisplayState d;
    int i;

    display_set_images(IMGS, 8);

    /* Index 0 is Dynamic, then the colour presets, then one per image. */
    assert(display_palette_count() == 1 + DISP_PRESET_N + 8);
    assert(strcmp(display_preset_name(DISP_PAL_DYNAMIC), "Dynamic") == 0);
    assert(display_preset_bg(DISP_PAL_DYNAMIC)   == DISP_BG_BLACK);
    assert(display_preset_text(DISP_PAL_DYNAMIC) == DISP_TEXT_WHITE);

    /* The colour presets shifted up by one and still read correctly. */
    for (i = 0; i < DISP_PRESET_N; i++) {
        assert(display_preset_name(i + 1) != NULL);
        assert(display_preset_name(i + 1)[0] != '\0');
        assert(strlen(display_preset_name(i + 1)) <= 16);
        assert(display_preset_image(i + 1) == DISP_IMAGE_NONE);
    }
    /* ...and the image presets sit after them. */
    for (i = 0; i < 8; i++)
        assert(display_preset_image(DISP_PRESET_N + 1 + i) == i);

    /* Dynamic resolves through the category table, and holds on an event. */
    display_set_dynamic_category(TC_TOWN);
    assert(strcmp(display_image_file(display_dynamic_slot()), "HOUSE.TGA") == 0);
    display_set_dynamic_category(TC_DANGER);          /* no art: hold */
    assert(strcmp(display_image_file(display_dynamic_slot()), "HOUSE.TGA") == 0);
    display_set_dynamic_category(TC_SCIFI);
    assert(strcmp(display_image_file(display_dynamic_slot()), "COMPUTER.TGA") == 0);
    display_set_dynamic_category(-1);                 /* out of range: hold */
    assert(strcmp(display_image_file(display_dynamic_slot()), "COMPUTER.TGA") == 0);

    /* Dynamic is the default, and its image tracks the category. */
    display_defaults(&d);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.image == display_dynamic_slot());
    assert(display_is_image(&d));
    assert(strcmp(display_palette_name(&d), "Dynamic") == 0);

    /* Cycling forward off Dynamic lands on the first colour preset. */
    display_cycle_palette(&d, +1);
    assert(d.palette == 1);
    assert(d.image == DISP_IMAGE_NONE);
    /* ...and cycling back returns to it. */
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PAL_DYNAMIC);

    /* With no art on the disc, Dynamic is skipped and is not the default. */
    display_set_images(NULL, 0);
    display_defaults(&d);
    assert(d.palette == 1);
    assert(d.image == DISP_IMAGE_NONE);
    display_cycle_palette(&d, -1);            /* would land on 0 without the skip */
    assert(d.palette != DISP_PAL_DYNAMIC);

    display_set_images(IMGS, 8);              /* leave the model as we found it */
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
```

Expected: fails to compile — `DISP_PAL_DYNAMIC`, `display_set_dynamic_category`,
`display_dynamic_slot` undeclared.

- [ ] **Step 3: Declare in `display.h`**

Add near `DISP_IMAGE_NONE`:

```c
/*----------------------
 | DISP_PAL_DYNAMIC
 | Description: The palette index that means "let the text category choose the
 |   picture". It sits at 0, ahead of the colour presets, and always exists so the
 |   display_preset_* accessors stay unconditional -- display_cycle_palette skips
 |   it instead when the disc carries no art.
 | Author: suinevere
 ----------------------*/
#define DISP_PAL_DYNAMIC 0

/*----------------------
 | display_set_dynamic_category / display_dynamic_slot
 | Description: set_dynamic_category resolves a text category to an image slot and
 |   remembers it, ignoring any category with no art so the wallpaper holds;
 |   dynamic_slot returns that slot. It stores the resolved SLOT rather than the
 |   raw category so "keep current" is never a transient answer: cycling onto
 |   Dynamic during a TC_DANGER moment would otherwise have no current picture to
 |   keep and would land on no wallpaper at all. It returns DISP_IMAGE_NONE only
 |   on a disc with no art.
 | Author: suinevere
 ----------------------*/
void display_set_dynamic_category(int cat);
int  display_dynamic_slot(void);
```

- [ ] **Step 4: Implement in `display.c`**

Add the state and the two functions after `image_slot_of`:

```c
/*----------------------
 | g_dyn_slot
 | Description: The image slot the Dynamic palette is currently showing. Seeded by
 |   display_set_images from TC_NEUTRAL's picture so Dynamic has something to show
 |   before any room has been classified.
 | Author: suinevere
 ----------------------*/
static int g_dyn_slot = DISP_IMAGE_NONE;

void display_set_dynamic_category(int cat) {
    const char *file = display_category_image(cat);
    int slot;
    if (!file) return;                 /* no art for this mood: hold what is showing */
    slot = image_slot_of(file);
    if (slot >= 0) g_dyn_slot = slot;  /* absent from this disc: likewise hold */
}

int display_dynamic_slot(void) {
    if (g_dyn_slot >= 0 && g_dyn_slot < g_image_count) return g_dyn_slot;
    return DISP_IMAGE_NONE;
}
```

At the end of `display_set_images`, seed the slot:

```c
    /* Seed Dynamic from the neutral picture, so it shows something before any
       room has been classified -- and re-seed on every scan, because the slot is
       an index into the list that just changed. */
    g_dyn_slot = image_slot_of(display_category_image(TC_NEUTRAL));
```

Now shift the palette accessors. Replace the four of them with:

```c
int display_palette_count(void) { return 1 + DISP_PRESET_N + g_image_count; }

int display_preset_image(int index) {
    if (index == DISP_PAL_DYNAMIC) return display_dynamic_slot();
    if (index <= DISP_PRESET_N || index >= display_palette_count()) return DISP_IMAGE_NONE;
    return index - DISP_PRESET_N - 1;
}

const char *display_preset_name(int index) {
    if (index < 0 || index >= display_palette_count()) return "?";
    if (index == DISP_PAL_DYNAMIC) return "Dynamic";
    if (index > DISP_PRESET_N) return display_image_label(index - DISP_PRESET_N - 1);
    return PRESETS[index - 1].name;
}

int display_preset_bg(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_BG_BLACK;
    if (index == DISP_PAL_DYNAMIC || index > DISP_PRESET_N) return DISP_BG_BLACK;
    return PRESETS[index - 1].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_TEXT_BRIGHT_GREEN;
    if (index == DISP_PAL_DYNAMIC || index > DISP_PRESET_N) return DISP_TEXT_WHITE;
    return PRESETS[index - 1].text;
}
```

Update each one's doc comment for the new index layout, and note in
`display_preset_bg` / `display_preset_text` that Dynamic takes the same
black-and-white treatment as the image presets, for the same reason.

Replace `display_defaults`:

```c
void display_defaults(DisplayState *d) {
    if (g_image_count > 0) {
        /* Dynamic: the picture follows the room, which is the shipped default. */
        d->palette = DISP_PAL_DYNAMIC;
        d->bg      = DISP_BG_BLACK;
        d->text    = DISP_TEXT_WHITE;
        d->image   = display_dynamic_slot();
    } else {
        /* No art on the disc, so there is nothing for Dynamic to show. */
        d->palette = 1;                        /* IBM PC (MDA): closest to the */
        d->bg      = PRESETS[0].bg;            /* previous hardcoded appearance */
        d->text    = PRESETS[0].text;
        d->image   = DISP_IMAGE_NONE;
    }
}
```

In `display_cycle_palette`, skip Dynamic when there is no art. After the existing
`next = step(...)` / custom-label branch and before `d->palette = next;`:

```c
    /* Dynamic always holds index 0 so the accessors above can stay
       unconditional, but it has nothing to show on a disc with no art -- so it
       is stepped over here rather than changing the shape of the row. */
    if (next == DISP_PAL_DYNAMIC && g_image_count == 0)
        next = step(next, dir, count);
```

- [ ] **Step 5: Run the tests**

```bash
gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
python saturn/tests/test_category_art.py
```

Expected: `test_display: OK` and `test_category_art: OK`.

If `test_tables_well_formed` fails, it is looping `i < DISP_PRESET_N` over raw
indices — update it to `display_preset_name(i + 1)` so it still checks the colour
presets rather than sliding onto Dynamic.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/display.h saturn/src/video/display.c saturn/tests/test_display.c
git commit -m "feat(display): Dynamic palette at index 0, and as the default

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 7: Persist the Dynamic palette

Blob sentinel 3 → 4. Older blobs still decode, with colour-preset indices shifted by one into the new space — this is the one change whose failure is invisible until a player reboots with existing settings, so it gets a direct test.

**Files:**
- Modify: `saturn/src/video/display.h` (doc), `saturn/src/video/display.c` (`display_encode`/`display_decode`)
- Modify: `saturn/tests/test_display.c` (append)

**Interfaces:**
- Consumes: `DISP_PAL_DYNAMIC` (Task 6).
- Produces: sentinel-4 blobs. `DISP_BLOB_BYTES` is unchanged, so `options_save`'s 62-byte budget is unaffected.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_display.c` and call it from `main()`:

```c
static void test_blob_roundtrip(void) {
    unsigned char buf[DISP_BLOB_BYTES];
    DisplayState a, b;

    display_set_images(IMGS, 8);

    /* Dynamic survives a round trip. */
    display_defaults(&a);
    assert(a.palette == DISP_PAL_DYNAMIC);
    assert(display_encode(&a, buf) == DISP_BLOB_BYTES);
    assert(buf[0] == 4);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == DISP_PAL_DYNAMIC);
    assert(b.bg == a.bg && b.text == a.text);

    /* A colour preset survives a round trip in the new space. */
    a.palette = 5; a.bg = display_preset_bg(5); a.text = display_preset_text(5);
    a.image = DISP_IMAGE_NONE;
    display_encode(&a, buf);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == 5);

    /* An image preset survives, resolved by name. */
    a.palette = DISP_PRESET_N + 1 + 3;      /* CLIFF.TGA */
    a.bg = DISP_BG_BLACK; a.text = DISP_TEXT_WHITE; a.image = 3;
    display_encode(&a, buf);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == DISP_PRESET_N + 1 + 3);
    assert(b.image == 3);

    /* A sentinel-3 blob written before Dynamic existed: its colour preset index
       is in the OLD space and must shift up by one, or every saved appearance
       silently moves one entry along the row. */
    {
        unsigned char old[DISP_BLOB_BYTES];
        int i;
        for (i = 0; i < DISP_BLOB_BYTES; i++) old[i] = 0;
        old[0] = 3;                          /* pre-Dynamic sentinel */
        old[1] = 5;                          /* old colour-preset index 5 */
        old[2] = (unsigned char) display_preset_bg(6);
        old[3] = (unsigned char) display_preset_text(6);
        assert(display_decode(old, DISP_BLOB_BYTES, &b) == 1);
        assert(b.palette == 6);              /* 5 in the old space == 6 in the new */
        assert(b.image == DISP_IMAGE_NONE);
    }

    /* A sentinel-1 blob shifts the same way. */
    {
        unsigned char old[4];
        old[0] = 1; old[1] = 0;              /* old preset 0 */
        old[2] = (unsigned char) display_preset_bg(1);
        old[3] = (unsigned char) display_preset_text(1);
        assert(display_decode(old, 4, &b) == 1);
        assert(b.palette == 1);
    }
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
```

Expected: assertion failure — the encoder still writes sentinel 3.

- [ ] **Step 3: Implement**

In `display.c`, near `DISP_BLOB_IMAGE`, add:

```c
/*----------------------
 | DISP_BLOB_DYNAMIC
 | Description: The palette byte's marker for the Dynamic palette, which has no
 |   stored picture name because the picture is chosen at runtime. Distinct from
 |   DISP_BLOB_IMAGE (0xFF), which does carry one.
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_DYNAMIC 0xFE
```

Replace `display_encode`'s first lines:

```c
    out[0] = 4;                                /* block sentinel: + Dynamic palette */
    out[1] = (d->palette == DISP_PAL_DYNAMIC) ? DISP_BLOB_DYNAMIC
           : (d->palette >  DISP_PRESET_N)    ? DISP_BLOB_IMAGE
           : (unsigned char) d->palette;
```

and its name lookup, which must use the new image-slot arithmetic:

```c
    if (d->image >= 0)                    name = image_slot_name(d->image);
    else if (d->palette > DISP_PRESET_N)  name = image_slot_name(d->palette - DISP_PRESET_N - 1);
```

In `display_decode`, the sentinel-1 branch shifts its colour index:

```c
    if (buf[0] == 1) {
        /* Original form: slot numbers, no name, and palette indices from before
           Dynamic took index 0 -- so a colour preset shifts up by one. An image
           slot still cannot be trusted, so it is refused not guessed. */
        if (buf[1] < DISP_PRESET_N)   d->palette = (int) buf[1] + 1;  else ok = 0;
```

Change the `buf[0] == 2 || buf[0] == 3` branch to also accept 4, and shift only
the older forms. Replace its palette resolution with:

```c
        if (buf[1] == DISP_BLOB_DYNAMIC) {
            /* Only sentinel 4 writes this, and only when the disc had art. */
            if (g_image_count > 0) d->palette = DISP_PAL_DYNAMIC; else ok = 0;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            if (slot >= 0) d->palette = DISP_PRESET_N + 1 + slot;  else ok = 0;
        } else if (buf[1] < DISP_PRESET_N) {
            /* Sentinels 2 and 3 predate Dynamic, so their colour-preset indices
               are in the old space and shift up by one. Sentinel 4 already
               stores the new index. */
            d->palette = (buf[0] == 4) ? (int) buf[1] : (int) buf[1] + 1;
        } else {
            ok = 0;
        }
```

Guard the branch condition itself: `} else if (buf[0] == 2 || buf[0] == 3 || buf[0] == 4) {`.

Finally, for Dynamic the image must follow the engine rather than the blob — after
the palette resolution, ensure:

```c
        if (d->palette == DISP_PAL_DYNAMIC) d->image = display_dynamic_slot();
```

Update the `DISP_BLOB_BYTES` doc block in `display.h` to describe sentinel 4 and
the +1 shift applied to sentinels 1–3.

- [ ] **Step 4: Run the tests**

```bash
gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
python saturn/tests/test_category_art.py
```

Expected: `test_display: OK`, `test_category_art: OK`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/display.h saturn/src/video/display.c saturn/tests/test_display.c
git commit -m "feat(display): persist the Dynamic palette (blob sentinel 4)

Older blobs still decode; their colour-preset indices shift up by one into the
space Dynamic now occupies at index 0.

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 8: Saturn wiring

The NBG0-only fade, the category handler, and the one index the fallback path hardcodes.

**Files:**
- Modify: `saturn/src/video/title.h`, `saturn/src/video/title.cxx` (NBG0 fade)
- Modify: `saturn/src/menu/options.cxx` (fallback preset index)
- Modify: `saturn/src/main.cxx` (install the handlers)

**Interfaces:**
- Consumes: `music_set_category_fn` (Task 2), `music_set_fade_fn`/`music_set_fade_frames` (Task 4), `display_set_dynamic_category`/`display_dynamic_slot`/`DISP_PAL_DYNAMIC` (Task 6).
- Produces: `void title_bg_dyn_fade(int level);`

- [ ] **Step 1: Add the NBG0-only fade to `title.cxx`**

Add after `title_bg_fade_reset`:

```c
/*----------------------
 | title_bg_dyn_fade
 | Description: Dims the wallpaper alone, for the in-game transition between one
 |   room mood's picture and the next. `level` runs 0 (black) to 255 (normal).
 |
 |   This cannot go through title_fade_engage: that points NBG0 AND NBG3 at
 |   channel A so the title screen dims as a unit, which in game would blink the
 |   player's text out mid-sentence. The wallpaper gets channel B on NBG0 alone,
 |   leaving channel A entirely to the screen-wide page and title fades.
 |
 |   NBG3 has to be cleared off channel B explicitly first. SRL seeds
 |   OffsetBScrolls to NBG3ON (srl_vdp2.hpp:334), and UseColorOffset re-registers
 |   BOTH masks from those bitfields on every call -- so engaging NBG0 on B without
 |   this would drag the text onto B as well and fade exactly what must stay lit.
 |
 |   Engage/disengage happen only at the ends of a ramp, because UseColorOffset
 |   calls slColOffsetOn(0) and re-registers; the per-frame steps in between are
 |   just SetColorOffsetB value writes.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_dyn_faded
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: N/A
 ----------------------*/
static bool g_dyn_faded = false;

void title_bg_dyn_fade(int level) {
    if (level < 0)   level = 0;
    if (level > 255) level = 255;

    if (level >= 255) {
        if (g_dyn_faded) {
            SRL::VDP2::ColorOffset clear(0, 0, 0);
            SRL::VDP2::SetColorOffsetB(clear);
            SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
            g_dyn_faded = false;
        }
        return;
    }

    if (!g_dyn_faded) {
        // Take NBG3 off both channels BEFORE putting NBG0 on B -- see above.
        SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
        SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetB);
        g_dyn_faded = true;
    }
    SRL::VDP2::ColorOffset off((int16_t)(level - 255), (int16_t)(level - 255),
                               (int16_t)(level - 255));
    SRL::VDP2::SetColorOffsetB(off);
}
```

Declare it in `saturn/src/video/title.h` beside `title_bg_fade_reset`, with a
short doc block pointing at the implementation for the channel reasoning.

- [ ] **Step 2: Fix the hardcoded fallback index in `options.cxx`**

In `display_apply`'s image-load failure path, the fallback preset index moved with
everything else. Change:

```c
            if (p >= DISP_PRESET_N || p < 0) p = 12;   // IBM PC (MDA), the startup default
```

to:

```c
            // Palette indices shifted by one when Dynamic took index 0.
            if (p > DISP_PRESET_N || p < 1) p = 13;   // IBM PC (MDA)
```

- [ ] **Step 3: Install the handlers in `main.cxx`**

Add the two handlers above `main()` (near the other file-scope helpers):

```c
/*----------------------
 | on_text_category
 | Description: The background art's half of a text-category change. Moves the
 |   Dynamic palette's picture to the new mood's art and repaints; a category with
 |   no art of its own leaves display_set_dynamic_category's stored slot alone, so
 |   this re-requests the picture already showing and title_bg_show short-circuits
 |   -- the wallpaper holds without a special case here.
 |
 |   Goes through display_apply rather than title_bg_show directly, so a picture
 |   that fails to load takes the same colour-preset fallback every other display
 |   change does.
 | Author: suinevere
 | Dependencies: display.h, options.h
 | Globals: g_display
 | Params: cat -- the TC_* category now sounding
 | Returns: N/A
 ----------------------*/
static void on_text_category(int cat) {
    display_set_dynamic_category(cat);
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    int slot = display_dynamic_slot();
    if (slot == DISP_IMAGE_NONE) return;      // disc carries no art
    g_display.image = slot;
    display_apply();
}

/*----------------------
 | on_music_fade
 | Description: Drives one step of the transition ramp: the wallpaper's brightness
 |   and the CD-DA volume together, off the engine's single counter.
 |
 |   The volume floor is 1, never 0, and that is load-bearing rather than
 |   cosmetic: music_set_volume(0) calls StopPause(), which halts the drive
 |   outright, and music_set_volume has no resurrect path -- the ramp back up
 |   would raise a volume on a stopped disc and the music would simply be gone.
 |   Level 1 is quiet enough, and the swap at the bottom of the ramp re-issues the
 |   track anyway.
 |
 |   A player who has set Music to 0 keeps silence: the fade never raises what
 |   they turned off.
 | Author: suinevere
 | Dependencies: title.h, music.h, app_state.h
 | Globals: g_music_level
 | Params: level -- 0 (black/quiet) to 255 (normal)
 | Returns: N/A
 ----------------------*/
static void on_music_fade(int level) {
    title_bg_dyn_fade(level);
    if (g_music_level <= 0) return;
    int v = 1 + ((g_music_level - 1) * level) / 255;
    if (v < 1)               v = 1;
    if (v > g_music_level)   v = g_music_level;
    music_set_volume(v);
}
```

Then install them immediately after the `display_apply();` on line 184 — **after**,
deliberately: the title screen shows its own `HOUSE.TGA` and `music_start_menu()`
on line 180 announces the neutral seed, and neither should repaint the title.

```c
    display_apply();              // set the menu's background image/colour + text
    // Installed here rather than beside the other music callbacks above: the
    // title screen shows HOUSE.TGA explicitly and music_start_menu() announces
    // the neutral seed, and neither is meant to repaint the title behind them.
    music_set_category_fn(on_text_category);
    music_set_fade_fn(on_music_fade);
    music_set_fade_frames(MUSIC_FADE_FRAMES);
```

Add near the other frame-count constants in `main.cxx`:

```c
/*----------------------
 | MUSIC_FADE_FRAMES
 | Description: Half the length of a room-mood transition, in frames: the
 |   wallpaper and music ramp down over this many, swap, and ramp back up over as
 |   many again. 20 is a third of a second each way, which lands a mood change a
 |   little over two seconds after the player stops moving once the 90-frame
 |   settle is counted. This is the number to cut if that reads sluggish -- not
 |   the settle, which is what stops fast movement from thrashing.
 | Author: suinevere
 ----------------------*/
#define MUSIC_FADE_FRAMES 20
```

- [ ] **Step 4: Type-check both configurations**

```bash
sh saturn/syntax-check.sh src/main.cxx src/video/title.cxx src/menu/options.cxx src/video/display.c
NETBIN=1 sh saturn/syntax-check.sh src/video/display.c src/menu/options.cxx
```

Expected: no errors from either invocation, for both DEBUG and release.

Do **not** run `compile.bat` — the user runs real builds.

- [ ] **Step 5: Re-run every host test**

```bash
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c          saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mt
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c      saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mmt
gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct
gcc -O2 -I saturn/src -o /tmp/td  saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td
python saturn/tests/test_category_art.py
python saturn/tests/test_netbin_sources.py
python saturn/tests/test_lwram_splash_budget.py
```

Expected: all pass. `test_netbin_sources.py` matters here — it confirms no new
source file crept onto the netbin's 18-object list.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/title.h saturn/src/video/title.cxx \
        saturn/src/menu/options.cxx saturn/src/main.cxx
git commit -m "feat: wire the Dynamic palette to the engine's category changes

The wallpaper fade drives colour offset channel B on NBG0 alone, after clearing
NBG3 off it -- SRL seeds OffsetBScrolls to NBG3ON and UseColorOffset re-registers
both masks, so engaging NBG0 naively would fade the game text too.

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

---

### Task 9: Documentation and on-device verification

**Files:**
- Modify: `docs/superpowers/specs/2026-07-30-dynamic-background-art-design.md` (status)
- Modify: `README.md` if it documents Display Options

- [ ] **Step 1: Check whether the README describes the Palette row**

```bash
grep -n "Palette\|Display Options\|background" README.md | head -20
```

If it lists the palette entries or the default appearance, add Dynamic and say it
is the default: the picture follows the room's mood, and the two event moods hold
the current picture rather than flicking.

- [ ] **Step 2: Mark the spec implemented**

Change the spec's `**Status:** Approved for planning` to
`**Status:** Implemented 2026-07-30`.

- [ ] **Step 3: Commit the docs**

```bash
git add docs/superpowers/specs/2026-07-30-dynamic-background-art-design.md README.md
git commit -m "docs: record the Dynamic palette as shipped

Claude-Session: https://claude.ai/code/session_014tM8ZtkSXcPPkDHfCtcw3U"
```

- [ ] **Step 4: Hand the tree back for a real build and record the on-device results**

The user runs `saturn/compile.bat` and the emulator. Checklist to report against:

- Display Options shows **Dynamic** first in the Palette row, and it is the
  default on cleared backup RAM.
- Walking between rooms of different moods changes picture and track together,
  through a fade rather than a cut.
- **The game text stays at full brightness throughout the fade** — this is the
  channel-B split, and the most likely thing to be wrong.
- Moving room-to-room faster than 1.5s changes neither until you stop.
- Combat/treasure text shifts the track but not the picture.
- Cycling the Palette row in Display Options still swaps instantly, with no fade.
- An existing saved palette still selects the same appearance after the sentinel
  bump (test with a backup RAM saved before this change, if one exists).
- The loading screen still hides and restores the wallpaper with no flash.
- No audible skip in the music when the picture changes (all eight pictures
  should be cache-resident; a skip means one failed to cache).
- No tearing on the picture swap.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|---|---|
| §1 category notification | Task 2 |
| §2 debounce 1.5s + restart on room change | Task 3 |
| §3 transition fade (engine phases) | Task 4 |
| §3 fade: NBG0-only channel, volume floor | Task 8 |
| §3 Options unaffected | Task 8 (no code needed — verified in Task 9 checklist) |
| §4 category → picture table | Task 5 |
| §5 Dynamic palette indexing + defaults | Task 6 |
| §6 persistence, sentinel 4, +1 shift | Task 7 |
| §7 Saturn wiring | Task 8 |
| Rename | Task 1 |
| Error handling table | Tasks 5 (NULL/absent), 6 (no art on disc), 8 (load failure) |
| Testing | Tasks 2–8 host tests; Task 9 on-device |

**Placeholder scan:** No TBD/TODO. Every code step carries the actual code. The
one judgement left to the implementer is Task 1 Step 3 (rewriting English prose in
doc comments), which is stated as a concrete instruction against named blocks.

**Type consistency:** `music_set_category_fn(void (*)(int))` and
`music_set_fade_fn(void (*)(int))` are declared in Task 2/4 and installed in Task 8
with matching handler signatures (`on_text_category(int)`, `on_music_fade(int)`).
`display_category_image(int) -> const char *` is produced in Task 5 and consumed in
Task 6. `display_set_dynamic_category(int)` / `display_dynamic_slot(void) -> int` /
`DISP_PAL_DYNAMIC` are produced in Task 6 and consumed in Tasks 7 and 8.
`title_bg_dyn_fade(int)` is produced and consumed within Task 8.

**Deviation from the spec, recorded:** the spec put the category→picture table in a
new `text_art.c`. Task 5 puts it in `display.c` instead, because `display.c` is on
the NETBIN source list that `test_netbin_sources.py` pins to exactly 18 objects — a
new file it depended on would break the netbin link and that test. The tradeoff is
that `display.c` gains an `#include "sound/music.h"`, which `options.cxx` (also on
that list) already carries.

**Correction to the spec:** the spec's error-handling table said the fade floor was
silence. It is volume level **1**, because `music_set_volume(0)` calls
`StopPause()` and there is no resurrect path in that function — a 0 would stop the
drive and the ramp back up would never restart it. Task 8 documents this at the
call site.
