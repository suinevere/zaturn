---
name: 2026-09-03-map-colours-coverage-session-handoff
description: "Session handoff for branch map-baggage-marks: 21 unpushed commits taking the in-game map from a one-colour drawing on four different papers to a per-sheet, per-seat one, and from 843 atlas rooms to 1,848 across 30 stories. Two owner-reported regressions were traced and fixed on the way. Nothing in any of it has been seen on a screen by me; every report so far came from the owner's hardware."
metadata:
  type: project
---

Branch `map-baggage-marks`, **21 commits unpushed**, `1c9d95f`..`cdafa30` on top
of 82dbbd2. Continues [[2026-09-03-map-inset-to-parchment-handoff]].

> STALE on the commit count and on what is unverified: the branch has since
> grown to 22 unpushed commits and six more owner reports were closed. See
> [[2026-09-04-map-paging-session-handoff]]. Everything else here stands.

## Where the detail is -- do not restate it here

Two live notes carry everything worth knowing, written as the work happened:

- [[2026-09-03-map-party-colours-handoff]] -- the map's colours, the borrowed
  palette slots, the label layout, and the marble strip that outstayed a switch
  to a real keyboard.
- [[2026-09-03-map-coverage-and-the-missing-graph-reveal]] -- why a tableless
  story showed no map, the measured spike, both shipped stages of the fix, and
  **both regressions with their root causes**.

The commit messages carry the reasoning per change; `git log 82dbbd2..HEAD` is
the readable index. Nothing below repeats any of it.

## State

Both Saturn targets compile and link. The ISO step fails at Error 127 for want
of `xorrisofs` on the git-bash PATH -- the documented environment gap. Host
tests: 174 python (`pytest saturn/tests`), and all five map C tests. The atlas
regenerates byte-identically on a second run.

`saturn/run_with_mednafen.bat` is modified in the working tree and is the
owner's, not mine. Left alone.

## What is NOT verified

**Nothing in these 21 commits has been on a screen in this session.** Every
preview was composited from the real tiles and the real palette arithmetic by a
scratch script. Both regressions this session were found by the owner on
hardware and neither was visible to any test -- see the note above for why.

The owner's last two reports were about The Lurking Horror specifically. The
second fix (`724f749`) is the one least confirmed: it changes what
`map_view.cxx` draws, and `map_view.cxx` cannot be linked on the host, so the
only check is the rule extracted into `map_layout.h`. **Ask for a screenshot of
Lurking Horror floor 7 before believing it is done.**

## Open, in rough order of value

1. **Look at the map on hardware.** Four sheets, a party, a shared room, a
   staircase off the edge. Everything else here is inference.
2. **The here-mark and the peer-mark are still tinted ramp entries** (12 and 15),
   so they bend toward the map's tan whatever sheet is under them -- faint on
   the white sheet, pale on the black one. Raised with the owner, never
   answered. They would take the same per-sheet treatment the location fill got
   in `4493818`.
3. **Deadline (83%) and Hypochondriac (25%) still ship no table.** One threshold
   line if a rough map beats none.
4. **`saturn/tests/test_hwram_budget.py` reads whichever config built last.**
   Build the CD target last before running the suite.

## Traps that cost time this session

- **Never build one Saturn config on top of the other.** `make NETBIN=1` after a
  CD build reuses the CD objects and fails at link with a spray of undefined
  references. `make clean-preserve-audio` between configs. Not a code fault.
- **`REPAIR_SPAN` is 3.** Two test fixtures were written displacing a room
  further than that and both times the fixture was wrong, not the code. There is
  a test pinning the bound now.
- **The heredoc backslash trap in [[bash-heredoc-eats-backslashes]] bit twice**
  on Python patch scripts containing `\` line continuations. Write the script to
  the scratchpad with the Write tool and run it by path.
- **There are five map host tests, not four** -- see
  [[zaturn-five-map-host-tests]]. `test_map_atlas.c` is the only one that reads
  the generated table and it caught two real breakages this session.

## Suggested skills

- **`superpowers:systematic-debugging`** for anything the owner reports off a
  screen. It earned its keep twice here: both regressions had a plausible wrong
  answer available immediately, and gathering evidence first killed two
  hypotheses before either was implemented. Take a report like "Renovated Cave
  North" literally -- that room has no north exit, and noticing so is what found
  the second bug.
- **`superpowers:brainstorming`** before any further map feature. The spike path
  is what produced the measurement that justified both shipped stages; going
  straight to code would have built the runtime graph-walk I first proposed and
  was wrong about.
- **`superpowers:test-driven-development`** for generator work specifically. The
  alignment baselines are the load-bearing guard in this area, and the right
  move when one fails is to split the measurement, not lower the number -- see
  what `a0350a9` did to `test_atlas_axis.py`.
- **`superpowers:verification-before-completion`** before telling the owner
  anything is fixed. Twice this session a claim of mine was corrected by the
  next screenshot.
