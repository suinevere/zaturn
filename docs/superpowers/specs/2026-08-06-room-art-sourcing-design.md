# Room Art — Sourced Pools, Foldered Disc Layout, Adjustable Dim — Design

**Date:** 2026-08-06
**Status:** Proposed
**Follows:** `2026-07-30-dynamic-background-art-design.md`, `2026-08-06-classification-vocabulary-design.md`

## Goal

Twelve place moods share 37 pictures — three or four each. A player who spends an
hour underground sees the same three caves on rotation, and the classifier work
that just landed made this worse rather than better: 150 rooms were reclassified
into moods whose pools did not grow to meet them.

The fix is depth, not cleverness. Ninety-nine pictures per mood, sourced from
Pixabay against a query plan derived from the classifier's own vocabulary, gated
on legibility and quantisation, reviewed by hand, and filed one folder per mood
on the disc.

The mood stays the only key. A room's picture is chosen by its `TC_*` category
exactly as it is today; nothing in `room_class.c` moves, and `blessed.inc` is
untouched.

## Scope

In:

- A Pixabay fetcher driven by a hand-edited JSON vocabulary plus nouns parsed
  from `room_class_data.c`.
- An automatic metric gate and an HTML contact-sheet review pass.
- Disc layout moving from a flat `/TGA` to one folder per mood.
- Filename synthesis in `display.c`, replacing the boot scan and the hand-written
  `CATEGORY_IMAGE` string arrays.
- A user-adjustable wallpaper dim in Display Options, on VDP2 Colour Offset B.
- Git LFS for the generated TGAs.

Out:

- Any change to classification. `text_classify_room` keeps its signature, the
  keyword tables are read but never written, and `blessed.inc` is not
  regenerated.
- Per-noun or per-room-type runtime selection. The folder hierarchy under
  `tools/assets/png/` records where a picture came from; it does not reach the
  Saturn.
- Any second image source. The manifest records a licence string per image so a
  later source is possible, but only Pixabay is implemented.

## Architecture

### Disc layout

```
/TGA/WILDER/01.TGA   … 99.TGA
/TGA/UNDRGRND/01.TGA … 99.TGA
/TGA/NAUTICAL/01.TGA … 99.TGA
/TGA/SUINE.TGA                  (boot splash, stays at /TGA root)
```

The folder is the mood; the file is a bare two-digit index.

Each mood has exactly **one canonical short name**, and it is the directory name
`tools/assets/png/` already uses: `WILDER`, `UNDRGRND`, `WATER`, `NAUTICAL`,
`TOWN`, `DUNGN`, `DESERT`, `MAGIC`, `SCIFI`, `HORROR`, `MYSTERY`, `HOUSE`. All
twelve are already eight characters or fewer, so nothing is renamed. That one
name is the disc folder, the source-tree folder, the key in
`art_queries.json`, and the spelling used in a `donors` list — there is no second
vocabulary to keep in step with the first.

This is what makes 99 reachable. A flat disc names files `<stem><index>` inside
an eight-character ISO9660 8.3 stem, so a two-digit index needs a six-character
prefix — and `MYSTERY`, `NAUTICA`, `UNDRGRN` are seven. `display.c:354` records
the two truncations that constraint already forced. Moving the mood into the path
frees the whole stem and those truncations disappear.

Folders also bound the directory table. `saturn/makefile:7` sets
`SRL_MAX_CD_FILES = 256`; 99 entries per mood sits well inside it, where 1188
files in one directory would not.

### Filename synthesis, and the death of the boot scan

`display_scan_images()` (`title.cxx:240`) walks `/TGA` at boot and copies every
name into `g_image_name[DISP_IMAGE_MAX][16]`, and `display.c` hand-lists each
filename in twelve `IMG_*` arrays. At 1188 pictures that is 24KB of name storage
and 1188 string literals maintained by hand. Both are removed.

