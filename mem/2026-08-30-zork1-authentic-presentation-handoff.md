---
name: zork1-authentic-presentation-handoff
description: Zork I's 110 rooms now show their original Saturn background and play their original CD-DA track from a measured, generated table instead of a blessed scene tag; nine of ten tasks landed on branch zork1-authentic-presentation, dark rooms were investigated and left showing their picture, and none of it has been built or run.
metadata:
  type: project
---

**PARTLY STALE as of 2026-08-31.** The branch this describes no longer exists as
a series: all 27 of its commits were squashed onto `main` as `02ebc6c`, and three
further fixes landed inside that squash. The task table and the branch tips below
are history, not the tree. Everything else here -- the two alias rows, the twenty
unproven room orderings, Task 9's rejection, the `.CGL` tracking decision, the
four out-of-scope sub-projects -- still stands. See
[[dynamic-palette-strip-shift-and-mix-removal-handoff]].

Branch `zork1-authentic-presentation`, off `main` at `14fe384`. Spec
`docs/superpowers/specs/2026-08-30-zork1-authentic-backgrounds-and-audio-design.md`,
plan `docs/superpowers/plans/2026-08-30-zork1-authentic-backgrounds-and-audio.md`,
ledger `.superpowers/sdd/2026-08-30-zork1-authentic-backgrounds-and-audio/progress.md`
(the ledger is the authoritative record of every ruling made during execution;
this file does not restate its reasoning). Supersedes the Zork I half of
[[scene-tagged-art-handoff]] — see the staleness note at the top of that file.

## The owner has not built or run any of this

Not once, on any task. Every commit below was verified by host `gcc` tests and
`sh saturn/syntax-check.sh` only. Nothing has been seen on Mednafen or
hardware. The plan's own final-verification section still has an unchecked
build step, a Mednafen walkthrough, and a hardware pass — all owner work, not
done here, per the standing rule that the owner runs every build and every
emulator session.

**`syntax-check.sh` is weaker evidence than it sounds.** It runs
`-fsyntax-only` with no `-Wall`/`-Wextra` — it type-checks against the real
SRL headers under both `DEBUG` and release defines and nothing more: no object
file, no link, no ELF. Most files this feature touches (`cgl.c`'s SRL-facing
half, `title.cxx`, `room_art.cxx`, `main.cxx`) are SRL-only and cannot be
host-compiled at all, so they only ever got that syntax check. The pure-logic
core — the CGL decoder, the presentation table, `pres_of_room`/`pres_frame`
lookup, and the music room-subscriber wiring — is the part with real host-gcc
test coverage; everything that touches the screen is compile-checked only.

## What landed

| Task | Commits | What |
|---|---|---|
| 1 | `14fe384..89c3a95` | CGL decoder ported to C, proved against Python checksums for all 75 frames |
| 2 | `89c3a95..55cd270` | Eleven `.CGL` archives added to the disc image |
| 3 | `55cd270..7a39fda` | Generated `game_presentation.inc`; two fix rounds for a join defect (below) |
| 4 | `7a39fda..6fc0aa0` | `presentation.c`/`.h` — `pres_of_room`, `pres_frame`, `pres_area_name`, `pres_game_index` |
| 5 | `6fc0aa0..6c1474a` | `console_view` gains a real row count; six full-screen clear loops derived from it instead of hardcoded 28 |
| 6 | `6c1474a..b82d102` | `title_bg_show_raw`, the raw-CGL-frame blit path |
| 7 | `b82d102..5d5bf8d` | `room_art.cxx` — holds an area's archive resident, decompresses one frame per room, zero disc access inside an area |
| 8 | `5d5bf8d..8a7d001` | Music gets a room subscriber; category becomes `CAT_KIND_ROOM` for Zork I, silencing the old scene art path |
| 9 | blocked, no commit | Dark-room detection investigated and rejected; nothing shipped (below) |
| 10 | this task | Documentation only |

Branch tip after this task's commit is `8a7d001` plus the docs commit. Task 3
alone needed two fix rounds (`bd8f414..78d43aa..7a39fda`) to correct a room
swap the plan's own join algorithm produced — see the ledger's Task 3 entries
for the full defect and fix.

## The two alias rows, confirmed by hand

