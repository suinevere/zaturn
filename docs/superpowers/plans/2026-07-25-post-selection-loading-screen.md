# Post-Selection Loading Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the black screen shown between picking a game and the story loading with a typed-out retro-OS boot screen (fading in/out) with a fading `LOADCD.PCM` audio cue underneath.

**Architecture:** A pure-logic text-formatting module (`loading_text.c`, host-testable) builds the 11-line boot block with the game's title substituted and truncated to fit. A hardware orchestration module (`loading_screen.cxx`) drives the VDP2 color-offset fade, the typewriter draw loop, and a new PCM module (`loading_music.cxx`, mirroring the existing `boot_music.cxx`) that loads/fades/plays `LOADCD.PCM`. `main.cxx` calls it once, right after `game_select()` returns and before the existing CD-read loop.

**Tech Stack:** SH-2 cross-compiled C/C++ against SaturnRingLib (SRL) + SGL, built via `saturn/compile.bat` (do not run it yourself — see Global Constraints). Host-side pure-C modules are additionally verified with plain `gcc` per `saturn/tests/`'s existing convention.

## Global Constraints

- Never run `saturn/compile.bat` yourself — the user builds; make edits and let them compile. [[do-not-run-compile]] [[build-with-compile-bat]]
- The Saturn debug console is a hard 40 columns wide (`MENU_SCREEN_COLS`/`CONSOLE_COLS` both confirm it) and 0-28 rows tall.
- `SRL::Sound::Pcm::SetVolumePan` kills a running PCM channel outright — never use it for a live fade in either direction. Fade-in must be baked into sample bytes before `Play()`; fade-out must go through the SGL driver's *master* volume (`SND_SetTlVl`), exactly as `boot_music.cxx` already does. [[srl-pcm-live-volume-does-not-work]]
- `SRL::Debug::Print` only supports `%c %s %d %0Nd` format specifiers. [[srl-debug-print-format-limits]]
- Any new plain-C header included from a `.cxx` file needs either its own `#ifdef __cplusplus extern "C" {}` guard (like `boot_music.h`/`splash.h`) or an external `extern "C" { #include "..." }` wrap at the call site (like `display.h`/`music.h` in `main.cxx`) — never both.
- Every new `.c`/`.cxx` file under `saturn/src/` is picked up automatically by `saturn/makefile`'s `find`-based glob — no manual registration needed.
- Verify hardware-dependent C++ changes with `saturn/syntax-check.sh <file>` (type-checks against the real SRL/SGL headers via `-fsyntax-only`, both DEBUG and release configs, no object files written) before considering a task done — this is the closest thing to a build check available without hardware/an emulator.
- Verify pure-C changes by actually compiling and running their host test with `gcc`, per `saturn/tests/`'s existing convention (see `test_menu_layout.c` for the established style: `assert()`-based, one `main()` that calls every test function).

---

## File Structure

| File | Responsibility |
|---|---|
| `tools/assets/pvms.bat` (modify) | Add the `loadCD.ogg` → `LOADCD.PCM` conversion call |
| `saturn/src/video/loading_text.h` / `.c` (new) | Pure logic: builds the 11-line boot text block, title substituted and truncated to fit 40 columns. No SRL/hardware dependency — host-testable. |
| `saturn/tests/test_loading_text.c` (new) | Host-side unit tests for `loading_text.c` |
| `saturn/src/sound/msc_dir.h` / `.cxx` (new) | Shared `/MSC` CD-directory-entry helper, extracted from `boot_music.cxx`'s private copy |
| `saturn/src/sound/boot_music.cxx` (modify) | Drop its private `cd_enter_msc`, use the shared one |
| `saturn/src/sound/loading_music.h` / `.cxx` (new) | Loads/fades-in/plays/fades-out/frees `LOADCD.PCM` — mirrors `boot_music.{h,cxx}` |
| `saturn/src/menu/game_catalog.h` / `.cxx` (modify) | Add `game_catalog_title_for(filename)` lookup |
| `saturn/src/video/loading_screen.h` / `.cxx` (new) | Orchestrates the whole screen: pause CD-DA, fade in screen+audio, type text, fade out, restore colors |
| `saturn/src/main.cxx` (modify) | Call `loading_screen()` after `game_select()` succeeds |

---

### Task 1: `pvms.bat` — convert `loadCD.ogg` to `LOADCD.PCM`

**Files:**
- Modify: `tools/assets/pvms.bat`

**Interfaces:**
- Consumes: the existing generic `convert_boot_music(src_ogg, out_dir, out_name)` shell function (`tools/assets/lib/pvms.sh`) and its PowerShell equivalent (`tools/assets/lib/pvms.ps1`) — both unchanged.
- Produces: `saturn/cd/data/MSC/LOADCD.PCM`, consumed by Task 3's `loading_music_load()`.

- [ ] **Step 1: Add the Linux/macOS conversion call**

In `tools/assets/pvms.bat`, in the `:; # === Linux & macOS Execution Block ===` section, right after the existing `convert_boot_music` call for `WISDOM.PCM`:

```
:; convert_boot_music "music/A Brand New Wisdom.ogg" "../../saturn/cd/data/MSC" "WISDOM.PCM"
:; convert_boot_music "music/loadCD.ogg" "../../saturn/cd/data/MSC" "LOADCD.PCM"
:; exit
```

- [ ] **Step 2: Add the Windows conversion call**

In the `@ECHO OFF` / `REM === Windows Execution Block ===` section, right after the existing `WISDOM.PCM` block:

