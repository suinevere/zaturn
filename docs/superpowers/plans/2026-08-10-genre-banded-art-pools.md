# Genre-Banded Art Pools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a running game's genre pick which pictures a text category draws on, so Plundered Hearts' wooden ship and Seastalker's submarine stop sharing one round-robin pool.

**Architecture:** Genre becomes a band inside each category's existing 1..99 index range, never a folder and never a new `TC_*` row. `make_tga.py` buckets each mood's pictures by a `noun_genre` tag in `art_queries.json` and emits one gapless run per band; `display.c` keeps a current band and confines counting, rotation and shuffling to it, falling back to the neutral band. The disc path, the 16-byte path buffer and the save blob format are all untouched.

**Tech Stack:** Python 3.9+ with Pillow (host tooling), C89-style C for the Saturn target, gcc for host C tests, pytest for host Python tests.

## Global Constraints

- **Never change the `TC_*` enum or its order.** The category index is the row index of `music_data.c`'s `CATEGORY_POOL` and display's art table; inserting a row silently repoints every music row after it. Genre is an art-only dimension.
- **A slot stays `cat * SLOT_STRIDE + index` with `index` in 1..99.** `SLOT_STRIDE` is 100. Do not widen it — `display_slot_make` / `display_slot_valid` arithmetic and every save blob already on a memory card depend on it.
- **Never lengthen a disc path.** `g_file_buf` is `[2][16]`, exactly `"UNDRGRND/99.TGA"` plus terminator, and `make_tga.py` holds stems to the ISO 8.3 rule. `display_image_file()` must not be modified.
- **Do not change the save blob format.** It is at sentinel 6 and its name field is frozen for compatibility.
- **Every band's run must be gapless.** `CATEGORY_ART_N`'s invariant is that a count can never name a picture the disc lacks. Under bands this must hold *per band*: a gap inside a band hands `display_slot_make` an index with no file behind it, and the symptom is a missing picture at runtime, not a build error.
- **The repo owner runs all builds.** Never run `compile.bat`, `make`, or the emulator. Host C tests compile a single translation unit with gcc to a scratch path and are fine to run.
- **Test fixtures must be asymmetric.** A category whose bands are equal widths, or whose counts coincide, cannot distinguish a band lookup from a category lookup. Give every fixture distinct widths and distinct counts.
- **Comment style:** every function, constant and file gets the project's `/*---- | name | Description: ... ----*/` header block. No comments inside function bodies. Tests get a file header only.

## Refinements to the spec

Two details the spec left open, settled here:

**Four bands, not three.** `room_class.c`'s `genre_slot()` already maps `GN_FANTASY`→0, `GN_SCIFI`→1, `GN_MODERN`→2. Carrying a `GN_SCIFI` band alongside `ANY`/`FANTASY`/`MODERN` costs one array column and a zero count for moods that never use it, and avoids a second migration when a sci-fi mood wants its own art.

**Bands are packed, not fixed windows.** The spec sketched fixed ranges (01-59 / 60-79 / 80-99). Instead, assign each band's base from the cumulative count of the bands before it, so the whole category stays gapless *and* each band is gapless within itself. No index range is reserved and wasted, and the existing "more than 99" cap still applies to the category total.

## File Structure

- `tools/art_queries.py` — gains `noun_genre` validation. Already owns vocabulary validation; the tag lives with the nouns it describes.
- `tools/assets/art_queries.json` — gains the `noun_genre` map for NAUTICAL.
- `tools/make_tga.py` — gains band bucketing in `convert_tree` and a band table in `write_inc`. Already owns the PNG tree walk and the generated `.inc`.
- `saturn/src/video/category_art.inc` — GENERATED. Becomes a band table instead of a count row.
- `saturn/src/video/display.c` — gains a current band and band-confined selection.
- `saturn/src/video/display.h` — gains `display_set_art_band`.
- `saturn/src/classify/room_class.c` / `.h` — gains `room_class_genre()`, a public reader for the already-resolved `g_genre`.
- `saturn/src/main.cxx` — maps the classifier's genre to a band and tells display.

Tests: `tools/tests/test_art_queries.py`, `tools/tests/test_make_tga.py`, `saturn/tests/test_display.c`, `saturn/tests/test_category_art.py`.

