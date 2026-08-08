# Art Review — Reversible Decisions, Manifest-Sourced Sheets — Design

**Date:** 2026-08-08
**Status:** Proposed
**Follows:** `2026-08-06-room-art-sourcing-design.md`

## Goal

The review loop is one-way. `promote()` skips any record whose status is not
`CANDIDATE`, so the first verdict written for an image is its last. `sheet()`
renders only `CANDIDATE` records with a hardcoded `checked` box, so a decided
image disappears from the sheets entirely and an in-progress pass is lost the
moment the tab closes.

The first sitting made the cost concrete: 168 images were rejected in a single
pass, and there is no way to reconsider one of them short of editing
`art_manifest.json` by hand. Curation is a matter of taste applied repeatedly to
the same pictures, so a decision has to be a thing you can change.

This design makes every human decision reversible and makes the committed
manifest the only thing that has to survive.

## Scope

In:

- Sheets rendered from stored status, showing accepted, rejected and undecided
  images together with their current decision.
- `promote()` as a state machine over all four transitions, including
  un-accepting, which moves a file back out of the tracked source tree.
- A folder-glob `--promote` that applies every verdicts file in one run.
- An index page over the twelve mood sheets.
- Best-effort in-browser persistence of unsaved marks.

Out:

- Any change to fetching, scoring, or the query vocabulary.
- A local review server. The sheets stay static files opened from disk.
- Committing `candidates/` or `sheets/`. Both stay git-ignored.

## The persistence model

`tools/assets/art_manifest.json` is the single source of truth for decisions. It
is tracked, it carries a status for every image, and it carries `image_url` and
`page_url` for every image, which makes lost pixels recoverable.

Everything else is a disposable cache:

| Artefact | Tracked | Survives a fresh clone | Rebuilt by |
|---|---|---|---|
| `art_manifest.json` | yes | yes | never — it *is* the state |
| `tools/assets/png/` | yes | yes | `--promote` |
| `tools/assets/candidates/` | no (`.gitignore:53`) | no | `--sheets --refetch` |
| `tools/assets/sheets/` | no (`.gitignore:55`) | no | `--sheets` |
| browser `localStorage` | no | no | discarded; manifest repaints |

The consequence that drives the rest of this design: on a fresh clone the
manifest knows image `182635` is rejected, but the file is gone, because rejected
images live only in the git-ignored `candidates/` tree. A decision must therefore
remain viewable and flippable with **no image file present and no network**.

`localStorage` is explicitly not part of this model. `file://` origin handling
differs between browsers — Chrome treats file URLs as opaque origins and may silo
or refuse storage — so it is best-effort scratch space for marks not yet
exported. Losing it must never cost a committed decision.

## Components

### `sheet(mood, records, ...)`

Renders every record for the mood whose status is `ACCEPTED`, `REJECTED` or
`CANDIDATE`. `METRIC_REJECTED` records are excluded and cannot be included: the
fetcher only ever writes gate-passing images to disk, so no file has ever existed
for them.

Image source depends on status, because promotion moves files:

- `ACCEPTED` → `tools/assets/png/<mood>/<donor>/<noun>/<id>.png`
- `REJECTED`, `CANDIDATE` → `tools/assets/candidates/<same path>`

Checkbox and label come from stored status:

| Status | Box | Label |
|---|---|---|
| `ACCEPTED` | checked | accepted |
| `CANDIDATE` | checked | undecided |
| `REJECTED` | unchecked | rejected |

`CANDIDATE` defaults to checked, preserving today's default-keep behaviour.

When the file is absent, the tile renders a labelled placeholder carrying a link
to `page_url` instead of an image. The checkbox stays live, so decisions are
flippable offline on a clone that has no pixels at all.

Each page carries a nav strip: back to index, previous mood, next mood.

Marks are persisted best-effort to `localStorage` as they are made, keyed by mood
and image id, so closing the tab mid-pass does not lose work. On load the page
paints stored manifest status first, then overlays any saved marks, so an
un-exported change is visibly distinct from a committed one. A **Clear marks**
button discards the overlay and reverts the page to manifest truth.

Storage is best-effort by design. If the browser refuses it — a real possibility
on `file://` — the page must still render and export correctly, losing only the
convenience of resuming.

### `index.html`

