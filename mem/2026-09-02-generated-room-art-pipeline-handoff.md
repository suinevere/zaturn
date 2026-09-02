---
name: generated-room-art-pipeline-handoff
description: The presentation review app lost its confirming click and gained areas, genres and a best-guess basis so all 1,931 rooms now name a picture and a track; the 74 pictures were finally LOOKED at, which found CORRIDOR pointed at a rock face and under 22% of the disc; and the road to new art is now open at both ends -- Stable Diffusion runs on the owner's machine and a CGL writer exists and is verified against the console's own decoder -- with only IMAGE_FRAME's 74-entry ceiling left between generated plates and the disc.
metadata:
  type: project
---

## Where this got to

Twelve commits, `e47f607` through `9692986`, all on `input-dashboard`. The
branch is **82 ahead / 2 behind `origin/input-dashboard`** and nothing has been
pushed, built or run on hardware. `git log e47f607^..HEAD` is the narrative;
this document is only the things a diff does not say.

Every one of the 1,931 rooms across the thirty games now names both a picture
and a track, none unfounded: 197 strong, 297 weak, 486 analogue, **951 best
guess**, 0 chosen, 0 unset. 65 of the 74 pictures are in use with a worst share
of 11%. `saturn/src/scene/game_presentation.inc` covers all 31 games and is
23,808 bytes of const table; it syntax-checks but has never been compiled into
a build. 195 tests pass.

## The findings, which are the part worth keeping

Each of these was believed to be otherwise until it was measured, and each
would have shipped something wrong.

- **The pictures were never looked at.** Every mapping was made from the NAME of
  the Zork I room each picture was drawn for, and a name is a caption. #37 is
  called East-West Passage, so CORRIDOR pointed at it and it landed under 441
  rooms -- 22% of the disc -- and #37 is a rough ochre rock face that is not a
  passage. The corridors are 30, 33, 39. The only prison cell is 18, which has a
  grating in its ceiling. The only dunes are 62, called Sandy Cave.
  `tools/image_looks.py` now records what each of the 74 actually shows.
- **A sample of one was outranking everything.** KITCHEN, PARLOR and ROAD are
  each ONE Zork I room, so each agreed with itself 100%, cleared the 60% bar and
  kept its picture wherever it was guessed. That is how the Bridge of the Heart
  of Gold came to be a dim Victorian kitchen. `room_guess.MIN_ROOMS` is why a
  scene now needs three measured rooms before it outranks spreading.
- **The plates hold about TWENTY-SIX colours, not 256**, and live in a narrow
  dark band -- BDAM_00 runs 0 to 68, mean 9. The posterisation and the darkness
  are as much of the look as the tint. Matching them also halved the encoded
  record, to just under the size of the frame it would replace.
- **The colour grade is per FRAME, not per area.** BCEL holds brown rooms and
  blue ones, hue spread 71 degrees; BDED's is 107. "Grade it like the cellar
  archive" does not name a colour.
- **Spellbreaker's spell book was being mistaken for its map.** See `6d2c596`.
  Left behind by that: `build_mojozork` returned a path gcc had not written on
  Windows, so the runtime half of the room corpus had been silently absent on
  this platform for its whole life. Other stories may still be thin as a result.

## The owner's machine, which is not in the repo

Stable Diffusion runs and generates in 4-13s per 512x384 plate. Getting there
took two fixes that will recur if anything is reinstalled:

- `sd-webui-forge-neo` at `C:\Users\saggl\CLionProjects\sd-webui-forge-neo`
  (**neo**, not the "forge-classic" it was described as). Needs `--api`; without
  it every `/sdapi/v1/*` is a 404 while the UI works fine.
- **The GPU is a GTX 1070 Ti, compute capability 6.1.** The shipped
  `torch 2.13.0+cu130` carries no kernels below `sm_75`, so every generate
  returned `AcceleratorError` and no Forge setting could fix it. Replaced with
  the same versions from the **cu126** index, whose arch list starts at `sm_50`.
  A force-reinstall then broke gradio's pins; markupsafe is back at 2.1.5 and
  pillow at 11.3.0, which satisfies pillow-heif and violates only gradio's
  conservative upper bound, as it did before.