Run the Python suite with `tools/.venv/Scripts/python.exe -m pytest tools/tests saturn/tests -q`.

---

### Task 1: `noun_genre` tag and its validation

**Files:**
- Modify: `tools/art_queries.py` (the `validate` function, around line 24)
- Modify: `tools/assets/art_queries.json` (the `NAUTICAL` entry)
- Test: `tools/tests/test_art_queries.py`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `art_queries.load(path)` returns a vocabulary whose mood entries may carry `"noun_genre": {noun: "FANTASY"|"SCIFI"|"MODERN"}`. Task 2 reads this map. Absent or empty means every noun in that mood is neutral.

- [ ] **Step 1: Write the failing tests**

Append to `tools/tests/test_art_queries.py`:

```python
def test_noun_genre_rejects_an_unknown_genre_name():
    vocab = {"HORROR": {"adjectives": ["dark"], "donors": ["HOUSE"],
                        "extra_nouns": ["morgue"], "exclude_nouns": [],
                        "noun_genre": {"morgue": "GOTHIC"}, "target": 99}}
    with pytest.raises(ValueError, match="GOTHIC"):
        art_queries.validate(vocab)


def test_noun_genre_rejects_a_noun_the_mood_cannot_reach():
    vocab = {"HORROR": {"adjectives": ["dark"], "donors": ["HOUSE"],
                        "extra_nouns": ["morgue"], "exclude_nouns": [],
                        "noun_genre": {"quarterdeck": "MODERN"}, "target": 99}}
    with pytest.raises(ValueError, match="quarterdeck"):
        art_queries.validate(vocab)


def test_noun_genre_is_optional():
    vocab = {"HORROR": {"adjectives": ["dark"], "donors": ["HOUSE"],
                        "extra_nouns": ["morgue"], "exclude_nouns": [],
                        "target": 99}}
    assert art_queries.validate(vocab) is vocab


def test_the_shipped_vocabulary_tags_both_nautical_periods():
    repo = Path(__file__).resolve().parents[2]
    vocab = art_queries.load(repo / "tools" / "assets" / "art_queries.json")
    tags = vocab["NAUTICAL"]["noun_genre"]
    assert tags["sailing ship cabin"] == "FANTASY"
    assert tags["submarine interior"] == "MODERN"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_queries.py -k noun_genre -q`
Expected: FAIL — `validate` accepts anything, so the two `pytest.raises` tests report `DID NOT RAISE`.

- [ ] **Step 3: Implement validation**

In `tools/art_queries.py`, add the constant above `validate`:

```python
GENRES = ("FANTASY", "SCIFI", "MODERN")
```

Give it the project's header block:

```python
"""
----------------------
| GENRES
| Description: The genre names noun_genre may use, matching room_class.h's
|   GN_FANTASY / GN_SCIFI / GN_MODERN. An untagged noun is neutral and serves
|   every game, which is what most art is.
| Author: suinevere
----------------------
"""
```

Inside `validate`, after the existing `target` check and still within the
`for mood, entry in vocab.items()` loop:

```python
        tags = entry.get("noun_genre", {})
        reachable = set(entry.get("extra_nouns", []))
        for noun, genre in tags.items():
            if genre not in GENRES:
                raise ValueError(
                    f"{mood}: noun_genre {noun!r} has genre {genre!r}; "
                    f"expected one of {GENRES}")
            if noun not in reachable:
                raise ValueError(
                    f"{mood}: noun_genre names {noun!r}, which is not one of "
                    f"this mood's extra_nouns; only a qualified noun carries "
                    f"a period")
```

Tagging is restricted to `extra_nouns` deliberately: a donor noun is derived
from the classifier and shared, so a period tag on one would silently change
what another mood's pictures mean.

- [ ] **Step 4: Tag NAUTICAL in the shipped vocabulary**

In `tools/assets/art_queries.json`, add to the `NAUTICAL` object:

```json
"noun_genre": {
  "sailing ship cabin": "FANTASY",
  "sailing ship deck":  "FANTASY",
  "tall ship deck":     "FANTASY",
  "wooden ship hull":   "FANTASY",
  "galleon interior":   "FANTASY",
  "quarterdeck":        "FANTASY",
  "rigging":            "FANTASY",
  "submarine interior": "MODERN",
  "submarine control room": "MODERN",
  "cargo ship deck":    "MODERN",
  "steel ship hull":    "MODERN"
}
```

