# Design: sentence shape from the story's own grammar, and a word-list tab strip

**Date:** 2026-09-06
**Status:** Proposed

## Goal

Make the command panel offer the sentence the story's parser actually accepts,
and give the player a way to overrule it when it guesses wrong.

The panel builds one fixed shape today -- `VERB NOUN [PREP NOUN2]` -- walked by a
switch in `cp_pick` (`saturn/src/input/command_panel.c:158`), with the branch
decided by `cv_verb_wants_prep` (`saturn/src/video/command_view.cxx:1232`), which
answers "does this verb have any `TYPE_PREP` transition at all". That is the wrong
question, and it has two consequences: a preposition can never precede the first
noun, so `look at lamp` cannot be built at all; and a verb that takes a
preposition in one syntax line opens a preposition slot in every one.

## What the story already carries

`build_typeahead_from_story` decodes the v3 verb grammar table
(`saturn/src/input/typeahead_extract.c:585-612`) and reads bytes 1-4 of each
8-byte syntax row into two flat bags -- `preps[]` and `dattrs[]` -- discarding
byte 0 and the pairing between the rest. `tools/typeahead/gen_typeahead.py:236`
drops the same byte (`rows.append((r[1], r[2], r[3], r[4]))`).

Byte 0 is the row's object count, and with the two preposition bytes it is the
whole sentence shape. Parsed across the twenty v3 stories under
`saturn/cd/data/Z3/`, Zork I has **125 verbs, 246 syntax rows, and exactly six
distinct shapes**:

| `(nobj, prep1, prep2)` | sentence | buildable today |
| --- | --- | --- |
| `(0,0,0)` | `look` | yes |
| `(1,0,0)` | `take lamp` | yes |
| `(1,1,0)` | `look` + `at` + `lamp` | **no** |
| `(2,0,0)` | `give troll sword` | no |
| `(2,0,1)` | `put lamp` + `in` + `case` | yes |
| `(2,1,1)` | `dig` + `in` + `sand` + `with` + `shovel` | **no** |

**76 of Zork I's 246 rows (31%) put a preposition before the first object.** That
is the missing-particle case, and it is a third of the game's grammar.

Worst case across all twenty stories: **184 verbs, 364 rows**.

## Non-goals

- No parser. Nothing here decides whether a sentence is *right*, only which
  words the panel offers next; the story's own parser still answers.
- No change to how candidate words are sourced. `cv_build_verb_cands`,
  `cv_build_noun_cands` and `cv_build_prep_cands` are reached by a new route and
  are otherwise untouched.
- No change to the on-screen keyboard's typeahead, the solution overlay, or the
  trie's ranking.
- No v4+ grammar. The extractor is v3-only (`typeahead_extract.c:450`) and so is
  this.

## Section 1 -- the shape module

New files `saturn/src/input/sentence_shape.c` / `.h`, beside typeahead because it
is the same concern: turning a story's parser tables into what the panel offers.

### It copies rather than re-reads

The story bytes are not reliably resident after the trie is built. The main game
holds them (`saturn_story_data`), the NETBIN build holds a `.rodata` blob, but
the online CD path **frees** its buffer as soon as the trie exists
(`saturn/src/net/online.cxx:329`). A design that re-walks the story at slot time
would work in two paths and silently offer nothing in the third. So the table is
built when the trie is built, owned, and freed beside it.

### Data

    typedef struct {
        unsigned char nobj;    /* syntax row byte 0: 0, 1 or 2 objects */
        unsigned char prep1;   /* byte 1: dictionary id, 0 for none */
        unsigned char prep2;   /* byte 2 */
        unsigned char attr1;   /* byte 3 */
        unsigned char attr2;   /* byte 4 */
    } ShapeRow;

- one flat `ShapeRow` array, all verbs' rows end to end
- an index: verb dictionary id -> (first row, count)
- the 256-entry canonical-preposition table (`DictionaryWord*` per id) that
  `typeahead_extract.c:551-566` already computes and currently throws away when
  the build ends

Estimated at **about 3.6 KB** when this was written; **measured at 9,992 bytes**,
one `TYPEAHEAD_MALLOC` block, against a HWRAM C heap of roughly 194 KB. The
estimate was wrong in two ways, both found during implementation. The verb index
is keyed by *spelling* and synonyms share a dictionary id, so it holds 384 entries
rather than 184 -- LEATHERG.Z3 alone has 359. And `ShapeVerb` pads to 14 bytes,
not the 12 its fields add up to.

Nothing of this lands in the image: the table is malloc'd, so it costs no `.bss`
and does not move `__heap_start`. What it costs is runtime heap that the story and
the trie are already competing for, which is a thing only a build can measure.

