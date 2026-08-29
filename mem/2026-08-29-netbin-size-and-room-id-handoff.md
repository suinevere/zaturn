---
name: netbin-size-and-room-id-handoff
description: The netbin cut from 242 KB to 159 KB, given the command panel, and taught to learn its room from multizorkd out of band; pushed to main as 45ac1a9 and 7f10cd2, verified against a live daemon but never on hardware.
metadata:
  type: project
---

Pushed to `main` as two commits: `45ac1a9` (size work, command panel, easy-mode fix) and
`7f10cd2` (the room-id channel, both ends). `cd779d1` — the dial-gap fix, amended by the owner
from two seconds to three — was already on the remote and is untouched.

The design is in
`docs/superpowers/specs/2026-08-29-netbin-server-room-id-design.md`, marked implemented with
hardware verification outstanding. The commits carry the what. This file carries the numbers they
were built on, the negative results worth not repeating, and a bug found and deliberately not
fixed.

Related: [[game-load-time-handoff]] for the load-time work these sit on top of,
[[command-panel-and-dim-handoff]] and [[controls-and-panel-interface-handoff]] for the panel this
put on the netbin, [[multizork-prompt-rewording-handoff]] and
[[multizork-lobby-and-four-seats-handoff]] for the server it now talks to.

## Nothing here has run on a Saturn

Every client-side claim is link-time or host-test evidence. The panel's on-screen geometry, the
rose changing as you walk, and the three-second dial gap have all never been seen. The server half
*has* run, under `tests/test_multizork_room_id.py`, but only in a container on this machine.

## The size trail, measured on clean rebuilds

| step | bytes | delta |
|---|---:|---:|
| baseline | 242,224 | |
| trim the embedded story | 177,376 | −64,848 |
| drop the CD filesystem | 149,456 | −27,920 |
| add the command panel | 159,472 | +10,016 |
| add the room-id client | 163,904 | +4,432 |

Incremental builds lie by ~32 bytes here; every figure above is after `compile-netbin.bat clean`.

The two big wins were both one-line-shaped. The story blob was the whole of Zork I embedded only
so the typeahead had a dictionary — 76.4% of it is Z-code and prose nothing in that build reads.
The CD filesystem was 23 KB of `LIBCD` hanging off a single undefined `GFS_Init`, which
`SRL::Core::Initialize` calls unconditionally.

## Negative results — do not spend time on these again

- **`-Os` is worth exactly zero bytes.** Measured on a clean build: 149,456 either way. The image
  is `.rodata`-dominated, so code-size flags have nothing to bite on. Reverted rather than left in
  the makefile making a claim it cannot keep.
- **`--gc-sections` cannot link against this SDK.** `KEEP()` for `PRELOADER`, `SLSTART`, `SLPROG`,
  `COMMON`, `SEGA_P`, `.ctors`/`.dtors`, `WORK_AREA` and `COMMAND_BUF` clears the first wave, then
  it fails *inside the SDK's own prebuilt objects*: `preloader.o` loses `memset` and
  `tlsf_create_with_pool`, `tlsf.o` loses `__ashlsi3_r0`/`__clzsi2`/`__lshrsi3_r0`/`__ffssi2`.
  Those are archive members `ld` stops pulling once GC is on — a link-line ordering problem in
  `SaturnRingLib/saturnringlib/shared.mk`, which `SRL_CUSTOM_LDFLAGS` can only append to. Would
  need SDK-side changes (`--start-group`, or repeating the archives).

## A bug found and deliberately left alone

`multizorkd` reads Z-machine globals as a native `uint16 *` (`multizorkd.c:1623`, `:1721`) while
mojozork reads them big-endian via `READUI16` (`mojozork.c:57`, `:198`). On a little-endian host
those disagree, so **`player->gvar_location` holds the byte-swap of the real object id**. It
round-trips consistently, so the game and the SQLite persistence work.

But `get_room_name(inst, objid)` gates on `objid <= 255`, and a swapped id never passes. The
predicted live symptom is an empty room name in the multiplayer broadcasts:
`*** someone entered . ***`. Not confirmed on the deployed server — worth ten seconds with two
connections in one room.

