# Design: `zaturn.netbin` — minimal online-only multizork client

**Date:** 2026-07-25
**Status:** Approved, pending implementation plan
**Supersedes:** `2026-07-21-netbin-build-design.md`

## Goal

Produce a second build target that emits a single self-contained Saturn
executable loadable by the PlanetWeb 4.0 browser's `.netbin` loader: a **pure
multizork telnet client**, as small as it can be made.

It boots straight to the dialer page, dials the NetLink modem, runs the
terminal until the link drops or the player quits, and returns to the dialer.
There is no Z-machine, no story file, no CD access, no sound, and no title
screen.

**Target size: ~122 KB.**

## Why this replaces the 2026-07-21 design

That spec described a "full port, CD assets stripped" build carrying the local
interpreter, the embedded story, the menu flow, sound and CD-DA. Two things
invalidated it:

1. **Its budget no longer closes.** It was written against a ~313 KB image.
   `saturn/cd/data/0.bin` is now **342,320 bytes**, and the tree has since
   gained the splash, loading screen, boot/loading music, `msc_dir` and
   `save_ui`. Adding `ZORK1.Z3` (84,876 B) and `SDDRVS.TSK` (26,610 B) lands
   at ~454 KB — 54 KB past the loader's 400 KB ceiling. "Keep everything" is
   not a choice that is still available.
2. **The netbin is the online build.** The game runs on the multizork server,
   so the local interpreter, the story file and the whole save/restore surface
   are dead weight. Removing them takes out the three largest objects in the
   image at once.

Prior implementation lives on branch `netbin-build` (21 commits, based on
July-21 `main`, before `src/` was split into subdirectories). Only its build
scaffolding is reused; see Build configuration.

## Size accounting

Measured with `sh2eb-elf-size` over the current objects (loadable = text+data;
`.bss` is `NOLOAD` and costs nothing in the image).

### Kept — 18 objects

| Module | KB |
| --- | --- |
| SRL / SGL / libc / preloader | ~33 |
| `net/online` | 13.7 |
| `net/netbin_pages` *(new)* | ~15 |
| `video/console_view` | 13.0 |
| `menu/menu` + `menu/menu_layout` | 13.4 |
| `input/input` | 7.1 |
| `input/typeahead` | 5.6 |
| `menu/options` | 4.5 |
| `main_netbin` *(new)* | ~4 |
| `input/saturn_keyboard` | 3.5 |
| `net/net_connect` + `net/term` + `net/transport_uart` | 3.0 |
| `video/display` | 3.0 |
| `system/saturn_backup` | 2.3 |
| `input/keyboard` + `video/console` + `engine/app_state` | 1.7 |
| **Total** | **~122** |

### Dropped — 23 objects

(16 of the 18 kept entries above are existing objects; 16 + 23 = the 39 objects
the CD build links today, plus the two new files.)

`engine/mojozork_saturn` (18.2), `engine/saturn_glue` (11.0),
`engine/saturn_compat` (1.8), `engine/saturn_filestub` (0.03),
`input/typeahead_extract` (5.1), `input/typeahead_solution` (64.8),
`video/title` (15.9), `menu/menu_pages` (51.7), `menu/save_ui` (12.4),
`menu/game_catalog` (5.9), `menu/game_titles` (4.1), `video/splash` (6.2),
`video/loading_screen` (7.7), `video/loading_text` (0.7),
`engine/soft_reset` (8.0), and all eight sound modules — `sound/sound` (7.8),
`sound/loading_music` (4.3), `sound/boot_music` (3.4), `sound/music_cdda`
(3.1), `sound/music` (2.8), `sound/music_data` (2.4), `sound/msc_dir` (1.7),
`sound/sound_blorb` (1.1).

`SRL_USE_SGL_SOUND_DRIVER` is set to `0` for this target so SRL's own sound
code does not link either, and no `SDDRVS.TSK` / `BOOTSND.MAP` is embedded.

## Target contract

- **Entry point / load base `0x06010000`.** Stock SRL links at `0x06004000`.
  Non-negotiable loader requirement.
- **Single image well under 400 KB.** See Risks — the loader's real ceiling is
  known to be lower than 400 KB but its value is not established here.
- **Startup re-initializes video itself.** The browser's leftover VDP1/VDP2
  state cannot be assumed.
- **No CD access at any point.** At boot the drive holds PlanetWeb's own disc.
  Every `SRL::Cd::File` read, `GFS_Reset` and `cd_capture_root` is gone, not
  merely guarded at the call site.

## Architecture

Two new orchestrators replace the two fat ones; the Makefile selects the object
set on `NETBIN=1`. `main.cxx` (338 lines) and `saturn_glue.cxx` (521 lines) are
not built into this target at all.

