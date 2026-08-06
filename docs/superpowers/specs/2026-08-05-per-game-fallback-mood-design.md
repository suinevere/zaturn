# Per-Game Fallback Mood — Design

**Date:** 2026-08-05
**Status:** Implemented 2026-08-05
**Follows:** `2026-08-04-room-categorization-tiers-design.md`

## Goal

295 of the 1024 rooms in the game library classify as `TC_NEUTRAL` — 29%. That
category has no picture pool (`CATEGORY_IMAGE[TC_NEUTRAL] = { 0, 0 }`), so
`display_set_dynamic_category` holds whatever was already on screen. The player
walks into those rooms and the wallpaper does not change.

Separately, three mood categories are structurally unreachable. Measured from the
keyword table:

```
category     Struct  Biome  Feat
MAGIC             0      0     8
HORROR            0      0     7
MYSTERY           2      0     6
```

Every `MAGIC` and `HORROR` keyword names a *thing* — `spell`, `wizard`, `corpse`,
`stench` — and under the tier rules a thing always loses to a place. They win 5
and 2 rooms out of 1024. Their 3 and 4 pictures effectively never appear.

Both problems have one answer: **give each game a default mood, and show it when
the room text says nothing for long enough.** A run of featureless rooms in
Lurking Horror becomes horror; in Sorcerer, magic; in Starcross, sci-fi. The
starved categories are rescued by authorship rather than by weakening the tier
rules that make a lake in a cave a cave.

## Scope

In:

- A `fallback` column on `GAME_GENRE`, one `TC_*` byte per shipped game.
- A consecutive-unclassified-room counter in the music engine, and the
  substitution of the game's fallback once it reaches `MUSIC_FALLBACK_ROOMS`.
- A synthetic room-sequence test for the run behaviour, and the corpus
  byte-identity invariant that proves the classifier was not touched.

Out:

- **Image expansion.** Sourcing art, raising `DISP_IMAGE_MAX` (currently 40, with
  38 TGAs already on the disc — two free slots), and re-verifying the LWRAM cache
  budget are a separate project. It is deliberately sequenced *after* this one:
  until the fallback ships, nobody knows which pools carry real weight. Today
  `HORROR` wins 2 rooms and 4 pictures is plenty; afterwards it may be carrying a
  whole game.
- **New keywords.** The vocabulary gap is real — `room` alone appears 220 times
  in the NEUTRAL set — but it is a separate change with its own snapshot churn.
- **Any change to `text_classify_room`.** The classifier stays a pure function of
  one room's text. See Architecture.
- **Runtime inference of a fallback** for unlisted games. 275 of the 295 NEUTRAL
  rooms are in authored games, so the table covers 93% of the problem; an
  unlisted game keeps today's sticky behaviour, which is a clean degradation
  rather than a guess.

## Architecture

The fallback lives in `music.c`, not in the classifier.

That is the load-bearing decision. Putting it in `text_classify_room` would be
fewer lines, but `music_on_turn` memoizes verdicts in `g_room_cache`, so the
fallback would be cached *as though it were a classification*. A room would lose
the ability to re-decide, and the corpus snapshot could no longer distinguish
"this text says nothing" from "this game prefers horror". The engine already owns
room-to-room state — `g_same_cat_rooms`, the debounce, the genre-lock latch — so
a neutral-run counter belongs beside them.

**The classifier is untouched by this work.** That yields a strong, cheap
invariant: `test/corpus/blessed.inc` must come out byte-identical. Any diff means
the fallback leaked into classification.

### Data

```c
typedef struct {
    unsigned short release;
    const char*    serial;
    unsigned char  genre;      /* GN_* mask, unchanged */
    unsigned char  fallback;   /* TC_* to show after a run of unclassified
                                  rooms; TC_NEUTRAL means this game has none */
} GameGenre;
```

One byte per row across 29 rows. A new accessor mirrors `text_game_genre`:

```c
unsigned char text_game_fallback(unsigned int release, const char* serial);
```

Returning `TC_NEUTRAL` for an unlisted game is what makes "no default" and "not
in the table" the same case, with no separate flag.

### Control flow

In `music.c`, beside the existing engine state:

```c
static int g_neutral_rooms = 0;   /* consecutive rooms that classified NEUTRAL */
static unsigned char g_fallback_cat = TC_NEUTRAL;   /* this game's default */
```

