# Room Classification Vocabulary — Inflection and Place-Words — Design

**Date:** 2026-08-06
**Status:** Implemented 2026-08-06
**Follows:** `2026-08-05-per-game-fallback-mood-design.md`

## Goal

295 of the 1024 corpus rooms classify as `TC_NEUTRAL`. The per-game fallback now
covers runs of three or more, but shorter gaps still hold the previous
wallpaper, and a fallback is a worse answer than a correct one — it shows the
game's mood rather than the room's.

Two gaps account for half of those rooms, and neither is a scoring problem:

- **Inflection.** The matcher is whole-word only, so `passages` (34 rooms),
  `catacombs` and `tunnels` are missed although `passage`, `catacomb` and
  `tunnel` are all in the table. The table already works around this once, with
  `tree` and `trees` as separate rows.
- **Missing place-words.** Words that plainly name a place appear repeatedly in
  unclassified rooms and are simply absent: `ballroom` (14), `fireplace` (13),
  `closet` (11), `maze` (11), `canyon` (10).

This closes both. It does not touch the scoring rules.

## Scope

In:

- A trailing-inflection relaxation in `has_word_n` accepting `s` and `es`.
- Removal of the now-redundant `trees` keyword row.
- Three explicit compound rows: `hallway`, `passageway`, `pathway`.
- Fourteen new place-word rows.

Out:

- **Generic interior words.** `room` (220 occurrences in the NEUTRAL set),
  `wall`, `floor`, `door`, `exit`, `entrance` (15). These name a connection or a
  surface, not a mood. Voting them anywhere would be wrong everywhere else, and
  a room whose text offers only these is genuinely one the fallback should
  answer.
- **`storage` (11).** Splits between house closets and Stationfall's cargo bays.
  It wants the genre mechanism, not a flat row, and that is a separate change.
- **`well` (6).** Collides with the adverb — `has_word` would match "as **well**
  as" and vote water in any room containing that phrase.
- **`gate` (4).** Appears in prose more often than as a room name.
- **Event inflection.** `EV[]` keeps literal matching. The relaxation would newly
  match `jewels`, `chests`, `fires` and `grues`, about five hits, but events fire
  on every turn with first-match-wins semantics; changing that is unrelated to
  room classification.
- **Any change to tier, sentence-scope or genre scoring.** Only the matcher's
  boundary rule and the keyword table change.

## Architecture

### Inflection

`has_word_n` accepts a match when the character after the word is a non-letter.
The relaxed form adds one accepting case: the following characters are `s` or
`es`, and the character after *those* is a non-letter.

**This is a new function, not an edit to `has_word_n`.** `has_word` is defined in
terms of `has_word_n`, and `text_scan_event` calls `has_word` — so relaxing
`has_word_n` in place would silently inflect the event table too, which Scope
rules out. Instead:

- `has_word_infl(text, len, word)` carries the relaxed boundary test.
- `has_word_n` and `has_word` keep their present semantics untouched.
- `text_classify_room` calls `has_word_infl` in both its sentence pass and its
  title pass.
- `text_scan_event` keeps calling `has_word` and is unaffected.

The relaxation is a change to the boundary test, not a retry loop. A naive
implementation would re-scan the text once per candidate suffix; this scans once,
exactly as today. That matters because the classifier already walks ~130 keywords
across every sentence of the room text, on a machine where this runs beside a CD
read.

### Why no precedence machinery

An earlier draft of this design proposed a rule that an explicit keyword row
beats any inflection derived from another row, plus a mechanism to enforce it.
Checking the table showed the mechanism is unnecessary. Scanning all 128 rows for
words that are another row plus `s`, `es` or `way` finds exactly one: `trees`.

So:

- **`trees` is deleted.** `tree` plus the relaxation covers it. This removes the
  only case where two rows could both match the same surface form. Both voted
  `TC_WILDERNESS` at Feature tier, so the double-count was harmless, but it was
  double-counting.