The spec's join table (`STRANGE PASSAGE`/`NARROW PASSAGE` and `CAVE`(x2)/`SHAFT`(x2))
flagged these two as the only rows sharing no vocabulary an edit-distance
match could find. Task 3 confirmed both directly against
`cd/Zork I - The Great Underground Empire (Japan)/zork1/1dungeon.zil`, by
grepping for the ZIL object names (not the room titles, which don't match)
and reading each candidate room's `DESC` and exit properties by hand:

- `STRANGE-PASSAGE` (exits west/in to Cyclops Room, east to Living Room) sits,
  in ZIL declaration order, exactly where Saturn room 42 sits in the CSV — right
  after Cyclops Room, right before Treasure Room. The ZIL's own,
  unrelated `NARROW-PASSAGE` sits where Saturn room 53 sits instead. The Saturn
  localization renamed "Strange Passage" to "Narrow Passage"; the title
  collision with the ZIL's real Narrow Passage is coincidental.
- Two ZIL rooms are titled verbatim "Cave" (`SMALL-CAVE`, `TINY-CAVE`, both
  staircases descending into darkness) and sit, in declaration order, exactly
  where Saturn's two `SHAFT` rows (54, 55) sit in the CSV.

Both pairings held as the spec already had them; positional confirmation
against the ZIL's declaration order was the check, not the title text. Full
detail in `.superpowers/sdd/2026-08-30-zork1-authentic-backgrounds-and-audio/task-3-report.md`.

**Decode time per room was never measured.** No task took this measurement;
if it matters before hardware verification, it is still open.

## Twenty of 110 rooms have unproven within-group ordering

Two duplicate-image groups pair Saturn rows to story rooms only by object
order versus Saturn index order, an assumption Task 3 proved unsound once (the
Strange/Narrow Passage swap) and could not re-verify for these two groups from
available data:

- **The 15 maze rooms** (Saturn rows 26–40, split 4 on `BMAZ_00` / 11 on
  `BMAZ_01`). Saturn resorted its rooms by title; the ZIL interleaves
  `DEAD-END-1..4` among `MAZE-1..15`. No available data recovers Saturn's
  internal order.
- **The 5 Frigid River rooms** (all track 0, four sharing `BRIV_01`, one on
  `BRIV_02`). Matching the ZIL's `RIVER-1..5` LDESCs to story objects gives an
  object-number order with no relation to river geography, and there is no
  Saturn-side connectivity data to establish the true order.

Both are parked, not guessed — a wrong guess is not better than the current
one. Both are settled by the same fix: the Mednafen breakpoint capture that
sub-project E (the seven unattributed tracks) already needs — read the live
room index at `0x060A597C` while walking the maze / floating downriver and
record which index shows which frame.

## Task 9 shipped nothing, deliberately

Dark rooms still show their picture. The investigation hit two independent,
either-sufficient blockers, both recorded in the ledger's Task 9 entries and
now also in the spec's Dark rooms section:

1. The ZIL compiles to Zork I release 119 serial `880429`, not the release 88
   serial `840726` the disc boots — no `ONBIT` number taken from it is
   trustworthy against the shipped binary.
2. Stronger, and independent of release: Zork's own `LIT?` routine scope-scans
   room contents and open containers; `room_model.c` walks immediate children
   only. A carried lit lamp — the ordinary case underground — is exactly what
   a room-attribute-only check would miss, and shipping one would have
   reported the Cellar dark while the player stood in it holding a lit lamp.

This does **not** invalidate Task 3's use of the same ZIL file — room count
(110, all three sides) and the two contested exits (Narrow/Strange Passage,
Cyclops/Living Room) are structural map facts, stable across Zork I releases,
and were re-verified once the release mismatch surfaced. What unblocks Task 9
is giving `room_model` a scope-aware traversal.

## The `.CGL` archives are tracked in git — confirm before merging

Task 2 tracked the eleven `.CGL` archives under `saturn/cd/data/BG/` rather
than gitignoring and staging them the way `cd/data/**/*.TGA` is (TGAs are
build products with a generator to hang a staging step on; the archives are
verbatim inputs with none, and a silently-failed staging step would ship a
disc with no room art). This duplicates the same 2.0 MB already tracked in
`analysis/zork_bg/raw/`. Reversible now by resetting this branch; **not**
reversible after merge without a history rewrite. This is the one decision
from this project the owner should confirm before merging — see the ledger's
Task 2 entry for the full ruling.

## Still open

The four sub-projects the spec named out of scope at the top:
- **Item pictures** (sub-project C)
- **Sound effects** (sub-project D)
- **The seven unattributed CD-DA tracks** (sub-project E) — needs the same
  Mednafen capture that would also settle the maze and river ordering above
- **The ending art** (sub-project F)

Plus, from this project itself: the maze and river ordering, dark-room
detection, and everything in the plan's final-verification section that only
the owner can run.

Related: [[scene-tagged-art-handoff]]
