---
name: room-categorization-handoff
description: Where the tiered/genre-aware room-classification work stopped mid-Task-3, and the one artifact that must not be regenerated carelessly.
metadata:
  type: project
---

Paused 2026-08-05 near a usage limit, mid-Task 3 of 7. Branch
`room-categorization-tiers`, branched from `main` at 9f8b80a. **Not a worktree** —
`tools/assets/Z3/` is gitignored, so a worktree would have an empty game library
and the corpus generator would fail.

Spec: `docs/superpowers/specs/2026-08-04-room-categorization-tiers-design.md`
Plan: `docs/superpowers/plans/2026-08-05-room-categorization-tiers.md`
Ledger: `.superpowers/sdd/2026-08-05-room-categorization-tiers/progress.md` — this
is the recovery map; trust it and `git log` over recollection.

Done: Task 1 (`898df4d`, classifier moved to `saturn/src/classify/`, verbatim,
verified) and Task 2 (`ab721d8`, 1024-room corpus). Tasks 4-7 not started.

**The one thing to be careful about.** `test/corpus/blessed.inc` exists
uncommitted and is complete and correct: 1024 rows, blessed against the
*unmodified* classifier. It is the gate the whole plan rests on — Tasks 4-6 are
judged by the diff they produce against it. It is only valid because no scoring
change has landed yet. Once Task 4 begins, re-running `--bless` records the NEW
behaviour and the suite passes forever while proving nothing.

`test/room_class_test.c` is also uncommitted and **incomplete** — it still holds
the deliberate Step 1 stub (`run_suites` returns 0). Task 3 resumes by writing
Step 3's real snapshot suite, then committing both files together.

Both files are untracked, so `git clean -fdx` would destroy them. `blessed.inc`
is regenerable *only while no scoring change exists*.

Two design calls worth carrying forward, both corrections to my own first answer:
the corpus is built by decoding the story files directly (`tools/zstory.py`), not
by driving the interpreter — the runtime capture survives only as a complement for
the 781 rooms whose description is a routine rather than stored text. And
`LRKHOROR.WIN` was renamed to `LURKING.WIN` so walkthroughs pair with stories
strictly by stem, with no alias table.

Related: [[never-print-via-srl-debug]], [[user-runs-all-builds]]