- Checkpoint `realisticVisionV60B1_v51HyperVAE` with its VAE baked in, CLIP skip
  1. Working settings **8 steps / CFG 2.5, DPM++ SDE, SGM Uniform, 512x384**.
  The Hyper merge tolerates the default 20/7 rather than burning; 8 steps is
  chosen because everything past it dies in the downsample to 320x240.

## What is not done

- **`IMAGE_FRAME` stops at 74.** This is the whole remaining gate. New pictures
  need entries past it and new or extended archives written; `tools/cgl_encode.py`
  can write the records, and nothing yet writes an archive or extends the table.
- **The style is proven on exactly one plate against one reference**, the Dam
  Control Room, which is among the least saturated frames on the disc. It has
  not been tried against a green or gold reference and may need work there.
- **Grain is a hook at zero.** `room_art_style.stylise(grain=)` exists because
  the originals carry photographic noise the ramp cannot invent, but no value
  was chosen -- that is a look judgement and wants the owner's eye.
- Nothing generated has been wired into the game. `analysis/room_art_tests/`
  holds the one styled plate, its inputs and its encoded `.cgl` as evidence.
- Nine of the 74 pictures are still never selected by any room.

## Traps

- `tools/tests/test_gen_presentation.py::test_regeneration_is_byte_identical`
  **rewrites `game_presentation.inc` as a side effect of running.** If it fails,
  `git checkout` the file. It is green now and stays green only while the store
  and the table agree.
- Run everything with `tools/.venv/Scripts/python.exe`. The owner runs all
  builds; do not run `compile.bat` or the emulator. SH-2 syntax checks are worth
  doing and the incantation is in the session log:
  `../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe -fsyntax-only`
  with `-DSRL_MAX_TEXTURES=100 -DSGL_MAX_VERTICES=3000 -DSGL_MAX_POLYGONS=3000
  -DSGL_MAX_EVENTS=64 -DSGL_MAX_WORKS=100 -DSRL_MAX_CD_BACKGROUND_JOBS=1
  -DSRL_MAX_CD_FILES=256 -DSRL_MAX_CD_RETRIES=5
  -DSRL_DEBUG_MAX_PRINT_LENGTH=512`, `-std=gnu++2b`, and includes for every
  `src` subdirectory plus `saturnringlib`, `modules/sgl/INC` and
  `modules/SaturnMathPP`.
- `curl` and `wget` are intercepted in this environment. Use `urllib` from
  Python, or the context-mode execute tool, to reach the Forge API.
- The CGL encoder's overlapping match cannot be validated by comparing against
  the input; it must be simulated against the ring. See `9685d50`.
- Areas are derived and have no stored identity. An area's id is its lowest
  object number and legitimately changes when a room is re-tagged; only the
  per-room verdict is stored.

## Suggested skills

- **`superpowers:verification-before-completion`** -- most of the real findings
  in this session came from checking a claim that looked obviously true. The
  pictures, the colour count, the sample size and the GPU arch list were all
  assumed correct until measured.
- **`superpowers:systematic-debugging`** -- for the `AcceleratorError` and the
  Spellbreaker hub, both of which had a plausible wrong cause (dtype, and a
  broken story file) that would have wasted a session.
- **`superpowers:test-driven-development`** -- the codec and the grading both
  have properties that are cheap to state and expensive to get wrong quietly.
- **`superpowers:requesting-code-review`** before merging: 82 commits, none seen
  on hardware, and a 24 KB const table added to the image.
- Not `brainstorming` -- the remaining work is specified.

## Related

[[one-save-record-per-slot-handoff]], [[zork1-situational-cues-wired-handoff]],
[[map-passage-marks-and-exit-destinations-handoff]],
[[room-art-pipeline-handoff]], [[scene-tagged-art-handoff]],
[[genre-banded-art-handoff]]
