---
name: netbin-menus-and-tile-chrome-handoff
description: The netbin got a pause menu and the marble dashboard, every menu box's border moved onto the tile layer, and the flicker that caused was diagnosed and fixed; pushed to main through 405f040, with the marble-recolour question left open and unanswered.
metadata:
  type: project
---

Five commits, all on `origin/main`, ending at **`405f040`**. Every design decision is in
the three specs below; this file carries only what they do not — the measurements as a
series, the traps, and the one question the session ended on.

- `docs/superpowers/specs/2026-08-29-netbin-pause-menu-design.md`
- `docs/superpowers/specs/2026-08-29-netbin-dashboard-design.md`
- `docs/superpowers/specs/2026-08-29-menu-border-design.md`

Related: [[netbin-size-and-room-id-handoff]] (whose "netbin has no difficulty UI" open item
this closed, and which is now edited to say so), [[input-dashboard-handoff]] for the NBG2
layer this builds on, [[command-panel-and-dim-handoff]] for the strip itself.

## The size series, all clean rebuilds

`compile-netbin.bat clean` first, every time — incremental builds lie by ~32 bytes here.

| step | bytes | delta |
|---|---:|---:|
| session start | 163,904 | |
| pause menu (`1017e90`) | 176,592 | +12,688 |
| dashboard into the netbin (`44f8ef9`) | 181,920 | +5,328 |
| menu borders on NBG2 (`6cc79c2`) | 182,992 | +1,072 |
| flicker fix + fade channel (`405f040`) | 183,488 | +496 |

~45% of `post.makefile`'s 400 KB gate, which still documents that the real loader ceiling
is lower and has never been measured.

## Nothing here has been seen except the flicker

The owner ran it and confirmed **the flicker is gone**. That is the only visual claim in
this session with evidence behind it. Not seen by anyone: the bevel border itself, the
pause menu, the dashboard in the netbin, and the fade now that NBG2 rides channel A.

The netbin cannot be run on this machine at all — it needs the PlanetWeb loader, a NetLink
modem and a live multizork daemon to reach any screen that shows this work.

## The measurement method, which is the reusable part

Every cost figure above was measured, not estimated, and twice the estimate was wrong in a
way worth remembering:

- **Estimated 20 new tiles for the menu border; it was 8.** The panel's frame tiles come in
  fours only because each carries marble that must stay in register with a 32-pixel repeat.
  Nothing sits behind the box tiles, so there is nothing to phase.
- **Estimated "6 KB or possibly much more" for the dashboard; it was 5,328 with no "much
  more".** The feared SRL VRAM/CRAM/tilemap machinery was already linked via `text_map`.

The probe technique: copy the target `.cxx` to the scratchpad, append the lifted bodies,
compile with the exact flags from the build log, and diff `sh2eb-elf-size`. For a true
image delta, swap the probe file in, clean-build, read the `zaturn.netbin:` line, restore.
`sh2eb-elf-nm --print-size --size-sort` on the `.o` gives per-function bytes.

**Trap:** the `dash_*.o` files left on disk after `compile.bat` are `-DDEBUG` objects. Sizing
those instead of the netbin build's own is what produced a wrong 5,644.

## The flicker, and why the first design was wrong

Worth reading `docs/superpowers/specs/2026-08-29-menu-border-design.md` for the mechanism.
The part that generalises: **`g_menu_backing_depth` does not mean "a menu page is open".**
`online_mode()` declares a `MenuBacking` at `online.cxx:350` that covers the entire telnet
session, so anything keyed on that refcount stays true through all of gameplay. The fix
asks the layer what is painted on it instead.

Diagnosis used a throwaway host harness that replayed the real per-frame call order against
the real `dash_map.c` and asserted on `dash_cell`. It went red on both reported symptoms
before any theory existed, including "first load-in" with no pause menu in the path.

**The harness was not kept**, because it re-implements menu.cxx's frame driver and a copy
of that logic in the test tree would drift. That leaves a real gap: the two halves are
pinned where they live (`test_dash_map.c` for the hold's scoping, a source check in
`test_netbin_lift.py` for the page frame-drops) but nothing covers the *interaction*.
Anything that changes the claim/expire protocol should expect to rebuild it. Rebuilding it
took about twenty minutes and the transcript of what to model is the spec's own list of
call sites.

