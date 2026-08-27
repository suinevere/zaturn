# Design: netbin direction rose — all twelve directions, always

**Date:** 2026-08-25
**Status:** Approved, pending implementation plan
**Extends:** `docs/superpowers/specs/2026-07-25-netbin-minimal-design.md`
**Independent of:** `2026-08-25-netbin-typeahead-design.md` — neither needs the
other, and they touch disjoint code apart from the shared source-list gate.

## Goal

Give the netbin's online terminal the bordered keyboard strip with a compass
rose on its left, the same shape the CD build's in-game keyboard uses, with the
rose showing **all twelve directions unconditionally** rather than the current
room's actual exits.

## Why the rose is static here

The CD build's rose draws the room's real exits, read from a live Z-machine:
`room_model_refresh()` takes the player's location out of global 0 of the bound
story (`engine/room_model.c:726-731`). The netbin has no interpreter — the game
runs on the multizork server and arrives as telnet text — so there is no live
object tree to read.

The obvious workaround is to recover the room by matching the server's printed
room name against the story's object names. **Rejected.** Infocom games reuse
room names with different exits, so a name-keyed lookup would silently draw a
confidently wrong rose — worse than no exit information at all, because the
player has no way to tell a wrong rose from a right one. A rose that offers all
twelve directions makes no claim it cannot keep: the player picks one, and the
server answers "You can't go that way" exactly as it would for a typed command.

This also removes the whole reason the rose looked expensive. `command_rose`
takes `exits` as a parameter and never calls `room_model` itself, so a static
all-open array drops `room_model` (4,176 B + 260 B bss) and the resolver
together.

## Size accounting

Measured with `sh2eb-elf-size`; loadable is `text+data`.

| Item | Image | `.bss` |
| --- | ---: | ---: |
| `video/command_rose.o` | 1,616 | 0 |
| `input/game_kb.o` | 223 | 0 |
| `video/rose_draw.o` (`cv_draw_rose_row`, relocated) | ~200 | 0 |
| `cr_dir_word` (12-entry direction table) | ~80 | 0 |
| `video/console_view.o` growth from un-gating (estimate) | ~3,000 | 5 |
| **total** | **~5.1 KB** | **~5 B** |

Netbin goes from ~124 KB to **~129 KB** on its own, or ~220 KB alongside the
typeahead design. Either is far inside the 409,600 B gate (`post.makefile:10`).

The `console_view` figure is the one estimate here: the gated regions are 215
of the file's 831 lines (26%), and the object is 12,842 B when compiled without
`NETBIN`. A clean cross-compile of that translation unit needs the full SGL
define set and was not obtained; measure it during implementation and correct
this line.

**Not linked**, and this is the point of the design: `video/command_view.o`
(10,368 B + **15,404 B bss**), `input/command_panel.o` (1,400 B),
`engine/room_model.o` (4,176 B + 260 B bss).

## What `console_view` actually depends on

`console_view.o`'s undefined symbols reaching into this cluster are exactly
eight, and each is either satisfied cheaply or replaced:

| Symbol | Home | Disposition |
| --- | --- | --- |
| `cr_enter`, `cr_move`, `cr_dir_row` | `command_rose.o` | link it — 1,616 B, no outgoing edges |
| `game_kb_char_at`, `game_kb_move` | `game_kb.o` | link it — 223 B, no outgoing edges |
| `cv_draw_rose_row` | `command_view.o` | **relocate** — see below |
| `room_model_get` | `room_model.o` | **replace** — static all-open exits |
| `room_model_dir_word` | `room_model.o` | **replace** — `cr_dir_word` |

Linking `command_view.o` for one draw function would cost 10 KB of image and
15 KB of `.bss` for a panel this build never shows.

## Architecture

### `cv_draw_rose_row` moves to `rose_draw.cxx`

`command_view.cxx:685-703` is eighteen lines that call `cr_row`, `cr_dir_cell`,
`text_print` and `text_print_hl` — rose geometry plus text output, and nothing
else from `command_view`. It moves to a new translation unit so both builds
share one copy and neither needs the panel.

It must be a **C++** unit, not `command_rose.c`. `text_map.h`'s `extern "C" {`
at `:49` has no `#ifdef __cplusplus` guard and no `.c` file in the tree includes
the header; `text_print` itself is a C++ inline and variadic template outside
that block (`:152`, `:158`). A plain-C caller cannot include it. So the function
lands in `src/video/rose_draw.cxx`, declared in `src/video/rose_draw.h` with
C++ linkage — the same mangled symbol it has today, so neither call site's code
changes, only its includes.

Net effect on the CD build: `command_view.o` shrinks by ~200 B, a new
`rose_draw.o` carries the same ~200 B. No behavior change.

### `cr_dir_word` is added to `command_rose.c`

`room_model_dir_word` (`room_model.c:247-250`) is a pure index-to-string lookup
over a twelve-entry table, documented `Globals: N/A` — no story dependency at
all. Rather than link 4 KB of room model for it, `command_rose.c` gains an
identical `cr_dir_word(int dir)`, and `console_view.cxx:528` calls that.

`room_model.c` is left alone. The twelve short strings are duplicated (~60 B in
the CD build, which links both); folding `room_model_dir_word` into a call to
`cr_dir_word` is a reasonable follow-up cleanup but is deliberately out of scope
here, so this change cannot regress local play.

### `kb_exits` returns an all-open table under NETBIN

