# Room Art — Saturn Side — Implementation Plan (Plan A)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move room art from a flat `/TGA` folder with hand-listed filenames to one folder per mood with synthesised filenames, and add a player-adjustable wallpaper dim.

**Architecture:** The disc gains one directory per mood (`/TGA/HORROR/01.TGA`). `make_tga.py` emits a generated `category_art.inc` holding a per-mood count, so the boot directory scan and the twelve hand-written `IMG_*` arrays are deleted. Image *slots* survive but become virtual — `slot = cat * 100 + index` — because `DisplayState.image` is consumed across `main.cxx`, `menu_pages.cxx` and `options.cxx`. Separately, a held VDP2 Colour Offset B value composes with the existing transition ramp to give an adjustable dim.

**Tech Stack:** C99 (`saturn/src`), C++ for `title.cxx`/`menu_pages.cxx`, Python 3.9+ with Pillow (`tools/make_tga.py`), host `gcc` for unit tests, `pytest` for asset tests.

**Spec:** `docs/superpowers/specs/2026-08-06-room-art-sourcing-design.md`

## Global Constraints

- **The user runs all builds.** Never run `compile.bat`, `compile-cd.bat`, or the emulator. Cross-compile changed units to a scratch directory to check syntax only. (`mem/MEMORY.md`)
- **Comment style is mandatory.** Every method, constant, and file gets the `/*---- | Name | Description | Author: suinevere | Dependencies | Globals | Params | Returns ----*/` header block. Tests get a file header only. **No comments inside function bodies** except where an existing line already carries one.
- **Commit after every task.** One sentence, no body, no trailers, no mention of Claude/AI/session.
- Mood folder names are exactly these twelve, unchanged from `tools/assets/png/`: `WILDER`, `UNDRGRND`, `WATER`, `NAUTICAL`, `TOWN`, `DUNGN`, `DESERT`, `MAGIC`, `SCIFI`, `HORROR`, `MYSTERY`, `HOUSE`.
- Filenames on disc are exactly two digits: `01.TGA` … `99.TGA`. Index 0 is never used.
- Slot encoding is `cat * 100 + index`, index `1..99`. `DISP_IMAGE_NONE` is `-1` and stays so.
- `SUINE.TGA` stays at `/TGA` root and is never a selectable background.
- Row order of any `TEXT_NUM_CATEGORIES`-sized table is the `TC_*` enum order in `saturn/src/sound/music.h:45`. `TC_NEUTRAL`, `TC_DANGER`, `TC_TRIUMPH` carry zero art.
- Host C tests build with `gcc -O2 -I saturn/src`, and carry their build line as the file's first comment (see `saturn/tests/test_display.c:1`).

---

### Task 1: Per-mood output folders and the generated count table

**Files:**
- Modify: `tools/make_tga.py` (module docstring; the batch-walk and write path)
- Create: `saturn/src/video/category_art.inc` (generated, committed)
- Modify: `saturn/tests/test_category_art.py` (rewritten)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `saturn/src/video/category_art.inc` defining
  `static const unsigned char CATEGORY_ART_N[TEXT_NUM_CATEGORIES]` — 15 entries in `TC_*` order, `unsigned char`, values `0..99`. Task 2 includes this file.
- Produces: disc tree `saturn/cd/data/TGA/<MOOD>/NN.TGA` plus `saturn/cd/data/TGA/SUINE.TGA`.

- [ ] **Step 1: Write the failing test**

Replace `saturn/tests/test_category_art.py` entirely:

```python
"""Gate the generated art count table against the files actually on the disc.

The disjointness check this file used to carry is gone: a picture now lives in
exactly one mood folder, so two moods cannot name the same file and there is
nothing left to assert.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TGA = REPO / "saturn" / "cd" / "data" / "TGA"
INC = REPO / "saturn" / "src" / "video" / "category_art.inc"

MOODS = ["WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
         "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE"]

# TC_* enum order, saturn/src/sound/music.h:45. None means "carries no art".
ENUM_ORDER = [None, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
              "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
              None, None]


def parse_inc():
    text = INC.read_text()
    body = re.search(r"CATEGORY_ART_N\[TEXT_NUM_CATEGORIES\]\s*=\s*\{(.*?)\}",
                     text, re.S).group(1)
    return [int(v) for v in re.findall(r"\d+", body)]


def test_inc_has_one_entry_per_category():
    assert len(parse_inc()) == len(ENUM_ORDER)


def test_counts_match_the_disc():
    counts = parse_inc()
    for slot, mood in enumerate(ENUM_ORDER):
        if mood is None:
            assert counts[slot] == 0, f"row {slot} must carry no art"
            continue
        n = len(list((TGA / mood).glob("*.TGA"))) if (TGA / mood).is_dir() else 0
        assert counts[slot] == n, f"{mood}: table says {counts[slot]}, disc has {n}"


def test_filenames_are_two_digits_from_one():
    for mood in MOODS:
        d = TGA / mood
        if not d.is_dir():
            continue
        names = sorted(p.name for p in d.glob("*.TGA"))
        assert names == [f"{i:02d}.TGA" for i in range(1, len(names) + 1)], \
            f"{mood}: expected a gapless 01..NN run, got {names}"


def test_folder_and_file_names_fit_iso9660():
    for mood in MOODS:
        assert len(mood) <= 8, f"{mood} exceeds the 8-character directory limit"
    for mood in MOODS:
        d = TGA / mood
        if not d.is_dir():
            continue
        for p in d.glob("*"):
            stem, _, ext = p.name.partition(".")
            assert len(stem) <= 8 and len(ext) <= 3, f"{p.name} is not 8.3"


def test_splash_stays_at_the_root():
    assert (TGA / "SUINE.TGA").is_file()
    for mood in MOODS:
        assert not (TGA / mood / "SUINE.TGA").exists()
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest saturn/tests/test_category_art.py -v`
Expected: FAIL — `category_art.inc` does not exist, so `parse_inc` raises `FileNotFoundError`.

