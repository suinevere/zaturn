# Design: netbin in-session pause menu

**Date:** 2026-08-29
**Status:** Implemented, hardware verification outstanding
**Amends:** `2026-07-25-netbin-minimal-design.md`, whose "Dropped" list names
Display options and Gameplay options as non-goals. They come back here, behind
a pause menu, at a measured cost of 12,688 bytes.

## Goal

Give the netbin the five settings the player can plausibly want mid-session,
without giving it the CD build's Options menu. Start opens a **PAUSED** box over
the live terminal offering **Resume**, **Display**, **Gameplay**, **Controls**
and **Restart**, and nothing else.

## Why these five and not the others

`options_menu` (`menu_pages.cxx:1428`) builds its row list at runtime from
`g_in_game` and whether sound is available. Of its nine rows:

| row | netbin |
|---|---|
| Resume | kept — this menu is over a paused-looking game, and naming the way back is the point |
| Save / Load | dropped — the game state lives in the multizork server's SQLite, not in backup RAM |
| Gameplay | kept |
| Display | kept, minus Dynamic |
| Sound | dropped — `SRL_USE_SGL_SOUND_DRIVER = 0`; there is no audio object in this build |
| Controls | kept |
| Network | dropped — the dialer is the root page, reached by Restart |
| Title Screen | replaced by Restart, which is the same thing in a build with no title |

## Display: why dropping Dynamic is what makes it affordable

`display_options_page` carries a Dimming row that appears only under the
Dynamic palette, and pins a wallpaper image slot on entry via
`display_pin_dynamic_slot(display_image_slot(title_bg_loaded_file()))`.

`title_bg_loaded_file` lives in `video/title.cxx` (15.9 KB), which this build
does not link. Lifting the page as-is would drag it back in and roughly double
the cost of the whole feature.

Dynamic is already unreachable here for two independent reasons —
`display_image_count()` returns 0 with no game selected, so
`display_cycle_palette` steps past `DISP_PAL_DYNAMIC`, and `display_apply`'s
image branch is `#ifndef NETBIN` — so the lifted page drops the Dimming row and
both slot calls rather than relying on either. `tests/test_netbin_lift.py`
asserts none of them come back.

The remaining three rows (Palette, Background, Text) cost nothing beyond the
page body: `video/display.c` and `menu/options.cxx` are both already in the
netbin's object list, so `display_cycle_row`, `display_apply` and every
`display_*_name` helper are already linked.

## Gameplay: one live row and one that has to be typed at the parser

**Difficulty** is live. `g_difficulty` picks the typeahead's ranking mode, which
this build has had since `2026-08-25-netbin-typeahead-design.md`;
`online.cxx:209` guards the trie rebuild on `g_online_diff == g_difficulty`, so
a change made here takes effect on the next turn. This closes the open item
named in `mem/2026-08-29-netbin-size-and-room-id-handoff.md`: until now
`g_difficulty` could only be set from the CD build's Gameplay page, and a
netbin-only player was stuck with whatever backup RAM held.

**Room text** is not. `g_verbosity` is read only by `engine/saturn_glue.cxx`,
which drives the local Z-machine and is not linked here — the parser is on the
server. Shipping the row as a number that moves and changes nothing was not
acceptable, so `online_mode` snapshots `g_verbosity` across the menu and, on a
change, types `verbosity_command()` at the server as one turn. That is exactly
what `saturn_glue.cxx:513` does for the CD build's Options menu, and
`verbosity_command()` lives in `engine/app_state.cxx`, which is already linked.

The page body itself is therefore a verbatim lift, and
`tests/test_netbin_lift.py` compares it against the original.

The player's half-built input line is saved and restored around that submit,
because `term_submit_line` resets the `KeyboardState` it sends from.

## Nothing is paused, so the wire must keep being pumped

This is the part that is not a byte cost.

The netbin's terminal loop calls `term_service(&ts, tr, ZATURN_RX_BUDGET)` once
per frame. `net/transport_uart.c` has **no software receive ring** — `tu_rx_byte`
reads the 16550's own FIFO directly. Any modal that runs its own poll loop stops
servicing, and after roughly a FIFO's worth of bytes the rest are lost.

The existing `reboot`-command confirm has the same exposure and gets away with
it because it opens immediately after a submit, when the server is quiet. A
pause menu does not: the player can open it mid-turn while a room description is
streaming, and can sit in it indefinitely.

The fix is a per-frame service callback registered on `menu_sync`:

- `menu.h` / `menu.cxx` gain `menu_set_service(fn, ctx)` under `#ifdef NETBIN`,
  invoked from `menu_sync` in the slot where the CD build calls
  `sound_service()` and `music_tick()`. The symmetry is exact — in the CD build
  `menu_sync` keeps the audio from starving, and here it keeps the carrier from
  starving. The CD build is byte-identical.
- `menu_sync` is the right hook because every modal in the build already calls
  it once per frame, so the pump reaches the Display, Gameplay and Controls
  pages nested under the pause menu without any of them knowing about it. That
  matters most for the Controls pages, whose bodies are verbatim lifts and
  cannot take an extra call.
- `online_mode` registers the pump around `netbin_pause_menu()` only, not for
  the whole session — the terminal loop already services the wire itself, and a
  registration held across it would double the per-frame RX budget.
- There is one slot, not a stack. A caller that registers must clear.
- `main()`'s post-`setjmp` landing clears it too, next to the existing
  `g_menu_backing_depth = 0`, and for the same reason: Restart longjmps out from
  inside the menu, leaving `menu_sync` holding a pointer into a dead frame.

Draining into the console is enough. The bytes land in the scrollback the
player returns to, which is the whole point of not losing them.

## Trigger

Start, pressed in the terminal loop. It is unclaimed there: Esc and a ~0.75 s
L+R hold are disconnect, L/R alone cycles suggestions, and the panel/keyboard
interface toggle is a Y/Z-class tap (`g_toggle_btn`, default 0). Inside the
menu, Start is a second way to close it, matching `options_menu`.

`mode_toggle_reset()` is called on return: the toggle button can be pressed and
released entirely while the menu owns the screen.

## Size

Clean rebuilds (`compile-netbin.bat clean` first — incremental builds lie by
~32 bytes here).

| step | bytes | delta |
|---|---:|---:|
| before | 163,904 | |
| pause menu, Display and Gameplay pages | 175,904 | +12,000 |
| trigger, RX pump and verbosity wiring | 176,592 | +688 |

Per-symbol, from `sh2eb-elf-nm`: the pause shell with `gameplay_page` inlined
into it is 8,036 bytes, the Display page 3,884.

Still far under `post.makefile`'s 400 KB gate, but that gate is documented as
softer than it looks — the real loader ceiling is lower and has never been
measured. The number that is real is transfer time: +12.7 KB is roughly +3.5 s
on a ~48 s download at 28.8k.

## Not done

- **Hardware.** Nothing here has run on a Saturn. In particular the claim that
  the RX pump keeps output intact across a long pause is a code-reading claim
  about a FIFO nobody has watched overflow.
- **Difficulty's rebuild cost.** Changing it mid-session re-runs the typeahead
  trie build: 4,722 allocations, ~77 KB of Low Work RAM. Expect a visible hitch,
  and confirm the previous trie is actually released before calling this a
  finished mid-session control.
- **The Room text round trip.** The typed verbosity command is sent but nothing
  reconciles `g_verbosity` with what the server actually did with it, or with a
  verbosity another player changed.