`console_view.cxx:454-460` currently returns `room_model_get()->exits`,
flattened to `RM_EXIT_MAYBE` on Hard difficulty. Under `NETBIN` it returns a
`static const unsigned char[RM_DIR_N]` of `RM_EXIT_OPEN`, ignoring difficulty —
there is nothing to give away when every direction is offered regardless.

All twelve read as `RM_EXIT_OPEN` rather than `RM_EXIT_MAYBE`. `MAYBE` in this
UI means "this room might have this exit", which is a claim about a room the
netbin knows nothing about; `OPEN` here means "you may select this", which is
true. `game_kb_travel` (`:523-530`) gates submission on `OPEN || MAYBE`, so all
twelve submit.

`RM_DIR_N` and the `RM_EXIT_*` enum come from `room_model.h`, which is
header-only — `command_rose.c` already includes it for exactly these constants
without creating a link edge.

### The five gated regions un-gate

`console_view.cxx` has five `#ifndef NETBIN` regions, all keyed on `g_in_game`:

| Lines | Contents |
| --- | --- |
| 415-531 | `kb_rose_row`, `kb_grid_row`, `kb_exits`, `game_kb_dpad`, `game_kb_travel` |
| 547-550 | d-pad dispatch to `game_kb_dpad` |
| 558-570 | face-button dispatch — travel from the rose, type from the grid |
| 677-764 | `render_game_keyboard` — the bordered strip and rose rendering |
| 806-808 | render dispatch to `render_game_keyboard` |

All five become unconditional. They are already `g_in_game`-guarded at runtime,
so the CD build is unaffected and the netbin only enters them when it sets that
flag.

### `g_in_game` for the session

`g_in_game` lives in `engine/app_state.cxx:143`, which the netbin already links,
and `main_netbin.cxx` never sets it. It is set true and false around the
`online_mode()` call in `main_netbin.cxx`'s dial loop, not inside `online_mode()`
itself: that function has three exit paths (`online.cxx:303`, `:312`, `:419`) and
is also escaped by the reboot `longjmp`, so bracketing the one call site is both
smaller and safer. Because `main_netbin.cxx` is netbin-only, no `#ifdef` is
needed, and the CD build's title-screen online terminal keeps the flag false and
the four-row grid.

The `longjmp` skips the trailing `g_in_game = false`, so the flag is also cleared
at the post-`setjmp` landing point, beside the existing `g_menu_backing_depth = 0`
reset that exists for exactly the same reason. Without that, the dial page would
render with the strip layout after a reboot.

That flips `console_height()` (`:177-185`) from `avail - (1 + KB_ROWS)` to
`avail - (1 + CV_STRIP_ROWS + 2)`, reserving ten rows for the strip instead of
five. `CV_STRIP_ROWS` is a `#define` in `command_view.h` and costs no link edge,
the same way `options.cxx` already takes constants from `music.h` without
linking the music modules.

The one other unguarded `g_in_game` site is `console_view.cxx:797`
(`image_window_off()`), which is defined in `console_view.cxx:134` and already
compiles into this build. Turning off an image window that was never on is a
no-op, but confirm it on hardware.

The name `g_in_game` becomes slightly inaccurate — in the netbin the session is
the game, but no local interpreter is running. Adding a distinct `g_strip_ui`
flag was considered and rejected as churn across two builds for a rename; the
declaration's comment block should say what the flag means in each build
instead.

### `saturn/Makefile`

Two sources added to the `NETBIN=1` block: `src/video/command_rose.c` and
`src/input/game_kb.c`. Neither is netbin-only — the CD build compiles both
already — so `NETBIN_ONLY_SOURCES` does not change.

## Testing

- **Host**: `tests/test_command_rose.c` exists; extend it with an all-open exit
  table asserting that `cr_enter` succeeds from every row, `cr_move` reaches all
  twelve directions, and `cr_dir_cell` agrees with `cr_row`'s output for each.
- **Host**: a case asserting `cr_dir_word` matches `room_model_dir_word` for all
  twelve indices, so the duplicated table cannot drift.
- **Host**: `tests/test_netbin_sources.py` — `EXPECTED` gains two entries (on
  top of whatever the typeahead design lands).
- **Hardware**: dial, connect, confirm the strip renders with all twelve
  directions, the d-pad crosses left onto the rose and back, and selecting a
  direction submits its word to the server.
- **Regression**: the CD build's in-game rose must still show room-accurate
  exits, and `cd/data/0.bin` should change only by the ~200 B that moved between
  `command_view.o` and `command_rose.o`. It is currently 368,723 B.

## Risks

- **Ten reserved rows is a lot of a 224-line screen.** The strip costs five more
  console rows than the grid it replaces, in a terminal whose scrollback is the
  whole UI. This is the same trade the CD build's in-game view already makes, but
  that view has room art to justify it and this one does not. Check the row
  budget on hardware before committing to the strip; if it reads badly, the
  fallback is the rose alone beside the existing four-row grid, which needs
  `console_height` to grow a third case.
- **`console_view` growth is estimated, not measured.** ~3 KB is a line-count
  scaling of the CD build's 12,842 B object. Immaterial against ~194 KB of
  headroom, but replace the figure once the real build runs.
- **All-open invites dead ends.** Every direction submits, so a player leaning on
  the rose will hit "You can't go that way" often. That is the accepted cost of
  not lying about exits; it is also exactly what typing the direction would do.

## Non-goals

- Any room-name-to-object resolver, or any attempt to track the player's
  location from server text.
- Linking `command_view`, `command_panel` or `room_model` into the netbin.
- The command panel interface (`IFACE_PANEL`) in any form — this build has one
  input interface, the keyboard strip.
- Changing the CD build's rose behavior, which stays room-accurate.