`dockside` stays untagged — a dock photographs the same in either period.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_art_queries.py -q`
Expected: PASS, all tests.

- [ ] **Step 6: Mutation-verify the guards**

Change `"sailing ship cabin": "FANTASY"` to `"MODERN"` in the JSON, run
`-k tags_both_nautical_periods`, confirm FAIL, restore. Then delete the
`if genre not in GENRES` block, run `-k unknown_genre`, confirm FAIL, restore.

- [ ] **Step 7: Commit**

```bash
git add tools/art_queries.py tools/assets/art_queries.json tools/tests/test_art_queries.py
git commit -m "Tag NAUTICAL's qualified nouns with the period they depict."
```

---

### Task 2: `make_tga.py` emits one gapless run per band

**Files:**
- Modify: `tools/make_tga.py` (`convert_tree`, lines 167-236)
- Test: `tools/tests/test_make_tga.py`

**Interfaces:**
- Consumes: `art_queries.load` from Task 1, for the `noun_genre` map.
- Produces: `convert_tree(src_root, dst_root)` returns `{mood: [n_any, n_fantasy, n_scifi, n_modern]}` — a four-element list per mood, replacing today's single int. Task 3's `write_inc` consumes exactly this shape. Band order is fixed: index 0 is neutral, then `room_class.c`'s `genre_slot()` order plus one.

- [ ] **Step 1: Write the failing test**

Append to `tools/tests/test_make_tga.py`, following the file's `check()` convention:

```python
def test_convert_tree_packs_each_genre_band_gaplessly():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        src, dst = root / "png", root / "TGA"
        plan = {"NAUTICAL": {"boat": 3, "sailing ship cabin": 2,
                             "submarine interior": 1}}
        for noun, count in plan["NAUTICAL"].items():
            d = src / "NAUTICAL" / "EXTRA" / noun
            d.mkdir(parents=True)
            for i in range(count):
                Image.new("RGB", (320, 224), (9 * i, 40, 80)).save(
                    d / f"{noun.replace(' ', '')}{i}.png", "PNG")

        counts = make_tga.convert_tree(src, dst, genre_of=lambda m, n: {
            "sailing ship cabin": "FANTASY",
            "submarine interior": "MODERN"}.get(n))

        check(counts["NAUTICAL"] == [3, 2, 0, 1],
              "three neutral, two fantasy, none scifi, one modern")
        made = sorted(p.name for p in (dst / "NAUTICAL").glob("*.TGA"))
        check(made == ["01.TGA", "02.TGA", "03.TGA", "04.TGA", "05.TGA",
                       "06.TGA"],
              "six files, gapless across the packed bands")
```

The fixture is asymmetric on purpose: 3 / 2 / 0 / 1 are four distinct widths,
so a band lookup that accidentally returns the category total, or the first
band, or a fixed window, cannot pass.

- [ ] **Step 2: Run the test to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_make_tga.py -k genre_band -q`
Expected: FAIL with `TypeError: convert_tree() got an unexpected keyword argument 'genre_of'`.

- [ ] **Step 3: Implement band bucketing**

Add above `convert_tree` in `tools/make_tga.py`:

```python
BANDS = ("ANY", "FANTASY", "SCIFI", "MODERN")
```

with the header block:

```python
"""
----------------------
| BANDS
| Description: The genre bands a category's 1..99 index range is packed into,
|   in the order display.c indexes them. Index 0 is the neutral band every
|   game falls back to; the rest follow room_class.c's genre_slot() order.
| Author: suinevere
----------------------
"""
```

Change the signature to `def convert_tree(src_root, dst_root, genre_of=None):`
and document the new parameter in its header block as
`genre_of -- callable (mood, noun) -> "FANTASY"|"SCIFI"|"MODERN"|None; None
means every picture is neutral`.

Replace the body's per-mood conversion block (currently the `sources = sorted(...)`
through `counts[mood] = n` section) with:

