---
name: multigame-atlas-handoff
description: The authored map table taken from Zork I alone to eighteen of the disc's games and 843 rooms by automating the labelling with OCR, plus the four games still unmapped and why; two commits on main, the reveal switch still on and nothing yet compiled for the target.
metadata:
  type: project
---

Commits `3ded45e` (thirteen games) and `276b708` (eighteen). **Unpushed** at the time of
writing, along with the owner's own boxart commits.

Continues [[map-atlas-handoff]], which still holds for the engine side -- `map_atlas` /
`map_model` / `map_view`, the reveal switch, the netbin's 448 bytes. See "What that handoff
now gets wrong" at the end. This file is only about the table's contents and the tool that
produces them.

## Where it stands

**Eighteen of the disc's thirty-one games, 843 rooms, about 3.4 KB.** The per-game numbers
are in `saturn/src/engine/map_atlas_data.inc`'s own generated headers -- each table records
its release, serial, room count and how many of its exits agree with the drawing -- so they
are not repeated here.

Four games have a map on the site and do not pass: **Starcross** (80%), **Planetfall**,
**Deadline**, **Suspect**. Nine have no map at all: Advent, Hitchhiker's, Hypochondriac,
both Infocom samplers, the three Mini-Zorks, Seastalker. All thirteen keep the graph walk.

## The one thing that is not written down anywhere durable

**The tool now needs an OCR engine, and nothing installs it.** `pip install
rapidocr-onnxruntime` -- ONNX, about 50 MB, no PyTorch. It is named only in
`tools/gen_map_atlas.py`'s docstring; this repo has no requirements file to add it to, and a
fresh clone will fail on the import. The other three (pymupdf, opencv-python, numpy) were
already in use.

## Judgement calls the owner has not made yet

Both were put to the owner and neither has been answered. Do not quietly decide either.

1. **Is 86-88% good enough to ship?** Zork III and Sorcerer sit just over the 85% gate, on
   large samples (56 and 140 exits).
2. **Should Moonmist and Witness ship on four and eight exits of evidence?** Both read 100%,
   but a perfect score over four exits says much less than Zork I's 95% over 141. If a room
   looks misplaced in play, those two are where to look first.

The gate itself is `PASS_RATE` in the generator. Lowering it to admit Starcross would be the
wrong move -- its failures are wrong room *identities*, which put rooms permanently in wrong
cells.

## What the maps turned out to be

Two of the five games that looked like floor plans were not.

* **Starcross** is the ordinary box-and-line map drawn in negative -- white rules on black,
  because it is set aboard a starship. A detector looking only for dark ink found nothing on
  any page; inverted it finds 139, 20 and 60. Both polarities are now tried.
* **Moonmist** was detected correctly all along. It failed a gate demanding half a page's
  boxes resolve to a room name, which its scan cannot meet (`riveuay` for driveway, `itchen`
  for kitchen) even though nineteen rooms is ample.
* **Suspended** and **The Lurking Horror** are genuine architectural plans: rooms are
  wall-delimited areas labelled in bare text. Page-wide OCR replaces box detection there and
  feeds the same lane-snapping, assignment and validation unchanged.
* **The Witness** is drawn square to the building rather than to the compass. Every one of
  its testable exits failed *as half of a reciprocal pair*, which is a turned map rather than
  misplaced rooms. A quarter turn gives 8 of 8.

## Reconnaissance worth not repeating

**The door glyphs and dashed lines on these maps are not needed.** The owner offered them,
and it is worth recording why they were declined rather than leaving it to be re-proposed:
adjacency never comes from the drawing. Exits come from the story file, which is ground truth
and already knows which rooms connect and in which direction. The map is only ever asked
*where a room sits*. Reading connectivity off a scan could only add error where there is
currently none.