- [ ] **Step 3: Change `make_tga.py` to write per-mood folders**

In `tools/make_tga.py`, replace the batch-walk that flattens every subfolder into one destination. The new rule: the **first** path component under the source root is the mood; everything below it is provenance and is flattened away.

```python
def mood_of(src_root, path):
    """The mood folder a source picture belongs to: the first component under the root."""
    return path.relative_to(src_root).parts[0]


def convert_tree(src_root, dst_root):
    """Convert every source picture into dst_root/<MOOD>/NN.TGA and return per-mood counts."""
    counts = {}
    for mood in sorted({p for p in (d.name for d in src_root.iterdir() if d.is_dir())}):
        out_dir = dst_root / mood
        out_dir.mkdir(parents=True, exist_ok=True)
        for old in out_dir.glob("*.TGA"):
            old.unlink()

        sources = sorted(
            p for p in (src_root / mood).rglob("*")
            if p.suffix.lower() in SOURCE_EXT
        )
        n = 0
        for src in sources:
            if n >= 99:
                print(f"  {mood}: more than 99 pictures, ignoring {src.name}")
                continue
            try:
                im = Image.open(src).convert("RGB")
            except Exception as exc:
                print(f"  skipped {src}: {exc}")
                continue
            if im.size != (WIDTH, HEIGHT):
                print(f"  skipped {src}: {im.size} is not {WIDTH}x{HEIGHT}")
                continue
            n += 1
            (out_dir / f"{n:02d}.TGA").write_bytes(encode_tga(im))
        counts[mood] = n
        print(f"  {mood}: {n}")
    return counts
```

- [ ] **Step 4: Emit the generated table**

Add to `tools/make_tga.py`:

```python
# TC_* enum order, saturn/src/sound/music.h:45. None carries no art.
ENUM_ORDER = [None, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
              "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE",
              None, None]


def write_inc(counts, path):
    """Write the generated per-category art-count table consumed by display.c."""
    row = ", ".join(str(0 if m is None else counts.get(m, 0)) for m in ENUM_ORDER)
    path.write_text(
        "/*----------------------\n"
        " | category_art.inc\n"
        " | Description: How many pictures each text category carries on this disc.\n"
        " |   GENERATED by tools/make_tga.py -- do not edit. Row order is the TC_*\n"
        " |   enum order in sound/music.h; the three zero rows are TC_NEUTRAL,\n"
        " |   TC_DANGER and TC_TRIUMPH, which hold whatever is showing.\n"
        " | Author: suinevere\n"
        " ----------------------*/\n"
        "static const unsigned char CATEGORY_ART_N[TEXT_NUM_CATEGORIES] = {\n"
        f"    {row}\n"
        "};\n"
    )
```

Call both from the batch path: `counts = convert_tree(src, dst)` then
`write_inc(counts, REPO / "saturn" / "src" / "video" / "category_art.inc")`.

- [ ] **Step 5: Move the existing 37 pictures into mood folders**

The source tree already has one folder per mood. Only the *destination* changes shape, so regenerating is enough:

```bash
rm -f saturn/cd/data/TGA/*.TGA
git checkout saturn/cd/data/TGA/SUINE.TGA 2>/dev/null || true
sh tools/convert-backgrounds.sh
ls saturn/cd/data/TGA/
```

Expected: twelve directories plus `SUINE.TGA`. If `SUINE.TGA` was removed, restore it from git — it is the boot splash and is not produced by the mood walk.

- [ ] **Step 6: Run the tests and watch them pass**

Run: `python -m pytest saturn/tests/test_category_art.py -v`
Expected: all six PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/make_tga.py saturn/tests/test_category_art.py \
        saturn/src/video/category_art.inc saturn/cd/data/TGA
git commit -m "build: convert backgrounds into one disc folder per mood and generate the art count table"
```

---

### Task 2: Virtual image slots in display.c

**Files:**
- Modify: `saturn/src/video/display.c:141-310` (registry), `:344-406` (`CATEGORY_IMAGE`), `:694-730` (blob helpers)
- Modify: `saturn/src/video/display.h:138-190` (image API block)
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: `CATEGORY_ART_N[]` from `category_art.inc` (Task 1).
- Produces, all in `display.h`:
  - `int display_slot_valid(int slot)` — 1 when `slot` names a picture this disc carries, else 0. `DISP_IMAGE_NONE` is not valid.
  - `int display_slot_make(int cat, int index)` — the slot for a 1-based index, or `DISP_IMAGE_NONE` if out of range.
  - `const char *display_image_file(int slot)` — **unchanged signature**, now synthesising `"HORROR/07.TGA"` into a rotating buffer; `""` for an invalid slot.
  - `int display_image_slot(const char *name)` — **unchanged signature**, now parsing `"HORROR/07.TGA"` back.
  - `int display_image_count(void)` — **unchanged signature**, now the sum of `CATEGORY_ART_N`.
- Deleted: `display_set_images`, `g_image_names`, `DISP_IMAGE_MAX`, `image_slot_name`.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_display.c`, and add the call to `main`:

