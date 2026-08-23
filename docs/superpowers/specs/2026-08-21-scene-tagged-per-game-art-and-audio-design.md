# Scene-Tagged Per-Game Art and Audio — Design

**Date:** 2026-08-21
**Status:** Proposed
**Supersedes:** `2026-08-10-genre-banded-art-pools-design.md`,
`2026-08-06-classification-vocabulary-design.md`,
`2026-08-04-room-categorization-tiers-design.md`,
`2026-08-05-per-game-fallback-mood-design.md`
**Follows:** `2026-07-30-dynamic-background-art-design.md`,
`2026-07-13-dynamic-room-audio-design.md`

## Goal

Delete the mood classifier. A room's picture and its music come from a scene
tag a human assigned to that specific room in that specific game, looked up by
the room's object number, and the pictures live in a folder belonging to that
one game.

Today a room's mood is inferred from the words in its printed text by a
1100-line keyword engine — three reliability tiers, per-genre bitmasks, a
memo cache, a neutral-run fallback counter, and a cache flush for when the
genre resolves late. Every one of those mechanisms exists to compensate for
the same root problem: the text is being asked a question it was never written
to answer. "Loud Room" says nothing about stone. "Cube" says nothing at all.

The judgement is cheap to make once, offline, by a human who knows the game,
and it never changes afterwards — a room's scenery is fixed at the moment the
story file was compiled. So make it once, store it, and look it up.

## Scope

In:

- A global scene vocabulary (32 tags) that every game maps into.
- Per-game room maps: object number to scene, authored offline, generated to C.
- Per-game image folders on disc; two games needing a forest each get their
  own forest.
- Per-game scene-to-track selection over the existing shared CD-DA tracks.
- An authoring pipeline: inventory, rule pass, review queue, blessed verdicts,
  generator.
- A web review tool for clearing the queue.
- Deletion of `room_class.{c,h}`, `room_class_data.c`, `blessed.inc`, and the
  compensation machinery in `music.c` that existed only to serve them.

Out:

- Event stings. `TC_DANGER` and `TC_TRIUMPH` are moments, not places; they keep
  their keyword scan, they keep carrying no picture, and they are the only
  text-driven thing left. Nothing else in this design touches them.
- Per-game CD-DA audio. Physically impossible — see "The audio asymmetry".
- The mix-mode state machine, debounce, `MUSIC_DYN_LOOPS`, `MUSIC_ROTATE_ROOMS`.
  None of that was ever about classification.
- The save blob format, `g_file_buf`, and the slot encoding. See "Why a flat
  per-game folder".

## Relationship to the genre-band design

`2026-08-10-genre-banded-art-pools-design.md` ruled per-game art out of scope
with: *"Per-game override packs are a later layer over this one, not a
substitute for it."*

This is that later layer, and it turns out to be a substitute rather than an
addition. Genre bands existed to stop Plundered Hearts' 17th-century cabin and
Seastalker's submarine drawing from one shared `NAUTICAL` pool. Per-game
folders dissolve that problem instead of narrowing it: the two games never
share a pool to begin with. The `GN_*` masks, the genre lock, the band
selection and the period axis the last handoff found broken all go away
together, because the thing they were correcting for no longer exists.

The `ArtBand {base, count}` struct survives. Its meaning changes from "where a
genre sits inside a category's 1..99" to "where a scene sits inside a game's
1..99". Same struct, same arithmetic, same generator property.

## The three tables

Everything the classifier did is replaced by three lookups on a number the
interpreter already has.

| Table | Shape | Size | Generated from |
|---|---|---|---|
| room to scene | per game, `unsigned char[256]`, indexed by object number | 7.9 KB | blessed scene JSON |
| scene to image range | `[game][scene] = {base, count}`, 31 × 32 × 2 B | 2.0 KB | files that actually converted |
| scene to track pool | `[game][scene]` = `unsigned long` bitmask, one bit per CD-DA track, 31 × 32 × 4 B | 4.0 KB | authored data |

The track pool is a bitmask rather than a list because there are 31 tracks and
a 32-bit word holds them all: a game's scene selects any subset with no
indirection and no length field, and "no tracks authored" is the zero mask,
which falls back to the neutral pool.

A v3 story's object table holds at most 255 objects, so a room's object number
fits in a byte and a game's whole map is one 256-byte array. `mojozork.c:1322`
already reads the location object from global `0x10` and passes it to
`music_on_turn`.

The value stored is `scene + 1`, so 0 means "no scene authored" and the room
holds whatever is showing — the same behaviour `TC_NEUTRAL` has today.

## The scene vocabulary