### Interface

    #define SHAPE_PREP_MAX 12   /* one verb's rows cap at 12, so at most 12 preps */

    enum { SHAPE_PREP, SHAPE_NOUN, SHAPE_END, SHAPE_FREE };

    typedef struct {
        int kind;
        DictionaryWord *prep[SHAPE_PREP_MAX];
        int nprep;
    } ShapeSlot;

    void shape_build(const unsigned char *story, unsigned int len, TrieNode *root);
    void shape_destroy(void);
    void shape_next(const char *const *picked, int npicked, ShapeSlot *out);

### The matching rule

`shape_next` matches the words picked so far against the verb's rows, position by
position; a row that disagrees drops out. What survives decides the next slot:

- all survivors want a preposition here -> `SHAPE_PREP`, `prep[]` being their
  union in trie-weight order
- all want an object -> `SHAPE_NOUN`
- all are exhausted -> `SHAPE_END`, and the command completes
- survivors disagree (some want a preposition, some an object) -> the union is
  offered with the prepositions first, and the player's pick resolves it
- no rows survive -> `SHAPE_FREE`

`SHAPE_FREE` is reached three ways, and all three fall back to today's fixed
chain rather than to an empty module:

1. the verb has no grammar entry, or its entry failed the `1 <= nrows <= 12`
   sanity check the extractor already applies
2. a tab override took the sentence off-grammar
3. **Hard difficulty**, where `ensure_typeahead` builds no trie at all
   (`saturn/src/engine/saturn_glue.cxx:198`) and the noun list already comes from
   `cv_build_dict_nouns`. `shape_build` is not called on Hard.

## Section 2 -- the panel's slot chain

`CP_SLOT_VERB .. CP_SLOT_DONE` is a fixed order and `wants_prep` is a bool,
because the old chain had exactly one place a preposition could sit. Both
generalise:

- `cp_pick(p, word, wants_prep)` becomes `cp_pick(p, word, next_kind)`, the kind
  coming from `shape_next`.
- `p.slot` becomes a **kind** (VERB / PREP / NOUN / DONE) rather than a position,
  since preposition and noun can each occur twice in one sentence.
- `slot_cursor[]` and `slot_top[]` stay indexed by **position**, not kind, so the
  second noun does not inherit the first noun's remembered row. The longest shape
  is `(2,1,1)` -- verb, prep, noun, prep, noun -- so there are five positions,
  not the four the current `slot_cursor[CP_SLOT_DONE]` holds.
- `cp_load_line` derives the slot from the word count today; it re-derives by
  replaying the same match, so Back still unwinds a recalled command correctly.
- `cv_verb_wants_prep` is **deleted**. It exists only to answer the wrong
  question.

`command_view.cxx` and `saturn/tests/test_command_panel.c` are the only callers
of the changed API.

## Section 3 -- the word module's three zones

The module is one 5 x 2 grid today with the cursor a single index into it. It
becomes three zones, held in a new `zone` field on `CommandPanel`
(`ZONE_TABS`, `ZONE_LETTERS`, `ZONE_LIST`), the cursor meaning whatever the zone
it sits in says it means.

### Tab strip -- row 0

The module draws into rows 1-5 of a seven-row band (`CV_LIST_ROW0 1`,
`command_view.cxx:78`), so row 0 of that column is already blank and the strip
costs the rose and the command module nothing.

Layout, in the 14 columns the two 7-character fields occupy
(`CV_WORD_X + 1`, `command_view.cxx:823`):

    row0   Vb Nn Pr AZ
    row1   lamp    sword
    row2   rope    door
    row3   sack    leafle
    row4   ...     ...
    row5   ...     ...

The tab the shape module chose is drawn bright and the others dim -- the same
convention `cv_draw_word_row` uses for the cursor.

Reaching it costs no new button. `cp_word_move(dy = -1)` at row 0 with `top == 0`
falls through today and does nothing (`command_panel.c:412-419`); that dead press
enters the strip. Left/Right walks the tabs, and at the strip's two ends returns
-1 / +1 exactly as the list's column edges do, so focus carries out to the travel
and command modules by the rule already in force. Down, or a pick, drops back
into the list.

**An override lasts one slot.** The next `cp_pick` releases it and the panel
resumes choosing the tab from the shape.

### The dispatch inverts

`cv_refill_words` switches on `p.slot` today (`command_view.cxx:694-702`). It
switches on **tab** instead, and the slot's only remaining job is to choose the
default tab. `cv_build_verb_cands` / `cv_build_noun_cands` /
`cv_build_prep_cands` are then reached by either route unchanged, which is most
of why the tabs are affordable: they need no new sourcing code.

### A-Z

Selecting the fourth tab splits the module -- rows 1-2 become a 13-wide letter
grid, rows 3-5 the matching words, six per page:

    row0   Vb Nn Pr [AZ]
    row1   ABCDEFGHIJKLM
    row2   NOPQRSTUVWXYZ
    row3   lamp    lanter
    row4   large   leafle
    row5   ledge   letter

The letter under the cursor filters live, so walking the alphabet re-fills the
list beneath it with no second press; Down or Type drops into the words. The list
is the **whole story dictionary with no type filter** -- that is what makes it the
escape hatch when the extractor typed a word wrong or left it `TYPE_UNKNOWN`.