- **`way` is not a general suffix.** Only `hallway`, `passageway` and `pathway`
  occur, so they become explicit rows with their own categories. Three rows cost
  less than a suffix rule plus an exception mechanism, and each gets to say what
  it actually means rather than inheriting.

After both, no derived form collides with any row, and precedence never arises.

### The compound rows

| Word | Rooms | Votes | Tier | Why not inherited |
|---|---:|---|---|---|
| `hallway` | 47 | `TC_HOUSE` | Structure | `hall` votes `TC_TOWN`, and its own comment says it is there for "words that are as much a public building or a settlement as a home". Every observed `hallway` is a domestic interior: Cutthroats' inn landing, Deadline's and Moonmist's mansions, Infidel's pyramid. |
| `passageway` | 18 | `TC_UNDERGROUND` | Structure | Same as `passage`; explicit only because `way` is not a general suffix. |
| `pathway` | 2 | `TC_WILDERNESS` | Feature | Same as `path`, likewise. |

### The place-words

Chosen by counting occurrences within the 295 NEUTRAL rooms. Every one names a
place or a fixture of one; none is a connector or a surface.

| Word | Rooms | Votes | Tier |
|---|---:|---|---|
| `ballroom` | 14 | `TC_HOUSE` | Structure |
| `fireplace` | 13 | `TC_HOUSE` | Feature |
| `closet` | 11 | `TC_HOUSE` | Structure |
| `maze` | 11 | `TC_UNDERGROUND` | Structure |
| `canyon` | 10 | `TC_WILDERNESS` | Biome |
| `shaft` | 8 | `TC_UNDERGROUND` | Structure |
| `tent` | 7 | `TC_DESERT` | Structure |
| `volcano` | 6 | `TC_WILDERNESS` | Biome |
| `pit` | 6 | `TC_UNDERGROUND` | Structure |
| `ledge` | 4 | `TC_WILDERNESS` | Feature |
| `tower` | 3 | `TC_TOWN` | Structure |
| `wharf` | 3 | `TC_NAUTICAL` | Structure |
| `cliff` | 2 | `TC_WILDERNESS` | Biome |
| `alcove` | 2 | `TC_UNDERGROUND` | Structure |

All were designed to carry `GN_ANY`, on the belief that none was genre-ambiguous
and that the one which was (`storage`) had been excluded rather than guessed at.

**Two of them turned out to be ambiguous, and were split during implementation.**
The corpus said so plainly once the words were in:

| Word | Split | Evidence |
|---|---|---|
| `shaft` | `TC_UNDERGROUND`·Structure for `GN_FANTASY\|GN_MODERN`, `TC_SCIFI`·Structure for `GN_SCIFI` | Zork's and Sorcerer's mine shafts read underground correctly, but Stationfall's ventilation shafts dragged five rooms — and two of those, `Computer Control` and `Shaft at Level Nine`, had already been *correctly* `TC_SCIFI` and were made wrong. A single reading was net-negative. |
| `maze` | `TC_UNDERGROUND`·Structure for `GN_FANTASY`, `TC_WILDERNESS`·Biome for `GN_MODERN`, **no sci-fi row** | Adventure's and Zork's mazes are caves; Hollywood Hijinx's and Moonmist's are hedge mazes in gardens. The absent sci-fi row is deliberate: both sci-fi occurrences are metaphors — Leather Goddesses' "maze of neon" and Hitchhiker's "maze of twisty little synapses" inside Arthur's brain — and abstaining is the right answer for a word used figuratively. |

The `maze` split goes further than a genre carve-out: for `GN_MODERN` it changes
**both category and tier**, not just which genre a row votes in. That is a
larger move than this spec authorised, and it is recorded here rather than left
in a code comment. It is justified by the evidence above — a hedge maze is a
garden feature, not a structure — but a reader should know it was a judgement
made against the corpus during implementation, not a decision this design made in
advance.

The lesson generalises past these two words: a word's genre-ambiguity is not
reliably visible before the corpus is run against it. Assuming otherwise in a
spec's table invites exactly this drift between document and code.