A scene is a general location, not a room. Several rooms share one, which is
the point: Zork I's fifteen `Maze` rooms are one picture, and its Kitchen,
Attic and Living Room are three.

Starting vocabulary, 32 scenes. Room counts are what the title rules alone
decide across all 31 stories — 977 of 1967 rooms, 50%. The review queue adds
to every row.

**Outdoor natural** — `FOREST` (25, incl. woods, grove, thicket) · `GARDEN`
(23, incl. orchard, courtyard) · `DESERT` (24, incl. dune, oasis) · `ROCKY`
(47, incl. canyon, cliff, ledge, mountain, gorge, volcano) · `SHORE` (31,
incl. beach, ocean, lake, reservoir) · `RIVER` (26, incl. stream, falls,
rapids) · `ROAD` (36, incl. path, street, trail)

**Underground** — `CAVE` (88, incl. cavern, tunnel, passage, crawl) · `MAZE`
(65) · `MINE` (27, incl. shaft, quarry) · `PIT` (22, incl. chasm, abyss) ·
`CRYPT` (9, incl. tomb, catacomb)

**Built exterior** — `HOUSE_EXT` (45, incl. tent, porch) · `VILLAGE` (9, incl.
town, square) · `CASTLE` (10, incl. tower, fortress, ruins) · `DOCK` (21,
incl. wharf, pier, harbour)

**Domestic interior** — `PARLOR` (30, incl. living room, lounge, dining,
foyer) · `KITCHEN` (23, incl. pantry, galley) · `BEDROOM` (23) · `BATHROOM`
(8) · `LIBRARY` (16, incl. study) · `DARKROOM` (27, incl. attic, cellar,
closet)

**Institutional interior** — `CORRIDOR` (181, incl. hall, hallway, stairs,
landing) · `OFFICE` (21) · `LAB` (14) · `STORAGE` (19, incl. supply,
warehouse) · `CELL` (16, incl. dungeon, jail) · `THEATER` (3, incl. stage,
auditorium)

**Sacred** — `TEMPLE` (13, incl. altar, shrine, chapel)

**Vessel and space** — `SHIP_EXT` (35, incl. deck, forecastle) · `SHIP_INT`
(24, incl. cabin, stateroom, engine, bridge) · `SPACE` (16, incl. airlock,
orbit)

Measured per-game usage against this vocabulary: **mean 10.0 scenes per game,
maximum 18** — and that is a lower bound, since it counts only the half the
rules decide. Zork I uses 13 before review. This is the 8–20 window the design
targets, confirmed rather than assumed.

Two properties of this list are load-bearing:

**Every tag is a noun a photographer would type.** `589d6c5` measured that
adjectives describing an experience never produce a usable stock photograph —
`torchlit` 0/14, `bustling` 0/12, `creaking` 0/12, against `misty` 85% and
`ruined` 69%. The cleanest case was `dim` at 33% beside `dimly lit` at 0/10:
identical meaning, one of them a word a photographer uses. That measurement is
applied here at the vocabulary level. There is no `EERIE` scene, no
`FOREBODING`, no `OMINOUS`.

**Unused scenes are free.** A scene a game never uses has `{base 0, count 0}`
and occupies no disc. So the vocabulary is sized for expressiveness, not for
economy, and splitting a scene later costs one enum entry.

The list is data and is expected to move. The invariant it must keep: **a game
uses 8–20 scenes at 1–3 images each, never exceeding 99 images total.**

## Why a flat per-game folder

`display.c` holds its filename in `g_file_buf[2][16]`, sized for exactly
`"UNDRGRND/99.TGA"` — 15 characters and a terminator. The 8.3 rule caps a stem
at eight characters, and the save blob has a frozen name field of the same
width.

`"STARCROS/99.TGA"` is also 15 characters. A per-game flat folder is a rename,
not a restructuring: no buffer change, no save-format change, no ISO depth
change, no slot-encoding change.

A folder per game *per scene* (`ZORK1/CAVE/07.TGA`) would have overflowed all
three. Putting the scene in the index range instead — which is precisely what
`ArtBand` already does — costs nothing at all.

## The audio asymmetry

Art is duplicated per game. Audio cannot be.

`saturn/cd/music/tracklist` is 31 CD-DA tracks, roughly 550 MB of WAV. On a
650 MB disc that leaves about 100 MB for the data track, of which 19 MB is
used today. Per-game art at 31 games × 20 images × 72 KB is 45 MB, which fits.
A second set of audio tracks does not fit, and never will.