## The open question, unanswered

The session ended on: **"feasibility to change marble background color in game, i.e. purple
for green text, or require new textures".** It was interrupted before an answer. The facts
were gathered and are worth not re-deriving:

- **No new textures are needed.** `dash_tiles.c` is 4bpp — the tiles store palette *indices*
  0..15, never colours (`dash_tiles.h:20`). The entire look comes from `dash_palette[16]`,
  loaded into CRAM entries 16..31 by `dash_view.cxx:100`. Recolouring is writing 16 CRAM
  entries; the 63 tiles are untouched.
- The palette is a ramp by role, not arbitrary colours — see the `PALETTE` table in
  `tools/gen_dash_tiles.py`, where blue runs two steps above red and green throughout,
  "which is what makes the grey read as stone". A hue change is a transform of that ramp.
- There is already an input for "which colour": `DISP_TEXT_*` in `display.h` carries nine
  named text colours (`DISP_TEXT_GREEN` is 2), and `g_display.text` holds the player's
  choice. A rule tinting the chrome to complement the text colour has everything it needs.
- Open design questions nobody has answered: whether the tint is derived from
  `g_display.text` automatically or is its own Display row; whether it should also move with
  the palette presets; and whether a saturated stone still reads as stone at 16 entries.

`tools/preview_dash.py OUTDIR` renders this without a Saturn — it parses the palette out of
`dash_tiles.c`, so a recoloured palette can be judged by eye before anything is built.

## Environment traps hit again this session

- **The heredoc trap from [[netbin-size-and-room-id-handoff]] bit twice more.** A
  `python3 - <<'PY'` block writing `\n` into a makefile produced a literal `\n` and broke
  the NETBIN source list; a later one silently no-matched a `str.replace` whose pattern
  contained `\n`. Use `Write`/`Edit` for anything containing escapes. The silent no-match is
  the dangerous shape — it prints success and changes nothing.
- **`make clean NETBIN=1` deletes the CD build's artifacts**, because both targets share
  `BuildDrop` basenames. Any netbin build also overwrites the CD `.iso`/`.bin`. Run
  `compile.bat` to put the disc back.
- **`TMP` must be set before invoking the toolchain from git bash**, or `sh2eb-elf-g++`
  fails with `Cannot create temporary file in C:\WINDOWS\`. Running `compile.bat` through
  `cmd /c` from PowerShell avoids it entirely and is the easier path.

## State

`git status` shows only ` m SaturnRingLib` — the pinned SDK submodule, dirty from build
objects written inside it. Pre-existing, not part of this work, deliberately untouched.

The four pre-existing test failures named in [[netbin-size-and-room-id-handoff]] were not
re-run this session and are presumed unchanged.

## Suggested skills

- **`diagnosing-bugs`** — earned its place here. If the bevel, the pause menu or the fade
  misbehaves on hardware, the same move works: the symptom is visual but the state is in
  `dash_map.c`, which is SRL-free and host-compilable, so a harness replaying the real frame
  order beats reading code. Do not skip Phase 1 for this codebase; the flicker had two
  independent causes and only the loop separated them.
- **`superpowers:verification-before-completion`** — most of this is unseen. The temptation
  to call the border "working" because it links and the tests pass is exactly what that
  blocks.
- **`superpowers:brainstorming`** — before building the marble recolour. "Purple for green
  text" is one point in a design space that includes automatic derivation, a new Display
  row, and per-preset tints; the cheap part is the code and the expensive part is choosing.
- **`prototype`** — pairs with the above. `tools/preview_dash.py` already renders a palette
  to PNG without hardware, so candidate ramps can be looked at before any Saturn code moves.
- **`code-review`** — `405f040` touches `menu.cxx`, `title.cxx` and all 24 page frame-drops
  in both builds; the colour-offset change alters CD-build behaviour outside the menus
  (the gamepad strip now fades with in-game fades) and deserves a second pair of eyes.
