# Room Categorization — Tiers, Spatial Scope and Genre — Design

**Date:** 2026-08-04
**Status:** Designed

## Goal

`text_classify_room()` picks the mood category that drives both the CD-DA track
and the background picture. It currently counts flat keyword hits, one point
each, and takes the highest. That model loses two ways the player can see:

- **Scenery beats containment.** A landmark merely visible from a room outvotes
  the room itself. A lake seen from a cave makes the wallpaper water.
- **Genre collisions.** A word means different things in different games. "ship"
  is nautical in Zork and a spacecraft in Starcross, and the flat table can only
  hold one answer, so a space game gets sailing-vessel art.

This design replaces the flat count with a three-part ranking — keyword tier,
sentence scope, genre — and lands a golden-corpus regression test built from the
real room text of the games on the disc.

## Scope

In:

- A new `saturn/src/classify/` module owning classification; `music.c` keeps
  only its cache and playback.
- A **tier** on every keyword (Structure / Biome / Feature) with **dominant**
  comparison: a higher tier wins outright, regardless of count.
- **Sentence-scoped spatial modifiers**: a sentence with a negative phrase has
  its hits discarded; a positive phrase doubles its hits.
- A **genre mask** on every keyword, so ambiguous words vote differently per
  game. Genre comes from an authored per-game table, falling back to runtime
  inference for games not in it.
- A corpus generator that drives host mojozork with the shipped walkthroughs and
  emits real room text as C fixtures, plus the host test that consumes them.

Out:

- **No per-room category maps.** `text_game_room_category()` stays the empty
  stub it is. Genre is one byte per game; a room map is hundreds per game.
- **No separate classifier for art.** Music and the picture keep sharing one
  `TC_*` verdict. Forking them means two keyword tables drifting apart.
- **No new categories.** `TEXT_NUM_CATEGORIES` stays 15. Every change here is
  about *which* existing category wins.
- **No change to the category → track or category → picture pools.**

## Architecture

```
                    music.c
                       |  music_on_turn(): room changed?
                       v
              room_class_room(text, title)          saturn/src/classify/room_class.c
                       |
       +---------------+---------------+
       |               |               |
   genre gate     sentence split    tier scoring
       |               |               |
       +---------------+---------------+
                       v
                 winning TC_*  ->  g_room_cache[room]
                                        |
                          +-------------+-------------+
                          v                           v
                  music_category_track()      display_category_image()
```

`room_class.c` is the logic and stays under ~300 lines. `room_class_data.c` is
the tables — keywords, genre markers, per-game genres, modifier phrases — and is
meant to be edited freely, matching how `music_data.c` splits from `music.c`
today.

`music.c` loses `text_classify_room()`, `text_scan_event()`, `text_room_title()`,
`has_word()` and `TEXT_TITLE_*`. Event scanning moves with room scanning because
both derive a `TC_*` from text and both need `has_word()`; leaving one behind
would mean two copies of the matcher. `music.c` keeps `music_note_room_title()`
(the interpreter hands the authoritative room name in there) and forwards the
stored title into `room_class_room()`.

Event keywords are not tiered, scoped or genre-gated — `text_scan_event()` moves
unchanged and keeps its first-match-wins behaviour. It fires on any turn, not
just a room change, so sentence splitting would cost far more there for no
benefit.

### Data model

```c
typedef enum { KT_STRUCTURE = 0, KT_BIOME = 1, KT_FEATURE = 2 } KeywordTier;

#define GN_FANTASY 0x01
#define GN_SCIFI   0x02
#define GN_MODERN  0x04
#define GN_ANY     0xFF

typedef struct {
    const char   *word;
    unsigned char cat;    /* TC_* */
    unsigned char tier;   /* KT_* */
    unsigned char genre;  /* mask of genres this entry votes in */
} TextKeyword;
```

Two bytes added to each of ~110 entries, about 220 bytes of extra ROM, nothing
on RAM.

**Tier is a property of the word's reliability, not of the category it votes
for.** `cave`, `tunnel`, `chamber`, `house`, `ship`, `corridor` are Structure;
`forest`, `desert`, `mountain`, `sea`, `wasteland` are Biome; `tree`, `boulder`,
`rug`, `desk`, `altar`, `scroll` are Feature. Several existing entries change
category as a consequence — `altar` is a Feature and should not by itself drag a
room to `TC_DUNGEON`.