This is deliberately *not* the 07-21 design's approach of `#ifdef NETBIN`
throughout `main.cxx`. At this strip level that turns the shared orchestrator
into swiss cheese and puts the working CD build at risk on every edit. Two
small parallel files leave the CD path byte-identical.

### New files

**`saturn/src/main_netbin.cxx`** (~120 lines)

Boot sequence, in order:

1. `SRL::Core::Initialize(HighColor::Colors::Black)`
2. Explicit VDP re-initialization mirroring SRL's own `slInitSystem` call
3. `console_init()`
4. `options_load()`
5. `setjmp` reboot point
6. `netbin_dial_page()` → `online_mode()` → loop back to (6)

It also carries a slim reset implementation exposing the same four symbols
`soft_reset.h` declares and `online.cxx` calls — `is_reboot_command`,
`soft_reset_chord_held`, `confirm_return_to_title`, `check_soft_reset` — but
`longjmp`ing to the dialer instead of a title screen. `engine/soft_reset.cxx`
itself is not linked: it is 8.0 KB and includes `sound.h` and
`net/net_connect.h`, and its "return to title" semantics do not exist here.

**`saturn/src/net/netbin_pages.cxx`** (~450 lines)

Lifted from `menu_pages.cxx` so the netbin does not link 51.7 KB of pages for
three screens:

| Source | Lines | Role |
| --- | --- | --- |
| `network_page` | 125–258 | the dialer; gains one **Controls** row |
| `controls_page` | 258–378 | pad controls |
| `keyboard_controls_page` | 378–490 | keyboard controls |
| `controls_dispatch` | 1036–1039 | picks one by `g_kbd_visible` |
| `menu_digit_row` | 85–125 | shared helper |
| `page_fade_out` / `page_fade_in` | 64–65 | shared helpers |

Bodies are moved verbatim apart from the added Controls row. `network_page` is
exposed as `netbin_dial_page()`.

**`saturn/sgl-netbin.linker`**

A copy of `SaturnRingLib/modules/sgl/sgl.linker` differing in exactly one
literal: `PRELOADER 0x06004000` becomes `PRELOADER 0x06010000`. Every section
(`.text`/`.data`/`.rodata`/`.bss`/heap) shifts up as a block. The only cost is
48 KB of heap headroom, since the heap ends at the fixed
`work_area_start = ALIGN(0x060FC000 …)`; irrelevant at this size. The CD build
keeps using the unmodified `sgl.linker`. Cherry-picked from `0690a7f`.

### Existing files touched — three files, five guards

Each guarded region is a *link* dependency on a dropped object, verified by
call-site inspection rather than by the `#include` list. Several headers in the
keep-set (`music.h` in `options.cxx`, `sound.h`/`music.h` in `menu_pages.cxx`)
contribute only constants and cost nothing.

**`saturn/src/net/online.cxx`** — three `#ifdef NETBIN` guards:

- The body of `ensure_online_typeahead()`, which reads `ZORK1.Z3` off the CD
  and calls into `game_catalog` (`scan_z3_folder`), `typeahead_extract`
  (`build_typeahead_from_story`) and `typeahead_solution`
  (`apply_solution_overlay`). It falls through to the empty trie the function
  *already* builds for `DIFF_HARD`, so this is an existing supported path, not
  new behavior.
- `music_cdda_is_playing()` and `music_refresh()` at the top of `online_mode()`.

**`saturn/src/menu/menu.cxx`** — one guard:

- `sound_service()` and `music_tick()` in `menu_sync()` (`menu.cxx:50-51`),
  leaving the bare `SRL::Core::Synchronize()`. These are the only calls out of
  `menu.cxx` into a dropped object.

**`saturn/src/menu/options.cxx`** — one guard:

- The image branch of `display_apply()` (`options.cxx:82,90,95`), which calls
  `title_bg_show()` / `title_bg_hide()`; guarding it keeps `video/title.cxx`
  out.

`options.cxx` needs **no** guard around the settings blob. It makes zero calls
into `music`/`sound` — it uses only `music.h` constants, and
`g_music_level`/`g_pcm_level`/`g_mix_mode`/`g_sel_track` are defined in
`engine/app_state.cxx`, which the netbin links. The netbin therefore reads and
writes the identical `MOJOOPTS` layout, and settings stay compatible between
the two builds with no work.

### One additive function, no guard

**`saturn/src/net/net_connect.{h,c}`** gains `void net_connect_reset(void)`:
detect the UART, `modem_escape_to_command()` (the `+++` guard-timed escape),
then `modem_hangup()`. Both primitives already exist in `net/modem.h`
(lines 124 and 231). `main_netbin` calls it once at boot, before the first
dial, to drop the data session PlanetWeb leaves live on the line. The CD build
never calls it, so this is purely additive.