**OCR is a closed-vocabulary problem here, not open reading.** The answer is always one of
about a hundred names the story itself supplies, so a nearest match over that list survives
scans that exact matching cannot -- Zork II's `Insldo the tarrow`, Ballyhoo's `Bupupls Rooml
Ony`. Validating the matcher on Zork I alone hid this: its page 3 happens to scan perfectly
and read 21 of 21, which made exact matching look sufficient while it silently dropped nine
games.

**Starcross's failure is duplicate names, and two fixes did not work.** Five Red Halls, five
Green Halls, six Outskirts of Village, and only a handful of uniquely-named rooms to anchor
them. Refining each ambiguous group against the finished layout rather than against
whatever happened to be placed first moved it 77% to 80% and is kept. Splitting its two ship
regions at the gutter into separate coordinate spaces did nothing and was removed rather than
left in as speculative machinery.

## Three bugs found, and how

Each was found by disbelieving a plausible-looking result rather than by a test failing.

* **Blank boxes matched a real room.** Zork I's object 82 has an empty short name and one
  exit pointing at itself. It was in the name set, so every box whose OCR read nothing
  matched it -- which passed two of Zork I's non-map pages through the page gate. Caught by
  noticing the reported page list said 1,2,3,4,5 when only 3,4,5 are maps.
* **Labels merged across walls.** The fragment merger joined anything within a line and a
  half vertically, which in a dense plan reaches into the next room. The Witness returned one
  label reading `TUB Room BatHROOM ToIL` where the game has three rooms. Merging too eagerly
  does not misname a room, it deletes the ones it absorbs -- this was costing Suspended seven
  rooms and The Lurking Horror nine, in games that were already passing and so flagged
  nothing.
* **A vacuous regression check.** Comparing the automated Zork I output to the twenty
  hand-verified rooms by absolute coordinate would have compared nothing, since the origin
  moves. It is done by pairwise ordering instead: 380 of 380 preserved, re-run after every
  change to the matcher.

## What no gate could prove

Unchanged from [[map-atlas-handoff]] and worth restating because two sessions have now added
to it: **nothing on this branch has been compiled for the SH-2, linked, or run.** The gates
are host `gcc` over the SRL-free halves and `-fsyntax-only` over the rest.

Specific to this work: whether 843 rooms of authored geography actually read on a screen, and
whether the eighteen tables' room names -- which the generator now writes into the `.inc` as
trailing comments -- are right for the games nobody has played to check.

## Do not commit the map cache

The PDFs are Activision's, scanned by the Infocom Documentation Project
(infodoc.plover.net/maps/) and reproduced there with permission. `tools/gen_map_atlas.py`
downloads them into a `--cache` directory outside the tree and emits only coordinates. The
generator's docstring says this; keep it true. What ships is derived geography validated
against each game's own data, not the drawings.

The previous handoff flagged an untracked `zork1.pdf` in the repo root as a hazard because it
was the generator's only input. That is now moot -- the tool fetches its own copies -- and
the loose file can go.

## What that handoff now gets wrong

[[map-atlas-handoff]] is **PARTLY STALE**. Its engine-side content, the art measurements, the
dictionary byte offset and the outstanding items all stand. These do not:

- "Only pages 3 and 5 of the PDF were examined in depth; only page 3 is in the atlas" --
  all pages of eighteen games are now read, and page selection is automatic.
- "`zork1.pdf` is untracked" as a hazard -- moot, see above.
- "`test_scene_map.c` does not compile" -- gone; `origin/main` removed the scene system.
- Every room count it gives for Zork I (twenty) -- now eighty-four.

## Suggested skills

- **superpowers:verification-before-completion** -- the governing risk, and it has grown:
  843 rooms of geography, none of it seen on a screen, all of it passing host gates that
  cannot see a screen. Three of this session's defects produced perfectly plausible passing
  output.
- **superpowers:systematic-debugging** -- the open questions are all "does this look right in
  play", which arrives as an observation rather than a stack trace, exactly as the Witness
  rotation and the missing links did.
- **code-review** -- before pushing. Four commits are unpushed and the generator was rewritten
  twice in one session.