A dashboard over the twelve sheets: per-mood accepted / rejected / undecided
counts, progress against the per-mood target, and a link into each sheet. It
holds no decision state. Its purpose is to make a starved mood visible without
opening twelve files — SCIFI finished the first sitting with zero accepted out of
twenty-four, and nothing surfaced that.

### `--sheets --refetch`

Without the flag, `--sheets` performs no network access, exactly as today.

With it, a record whose local file is missing is re-downloaded from its
`image_url` before rendering. Every stage degrades: a failed download prints an
actionable line and falls back to the placeholder tile, and the rest of the run
continues.

### `dedup`

`--sheets` currently drops new candidates that near-match an accepted image's
perceptual hash. Once decided images appear in the sheet this must be scoped to
`CANDIDATE` records only. A decided image passes through untouched — dropping one
would remove the very tile needed to reverse its decision.

### `promote(verdicts, manifest, ...)`

Stops skipping non-candidates. Handles every transition, moving the file to match
the new status:

| From | To | File action |
|---|---|---|
| `CANDIDATE` | `ACCEPTED` | `candidates/` → `png/` |
| `CANDIDATE` | `REJECTED` | stays in `candidates/` |
| `REJECTED` | `ACCEPTED` | `candidates/` → `png/` |
| `ACCEPTED` | `REJECTED` | `png/` → `candidates/` |
| any | same | no-op |
| `METRIC_REJECTED` | any | skipped, no file exists |

`ACCEPTED` → `REJECTED` deletes a tracked file, surfacing in `git status` as a
deletion under `png/`. The file is moved rather than deleted so the decision can
be reversed again later without a re-fetch.

A missing source file is reported and skipped, never raised.

Returns per-mood gains and losses, so a run can report `+18 -2`.

### `--promote` with no argument

Globs `tools/assets/sheets/verdicts*.json` in sorted order and applies each in
turn. Browsers name repeat downloads `verdicts(1).json`, `verdicts(2).json` and
so on, which is what the folder already contains; the mapping from file to mood
is read from the manifest, never inferred from the filename.

`--promote <path>` keeps working unchanged.

## Testing

No network in any test. Every test stubs the HTTP layer.

Two properties carry the design and both must be mutation-verified — broken
deliberately, watched to fail, then restored:

1. **Idempotency.** Applying the same verdicts twice changes nothing the second
   time. This project has already shipped an idempotency test that passed for the
   wrong reason: the first `promote()` unlinked the source, so the second call's
   `src.exists()` check masked a missing status guard. The test must distinguish
   "blocked by the guard" from "blocked because the file was gone" by recreating
   the file before the second call.

2. **Manifest-only round-trip.** With a manifest and no image files whatsoever,
   `--sheets` renders placeholder tiles, a verdicts file flips a decision, and
   `--promote` records it. This is the fresh-clone case and it must work offline.

Also covered: each of the four transitions moves the file to the right tree;
`METRIC_REJECTED` records are never touched; `dedup` leaves a decided image in
place while still dropping a duplicate candidate; a missing source file degrades
rather than raising; folder-glob promote applies twelve files and maps each to
its mood via the manifest.

## Constraints

Inherited from `2026-08-06-room-art-pipeline.md` and binding here:

- Python 3.9 floor. No `match`, no PEP 604 unions, no runtime builtin generics.
- Every stage degrades, none aborts.
- Sheets stay self-contained: every `img src` is a `data:` URI, and the only
  external links are to `pixabay.com`. A Task 5 reviewer verified this property
  directly and it must survive.
- Comment style is mandatory. Module, function and constant docstrings carry
  Description, Author: suinevere, Dependencies, Globals, Params, Returns. Tests
  get a module docstring only. No comments inside function bodies.
- No API key in any file, fixture, or commit message.

## Repository size

Sheets stay one file per mood, embedding full-resolution PNG data-URIs. At
twenty-four images each that is ~3.4MB per page today, rising to roughly 14MB per
page at the ninety-nine-per-mood target.

That is workable but not comfortable. If it becomes a problem the fix is to embed
a JPEG preview of the same 320×224 frame rather than the canonical PNG — roughly
18KB against 107KB, visually indistinguishable for judging subject and framing,
with the PNG on disk still the source of truth. Deliberately not done now: it
trades exactness for size, and the size is not yet hurting.

## Open questions

None. The two decisions left to the repository owner by the previous design —
`display.c`'s in-function comments and `convert-backgrounds.sh`'s non-existent
offline fallback — are untouched by this work and remain open.
