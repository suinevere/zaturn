---
name: map-passage-marks-and-exit-destinations-handoff
description: The map gained four marks derived from the story's exit graph -- one-way arrowheads, U/D on vertical exits, dashed conditional runs, self-loop circles -- and then the owner's first look at it found that none of the conditional ones could ever have drawn, because the room decoder had always discarded the destination byte that door and flag exits carry; twenty commits sit on an unmerged branch, none of it seen on hardware, and the merge question is open.
metadata:
  type: project
---

Branch `map-passage-marks`, **twenty commits, unmerged**, forked from `main` at
`17200e3`. Only `saturn/boxart/RAW_ZORK.xcf` is uncommitted and no commit on this
branch touches it -- it is the owner's in-progress artwork and must stay that way.

**The merge question is open and is the first thing to ask.** The owner was
offered: merge to `main` locally, push and open a PR, keep the branch, or run a
fresh whole-branch review over the four follow-on commits first. They invoked
`/handoff` instead of answering, so nothing has been integrated.

## What is here

Two specs carry the design and the reasoning; do not restate them.

- `docs/superpowers/specs/2026-09-01-map-passage-marks-design.md` -- the four marks.
- `docs/superpowers/specs/2026-09-02-conditional-exit-destinations-design.md` -- the
  decoder defect and U/D on every vertical link.
- `docs/superpowers/plans/2026-09-01-map-passage-marks.md` -- the six-task plan.

Both specs are marked implemented on the branch and **not yet seen on hardware**,
which is the one claim neither the host suites nor the linkers can make.

The commit messages on this branch are long and explanatory by house style, and
they are the durable record of why each thing is the way it is. `git log
17200e3..HEAD` is the honest place to start.

## The thing a fresh agent most needs to know

The four marks shipped first. The owner then opened the map and asked why Behind
House and the Kitchen were not joined. The answer was that
`room_model_refresh_room` read an exit's destination only for one-byte direction
properties; door exits (five bytes) and flag exits (four) carry the room in byte 0
and it was thrown away, so `map_model_exits` dropped them for `dest == 0`. That one
discarded byte is why the map looked sparse **and** why the dashed-conditional mark
could never fire at all. Fixed in `480d2d9`, guarded so byte 0 is trusted only when
it names an object that is itself a room.

Two corrections worth carrying forward because they were mine, not the
implementers':

- The measured figures I first put in the 09-02 spec were wrong. Direction
  properties are identified from the dictionary's `FL_DIR` flag (`0x10`), the way
  `room_model.c` binds them and `tools/gen_map_atlas.py:171` gates on them -- not
  by a consensus heuristic. Correct: Zork I 31/31, all stories 843/853. The spec
  records the wrong numbers and why they were wrong rather than quietly replacing
  them.
- The first version of `saturn/tests/test_exit_dests.py` was a standalone Python
  decoder that never touched the C code and would have passed against a revert,
  while its own docstring called it the regression oracle. It is now bridged
  through `saturn/tests/dump_exits.c`, which drives the real decoder through its
  public API; the test compares C output against an independent Python decode over
  all 31 stories. **Its acceptance criterion is that reverting the two `dest` lines
  makes it fail** -- that was demonstrated and independently re-derived. Keep that
  property if you touch it.

## What has not been done

- **Nothing has run on hardware or in an emulator.** No emulator is drivable from
  this environment. When it is first run, look at: any `U`/`D` that appears or
  vanishes while scrolling, a dashed run that reaches unbroken from one mark to
  another, and whether the one-pixel `U`, `D` and loop ring read at all on a
  parchment background on a real television.
- **The hamburger mark for load-limited passages is deliberately not built.** The
  chimney is a four-byte flag exit, indistinguishable from any other by static
  data; the limit lives in a routine. The owner has decided to reach it through
  the atlas PDF scan instead, and to use that scan as a second verification oracle
  against the work already here. That is the next piece of work.
- **Neither the C host suites nor pytest run in CI.** Nothing in
  `.github/workflows/` compiles or runs them. This branch added several suites and
  a cross-check that only re-run when somebody remembers. Flagged to the owner as
  the highest-value follow-up; it is their call, not a defect.
- The whole-branch review was run at `cbb55cb` and **predates the four follow-on
  commits** (`7938a4c`, `480d2d9`, `a811bce`, `888f3ae`), one of which changes
  decoding for every shipped story.

## Verification state

Nine host C suites pass, pytest 39 passes, both Saturn targets link. Two of the C
suites need include paths and arguments the plan's own commands omit:

    gcc -O2 -Wall -Wextra -I saturn/src -I saturn/src/engine -o /tmp/t \
        saturn/tests/test_room_model_static.c saturn/src/engine/room_model.c
    /tmp/t saturn/cd/data/Z3/ZORK1.Z3

`test_command_rose` is the one that proves the decoder change did not alter exit
classification -- **build it fresh**, since a stale binary in `/tmp` will happily
print `ok` after a failed compile. The compass rose, command view and console view
read `exits[]` only; the newly populated `dest` field is read by `map_model.c` and
nothing else (`online.cxx:311` writes a zero into a synthetic model and never
reads the decoder's).

Do not run `make` -- see [[zaturn-make-from-git-bash-drops-c-sources]]. Drive
`saturn/compile.bat` from PowerShell with the `cd` and the build in one command;
`cmd //c compile.bat` from the Bash tool has failed in this repo.

## Working scratch

`.superpowers/sdd/2026-09-01-map-passage-marks/progress.md` holds the full ledger:
every ruling with its cost-if-wrong, the deferred minors from each review, and the
per-task reports. It is git-ignored scratch and will not survive a `git clean`;
the commit messages carry the same reasoning durably.

## Suggested skills

- `superpowers:finishing-a-development-branch` -- the branch is complete and
  unintegrated; this is the skill that presents the merge options properly. Start
  here unless the owner redirects.
- `superpowers:brainstorming` -- before any work on the hamburger mark or the PDF
  scan oracle. That is new design, and the last two rounds both began with a wrong
  assumption that a few minutes of measurement corrected.
- `superpowers:subagent-driven-development` -- if the PDF scan work is taken on as
  a plan. The six-task run recorded here found six plan defects pre-flight and
  four more in review, including undefined behaviour and a silently order-dependent
  route choice; the ceremony earned its cost.
- `superpowers:systematic-debugging` -- if the first hardware run turns up
  something. Resist explaining a symptom from the code alone: the decoder defect
  was found by decoding real story bytes, not by reading the decoder.

## Related

[[map-floors-crosshair-and-party-handoff]] is the immediate predecessor and the
last entry that describes the map screen this work extends.
[[ingame-map-handoff]] and [[map-atlas-handoff]] cover the model and the authored
table underneath it; [[multigame-atlas-handoff]] covers the eighteen-game reach of
that table, which is the same corpus the new cross-check oracle reads.