### Scoring

Each category accumulates a count **per tier** — three counters, not one total.
Its score is the pair `(best tier it scored in, count at that tier)`, compared
lexicographically:

1. Better tier wins outright. One Structure hit beats any number of Biome hits,
   which beat any number of Feature hits. "A lake in a cave is a cave" is true by
   construction, not by tuning a weight.
2. Same tier → higher count wins.
3. Still tied → higher total count across all tiers.
4. Still tied → lowest `TC_*` id, which is what today's strict `>` does
   implicitly. Preserved so a tie never becomes order-dependent.

`TC_NEUTRAL` is still the nothing-matched answer and is never a candidate; the
scan runs `TC_WILDERNESS .. TC_PLACE_LAST` as it does now.

**Title weight.** `TEXT_TITLE_WEIGHT` survives but now multiplies the count
*within* a tier instead of a flat score, so it can no longer promote a Feature
word past a Structure word. That is a deliberate weakening of the current
behaviour: "Up a Tree" can no longer outvote the forest around it. Under
dominant tiers `tree` is a Feature and loses to any Biome word in the
description, which is the better answer and the one the current comment says
weight 2 was a compromise to allow.

### Sentence scope

The description splits at `.`, `!` and `?`. The title is its own segment and is
always treated as positive.

```c
static const char *const NEG[] = { "in the distance", "far off", "through the window",
                                   "painted on", "on the horizon", "beyond the",
                                   "you can hear" };
static const char *const POS[] = { "you are in", "you are standing", "you stand",
                                   "this is a", "you find yourself", "you are on" };
```

A sentence containing a `NEG` phrase contributes nothing. A sentence containing
a `POS` phrase contributes double count. Doubling only breaks ties *within* a
tier, so a positive sentence cannot promote a Feature past a Structure — "You are
in a room with a rug" does not vote rug.

Both lists are deliberately short. `"you can see"` is excluded despite being a
distance idiom, because it also introduces genuinely present objects far more
often than distant ones.

Cost is one extra pass over at most `MUSIC_TEXT_MAX` (512) bytes, once per room
change, alongside a CD read. Irrelevant.

### Genre

`GAME_GENRE[]` in `room_class_data.c`, keyed by release + 6-char serial — the
same key `text_game_room_category()` and the typeahead `SOLUTIONS[]` table use.
One row per shipped game, one genre bit each.

Only ambiguous words need splitting into per-genre rows: `ship`, `deck`, `cabin`,
`hull`, `panel`, `hall`, `study`, `chamber`, `module`. Everything else carries
`GN_ANY` and the field is a mechanical addition.

```c
{"ship", TC_NAUTICAL, KT_STRUCTURE, GN_FANTASY|GN_MODERN},
{"ship", TC_SCIFI,    KT_STRUCTURE, GN_SCIFI},
```

**Unknown games.** Genre starts unresolved. While unresolved, an entry whose
mask is not `GN_ANY` **does not vote at all** — "ship" abstains rather than
guessing, and the room falls through to its other evidence instead of being
confidently wrong. A small `GENRE_KW[]` marker table (`airlock`, `hyperspace`,
`android` → sci-fi; `spell`, `elf`, `troll` → fantasy; `telephone`, `elevator` →
modern) accumulates across rooms. The genre locks when one has at least **3**
marker hits and leads every other genre by at least **2**, and never before the
third room has been classified. Once locked it never changes again for that
session. Both constants live in `room_class.h` as named defines.

**Cache invalidation.** `music.c` memoizes each room's verdict in
`g_room_cache[room]`. Every entry cached before a genre locks was computed with
ambiguous words abstaining. The lock must therefore:

1. clear `g_room_cache` entirely, and
2. re-classify the *current* room and re-announce its category, so the picture
   and the track catch up in the same turn.

Authored games resolve their genre at load and never take this path, which is
exactly why the bug would survive testing on the shipped disc.

## Testing

### Corpus generator

`tools/gen_room_corpus.py`, mirroring `tools/typeahead/gen_all.ps1`:

1. Build host mojozork once with `gcc` (`saturn/mojozork.c` already has a
   `main()` that loads a story file and reads commands from stdin).
