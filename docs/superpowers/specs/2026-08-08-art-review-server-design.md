# Art Review — Local Review Server — Design

**Date:** 2026-08-08
**Status:** Proposed
**Follows:** `2026-08-08-art-review-persistence-design.md`

## Goal

Curation is now the slow part of the pipeline. A round fetches 480 candidates
across twelve moods, and reviewing them means opening twelve static pages
averaging 8.5MB — because every tile embeds its picture as a base64 data URI —
then downloading twelve `verdicts.json` files, moving them into
`tools/assets/sheets/`, and running a promote. At the 99-per-mood target those
pages reach roughly 25MB each, which no browser opens comfortably.

A local server removes both problems at once rather than managing them. Images
served as files are not embedded at all, so page weight stops scaling with pool
size. A click can apply its verdict immediately, so the download-move-promote
round trip disappears.

This reverses an explicit "out" in the previous design, which ruled a review
server out of scope and kept the sheets static. That was the right call when a
sitting was 288 candidates reviewed once. It is the wrong one now.

## Scope

In:

- A Flask app on `localhost:8080` that lists moods, shows a mood's pictures
  grouped by sub-category, and records a verdict per picture.
- Filtering by stored status, progress against the per-mood target, an enlarged
  preview, and keyboard verdicts.
- Retiring the static sheet renderer and the verdicts-file round trip.

Out:

- Any change to fetching, scoring, the query vocabulary, or `promote()`'s
  transition rules.
- Authentication, multi-user access, or binding to anything but loopback. This
  is a single-operator tool on one machine.
- Editing `art_queries.json` through the UI. Vocabulary tuning stays a text edit.

## The manifest stays the only truth

`tools/assets/art_manifest.json` remains the sole authority for decisions, and
`promote()` remains the only writer of status and the only mover of files. The
server is a view and a controller over them, never a second store.

