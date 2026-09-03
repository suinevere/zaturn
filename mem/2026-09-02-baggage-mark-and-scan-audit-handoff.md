---
name: baggage-mark-and-scan-audit-handoff
description: Infocom's fifth and last legend mark -- the baggage limit -- now ships for Zork I, read off the scanned maps by a line tracer calibrated against the ZIL, and a whole-network audit says the scan is owed nothing further; eighteen commits on an unmerged branch, both Saturn targets linking, and still nothing seen on a screen.
metadata:
  type: project
---

Branch `map-baggage-marks`, **eighteen commits, unmerged**, forked from `origin/main`
at `1bfbc72`. Only `saturn/boxart/RAW_ZORK.xcf` is uncommitted; no commit here
touches it. Note local `main` is stale (2 ahead, 8 behind `origin/main`) -- the
base for this work is `origin/main`, not the local branch.

Two specs and a plan carry the design; do not restate them.

- `docs/superpowers/specs/2026-09-02-map-baggage-marks-and-scan-audit-design.md`
- `docs/superpowers/plans/2026-09-02-map-baggage-marks-and-scan-audit.md`
- `docs/ZORK1_MAP_SCAN_AUDIT.md` -- the second deliverable, and inert by design.

The commit messages are the durable record of why each thing is as it is.
`git log 1bfbc72..HEAD` is the honest place to start.

## What shipped

The fifth and last of Infocom's legend symbols, **"narrow passageway (baggage
limit)"** -- three bars struck through the line. It cannot be derived: what a
passage is conditional *on* appears nowhere in a compiled story. So a Python
pipeline reads it off Infocom's own scanned maps, reconciles it against the exit
graph, and ships six rows for Zork I in `saturn/src/engine/map_marks_data.inc`:

    Studio (94)  UP    dest=203  BAGGAGE   the graph had dest 0
    Kitchen(203) DOWN  ----      RETRACT   the drawing shows no descent
    Altar  (212) DOWN  ----      BAGGAGE
    Timber (206) WEST  ----      BAGGAGE
    Drafty (228) EAST  ----      BAGGAGE
    Drafty (228) OUT   ----      BAGGAGE

**The chimney was drawn backwards before this.** Studio's UP is a routine exit
whose destination decodes as 0 and was dropped; the Kitchen's DOWN survived and
drew a one-way arrow at the single direction the game refuses.

## The rule everything turns on

> The scan may resolve a passage only when **every** exit on it is
> `RM_EXIT_MAYBE`. If any exit is `OPEN` or `BLOCKED`, the graph has asserted
> something and wins outright, and the disagreement is reported, not applied.

As of the final fix wave this has **one implementation** (`_veto` in
`gen_map_marks.py`) and, for the first time, an **enforcement in the runtime** --
`record_exits` previously applied a retraction without consulting the exit it was
clearing.

## The audit's answer, which is a null result

Seeded from every box edge: 61 distinct pairs, 51 agree, 4 drawn-only, 24
graph-only, 0 direction-differs. **Not one drawn-only pair is eligible** -- three
are vetoed by an asserted exit and one is unjoined -- so over Zork I's whole drawn
network the rule permits the scan to add nothing beyond the three passages already
shipped. The scan is not sitting on unclaimed corrections.

All three shipped passages reappear as agreeing rows, found independently of the
cluster seeding. The document states its own recall honestly: 60 rooms traced
against the atlas's 84, 36% of seeds resolving to nothing, and it refuses to
present the zero direction-differs count as a pass rather than as a check that
barely ran.

## What has not been done

- **Nothing has been on a screen.** Both Saturn targets link (CD `EXIT=0`,
  852,372-byte ELF; netbin `EXIT=0`, 200,464 bytes) and 158 host tests pass, but no
  emulator is drivable here. The bars are **one pixel wide -- the thinnest ink in
  the whole tile set** -- on a parchment ground. The spec's own advice stands: look
  at the four existing marks on a television before trusting a fifth.
