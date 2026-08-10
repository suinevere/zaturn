# Room Art — Genre-Banded Category Pools — Design

**Date:** 2026-08-10
**Status:** Proposed
**Follows:** `2026-08-06-room-art-sourcing-design.md`,
`2026-08-05-per-game-fallback-mood-design.md`

## Goal

A room's picture is chosen by text category alone. `display_image_file()`
resolves a slot to `CATEGORY_DIR[cat]` plus a two-digit index, and
`make_tga.py` flattens the whole `png/<MOOD>/<DONOR>/<noun>/` tree into one
gapless `MOOD/01.TGA..NN.TGA` run per category. Nothing downstream of the
fetcher knows a donor, a noun, a genre or a game.

So Plundered Hearts' 17th-century cabin and Seastalker's submarine both
classify `TC_NAUTICAL` and draw from the same nine pictures, round-robin. A
wooden-ship room shows a modern trawler not occasionally but on schedule,
because `display_rotate_dynamic_category()` advances a counter rather than
sampling.

The classifier already resolves what would fix this. `GAME_GENRE[]` maps a
story's release and serial to `GN_FANTASY` / `GN_SCIFI` / `GN_MODERN`, and it
resolves at load for every listed game. The keyword table already marks the
ambiguous words — `{"cabin", TC_NAUTICAL, KT_STRUCTURE, GN_FANTASY|GN_MODERN}`.
The art path is the only part of the pipeline blind to it.

## Scope

In:

- A genre band within each category's existing 1..99 index range, and
  selection that prefers the running game's band.
- A per-noun genre tag in `art_queries.json`, and `make_tga.py` emitting
  banded rather than gapless runs.
- `CATEGORY_ART_N` becoming per-band counts.

Out:

- Any change to the `TC_*` enum. Its row index is shared with
  `music_data.c`'s `CATEGORY_POOL`; splitting a category into
  `TC_NAUTICAL_FANTASY` and `TC_NAUTICAL_MODERN` would silently repoint every
  music row after it. Genre is an art-only dimension.
- Per-game or per-room art. The pipeline retrieves stock photography; no
  photograph of "Flood Control Dam #3" exists to retrieve. Per-game override
  packs are a later layer over this one, not a substitute for it.
- The save blob format. See "Why bands, not folders".

## Why bands, not folders

The obvious shape — a second dimension on `CATEGORY_DIR` giving
`NAUTICL_M/NN.TGA` — does not fit, for two independent reasons.

`g_file_buf` is `[2][16]`, sized in its own comment for `"UNDRGRND/99.TGA"`:
fifteen characters and a terminator, exactly full. And `make_tga.py` holds
every name to the ISO 8.3 rule, so a folder stem cannot exceed eight
characters; `UNDRGRND` is already at the limit. A genre suffix overflows both
the buffer and the save blob's name field, which `display.c` calls "frozen at
its size for save-format compatibility".

Partitioning the index instead changes no path, no buffer and no save form.
`display_image_file()` is untouched. The slot stays `cat * SLOT_STRIDE + index`
with `index` in 1..99, so `display_slot_make()` and `display_slot_valid()` keep
their current arithmetic and every blob already on a memory card still reads.

## The bands

Three per category, in one 1..99 range:

```
NAUTICAL
  01-59  GN_ANY      boat, dock, harbor, sea, ocean, wharf, submarine
  60-79  GN_FANTASY  sailing ship cabin, tall ship deck, galleon interior
  80-99  GN_MODERN   submarine interior, cargo ship deck, steel ship hull
```

A neutral band is the point of the design. Most art has no period — a forest is
a forest — and forcing a genre onto all 309 accepted pictures would mean
re-triaging every one of them and would halve every pool to buy nothing. Only
nouns that genuinely carry a period get tagged; everything else stays neutral
and serves every game, as it does today.

Selection reads the game's resolved genre, tries that band, and falls back to
`GN_ANY`. Fallback is not an error path: it is what most rooms in most games
will do, because most bands will be empty for most moods.