`tools/make_tga.py` instead emits a generated, committed
`saturn/src/video/category_art.inc`:

```c
static const unsigned char CATEGORY_ART_N[TEXT_NUM_CATEGORIES] = {
    0, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 0, 0
};
```

Row order is the `TC_*` enum order, as `CATEGORY_IMAGE` is today. The three zero
rows are `TC_NEUTRAL`, `TC_DANGER` and `TC_TRIUMPH`, which hold whatever is
showing and must keep doing so.

`display_category_image(cat)` formats `"%s/%02d.TGA"` from a small
`CATEGORY_DIR[]` table and the existing `g_cat_rot` counter. Twelve bytes replace
24KB, `DISP_IMAGE_MAX` and `display_set_images` are deleted, and the counts come
from the files that actually converted rather than from names a human typed.

Pool disjointness stops being a property maintained by care. A file lives in one
folder; two moods cannot name it.

### Query scaffold

Two inputs, one derived and one hand-edited.

**Nouns, derived.** `tools/art_nouns.py` parses `room_class_data.c` and collects
every `KT_STRUCTURE` and `KT_BIOME` row — the words that name a place a camera
could stand in (`hallway`, `attic`, `ballroom`, `cave`, `shaft`, `tunnel`).
`KT_FEATURE` rows are excluded: a rug is not a room. A keyword added to the
classifier later gains art coverage with no edit here, which is the point of
deriving rather than duplicating.

**Adjectives and donors, hand-edited.** `tools/assets/art_queries.json`, with
`tools/assets/art_queries.md` beside it documenting each field:

```json
{
  "HORROR": {
    "adjectives": ["dark", "abandoned", "derelict", "decaying",
                   "eerie", "ruined", "gloomy", "foggy"],
    "donors": ["HOUSE", "UNDRGRND", "DUNGN"],
    "extra_nouns": ["morgue", "crypt", "asylum ward"],
    "exclude_nouns": ["kitchen", "porch"],
    "target": 99
  }
}
```

`donors` is what makes `TC_HORROR` work at all. Its seven keywords are qualities,
not places — `corpse`, `decay`, `eerie`, `rotting`, `shadow`, `skeleton`,
`stench` — and nothing photographs ninety-nine distinct stenches. Horror is a
mood applied to a place, so it borrows its nouns from the moods whose places it
haunts. `TC_MAGIC` and `TC_MYSTERY` have the same shape and the same remedy;
`TC_HOUSE` and `TC_UNDERGROUND`, whose keywords are all concrete places, donate
to themselves.

Query is `adjective × noun`. `extra_nouns` and `exclude_nouns` are the escape
hatches — add a search word the classifier does not know, or drop one that keeps
returning junk — so tuning what gets searched never means editing Python or
`room_class_data.c`.

### Source tree

```
tools/assets/png/HORROR/HOUSE/hallway/01.png      ← "dark hallway"
tools/assets/png/HORROR/UNDRGRND/shaft/02.png     ← "derelict shaft"
tools/assets/png/HOUSE/HOUSE/ballroom/01.png      ← "victorian ballroom"
```

Mood, then donor mood, then noun. The path is the query's provenance: any picture
on the disc can be traced to the adjective and noun that found it. `make_tga.py`
already walks subfolders and flattens; it now flattens each mood's whole subtree
into that mood's one disc folder, numbering `01..99` in walk order.

### Fetch and gate

`tools/fetch_art.py` requests each query from Pixabay with `image_type=photo`,
`orientation=horizontal`, `safesearch=true`, taking roughly three candidates per
slot. The key comes from `PIXABAY_API_KEY` and is never committed. Candidates are
centre-cropped to 320×224, then scored on three axes:

1. **Legibility.** Mean luminance and high-frequency energy. Text draws over the
   picture, so this rejects more candidates than anything else.
2. **Quantisation error.** RMS delta between the crop and its 255-colour
   MEDIANCUT reduction — the same reduction `make_tga.py` performs. Gradient-heavy
   skies band into mud and are cheaper to reject here than to notice on hardware.