- **The bars cannot be thickened alone.** `test_dash_tiles.c` counts lit pixels in
  row 1 as the bar count, so `assert(lit == 3)` breaks if a bar becomes 2px wide.
  Artwork and test must change together.

## Round two, with the measurements already taken

- **`mapscan._boxes_at`'s ink threshold is the single blocking constant for
  non-scanned sources.** It thresholds at `gray < 120`, tuned for maps printed over
  stonework. A Trizbort export of Hitchhiker's has its 1st percentile at 121 --
  almost nothing clears it. Sweeping: 120 gives 7 boxes, 200 gives 38, and at 200
  **23 of 28 OCR'd names fuzzy-match the story's 27 rooms.** The same constant
  blocks Seastalker's blueprint. Measuring it per page, as Task 3 measured
  `SHAFT_MIN`, reaches both.
- **Raster sources must be wrapped at 1 pixel = 1 point.** `convert_to_pdf()`
  preserves physical size, so a 600-DPI JPEG became a 188x231pt page and the
  200-DPI render sampled a third of its detail: 0 boxes. Re-wrapped at pixel scale:
  19.
- **Four games have a map already cached but were dropped by the `PASS_RATE=0.85`
  gate**, needing no new art: `PLNTFALL` (104 rooms) and `DEADLINE` (50) fail with
  "no page yielded named boxes"; `SUSPECT` (56) with `0/0 exits agree`; `STARCROS`
  (85) at 80%, six exits short. Suspect and Deadline are architectural floor plans,
  which `read_labels` already exists for. Starcross's white-on-black scan is *not*
  the problem -- `find_boxes` already tries both polarities and found 124 testable
  exits -- but `trace_edges.ink_mask` is single-polarity and would find nothing on
  it, so Starcross can get an atlas but no marks until that is fixed.
- **Nine stories have no map PDF at all** (500 rooms): ADVENT 126, INFOSAM5 75,
  MZORKI2 69, MZORKI 66, MZORKII 47, INFOSAM7 44, SEASTLKR 30, HITCHHKR 27,
  HYPOCOND 16. The three Mini-Zorks (182 rooms) are subsets of Zork I/II geography
  and may be reachable by name-matching the existing tables rather than new art.
  Owner-supplied sources are cached under `tools/assets/cache/alt/` (gitignored):
  the mocagh Planetfall PDF reads 43-44 boxes on pages 3-4 with recognisable room
  names, and is the strongest single lead.

## Parked, with reasons

- `follow`'s gap-jump could chain through letterform gaps. Now bounded at 3, chosen
  against an instrumented scan whose longest real chain is **1**.
- `_veto` is a denylist where the old check was an allowlist -- identical on today's
  three exit kinds, but **fail-open** on a hypothetical fourth.
- `tools/tests/test_zil_exits.py:15` still builds its ZIL path relative to the
  working directory. It fails loudly rather than skipping, unlike the three files
  that were anchored.
- `compile-netbin.bat` writes its `.elf`/`.map` under the same `BuildDrop` names as
  `compile.bat`, so **build order is load-bearing** -- the CD build must run last.
  Pre-existing, not caused here.

## What this round taught about the process

Every defect found was in the **plan**, not the implementations: an oscillating
stroke-walker, a test helper that did not exist, two wrong build commands (one of
which would have produced a *false pass* on the suite guarding exit
classification), and an assertion that was vacuously true in the very test meant to
prove a baggage run is not dashed. Two more were in the controller's own rulings.
Each was caught by an implementer reading the real code or real pixels instead of
trusting the brief.

A Minor from one review became the round's most expensive near-miss: `arrow_end`
was flagged as "validated against exactly one arrowhead", and when exposed to four
it **read the wrong end on two**, which would have shipped three of six marks
wrong.

## Related

[[map-passage-marks-and-exit-destinations-handoff]] is the immediate predecessor
and describes the four marks this completes. [[map-floors-crosshair-and-party-handoff]],
[[map-atlas-handoff]] and [[multigame-atlas-handoff]] cover the map screen, the
authored table and its eighteen-game reach.
