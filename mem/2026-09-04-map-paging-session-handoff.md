---
name: 2026-09-04-map-paging-session-handoff
description: "Session handoff for branch map-baggage-marks: 22 unpushed commits closing six owner reports about the in-game map, four of which were the same fault -- the map's floor paging -- and none of which I checked until the fourth. Ends with L/R following the staircase out of the room under the crosshair instead of the page index, and with the test that should have caught all four. Nothing in any of it has been built for the Saturn target or seen on a screen."
metadata:
  type: project
---

Branch `map-baggage-marks`, **22 commits unpushed**, `86c9751`..`8a18540` on top
of `82dbbd2`. Continues [[2026-09-03-map-colours-coverage-session-handoff]],
which is now stale on its commit count and on nothing else.

## Where the detail is -- do not restate it here

One live note carries every root cause, every measurement and every thing I got
wrong, written as the work happened:

- [[2026-09-03-map-coverage-and-the-missing-graph-reveal]] -- read the sections
  from "The third regression" onward. This session appended six of them.

The commit messages carry the rest. `git log 86c9751..HEAD` is the change log;
they are long on purpose and none of it is repeated here.

## What this session was

Six owner reports off their own hardware, in order. **Four of the six were the
same fault** and I did not check for it until the fourth:

1. Concrete Box drew a dashed run southward for an exit the story says is north,
   and no U for its up exit. Two causes, one fact -- every one of its direction
   properties is a routine or a conditional. `c1d6b91`, `2d32f26`.
2. Terminal Room drew a bare **D** for `out`. `IN` and `OUT` sat past `RM_UP` in
   the direction enum and the letter came off the parity of the index.
   `782ab50`.
3. Cursor should not reset on a floor change; connected rooms should share a
   coordinate. `40fc3c3`, `a50cddb` -- and `f86e7dd`, which fixed the collision
   rule those two broke and a latent fault older than both.
4. Third Floor and Computer Center not showing. `84674f6`, `f86e33e`.
5. A stray U by the Roof; U and D wanted at the rose's own positions.
   `bee2806`. Plus `20e9c8e`, backing out a vote exclusion I had shipped one
   commit earlier.
6. **"Are you even checking the map after you make a change?"** No -- see below.
   `32d348b`, `fbfbcc4`.

## The thing to carry forward

**Every fix in this session was verified against what I had just changed, and
not against what was being reported.** Reports 3, 4, 5 and 6 were all about
which floor L and R reach. I checked room coordinates, and which room the
crosshair landed on, and never once what paging actually walked through. The
first thing built in the last round was that check, and it said immediately that
15 of the disc's staircases run backwards in page order.

Build the check for the REPORT first. Run it before the fix. Let it say whether
the fix was needed.

## Open, and the reasons

- **Nothing has been built for the Saturn target or seen on a screen by me.**
  Six rounds of reports, all from the owner's hardware. `map_view.cxx` cannot be
  host-linked, which is why every decision found this way keeps being moved into
  `map_layout.h` or `map_model.c`.
- **`map_edges_stub` has no caller.** The U/D letter used to be paired with a
  stub; at a fixed cell that would be a second mark on one exit. Left in place
  deliberately -- if the bare letter reads badly on a television it comes
  straight back, and that is a call from a screenshot.
- **Six objects in the tables are not rooms** -- Lurking's 49, Zork I's 82, Zork
  II's 230, Wishbringer's 34, Spellbreaker's 41, Stationfall's 40 ("it"). A
  nameless object whose only exit leads to itself. Lurking's floor 0 pages to a
  crosshair sitting on one. `build_game` already excludes them from the name set
  (see its comment); the fill does not. Needs a rule better than "the name is
  empty".
- **Terminal Room's storey.** Infocom's drawing puts it on the Third Floor's
  row; the compiled story says `Second Floor --north--> Terminal Room`, a plain
  compass exit. The owner chose the story. The evidence and the option not taken
  are in the live note.
- **`saturn/run_with_mednafen.bat` was already modified** when this session
  started and was left alone.

## Traps that cost time here

- **Quoted heredocs eat one level of backslash.** Every patch script with a
  `\n` in it must be written to the scratchpad with the Write tool and run by
  path. This bit twice. Already recorded as a global memory.
- **Every source file in this repo is CRLF.** Read as bytes, normalise, patch,
  write back with `\r\n`. A patch that writes LF makes git rewrite the file.
- **Regenerating the atlas needs the right base.** `carried()` reads every cell
  as an anchor, so `--walk --merge` must run against the last measured-only
  `.inc`, never the shipped one. A re-scan needs `--cache tools/assets/cache`
  (gitignored, already populated, and pymupdf/opencv/rapidocr are installed).
  Full recipe in the live note.
- **A scan is reproducible.** With a change reverted it reproduces `82dbbd2`
  byte for byte bar one later header paragraph. That is the only way to tell a
  rule's effect from OCR noise -- establish it before measuring anything.
- **Don't trust a prototype that drops a term.** The layering prototype ignored
  `sheet` and predicted 15 backwards staircases becoming 0; the real generator
  gave 9, because the key is `(sheet, height)` and the sheet sorts first.

## How to check the work

```
# five map host tests
gcc -O2 -I saturn/src -o /tmp/t saturn/tests/test_map_model.c \
    saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c \
    saturn/src/engine/map_marks.c && /tmp/t
# (test_map_atlas.c, test_map_edges.c, test_map_layout.c, test_map_marks.c
#  each carry their own Build: line)

python -m pytest saturn/tests tools/tests tests -q
```

449 passed, 3 skipped (`test_hwram_budget.py`, needs a build to measure the
heap). All five C tests pass. `saturn/tests/test_atlas_stairs.py` is new and is
the one that scores the shipped table against the stories' own staircases.

## Suggested skills for the next session

- **`superpowers:systematic-debugging`** for any further owner report. Phase 1
  is the whole lesson above: reproduce the REPORTED symptom before touching
  anything. This session's failures were all skipped Phase 1.
- **`superpowers:verification-before-completion`** before telling the owner
  anything is fixed. Six rounds of "fixed" that were not.
- **`superpowers:test-driven-development`** -- every fix here that stuck was
  written failing-test-first against `test_map_model.c`, `test_map_layout.c` or
  `test_atlas_merge.py`.
- **`run`** if a Saturn build can be made to happen. It is the one thing that
  would end the loop of owner-reported-only verification.
- Not `brainstorming` -- the design questions are settled; what is left is
  hardware verification and the open items above.