```python
        sources = sorted(
            p for p in (src_root / mood).rglob("*")
            if p.suffix.lower() in SOURCE_EXT
        )
        buckets = {b: [] for b in BANDS}
        for src in sources:
            genre = genre_of(mood, src.parent.name) if genre_of else None
            buckets[genre if genre in BANDS else "ANY"].append(src)

        per_band, n = [], 0
        for band in BANDS:
            made = 0
            for src in buckets[band]:
                if n >= 99:
                    print(f"  {mood}: more than 99 pictures, ignoring {src.name}")
                    continue
                if _convert_source(src, out_dir / f"{n + 1:02d}.TGA"):
                    n += 1
                    made += 1
            per_band.append(made)
        counts[mood] = per_band
        print(f"  {mood}: {n} ({', '.join(f'{b}={c}' for b, c in zip(BANDS, per_band))})")
```

Converting band by band is what makes each band's run contiguous, and
incrementing `n` only on success is what keeps it gapless when a source is
off-size or unreadable.

- [ ] **Step 4: Run the test to verify it passes**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_make_tga.py -k genre_band -q`
Expected: PASS.

- [ ] **Step 5: Run the whole make_tga suite for regressions**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_make_tga.py -q`
Expected: the existing tests that assert `counts[mood] == <int>` now FAIL,
because the return shape changed. Update each to compare against a four-element
list — for example `check(counts["HOUSE"] == [2, 0, 0, 0], ...)` where the test
previously expected `2`. Do not change what the tests exercise, only the shape.

- [ ] **Step 6: Mutation-verify the packing**

Change `for band in BANDS:` to `for band in reversed(BANDS):`, run
`-k genre_band`, confirm the count assertion still passes but the *ordering*
is now wrong — then add this assertion to lock it and confirm it fails:

```python
        first = (dst / "NAUTICAL" / "01.TGA").read_bytes()
        check(len(first) > 0, "neutral band occupies the lowest indices")
```

Restore, and confirm PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/make_tga.py tools/tests/test_make_tga.py
git commit -m "Pack each mood's pictures into gapless genre bands when converting."
```

---

### Task 3: The generated band table

**Files:**
- Modify: `tools/make_tga.py` (`write_inc`, lines 238-266; `main`, lines 268-300)
- Modify (GENERATED): `saturn/src/video/category_art.inc`
- Test: `tools/tests/test_make_tga.py`, `saturn/tests/test_category_art.py`

**Interfaces:**
- Consumes: `convert_tree`'s `{mood: [n_any, n_fantasy, n_scifi, n_modern]}` from Task 2.
- Produces: `category_art.inc` defining `static const ArtBand CATEGORY_BAND[TEXT_NUM_CATEGORIES][ART_BAND_N]`, where `ArtBand` is `{unsigned char base, count;}` and `base` is 0-based. Task 5 reads this. `CATEGORY_ART_N` is removed.

- [ ] **Step 1: Write the failing test**

```python
def test_write_inc_emits_bases_that_follow_the_bands_before_them():
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "category_art.inc"
        make_tga.write_inc({"NAUTICAL": [3, 2, 0, 1]}, out)
        text = out.read_text()

        check("{ 0, 3}, { 3, 2}, { 5, 0}, { 5, 1}" in text,
              "NAUTICAL bases accumulate: 0, 3, 5, 5")
        check("CATEGORY_ART_N" not in text,
              "the flat count row is gone, not left beside the table")
```

Bases 0, 3, 5, 5 are four values where two coincide — a deliberately awkward
fixture, because an empty band must still carry the base of the band after it.

- [ ] **Step 2: Run it to verify it fails**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_make_tga.py -k write_inc_emits -q`
Expected: FAIL — `write_inc` emits `CATEGORY_ART_N` and cannot index a list.

- [ ] **Step 3: Rewrite `write_inc`**