`music_set_game` already forwards to `room_class_set_game`; it gains one more
line beside it, `g_fallback_cat = text_game_fallback(g_release, g_serial);`.

`music_reset` does **not** clear `g_fallback_cat` back to `TC_NEUTRAL` --
that was the design's original mistake, and shipping it would have silently
undone the whole feature every time a game reached `music_reset` after
`music_set_game` had already run. `main.cxx` calls `music_set_game` at :578
and `music_reset` at :580, so anything the former derives, the latter must
rebuild or it is lost for the rest of the session. `g_release`/`g_serial`
deliberately survive `music_reset` for exactly this reason: `music_reset`
clears play state, then calls `room_class_reset()` followed by
`room_class_set_game(g_release, g_serial)`, `g_fallback_cat =
text_game_fallback(g_release, g_serial)`, and `g_genre_was_locked =
room_class_genre_locked()` -- re-deriving from game identity everything that
`music_set_game` had already set, rather than wiping it.

`GAME_GENRE` stays in `room_class_data.c` rather than moving to `music_data.c`.
It is already keyed by release + serial and already read through an accessor, so
adding a column costs nothing; splitting the table so the engine could own one
field of it would cost a second table keyed the same way, kept in sync by hand.
`music.c` already includes `room_class.h`.

Inside `music_on_turn`'s `room_changed` branch, after `base` is resolved and
cached:

- `base != TC_NEUTRAL` → `g_neutral_rooms = 0`.
- `base == TC_NEUTRAL` → `g_neutral_rooms++`; then if
  `g_neutral_rooms >= MUSIC_FALLBACK_ROOMS` and `g_fallback_cat != TC_NEUTRAL`,
  set `base = g_fallback_cat`.

Everything downstream is unchanged. The existing target comparison sees an
ordinary mood change, the debounce applies, and `notify_cat` tells both the art
and the track — so the picture and the music move together with no new plumbing.

```
room changed
     |
     v
classify -> base            cache stores the TRUE base, never the fallback
     |
     +-- base != NEUTRAL --> g_neutral_rooms = 0
     |
     +-- base == NEUTRAL --> ++g_neutral_rooms
                                  |
                                  +-- >= 3 and game has a fallback?
                                          |
                                          yes --> base = g_fallback_cat
     |
     v
existing target / debounce / notify path (art + track together)
```

### Constants

`MUSIC_FALLBACK_ROOMS` is **3**, matching `MUSIC_ROTATE_ROOMS`. Both answer the
same shape of question — how many rooms of one state before the engine acts — and
letting them disagree would be noise with no reason behind it.

Three is also what preserves the behaviour worth keeping. Today's hold-the-
previous-picture is not purely a bug: walking a short corridor between two cave
rooms keeps the cave art up, which reads as continuity. A threshold of 1 would
turn every isolated featureless room into two wallpaper changes where there are
currently none.

### Interactions

- **The cache holds the true verdict.** `g_room_cache[room]` stores `TC_NEUTRAL`,
  not the fallback, so re-entering a room behaves the same as the first visit and
  the corpus keeps its meaning.
- **Rotation still applies.** Once the fallback is the active category, the
  existing `g_same_cat_rooms` counter starts running, so a long dead zone rotates
  through that category's pictures after three more rooms rather than freezing on
  one. This is desirable and needs no new code.
- **Events are unaffected.** `TC_DANGER` / `TC_TRIUMPH` override `base` per turn
  and are scanned separately; the fallback only ever substitutes for `base`.
- **The per-room override still wins.** `text_game_room_category` is consulted
  first and short-circuits classification; a room it answers for is by definition
  not `TC_NEUTRAL`, so the counter resets. It is an empty stub today.
- **Genre lock is orthogonal.** The genre-lock cache flush re-classifies the
  current room; that path produces a fresh `base` and feeds the counter normally.
- **The fallback holds for the whole run.** It is recomputed each room but yields
  the same value, so a ten-room dead zone is one category change, not eight.

## The table

| Fallback | Games |
|---|---|
| `TC_UNDERGROUND` | Zork I, Zork II, Zork III, Mini-Zork I (×2 releases), Mini-Zork II, Adventure |
| `TC_MAGIC` | Enchanter, Sorcerer, Spellbreaker |
| `TC_SCIFI` | Planetfall, Stationfall, Starcross, Suspended, Hitchhiker's Guide, Leather Goddesses |
| `TC_MYSTERY` | Deadline, The Witness, Suspect, Moonmist, Ballyhoo |
| `TC_HORROR` | The Lurking Horror |
| `TC_DESERT` | Infidel |
| `TC_NAUTICAL` | Cutthroats, Plundered Hearts, Seastalker |
| `TC_HOUSE` | Hollywood Hijinx |
| `TC_TOWN` | Wishbringer |
| `TC_NEUTRAL` (none) | Hypochondriac |

