---
name: per-room-art-generation-handoff
description: Every room of the thirty non-Zork games is getting a picture drawn from its own title and prose, one per room with none shared; the byte-wide picture index, the 26-archive stem ceiling and three separate causes of people appearing in the art are all fixed, brightness now aims at the disc's own 24 rather than 40, and archives are resident again but ordered by a depth-first walk of each game's exit graph, which beats every other key tried. 625 of 1,931 rooms are drawn; the owner runs the generator, not the agent.
metadata:
  type: project
---

## Where this got to

Twenty commits, `9bfc0a6` through `06a3c08`, on `input-dashboard`. `git log
6673a78..HEAD` is the narrative and the reasoning is in the messages; this
records only what a diff cannot say.

**The owner runs the long jobs. The agent does not.** This was said explicitly
and violated twice — a background `gen_art_archive` was started after the
instruction, and earlier a stale one was left in flight. See Traps.

## State on disk right now

- `tools/assets/art/manifest.json` — **734 plates**, of which **625 are
  room-specific** out of 1,931 (32%). 736 raw generations on disk.
- **Styled plates and generated archives are deliberately flushed.**
  `git status --short analysis/zork_bg/png` shows **109 deletions** — committed
  scene plates removed so they rebuild at the new brightness. Recover with
  `gen_art_archive.py`, *not* `git checkout`, which restores the old grade.
- `saturn/tests/test_lwram_budget.py::test_every_frame_lies_inside_its_archive`
  is **red** until the rebuild runs. That is the flush, not a defect.
- Forge is up on `127.0.0.1:7860`, GPU verified working (torch 2.13.0+cu126,
  sm_61 present, 1070 Ti). Nothing is generating.

## The command sequence

```
tools\.venv\Scripts\python.exe tools\gen_art_source.py --sheet tools\assets\art\room_prompts.json
tools\.venv\Scripts\python.exe tools\gen_art_archive.py
tools\.venv\Scripts\python.exe tools\assign_room_art.py
tools\.venv\Scripts\python.exe tools\gen_presentation.py
tools\.venv\Scripts\python.exe tools\gen_pool.py
```

Step 1 resumes from the manifest and checkpoints every 25. Step 2 must not run
while step 1 is in flight. Step 3 refuses to write while any room lacks a plate.
Between 2 and 3, `tools/check_plates.py` finds people — it needs Forge's venv
for torch, not `tools/.venv`.

## Findings that are not in a commit message

- **The disc is authored darker than anything readable.** Its 74 frames run
  7.4 to 74.2 mean luminance, mean-of-means 18.8, median 14.3. `TARGET_MEAN`
  is 24 in `room_art_style.py` — inside that range, and where the plate the
  owner approved (`advent_201`) already sat. Three earlier rounds of "too dark"
  were a tone-match bug plus a reference near the disc's floor, not the target.
- **Looking at plates does not work.** 109 were checked by eye; the owner then
  found two more that had been looked straight at. `check_plates.py` is the
  answer, but its false positives are terrain read as a body scoring 0.8+ while
  the real figure was 8x15 px — which is why it rejects on **box size**, not
  confidence. Roughly 2% of plates flag; expect ~40 to eyeball at full scale.
- **No automatic check for text/watermarks exists.** The 6% edge crop catches
  the signatures seen so far because both sat in the bottom 4.9%, but nothing
  detects one elsewhere in frame. This is the largest remaining quality gap.
- **Agent-launched long processes get reaped here** within a minute or two,
  cleanly, with no error. Forge and the review server both died this way
  repeatedly. This is why the owner runs them.
- The review server is `start_review_server.bat`, port 8080; `/g/<STEM>` for a
  game, `/g/<STEM>/<obj>` for one room's picker.

## Traps

- **A stale `gen_art_archive` will certify old work as current.** One finished
  after a deliberate flush, reported "409 unchanged" from its fingerprint cache,
  and rewrote previews at the *old* brightness plus a `frames.json` from a
  559-plate snapshot against a 734-plate manifest. Never leave one in flight
  while changing styling or the manifest.
- **Manifest order is frame index.** A room record stores the index, so plates
  may be appended but never reordered or removed. Packing order is free —
  `gen_art_archive` packs in walk order and scatters placements back.
- **`presentation.h` carries hand-copied counts** that must equal the `.inc`'s.
  `saturn/tests/test_presentation_counts.py` catches drift; they change on
  every art addition.
- **Do not wipe `analysis/`.** It holds ~170 tracked files of RE tooling
  including `zork_cgl.py`, which the whole art pipeline imports. Only
  `analysis/zork_bg/png/<plate>.png` is derived art; the 75 `B*_NN.png` beside
  them are the disc's own frames and every plate is graded against one.
- **Patch scripts with regexes through a bash heredoc mangle escapes.** This
  happened three times: `\b` became a literal backspace inside a test, `\n`
  became a real newline inside a string. Use the Edit tool for anything
  containing backslashes.
- Run everything with `tools/.venv/Scripts/python.exe`, except `check_plates.py`
  which needs Forge's venv.

## What is not done

- 1,306 rooms still undrawn (~2 hours GPU).
- Nothing has been built or run on hardware. `room_art.cxx` passes SH-2
  `-fsyntax-only` only.
- `music_pause`/`music_resume` around the archive read is identified and
  untried — see `c87ef7f`. Whether the two extra drive commands are worth it is
  a question about a real drive.
- Per-game packing would remove archive crossings almost entirely but a game
  needs ~425 KB against a 256 KB cap; unresolved, see `6f161ee` for the
  measurements.

## Suggested skills

- **`superpowers:verification-before-completion`** — the two worst moments this
  session were both claims that looked obviously right: a detector whose top
  result was a lunar crater, and a rebuild that reported success while writing
  the wrong brightness. Both were caught only by checking disk state.
- **`superpowers:systematic-debugging`** — the people-in-plates problem had
  three independent causes and fixing one would have looked like failure.
- **`superpowers:test-driven-development`** — the margin crop and the tone
  match both have properties cheap to state and expensive to get wrong quietly;
  each is now paired with a test that fails when the fix is removed.
- Not `brainstorming` — the remaining work is a command sequence.

## Related

[[generated-room-art-pipeline-handoff]], [[zork1-authentic-presentation-handoff]],
[[scene-tagged-art-handoff]], [[genre-banded-art-handoff]],
[[room-art-pipeline-handoff]]