```bat
SET "SRL_SOX=%~dp0..\..\SaturnRingLib\Compiler\msys2\usr\bin\sox.exe"
SET "BOOT_MUSIC_SRC=%~dp0music\A Brand New Wisdom.ogg"
SET "BOOT_MUSIC_OUT=%~dp0..\..\saturn\cd\data\MSC"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\pvms.ps1" -Sox "%SRL_SOX%" -InFile "%BOOT_MUSIC_SRC%" -OutDir "%BOOT_MUSIC_OUT%" -OutName "WISDOM.PCM"

SET "LOADCD_MUSIC_SRC=%~dp0music\loadCD.ogg"
SET "LOADCD_MUSIC_OUT=%~dp0..\..\saturn\cd\data\MSC"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\pvms.ps1" -Sox "%SRL_SOX%" -InFile "%LOADCD_MUSIC_SRC%" -OutDir "%LOADCD_MUSIC_OUT%" -OutName "LOADCD.PCM"
```

- [ ] **Step 3: Run the conversion for real and verify the output**

`tools/assets/music/loadCD.ogg` already exists in the repo, and `sox` is available at `SaturnRingLib/Compiler/msys2/usr/bin/sox.exe`, so this can be verified end-to-end right now (not just read-through):

Run (from `tools/assets/`, in a POSIX shell — this exercises the same `convert_boot_music` function `pvms.bat`'s Linux block calls):
```bash
cd tools/assets
. lib/pvms.sh
SRL_SOX="../../SaturnRingLib/Compiler/msys2/usr/bin/sox.exe"
convert_boot_music "music/loadCD.ogg" "../../saturn/cd/data/MSC" "LOADCD.PCM"
ls -la ../../saturn/cd/data/MSC/LOADCD.PCM
```
Expected: `Converted boot music -> ../../saturn/cd/data/MSC/LOADCD.PCM` printed, and the file exists with a non-zero size (roughly `duration_seconds * 22050` bytes for 8-bit mono).

- [ ] **Step 4: Commit**

```bash
git add tools/assets/pvms.bat saturn/cd/data/MSC/LOADCD.PCM
git commit -m "Convert loadCD.ogg to LOADCD.PCM in pvms.bat"
```

---

### Task 2: `loading_text` — pure boot-text builder (TDD)

**Files:**
- Create: `saturn/src/video/loading_text.h`
- Create: `saturn/src/video/loading_text.c`
- Test: `saturn/tests/test_loading_text.c`

**Interfaces:**
- Consumes: nothing (pure C, no project dependencies)
- Produces: `LOADING_TEXT_COLS` (40), `LOADING_TEXT_LINES` (11), and
  `void loading_text_build(const char *title, char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]);`
  — consumed by Task 5's `loading_screen.cxx`.

- [ ] **Step 1: Write the header**

Create `saturn/src/video/loading_text.h`:

```c
/*----------------------
 | loading_text.h
 | Description: Builds the post-selection loading screen's fixed boot-text
 |   block, substituting the chosen game's title into the two spots that
 |   need it and truncating per-line so no row ever exceeds the console's 40
 |   columns. Pure string logic, no SRL/hardware dependency -- host-testable
 |   with plain gcc (see saturn/tests/test_loading_text.c).
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef LOADING_TEXT_H
#define LOADING_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOADING_TEXT_COLS  40
#define LOADING_TEXT_LINES 11

/*----------------------
 | loading_text_build
 | Description: Fills `lines` with the 11-row boot sequence, NUL-terminated
 |   per row, substituting `title` for the LOAD/SEARCHING FOR lines' target
 |   and truncating it (a plain byte cut, no ellipsis) if the fixed
 |   prefix/suffix on that row would otherwise push past LOADING_TEXT_COLS.
 |   A NULL title is treated as an empty string.
 | Author: suinevere
 | Dependencies: none
 | Params: title -- the game's display title, or NULL; lines -- output,
 |   caller-owned
 | Returns: N/A
 ----------------------*/
void loading_text_build(const char *title, char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]);

#ifdef __cplusplus
}
#endif
#endif /* LOADING_TEXT_H */
```

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_loading_text.c`:

```c
#include "../src/video/loading_text.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void test_fixed_lines_match_exactly(void) {
    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build("ZORK I", lines);

    assert(strcmp(lines[0], "**** SEGA SATURN 32-BIT OS V1.00 ****") == 0);
    assert(strcmp(lines[1], "") == 0);
    assert(strcmp(lines[2], "2048K RAM SYSTEM  2093056 SYS BYTES FREE") == 0);
    assert(strcmp(lines[3], "") == 0);
    assert(strcmp(lines[4], "READY!") == 0);
    assert(strcmp(lines[6], "") == 0);
    assert(strcmp(lines[8], "LOADING FROM CD-ROM BLOCK...") == 0);
    assert(strcmp(lines[9], "READY!") == 0);
    assert(strcmp(lines[10], "RUN") == 0);
}

static void test_short_title_appears_in_full(void) {
    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build("ZORK I", lines);

    assert(strcmp(lines[5], "LOAD ZORK I,8,1") == 0);
    assert(strcmp(lines[7], "SEARCHING FOR ZORK I") == 0);
}

static void test_max_length_title_load_line_fits_exactly(void) {
    /* 31 chars -- the catalogue's MENU_ROW_TEXT_MAX cap on a display title. */
    const char *title31 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";
    assert(strlen(title31) == 31);

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title31, lines);

    assert(strcmp(lines[5], "LOAD ABCDEFGHIJKLMNOPQRSTUVWXYZ01234,8,1") == 0);
    assert(strlen(lines[5]) == 40);
}

