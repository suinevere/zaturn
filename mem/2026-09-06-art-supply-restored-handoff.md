---
name: art-supply-restored-handoff
description: The tracked art manifest had lost 1,867 of its 1,868 plates to a checkout and nobody had noticed because only the derived frames.json was ever committed; restored, built into 171 archives, and the whole supply is now on origin/main with 586 tests passing and Lurking Horror's heap floor clear at 41,752 bytes. The CI pipeline could not build any of it and reported success anyway; that is now closed by committing the archives rather than building them (see the CLOSED section).
metadata:
  type: project
---

Branch `art-v2`, level with `origin/main` at `28bfb8e`. Six commits went out one
at a time after a three-commit squash-and-cherry-pick onto a force-updated
`origin/main` (`b63eae6` -> `939f37d`); read `git log 939f37d..28bfb8e` for the
sequence. The working tree carries the owner's unstaged `SaturnRingLib` submodule
bump (`344c58c` -> `2cb7abf`, +136 upstream commits) and an untracked
`.~lock.controls.xls#`; neither is mine and neither was touched.

## What was actually wrong

The reported symptom was `tools/gen_art_archive.py` failing with *"bridge.png is
in the manifest but not in tools/assets/art"*. That was the least of it.

**`tools/assets/art/manifest.json` is tracked but was never committed with its
plates.** Commit `40cb396` shipped the derived `frames.json` describing 409
plates while leaving the manifest at the single `bridge.png` demo entry it was
introduced with. From then on every checkout silently reverted a working copy
that had them all -- and one did, at 06:44 on 5 September, hours after the
owner's 1,759-plate generation finished at 22:08 the night before. The
2026-08-16 genre-banded note had already recorded this exact failure mode ("a
tracked manifest lost the curation to a branch switch"); it happened again
because nothing held the two files to each other.

Recovery was reconstruction, not git: the manifest was rebuilt to 1,868 plates
from `frames.json` plus the two prompt sheets. The 409 already-placed plates come
first in their own order so no index between 75 and 483 moves and no room record
repoints; the other 1,459 append in sheet order, which is what `gen_art_source`
would have done. `drawn_from` was safe to recompute because `room_prompts.json`
(mtime 09-04 13:52) predates every drawn plate (14:49--22:08).

`load_manifest` demanded a raw generation while `styled`, `from_preview`,
`fingerprint` and `.gitignore` all say the raw is scratch and the styled plate
under `analysis/zork_bg/png` is the source of record -- so the documented
checkout-with-no-raw-generations case was impossible and `bridge.png`, which has
only ever existed here as a styled plate, was simply the first entry to hit it.

## Traps this session actually fell into

- **A `ProcessPoolExecutor` sized at `os.cpu_count()` locked the owner's machine
  up** and the run had to be killed at 450 of 1,868 records. `cgl_archive.default_jobs()`
  is now a third of the cores and `--jobs` overrides it. This is a daily-driver
  desktop; never take every core. Recorded in the user memory
  `user-runs-all-builds`.
- **The encode is not a hang, it is 106 minutes.** `cgl_encode.record` is a
  byte-at-a-time LZSS in Python at ~3.4s a picture. Parallel across 4 workers it
  is ~25 minutes, byte-identical (a record is a pure function of its own palette
  and pixels), and now prints `encoded N/1868` as it goes.
- **`gen_pool.py` silently dropped all fourteen measured scene defaults.**
  `GAME_PRES_ZORK1` became a byte-per-room slot index when the presentation table
  was pooled, but `scene_evidence` still parsed it as `{image, track, effects}`
  triples, matched nothing, and let every scene fall through to its analogue. The
  committed pool had kept them only because nobody had re-run the generator since
  the shape changed. An empty parse of either table is fatal now.
- **A byte-identical regeneration still moves an mtime.** `test_gen_presentation`
  rewrites `game_presentation.inc` to prove it is idempotent, which made the link
  map look older than a source that had not changed and skipped the whole HWRAM
  budget file -- including the Lurking Horror floor -- for the rest of that
  session. `tools/gen_emit.py:write_if_changed` is now how the generators write.
- **A stale link map is not a heap regression.** The map in `BuildDrop` measures
  the build that produced it and nothing else; the one being read was written 80
  minutes before `ea31947` took the story heap from 135,616 to 192,800.
  `heap_bytes` returns nothing when the map is older than `saturn/src`.

## Where it stands

`tools/gen_art_archive.py` builds 1,868 frames into 171 archives numbered
75..1942, written to both `tools/assets/BG` and `saturn/cd/data/BG`. The styled
plates are committed (1,943 PNGs, ~46 MB), which is the only reason a clean
checkout can rebuild any of this. `test.bat` runs the suite on Windows, macOS and
Linux through the same shell-label trick `compile.bat` uses.

Last full run on `28bfb8e` after the owner's `compile.bat`: **586 passed, 8
skipped, 0 failed.** Lurking Horror clears its floor -- heap 171,456,
`LURKING.Z3` 129,704, **41,752 free against a 16,384 floor**.

## CLOSED: the CI pipeline shipped a disc with no pictures

**Was:** all 1,931 room records name a picture of index >= 75, so every room
depends on a `GEN*.CGL`; neither workflow installed PIL or numpy, `bg.bat` ran
`gen_art_archive.py` softly (`|| echo "WARNING: generated art not built"`), and
the disc check iterated `BG_MANIFEST` -- the 11 originals plus `OITEM.CZ`, no
`GEN` among them. A CI disc missing all 171 archives passed every check it had.
Worse than the note knew: `release.yml`, the workflow that publishes, never ran
`bg.bat` at all, so the released disc carried no pictures of either kind.

**Fixed by not building them there.** The owner's call was to commit the
archives, and the third option on the list -- install Pillow, budget ~100 minutes
on a 2-core runner -- was never really available to `release.yml` anyway:
styling grades each plate against the ORIGINAL frame beside it, so a rebuild
needs `bg.bat`'s staged archives, and those come off a disc a public release
workflow has no business downloading. So `saturn/cd/data/BG/GEN*.CGL` is tracked
now (`saturn/.gitignore` negates the glob for `GEN` alone; the originals stay
out, being that disc's assets), the ordinary build bakes them into the base ISO,
and both workflows verify the built disc against the size and SHA-256 already in
`frames.json` -- stdlib only, no imaging stack. `saturn/tests/test_art_archives.py`
holds the committed bytes to the same record. Verified by running the release
check against a real built `.bin`: 171 archives present and byte-identical,
carrying 1,868 pictures.

**What that costs, and the one thing to watch.** +38 MB to a pack already at
454 MB, and another ~38 MB on every regeneration, permanently. Regenerating is
still a local job needing Pillow, numpy, the staged originals and ~25 minutes on
four workers. If the art churns often enough for that to hurt, the alternative
considered and not taken was publishing the archives as a pinned release asset
(`art-vN`) for CI to download, which keeps them out of the pack entirely.

**Two smaller things left open:**

- **~21 KB of story heap went somewhere.** `ea31947` recorded 192,800; it now
  measures 171,456. The obvious suspect is this session's supply growth
  (`IMAGE_FRAME` 483 -> 1,942 entries, `PRES_AREA` 65 -> 182), but it could not be
  confirmed from the map because those tables are `static const` folded into
  `.rodata` without their own symbols. The floor still clears by 25,368 bytes.
- **`pip install -e .[maps]` turns seven more tests on.** Six map-scan modules
  skip for want of OpenCV and pymupdf (they used to abort collection outright,
  which is why a bare `pytest` failed); the seventh is
  `test_exit_dests.py::test_gen_map_marks_decodes_the_same_destinations_for_every_shipped_story`,
  the third copy of the plen-4/5 conditional-destination rule, which should be on
  every run. The one remaining skip after that,
  `test_a_reference_too_dark_to_reach_the_target_is_left_at_full_lift`, is a
  self-arming conditional and correct as it is.
- **The 47 `saturn/tests/*.c` files have no runner** anyone has found. `test.bat`
  does not cover them.

## Suggested skills

- `superpowers:verification-before-completion` -- three separate claims this
  session ("the run succeeded", "the heap tests pass", "the archives are on the
  disc") were wrong until the output was actually read. The CI question above is
  the same shape and wants evidence, not reasoning.
- `superpowers:systematic-debugging` -- for the ~21 KB heap delta, which is a
  measurement question and not a code-reading one.
- `diagnosing-bugs` -- if the CI disc is built and rooms come back blank.
- `code-review` since `939f37d` -- the six commits were pushed one at a time and
  never reviewed as a set.

Related: [[lwram-sound-and-image-trim-handoff]] for the heap floors and the
192,800 figure, [[per-room-art-generation-handoff]] and
[[art-prompt-and-model-handoff]] for how the plates were drawn,
[[generated-room-art-pipeline-handoff]] for the CGL writer.