`tent` is worth noting: it gives `TC_DESERT` its **first Structure-tier word**.
That category previously had only Biome and Feature entries, so any indoor word
anywhere in an Infidel camp room outranked it.

`fireplace` is Feature rather than Structure deliberately — it is an object in a
room, not the room. That is enough to rescue a NEUTRAL room, because Feature only
has to beat nothing.

## Sequencing

Two commits, each with its own reviewable snapshot diff:

1. **Inflection.** The matcher change, the `trees` deletion, the three compound
   rows. Every moved room should be explainable as "we already had this word,
   now we match its plural or its compound".
2. **Vocabulary.** The fourteen place-words, against the baseline commit 1
   leaves behind.

Landing them together would mean ~150 rooms moving at once with no way to
attribute a surprise to the matcher or to the data without checking by hand. The
previous project split tiers from sentence-scope from genre for the same reason,
and each diff stayed interpretable because of it.

## Testing

**The snapshot diff is the deliverable.** `test/corpus/blessed.inc` will move by
roughly 150 rooms across the two commits. Reading those changes and judging each
is the work; the code is small. A change that cannot be justified is a signal to
fix the data, not to bless it.

Expected magnitudes, from measuring against the current corpus:

- Commit 1: ~44 rooms, dominated by `passages` (34) and the compounds.
- Commit 2: ~103 rooms.

**Both estimates were low, and the reason is worth carrying forward.** Actual
counts were 73 and 133. The estimates counted only rooms rescued *from*
`TC_NEUTRAL` (46 and 82, both close to prediction); the snapshot counts every
verdict change, including the 27 and ~51 rooms that moved between two categories
that were already real. Those between-category moves are the ones needing the
most judgement, and an estimate that omits them understates the review effort by
roughly half.

**Where the judgement is recorded.** The per-room reasoning lives in the task
reports under `.superpowers/sdd/2026-08-06-classification-vocabulary/`, which is
git-ignored — so it is not in the repository's history. What *is* durable is
`git log -p test/corpus/blessed.inc`: every verdict change is a committed diff
line, attributable to the commit that caused it. A future reader can see exactly
what moved and when, but not the argument for each. If that trail matters more
than it did here, the fix is to summarise the between-category moves in the
commit body — which this project's one-sentence commit rule currently forbids,
so it would be a deliberate exception rather than an oversight to correct.

Assertions to add to `test/room_class_test.c`, beyond the snapshot:

1. `passages` classifies as `passage` does — the plural relaxation works.
2. `caverns` does **not** match `cave` — the relaxation is trailing-only and must
   not become prefix matching, which would collapse distinct keywords.
3. `hallway` votes `TC_HOUSE`, not `hall`'s `TC_TOWN`.
4. A room naming only a tent in a desert classifies `TC_DESERT`, proving the new
   Structure word outranks the Biome and Feature words around it.
5. `text_scan_event` is unaffected: a turn containing `jewels` but not `jewel`
   raises no event.

`saturn/tests/test_category_art.py` and `test_netbin_sources.py` are unaffected.

## Risks

- **The relaxation is the risky half, not the data.** It changes how every one of
  ~130 keywords matches, everywhere. The `caverns` assertion exists because the
  obvious implementation error — relaxing the leading boundary too, or matching
  any trailing letters — would silently collapse `cave`/`cavern` and several
  other pairs the table distinguishes on purpose.
- **`pit` may be noisier than its count suggests.** It is short and appears in
  compounds the relaxation will not catch but prose might use loosely. If the
  snapshot shows it dragging rooms wrongly, drop it; it is worth 6 rooms.
- **Reduced fallback coverage is the point, not a regression.** Rooms that stop
  being `TC_NEUTRAL` no longer contribute to a fallback run, so some runs that
  reached three will no longer do so. That is the desired direction — a correct
  classification beats a game-level default — but it means the fallback's
  observed frequency will drop, and that should not be read as the fallback
  breaking.
- **Nothing here helps the ~60 genuinely featureless rooms** — Adventure's
  `Cul-de-Sac`, `Dead End Crawl`. They stay `TC_NEUTRAL` and stay the fallback's
  job.
