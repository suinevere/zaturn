---
name: room-art-pipeline-handoff
description: PARTLY STALE (2026-08-08) — the .env bug is closed and the pipeline has run; the calibration warnings and the two owner decisions still stand.
metadata:
  type: project
---

**Superseded in part on 2026-08-08.** Three sections below are no longer true and
are kept only so the reasoning survives:

- *The one open bug* — closed in `7d67dd9`, mutation-verified twice.
- *Before running Task 6* — the pipeline has run. Sitting 1 committed 120
  backgrounds across eleven moods (`b126c47`); SCIFI took nothing. The calibration
  advice was right: `busyness_max` was mis-scaled at 18.0 and is now 35.0
  (`ca424ea`), `banding_max` has still never fired against a real photograph, and
  whether 35.0 is the right legibility limit remains unproven pending the hardware
  check. The key was never rotated — the owner chose to keep using it.
- *Suggested skills* — review decisions are reversible now; see
  `docs/superpowers/specs/2026-08-08-art-review-persistence-design.md`.

Still live: the two decisions left for the repository owner, and the whole
*What this project taught* section, which has since grown from six defective
checks to nine. See also [[classify-from-captured-turn-text]] for the two
classifier bugs found afterwards.

Paused 2026-08-07 before Task 6, the only remaining work. Branch
`room-art-pipeline` @ `35efe79`, 17 commits ahead of `origin/main`, working tree
clean, 89 tests passing.

Spec: `docs/superpowers/specs/2026-08-06-room-art-sourcing-design.md`
Plans: `docs/superpowers/plans/2026-08-06-room-art-saturn.md` (done, merged) and
`docs/superpowers/plans/2026-08-06-room-art-pipeline.md` (Tasks 1-5 done, 6 open).
Ledgers: `.superpowers/sdd/2026-08-06-room-art-saturn/progress.md` and
`.superpowers/sdd/2026-08-06-room-art-pipeline/progress.md` — these are the
recovery maps and hold every finding, ruling and deferred minor. They are
git-ignored scratch, so `git clean -fdx` destroys them; `git log` is the backup.

**Plan A is merged and hardware-verified.** Per-mood disc folders, slot synthesis
replacing the boot scan, and an adjustable wallpaper dim. Nothing outstanding.

**Plan B builds the pipeline but has never fetched anything.** `art_nouns.py`,
`art_queries.py`, `art_metrics.py`, `fetch_art.py`, `art_review.py`, plus
`art_status.py`. All offline, all reviewed.

## The one open bug

`fetch_art._parse_dotenv` catches only `OSError` around
`read_text(encoding="utf-8")`. A `.env` holding invalid UTF-8 raises
`UnicodeDecodeError` — a `ValueError` — which escapes `load_dotenv_into_environ`
and crashes `main()` instead of printing the not-set message, contradicting the
function's own docstring. Fix is to catch `ValueError` alongside `OSError` and add
a test writing non-UTF-8 bytes. The existing tests only cover line-level
malformation, so 89 tests pass over the crash.

## Before running Task 6

Do not run `fetch_art.py` bare. Its default is 99 per mood, 1188 images. Start at
about 12 per mood. `tools/.env` holds the Pixabay key, is git-ignored as of
`08a680a`, and was briefly staged before that — it never reached history, but the
key is exposed in the paused session's transcript and should be rotated.

**Treat the first batch as threshold calibration, not curation.** All three values
in `art_metrics.THRESHOLDS` are guesses that have never scored a real photograph,
and `banding_max = 12.0` may never fire — a full-gamut synthetic gradient scores
about 4.12. Read the rejections before the acceptances. The manifest records
metric rejections under their own status precisely so they can be told apart from
human taste rejections later.

## Two decisions left open for the repository owner

`CLAUDE.md` forbids comments inside function bodies, but `display.c` is full of
them and predates all this work. New code has been held to the rule, which is
slowly making that file inconsistent with itself.

`tools/convert-backgrounds.sh` promises a fallback to "the TGAs already committed
under `saturn/cd/data/TGA/`". They are git-ignored and have never been tracked, so
a clean checkout with no Python builds a disc with no art. Either make the promise
real or drop it from the prose.

## What this project taught, worth carrying forward

Across both plans the implementation specifications were reliably correct and the
verification specifications were reliably wrong. Six defective checks were found:
a banding fixture with too few colours to band, a Pillow `FIND_EDGES` border leak
that made busyness measure brightness, a stub whose ids never repeated so a dedup
branch was unreachable, an idempotency test masked by an already-unlinked file, a
tautological `or True` assertion, and the `.env` decode gap above. Most were caught
by implementers refusing to accept a passing test at face value.

Two habits earned their keep. Mutation-test every guard — break the thing a test
protects and watch it fail before trusting it. And run the whole suite: a
`test_make_tga.py` broken by Plan A's first task survived to `main` because every
command in that plan was `pytest saturn/tests/` and nobody looked at
`tools/tests/`.

Related: [[user-runs-all-builds]], [[verify-before-claiming-root-cause]],
[[gfs-loaddir-zero-is-current-dir]]

## Suggested skills

`superpowers:subagent-driven-development` to resume Plan B — the ledger's format
and resume rules are what the remaining work expects. Task 6 is curation and needs
a human at the contact sheets, so it is not fully agent-executable.

`superpowers:systematic-debugging` for the `.env` decode bug rather than patching
the except clause blind.

`superpowers:verification-before-completion` before claiming any of this works —
this project's whole lesson is that a green suite proved less than it appeared to.

Note for whoever dispatches subagents here: the auto-mode permission classifier
blocked roughly a third of dispatches this session, always cleared by shortening
the prompt. Lean on the extracted task brief and pass only what the brief cannot
know.
