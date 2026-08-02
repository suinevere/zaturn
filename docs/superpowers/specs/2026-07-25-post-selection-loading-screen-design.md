# Post-selection loading screen: retro OS boot text + LOADCD.PCM

## Problem

Today, once the player picks a game (`game_select()` in `main.cxx`), the
screen is faded to black and stays black through the entire CD read of the
story file (`main.cxx:213-234`) — the `"loading %s..."` `Debug::Print` at
that point draws into the held-black screen, unseen, per the existing
comment at `main.cxx:254-258`. There is no feedback that anything is
happening between picking a game and the game itself appearing.

The developer wants that dead time replaced with a themed "period computer
booting a program" screen: a fixed block of text that types itself out line
by line (mimicking a Commodore-style `LOAD`/`RUN` boot sequence, with the
chosen game's title substituted in), the whole screen fading in and out
around it, and a new looping-free PCM cue (`LOADCD.PCM`, converted at build
time from `tools/assets/music/loadCD.ogg`) fading in and out alongside it.

## Decisions

1. **Fixed boot text, title substituted in.** The exact block (blank lines
   included):
   ```
   **** SEGA SATURN 32-BIT OS V1.00 ****

   2048K RAM SYSTEM  2093056 SYSTEM BYTES FREE

   READY!
   LOAD <TITLE>,8,1

   SEARCHING FOR <TITLE>
   LOADING FROM CD-ROM BLOCK...
   READY!
   RUN
   ```
   `<TITLE>` is the catalog's display title (e.g. `ZORK I`), not the CD
   filename — it's what the player just saw on the game-picker screen, and
   reads naturally as a program name. Both occurrences are truncated (with
   a plain byte cut, no ellipsis) if the fixed prefix/suffix on that line
   would push the 40-column console width, since catalog titles run up to
   39 characters (`labels[MAX_GAMES][40]` in `game_catalog.cxx`) and
   `"SEARCHING FOR "` (14 chars) + a full-length title can exceed 40.
2. **Typed at ~1 char / 2 frames (~30 chars/sec), skippable.** Roughly
   5-6 seconds to type the full block at that pace. Any button or key
   (same check `menu_wait()` already uses: `A`/`B`/`C`/`START` +
   `saturn_keyboard_poll()`) instantly fills in the remaining text and
   moves on to the fade-out — it does not jump-cut past the fade-out itself.
   Blank lines and the two `READY!` lines get a short fixed pause (no
   characters to type) for boot-sequence rhythm.
3. **Screen forced to black backdrop / white text, independent of the
   player's Display Options theme.** This is fixed retro-OS flavor text,
   not the interpreter's themed output (`g_display`, which governs the
   in-game console's colors elsewhere). `text_set_color`/`SetBackColor`
   are overridden for the duration and explicitly restored to
   `display_text_rgb(g_display.text)` / `display_bg_rgb(g_display.bg)`
   before returning, so the interpreter's own text isn't left white-on-black.
4. **Screen fade in/out via a fixed-black ramp, not `menu_fade_in`/`menu_fade_out`.**
   Those two existing functions re-write the backdrop plane toward
   `display_bg_rgb(g_display.bg)` every ramp step (the backdrop plane can't
   take a hardware color offset and must be redrawn each frame — see
   `menu_intro_scale`'s doc comment). Since this screen's backdrop is
   always pure black, it needs no per-frame backdrop rewrite at all (black
   scaled by any factor is still black): set it once, then only ramp
   `SRL::VDP2::SetColorOffsetA` on NBG0+NBG3 between hidden and normal.
   Implemented as a small local helper pair in the new module rather than
   reusing the theme-coupled functions.
5. **`LOADCD.PCM` fades exactly like the boot splash's `WISDOM.PCM` does,
   for the same hardware reason.** Per `boot_music.h`'s documented finding:
   `SRL::Sound::Pcm::SetVolumePan` does not lower a *running* PCM channel,
   it kills it — silently, with no working example anywhere in SRL. So
   fade-in must be baked into the sample bytes before `Play()`, and
   fade-out must go through the SGL driver's *master* volume
   (`SND_SetTlVl`), never the channel's own level. New module
   `loading_music.{h,cxx}` mirrors `boot_music.{h,cxx}`'s
   load/fade-in-bake/play/set-level/stop shape, including the documented
   double-write master-volume restore (`BOOT_MASTER_MAX`/`BOOT_MASTER_NUDGE`
   equivalents) — that quirk was pinned down empirically and applies to the
   driver as a whole, not to any one sample.
6. **The title-menu CD-DA track is paused, not ducked, before `LOADCD.PCM`
   plays.** At the point `loading_screen()` runs, `music_start_menu()`'s
   CD-DA is normally still audible. Master-volume fade-out would duck it
   too, audibly, for no reason (it's about to be silenced anyway by the
   real story-file CD read a few seconds later — see
   [[cd-reads-stop-cdda]]). Calling the already-exported `music_pause()`
   ("hold the drive where it is") at the top of `loading_screen()` hands
   off audio cleanly instead. Nothing needs to resume it: `mojo_boot`'s own
   `music_reset()`/`music_start()` sequence takes over audio moments later
   regardless of how this screen exits.
7. **`cd_enter_msc` is extracted to a new shared module, `sound/msc_dir.{h,cxx}`.**
   `boot_music.cxx` currently has a private `static` copy; `loading_music.cxx`
   needs the identical `/MSC` directory-table dance. Per explicit direction:
   extract rather than duplicate. (Note: `cd_enter_tga`, the closest analog
   in `title.cxx`, is itself private/static — `cd_enter_root` is the only
   directory helper this codebase shared before now — so this is a new
   shared module, not an existing export being reused. It lives under
   `sound/` alongside its two PCM-module consumers rather than in `title.h`,
   which is video-domain.)
8. **`LOADCD.PCM` is loaded on demand, not preloaded at boot.** Unlike
   `WISDOM.PCM` (needed the instant the splash starts, before any menu
   exists), this only plays once per game launch and its own load-time CD
   read is harmless here: the title-menu CD-DA is already paused (Decision
   6) by the time it happens, and the read itself is small (a few seconds
   of 22050Hz 8-bit mono audio). Its buffer is freed again before
   `loading_screen()` returns — the typeahead trie (`typeahead_malloc`,
   89-318 KB) is built during `mojo_boot` immediately afterward and needs
   that Low Work RAM headroom (see [[lwram-is-free-space]]).
9. **`convert_boot_music` (in `tools/assets/lib/pvms.sh`/`pvms.ps1`) is
   reused unchanged.** It's already a generic ogg → raw 8-bit signed mono
   PCM converter parameterized on source/output — only named after its
   first caller. `pvms.bat` gets one more invocation, no changes to the
   shared conversion logic.

## Design

### `tools/assets/pvms.bat`

Both the Linux/macOS block and the Windows block get a second conversion
call, alongside the existing `WISDOM.PCM` one:

```
convert_boot_music "music/loadCD.ogg" "../../saturn/cd/data/MSC" "LOADCD.PCM"
```

```
SET "LOADCD_MUSIC_SRC=%~dp0music\loadCD.ogg"
SET "LOADCD_MUSIC_OUT=%~dp0..\..\saturn\cd\data\MSC"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\pvms.ps1" -Sox "%SRL_SOX%" -InFile "%LOADCD_MUSIC_SRC%" -OutDir "%LOADCD_MUSIC_OUT%" -OutName "LOADCD.PCM"
```

Same missing-source/missing-sox behavior as the existing call: a warning,
not a build failure.

### New module: `saturn/src/sound/msc_dir.h` / `msc_dir.cxx`

One entry point, extracted from `boot_music.cxx`'s current private
`static bool cd_enter_msc(void)`:

```c
bool cd_enter_msc(void);   // enters /MSC; false if not found
```

`boot_music.cxx` drops its private copy and calls this shared one instead;
`loading_music.cxx` (below) uses it too. Lives under `sound/` alongside its
two PCM-module consumers rather than in `title.h`, which is video-domain.

### New module: `saturn/src/sound/loading_music.h` / `loading_music.cxx`

Mirrors `boot_music.{h,cxx}` structurally:

```c
void loading_music_load(void);          // MSC/LOADCD.PCM -> Low Work RAM
void loading_music_fade_in(int frames); // bakes a rising ramp into the sample head
void loading_music_play(void);          // opens the channel at full level
void loading_music_set_level(int level);// 0..LOADING_MUSIC_LEVEL_MAX via SND_SetTlVl (master)
void loading_music_stop(void);          // restores master volume, stops channel, frees buffer
```

Same buffer-ownership pattern (`static int8_t* g_loading_music_buf`, a
`LoadingMusicPcm : IPcmFile` subclass), same `slPCMOn` minimum-size pad,
same master-volume restore double-write. Uses the shared `cd_enter_msc`
(`sound/msc_dir.h`, above) rather than its own copy. A no-op throughout if
the file is missing (mirrors `boot_music_load`'s missing-file tolerance —
the pvms.bat conversion is a warning, not a hard failure, so the loading
screen must tolerate a build where `LOADCD.PCM` never got produced).

### New module: `saturn/src/video/loading_screen.h` / `loading_screen.cxx`

One entry point:

```c
void loading_screen(const char *title);
```

Sequence:

1. `music_pause()`.
2. `text_set_color(white)`; `SRL::VDP2::SetBackColor(black)` (once — not
   re-touched again until the restore step).
3. `loading_music_load()`; `loading_music_fade_in(LOADING_FADE_FRAMES)`;
   `loading_music_play()`.
4. Screen fade-in: engage `UseColorOffset(OffsetChannel::OffsetA)` on
   NBG0+NBG3, ramp `SetColorOffsetA` linearly from `(-255,-255,-255)` to
   `(0,0,0)` over `LOADING_FADE_FRAMES`, one `SRL::Core::Synchronize()`
   per step — same shape as `splash.cxx`'s fade, sized so it runs roughly
   concurrent with the audio's baked-in ramp (both span the same frame
   count from the same starting instant).
5. Build the boot-text block into a local buffer with `<TITLE>` substituted
   (truncated per Decision 1), then type it onto the console: for each
   line, `Debug::Print` progressively longer prefixes of the line at
   ~1 char/2 frames; blank/`READY!` lines get a short fixed pause instead.
   Polls the skip check (`WasPressed(A|B|C|START)` or
   `saturn_keyboard_poll()`) once per frame; on skip, immediately draws
   every remaining line in full and stops typing.
6. Screen fade-out: mirror of step 4, `(0,0,0)` back to
   `(-255,-255,-255)`, combined in the same per-frame loop with
   `loading_music_set_level`'s ramp from `LOADING_MUSIC_LEVEL_MAX` to `0`
   (master volume) over the same `LOADING_FADE_FRAMES`, so screen and
   audio dissolve together.
7. `loading_music_stop()`.
8. Disable the color offset on NBG0+NBG3 (`OffsetChannel::NoOffset`),
   restore `text_set_color(display_text_rgb(g_display.text))` and
   `SetBackColor(display_bg_rgb(g_display.bg))`, `menu_clear()`.

`LOADING_FADE_FRAMES = 45` (0.75s) — between `QUICK_FADE_FRAMES` (15,
snappy menu transitions) and `SPLASH_FADE_FRAMES` (90, the prominent logo
fade); noticeable but not a long wait, given the typing itself already
takes several seconds. Total screen time lands around 6-7 seconds
(2 x 0.75s fades + ~5-6s of typing).

### `game_catalog.h` / `game_catalog.cxx` change

Add a lookup so `main.cxx` can turn the chosen filename back into its
display title:

```c
const char* game_catalog_title_for(const char *filename);
```

Linear scan of the existing private `names[]`/`labels[]` arrays (same ones
`game_select()` already populates); returns the matching `labels[i]`, or
`filename` itself as a defensive fallback if somehow not found (shouldn't
happen — `game_select()` only ever returns a name it just read from this
same table).

### `main.cxx` change

After the shared post-loop point (`main.cxx:213-215`, where
`g_story_filename = game_file;` / `g_menu_page_fade = 0;` already run for
both the normal-play and Restore-flow paths) and before the existing
CD-read loop:

```c
loading_screen(game_catalog_title_for(game_file));
```

The CD-read loop, `mojo_boot`, sound/music init, and the `menu_clear();
menu_fade_clear();` hand-off into `mojo_run()` that follow are unchanged —
`loading_screen()` already leaves the screen and audio in the same
"clean, normal-colors, silent" state that code already expects to find
after the previous black-screen hold.

## Testing

No emulator or hardware run is available to this assistant. Verification
is limited to: build succeeds (`compile.bat`, run by the user per
[[build-with-compile-bat]] / [[do-not-run-compile]]), and a manual
read-through confirming: the boot text never exceeds 40 columns for a
maximum-length catalog title, the skip check matches `menu_wait()`'s
existing pattern exactly, `music_pause()` has no dangling
expected-resume anywhere on this path, `loading_music_load`/`_stop`
tolerate a missing `LOADCD.PCM` without crashing, and the restored
text/backdrop colors match `g_display` exactly (a mismatch here would
leak into the interpreter's first on-screen frame). The user should
confirm the actual look, timing, and audio balance on hardware or in an
emulator once built.

## Out of scope

- Any change to `DISP_IMAGE_MAX`, the Display Options menu, or the
  interpreter's own themed text rendering.
- Looping `LOADCD.PCM` or varying it per game/category (one fixed cue).
- A configurable typing speed or a way to disable the screen entirely
  (skip-by-button covers the "seen it before" case).
- Syncing the typewriter's pacing to the real CD read of the story file —
  the CD read runs after this screen, unconditionally, regardless of how
  long typing took or whether it was skipped.