So per-game audio independence is expressed as **selection, not duplication**:
each game's scenes draw from disjoint subsets of the shared 31 tracks. Zork I's
`CAVE` and Planetfall's `CAVE` can have no track in common and sound nothing
alike. This is a data table, so it costs ~2 KB and no disc.

## The authoring pipeline

Four stages with a file boundary between each, so any stage re-runs without
disturbing the others.

**1. Inventory** — `tools/gen_room_inventory.py`

Per story, emit every room-shaped object as `{obj, title, description,
source}`. Static decoding of the object table (`tools/zstory.py`) gives the
object number and about 60% of descriptions. The runtime pass — driving host
mojozork through a walkthrough where one exists and always through the fixed
wander script, with the interpreter RNG pinned at link time — fills what it can
of the remaining 40%, matched back by title.

Where a title is duplicated, a runtime description attaches to every object
carrying it. This is safe: duplicate-titled rooms are duplicated *because* they
are the same place repeated, and they share a scene.

26 of 31 games have walkthroughs. The two Infocom samplers never navigate and
are already a documented exception. The three MultiZork variants mirror Zork I
and II and borrow descriptions by title.

Output: `tools/assets/rooms/<STORY>.json`. Generated, deterministic, committed.

**2. Rule pass** — `tools/room_scenes.py`

An ordered rule list mapping title patterns to scenes. First match wins, so
priority is expressed by ordering rather than by weights — which is how `Shore
Road`, `Wharf Road` and `Upstairs Hallway` get decided instead of counted as
conflicts.

A rule that does not match **refuses**. Refusal is the designed output.
Measured against the 32-scene vocabulary, the pass decides **977 of 1967 rooms
(50%)** before any priority ordering is applied; ordering resolves the
multi-match cases on top of that.

**3. Review queue**

Every refusal lands in `tools/assets/scenes/<STORY>.review.json` carrying
`obj`, `title`, and **the description**. A human writes a scene into each
entry through the review tool.

The undecided set is what you would expect: `Dead End` ×24, `Oddly-angled
Room` ×9, `Cube` ×8, `Land of Shadow` ×8, `The Troll Room`, `Mirror Room`,
`Machine Room`, `Studio`. Titles naming a shape or a joke, not a scenery. 405
of them have no stored description and depend on the runtime pass.

**4. Blessed and generate**

Cleared verdicts move to `tools/assets/scenes/<STORY>.json`, committed. A
generator emits the three C tables as `.inc` files.

### The precedence rule

**A human verdict is authoritative and is never overwritten by a rule re-run.**
Re-running the rules after editing the vocabulary re-decides only rooms no
human has ruled on.

This is what lets the vocabulary keep evolving without discarding work, and it
is the direct lesson of a manifest that lost 365 curated images to a branch
switch twice in one session.

### The review tool

A Flask page in the shape of the existing `tools/art_server.py` on :8080.
Shows title and description, a row of scene buttons with keyboard shortcuts,
and auto-advance. Rooms with identical titles are grouped into one decision,
so Zork I's fifteen `Maze` rooms cost one keystroke.

It shows the room's old `blessed.inc` mood as a hint. Those 1024 verdicts are
moods rather than scenes and cannot auto-convert — `TC_UNDERGROUND` is `CAVE`
or `MINE` or `CRYPT` and only a human knows which — but as a prior they narrow
32 buttons to three or four plausible ones. The old work informs the new
instead of being discarded.

## Manifest persistence

`tools/assets/art_manifest.json` becomes gitignored, with a snapshot committed
at promote time.

It is tracked today, and being tracked has destroyed curation twice: a failed
`git stash` pop and a branch switch each reverted it to the same 412-record,
119-accepted signature, orphaning 365 images that exist on disk and cannot be
shown. This rewrite churns that manifest harder than anything before it —
every game is a fresh fetch campaign — so the exposure is worse, not better.

The working manifest is local state. The snapshot at promotion is the state
worth preserving, because that is the point at which images reach the disc.

Blessed scene files are the opposite case: human-authored, append-mostly,
meaningful in a diff. They stay tracked.

Before any refetch, the 365 orphans should be checked against the new
vocabulary. They were fetched for nouns — `cottage`, `attic`, `farmhouse`,
`chamber`, `forest`, `grove` — that map onto these scenes directly.

## Runtime changes

### Deleted

`saturn/src/classify/room_class.{c,h}` and `room_class_data.c`, entire — the
keyword tables, the `KT_*` tier system, the `GN_*` genre masks,
`text_classify_room`, `room_class_genre`, `room_class_genre_locked`.

In `main.cxx`: `art_band_of_genre` and the three `display_set_art_band` calls.

