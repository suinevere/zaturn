---
name: cgl-only-presentation-handoff
description: The category art system is gone and Zork I's own CGL pictures and CD tracks are now the whole art supply for all 31 games -- the eleven archives are injected into /BG post-build instead of committed, the presentation table extends to every game, and one review app assigns a picture and a track per room from what Zork I did.
metadata:
  type: project
---

Continues [[dynamic-palette-strip-shift-and-mix-removal-handoff]] and
[[zork1-authentic-presentation-handoff]]. Those still hold everything about the
Zork I presentation feature itself -- the CGL decoder, the join, the two alias
rows, the twenty unproven room orderings. This file covers only what changed
after them.

## What the owner asked for

Three things, in one instruction: inject the backgrounds post-build through
`tools/assets/*.bat` the way the Z3 games already are; gut the random
image-backgrounds-by-category system and keep only the Zork I per-room approach;
and update the local review app to assign per-room choices for every game,
inferring from Zork I.

Answers given during the work that shaped it:

- **The archives come from the disc `AUDIO_URL` already names.** `music.bat`
  downloads the Japanese Zork I disc for its CD audio and prints "Skipping
  Track 1" -- the data track holding the eleven `B*.CGL` archives was fetched
  and thrown away. Nothing new is downloaded and nothing copyrighted is
  committed.
- **The TGA path and per-game art go entirely.** Images come only from Zork I's
  CGL frames and music only from Zork I's CD tracks. The other thirty games get
  a per-room table of the same shape, filled by blessing which Zork I picture
  and track each of their rooms gets.

## Where the repo is

`main` is at `e1329d8`, **9 commits ahead of `origin/main`, unpushed** -- the 5
that were already local (see the previous handoff) plus these 4:

| Commit | What |
|---|---|
| `d9ebd67` | `/BG` injection: `tools/extract_bg.py`, `tools/assets/bg.bat`, two-map xorriso commit; `saturn/cd/data/BG/` deleted |
| `5788345` | `tools/gen_pool.py` -> `tools/assets/zork1_pool.json`, the picture/track catalogue and per-scene evidence |
| `ade1818` | The category art system removed, engine and tools |
| `e1329d8` | Presentation table extended to 31 games; `tools/pres_server.py` review app |

The `.CGL` tracking question the two earlier handoffs both flagged is **settled
and reversed**: `saturn/cd/data/BG/` is gone, `tools/assets/BG/` is gitignored,
and the archives are extracted per build. `analysis/zork_bg/raw/` still holds
its own tracked copy -- that is the reverse-engineering record, untouched.

## The pipeline now

`update.bat` runs `bg.bat` -> `games.bat` -> `music.bat`, in that order and for
a reason: `games.bat` injects what `bg.bat` stages, and `music.bat` promotes the
resulting data track to Track 01. BG staged after `games.bat` would never reach
the disc.

`bg.bat` prefers `ZORK_DISC` (a local disc, new key in `CONFIG.ME`, defaulting
to the reference copy under `cd/`), else downloads and caches `AUDIO_URL`. It
skips entirely when `tools/assets/BG/` is already complete.

`extract_bg.py` verifies every archive by size **and SHA-256** against
`BG_MANIFEST` before staging. That check is load-bearing, not defensive:
`game_presentation.inc` records a byte offset and length per frame measured
against those exact bytes, so a different disc revision would not fail to open
-- it would decompress from the wrong offset and show garbage, or hang the LZSS
loop, with nothing upstream to say why.

Both `-map` arguments go into **one** xorriso commit. Two commits would rewrite
a several-hundred-MB image twice and force IP.BIN to be restored over the
first's output.

### Proven, not assumed