static void test_max_length_title_truncates_on_searching_line(void) {
    const char *title31 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title31, lines);

    /* "SEARCHING FOR " is 14 cols, leaving a 26-char budget. */
    assert(strcmp(lines[7], "SEARCHING FOR ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0);
    assert(strlen(lines[7]) == 40);
}

static void test_overlong_title_truncates_safely(void) {
    char title60[61];
    memset(title60, 'A', 60);
    title60[60] = '\0';

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title60, lines);

    assert(strlen(lines[5]) == 40);
    assert(strlen(lines[7]) == 40);
    assert(strcmp(lines[5], "LOAD AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA,8,1") == 0);
    assert(strcmp(lines[7], "SEARCHING FOR AAAAAAAAAAAAAAAAAAAAAAAAAA") == 0);
}

static void test_null_title_treated_as_empty(void) {
    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(NULL, lines);

    assert(strcmp(lines[5], "LOAD ,8,1") == 0);
    assert(strcmp(lines[7], "SEARCHING FOR ") == 0);
}

static void test_no_line_ever_exceeds_console_width(void) {
    const char *titles[] = { "", "ZORK I", "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234" };
    for (int t = 0; t < 3; t++) {
        char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
        loading_text_build(titles[t], lines);
        for (int row = 0; row < LOADING_TEXT_LINES; row++) {
            assert(strlen(lines[row]) <= LOADING_TEXT_COLS);
        }
    }
}