In `display.c`: `CATEGORY_DIR`, the genre meaning of `CATEGORY_BAND`,
`display_set_art_band`.

In `music.c`: `g_room_cache`, `g_neutral_rooms`, `g_fallback_cat`,
`MUSIC_FALLBACK_ROOMS`, `g_genre_was_locked` and its cache flush. Every one of
these exists to compensate for classification being unreliable.

In `mojozork.c`: `music_note_room_title` and its call site. The title only ever
existed to weight the classifier; scene lookup is by object number. The Saturn
hook collapses from a 64-byte stack buffer plus a per-turn ZSCII decode to a
single `music_on_turn(rmobj)`.

`test/corpus/blessed.inc` and the keyword half of `room_class_test.c`.

### Added

`saturn/src/scene/scene_map.{c,h}` — the `classify` folder is renamed, because
after this it does not classify, it looks up.

- `SC_*` and `SCENE_N` replace `TC_NEUTRAL..TC_PLACE_LAST`.
- `scene_of_room(release, serial, obj)` replaces `text_game_room_category`,
  returning `SC_*` or -1.
- Generated: `game_rooms.inc`, `game_scenes.inc`, `game_tracks.inc`.

`TC_DANGER` and `TC_TRIUMPH` keep their identity as event categories and are
renamed to match, but their scan and their tables are unchanged.

### Unchanged

`music_on_turn`'s debounce and pending-switch state machine; `MUSIC_DYN_LOOPS`;
`MUSIC_ROTATE_ROOMS` and the rotate callback; the four mix modes; the display
rotor, slot encoding and `display_image_slot`; `text_scan_event`.

## Testing

There is no judgement at runtime any more — it is a table lookup — so the tests
move to where the judgement now lives.

**Python**

- Rule-pass unit tests: title to expected scene, including the priority cases
  (`Shore Road`, `Wharf Road`, `Upstairs Hallway`, `Hall of the Mountain King`).
- Completeness guard: no blessed game has a room with no scene. A silent zero
  is the failure mode this design is most exposed to.
- Determinism: regenerating produces byte-identical `.inc` files, carrying
  forward the property `gen_room_corpus.py` protects today.
- Vocabulary guard: the scene list in Python and the `SC_*` enum in C agree,
  in both membership and order — the enum value is a table index.

**C**

- `scene_of_room` bounds: unknown game, object number past the map, object 0.
- Range validity: a game's scene ranges only name images the disc has.
  `make_tga.py` generates the range table from files that really converted, so
  a count cannot name a missing picture — the property `CATEGORY_BAND` has
  today, preserved.
- Budget guard: no game exceeds 99 images.

## Work order

Because art is per-game, the unit of work is one game end to end: tag its
rooms, let its scene set fall out, fetch and curate its images against those
scenes, convert, play it. Zork I first — 110 room objects, 65 decided by rules,
45 to review, and 13 scenes before review: `CAVE`, `DARKROOM`, `FOREST`,
`HOUSE_EXT`, `KITCHEN`, `MAZE`, `MINE`, `PARLOR`, `PIT`, `RIVER`, `ROCKY`,
`SHORE`, `TEMPLE`.

## Risks

**The neutral window.** Between deleting the classifier and blessing all 31
games, an unblessed game shows the neutral picture and the neutral track pool.
This is accepted deliberately. The per-game work order means each game goes
from neutral to finished in one pass rather than 31 games sitting half-done,
but the build does look worse than today until the first games land.

**`CORRIDOR` is the largest scene at 181 rooms.** Per-game folders remove the
cross-game half of the problem — Zork's corridor pool holds only stone dungeon
passages. What remains is within-game repetition in corridor-heavy games
(Planetfall, Stationfall, Suspended). Mitigation is curation: give those games'
corridor ranges more images, since the rotor already cycles them. If that is
not enough, splitting `CORRIDOR` costs one enum entry and no restructuring.

**Rooms with neither a stored nor a capturable description.** A room the
walkthrough and the wander script both miss, whose text is computed, is judged
from its title and game knowledge alone. The reviewer may leave it unassigned;
unassigned is a supported state, not a broken one.

**Object numbers are per story file.** A different release of the same game has
a different object table. Maps are keyed by release and serial, as `GAME_MAPS`
already is, so a mismatched release falls through to unmapped rather than
mis-mapping.

## Open items

- Which of the 31 shared CD-DA tracks each game's scenes select. Authored per
  game once its scene set is known; not blocking the pipeline.
- Whether the 365 orphaned images can be re-indexed against the new vocabulary
  rather than refetched. Worth checking before the first fetch campaign.