Band boundaries are per category, not global. A mood with no period problem
declares one `GN_ANY` band spanning 1..99 and behaves exactly as it does now.

## Where a picture's genre comes from

`art_queries.json` gains an optional `noun_genre` map per mood:

```json
"NAUTICAL": {
  "noun_genre": {
    "sailing ship cabin": "FANTASY",
    "tall ship deck":     "FANTASY",
    "galleon interior":   "FANTASY",
    "submarine interior": "MODERN",
    "cargo ship deck":    "MODERN",
    "steel ship hull":    "MODERN"
  }
}
```

An untagged noun is neutral, so the map stays short and the default is the
current behaviour. `make_tga.py` already walks `png/<MOOD>/**`, and a file's
noun is its parent directory name, so it can bucket by noun without reading the
manifest or moving a file.

This is why the qualified nouns landed first (`f7baec8`). `sailing ship cabin`
and `submarine interior` are separate directories already; the tag names an
existing directory rather than introducing a taxonomy.

## Unresolved genre

`GAME_GENRE[]` covers the listed stories and resolves at load. Two on the disc
are absent from it — `INFOSAM5` and `INFOSAM7` — and `room_class.c` infers
their genre from text markers behind a hit-count and lead threshold, so
`g_genre` is 0 for some opening stretch and `g_genre_lock` is 0 with it.

Art follows the classifier's existing rule rather than inventing one: while
genre is unresolved, draw from `GN_ANY` only. `room_class.h` states the
principle for keywords — "guessing puts sailing-ship art on a starship" — and
it holds identically here. A game whose genre later locks starts drawing its
band from that point; pictures already shown are not revisited.

## Curation consequence

The 99-per-mood target has never been an aesthetic goal. It is `SLOT_STRIDE`:
an index must fit two digits inside one category. Under bands the neutral pool
target falls to its band width, and a split mood needs pictures in two or three
bands rather than one.

Present counts make the near-term cost small. `CATEGORY_ART_N` is
`{0, 25, 11, 22, 9, 7, 11, 21, 10, 3, 12, 16, 9, 0, 0}` — NAUTICAL has 9
pictures, TOWN 7, SCIFI 3, against a 99 ceiling. Nothing is close to binding,
and a 59-wide neutral band will not bind for a long time.

The real cost is review hours: NAUTICAL's 17 accepted pictures become a neutral
pool plus two thin period pools, and the period pools start near empty. That
cost is paid in fetching and reviewing, not in code, and it is the reason to
tag few nouns rather than many.

## Risks

`CATEGORY_ART_N` currently guarantees that a count can never name a picture the
disc lacks, because `make_tga.py` generates it from the files that actually
converted. Banding must preserve that invariant per band, not per category. A
band whose count is right but whose run is gapped hands `display_slot_make()`
an index with no file behind it, and the failure is a missing picture at
runtime rather than a build error.

Rotation must stay inside its band. `display_rotate_dynamic_category()` walks
`(g_cat_rot[cat] + 1) % n` and relies on every index 1..n being real; with
bands, `n` is the band's width and the walk needs the band's base added back.
Getting this wrong shows the wrong genre's art rather than crashing, which is
exactly the class of defect this design exists to remove — so it wants a test
that asserts a fantasy game never resolves a modern slot.

`display_shuffle_category()` is called by the title screen to pick a different
house each boot. It reduces `r` modulo the pool size and must reduce modulo the
band instead, or the title screen will point the pool outside the band that
selection then reads.

## Verification

Host-side tests over `make_tga.py` for band assignment, per-band counts, and
the gapless-within-band invariant.

C-side, the arithmetic is pure: `display_slot_make()`, the banded rotate and
the banded shuffle are testable without hardware, and the assertion that
matters is that resolving a slot for a `GN_FANTASY` game never yields an index
inside a `GN_MODERN` band.

Both need asymmetric fixtures. A category whose three bands are the same width,
or whose counts coincide, cannot distinguish a band lookup from a category
lookup — the failure this project keeps repeating.