int main(void) {
    test_fixed_lines_match_exactly();
    test_short_title_appears_in_full();
    test_max_length_title_load_line_fits_exactly();
    test_max_length_title_truncates_on_searching_line();
    test_overlong_title_truncates_safely();
    test_null_title_treated_as_empty();
    test_no_line_ever_exceeds_console_width();
    printf("test_loading_text: OK\n");
    return 0;
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run (from `saturn/`):
```bash
gcc -std=c11 -Wall -Wextra -I src tests/test_loading_text.c src/video/loading_text.c -o /tmp/test_loading_text
```
Expected: FAIL to link/compile — `src/video/loading_text.c` doesn't exist yet.

- [ ] **Step 4: Write the minimal implementation**

Create `saturn/src/video/loading_text.c`:

```c
/*----------------------
 | loading_text.c
 | Description: See loading_text.h.
 | Author: suinevere
 | Dependencies: loading_text.h
 ----------------------*/
#include "loading_text.h"

/*----------------------
 | put_line
 | Description: Copies `prefix`, then as much of `title` as fits in the
 |   columns left over after `prefix` and `suffix`, then `suffix`, into
 |   `out` (a LOADING_TEXT_COLS+1 byte row), NUL-terminated. Every copy loop
 |   is bounded by LOADING_TEXT_COLS as a hard backstop, independent of the
 |   budget arithmetic, since this writes into a fixed-size caller buffer.
 | Author: suinevere
 | Params: out -- LOADING_TEXT_COLS+1 bytes; prefix, title, suffix -- NUL-
 |   terminated, none may be NULL (title is NULL-checked by the caller)
 | Returns: N/A
 ----------------------*/
static void put_line(char *out, const char *prefix, const char *title, const char *suffix) {
    int i = 0;
    for (const char *p = prefix; *p && i < LOADING_TEXT_COLS; p++) out[i++] = *p;

    int suffix_len = 0;
    while (suffix[suffix_len]) suffix_len++;

    int budget = LOADING_TEXT_COLS - i - suffix_len;
    if (budget < 0) budget = 0;
    for (const char *t = title; *t && budget > 0 && i < LOADING_TEXT_COLS; t++, budget--) out[i++] = *t;

    for (const char *s = suffix; *s && i < LOADING_TEXT_COLS; s++) out[i++] = *s;
    out[i] = '\0';
}

void loading_text_build(const char *title, char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]) {
    if (!title) title = "";

    put_line(lines[0],  "**** SEGA SATURN 32-BIT OS V1.00 ****", "", "");
    put_line(lines[1],  "", "", "");
    put_line(lines[2],  "2048K RAM SYSTEM  2093056 SYS BYTES FREE", "", "");
    put_line(lines[3],  "", "", "");
    put_line(lines[4],  "READY!", "", "");
    put_line(lines[5],  "LOAD ", title, ",8,1");
    put_line(lines[6],  "", "", "");
    put_line(lines[7],  "SEARCHING FOR ", title, "");
    put_line(lines[8],  "LOADING FROM CD-ROM BLOCK...", "", "");
    put_line(lines[9],  "READY!", "", "");
    put_line(lines[10], "RUN", "", "");
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run (from `saturn/`):
```bash
gcc -std=c11 -Wall -Wextra -I src tests/test_loading_text.c src/video/loading_text.c -o /tmp/test_loading_text && /tmp/test_loading_text
```
Expected: `test_loading_text: OK`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/loading_text.h saturn/src/video/loading_text.c saturn/tests/test_loading_text.c
git commit -m "Add loading_text: pure boot-text builder for the loading screen"
```

---

### Task 3: `msc_dir` (shared) + `loading_music` — PCM load/fade/play module

**Files:**
- Create: `saturn/src/sound/msc_dir.h`
- Create: `saturn/src/sound/msc_dir.cxx`
- Modify: `saturn/src/sound/boot_music.cxx:11` (include), `:53-77` (remove private `cd_enter_msc`), `:91-92` (call the shared one)
- Create: `saturn/src/sound/loading_music.h`
- Create: `saturn/src/sound/loading_music.cxx`

**Interfaces:**
- Consumes: `cd_enter_root()` from `title.h` (already exported); SRL `Cd::File`, `Sound::Pcm`, `Memory::LowWorkRam`; SGL `SND_SetTlVl`.
- Produces:
  - `bool cd_enter_msc(void);` (`msc_dir.h`) — consumed by both `boot_music.cxx` (this task) and `loading_music.cxx` (this task)
  - `LOADING_MUSIC_LEVEL_MAX` (127) and
    `void loading_music_load(void); void loading_music_fade_in(int frames); void loading_music_play(void); void loading_music_set_level(int level); void loading_music_stop(void);`
    — consumed by Task 5's `loading_screen.cxx`.

- [ ] **Step 1: Extract `cd_enter_msc` into a shared module**

Create `saturn/src/sound/msc_dir.h`:

```c
/*----------------------
 | msc_dir.h
 | Description: Shared CD-directory-entry helper for the /MSC folder, where
 |   both PCM cues in this codebase (boot_music.cxx's WISDOM.PCM,
 |   loading_music.cxx's LOADCD.PCM) live. Extracted from boot_music.cxx's
 |   original private copy once a second module needed the identical
 |   directory-table dance.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MSC_DIR_H
#define MSC_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | cd_enter_msc
 | Description: Sets the CD current directory to /MSC.
 | Author: suinevere
 | Dependencies: SRL
 | Returns: true if /MSC was found and entered
 ----------------------*/
bool cd_enter_msc(void);

#ifdef __cplusplus
}
#endif
#endif /* MSC_DIR_H */
```

Create `saturn/src/sound/msc_dir.cxx` (body moved verbatim from `boot_music.cxx`'s current private `static bool cd_enter_msc(void)`, dropping `static` and the doc comment's "private duplicate" framing):

```c++
/*----------------------
 | msc_dir.cxx
 | Description: See msc_dir.h.
 | Author: suinevere
 | Dependencies: msc_dir.h, SRL
 ----------------------*/
#include "msc_dir.h"
#include <srl.hpp>

extern "C" bool cd_enter_msc(void) {
    static GfsDirName dirnames[SRL_MAX_CD_FILES];
    static GfsDirTbl  tbl;
    int32_t fid = GFS_NameToId((int8_t *) "MSC");
    if (fid < 0) return false;
    GFS_DIRTBL_TYPE(&tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&tbl) = dirnames;
    GFS_DIRTBL_NDIR(&tbl)    = SRL_MAX_CD_FILES;
    if (GFS_LoadDir(fid, &tbl) < 0) return false;
    GFS_SetDir(&tbl);
    return true;
}
```

- [ ] **Step 2: Point `boot_music.cxx` at the shared helper**

In `saturn/src/sound/boot_music.cxx`, add the include next to the existing ones (near line 11-13):

```c++
#include "boot_music.h"
#include "title.h"
#include "msc_dir.h"
#include <srl.hpp>
```

Then delete the private `cd_enter_msc` function entirely (currently lines 53-77 — the whole `/*----------------------\n | cd_enter_msc\n ... ----------------------*/` doc comment plus the `static bool cd_enter_msc(void) { ... }` body). `boot_music_load`'s existing call `if (!cd_enter_msc()) { ... }` (around line 92) is unchanged — it now resolves to the shared, non-static one from `msc_dir.h`.

- [ ] **Step 3: Syntax-check `boot_music.cxx`**

Run (from `saturn/`):
```bash
sh syntax-check.sh src/sound/msc_dir.cxx src/sound/boot_music.cxx
```
Expected: both configs clean, exit code 0.

- [ ] **Step 4: Write the `loading_music` header**

Create `saturn/src/sound/loading_music.h`:

```c
/*----------------------
 | loading_music.h
 | Description: The post-selection loading screen's background PCM cue: a
 |   short sample loaded whole into Low Work RAM from MSC/LOADCD.PCM and
 |   played on an SCSP PCM channel. Mirrors boot_music.h's shape exactly --
 |   see that header's "the two fades, and why they work differently" box
 |   for why fade-in is baked into the sample and fade-out goes through the
 |   driver's master volume rather than the channel's own level
 |   (SRL::Sound::Pcm::SetVolumePan kills a running channel outright).
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef LOADING_MUSIC_H
#define LOADING_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | loading_music_load
 | Description: Loads MSC/LOADCD.PCM whole into a Low Work RAM buffer. A
 |   no-op if already loaded, or if the file is missing -- pvms.bat's
 |   conversion is a warning, not a hard build failure, so a build without
 |   sox/loadCD.ogg must still boot cleanly.
 | Author: suinevere
 | Dependencies: SRL (Cd::File, Memory::LowWorkRam), title.h (cd_enter_root)
 ----------------------*/
void loading_music_load(void);

/*----------------------
 | LOADING_MUSIC_LEVEL_MAX
 | Description: Full volume for the SCSP PCM channel, 0..127 scale (not the
 |   0..7 CD-DA scale music.h's calls take).
 | Author: suinevere
 ----------------------*/
#define LOADING_MUSIC_LEVEL_MAX 127

/*----------------------
 | loading_music_fade_in
 | Description: Scales the first `frames` frames of the loaded sample by a
 |   rising ramp in place. Call after loading_music_load and BEFORE
 |   loading_music_play -- it edits the buffer the SCSP is about to be
 |   handed, so it has no effect once that buffer is playing. A no-op if
 |   nothing was loaded. Not idempotent.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
void loading_music_fade_in(int frames);

/*----------------------
 | loading_music_play
 | Description: Plays the loaded sample once on a free PCM channel at full
 |   level. Any fade-in must already be baked in. A no-op if nothing was
 |   loaded or a channel is already playing it.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm)
 ----------------------*/
void loading_music_play(void);

/*----------------------
 | loading_music_set_level
 | Description: Turns the sound driver's *master* volume down to `level`,
 |   0 (silent) to LOADING_MUSIC_LEVEL_MAX (full) -- the fade-out ramp.
 |   Never touches the PCM channel's own level (see the header box above).
 |   Safe to call when nothing is playing; values outside the range are
 |   clamped. Must be put back by loading_music_stop.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl)
 ----------------------*/
void loading_music_set_level(int level);

/*----------------------
 | loading_music_stop
 | Description: Restores the driver's master volume, stops playback if
 |   active, and frees the Low Work RAM buffer. Call once the loading
 |   screen's fade-out has finished -- the typeahead trie is built moments
 |   later and needs that Low Work RAM headroom.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm, Memory), SGL (SND_SetTlVl)
 ----------------------*/
void loading_music_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* LOADING_MUSIC_H */
```

- [ ] **Step 5: Write the `loading_music` implementation**

Create `saturn/src/sound/loading_music.cxx`:

```c++
/*----------------------
 | loading_music.cxx
 | Description: Loads and plays the post-selection loading screen's PCM
 |   cue. See loading_music.h. Mirrors boot_music.cxx's load/fade/play/stop
 |   shape exactly -- same 8-bit signed mono raw format, same master-volume
 |   fade-out mechanism -- because that module already worked out the
 |   hardware traps involved. tools/assets/pvms.bat converts the source
 |   .ogg to this format with sox and writes it to cd/data/MSC/LOADCD.PCM.
 | Author: suinevere
 | Dependencies: loading_music.h, title.h (cd_enter_root), msc_dir.h
 |   (cd_enter_msc), SRL (Cd::File, Sound::Pcm, Memory::LowWorkRam)
 ----------------------*/
#include "loading_music.h"
#include "title.h"
#include "msc_dir.h"
#include <srl.hpp>

#define LOADING_MUSIC_FILE "LOADCD.PCM"
#define LOADING_MUSIC_RATE 22050

/*----------------------
 | LoadingMusicPcm
 | Description: A pre-loaded 8-bit mono PCM sample handed to SRL's PCM
 |   machinery. IPcmFile's fields are protected, so this subclass fills
 |   them via set(); the sample buffer itself is owned by
 |   g_loading_music_buf, not by this object (mirrors boot_music.cxx's
 |   BootMusicPcm).
 | Author: suinevere
 ----------------------*/
class LoadingMusicPcm : public SRL::Sound::Pcm::IPcmFile {
public:
    void set(int8_t* d, uint32_t n, uint16_t r) {
        data = d; dataSize = n; mode = _Mono; depth = _PCM8Bit; sampleRate = r;
    }
};

static int8_t*         g_loading_music_buf     = nullptr;
static uint32_t        g_loading_music_size    = 0;
static LoadingMusicPcm  g_loading_music_pcm;
static int              g_loading_music_channel = -1;

extern "C" void loading_music_load(void) {
    if (g_loading_music_buf) return;

    cd_enter_root();
    if (!cd_enter_msc()) { cd_enter_root(); return; }

    SRL::Cd::File file(LOADING_MUSIC_FILE);
    if (!file.Exists()) { cd_enter_root(); return; }

    uint32_t size = (uint32_t) file.Size.Bytes;
    uint32_t play = size < 0x900 ? 0x900 : size;   // slPCMOn's minimum
    int8_t* buf = (int8_t *) SRL::Memory::LowWorkRam::Malloc(play);
    if (!buf) { cd_enter_root(); return; }

    int32_t got = file.LoadBytes(0, (int32_t) size, (uint8_t *) buf);
    cd_enter_root();
    if (got != (int32_t) size) {
        SRL::Memory::Free(buf);
        return;
    }
    for (uint32_t i = size; i < play; i++) buf[i] = 0;

    g_loading_music_buf  = buf;
    g_loading_music_size = play;
    g_loading_music_pcm.set(buf, play, LOADING_MUSIC_RATE);
}

#define LOADING_FADE_STEPS 256

extern "C" void loading_music_fade_in(int frames) {
    if (!g_loading_music_buf || frames <= 0) return;

    uint32_t n = (uint32_t) frames * (LOADING_MUSIC_RATE / 60);
    if (n > g_loading_music_size) n = g_loading_music_size;

    uint32_t seg = n / LOADING_FADE_STEPS;
    if (seg == 0) return;   // ramp too short to segment; leave it at full

    for (uint32_t s = 0; s < LOADING_FADE_STEPS; s++) {
        int32_t  gain = (int32_t) s;
        uint32_t end  = (s + 1) * seg;
        for (uint32_t i = s * seg; i < end; i++)
            g_loading_music_buf[i] = (int8_t) (((int32_t) g_loading_music_buf[i] * gain) >> 8);
    }
}

extern "C" void loading_music_play(void) {
    if (!g_loading_music_buf || g_loading_music_channel >= 0) return;
    g_loading_music_channel = g_loading_music_pcm.Play(LOADING_MUSIC_LEVEL_MAX);
}

#define LOADING_MASTER_MAX   15
#define LOADING_MASTER_NUDGE 7

static void loading_master_restore(void) {
    SND_SetTlVl((SndTlVl) LOADING_MASTER_NUDGE);
    SND_SetTlVl((SndTlVl) LOADING_MASTER_MAX);
}

extern "C" void loading_music_set_level(int level) {
    if (level < 0) level = 0;
    if (level > LOADING_MUSIC_LEVEL_MAX) level = LOADING_MUSIC_LEVEL_MAX;
    SND_SetTlVl((SndTlVl) ((LOADING_MASTER_MAX * level) / LOADING_MUSIC_LEVEL_MAX));
}

extern "C" void loading_music_stop(void) {
    loading_master_restore();
    if (g_loading_music_channel >= 0) {
        SRL::Sound::Pcm::StopSound((uint8_t) g_loading_music_channel);
        g_loading_music_channel = -1;
    }
    if (g_loading_music_buf) {
        SRL::Memory::Free(g_loading_music_buf);
        g_loading_music_buf = nullptr;
    }
}
```

- [ ] **Step 6: Syntax-check `loading_music.cxx`**

Run (from `saturn/`):
```bash
sh syntax-check.sh src/sound/loading_music.cxx
```
Expected: `syntax-check: DEBUG build` then `syntax-check: release build`, no errors printed to stderr, exit code 0.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/sound/msc_dir.h saturn/src/sound/msc_dir.cxx \
        saturn/src/sound/boot_music.cxx \
        saturn/src/sound/loading_music.h saturn/src/sound/loading_music.cxx
git commit -m "Extract shared cd_enter_msc; add loading_music PCM module"
```

---

### Task 4: `game_catalog_title_for` lookup

**Files:**
- Modify: `saturn/src/menu/game_catalog.h`
- Modify: `saturn/src/menu/game_catalog.cxx:230-231` (insert between `preload_game_catalog` and `game_select`)

**Interfaces:**
- Consumes: the module's existing private `names[]`/`labels[]`/`g_catalog_count` (already populated by `preload_game_catalog`, called by `game_select` before it can return a filename).
- Produces: `const char* game_catalog_title_for(const char *filename);` — consumed by Task 6's `main.cxx` change.

- [ ] **Step 1: Add the declaration to `game_catalog.h`**

In `saturn/src/menu/game_catalog.h`, after `preload_game_catalog`'s declaration and before `game_select`'s:

```c
/*----------------------
 | game_catalog_title_for
 | Description: Looks up the display title (as shown on the game-picker
 |   screen) for a story filename previously returned by game_select. Used
 |   by the post-selection loading screen so its boot text can show the
 |   game's real title rather than its CD filename.
 | Author: suinevere
 | Dependencies: none
 | Globals: names, labels, g_catalog_count
 | Params: filename -- a name previously returned by game_select
 | Returns: the matching catalogue label, or `filename` itself as a
 |   defensive fallback if no match is found (should not happen in
 |   practice -- game_select only ever returns a name it just read from
 |   this same table)
 ----------------------*/
const char* game_catalog_title_for(const char *filename);
```

- [ ] **Step 2: Implement it in `game_catalog.cxx`**

Insert into `saturn/src/menu/game_catalog.cxx` at line 231 (the blank line between `preload_game_catalog`'s closing `}` at line 230 and the `game_select` doc comment at line 232):

```c++