## Flow and feature surface

### Kept

- **Dialer page.** Entry screen. Edits and validates the phone number
  (`valid_dialnum`, digits only, `DIALNUM_MAX`), seeded from `g_dialnum`
  (default `"199403"`). Ok dials; Controls opens the controls pages.
- **Controls pages.** Pad and keyboard variants, dispatched by `g_kbd_visible`,
  including face-button and chord reassignment.
- **The multizork terminal.** `online_mode()` unchanged: auto-redial,
  `term_service` RX pump, on-screen and hardware keyboard, scrollback, Esc or a
  ~0.75 s L+R hold to disconnect.
- **Settings persistence.** All three kept pages already call `options_save()`,
  so the dial number and the control bindings survive across boots in backup
  RAM via `saturn_backup`.
- **Reboot.** The `reboot` command and the soft-reset chord return to the
  dialer.

### Dropped

- Local single-player Z-machine, the embedded story, and every CD read.
- Save / Restore — the game state lives on the multizork server.
- Autocomplete suggestions and the suggestion row. `typeahead_edit` in
  `console_view.cxx` is still the line editor and is still linked, but it runs
  against an empty trie. Restoring suggestions would mean embedding `ZORK1.Z3`
  purely for its dictionary: ~69 KB (60.9 DEFLATE + 5.1 `typeahead_extract` +
  3.0 `puff`), nearly doubling the image.
- All sound: PCM effects, CD-DA, boot and loading music.
- Title screen, splash, loading screen, background artwork, Display options,
  Sound options, Gameplay options, the game catalog and game-select menu.

## Build configuration

A new build path selected by a `NETBIN=1` make flag. It does **not** replace
the CD build; both must continue to work, and every change is additive or
`#ifdef NETBIN`-guarded.

1. **Object list.** `NETBIN=1` selects the 18-object set above, substituting
   `main_netbin.cxx` for `main.cxx` and adding `netbin_pages.cxx`.
2. **Linker.** `sgl-netbin.linker` selected via a command-line `LDFILE`
   override, which beats the plain assignment in the SDK's included makefile.
   No file under `SaturnRingLib/` is modified — it is a pinned submodule.
3. **Sound off.** `SRL_USE_SGL_SOUND_DRIVER = 0` for this target only.
4. **Packaging.** `post.makefile` converts the linked ELF to a flat raw image
   with `objcopy -O binary`, emits `BuildDrop/zaturn.netbin`, and **hard-fails**
   a size gate. We assume the loader consumes a bare raw image whose first byte
   corresponds to `0x06010000`, with no container header. If hardware testing
   shows a wrapper is required, only this packaging step changes.
5. **Entry point.** `saturn/compile-netbin.bat`, mirroring `compile.bat`.

Items 2, 4 and 5 are cherry-picked from `netbin-build` commit `0690a7f`. The
blob generator (`0eeee04`), DEFLATE packing (`df85e24`) and pre-build payload
regeneration (`371f1cc`) are **not** carried over — there is no payload to
embed. `tools/gen_blob.py`, `src/puff.{c,h}` and `src/netbin_blobs.{c,h}` do
not exist in this design.

## Risks

- **The modem is mid-session at hand-over.** PlanetWeb downloaded this
  executable *over the NetLink modem*, so at entry the line is most likely
  off-hook in a live data session with the ISP — not in the cold, unknown state
  the 07-21 design assumed. In data mode the modem treats `AT` as payload, so
  `modem_probe()` would fail and `net_connect_open()` would report
  `NET_NO_MODEM` on a perfectly good modem. `net_connect_reset()` above is the
  intended fix and is cheap insurance even if the line turns out to be idle,
  but whether the escape sequence and guard timing suffice against a live PPP
  session is the top hardware unknown and the most likely thing to need
  iteration.
- **Video re-init correctness.** Depends on the actual VDP state the browser
  hands over; likely needs iteration on hardware.
- **The loader's real download ceiling is not established.** Branch commit
  `a00537d` records that an earlier netbin "exceeded the PlanetWeb loader's real
  download ceiling" while under 400 KB, but does not state the value. At ~122 KB
  this is very likely moot; the size gate should use the real number once known.
- **No fallback without a modem.** If no NetLink is present, `net_connect_open`
  returns `NET_NO_MODEM`, the dialer reports it and the player is returned to
  the dialer page. There is nothing else this build can do.

## Non-goals

- Any embedded story file.
- Local single-player play, save/restore, or a local/online fallback.
- Sound of any kind, including a companion audio disc.
- Downloading story files over NetLink.
- Changing or refactoring the CD build's behavior.