This matters because `promote()` already encodes the four transitions, the
`png/` ↔ `candidates/` moves, the reconciliation rule ("move a file only when
the destination is missing"), and idempotency — all mutation-verified. A server
that wrote statuses itself would duplicate that logic and drift from it.

## Components

### `tools/art_server.py`

The whole server. One module, because the routes, the grouping and the template
are one concern and splitting them would separate a route from the shape it
renders.

Depends on `fetch_art.load_manifest` / `save_manifest` and `art_review.promote`.
It adds no new persistence and no new status vocabulary; `art_status` constants
remain the only spelling of a status.

### Routes

| Route | Does |
|---|---|
| `GET /` | Index: every mood with accepted / rejected / undecided counts and progress against its target. The target is that mood's `target` field in `tools/assets/art_queries.json` — 99 for all twelve today — read through `art_queries.load`, never hardcoded, so retuning the vocabulary retunes the goal. |
| `GET /mood/<MOOD>` | One mood's pictures, grouped by `DONOR / noun`, each group headed with its own counts. Accepts `?status=undecided\|accepted\|rejected\|all`, defaulting to `undecided`. |
| `POST /verdict` | Body `{"id": "...", "verdict": "accept"\|"reject"}`. Applies it through `promote()`, saves the manifest, and returns the record's new status plus that mood's and group's refreshed counts. |
| `GET /image/<id>` | Serves the picture from `png/` or `candidates/`, whichever holds it. |

`GET /mood/<MOOD>` for an unknown mood, and `GET /image/<id>` for an unknown id,
both answer 404 rather than raising.

### Interaction

A verdict applies the moment it is clicked — no staging, no save button, nothing
lost if the browser closes. Clicking the opposite verdict reverses it, which is
safe because `ACCEPTED → REJECTED` moves the file back to `candidates/` rather
than deleting it.

An untouched picture stays undecided. The server has no submit moment, so
nothing is ever implicitly rejected — this differs deliberately from the static
sheets, where downloading a mood's verdicts rejected everything left unticked.

The default filter is `undecided`, so resuming a half-finished pass shows only
what is left rather than everything already decided.

Clicking the picture itself — as opposed to its verdict controls — enlarges it,
for judging framing before deciding.

Keyboard: `A` accepts the focused picture, `R` rejects it, arrow keys move
focus, and both verdict keys advance focus to the next undecided picture so a
long pass needs no mouse.

### `tools/requirements-review.txt`

Flask goes here, not in `tools/requirements.txt`. That file is installed into
the build venv by `convert-backgrounds.sh` on every build, and the build has no
use for a web server; adding Flask there would pull it and its dependencies into
every clean checkout.

A missing Flask prints the install line and exits 0, matching the
everything-degrades rule the rest of the tools follow.

## What this retires

- `art_review.sheet()`, `art_review.index_page()`, and the `--sheets` subcommand.
- The *browser* round trip: nothing generates a verdicts file any more, and the
  no-argument folder-glob form of `--promote` that existed to sweep twelve of
  them out of `tools/assets/sheets/` goes with it.
- The tests covering those, replaced by route tests.

`--promote <path>` survives, and so does the verdicts file *format* it reads —
one JSON object of id to `"accept"`/`"reject"`. Nothing produces such a file
automatically now; it remains for scripted or recovery use, where an operator
writes one by hand.

`promote()`, `dedup()`, `refetch_missing()`, `art_status`, and the manifest model
are untouched.

### The build step goes too

`convert-backgrounds.sh` currently runs `art_review.py --promote` before
converting, so a verdicts file downloaded but never applied could not survive to
the disc. With the server applying verdicts on click, that failure mode stops
existing and the step would run on every build only to find nothing. It is
removed. `pre.makefile`'s message reverts to naming conversion alone.

## Error handling

Every stage degrades, none aborts — the rule the rest of the pipeline follows.

- Flask absent: print the install command, exit 0.
- Port 8080 already bound: report it and exit 0, naming the port so the operator
  can free it. Do not silently pick another port; the operator has that URL open.
- A record whose picture is missing from both trees: the tile renders a
  placeholder linking to its Pixabay page, and its verdict controls still work.
  This is the fresh-clone case, where `candidates/` does not exist at all.
- `promote()` reporting no file to move: the status change is still recorded and
  the response says so, because the manifest is the decision and the file
  location follows it.

## Testing

Flask's test client throughout. No live server, no sockets, no network.

Covered:

- Each route's success shape, and 404 for an unknown mood and an unknown id.
- All four verdict transitions driven through `POST /verdict`, asserting both
  the manifest status and the file's tree afterwards.
- Applying the same verdict twice is a no-op the second time.
- The status filter returns exactly the records with that status, and `all`
  returns every non-`METRIC_REJECTED` record for the mood.
- Grouping: one heading per `DONOR / noun`, sorted, counts correct, and no
  record lost — the set of ids rendered equals the set for that mood.
- A record with no picture on disk renders a placeholder and still accepts a
  verdict.
- `METRIC_REJECTED` records never appear. They have no file and never have.

Every test that pins a guard must be mutation-verified: break the guard, watch
the test fail, restore. This project has shipped nine tests that passed while the
thing they protected was broken, three of them in the last two days.

## Constraints

Inherited and binding:

- Python 3.9 floor. No `match`, no PEP 604 unions, no runtime builtin generics.
- Every stage degrades, none aborts.
- Module, function and constant docstrings carry Description, Author: suinevere,
  Dependencies, Globals, Params, Returns. Tests get a module docstring only. No
  comments inside function bodies.
- `art_status` constants only; no bare status strings.
- No API key in any file, fixture, or commit message.
- Bind to `0.0.0.0`. **Changed 2026-08-08**, after the owner asked to review from
  a second machine: loopback-only made the host unreachable by name no matter
  what local DNS resolved, because nothing was listening on the LAN interface.
  The exposure is deliberate and its consequence is real — there is no
  authentication and `POST /verdict` moves and deletes files inside the
  repository, so anyone who can reach the port can re-curate the pool. Acceptable
  only on a trusted network; revisit if this ever runs anywhere else.

## Open questions

None.