```python
def write_inc(counts, path):
    """
    ----------------------
    | write_inc
    | Description: Write the generated per-category genre-band table consumed
    |   by display.c. Each row is one category; each column is a band, holding
    |   its 0-based base within the category's 1..99 index range and how many
    |   pictures it carries. A band's base is the sum of the counts before it,
    |   so an empty band still names where the next one starts.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: ENUM_ORDER, BANDS
    | Params: counts -- per-mood band-count lists from convert_tree; path --
    |   output .inc file path
    | Returns: N/A
    ----------------------
    """
    rows = []
    for mood in ENUM_ORDER:
        per_band = [0] * len(BANDS) if mood is None else counts.get(
            mood, [0] * len(BANDS))
        cells, base = [], 0
        for n in per_band:
            cells.append(f"{{{base:2d},{n:2d}}}")
            base += n
        rows.append("    { " + ", ".join(cells) + " },")
    path.write_text(
        "/*----------------------\n"
        " | category_art.inc\n"
        " | Description: Where each text category's genre bands sit inside its\n"
        " |   1..99 index range on this disc, as {base, count} per band.\n"
        " |   GENERATED by tools/make_tga.py -- do not edit. Row order is the\n"
        " |   TC_* enum order in sound/music.h; column order is display.c's\n"
        " |   ART_BAND_* order, neutral first. The three zero rows are\n"
        " |   TC_NEUTRAL, TC_DANGER and TC_TRIUMPH, which hold whatever is\n"
        " |   showing.\n"
        " | Author: suinevere\n"
        " ----------------------*/\n"
        "static const ArtBand CATEGORY_BAND[TEXT_NUM_CATEGORIES][ART_BAND_N] = {\n"
        + "\n".join(rows) + "\n"
        "};\n"
    )
```

Note the emitted cell format is `{ 0, 3}` — `f"{{{base:2d},{n:2d}}}"` renders
one leading space for single digits, which is what the Step 1 assertion
matches.

- [ ] **Step 4: Run it to verify it passes**

Run: `tools/.venv/Scripts/python.exe -m pytest tools/tests/test_make_tga.py -k write_inc_emits -q`
Expected: PASS.

- [ ] **Step 5: Pass the vocabulary's tags through `main`**

In `make_tga.py`'s `main`, replace the `counts = convert_tree(src, dst)` line
with:

```python
        vocab = art_queries.load(REPO / "tools" / "assets" / "art_queries.json")
        counts = convert_tree(src, dst, genre_of=lambda mood, noun: (
            vocab.get(mood, {}).get("noun_genre", {}).get(noun)))
```

and add `import art_queries` beside the existing imports. Update `main`'s
header block `Dependencies:` to name `art_queries`.

- [ ] **Step 6: Update the disc-gate test**

`saturn/tests/test_category_art.py` parses `CATEGORY_ART_N` out of the `.inc`
and compares it against files on disc. Change its regex to read the
`CATEGORY_BAND` rows and assert the sum of each row's counts equals the number
of TGAs in that mood's folder, and that each row's bases accumulate from its
counts. Keep the file's existing docstring intent — it gates the generated
table against reality.

- [ ] **Step 7: Regenerate and run everything**

Run: `tools/.venv/Scripts/python.exe tools/make_tga.py tools/assets/png saturn/cd/data/TGA`
Then: `tools/.venv/Scripts/python.exe -m pytest tools/tests saturn/tests -q`
Expected: PASS. The regenerated `category_art.inc` will not compile until
Task 5 defines `ArtBand` and `ART_BAND_N`; that is expected and is why the C
tasks follow immediately.

- [ ] **Step 8: Commit**

```bash
git add tools/make_tga.py tools/tests/test_make_tga.py saturn/tests/test_category_art.py saturn/src/video/category_art.inc
git commit -m "Generate a per-category genre band table in place of the flat art counts."
```

---

### Task 4: Expose the classifier's resolved genre

**Files:**
- Modify: `saturn/src/classify/room_class.c` (beside `room_class_genre_locked`, line 333)
- Modify: `saturn/src/classify/room_class.h` (beside line 147)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `unsigned char room_class_genre(void)` returning the resolved genre mask, or 0 while unresolved. Task 5's caller in `main.cxx` uses it.

- [ ] **Step 1: Write the failing test**

`saturn/tests/` carries no classifier host test today, so create a new one at
`saturn/tests/test_room_genre.c`. It is a standalone program with its own
`main`, matching the build-line-in-a-header-comment convention every other
`saturn/tests/*.c` file uses:

```c
/* Build:
     gcc -O2 -I saturn/src -o /tmp/trg saturn/tests/test_room_genre.c \
         saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c \
         && /tmp/trg */
#include "../src/classify/room_class.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    room_class_reset();
    room_class_set_game(88, "840726");        /* Zork I, authored GN_FANTASY */
    assert(room_class_genre() == GN_FANTASY);
    assert(room_class_genre_locked() == 1);

    room_class_reset();
    room_class_set_game(16, "850603");        /* Seastalker, authored GN_MODERN */
    assert(room_class_genre() == GN_MODERN);

    room_class_reset();
    room_class_set_game(1, "000000");         /* unlisted: unresolved */
    assert(room_class_genre() == 0);
    assert(room_class_genre_locked() == 0);

    printf("test_room_genre ok\n");
    return 0;
}
```

Zork I and Seastalker are chosen because their authored genres differ, so a
stub returning a constant fails one of them.

- [ ] **Step 2: Run it to verify it fails**

Run: `gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/trg.exe saturn/tests/test_room_genre.c saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c && /tmp/trg.exe`
Expected: FAIL at link with `undefined reference to 'room_class_genre'`.

- [ ] **Step 3: Implement the reader**

In `room_class.c`, beside `room_class_genre_locked`:

```c
/*----------------------
 | room_class_genre
 | Description: The resolved genre mask, or 0 while it is still unresolved.
 |   Callers outside classification use this to pick art; 0 means "no opinion
 |   yet", which they answer with neutral art rather than a guess.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_genre
 | Params: N/A
 | Returns: GN_FANTASY, GN_SCIFI, GN_MODERN, or 0
 ----------------------*/
unsigned char room_class_genre(void) { return g_genre; }
```

In `room_class.h`, beside line 147:

```c
unsigned char room_class_genre(void);
```

- [ ] **Step 4: Run it to verify it passes**

Run: `gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/trg.exe saturn/tests/test_room_genre.c saturn/src/classify/room_class.c saturn/src/classify/room_class_data.c && /tmp/trg.exe`
Expected: `test_room_genre ok`.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/classify/room_class.c saturn/src/classify/room_class.h saturn/tests/test_room_genre.c
git commit -m "Expose the classifier's resolved genre so art can read it."
```

---

### Task 5: Band-confined selection in display.c

**Files:**
- Modify: `saturn/src/video/display.c` (above the `#include "category_art.inc"` at line 204; `display_category_image_count` line 438; `display_shuffle_category` line 447; `display_rotate_dynamic_category` line 470)
- Modify: `saturn/src/video/display.h` (beside line 213)
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: `CATEGORY_BAND` from Task 3.
- Produces: `void display_set_art_band(int band)` where band is 0 (neutral), 1 (fantasy), 2 (sci-fi), 3 (modern). Task 6's caller in `main.cxx` uses it. `display.c` gains no dependency on `room_class.h`; the caller maps genre to band.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_display.c`:

```c
static void test_band_confines_selection(void) {
    int cat = TC_NAUTICAL, i, seen_outside = 0;
    int total = 0;

    display_set_art_band(3);
    for (i = 0; i < 40; i++) {
        int slot;
        display_rotate_dynamic_category(cat);
        slot = display_image_slot(display_category_image(cat));
        if (slot < 0) continue;
        total++;
        if (slot % 100 <= CATEGORY_BAND[cat][0].count) seen_outside = 1;
    }
    assert(total > 0);
    assert(!seen_outside);
}
```

This is the assertion the whole design exists for: a modern game must never
resolve a slot inside the neutral band's index range.

`test_display.c` runs its tests by calling each one explicitly from `main`
(see the call list ending at `test_old_blobs_get_no_dim();`). Register the new
test there too, beside `test_rotate_dynamic_category();`:

```c
    test_band_confines_selection();
```

A test defined but never called is a test that asserts nothing. Verify it
actually runs by confirming the failure in Step 2 comes from this test, not
merely from a compile error elsewhere.

- [ ] **Step 2: Run it to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: FAIL to compile — `display_set_art_band` undeclared, `ArtBand` and
`ART_BAND_N` undefined for the regenerated `.inc`.

- [ ] **Step 3: Define the band type and current band**

In `display.c`, immediately above `#include "category_art.inc"`:

```c
/*----------------------
 | ArtBand / ART_BAND_N / g_art_band
 | Description: A category's 1..99 index range is packed into bands, one per
 |   genre plus a neutral band every game falls back to. base is 0-based, so
 |   the nth picture of a band is index base + n + 1. g_art_band is the band
 |   the running game prefers; 0 until something sets it, which is the neutral
 |   band and today's behaviour.
 |
 |   A folder per genre was not possible: g_file_buf is exactly
 |   "UNDRGRND/99.TGA" and the ISO 8.3 rule caps a stem at eight characters,
 |   so a suffixed name overflows both that buffer and the save blob's frozen
 |   name field. Banding the index changes no path at all.
 | Author: suinevere
 ----------------------*/
typedef struct { unsigned char base, count; } ArtBand;
#define ART_BAND_N 4
static int g_art_band = 0;
```

- [ ] **Step 4: Add the effective-band helper and the setter**

Below the `#include "category_art.inc"` and `SLOT_STRIDE` define:

```c
/*----------------------
 | effective_band
 | Description: The band a category will actually draw from: the current one
 |   when it holds pictures, otherwise the neutral band. Falling back is the
 |   normal path, not an error one -- most moods carry no period art at all,
 |   and a game whose genre is unresolved has no band of its own yet.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_BAND, g_art_band
 | Params: cat -- the TC_* category
 | Returns: a pointer to the band to use
 ----------------------*/
static const ArtBand *effective_band(int cat) {
    const ArtBand *b = &CATEGORY_BAND[cat][g_art_band];
    return b->count ? b : &CATEGORY_BAND[cat][0];
}

/*----------------------
 | display_set_art_band
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_art_band
 | Params: band -- 0 neutral, 1 fantasy, 2 sci-fi, 3 modern
 | Returns: N/A
 ----------------------*/
void display_set_art_band(int band) {
    if (band < 0 || band >= ART_BAND_N) return;
    g_art_band = band;
}
```

- [ ] **Step 5: Confine counting, shuffling and rotation to the band**

Replace `display_category_image_count`'s body's final line with:

```c
    return (int) effective_band(cat)->count;
```

In `display_shuffle_category`, replace `n = (int) CATEGORY_ART_N[cat];` and the
assignment with:

```c
    const ArtBand *b = effective_band(cat);
    if (!b->count) return;
    g_cat_rot[cat] = (unsigned char)(b->base + r % (unsigned int) b->count);
```

In `display_rotate_dynamic_category`, replace `n = (int) CATEGORY_ART_N[cat];`
with `const ArtBand *b = effective_band(cat); n = (int) b->count;` and the walk
body with:

```c
        g_cat_rot[cat] = (unsigned char)(
            b->base + (((int) g_cat_rot[cat] - (int) b->base + 1) % n));
        slot = display_slot_make(cat, (int) g_cat_rot[cat] + 1);
```

`g_cat_rot` holds an absolute 0-based index, so the walk subtracts the base
before wrapping and adds it back. Storing an absolute index is what lets
`display_category_image` keep resolving `g_cat_rot[cat] + 1` unchanged.

- [ ] **Step 6: Declare the setter**

In `display.h`, beside line 213:

```c
/*----------------------
 | display_set_art_band
 | Description: Which genre band every category draws from until told
 |   otherwise. The caller maps the classifier's genre mask to a band, so the
 |   display model gains no dependency on classification. A band a category
 |   has no pictures in falls back to the neutral band, so setting one is
 |   always safe.
 | Author: suinevere
 ----------------------*/
void display_set_art_band(int band);
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: PASS.

- [ ] **Step 8: Mutation-verify the confinement**

Change `effective_band` to always `return &CATEGORY_BAND[cat][0];`, rebuild,
confirm `test_band_confines_selection` fails. Then change the rotate walk back
to `(g_cat_rot[cat] + 1) % n` without the base arithmetic, rebuild, confirm it
fails again. Restore and confirm PASS.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/display.c saturn/src/video/display.h saturn/tests/test_display.c
git commit -m "Confine art counting, shuffling and rotation to the running game's genre band."
```

---

### Task 6: Wire the genre to the band

**Files:**
- Modify: `saturn/src/main.cxx` (beside `display_rotate_dynamic_category` at line 154 and `display_shuffle_category` at line 372)

**Interfaces:**
- Consumes: `room_class_genre()` from Task 4, `display_set_art_band` from Task 5.
- Produces: nothing later tasks depend on. This is the final wiring.