```c
static void test_virtual_slots(void) {
    /* Indices are checked against the count this disc actually carries, so the
       test does not go stale every time a mood gains a picture. */
    int n    = display_category_image_count(TC_HORROR);
    int slot = display_slot_make(TC_HORROR, n);
    assert(n >= 1);
    assert(slot == TC_HORROR * 100 + n);
    assert(display_slot_valid(slot));
    {
        char want[16];
        sprintf(want, "HORROR/%02d.TGA", n);
        assert(strcmp(display_image_file(slot), want) == 0);
        assert(display_image_slot(want) == slot);
    }

    /* Index 0 is never a filename, one past the end is not carried, and the
       sparse space rejects between moods. */
    assert(display_slot_make(TC_HORROR, 0) == DISP_IMAGE_NONE);
    assert(display_slot_make(TC_HORROR, n + 1) == DISP_IMAGE_NONE);
    assert(!display_slot_valid(TC_HORROR * 100));
    assert(!display_slot_valid(-1));

    /* Categories that carry no art reject every index. */
    assert(display_slot_make(TC_NEUTRAL, 1) == DISP_IMAGE_NONE);
    assert(display_slot_make(TC_DANGER, 1)  == DISP_IMAGE_NONE);

    /* An old blob's flat name no longer resolves, which is the intended miss. */
    assert(display_image_slot("HOUSE1.TGA") == DISP_IMAGE_NONE);
    assert(display_image_slot("") == DISP_IMAGE_NONE);
    assert(display_image_slot(NULL) == DISP_IMAGE_NONE);

    /* Two live filenames at once: display_image_file must rotate its buffers. */
    {
        const char *a = display_image_file(display_slot_make(TC_HOUSE, 1));
        const char *b = display_image_file(display_slot_make(TC_TOWN, 2));
        assert(strcmp(a, "HOUSE/01.TGA") == 0);
        assert(strcmp(b, "TOWN/02.TGA") == 0);
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: FAIL to compile — `display_slot_make` and `display_slot_valid` are not declared.

- [ ] **Step 3: Replace the registry with the mood table**

In `display.c`, delete `g_image_names`, `display_set_images`, and `image_slot_name`. Replace the twelve `IMG_*` arrays and `CATEGORY_IMAGE` with:

```c
/*----------------------
 | CATEGORY_DIR / CATEGORY_ART_N
 | Description: The disc folder each text category's pictures live in, and how
 |   many it carries. The folder name is the only name a picture has now -- files
 |   inside it are a bare two-digit index -- so a slot is (category, index) rather
 |   than a position in a scanned list.
 |
 |   Row order is the TC_* enum order in music.h and has to stay that way. The
 |   three empty rows are TC_NEUTRAL, TC_DANGER and TC_TRIUMPH: a room that named
 |   nothing and a moment that is not a place both hold whatever is showing.
 |
 |   CATEGORY_ART_N is generated by tools/make_tga.py from the files that actually
 |   converted, so a count can never name a picture the disc lacks.
 | Author: suinevere
 ----------------------*/
static const char *const CATEGORY_DIR[TEXT_NUM_CATEGORIES] = {
    0, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
    "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE", 0, 0
};

#include "category_art.inc"

#define SLOT_STRIDE 100
```

- [ ] **Step 4: Add the slot primitives**

```c
/*----------------------
 | display_slot_make
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: cat -- the TC_* category; index -- 1-based, 1..the category's count
 | Returns: the slot, or DISP_IMAGE_NONE when the category or index is out of range
 ----------------------*/
int display_slot_make(int cat, int index) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return DISP_IMAGE_NONE;
    if (index < 1 || index > (int) CATEGORY_ART_N[cat]) return DISP_IMAGE_NONE;
    return cat * SLOT_STRIDE + index;
}

/*----------------------
 | display_slot_valid
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: 1 when the slot names a picture this disc carries, else 0
 ----------------------*/
int display_slot_valid(int slot) {
    if (slot < 0) return 0;
    return display_slot_make(slot / SLOT_STRIDE, slot % SLOT_STRIDE) == slot;
}
```

- [ ] **Step 5: Synthesise and parse filenames**

```c
/*----------------------
 | g_file_buf
 | Description: Two rotating buffers for display_image_file, so one screen draw
 |   can hold two filenames at once -- the same reason display_image_label keeps
 |   two. "UNDRGRND/99.TGA" is 15 characters plus a terminator.
 | Author: suinevere
 ----------------------*/
static char g_file_buf[2][16];
static int  g_file_turn = 0;

/*----------------------
 | display_image_file
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_DIR, g_file_buf, g_file_turn
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: the disc path, or "" when the slot names no picture
 ----------------------*/
const char *display_image_file(int slot) {
    char *out;
    const char *dir;
    int index, k = 0;

    if (!display_slot_valid(slot)) return "";
    dir   = CATEGORY_DIR[slot / SLOT_STRIDE];
    index = slot % SLOT_STRIDE;

    out = g_file_buf[g_file_turn];
    g_file_turn ^= 1;

    while (*dir) out[k++] = *dir++;
    out[k++] = '/';
    out[k++] = (char) ('0' + index / 10);
    out[k++] = (char) ('0' + index % 10);
    out[k++] = '.'; out[k++] = 'T'; out[k++] = 'G'; out[k++] = 'A';
    out[k]   = '\0';
    return out;
}

/*----------------------
 | image_slot_of
 | Description: The slot a disc path names, or -1. Parses the form
 |   display_image_file writes and nothing else, so a flat name from an older save
 |   blob misses -- which is the wanted answer, since the Palette row stopped
 |   honouring pinned pictures.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_DIR
 | Params: name -- a disc path; NULL and "" both miss
 | Returns: the slot, or -1
 ----------------------*/
static int image_slot_of(const char *name) {
    int cat, i, index;
    const char *dir, *p;

    if (!name || !name[0]) return -1;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        dir = CATEGORY_DIR[cat];
        if (!dir) continue;
        for (i = 0; dir[i] && name[i] == dir[i]; i++) { }
        if (dir[i] || name[i] != '/') continue;
        p = name + i + 1;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
        index = (p[0] - '0') * 10 + (p[1] - '0');
        if (strcmp(p + 2, ".TGA") != 0) return -1;
        return display_slot_make(cat, index);
    }
    return -1;
}
```

`display_image_slot` keeps its existing body, which already wraps `image_slot_of`
and maps a negative result onto `DISP_IMAGE_NONE`.

- [ ] **Step 6: Recompute the total**

```c
/*----------------------
 | display_image_count
 | Description: See display.h. Only ever compared against zero now -- the slot
 |   space is sparse, so this is not an upper bound and display_slot_valid is what
 |   answers "is this slot real".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: N/A
 | Returns: how many pictures the disc carries in total
 ----------------------*/