3. **Perceptual-hash dedup.** Against every already-accepted image across all
   moods, so pools stay varied and a near-duplicate cannot appear in two.

The two legibility metrics are weighted differently now that a dim exists — see
*Adjustable dim* below.

Survivors render into twelve HTML contact sheets, one per mood, each thumbnail
captioned with its query and its three scores. Verdicts land in
`tools/assets/verdicts.json`; accepted images are promoted into the source tree.
Re-running tops up only folders short of `target`, so review is resumable across
many sittings — which at 1188 images it will need to be.

`tools/assets/art_manifest.json` is committed and records, per accepted image:
Pixabay ID and URL, the query that found it, the licence string, the fetch date,
and its three scores. Without it, "where did this picture come from" has no answer
in a year.

### Adjustable dim

`title_bg_dyn_fade` (`title.cxx:1183`) already puts NBG0 alone on Colour Offset
channel B, clearing NBG3 off it first so the text never dims with the picture.
VDP2 adds a signed per-channel constant after palette lookup: exact arithmetic,
no dithering. Because it is signed it lightens as well as darkens, which matters —
`DISP_TEXT_BLACK` is selectable and dark text wants a lighter wallpaper.

A held offset `g_bg_hold` (−255..+255, default 0) composes additively with the
existing transition ramp:

```
effective = clamp(g_bg_hold + (level - 255), -255, +255)
disengage channel B only when effective == 0
```

Every existing caller keeps passing 0..255 unchanged. With no dim set, behaviour
is byte-identical to today. With one set, the resting state holds it and
room-to-room transitions still dip to black through it.

The hazard is `title_fade_engage`, which moves NBG0 to channel **A** for
screen-wide fades and clears `g_dyn_faded`. A scroll uses A or B, not both, so a
title fade silently drops the held dim; it must be re-applied on fade disengage.
`title.cxx:1173` already reasons about this claim being released, so the pattern
exists — but this is where a bug will live if one does.

Display Options gains a row of discrete stops rather than a slider, because at
8bpp an extreme offset clips distinct palette entries onto the same value and
posterises. Seven stops in steps of 32, `Lighter +2` through `Normal` to
`Darker −4`, giving held offsets of `+64, +32, 0, −32, −64, −96, −128`. The row
previews live while focused; judging a dim without seeing it is pointless.

`DisplayState` gains a `dim` field. New sentinel **5**, `DISP_BLOB_BYTES` growing
17 → 18, so old blobs stay distinguishable by both sentinel and length — the
discrimination the four existing forms rely on (`display.h:268`) is preserved
rather than weakened. Sentinels 1–4 decode with `dim = 0`, which is today's
appearance exactly.

**Effect on the gate.** A uniform offset rescues an image that is merely too
bright; it does nothing for one that is too busy, because local contrast survives
the offset unchanged. So mean-luminance rejection loosens and high-frequency-energy
rejection tightens. Net effect is more usable candidates per query, which
materially reduces the sourcing burden at 99 per mood.

### Repository size

1188 TGAs are ~86MB raw and ~40MB packed, and every re-curation churns those blobs
permanently. `saturn/cd/data/TGA/` is committed on purpose today:
`convert-backgrounds.sh` falls back to it when Python or the network is missing,
so a clean checkout always builds with art.

The curated 320×224 PNGs are committed as plain blobs — they are the editable
masters and the product of the review work. The generated TGAs move to **Git
LFS**, which keeps the offline-fallback promise without the pack growing without
bound.

## Sequencing

This ships as **two independent plans**. They share exactly one thing — the
per-mood folder convention fixed in *Disc layout* above — and nothing else. Plan
A touches only Saturn C and can go to hardware on the 37 pictures that already
exist; Plan B touches only Python and never compiles for the SH-2.

### Plan A — Saturn: foldered art and an adjustable dim