Hypochondriac is left without a default deliberately. It is an obscure title and
guessing at its mood would be worse than keeping today's behaviour for it;
`TC_NEUTRAL` in the column is the honest way to say so.

Every other fallback names a category with a real picture pool. A fallback
pointing at a pool-less category would silently do nothing, so the art test
gains a check for it.

## Expected effect

275 of the 295 NEUTRAL rooms are in authored games, distributed as:

```
Stationfall 29   Adventure 28   Planetfall 26   Zork II 20   Sorcerer 19
Suspect 19       Zork I 16      Starcross 11    Mini-Zork II 10
Infidel 9        Mini-Zork I 9  Leather Goddesses 8   Plundered Hearts 8
Enchanter 7      Lurking Horror 6
```

**These are upper bounds, not predictions.** The fallback requires three
*consecutive* unclassified rooms, and the corpus is a static list with no
traversal order — it cannot say which NEUTRAL rooms are adjacent in play. The
real figure is lower and is not knowable from this data. The shape of the win is
right; the magnitude is not something this spec can honestly quantify.

## Testing

The corpus can prove one half of this and not the other, and the split drives the
test design.

**What the corpus proves.** That the classifier was not touched.
`test/corpus/blessed.inc` must be **byte-identical** after this change. This is
the primary regression gate and it is nearly free — no re-blessing, no diff to
review. A single changed row means the fallback leaked into classification.

**What the corpus cannot prove.** The run-length behaviour, because it has no
play order. That needs a synthetic sequence test driving `music_on_turn` through
a scripted room order, in the same shape as the harness that verified the
genre-lock cache flush. It extends `test/music_category_test.c`, which already
owns the question "does the engine announce the right category at the right
moment".

Cases:

1. **Two neutral rooms do not trigger.** Stickiness is preserved; no category
   change is announced.
2. **Three do.** The game's fallback is announced.
3. **The run holds.** Rooms four through ten announce nothing further — one
   change, not seven.
4. **A classified room resets it.** After a real category, the counter restarts
   from zero and three more neutral rooms are needed.
5. **A game with `TC_NEUTRAL` in the column never falls back**, however long the
   run.
6. **An unlisted game never falls back** — same path, different reason.
7. **Both subscribers hear it.** The announced category reaches the category
   callback, so art and track move together.
8. **`music_reset` clears the counter**, so one game's run cannot carry into the
   next.

`saturn/tests/test_category_art.py` gains one check: every `fallback` named in
`GAME_GENRE` must be either `TC_NEUTRAL` or a category with a non-empty picture
pool. A fallback pointing at an empty pool would be invisible at runtime — the
same class of silent failure that test already exists to catch.

## Risks

- **More wallpaper changes than today.** Every fallback firing is a change that
  previously did not happen. Three rooms of hysteresis is the mitigation; if it
  still feels busy in play, the constant is the dial.
- **One wallpaper change, two track changes.** `TC_NEUTRAL` has no picture pool
  (`CATEGORY_IMAGE[TC_NEUTRAL] = { 0, 0 }`), so the picture holds while
  `g_neutral_rooms` climbs -- but `TC_NEUTRAL` does have a real track pool,
  `P_NEUTRAL`, so the music moves to it as soon as the room's base category goes
  neutral. The fallback firing a few rooms later then moves the track a second
  time, while the wallpaper only ever makes its one move (holding, then jumping
  straight to the fallback's picture). A run of unclassified rooms is therefore
  audibly busier than it is visibly busier.
- **A wrong default is worse than none.** A game tagged with a mood that does not
  suit it will show that mood across its whole featureless middle. The table is
  the risky part of this change, not the code, and it is the part a build on real
  hardware will judge.
- **The magnitude is unverifiable before shipping.** Only play, or a
  traversal-ordered corpus, can say how often the fallback actually fires.
- **Music churn on long dead zones.** The fallback becoming the active category
  means the rotation counter starts, so a very long featureless stretch will
  rotate tracks within the fallback pool. Intended, but it is a behaviour change
  in a system with a history of track-thrash bugs.