Extraction and injection were run end to end: eleven archives extracted from the
real disc under `cd/`, byte-identical to the deleted `saturn/cd/data/BG/` copies;
a two-map commit against a synthetic base ISO produced `/BG` with all eleven,
`/Z3` **merged** (the base's `GAME.INF` survived alongside the injected stories),
`0.BIN` still at root; and all eleven verified byte-identical again after the
ISO round trip.

**A git-bash trap worth knowing.** MSYS path conversion rewrites xorriso's `/Z3`
and `/BG` *destination* arguments into `C:/Program Files/Git/Z3`, so the files
land in a nonsense directory. It does not affect the real pipeline (PowerShell
on Windows, a real shell elsewhere) but it will bite anyone testing the sh half
from git-bash. `MSYS2_ARG_CONV_EXCL='*'` is the workaround. This is a cousin of
the already-recorded git-bash `find` quirk in the auto-memory.

## What was removed, and why it cost nothing

Deleted: `fetch_art`, `art_server`, `art_review`, `art_metrics`, `art_nouns`,
`art_terms`, `art_queries`, `art_status`, `scene_server`, `scene_tracks`,
`room_scenes`, `gen_scene_tables`, `servers.py`, `walkthrough.py`, `make_tga.py`,
their 15 tests, the three root `.bat` launchers, `scene_map.c/.h`,
`game_rooms.inc`, `game_scenes.inc`, `game_tracks.inc`, the orphaned
`src/classify/*.o`, and the TGA image-slot machinery in `display.c/.h`.

**It shipped no pictures.** `game_scenes.inc` was 992 x `{0,0}` -- no game had a
picture for any scene. `SCENE_TRACKS` was 32 x `0`. `art_manifest.snapshot.json`
was `{}`. `cd/data/TGA/` holds only `SUINE.TGA`. Removing it changed nothing on
screen, which is the only reason a removal this wide was safe.

### Three things that survived deliberately

1. **`tools/scene_vocab.py` and `tools/assets/scenes/`** -- the 32-scene
   vocabulary with its ordered title rules, and 1,021 hand- and rule-tagged
   rooms. Kept as **inference inputs for the review app only**; nothing reads
   them at runtime. They are what makes a suggestion possible at all.
2. **`tools/gen_title_art.py`** -- carved out of `make_tga.py` before deleting
   it, because `make_tga.py` also generated `title_art.inc`, which the title
   screen includes. Reproduces the same `.inc` byte for byte. The TGA loader
   survives for it and for the boot splash; nothing else is a TGA.
3. **`tools/trim_z3_vocab.py`** -- I deleted this and had to restore it. It is
   not art tooling at all: it trims the Z3 vocabulary for the netbin typeahead,
   and `saturn/tests/test_netbin_story_pin.py` imports it. Caught by running
   that test, not by reading.

## Two real defects found by tests, both fixed

**Thirty games would have fallen silent.** An unmapped room used to get a scene
index, which resolved through an all-zero mask to the neutral pool. Reading the
obvious replacement -- "no authored table means no category" -- puts it at
`CAT_KIND_NONE`, which `music_on_turn` early-returns on, so nothing plays at all.
Fixed with `CAT_KIND_POOL` (value 1, where `CAT_KIND_SCENE` sat): every room of
an unauthored story shares one pooled category, so the music does not restart on
every step and the neutral pool supplies the track. `test_music_static.c` caught
this.

**The review API accepted phantom rooms.** Assigning object 11 to DEADLINE
succeeded and reported nothing decided, because DEADLINE has no object 11. It
would have reached `game_presentation.inc` as a row nothing reads and nothing
reports as wrong. `api_assign` now refuses an object that is not one of the
game's rooms, a picture outside the pool, and a track not on the disc.

## The inference, and how good it actually is

`gen_pool.py` reads the object -> picture join out of `game_presentation.inc`
rather than rejoining Zork I's rooms by title. The first attempt rejoined by
title and reported MAZE with **225** pieces of evidence: fifteen rooms titled
MAZE cross-joined against fifteen CSV rows. The real number is 15.

Corrected, 70 Zork I rooms carry both a scene tag and a picture, covering 14 of
32 scenes:

| Scene | n | picture agreement |
|---|---|---|
| FOREST, PARLOR, KITCHEN, ROAD | 1-4 | 100% |
| MAZE | 15 | 73% |
| MINE | 6 | 66% |
| RIVER, HOUSE_EXT, DARKROOM, TEMPLE, PIT | 2-8 | 50% |
| ROCKY | 6 | 33% |
| SHORE | 5 | 20% |
| CAVE | 13 | **15%** |

**Track inference is far stronger than picture inference** -- MAZE 15/15, FOREST
4/4, MINE 6/6, HOUSE_EXT 4/4 on track, against CAVE's 2/13 on picture. The app
reports both and colours the confidence, because a UI that rendered FOREST and
CAVE identically would be lying about one of them.

The other 18 scenes never appear in Zork I. `SCENE_ANALOGUE` in `gen_pool.py`
names a hand-picked visual stand-in for each with one line of reasoning, marked
`analogue` so it is never mistaken for evidence.

Of the 1,857 rooms needing a verdict: 906 (48%) have no stored scene tag, so
`scene_of` falls back to the title rules; a title-derived scene is capped at
`weak` no matter how well the scene itself is supported, because two inferences
are stacked and only one has evidence.

## Verification reality

**Nothing has been built for the SH-2 or run, on Mednafen or hardware.** Same
ceiling as both previous handoffs.

- `sh saturn/syntax-check.sh` clean, DEBUG and release, and again under `NETBIN=1`.
- 20 host C tests build and pass, including the rewritten `test_display.c`,
  `test_music_static.c`, `test_music_pause.c` and `test/music_mix_test.c`.
- 67 Python tests pass (`tools/tests/`), including 22 new ones in
  `test_pres_store.py` covering the pool, the suggestions and every API refusal.
- The standalone `saturn/tests/*.py` pass, including `test_no_classifier.py`
  (the deletion-sweep guard) and `test_netbin_lift.py`.
- The `/BG` extraction and injection were actually executed and byte-verified.

### Two pre-existing failures, neither caused here

- **`test/music_test.c`** still does not compile: `EV_DANGER`/`EV_TRIUMPH` moved
  out of the header it includes, exactly as the previous handoff recorded. It
  also uses `music_set_category_fn`, now removed, and its "an unmapped room plays
  nothing" premise is stale under `CAT_KIND_POOL`. Left untouched rather than
  guessed at -- fixing it means resolving all three.
- **`saturn/tests/test_lwram_splash_budget.py`** fails with "no background art
  found under cd/data/TGA". **Verified pre-existing** by checking out `d9ebd67`
  and running it there: identical failure before any removal. The disc has
  shipped no TGAs for some time.

## What a next session would do

1. Build (`saturn/compile-cd.bat`) and run `tools/assets/update.bat`, then walk
   Zork I on Mednafen. This is still the first time any of the presentation work
   would be seen. Watch that `/BG` is actually on the disc and `room_art` opens
   it -- the injection is proven at the ISO level but never proven against the
   Saturn's own ISO9660 parser.
2. Start assigning: `start_review_server.bat`, then `/reference` first to see
   what the suggestions rest on. `Accept every strong suggestion` clears the
   well-founded ones per game; the weak, analogue and unfounded ones are the
   actual work. Run `python tools/gen_presentation.py` after, and rebuild.
3. Decide the push (still 9 commits unpushed) and the two stale remote branches
   from the previous handoff.
4. Unchanged from before: the Mednafen breakpoint capture at `0x060A597C` for
   the maze/river ordering and the seven unattributed tracks.

## Suggested skills

- **`superpowers:verification-before-completion`** -- the standing hazard is
  unchanged and this session added to the surface: every screen-facing claim
  here is compile-checked only.
- **`diagnosing-bugs`** -- the two defects found this session were both "the
  signal never reaches the code", same as the last two. Start at the subscriber.
- **`superpowers:finishing-a-development-branch`** -- for the still-unpushed 9
  commits and the stale remotes.