- [ ] **Step 1: Add the mapping helper**

In `main.cxx`, above its first use:

```c
/*----------------------
 | art_band_of_genre
 | Description: The display art band a classifier genre mask selects. Kept
 |   here rather than in display.c so the display model never includes
 |   room_class.h. An unresolved genre is band 0, the neutral band, which is
 |   the same answer as "this game has no period art".
 | Author: suinevere
 | Dependencies: room_class.h
 | Globals: N/A
 | Params: genre -- GN_FANTASY, GN_SCIFI, GN_MODERN or 0
 | Returns: 0..3, matching display.c's ART_BAND_* order
 ----------------------*/
static int art_band_of_genre(unsigned char genre) {
    if (genre == GN_FANTASY) return 1;
    if (genre == GN_SCIFI)   return 2;
    if (genre == GN_MODERN)  return 3;
    return 0;
}
```

- [ ] **Step 2: Set the band before rotating**

At `main.cxx:154`, immediately before `display_rotate_dynamic_category(cat);`:

```c
    display_set_art_band(art_band_of_genre(room_class_genre()));
```

Setting it per rotation rather than once at load is deliberate: an unlisted
game's genre resolves partway through play, and this picks the band up on the
next room change without a second notification path.

- [ ] **Step 3: Set the band before the title shuffle**

At `main.cxx:372`, immediately before `display_shuffle_category(TC_HOUSE, boot_entropy());`:

```c
    display_set_art_band(0);
```

The title screen precedes any game, so it draws neutral explicitly rather than
inheriting whatever the last session left behind.

- [ ] **Step 4: Syntax-check the changed unit**

`main.cxx` is Saturn-target code and cannot be linked on the host. Compile it
for syntax only, to a scratch object:

Run: `gcc -fsyntax-only -I saturn/src saturn/src/main.cxx 2>&1 | head -20`
Expected: errors only from Saturn SDK headers the host lacks, and none naming
`art_band_of_genre`, `display_set_art_band` or `room_class_genre`.

- [ ] **Step 5: Hand the build to the repo owner**

Do not run `compile.bat`. Report that Tasks 1-6 are complete and that a target
build plus an on-hardware or emulator check is needed, naming what to look for:
a fantasy game and a modern game visiting the same mood should show pictures
from different pools.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Pick the art band from the running game's genre."
```

---

## Self-Review

**Spec coverage.** Bands within 1..99 — Tasks 2, 3, 5. Selection prefers the
game's band and falls back to neutral — Task 5's `effective_band`, Task 6's
wiring. Per-noun genre tag — Task 1. `make_tga` emits banded runs — Task 2.
`CATEGORY_ART_N` becomes per-band — Task 3. Unresolved genre draws neutral —
Task 4's 0 return, Task 6's `art_band_of_genre` default, covered by the
unlisted-game assertion in Task 4 Step 1. `TC_*` untouched — no task modifies
the enum. Save format untouched — no task modifies the blob helpers. Rotation
and shuffle band-confinement, both named as risks in the spec — Task 5 Steps 5
and 8.

**Placeholders.** None: every code step carries the actual code, every test
step the actual test, every run step the actual command and expected output.

**Type consistency.** `convert_tree` returns `{mood: [int, int, int, int]}` in
Task 2 and is consumed in that shape by `write_inc` in Task 3. `ArtBand` is
`{unsigned char base, count;}` in Task 5 Step 3 and generated as `{base, count}`
pairs in Task 3 Step 3. `ART_BAND_N` is 4 in Task 5 and `BANDS` has four
entries in Task 2. `display_set_art_band(int)` is defined in Task 5 and called
in Task 6. `room_class_genre()` returns `unsigned char` in Task 4 and is passed
to `art_band_of_genre(unsigned char)` in Task 6. Band order — neutral, fantasy,
sci-fi, modern — is identical in `BANDS`, `CATEGORY_BAND`'s columns, and
`art_band_of_genre`.

**Gap found and closed.** Task 3 regenerates `category_art.inc` into a form
that will not compile until Task 5 defines `ArtBand`. Task 3 Step 7 now says so
explicitly rather than leaving an executor to discover a broken tree.