1. **Disc layout and synthesis.** Move the existing 37 pictures into per-mood
   folders, emit `category_art.inc`, replace the boot scan and `CATEGORY_IMAGE`
   with synthesis, delete `DISP_IMAGE_MAX` and `display_set_images`.
2. **Adjustable dim.** Offset composition in `title.cxx`, the Display Options
   row, blob sentinel 5.

Done when the disc renders its existing art from mood folders and the dim row
works — with no new picture sourced. Both the CD-navigation test and the
fade-interaction test belong here, which is the point of landing it first: the
two risky changes are proven before 1188 images depend on them.

### Plan B — Pipeline: sourcing, gating, curation

3. **Query scaffold.** `art_nouns.py`, `art_queries.json`, the query builder and
   its test. No network.
4. **Fetcher and gate.** `fetch_art.py`, the three metrics, the manifest.
5. **Contact sheets and review.** The HTML sheet, `verdicts.json`, promotion.
6. **Curation.** Fill the twelve folders. The long tail, done in sittings.
7. **Git LFS migration.** Last, once the volume is real.

Plan B's only dependency on Plan A is the folder layout `make_tga.py` writes
into. Steps 3–5 can be built and tested against the current 37-picture tree
while Plan A is still in review; step 6 is where the two meet.

## Testing

- `saturn/tests/test_category_art.py` is rewritten. The disjointness assertion is
  deleted — the filesystem enforces it. It asserts instead that every count in
  `category_art.inc` matches the files present in `/TGA/<MOOD>/`, that no folder
  exceeds 99, and that no filename falls outside `01..99`.
- A new test asserts every mood folder name is ≤8 characters and every filename
  fits 8.3 — the constraint that forced this layout, checked rather than
  remembered.
- A pure-Python test on the query builder: a fixed `art_queries.json` and a fixed
  keyword table produce an expected query list. No network.
- A pure-Python test on the metric gate: fixture images with known luminance,
  known busyness, and a known-banding gradient are scored and must land on the
  expected side of each threshold.
- A blob round-trip test: sentinel 5 encodes and decodes `dim`; sentinels 1–4
  still decode, with `dim = 0`.
- **A hardware-path test for the fade interaction.** Set a dim, run a
  screen-wide title fade, disengage it, and assert the dim is still applied —
  the specific failure the A/B channel exclusivity invites.
- **A CD-navigation test.** Load a picture from a mood subfolder, return to root,
  and assert a CD-DA track is still playing. `mem/MEMORY.md` records
  `GFS_LoadDir(0)` being mistaken for root and silently muting the drive for a
  whole session; per-load `ChangeDir` is new here and is the riskiest line in
  this design.

## Risks

**CD navigation during playback.** Every cache miss now enters a mood folder
before opening. This is the single most dangerous change and has its own test
above. If it proves unstable on hardware, the fallback is a flat `/TGA` with
`SRL_MAX_CD_FILES` raised past 1188 — costing roughly 14KB per directory-name
array across four call sites, which is the trade this design is declining, not
one it cannot make.

**Colour Offset channel exclusivity.** A held dim on B and a screen-wide fade on
A cannot coexist on NBG0. Re-application on disengage is specified above, but the
interaction surface is larger than it looks and includes the loading screen,
which also drives Offset A (`loading_screen.cxx:232`).

**Licence.** The Pixabay Content License permits commercial use without
attribution but is not an open-source licence, and it forbids redistributing
images as-is as a competing service. The manifest exists so this is auditable. If
the ISO is ever distributed under terms that need genuinely open assets, the
manifest is what makes a source swap tractable.

**Curation volume.** 1188 images reviewed by hand is the real cost of this
design, and no amount of automation removes it — only reduces it. Resumability
and the metric gate are the two mitigations; a mood that stalls short of 99 still
ships and simply rotates over fewer pictures.

**Posterisation at extreme dim.** Documented, and the reason for discrete stops
over a slider.
