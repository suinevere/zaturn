---
name: genre-banded-art-handoff
description: Genre banding shipped and merged into main, but a tracked manifest lost the curation to a branch switch and the period axis is on the wrong thing — the noun, not the photograph.
metadata:
  type: project
---

Seventeen commits ending `589d6c5` are **merged**: `589d6c5` is an ancestor of
`main`, which now sits 45 commits further on at `134f9a1`. Nothing is orphaned.
`room-art-pipeline`'s tip is unrelated controller work, which makes the branch
look empty of this — check ancestry, not the branch name, before concluding
anything was lost.

Spec `docs/superpowers/specs/2026-08-10-genre-banded-art-pools-design.md`, plan
`docs/superpowers/plans/2026-08-10-genre-banded-art-pools.md`, per-task ledger
and reports under `.superpowers/sdd/2026-08-10-genre-banded-art-pools/` —
git-ignored scratch, so `git clean -fdx` destroys them. The ledger records every
finding, ruling and parked minor; do not re-derive them.

Supersedes the curation half of [[art-review-server-handoff]]; its server
recipe, the sweep incident and the fixture-coincidence lesson all still stand.

## The manifest is tracked, and that is the live bug

`tools/assets/art_manifest.json` is **tracked by git**. Switching to `main`
overwrote it with the committed copy — 412 records, 119 accepted — discarding a
post-reset 480-candidate sweep. The disk and the manifest now disagree:

| | on disk | orphaned (no manifest row) |
|---|---|---|
| candidates | 438 | 306 |
| accepted | 181 | 59 |

365 images exist that the server cannot show. This is the second time this
session that curation state was lost to a git operation — the first was a
subagent's `git stash` whose pop failed on a locked `.xcf`, reverting the
manifest to this same 412/119 signature while it judged the rest "superseded".

**Gitignoring the manifest, with a separate committed snapshot at promote time,
ends this whole class of loss.** It was offered and never answered.

Pre-reset backup, outside the repo and untouched by `git clean`:
`C:\Users\saggl\CLionProjects\zaturn-art-backup-20260810-094811` — 2065 records,
310 accepted, 311 accepted PNGs, 1091 candidates. That is the state *before*
the owner's deliberate reset, not the batch that followed it.

Three routes, owner's call: restore that backup; rebuild a manifest from the 365
orphans on disk; or reset again with the now-merged vocabulary.

## The period axis is on the wrong thing

The design tags period on the **noun**, at fetch time (`noun_genre` in
`art_queries.json`). The owner found the flaw by trying to use it: asked to
review a non-period NAUTICAL picture, "should I pick ones that are period-
neutral to both?" has no sensible answer.

There is no period-neutral photograph of a boat. `dock` is period-*ambiguous*;
every image it returns is period-*specific* — timber pilings or container
cranes. Only `ocean` and `sea` are genuinely timeless. Neutrality is a property
of the photograph, and the only one who can see it is the reviewer.

So period belongs on the **verdict**, not the vocabulary: accept becomes
three-way (fantasy / modern / both). That turns the neutral band from a dumping
ground into a real judgement, and fixes the same latent trap in TOWN and HOUSE.

This invalidates only the Python half — Tasks 1-3 (`noun_genre`, the noun-keyed
bucketing in `make_tga.py`, the tag source for the generated table). **The C
side is untouched by it**: the band layout, `display_next_in_band`, the rotor
re-seating and the genre wiring all take a band index and do not care how a
picture earned it. Rework is roughly Tasks 1-3 in size and deletes more than it
adds.

## What the measurements settled

Adjectives that describe an *experience* never produce a keeper. Twenty-one
scored 0 across 219 fetches — `creaking` 0/12, `moored` 0/10, `bustling` 0/12,
`torchlit` 0/14, `wooden` 0/16 — against `misty` 85%, `ruined` 69%, `arid` 66%,
`old` 62%. The cleanest proof is `dim` 33% beside `dimly lit` 0/10: same
meaning, but only one is a word a photographer types. All 23 slots were
replaced in `589d6c5` and a guard test carries the dead list by name.

MYSTERY had been shipping five of eight dead — 62% of its query space — which
is why it was thin. Not its nouns.

Nouns collide two ways, and the guards are different. Mood-vs-mood (`cabin` is
NAUTICAL's and SCIFI's) is derived from the classifier table at test time, so a
new colliding keyword fails the test automatically. Mood-vs-world (`cabin` is a
log cabin before it is a ship's) is hand-listed, because no mood owns the rival
sense — the general guard is structurally blind to it.

## Two things not done

The final whole-branch review from the plan was never run. Tasks 1-6 each passed
task review; the branch-level pass did not happen.

Qualified nouns are effectively unreachable at real budgets. `build()` emits
noun-major with donor nouns first, so NAUTICAL's period nouns start at query #9
of 160 and a 16-request budget never reaches them. A `--noun` filter mirroring
the existing `--mood` one is the fix — the same problem `--mood` was added to
solve, one level down. Under the verdict-based redesign this matters less:
period should come from judging `dock` photos, not from asking for a
period-specific phrase.

## Suggested skills

`superpowers:brainstorming` before rebuilding the period axis. Both times a
vocabulary or source change was treated as obvious, a load-bearing design
question surfaced underneath — and the noun-vs-photograph error is exactly that
shape, caught only when the owner tried to act on it.

`superpowers:subagent-driven-development` to execute any resulting plan; the
ledger format under `.superpowers/sdd/` expects it. Forbid `git stash` in every
dispatch prompt the way `compile.bat` is forbidden.

`superpowers:verification-before-completion` before claiming any of this works.
Three plan defects this session were found only by reading the call sites: seven
`CATEGORY_ART_N` uses where the plan named three, a test referencing a
`static const` invisible to its translation unit, and a confinement assertion
that would have failed against correct code.

Related: [[art-review-server-handoff]], [[classify-from-captured-turn-text]],
[[user-runs-all-builds]], [[work-can-vanish-via-clion-rebase]],
[[never-print-via-srl-debug]]