Not fixed here because `gvar_location` is persisted in the `players` table, so changing its
representation has migration consequences that belong in their own change. The room-id emitter
routes around it by reading the two bytes big-endian directly, which is why it reports 180/81/75
rather than 46080/20736/19200.

## Running the server side

`tests/test_multizork_room_id.py` builds and runs its own daemon in Docker and tears it down, so
there is no recipe to follow: run it. multizorkd is POSIX-only and mingw cannot compile it, which
is why the test reaches for a container rather than a local build. The image caches, so only the
first run is slow.

That test also carries the two things that used to need writing down here — the four-input lobby
walk, and the fact that reaching a game at all is a precondition rather than an assertion. It
skips, never fails, on anything that is the environment's fault.

Two pre-existing `-Wformat-truncation` warnings in `show_lobby`/`build_lobby_rows` are noise.

**Do not point it at a daemon you did not start.** It plays the game, which writes rows into that
daemon's sqlite database. The first version of it defaulted to `127.0.0.1:2323` and would have
quietly littered the owner's own server with junk games every run.

Room ids worth reusing as fixtures, captured from a live daemon walking north, north, south from
the start: **180** West of House, **81** North of House, **75** Forest Path.
`tests/test_room_model_static.c` pins their exit tables and, more usefully, that the map joins up
— 180's north is 81 and 81's west is 180.

## Environment traps

The heredoc trap from [[game-load-time-handoff]] bit three more times, in new shapes. Writing C or
make syntax through `python3 - <<'PY'` mangles backslashes: `\\\n` in a Python literal became a
literal `n`, `'\x01'` became a real control byte in the source, and `\\n` inside a `printf` string
became a real newline mid-literal. Use the `Write`/`Edit` tools for any content containing escapes,
or build the string with `chr(92)`.

`git status` reported `saturn/src/net/term.c` as a *binary* file after one of these — that is the
symptom of an embedded NUL, not corruption of the repo.

## Open items

- **Hardware.** The rose changing as you walk, the panel's row budget on a 224-line screen, and
  the three-second dial gap landing before the browser's tone clears.
- **The `dynamic_memory` diff.** The spec's central risk: the client decodes exits from the story
  as shipped, and nothing yet proves Zork never rewrites a direction property at runtime. Pull an
  `instances.dynamic_memory` blob from `multizork.sqlite3` and diff it against `ZORK1.Z3` over the
  property-table ranges of the 112 objects that decode with exits. A difference means the server
  must send exit states rather than an id.
- **The endianness bug above**, if the empty room name is confirmed.
- **The netbin has no difficulty UI.** Easy-mode ranking works now, but `g_difficulty` can only be
  set from the CD build's Gameplay page; a netbin-only player is stuck with whatever backup RAM
  holds, defaulting to `DIFF_EASY`. A row on the netbin's Controls page would be small.
- **The CD build's online mode still holds a stale local room model** while a remote game runs.
  Named as a non-goal in the spec; it wants the same exits-only treatment.
- **Four pre-existing test failures**, unchanged and unrelated: `test_ci_boot_music.py`,
  `test_lwram_splash_budget.py`, `test_multizork_join.py`, `test_multizork_lobby.py`. All four
  reproduce at the commit before this work.
- **`squash-backup`** is a local-only branch holding the twelve unsquashed commits. Delete when
  the two squashed ones are trusted.

## Suggested skills

- **`superpowers:verification-before-completion`** — most of this is unverified on hardware, and
  the temptation to call the rose "working" because it links is exactly what that skill blocks.
- **`superpowers:systematic-debugging`** — for the `dynamic_memory` question and the endianness
  symptom, both of which are "find out what the bytes actually say" problems.
- **`diagnosing-bugs`** — if the rose draws wrong on hardware, the failure could be the frame
  parser, the decode, the id, or the geometry; that needs a loop, not a guess.
- **`superpowers:brainstorming`** — before building the "server sends derived facts" tier, if the
  `dynamic_memory` diff forces it. That is a protocol redesign, not an increment.
- **`code-review`** — `7f10cd2` touches both a public-facing server and shared client code that
  the CD build links; the `cv_cmd_accept` signature change alters CD behaviour deliberately and
  deserves a second pair of eyes.
