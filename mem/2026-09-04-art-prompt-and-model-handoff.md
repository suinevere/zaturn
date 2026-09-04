---
name: art-prompt-and-model-handoff
description: The room art was wrong for reasons that were never the wording -- a photorealism checkpoint at CFG 2.5 that could not read a prompt, then 24 words of boilerplate against 6 of room once it could, then one regex throwing away 1,050 sentences of scenery. All three are fixed, dreamshaper_8 at CFG 7 and 25 steps are the generator's defaults, and the full redraw is the only option left because every prompt in the sheet has changed. 29 commits on art-v2; a generation from the pre-fix sheet is still running and should be stopped.
metadata:
  type: project
---

## Where this got to

**29 commits on `art-v2`**, `c557893` through `3de32b9`, on top of the state
[[per-room-art-generation-handoff]] describes. The reasoning is in those
messages; this records only what a diff cannot say.

Nothing has been built or run on hardware, still. `room_art.cxx` passes SH-2
`-fsyntax-only` and that is all it has ever done.

## Stop the running generator

`gen_art_source.py` started **07:15:09** (PIDs 2004 / 14828) from the sheet as
it stood then. Every fix after that -- the weighting, the sculpture filter,
Stationfall's station, `way out`, the title/scene dedupe -- is in the sheet and
in none of its plates. It is drawing work that will have to be thrown away.

Stop it, then:

```
tools\.venv\Scripts\python.exe tools\reset_room_art.py --apply
tools\.venv\Scripts\python.exe tools\gen_art_source.py --sheet tools\assets\art\room_prompts.json
tools\.venv\Scripts\python.exe tools\gen_art_archive.py
tools\.venv\Scripts\python.exe tools\assign_room_art.py
tools\.venv\Scripts\python.exe tools\gen_presentation.py
tools\.venv\Scripts\python.exe tools\gen_pool.py
```

**Do not reset while it runs.** It has plates drawn but not checkpointed; the
reset would delete files the manifest is about to claim, and `gen_art_archive`
would fail on them later.

~1,759 plates at 25 steps is **ten hours or so**. Step 1 resumes, so stopping
it costs nothing.

## State on disk

- `manifest.json` 509 plates, 400 of them rooms; sheet 1,759 rooms.
- Of what is on disk: **0 current, 0 stale, 21 not in the manifest, 400
  unrecorded**. Every plate is one that cannot be trusted, which is the
  situation the whole review was being done blind in.
- 279 tests pass. The three `cv2` collection errors from `origin/main` are
  still there and still not ours.

## Findings that are not in a commit message

- **The words were never the problem, twice over.** The only checkpoint
  installed was `realisticVisionV60B1_v51Hyper` -- a photorealism model whose
  Hyper distillation requires CFG 2.5, at which a hundred-token negative
  prompt barely applies. People kept appearing in plates that forbade them
  nine ways. Then, at CFG 7, the prompt was finally read and it turned out to
  be 24 words of boilerplate against 6 words of room, with 190 prompts running
  past CLIP's 77-token window. Ten hours of rewording preceded measuring
  either.
- **Nine of the fixes to one room were deleting a word of mine.** `crawl
  space`, `house`, `brick piers`, `low ceiling`, `a shaft of light`, `slivers
  between the boards`, `floorboards`, `hanging wires`, `spacecraft`. The model
  was never hallucinating; it drew what it was asked for. Suspect the prompt
  before the model.
- **A composition is not a sentence.** After eight rewordings the crawl space
  was solved by drawing a geometry and using img2img at 0.6. The guide is
  generated, not photographed -- `tools/gen_art_refs.py` -- because what is
  wanted is the arrangement of the frame and nothing else. The one attempt to
  improve it made it worse and was reverted; it is committed at `fc550f0` plus
  a ground added in `92caff3`, and the ground mattered: a flat band gave the
  model nothing to hold and it painted the lower half as void.
- **`--style`, `--cfg`, `--steps`, `--checkpoint`, `--compose-from` and
  `--denoise` on `probe_prompts.py` exist so the next question is measured in
  four minutes rather than argued about.** Use them before changing a default.
- The owner reviews raw plates; what ships is graded to `TARGET_MEAN` 24 by
  `gen_art_archive`. **Nobody has once checked that a good raw plate survives
  that pass.** It is the obvious next thing and it has never been done.

## Traps

- **A plate says nothing about its own currency.** Two leftovers were reported
  as fresh failures, one after a reset that could not see it. Every plate now
  records a fingerprint of what it was drawn from and `tools/art_sheet.py`
  labels each row current / stale / not in the manifest. Read the label before
  reporting a plate.
- **The manifest is written every 25 plates**, so up to 24 drawn plates are
  always on disk and unlisted. `reset_room_art.py` used to trust the manifest
  and left 23 behind. It now takes its names from the sheet as well.
- **`\b` written through a bash heredoc becomes a literal backspace.** It hit
  four times this session, once in already-committed source where it had
  silently disabled a strip for weeks. `TestSourceHygiene` fails on any
  control character now. **Use the Edit tool for anything with a backslash.**
- **Object numbers are Z-machine object ids, not indices.** `statfall_1` does
  not exist. Get real names from `probe_prompts.py --dry-run` or `art_sheet`.
- Genre may say *when* a game happens but never *where*: Hitchhiker's is SCIFI
  and has a pub and a country lane in it. `GAME_SETTING` is where a place goes.

## What is not done

- The full redraw. It is the only option: every prompt changed.
- `statfall_224`'s first sentence still ends `which lies` -- the dangling-
  clause strip covers `which is|are` and not `which lies`.
- Six rooms are mis-tagged rather than mis-worded: five Cutthroats `Below
  Decks` and one Infidel `Below Deck` carry `SHIP_EXT` while being interiors.
  Upstream of prompting, left alone.
- **42% of rooms -- 752 of 1,759 -- the story file describes with nothing at
  all.** For those a picture is invention from a title and no rule can make it
  faithful. `art_sheet --no-prose` and `probe_prompts --no-prose` show only
  those. Whether they should get unique invented plates or share a per-scene
  one is undecided and worth deciding before the ten hours are spent.
- No second checkpoint. `GENRE_LOOK` carries an optional `checkpoint` per
  cluster, unset everywhere; the three clusters are 824 rooms of adventure,
  557 of science fiction, 378 of period crime.

## Suggested skills

- **`superpowers:systematic-debugging`** -- the two biggest wins of the session
  came from measuring the checkpoint settings and counting boilerplate words
  against room words. Both were available on day one and neither was looked at
  until ten hours of wording had been spent.
- **`superpowers:verification-before-completion`** -- a plate on disk looks
  finished whatever produced it. Two were reported as failures that were
  leftovers.
- Not `brainstorming`. What remains is a command sequence and one open
  question about the 752 silent rooms.

## Related

[[per-room-art-generation-handoff]], [[generated-room-art-pipeline-handoff]],
[[zork1-authentic-presentation-handoff]], [[art-data-is-disposable]],
[[user-runs-all-builds]]