int display_image_count(void) {
    int cat, n = 0;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) n += (int) CATEGORY_ART_N[cat];
    return n;
}
```

Rewrite `display_category_image` and `display_category_image_count` against the
new tables:

```c
const char *display_category_image(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    if (CATEGORY_ART_N[cat] == 0) return 0;
    return display_image_file(display_slot_make(
        cat, (int) (g_cat_rot[cat] % CATEGORY_ART_N[cat]) + 1));
}

int display_category_image_count(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    return (int) CATEGORY_ART_N[cat];
}
```

Update the same `CATEGORY_ART_N[cat]` substitution in `display_shuffle_category`
(`display.c:470`) and wherever `CATEGORY_IMAGE[cat].n` or `.p` still appears.

- [ ] **Step 7: Update display.h**

Delete the `display_set_images` declaration and the `DISP_IMAGE_MAX` define.
Declare the two new functions with full header blocks, and rewrite the
`display_image_file` block to say the name is synthesised. Keep
`DISP_IMAGE_NAME_MAX` at 13 — the blob layout still reserves that field.

- [ ] **Step 8: Run the tests and watch them pass**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: PASS, including every test that already existed in the file.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/display.c saturn/src/video/display.h saturn/tests/test_display.c
git commit -m "display: encode image slots as category and index so filenames are synthesised rather than scanned"
```

---

### Task 3: Replace every count-as-upper-bound test

**Files:**
- Modify: `saturn/src/video/display.c:230-231`, `:257`, `:576`, `:597`, `:685`
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: `display_slot_valid` (Task 2).
- Produces: no new API. This task is a correctness sweep.

The slot space is now sparse: slot 1007 is valid while 1050 may not be. Every
surviving `slot < g_image_count` is a latent bug that shows only as a wallpaper
silently failing to appear. These are the five sites, enumerated so none is
missed:

| Site | Function | Change |
|---|---|---|
| `display.c:230` | `display_dynamic_slot` | `g_dyn_pin >= 0 && g_dyn_pin < g_image_count` → `display_slot_valid(g_dyn_pin)` |
| `display.c:231` | `display_dynamic_slot` | same for `g_dyn_slot` |
| `display.c:257` | `display_pin_dynamic_slot` | `(slot >= 0 && slot < g_image_count)` → `display_slot_valid(slot)` |
| `display.c:576` | `display_defaults` | `g_image_count > 0` — **keep**, this is the "any art at all" question |
| `display.c:597` | `display_is_image` | `d->image >= 0 && d->image < g_image_count` → `display_slot_valid(d->image)` |
| `display.c:685` | `display_cycle_palette` | `g_image_count == 0` — **keep**, same question |

- [ ] **Step 1: Write the failing test**

```c
static void test_sparse_slot_space(void) {
    DisplayState d;
    int good = display_slot_make(TC_HOUSE, 1);
    int gap  = TC_HOUSE * 100;              /* index 0: never a file */

    display_defaults(&d);

    d.image = good;
    assert(display_is_image(&d));
    d.image = gap;
    assert(!display_is_image(&d));          /* a naive < count test passes this */
    d.image = DISP_IMAGE_NONE;
    assert(!display_is_image(&d));

    display_pin_dynamic_slot(gap);
    assert(display_dynamic_slot() != gap);
    display_pin_dynamic_slot(good);
    assert(display_dynamic_slot() == good);
    display_pin_dynamic_slot(DISP_IMAGE_NONE);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: FAIL at `assert(!display_is_image(&d))` — `TC_HOUSE * 100` is below the
total count and the old test accepts it.

- [ ] **Step 3: Apply the four substitutions**

Change exactly the four rows marked → in the table above. Leave the two marked
**keep** alone; update the `Globals:` line of each touched header block to name
`CATEGORY_ART_N` where it used to name `g_image_count`.

- [ ] **Step 4: Run the tests and watch them pass**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: PASS.

- [ ] **Step 5: Verify no upper-bound test survives**

Run: `grep -n "g_image_count" saturn/src/video/display.c`
Expected: exactly two hits, both `> 0` or `== 0` comparisons, at `display_defaults`
and `display_cycle_palette`. Any `<` comparison is a missed site — fix it and
re-run Step 4.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/video/display.c saturn/tests/test_display.c
git commit -m "display: validate image slots by lookup rather than by range now that the slot space is sparse"
```

---

### Task 4: Delete the boot scan and enter mood folders on load

**Files:**
- Modify: `saturn/src/video/title.cxx:240-277` (`display_scan_images`, deleted), the TGA open path
- Modify: `saturn/src/video/title.h` (remove the `display_scan_images` declaration if present)
- Modify: `saturn/src/main.cxx` (remove the `display_scan_images()` call)
- Test: `saturn/tests/test_cd_mood_dirs.py`