Two consequences:

- `CP_WORD_ROWS` stops being a constant the window arithmetic may assume.
  `cp_word_rows`, `cp_top_max`, `cp_fill` and `cp_word_move` take the visible row
  count (5 normally, 3 under A-Z) as a parameter rather than reading the macro.
- `cv_cache_stale` gains the tab and the letter in its key, or the list under a
  moving letter cursor never refills.

Display truncation is unchanged: `cv_truncate_all` still cuts cells to the six
characters a v3 dictionary entry distinguishes, and `cv_submit_form` still
recovers the full spelling from the room when the word names an object there and
sends it as it stands when it does not.

## Data flow, one prompt

    story bytes --> build_typeahead_from_story --> trie
                |
                +-> shape_build --------------> ShapeRow table + prep canon

    per pick:  p.line --> shape_next --> ShapeSlot.kind --> default tab
                                                        |
                         player override (tab strip) ---+
                                                        |
                                                        v
                                     cv_refill_words dispatches on tab
                                                        |
                                     cv_build_{verb,noun,prep}_cands or A-Z filter
                                                        |
                                     cv_reorder -> cv_truncate_all -> cp_fill

## Testing

Host tests, no device, run through `test.bat`.

Against the real grammar -- `saturn/tests/` already loads `ZORK1.Z3` directly
(`test_netbin_typeahead.c:20`, `test_room_model_static.c:36`):

- after `look`, the next slot is `SHAPE_PREP` and offers `at`
- after `look at`, the next slot is `SHAPE_NOUN`
- `put` offers no preposition in the first object's slot and offers `in` in the
  second's
- every one of Zork I's 246 rows resolves to one of the six shapes, and the
  76-row prep-before-first-object count holds
- a verb absent from the grammar table yields `SHAPE_FREE`

Against synthetic rows:

- survivors disagreeing -> union offered, prepositions first
- an override that takes the sentence off-grammar -> `SHAPE_FREE`, and the panel
  falls back to the fixed chain rather than emptying

Panel tests (`test_command_panel.c`, which this design breaks deliberately):

- the three zones and the cursor's meaning in each
- Up at row 0 with `top == 0` enters the strip; with `top > 0` it still scrolls
- the strip's ends carry focus to travel and command
- an override releases on the next pick
- the window arithmetic with 3 visible rows as well as 5

## Risks and budgets

| Risk | Handling |
| --- | --- |
| ~3.6 KB of new persistent heap against ~194 KB | Measured on the real build in the plan, not assumed. Freed with the trie. |
| ~~Netbin image growth, and there is almost no room~~ **RESOLVED -- the alarm was false** | `test_netbin_budget.py` enforces a 300 KiB (307,200 B) ceiling and a 32 KiB headroom floor, so the budget is **274,432 B**. This spec claimed the image stood at 272,688 B leaving 1,744 bytes of slack; that figure was lifted from commit `ea31947`'s message and was already stale when quoted, since `939f37d` afterwards took about 19,712 bytes off the netbin. **Measured with the work in: 260,736 B, 13,696 bytes of headroom, both budget tests passing.** No fallback was needed. The lesson is the one the plan's Task 1 existed to prevent: a baseline quoted from a commit message is not a measurement. |
| `test_command_panel.c` breaks by design | Rewritten in the same change, not deferred. |
| A story whose grammar decodes badly | Already guarded by the `1 <= nrows <= 12` and static-base range checks. A verb failing them is `SHAPE_FREE`, which is today's behaviour. |
| Three zones make the cursor harder to reason about | The zone is one field with one meaning per value, and every zone transition has a test. |

## Files touched

- `saturn/src/input/sentence_shape.c` / `.h` -- new
- `saturn/src/input/typeahead_extract.c` -- hand the canonical preposition table
  and the syntax rows to the shape module instead of discarding them
- `saturn/src/input/command_panel.c` / `.h` -- slot kind, zone, tab, letter,
  parameterised window arithmetic
- `saturn/src/video/command_view.cxx` -- tab strip and letter grid rendering,
  dispatch on tab, `cv_verb_wants_prep` deleted, cache key extended
- `saturn/src/engine/saturn_glue.cxx`, `saturn/src/net/online.cxx` -- build and
  destroy the shape table beside the trie in all three paths
- `saturn/makefile` -- the new source
- `saturn/tests/test_command_panel.c` -- rewritten; new shape tests added
- `tools/typeahead/gen_typeahead.py` -- keep byte 0 in `parse_grammar` so the
  host dump shows the same shapes the device builds

## Open items

- Whether the A-Z list should page by six or grow the word rows when the letter
  grid is not being walked. Six is specified; the plan may find five rows of
  words with a one-row letter strip reads better on a 320-wide display.
- The strip's exact labels. `Vb Nn Pr AZ` fits the 14 columns with room to
  spare; fuller words do not.