2. Discover `saturn/cd/data/Z3/*.Z3` and pair each with
   `tools/typeahead/solutions/<STEM>.WIN`, skipping any game with no non-empty
   walkthrough — the same rule `gen_all.ps1` uses.
3. Pipe the walkthrough in, capture output per command, keep the moves that
   produced a room entry, and dedupe by title.
4. Emit `test/corpus/rooms.inc`: a C array of
   `{ release, serial, title, description }`. **The generator never assigns an
   expected category** — it has no classifier and does not link one. It captures
   text only.

Expected verdicts live in a second generated file, `test/corpus/blessed.inc`,
written by `room_class_test --bless`. That keeps the two concerns apart: the
generator owns *what the games say*, the test owns *what we have agreed the
answer is*. A room present in `rooms.inc` with no row in `blessed.inc` fails the
test as unblessed rather than silently passing.

Both files are checked in. The generator is re-run when a new `.Z3` lands on the
disc, not on every build.

### What the test asserts

`test/room_class_test.c`, built the same single-`gcc`-line way as the other host
tests, includes `rooms.inc` and runs two suites:

- **Snapshot.** Every room in `rooms.inc` is classified and compared to its row
  in `blessed.inc`. Any diff fails the test, printing room title, blessed
  category and new category. Re-blessing is a deliberate `--bless` run with a
  reviewable diff, so every future keyword edit shows its blast radius across
  ~150 rooms.
- **Assertions.** A small hand-written set of rooms that are *wrong today* and
  must flip:
  - West of House → `TC_HOUSE`, not wilderness.
  - Reservoir / Stream View → the containing structure, not the distant water.
  - Zork II's boat rooms → `TC_NAUTICAL`.
  - Synthetic sci-fi fixtures asserting `ship`/`deck`/`hull` resolve to
    `TC_SCIFI` under `GN_SCIFI` and `TC_NAUTICAL` under `GN_FANTASY`.
  - An unresolved-genre fixture asserting `ship` abstains rather than voting.
  - A genre-lock fixture asserting the room cache is flushed and the category
    re-announced.

These are the cases the change exists for; the snapshot is what proves it did not
break the other 140 rooms on the way.

### Known coverage gap

Only `ZORK1.Z3`, `ZORK2.Z3` and `ZORK3.Z3` are on the disc today, and all three
are fantasy. **The corpus cannot exercise the genre layer against real text
until a sci-fi story file is present.** `PLNTFALL.WIN`, `STARCROS.WIN`,
`STATFALL.WIN`, `SUSPENDD.WIN` and `HITCHHKR.WIN` are already in
`tools/typeahead/solutions/`, so the generator will pick those games up
automatically the moment their `.Z3` lands. Until then the genre path rests on
the synthetic fixtures above, and that is a real limit on confidence, not a
formality.

### Existing tests

`test/music_category_test.c` and `saturn/tests/test_display.c` both exercise the
category pipeline and may shift when the scoring changes. Both are reviewed and
updated as part of the work; a changed expectation there needs the same
justification as re-blessing a snapshot row.

`saturn/tests/test_category_art.py` is unaffected — it validates the picture
table against the disc and knows nothing about how a category is chosen.

### Order of work

The snapshot must be blessed **against the current classifier, before any
scoring change lands**. Blessing after the rewrite records the new behaviour as
the baseline and the suite proves nothing — it would pass on day one no matter
what the rewrite did.

So: extract the existing classifier into `saturn/src/classify/` unchanged, build
the corpus, bless it, confirm the suite is green on old behaviour. Only then
change the scoring, and read the resulting diff as the actual review artifact of
this work.

## Risks

- **The snapshot bakes in current mistakes.** A room that is wrong today and not
  in the assertion set gets blessed as correct. Mitigated by reading the
  first-generation diff rather than accepting it blind, and by the assertion set
  covering the known-bad rooms.
- **Dominant tiers are unforgiving by design.** One mis-tiered keyword can swing
  every room containing it, where under flat counting it was one vote among
  many. The snapshot is what makes that visible immediately.
- **Genre inference can lock wrong** on a game that opens with misleading
  vocabulary. Bounded by the lock margin and by authoring a `GAME_GENRE[]` row
  for anything shipped.
- **Music changes with the art.** Every category improvement retunes track
  selection too. That is intended, but it means a bad tier assignment is audible
  as well as visible.