**Interfaces:**
- Consumes: `display_image_file` returning `"HORROR/07.TGA"` (Task 2).
- Produces: `static bool cd_enter_mood(const char *path, const char **leaf)` in
  `title.cxx` — enters the directory named by the part of `path` before `/`,
  writes the part after it to `*leaf`, and returns false if the directory is
  absent. Callers must call `cd_enter_root()` afterwards.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_cd_mood_dirs.py`:

```python
"""Gate the disc-side contract the mood folders rest on.

This is a source and layout test, not a hardware test -- the runtime half is
verified on hardware per the plan's Task 4 Step 7, which is the only way to
observe CD-DA surviving a directory change.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TITLE = (REPO / "saturn" / "src" / "video" / "title.cxx").read_text()
MAIN = (REPO / "saturn" / "src" / "main.cxx").read_text()


def test_boot_scan_is_gone():
    assert "display_scan_images" not in TITLE
    assert "display_scan_images" not in MAIN


def test_every_mood_entry_returns_to_root():
    """cd_enter_mood must be paired with cd_enter_root on every path that uses it."""
    enters = len(re.findall(r"\bcd_enter_mood\s*\(", TITLE))
    roots = len(re.findall(r"\bcd_enter_root\s*\(", TITLE))
    assert enters > 0, "cd_enter_mood is not used"
    assert roots >= enters, f"{enters} mood entries but only {roots} returns to root"


def test_no_flat_tga_names_remain():
    assert not re.search(r'"[A-Z]{4,8}\d\.TGA"', TITLE), \
        "a flat, pre-folder TGA filename is still hard-coded"
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python -m pytest saturn/tests/test_cd_mood_dirs.py -v`
Expected: FAIL — `display_scan_images` is still present in `title.cxx`.

- [ ] **Step 3: Delete the boot scan**

Remove `display_scan_images` entirely (`title.cxx:240-277`), along with
`g_tga_dirnames`, `g_tga_tbl`, `g_tga_dir_valid`, and the
`display_set_images` call. Remove its call site in `main.cxx` and its
declaration from `title.h`. `cd_enter_root` stays — the load path still uses it.

- [ ] **Step 4: Add the mood-folder entry**

```cxx
/*----------------------
 | cd_enter_mood
 | Description: Enters the mood directory named by the part of `path` before the
 |   slash, and hands back the bare filename after it. Every caller must
 |   cd_enter_root() once the file is open, because the working directory is
 |   process-wide and a game catalog scan that starts inside /TGA/HORROR finds
 |   nothing.
 |
 |   A path with no slash is left alone and reported as already at the root, which
 |   is how SUINE.TGA still loads.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: path -- "HORROR/07.TGA"; leaf -- receives "07.TGA"
 | Returns: true when the leaf can now be opened
 ----------------------*/
static bool cd_enter_mood(const char *path, const char **leaf) {
    char dir[16];
    int i = 0;

    while (path[i] && path[i] != '/' && i < (int) sizeof(dir) - 1) {
        dir[i] = path[i];
        i++;
    }
    if (path[i] != '/') { *leaf = path; return true; }
    dir[i] = '\0';
    *leaf = path + i + 1;

    cd_enter_root();
    return SRL::Cd::ChangeDir(dir) >= 0;
}
```

- [ ] **Step 5: Use it on the load path**

The open is `SRL::Cd::File f(file);` at `title.cxx:517`. Wrap it:

```cxx
    const char *leaf = file;
    if (!cd_enter_mood(file, &leaf)) { cd_enter_root(); return false; }
    SRL::Cd::File f(leaf);
    /* ... existing read, decode and return of `f`, unchanged ... */
    cd_enter_root();
```

Every early return between the entry and the final `cd_enter_root()` must call
`cd_enter_root()` first. This is the failure `mem/MEMORY.md` records: a capture
left pointing at the wrong directory mutes the drive for the rest of the session.

- [ ] **Step 6: Run the tests and watch them pass**

Run: `python -m pytest saturn/tests/test_cd_mood_dirs.py -v`
Expected: all three PASS.

- [ ] **Step 7: Ask the user to verify on hardware**

Do not build. Hand the user this check and wait for the result:

> Boot the disc, start a game with a CD-DA track playing, and walk between rooms
> of different moods until the wallpaper changes several times. Confirm: the
> pictures appear, the music does not stop, and returning to the game-select
> menu still lists every game.

The third is the one that catches a missing `cd_enter_root` — the catalog scan
runs from the working directory.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/video/title.cxx saturn/src/video/title.h saturn/src/main.cxx \
        saturn/tests/test_cd_mood_dirs.py
git commit -m "title: open backgrounds from their mood folder and drop the boot-time TGA scan"
```

---

### Task 5: A held wallpaper offset that composes with the transition ramp

**Files:**
- Modify: `saturn/src/video/title.cxx:1183` (`title_bg_dyn_fade`), `:1140-1152` (fade disengage)
- Modify: `saturn/src/video/title.h:151-170`
- Test: `saturn/tests/test_bg_hold.c`

**Interfaces:**
- Consumes: nothing from earlier tasks. Independent of Tasks 1–4.
- Produces:
  - `void title_bg_dim_set(int offset)` — holds `offset` in `-255..+255`; 0 releases.
  - `int title_bg_dim_get(void)` — the held offset.
  - `int title_bg_effective(int level, int hold)` — pure, testable on the host:
    returns `clamp(hold + (level - 255), -255, +255)`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_bg_hold.c`:

```c
/* Build:
     gcc -O2 -I saturn/src -o /tmp/tbh saturn/tests/test_bg_hold.c && /tmp/tbh
   title_bg_effective is duplicated here rather than linked: title.cxx pulls in
   SRL and cannot build on the host, and this arithmetic is the whole of what
   needs proving. Keep the two copies identical. */
#include <assert.h>
#include <stdio.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int title_bg_effective(int level, int hold) {
    return clampi(hold + (level - 255), -255, 255);
}

int main(void) {
    /* No hold: byte-identical to the behaviour before this change. */
    assert(title_bg_effective(255, 0) == 0);
    assert(title_bg_effective(0, 0)   == -255);
    assert(title_bg_effective(128, 0) == -127);

    /* A darkening hold rests dark and still dips to black. */
    assert(title_bg_effective(255, -96) == -96);
    assert(title_bg_effective(0, -96)   == -255);

    /* A lightening hold rests light and still dips. */
    assert(title_bg_effective(255, 64) == 64);
    assert(title_bg_effective(0, 64)   == -191);

    /* Only a resting, unheld wallpaper releases the channel. */
    assert(title_bg_effective(255, 0) == 0);
    assert(title_bg_effective(255, -32) != 0);

    printf("test_bg_hold: ok\n");
    return 0;
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `gcc -O2 -I saturn/src -o /tmp/tbh saturn/tests/test_bg_hold.c && /tmp/tbh`
Expected: PASS immediately — the reference implementation is inside the test.
This test's job is to pin the arithmetic *before* `title.cxx` is edited, so the
port can be compared against it. Record the output; Step 5 re-runs it.

- [ ] **Step 3: Add the hold to title.cxx**

```cxx
/*----------------------
 | g_bg_hold / title_bg_dim_set / title_bg_dim_get
 | Description: A wallpaper offset the player chose, held across rooms. Composes
 |   additively with title_bg_dyn_fade's transition ramp, so a held dim rests at
 |   its own level and a transition still dips to black through it.
 |
 |   Negative darkens, positive lightens -- VDP2's colour offset is signed, and a
 //   black text preset wants a lighter wallpaper as much as a white one wants a
 |   darker. Zero releases channel B entirely, which is exactly the behaviour
 |   before this existed.
 | Author: suinevere
 ----------------------*/
static int g_bg_hold = 0;

void title_bg_dim_set(int offset) {
    if (offset < -255) offset = -255;
    if (offset >  255) offset =  255;
    g_bg_hold = offset;
    title_bg_dyn_fade(255);
}

int title_bg_dim_get(void) { return g_bg_hold; }

/*----------------------
 | title_bg_effective
 | Description: The colour-offset value for a ramp level and a held offset.
 |   Separated out and kept pure so saturn/tests/test_bg_hold.c can pin it on the
 |   host, where SRL cannot build.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: level -- 0 (black) to 255 (unmodified); hold -- -255..+255
 | Returns: the signed per-channel offset, -255..+255
 ----------------------*/
int title_bg_effective(int level, int hold) {
    int v = hold + (level - 255);
    if (v < -255) v = -255;
    if (v >  255) v =  255;
    return v;
}
```

- [ ] **Step 4: Rewrite title_bg_dyn_fade around it**

```cxx
void title_bg_dyn_fade(int level) {
    int v;
    if (level < 0)   level = 0;
    if (level > 255) level = 255;

    v = title_bg_effective(level, g_bg_hold);

    if (v == 0) {
        if (g_dyn_faded) {
            SRL::VDP2::ColorOffset clear(0, 0, 0);
            SRL::VDP2::SetColorOffsetB(clear);
            SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
            g_dyn_faded = false;
        }
        return;
    }

    if (!g_dyn_faded) {
        SRL::VDP2::NBG3::UseColorOffset(SRL::VDP2::OffsetChannel::NoOffset);
        SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetB);
        g_dyn_faded = true;
    }
    SRL::VDP2::ColorOffset off((int16_t) v, (int16_t) v, (int16_t) v);
    SRL::VDP2::SetColorOffsetB(off);
}
```

Update the function's header block: `level` still runs 0..255, but the resting
state is now the held offset rather than unmodified, and channel B is released
only when both are neutral.

- [ ] **Step 5: Re-apply the hold when a screen-wide fade releases NBG0**

`title_fade_engage` moves NBG0 to channel A and clears `g_dyn_faded`. A scroll
uses A or B, not both, so the hold is silently dropped. At the end of
`title_fade_disengage` (`title.cxx:1149`), after both layers are set to
`NoOffset`, add:

```cxx
    title_bg_dyn_fade(255);
```

That re-engages channel B when, and only when, a hold is set.

- [ ] **Step 6: Run the test and check the port matches**

Run: `gcc -O2 -I saturn/src -o /tmp/tbh saturn/tests/test_bg_hold.c && /tmp/tbh`
Expected: PASS. Then diff the two bodies by eye — the copy in `title.cxx` and the
copy in the test must be character-identical inside the braces.

- [ ] **Step 7: Cross-compile to check syntax**

Run the project's SH-2 cross-compile of `title.cxx` into a scratch directory.
**Do not run `compile.bat`** — the user runs all builds.

- [ ] **Step 8: Ask the user to verify the fade interaction on hardware**

This is the hazard Step 5 exists for, and the only way to observe it is to run it.
The row that sets a dim does not exist until Task 7, so drive it from a temporary
call: set `g_bg_hold = -96;` at the top of `title_bg_dim_set` for this check only,
and remove it before committing.

> With the wallpaper visibly dimmed: enter and leave the Options menu (which runs
> a screen-wide title fade), then return to the game. Confirm the wallpaper is
> **still dimmed** afterwards and has not popped back to full brightness. Then
> walk to a room of a different mood and confirm the transition still dips to
> black and settles back to the dimmed level, not to full.

A wallpaper that brightens after a menu means the re-apply in Step 5 is missing or
is placed before the `NoOffset` writes rather than after them.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/title.cxx saturn/src/video/title.h saturn/tests/test_bg_hold.c
git commit -m "title: hold a player-chosen wallpaper offset that composes with the transition ramp"
```

---

### Task 6: The dim in DisplayState and the save blob

**Files:**
- Modify: `saturn/src/video/display.h` (`DisplayState`, `DISP_DIM_*`, `DISP_BLOB_BYTES`)
- Modify: `saturn/src/video/display.c:758-782` (`display_encode`), `:808+` (`display_decode`)
- Modify: `saturn/src/menu/options.h:22` (`DisplayCycleRow`)
- Modify: `saturn/src/menu/options.cxx:135` (`display_cycle_row`)
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: `title_bg_dim_set` (Task 5), `display_slot_valid` (Task 2).
- Produces:
  - `DisplayState.dim` — an index `0..DISP_DIM_N-1`, default `DISP_DIM_NORMAL`.
  - `#define DISP_DIM_N 7`, `#define DISP_DIM_NORMAL 2`
  - `int display_dim_offset(int index)` — the signed offset for an index.
  - `const char *display_dim_name(int index)` — the row's label.
  - `void display_cycle_dim(DisplayState *d, int dir)`
  - `DCR_DIM` appended to `enum DisplayCycleRow`.
- Blob: sentinel **5**, `DISP_BLOB_BYTES` 17 → 18, layout
  `[5][palette][bg][text][dim][name×13]`.

- [ ] **Step 1: Write the failing test**

```c
static void test_dim_table_and_blob(void) {
    DisplayState d, r;
    unsigned char buf[DISP_BLOB_BYTES];
    int i;

    assert(DISP_DIM_N == 7);
    assert(display_dim_offset(DISP_DIM_NORMAL) == 0);
    assert(display_dim_offset(0) ==  64);
    assert(display_dim_offset(1) ==  32);
    assert(display_dim_offset(3) == -32);
    assert(display_dim_offset(6) == -128);
    for (i = 0; i < DISP_DIM_N; i++) assert(display_dim_name(i)[0] != '\0');
    assert(display_dim_offset(-1) == 0);
    assert(display_dim_offset(DISP_DIM_N) == 0);

    display_defaults(&d);
    assert(d.dim == DISP_DIM_NORMAL);

    d.dim = 6;
    assert(display_encode(&d, buf) == DISP_BLOB_BYTES);
    assert(buf[0] == 5);
    assert(buf[4] == 6);
    assert(display_decode(buf, DISP_BLOB_BYTES, &r) == 1);
    assert(r.dim == 6);

    /* An out-of-range dim is defaulted, not trusted. */
    buf[4] = 99;
    assert(display_decode(buf, DISP_BLOB_BYTES, &r) == 0);
    assert(r.dim == DISP_DIM_NORMAL);
}

static void test_old_blobs_get_no_dim(void) {
    DisplayState d;
    unsigned char old[17];
    int i;

    for (i = 0; i < 17; i++) old[i] = 0;
    old[0] = 4;
    old[1] = DISP_PAL_PRESET0;
    old[2] = DISP_BG_BLACK;
    old[3] = DISP_TEXT_WHITE;

    /* A sentinel-4 blob is 17 bytes where a sentinel-5 blob is 18. The old branch
       must keep measuring against the OLD size, or growing DISP_BLOB_BYTES
       silently rejects every save file already on a memory card. */
    assert(display_decode(old, 17, &d) == 1);
    assert(d.palette == DISP_PAL_PRESET0);
    assert(d.bg   == DISP_BG_BLACK);
    assert(d.text == DISP_TEXT_WHITE);
    assert(d.dim  == DISP_DIM_NORMAL);

    old[0] = 1;
    display_decode(old, 4, &d);
    assert(d.dim == DISP_DIM_NORMAL);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: FAIL to compile — `DISP_DIM_N` is not defined.

- [ ] **Step 3: Add the table to display.h and display.c**

In `display.h`, add the field to `DisplayState`:

```c
    int dim;       /* 0..DISP_DIM_N-1; DISP_DIM_NORMAL is unmodified */
```

and the constants, with a header block explaining that the stops are discrete
because at 8bpp an extreme offset clips distinct palette entries onto the same
value and posterises. Grow `DISP_BLOB_BYTES`:

```c
#define DISP_DIM_N      7
#define DISP_DIM_NORMAL 2
#define DISP_BLOB_BYTES (5 + DISP_IMAGE_NAME_MAX)
```

In `display.c`:

```c
/*----------------------
 | DIM_STOPS / DIM_NAMES
 | Description: The wallpaper offsets the Display row steps through, brightest
 |   first, and their labels. Steps of 32 rather than a continuous slider: the
 |   picture is 8bpp, so a large offset clips distinct palette entries onto one
 |   value and posterises, and a stop the player can name is easier to return to
 |   than a position on a bar.
 | Author: suinevere
 ----------------------*/
static const short DIM_STOPS[DISP_DIM_N] = { 64, 32, 0, -32, -64, -96, -128 };
static const char *const DIM_NAMES[DISP_DIM_N] = {
    "Lighter +2", "Lighter +1", "Normal", "Darker -1",
    "Darker -2", "Darker -3", "Darker -4"
};

int display_dim_offset(int index) {
    if (index < 0 || index >= DISP_DIM_N) return 0;
    return (int) DIM_STOPS[index];
}

const char *display_dim_name(int index) {
    if (index < 0 || index >= DISP_DIM_N) return DIM_NAMES[DISP_DIM_NORMAL];
    return DIM_NAMES[index];
}

void display_cycle_dim(DisplayState *d, int dir) {
    d->dim = step(d->dim, dir, DISP_DIM_N);
}
```

Set `d->dim = DISP_DIM_NORMAL` in both branches of `display_defaults`.

- [ ] **Step 4: Move the blob to sentinel 5**

In `display_encode`, write `out[0] = 5`, `out[4] = (unsigned char) d->dim`, and
shift the name field to `out[5 + i]`.

**Pin the old block size before touching anything else.** The sentinel-2/3/4
branch guards with `if (len < DISP_BLOB_BYTES) return 0;` (`display.c:834`).
`DISP_BLOB_BYTES` just grew to 18, so that line would now reject every 17-byte
blob already written to a memory card. Add the old size and use it there:

```c
/*----------------------
 | DISP_BLOB_BYTES_V4
 | Description: The block size of save forms 2, 3 and 4 -- four header bytes plus
 |   the name. Frozen, and named rather than spelled 17, because DISP_BLOB_BYTES
 |   now describes form 5 and the two must not be confused: measuring an old blob
 |   against the new size rejects every save file already on a card.
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_BYTES_V4 (4 + DISP_IMAGE_NAME_MAX)
```

and change `display.c:834` to `if (len < DISP_BLOB_BYTES_V4) return 0;`.

In `display_decode`, add a `buf[0] == 5` branch **before** the existing
`2 || 3 || 4` branch. It is that branch's logic with the name read from `buf + 5`,
its own guard `if (len < DISP_BLOB_BYTES) return 0;`, and one addition:

```c
        if (buf[4] < DISP_DIM_N) d->dim = (int) buf[4];  else ok = 0;
```

Leave sentinels 1–4 untouched otherwise. They never set `dim`, so `display_defaults` at the
top of `display_decode` leaves it at `DISP_DIM_NORMAL` — today's appearance.
Extend the `DISP_BLOB_BYTES` header block to describe the fifth form and to say
that a sentinel-4 blob is 17 bytes where a sentinel-5 blob is 18, so length and
sentinel agree.

- [ ] **Step 5: Add the cycler row**

In `options.h:22`: `enum DisplayCycleRow { DCR_PALETTE, DCR_BG, DCR_TEXT, DCR_DIM };`

In `options.cxx:135`, inside `display_cycle_row`, alongside the `DCR_BG` case:

```cxx
        if (which == DCR_DIM) {
            display_cycle_dim(&g_display, dir);
            title_bg_dim_set(display_dim_offset(g_display.dim));
        }
```

`DCR_DIM` belongs with `DCR_BG`/`DCR_TEXT` in the non-`DCR_PALETTE` branch: like
a colour change it cannot fail to load, so it must not go through the
retry-until-loadable loop `DCR_PALETTE` uses.

- [ ] **Step 6: Run the tests and watch them pass**

Run: `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
Expected: PASS, including the pre-existing sentinel 1/2/3/4 round-trip tests.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/video/display.c saturn/src/video/display.h \
        saturn/src/menu/options.h saturn/src/menu/options.cxx saturn/tests/test_display.c
git commit -m "display: carry a wallpaper dim in the appearance state and save it under a fifth blob form"
```

---

### Task 7: The Display Options row

**Files:**
- Modify: `saturn/src/menu/menu_pages.cxx:713-714` (row enum and order), `:747-749` (cycling), `:762-770` (drawing)
- Modify: `saturn/src/menu/options.cxx:94-102` (apply path)
- Test: `saturn/tests/test_menu_layout.c`

**Interfaces:**
- Consumes: `DCR_DIM`, `display_dim_name`, `display_dim_offset`, `DisplayState.dim` (Task 6); `title_bg_dim_set` (Task 5).
- Produces: no new API.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_menu_layout.c`:

```c
static void test_dim_row_label_fits(void) {
    /* The Display page numbers its rows and boxes them; "N) Background" ending at
       column 17 is the longest label the box was sized for (menu_pages.cxx:691).
       The new row must not be longer. */
    int i, longest = 0;
    for (i = 0; i < DISP_DIM_N; i++) {
        int n = (int) strlen(display_dim_name(i));
        if (n > longest) longest = n;
    }
    assert(strlen("Dimming") <= strlen("Background"));
    assert(longest <= (int) strlen("Lighter +2"));
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `gcc -O2 -I saturn/src -o /tmp/tm saturn/tests/test_menu_layout.c saturn/src/video/display.c && /tmp/tm`
Expected: FAIL to compile — `display_dim_name` is not declared in this
translation unit until `display.h` is included; add the include if missing, then
it passes and pins the constraint.

- [ ] **Step 3: Add the row**

At `menu_pages.cxx:713`:

```cxx
    enum { DR_PALETTE, DR_BG, DR_TEXT, DR_DIM, DR_OK, DR_CANCEL };
    static const int rows[] = { DR_PALETTE, DR_BG, DR_TEXT, DR_DIM, DR_OK, DR_CANCEL };
```

At `:749`, after the `DR_TEXT` arm:

```cxx
            else if (row == DR_DIM)     display_cycle_row(DCR_DIM,     dir);
```

In the draw switch at `:762`, following the `DR_BG` case's shape exactly:

```cxx
                case DR_DIM:
                    if (nums) text_print(x, y, "%c %d) Dimming", cur, i + 1);
                    else      text_print(x, y, "%c    Dimming", cur);
                    text_print(vx, y, "%s", display_dim_name(g_display.dim));
                    break;
```

- [ ] **Step 4: Apply the hold when the page opens and when it commits**

In `options.cxx`, wherever the Display page applies `g_display` (around `:94`),
add `title_bg_dim_set(display_dim_offset(g_display.dim));`. This gives the live
preview: `display_cycle_row` already calls it on every press, and this covers
entering the page and cancelling back out of it.

Cancelling must restore the *saved* dim, not the edited one. Wherever the page
restores its pre-edit `DisplayState` on cancel, the `title_bg_dim_set` call above
runs against the restored state and does the right thing — verify that ordering
rather than assuming it.

- [ ] **Step 5: Run the test and watch it pass**

Run: `gcc -O2 -I saturn/src -o /tmp/tm saturn/tests/test_menu_layout.c saturn/src/video/display.c && /tmp/tm`
Expected: PASS.

- [ ] **Step 6: Cross-compile to check syntax**

Cross-compile `menu_pages.cxx` and `options.cxx` to scratch. Do not run
`compile.bat`.

- [ ] **Step 7: Ask the user to verify on hardware**

> Open Options → Display. Confirm: a **Dimming** row sits below Text; Left/Right
> changes the wallpaper brightness immediately while the row is selected; the
> menu text stays at full brightness throughout; Cancel restores the previous
> setting; and the menu's CD-DA track keeps playing the whole time.

The last point is what `display_pin_dynamic_slot` exists for and is the check
that the dim did not accidentally trigger a reload.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu/menu_pages.cxx saturn/src/menu/options.cxx saturn/tests/test_menu_layout.c
git commit -m "menu: add a Dimming row to Display options that previews live"
```

---

## Done when

- `saturn/cd/data/TGA/` holds twelve mood folders plus `SUINE.TGA`, and the disc
  shows its existing 37 pictures from them.
- `grep -rn "display_scan_images\|display_set_images\|DISP_IMAGE_MAX" saturn/src`
  returns nothing.
- `grep -n "g_image_count" saturn/src/video/display.c` returns exactly two hits,
  both zero comparisons.
- All host tests pass:
  `gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c saturn/src/video/display.c && /tmp/td`
  and `gcc -O2 -I saturn/src -o /tmp/tbh saturn/tests/test_bg_hold.c && /tmp/tbh`
- `python -m pytest saturn/tests/ -v` passes.
- The user has confirmed both hardware checks (Task 4 Step 7, Task 7 Step 7).

Plan B may begin at any point; its only dependency is the folder layout fixed in
Task 1.