/*----------------------
 | game_catalog_title_for
 | Description: See game_catalog.h. A manual character-by-character compare
 |   rather than strcmp, to avoid adding a <string.h> dependency to this
 |   file for one lookup.
 | Author: suinevere
 | Dependencies: none
 | Globals: names, labels, g_catalog_count
 ----------------------*/
const char* game_catalog_title_for(const char *filename) {
    for (int i = 0; i < g_catalog_count; i++) {
        int j = 0;
        while (filename[j] && names[i][j] == filename[j]) j++;
        if (filename[j] == '\0' && names[i][j] == '\0') return labels[i];
    }
    return filename;
}
```

- [ ] **Step 3: Syntax-check**

Run (from `saturn/`):
```bash
sh syntax-check.sh src/menu/game_catalog.cxx
```
Expected: both configs clean, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/menu/game_catalog.h saturn/src/menu/game_catalog.cxx
git commit -m "Add game_catalog_title_for: filename -> display title lookup"
```

---

### Task 5: `loading_screen` — orchestration

**Files:**
- Create: `saturn/src/video/loading_screen.h`
- Create: `saturn/src/video/loading_screen.cxx`

**Interfaces:**
- Consumes:
  - `loading_text_build`, `LOADING_TEXT_COLS`, `LOADING_TEXT_LINES` (Task 2)
  - `loading_music_load/_fade_in/_play/_set_level/_stop`, `LOADING_MUSIC_LEVEL_MAX` (Task 3)
  - `menu_clear()` (`menu.h`, existing)
  - `text_set_color(unsigned short)` (`options.h`, existing)
  - `display_bg_rgb(int)`, `display_text_rgb(int)`, `DISP_RGB555(r,g,b)` (`display.h`, existing)
  - `g_display` (`app_state.h`, existing) — its `.bg`/`.text` fields
  - `g_pad` (`input.h`, existing), `Button` (from `input.h`'s `using Button = SRL::Input::Digital::Button;`)
  - `saturn_keyboard_poll()`, `SATURN_KEY_NONE` (`saturn_keyboard.h`, existing)
  - `music_pause()` (`music.h`, existing)
- Produces: `void loading_screen(const char *title);` — consumed by Task 6's `main.cxx` change.

- [ ] **Step 1: Write the header**

Create `saturn/src/video/loading_screen.h`:

```c
/*----------------------
 | loading_screen.h
 | Description: The post-selection loading screen: shown once the player
 |   has picked a game and before the real CD read of the story file
 |   begins. Pauses the title-menu CD-DA track, fades in a fixed
 |   black-backdrop/white-text retro-OS boot screen together with
 |   LOADCD.PCM, types out the boot sequence (loading_text.h) with the
 |   game's title substituted in, then fades both back out and restores the
 |   player's normal display colors before returning. Skippable at any
 |   point with any button or key (same check menu_wait() uses): the
 |   remaining text fills in at once and the fade-out still runs in full.
 | Author: suinevere
 | Dependencies: loading_text.h, loading_music.h, menu.h, options.h,
 |   app_state.h, input.h, saturn_keyboard.h, music.h, display.h, SRL
 ----------------------*/
#ifndef LOADING_SCREEN_H
#define LOADING_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | loading_screen
 | Description: See loading_screen.h header comment.
 | Author: suinevere
 | Dependencies: loading_text.h, loading_music.h, menu.h, options.h,
 |   app_state.h, input.h, saturn_keyboard.h, music.h, SRL
 | Globals: g_display, g_pad
 | Params: title -- the game's display title (see game_catalog_title_for);
 |   must not be NULL
 | Returns: N/A
 ----------------------*/
void loading_screen(const char *title);

#ifdef __cplusplus
}
#endif
#endif /* LOADING_SCREEN_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/video/loading_screen.cxx`:

```c++
/*----------------------
 | loading_screen.cxx
 | Description: See loading_screen.h.
 | Author: suinevere
 | Dependencies: loading_screen.h, loading_text.h, loading_music.h, menu.h,
 |   options.h, app_state.h, input.h, saturn_keyboard.h, SRL
 ----------------------*/
#include "loading_screen.h"
#include "loading_text.h"
#include "loading_music.h"
#include "menu.h"
#include "options.h"
#include "app_state.h"
#include "input.h"
#include "saturn_keyboard.h"
#include <srl.hpp>

extern "C" {
#include "display.h"
#include "music.h"
}

using namespace SRL::Types;

/*----------------------
 | LOADING_FADE_FRAMES / LOADING_TEXT_TOP_ROW / TYPE_* constants
 | Description: LOADING_FADE_FRAMES (45 = 0.75s at 60fps) sits between
 |   QUICK_FADE_FRAMES (15, snappy menu transitions) and SPLASH_FADE_FRAMES
 |   (90, the prominent boot-splash logo fade) -- noticeable but not a long
 |   wait, given the typing itself already takes several seconds.
 |   TYPE_FRAMES_PER_CHAR (2) types at ~30 chars/sec. TYPE_PAUSE_BLANK and
 |   TYPE_PAUSE_READY give blank rows and the two READY! rows a beat of
 |   boot-sequence rhythm instead of typing through them at the same pace
 |   as real text.
 | Author: suinevere
 ----------------------*/
#define LOADING_FADE_FRAMES   45
#define LOADING_TEXT_TOP_ROW  6
#define TYPE_FRAMES_PER_CHAR  2
#define TYPE_PAUSE_BLANK      12
#define TYPE_PAUSE_READY      24

/*----------------------
 | PAUSE_AFTER_READY
 | Description: Rows 4 and 9 are the block's two "READY!" lines (see
 |   loading_text.c); indexed by row so loading_screen_type doesn't need a
 |   string compare against the drawn text to find them.
 | Author: suinevere
 ----------------------*/
static const bool PAUSE_AFTER_READY[LOADING_TEXT_LINES] = {
    false, false, false, false, true, false, false, false, false, true, false
};

/*----------------------
 | loading_screen_skip_pressed
 | Description: Same check menu_wait() uses for "press any key/button" --
 |   A/B/C/START plus any keyboard key.
 | Author: suinevere
 | Globals: g_pad
 ----------------------*/
static bool loading_screen_skip_pressed(void) {
    if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
        g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return true;
    return saturn_keyboard_poll().kind != SATURN_KEY_NONE;
}

/*----------------------
 | loading_screen_wait
 | Description: Synchronizes for `frames` fields, polling the skip check
 |   each frame.
 | Author: suinevere
 | Returns: true if the player skipped during the wait
 ----------------------*/
static bool loading_screen_wait(int frames) {
    for (int f = 0; f < frames; f++) {
        SRL::Core::Synchronize();
        if (loading_screen_skip_pressed()) return true;
    }
    return false;
}

static void loading_screen_set_offset(int v) {
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetA(off);
}

/*----------------------
 | loading_screen_fade_in
 | Description: Bakes and starts LOADCD.PCM's fade-in, then ramps the
 |   screen from hidden to normal over the same LOADING_FADE_FRAMES span so
 |   the two rise together (mirrors splash.cxx's logo/jingle pairing).
 | Author: suinevere
 ----------------------*/
static void loading_screen_fade_in(void) {
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
    loading_screen_set_offset(-255);

    loading_music_fade_in(LOADING_FADE_FRAMES);
    loading_music_play();

    for (int i = 0; i <= LOADING_FADE_FRAMES; i++) {
        loading_screen_set_offset(-(255 * i) / LOADING_FADE_FRAMES);
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | loading_screen_fade_out
 | Description: Ramps the screen and LOADCD.PCM's master-volume level back
 |   down together over LOADING_FADE_FRAMES, then releases the color
 |   offset channels.
 | Author: suinevere
 ----------------------*/
static void loading_screen_fade_out(void) {
    for (int i = 0; i <= LOADING_FADE_FRAMES; i++) {
        loading_screen_set_offset(-(255 * i) / LOADING_FADE_FRAMES);
        loading_music_set_level(LOADING_MUSIC_LEVEL_MAX - (LOADING_MUSIC_LEVEL_MAX * i) / LOADING_FADE_FRAMES);
        SRL::Core::Synchronize();
    }
    SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
    SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
}

/*----------------------
 | loading_screen_type
 | Description: Types each line of `lines` onto the console at
 |   ~1 char/TYPE_FRAMES_PER_CHAR frames, pausing on blank lines and after
 |   the two READY! lines. On skip, immediately draws every remaining line
 |   in full and stops.
 | Author: suinevere
 ----------------------*/
static void loading_screen_type(char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]) {
    bool skipped = false;
    char buf[LOADING_TEXT_COLS + 1];

    for (int row = 0; row < LOADING_TEXT_LINES && !skipped; row++) {
        const char *line = lines[row];
        int len = 0;
        while (line[len]) len++;

        if (len == 0) { skipped = loading_screen_wait(TYPE_PAUSE_BLANK); continue; }

        for (int c = 1; c <= len && !skipped; c++) {
            for (int j = 0; j < c; j++) buf[j] = line[j];
            buf[c] = '\0';
            SRL::Debug::Print(0, LOADING_TEXT_TOP_ROW + row, "%s", buf);
            skipped = loading_screen_wait(TYPE_FRAMES_PER_CHAR);
        }
        if (!skipped && PAUSE_AFTER_READY[row]) skipped = loading_screen_wait(TYPE_PAUSE_READY);
    }

    if (skipped) {
        for (int row = 0; row < LOADING_TEXT_LINES; row++)
            SRL::Debug::Print(0, LOADING_TEXT_TOP_ROW + row, "%s", lines[row]);
    }
}

extern "C" void loading_screen(const char *title) {
    music_pause();

    menu_clear();
    SRL::VDP2::SetBackColor(HighColor::Colors::Black);
    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));

    loading_music_load();

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title, lines);

    loading_screen_fade_in();
    loading_screen_type(lines);
    loading_screen_fade_out();

    loading_music_stop();

    text_set_color(display_text_rgb(g_display.text));
    SRL::VDP2::SetBackColor(HighColor(display_bg_rgb(g_display.bg)));
    menu_clear();
}
```

- [ ] **Step 3: Syntax-check**

Run (from `saturn/`):
```bash
sh syntax-check.sh src/video/loading_screen.cxx
```
Expected: both configs clean, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/video/loading_screen.h saturn/src/video/loading_screen.cxx
git commit -m "Add loading_screen: typewriter boot screen with fading audio"
```

---

### Task 6: Wire `loading_screen` into `main.cxx`

**Files:**
- Modify: `saturn/src/main.cxx:41` (add include)
- Modify: `saturn/src/main.cxx:213-217` (call site)

**Interfaces:**
- Consumes: `loading_screen(const char*)` (Task 5), `game_catalog_title_for(const char*)` (Task 4)

- [ ] **Step 1: Add the include**

In `saturn/src/main.cxx`, after the existing `#include "game_catalog.h"` at line 41:

```c++
#include "game_catalog.h"
#include "loading_screen.h"
#include "online.h"
```

(i.e. insert the new line between the existing `game_catalog.h` and `online.h` includes.)

- [ ] **Step 2: Call it after the game-pick loop, before the CD-read loop**

In `saturn/src/main.cxx`, the loop that picks `game_file` ends and falls through to:

```c++
    g_story_filename = game_file;
    g_menu_page_fade = 0;   // leaving the menu phase; in-game menus stay instant

    uint8_t *story = nullptr;
```

Insert the call between those two lines:

```c++
    g_story_filename = game_file;
    g_menu_page_fade = 0;   // leaving the menu phase; in-game menus stay instant

    loading_screen(game_catalog_title_for(game_file));

    uint8_t *story = nullptr;
```

- [ ] **Step 3: Syntax-check the whole entry point**

Run (from `saturn/`):
```bash
sh syntax-check.sh src/main.cxx
```
Expected: both configs clean, exit code 0. This is the integration check — it type-checks `main.cxx`'s call site against `loading_screen.h`'s and `game_catalog.h`'s real declarations.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Show the loading screen after picking a game, before the CD read"
```

---

## Final verification (not a task — a checklist for whoever runs the plan)

Steps 1-6 above cover everything this assistant can verify without hardware. Once all tasks are committed:

1. Run `saturn/compile.bat debug` (or `release`) — the user runs this, not the agent. [[do-not-run-compile]]
2. Boot in Mednafen (`saturn/run_with_mednafen.bat`) or on hardware, pick a game from the menu, and confirm:
   - The screen fades in from black with the boot text typing out, `LOADCD.PCM` fading in underneath.
   - The title shown matches the game just picked.
   - No line's text runs past the right edge of the screen or wraps.
   - Any button/key instantly fills in the remaining text; the fade-out and audio still finish normally.
   - The screen fades out, the game's own title-screen colors (Display Options preset) are correct on the first rendered frame — not stuck white-on-black.
   - The title-menu CD-DA track does not audibly glitch or double up when a game is picked.
