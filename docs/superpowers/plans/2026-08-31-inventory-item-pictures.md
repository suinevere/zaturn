# Inventory Item Pictures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the Japanese Zork I disc's own 64x80 painting of a carried treasure in a pane beside the gamepad inventory overlay's item list, blank when the item has no picture.

**Architecture:** `OITEM.CZ` joins the existing `/BG` injection so it ships on the disc. A host-testable decoder (`oitem.c`, reusing `cgl.c`'s LZSS) expands one picture and its CLUT from generated record offsets. A runtime module (`item_art.cxx`, modelled line for line on `room_art.cxx`) holds the archive only while the overlay is open and uploads the picture to a VDP2 NBG1 bitmap in bank A1. A hand-authored name-keyed JSON, checked by a refusing generator against `ZORK1.Z3`, binds nineteen object numbers to nineteen picture indices. The overlay grows from 9 rows to 12 only when the running story has that table.

**Tech Stack:** C99 + C++ (SH-2, SaturnRingLib/SGL), Python 3 generators, gcc host tests, pytest, xorriso via `tools/assets/*.bat`.

**Spec:** [`docs/superpowers/specs/2026-08-31-inventory-item-pictures-design.md`](../specs/2026-08-31-inventory-item-pictures-design.md)

## Global Constraints

- **Author of record is `suinevere`.** Every function, constant and file gets the project's header-block comment form. No comments inside function bodies. Tests and generated files get a file header only.
- **Commit after every change. One sentence, no body, no bullets, no trailers.** Never mention Claude, AI, or the session; no `Claude-Session:` line and no `claude.ai/code` URL.
- **Layout:** the entry point is the only file in `saturn/src/` root; everything else lives in a subfolder named for its concern. New video code goes in `saturn/src/video/`, new tables in `saturn/src/scene/`.
- **Story identity:** release `88`, serial `840726`. Any generator that sees anything else must raise, not warn.
- **Archive identity:** `OITEM.CZ` is 40,840 bytes, SHA-256 `04344f3bbc6404ab6163e0d2df16614e4fc67d53855a1472baab3cfe9f54a2e0`.
- **Geometry:** screen 40x30 cells of 8px. Today the strip is rows 21–29 (border 21, content 22–28, border 29) and the overlay box is rows 22–28. Tall: input line row 15, strip rows 16–29 (14), overlay box rows 17–28 (12), columns 2–35 inclusive. Interior: list columns 3–25, divider column 26, pane columns 27–34. Picture origin x=216, y=144, size 64x80.
- **Picture set:** 19 pictures, indices 0–18, each 5,120 bytes of 64x80 8bpp. 19 CLUTs, records 19–37, 512 bytes each. Picture *i* pairs with record *19+i*.
- **Never call `TransparentDisable()` on NBG1.** `main.cxx:361` calls it on NBG0 deliberately; NBG1 needs VDP2's default index-0 transparency so the unused container reads as clear.
- **Generated files regenerate byte-identically.** `.gitattributes` already pins `saturn/src/scene/**` to `eol=lf`.
- **No new netbin sources.** `saturn/tests/test_netbin_sources.py` pins the netbin source list exactly; every `item_art` include and call site goes behind `#ifndef NETBIN`, as `dash_view.cxx:16` does for the wallpaper.

---

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `tools/gen_oitem.py` | Walks `OITEM.CZ`, emits both the runtime record table and the host-test checksum fixture |
| `saturn/src/scene/oitem_records.inc` | GENERATED. 38 (offset, length) pairs |
| `saturn/tests/fixtures/oitem_sums.inc` | GENERATED. 19 rows of expected pixel/palette checksums |
| `saturn/src/video/oitem.c` / `.h` | The container layout on top of `cgl.c`'s LZSS. Pure logic, no SRL |
| `saturn/tests/test_oitem.c` | Host proof that the C port decodes all 19 correctly |
| `tools/assets/zork1_items.json` | Hand-authored picture index → object name |
| `tools/gen_items.py` | Resolves names against `ZORK1.Z3`, refuses on five conditions, emits the table |
| `saturn/src/scene/game_items.inc` | GENERATED. Object → picture index, keyed by release/serial |
| `saturn/src/scene/items.c` / `.h` | Runtime lookup over that table |
| `saturn/tests/test_items.c` | Host proof of the lookup |
| `tools/tests/test_gen_items.py` | The generator's five refusals and byte-identical regeneration |
| `saturn/src/video/item_art.cxx` / `.h` | Disc read, decode, NBG1 upload, and the refusal policy |
| `saturn/tests/test_overlay_layout.c` | The two overlay geometries, non-overlapping |

**Modified:**

| Path | Change |
|---|---|
| `tools/extract_bg.py:34-46` | One `BG_MANIFEST` row |
| `saturn/tests/test_bg_manifest.py` | New file, but grouped here: manifest matches the tracked bytes |
| `saturn/src/video/cgl.c` / `.h` | Extract `cgl_lzss`; `cgl_decode` becomes a wrapper |
| `saturn/src/video/dash_map.h` / `.c` | New `DASH_OVERLAY_TALL` variant |
| `saturn/tests/test_dash_map.c` | Cover the new variant |
| `saturn/src/video/command_view.cxx` | Two overlay geometries, the pane, the `item_art` calls |
| `saturn/src/main.cxx:619` | `item_art_set_game` beside `room_art_set_game` |
| `saturn/makefile` | Nothing — the CD build's `find` glob picks the new sources up automatically |
| `saturn/tests/test_lwram_budget.py` | The 46,472-byte overlay term |

---

## Task 1: Spike — prove NBG1 can carry a bitmap at all

The spec names this the one real risk. A second VDP2 bitmap layer costs VRAM access cycles, and the pattern with NBG0-bitmap + NBG1-bitmap + NBG2-tiles + NBG3-tiles may not be satisfiable. The failure is silent — a layer that does not draw, or draws as static, or takes NBG3's text down with it. Find out before anything else exists.

**Files:**
- Modify: `saturn/src/main.cxx` (temporary, reverted at the end of this task)

**Interfaces:**
- Consumes: nothing
- Produces: a decision recorded in the plan — either "NBG1 bitmap confirmed" (Task 6 proceeds as written) or "NBG1 bitmap refused, use tilemap" (Task 6 takes the fallback in Step 7 below)

- [ ] **Step 1: Add a throwaway NBG1 bring-up to `main.cxx`**

Put this immediately after the existing `SRL::VDP2::NBG0::TransparentDisable();` at `main.cxx:361`. It is deliberately crude — a magenta/green checkerboard needs no decoder and no disc, so a failure here is unambiguously the layer and not the data.

```cpp
    /* SPIKE ONLY -- reverted at the end of Task 1. */
    {
        static uint8_t probe[64 * 80];
        for (int y = 0; y < 80; y++)
            for (int x = 0; x < 64; x++)
                probe[y * 64 + x] = (uint8_t) (((x >> 3) ^ (y >> 3)) & 1 ? 1 : 2);

        static SRL::Types::HighColor cols[256];
        cols[0] = SRL::Types::HighColor(0x0000);
        cols[1] = SRL::Types::HighColor((unsigned short) (0x8000 | 0x001f));
        cols[2] = SRL::Types::HighColor((unsigned short) (0x8000 | 0x03e0));

        struct Probe : public SRL::Bitmap::IBitmap {
            uint8_t *px; SRL::Bitmap::Palette *pal;
            uint8_t *GetData() override { return px; }
            SRL::Bitmap::BitmapInfo GetInfo() override {
                return SRL::Bitmap::BitmapInfo(64, 80, pal);
            }
        } bmp;
        bmp.px = probe;
        bmp.pal = new SRL::Bitmap::Palette(cols, 256);

        SRL::VDP2::NBG1::LoadBitmap(&bmp);
        slPriorityNbg1(3);
        SRL::VDP2::NBG1::ScrollEnable();
    }
```

If `RawBitmap` (used by `title.cxx:469`) is already available in this translation unit, use it in place of the local `Probe` struct — it is the same interface and less code. Check `title.cxx:436-480` for its exact shape before writing your own.

- [ ] **Step 2: Type-check both configurations**

Run:
```bash
cd saturn && sh syntax-check.sh src/main.cxx && NETBIN=1 sh syntax-check.sh src/main.cxx
```
Expected: exit 0 for both. The spike is inside the CD-only region of `main.cxx`, which the netbin does not compile, so the netbin check should be unaffected.

- [ ] **Step 3: Build the disc**

Run the project's normal CD build (`compile-cd.bat`, or `make all` from `saturn/` if you are on a real shell — note the auto-memory warning that `make` from git-bash drops the `.c` sources, so use `compile.bat` on Windows).
Expected: a `.cue` / `.iso` pair in `BuildDrop/`.

- [ ] **Step 4: Look at it in Mednafen**

Boot the image and get to the title screen, then into a game with the Dynamic palette so NBG0 carries a wallpaper and NBG2 carries the marble strip.

Check all four, and write down the answer to each:
1. Is the checkerboard visible at the top-left of the screen?
2. Is it stable, or does it shimmer/tear (a starved cycle pattern draws as static)?
3. Is the console text still drawing? (NBG3 is the layer most at risk.)
4. Is the marble strip still drawing? (NBG2.)

- [ ] **Step 5: If all four pass — record the result and revert**

```bash
git checkout saturn/src/main.cxx
```

Note in the task list that NBG1-as-bitmap is confirmed, and that `AutoAllocateBmp` placed it (add a temporary `SRL::Debug::Print` of the returned address before reverting if you want the bank confirmed; A1 begins at `VDP2_VRAM_A1`).

- [ ] **Step 6: If any of the four fail — try the layer alone before concluding**

Temporarily comment out `dash_view`'s NBG2 bring-up and re-run. If NBG1 draws once NBG2 is gone, the diagnosis is the cycle pattern, not the layer. If NBG1 never draws at all, the diagnosis is the allocation — print `SRL::VDP2::NBG1::CellAddress` and check it is non-null and inside a VRAM bank.

- [ ] **Step 7: If the cycle pattern is the problem — switch Task 6 to the tilemap fallback**

Record the decision. Task 6's Step 5 has both paths written out; take the tilemap one. Nothing else in this plan moves: `oitem.c`'s output, the binding, the geometry and the tests are all identical either way. Only how `item_art` puts 5,120 bytes into VRAM changes.

- [ ] **Step 8: Revert and commit nothing**

```bash
git checkout saturn/src/main.cxx
git status --short
```
Expected: clean tree. A spike's output is an answer, not code.

---

## Task 2: Ship `OITEM.CZ` on the disc

**Files:**
- Modify: `tools/extract_bg.py:34-46`
- Create: `saturn/tests/test_bg_manifest.py`

**Interfaces:**
- Consumes: nothing
- Produces: `/BG/OITEM.CZ` on the built disc; `tools/assets/BG/OITEM.CZ` and `saturn/cd/data/BG/OITEM.CZ` in a staged working tree

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_bg_manifest.py`:

```python
#!/usr/bin/env python3
"""Hold BG_MANIFEST against the tracked reverse-engineering copies.

The manifest's size and SHA-256 are load-bearing rather than defensive: the
runtime holds measured byte offsets into these archives, so a wrong entry does
not fail to open -- it decompresses from the wrong offset and shows garbage, or
hangs the LZSS loop, with nothing upstream to say why. analysis/zork_bg/raw/
holds the bytes those offsets were measured against, so the two must agree.

Run as tests: pytest saturn/tests/test_bg_manifest.py
"""
import hashlib
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
RAW = ROOT / "analysis" / "zork_bg" / "raw"

sys.path.insert(0, str(ROOT / "tools"))
from extract_bg import BG_MANIFEST  # noqa: E402


@pytest.mark.parametrize("name", sorted(BG_MANIFEST))
def test_manifest_matches_tracked_bytes(name):
    size, digest = BG_MANIFEST[name]
    data = (RAW / name).read_bytes()
    assert len(data) == size, f"{name}: manifest size {size}, tracked {len(data)}"
    assert hashlib.sha256(data).hexdigest() == digest, f"{name}: digest mismatch"


def test_item_archive_is_in_the_manifest():
    assert "OITEM.CZ" in BG_MANIFEST, (
        "OITEM.CZ must ship for the inventory pane to have any pictures"
    )
```

- [ ] **Step 2: Run it to see it fail**

Run: `python -m pytest saturn/tests/test_bg_manifest.py -v`
Expected: the eleven parametrised cases PASS, `test_item_archive_is_in_the_manifest` FAILS with `assert 'OITEM.CZ' in {...}`.

- [ ] **Step 3: Add the manifest row**

In `tools/extract_bg.py`, inside `BG_MANIFEST`, after the `"BWOD.CGL"` line:

```python
    "OITEM.CZ": (40840, "04344f3bbc6404ab6163e0d2df16614e4fc67d53855a1472baab3cfe9f54a2e0"),
```

Then update the module docstring's `Description:` line, which currently says "Pulls Zork I's eleven room-background archives (B*.CGL)". It now pulls twelve files: the eleven room archives and the item-picture container.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python -m pytest saturn/tests/test_bg_manifest.py -v`
Expected: 13 passed.

- [ ] **Step 5: Prove the extractor actually stages it**

Run:
```bash
python tools/extract_bg.py "cd/Zork I - The Great Underground Empire (Japan)/Zork I - The Great Underground Empire (Japan) (Track 01).bin" -o /tmp/bgcheck
ls -l /tmp/bgcheck
```
Expected: twelve files, `OITEM.CZ` among them at 40,840 bytes. If `extract_bg.py` refuses, the manifest row is wrong — do not "fix" it by loosening the check.

- [ ] **Step 6: Stage into the working tree so later tasks have the file**

Run `bg.bat` the way `update.bat` does, or copy by hand into both locations. **Both are load-bearing and must not be collapsed:** `tools/assets/BG` is what `games.bat` injects and what the released kit ships; `saturn/cd/data/BG` is what makes a local build testable. Confirm:

```bash
ls -l tools/assets/BG/OITEM.CZ saturn/cd/data/BG/OITEM.CZ
```

Neither is committed — `saturn/.gitignore:17` covers `saturn/cd/data/BG/*.CGL` and you should extend it to `OITEM.CZ` in the same line's spirit if it does not already glob.

- [ ] **Step 7: Commit**

```bash
git add tools/extract_bg.py saturn/tests/test_bg_manifest.py saturn/.gitignore
git commit -m "Ship the Japanese Zork I disc's OITEM.CZ item-picture container on the disc, and hold every BG_MANIFEST entry against the tracked bytes its byte offsets were measured from."
```

---

## Task 3: Generate the record table and the test fixture

One generator, two outputs, because both come from the same single walk of the archive and splitting them would mean walking it twice with two chances to disagree.

**Files:**
- Create: `tools/gen_oitem.py`
- Create: `saturn/src/scene/oitem_records.inc` (generated)
- Create: `saturn/tests/fixtures/oitem_sums.inc` (generated)

**Interfaces:**
- Consumes: `analysis/zork_bg/raw/OITEM.CZ`, `analysis/zork_cgl._lzss`
- Produces:
  - `oitem_records.inc` defining `#define OITEM_RECORD_N 38`, `#define OITEM_PIC_N 19`, `#define OITEM_PIC_BYTES 5120`, `#define OITEM_PAL_BYTES 512`, and `static const OitemRecord OITEM_RECORDS[OITEM_RECORD_N]` where `OitemRecord` is `{ unsigned long offset; unsigned long length; }`
  - `oitem_sums.inc` yielding rows of `{ int picture; unsigned long pixel_sum; unsigned long pal_sum; }`

- [ ] **Step 1: Write the generator**

Create `tools/gen_oitem.py`:

```python
#!/usr/bin/env python3
"""/*----------------------
 | gen_oitem.py
 | Description: GENERATES saturn/src/scene/oitem_records.inc and
 |     saturn/tests/fixtures/oitem_sums.inc from the Japanese Zork I disc's
 |     OITEM.CZ. The .inc carries the byte offset and length of all 38 LZSS
 |     records so the runtime can reach record n without decompressing the
 |     n-1 before it -- the same reason game_presentation.inc carries per-frame
 |     offsets. The fixture carries FNV-1a checksums of the pixels and the
 |     palette the Python decoder produces, which test_oitem.c compares its
 |     own output against; that is what makes the C port provable off hardware.
 |
 |     One generator for both because both come out of the same single walk of
 |     the archive. Two generators would walk it twice and could disagree.
 |
 |     Refuses rather than emitting a short table: the archive must be exactly
 |     19 picture records of OITEM_PIC_BYTES followed by exactly 19 palette
 |     records of OITEM_PAL_BYTES, in that order, and must match BG_MANIFEST by
 |     size and SHA-256.
 | Author: suinevere
 | Dependencies: hashlib, pathlib, sys, struct, analysis.zork_cgl, tools.extract_bg
 | Globals: ROOT, RAW, OUT_INC, OUT_FIX, PIC_BYTES, PAL_BYTES, PIC_N
 ----------------------*/"""
import hashlib
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RAW = ROOT / "analysis" / "zork_bg" / "raw" / "OITEM.CZ"
OUT_INC = ROOT / "saturn" / "src" / "scene" / "oitem_records.inc"
OUT_FIX = ROOT / "saturn" / "tests" / "fixtures" / "oitem_sums.inc"

sys.path.insert(0, str(ROOT / "analysis"))
sys.path.insert(0, str(ROOT / "tools"))
import zork_cgl  # noqa: E402
from extract_bg import BG_MANIFEST  # noqa: E402

PIC_BYTES = 5120
PAL_BYTES = 512
PIC_N = 19


def fnv1a(data):
    """/*----------------------
     | fnv1a
     | Description: 32-bit FNV-1a, matching test_oitem.c's own implementation.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: data -- bytes to hash
     | Returns: the 32-bit hash as an int
     ----------------------*/"""
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def saturn_palette(clut):
    """/*----------------------
     | saturn_palette
     | Description: A decompressed 512-byte CLUT as the bytes the Saturn will
     |     hold -- each little-endian RGB555 word with the opaque bit forced on.
     |     The two formats share a channel layout, so there is no channel
     |     arithmetic. Deliberately not routed through zork_cgl.load_clut, whose
     |     expansion to 8-bit channels is lossy in the low bits and would not
     |     match what the C side computes.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: clut -- 512 decompressed bytes
     | Returns: 512 bytes
     ----------------------*/"""
    out = bytearray()
    for i in range(0, PAL_BYTES, 2):
        v = ((clut[i] | (clut[i + 1] << 8)) & 0x7FFF) | 0x8000
        out.append(v & 0xFF)
        out.append((v >> 8) & 0xFF)
    return bytes(out)


def walk(blob):
    """/*----------------------
     | walk
     | Description: Every LZSS record in the archive as (offset, length,
     |     decompressed bytes), following the 4-byte alignment between records.
     | Author: suinevere
     | Dependencies: struct, zork_cgl
     | Globals: N/A
     | Params: blob -- the whole OITEM.CZ
     | Returns: list of (offset, length, data)
     ----------------------*/"""
    recs, pos = [], 0
    while pos + 4 <= len(blob):
        size = struct.unpack_from("<I", blob, pos)[0]
        if size == 0 or size > (1 << 20):
            break
        data, nxt = zork_cgl._lzss(blob, pos)
        if len(data) != size:
            raise SystemExit(f"record at {pos}: declared {size}, expanded {len(data)}")
        recs.append((pos, nxt - pos, data))
        pos = (nxt + 3) & ~3
    return recs


def main(argv):
    """/*----------------------
     | main
     | Description: Verifies the archive, walks it, and writes both outputs.
     | Author: suinevere
     | Dependencies: hashlib, pathlib
     | Globals: RAW, OUT_INC, OUT_FIX, BG_MANIFEST, PIC_BYTES, PAL_BYTES, PIC_N
     | Params: argv -- unused
     | Returns: 0
     ----------------------*/"""
    blob = RAW.read_bytes()
    size, digest = BG_MANIFEST["OITEM.CZ"]
    if len(blob) != size or hashlib.sha256(blob).hexdigest() != digest:
        raise SystemExit("OITEM.CZ does not match BG_MANIFEST")

    recs = walk(blob)
    if len(recs) != PIC_N * 2:
        raise SystemExit(f"expected {PIC_N * 2} records, walked {len(recs)}")
    for i, (_, _, data) in enumerate(recs):
        want = PIC_BYTES if i < PIC_N else PAL_BYTES
        if len(data) != want:
            raise SystemExit(f"record {i}: expected {want} bytes, got {len(data)}")

    lines = [
        "/*----------------------",
        " | oitem_records.inc",
        " | Description: GENERATED FILE -- do not edit by hand; produced by",
        " |   tools/gen_oitem.py. The byte offset and length of every LZSS",
        " |   record in the Japanese Zork I disc's OITEM.CZ. Records 0..18 are",
        " |   64x80 8bpp pictures; records 19..37 are their RGB555 CLUTs, one",
        " |   per picture, so picture i pairs with record OITEM_PIC_N + i.",
        " | Author: suinevere",
        " ----------------------*/",
        "typedef struct {",
        "    unsigned long offset;",
        "    unsigned long length;",
        "} OitemRecord;",
        f"#define OITEM_RECORD_N {len(recs)}",
        f"#define OITEM_PIC_N {PIC_N}",
        f"#define OITEM_PIC_BYTES {PIC_BYTES}",
        f"#define OITEM_PAL_BYTES {PAL_BYTES}",
        "static const OitemRecord OITEM_RECORDS[OITEM_RECORD_N] = {",
    ]
    for off, length, _ in recs:
        lines.append(f"    {{ {off}UL, {length}UL }},")
    lines.append("};")
    OUT_INC.write_text("\n".join(lines) + "\n", newline="\n")

    fix = [
        "/*----------------------",
        " | oitem_sums.inc",
        " | Description: GENERATED FILE -- do not edit by hand; produced by",
        " |   tools/gen_oitem.py. FNV-1a checksums of each picture's pixels and",
        " |   its palette as the Python decoder produces them, for test_oitem.c",
        " |   to compare the C port against.",
        " | Author: suinevere",
        " ----------------------*/",
    ]
    for i in range(PIC_N):
        px = recs[i][2]
        pal = saturn_palette(recs[PIC_N + i][2])
        fix.append(f"    {{ {i}, {fnv1a(px)}UL, {fnv1a(pal)}UL }},")
    OUT_FIX.write_text("\n".join(fix) + "\n", newline="\n")

    print(f"{OUT_INC.relative_to(ROOT)}: {len(recs)} records")
    print(f"{OUT_FIX.relative_to(ROOT)}: {PIC_N} pictures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
```

- [ ] **Step 2: Run it**

Run: `python tools/gen_oitem.py`
Expected:
```
saturn/src/scene/oitem_records.inc: 38 records
saturn/tests/fixtures/oitem_sums.inc: 19 pictures
```

- [ ] **Step 3: Check the output against what was measured**

Run: `head -20 saturn/src/scene/oitem_records.inc && grep -c "UL }," saturn/src/scene/oitem_records.inc`
Expected: 38 rows, and the first four offsets are `0`, `1312`, `2496`, `3904` — these were measured directly off the archive and are the cheapest available sanity check that the walk is right.

- [ ] **Step 4: Prove it regenerates byte-identically**

Run:
```bash
cp saturn/src/scene/oitem_records.inc /tmp/rec1.inc
cp saturn/tests/fixtures/oitem_sums.inc /tmp/sum1.inc
python tools/gen_oitem.py
diff /tmp/rec1.inc saturn/src/scene/oitem_records.inc && diff /tmp/sum1.inc saturn/tests/fixtures/oitem_sums.inc && echo IDENTICAL
```
Expected: `IDENTICAL`.

- [ ] **Step 5: Commit**

```bash
git add tools/gen_oitem.py saturn/src/scene/oitem_records.inc saturn/tests/fixtures/oitem_sums.inc
git commit -m "Generate the OITEM.CZ record table and the decoder's host-test fixture from one walk of the archive, so the runtime can reach any of the nineteen pictures without decompressing the ones before it."
```

---

## Task 4: The decoder

**Files:**
- Modify: `saturn/src/video/cgl.h`, `saturn/src/video/cgl.c`
- Create: `saturn/src/video/oitem.h`, `saturn/src/video/oitem.c`
- Create: `saturn/tests/test_oitem.c`

**Interfaces:**
- Consumes: `OITEM_RECORDS`, `OITEM_PIC_N`, `OITEM_PIC_BYTES`, `OITEM_PAL_BYTES` from Task 3
- Produces:
  - `unsigned long cgl_lzss(const unsigned char *src, unsigned long src_len, unsigned char *dst, unsigned long dst_cap);`
  - `int oitem_count(void);`
  - `int oitem_decode(const unsigned char *archive, unsigned long archive_len, int picture, unsigned char *pixels, unsigned short *clut);`
    — returns 1 on success, 0 on any refusal; `pixels` must hold `OITEM_PIC_BYTES`, `clut` 256 words

- [ ] **Step 1: Write the failing host test**

Create `saturn/tests/test_oitem.c`:

```c
/*----------------------
 | test_oitem.c
 | Description: The C port of the OITEM.CZ decoder against checksums taken from
 |   the Python decoder in tools/gen_oitem.py, over the real archive in
 |   analysis/zork_bg/raw. Run from the repository root:
 |   gcc -O2 -I saturn/src -I saturn/tests -o /tmp/toitem \
 |       saturn/tests/test_oitem.c saturn/src/video/oitem.c \
 |       saturn/src/video/cgl.c && /tmp/toitem
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "video/oitem.h"
#include "video/cgl.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static unsigned long fnv1a(const unsigned char *p, unsigned long n) {
    unsigned long h = 2166136261UL;
    unsigned long i;
    for (i = 0; i < n; i++) { h ^= p[i]; h = (h * 16777619UL) & 0xFFFFFFFFUL; }
    return h;
}

typedef struct {
    int           picture;
    unsigned long pixel_sum;
    unsigned long pal_sum;
} OitemExpect;

static const OitemExpect EXPECT[] = {
#include "fixtures/oitem_sums.inc"
};
#define EXPECT_N ((int) (sizeof(EXPECT) / sizeof(EXPECT[0])))

static unsigned char *slurp(const char *path, unsigned long *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (unsigned char *) malloc((size_t) n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t) n, f) != (size_t) n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (unsigned long) n;
    return buf;
}

int main(void) {
    static unsigned char pixels[OITEM_PIC_BYTES];
    static unsigned char palbytes[OITEM_PAL_BYTES];
    unsigned short clut[256];
    unsigned long len = 0;
    unsigned char *blob = slurp("analysis/zork_bg/raw/OITEM.CZ", &len);
    int i;

    if (blob == NULL) { printf("FAIL cannot read analysis/zork_bg/raw/OITEM.CZ\n"); return 1; }

    check(oitem_count() == EXPECT_N, "oitem_count matches the fixture row count");
    check(EXPECT_N == 19, "the fixture carries nineteen pictures");

    for (i = 0; i < EXPECT_N; i++) {
        int j;
        check(oitem_decode(blob, len, EXPECT[i].picture, pixels, clut) == 1,
              "oitem_decode accepts a valid picture");
        for (j = 0; j < 256; j++) {
            palbytes[j * 2]     = (unsigned char) (clut[j] & 0xff);
            palbytes[j * 2 + 1] = (unsigned char) ((clut[j] >> 8) & 0xff);
        }
        if (fnv1a(pixels, OITEM_PIC_BYTES) != EXPECT[i].pixel_sum) {
            printf("FAIL picture %d pixels\n", EXPECT[i].picture); fails++;
        }
        if (fnv1a(palbytes, OITEM_PAL_BYTES) != EXPECT[i].pal_sum) {
            printf("FAIL picture %d palette\n", EXPECT[i].picture); fails++;
        }
    }

    check(oitem_decode(NULL, len, 0, pixels, clut) == 0, "refuses a null archive");
    check(oitem_decode(blob, len, 0, NULL, clut) == 0, "refuses a null pixel buffer");
    check(oitem_decode(blob, len, 0, pixels, NULL) == 0, "refuses a null palette");
    check(oitem_decode(blob, len, -1, pixels, clut) == 0, "refuses a negative index");
    check(oitem_decode(blob, len, OITEM_PIC_N, pixels, clut) == 0, "refuses an index past the end");
    check(oitem_decode(blob, 100, 0, pixels, clut) == 0, "refuses an archive too short for the record");

    free(blob);
    printf(fails ? "%d FAILURES\n" : "all pass\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -I saturn/tests -o /tmp/toitem saturn/tests/test_oitem.c saturn/src/video/oitem.c saturn/src/video/cgl.c && /tmp/toitem
```
Expected: FAIL — `saturn/src/video/oitem.c: No such file or directory`.

- [ ] **Step 3: Extract `cgl_lzss` from `cgl_decode`**

In `saturn/src/video/cgl.h`, add above `cgl_decode`'s block:

```c
/*----------------------
 | cgl_lzss
 | Description: Expands one Okumura-LZSS stream. The stream begins with its own
 |   4-byte little-endian decompressed size, so this is the whole codec with no
 |   knowledge of what wraps it -- a CGL record's palette prefix, or an OITEM
 |   record's absence of one. Refuses rather than truncating, on the same terms
 |   cgl_decode does.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ring
 | Params: src -- the stream, starting at its size header; src_len -- bytes
 |   available from src; dst -- destination; dst_cap -- capacity of dst
 | Returns: bytes written, or 0 on refusal
 ----------------------*/
unsigned long cgl_lzss(const unsigned char *src, unsigned long src_len,
                       unsigned char *dst, unsigned long dst_cap);
```

In `saturn/src/video/cgl.c`, rename `cgl_decode` to `cgl_lzss`, change its parameter names from `rec`/`rec_len` to `src`/`src_len`, drop the `rec_len < CGL_PAL_BYTES + 4` guard for `src_len < 4`, read the size from `src[0..3]` instead of `src[CGL_PAL_BYTES..+3]`, and start the byte cursor at `i = 4` instead of `i = CGL_PAL_BYTES + 4`. Then add the wrapper:

```c
/*----------------------
 | cgl_decode
 | Description: See cgl.h. A CGL record is its own CLUT followed by an LZSS
 |   stream, so this is cgl_lzss with the palette stepped over.
 | Author: suinevere
 | Dependencies: cgl_lzss
 | Globals: N/A
 | Params: rec, rec_len, dst, dst_cap -- see cgl.h
 | Returns: bytes written, or 0 on refusal
 ----------------------*/
unsigned long cgl_decode(const unsigned char *rec, unsigned long rec_len,
                         unsigned char *dst, unsigned long dst_cap) {
    if (rec == 0) return 0;
    if (rec_len < (unsigned long) CGL_PAL_BYTES + 4) return 0;
    return cgl_lzss(rec + CGL_PAL_BYTES, rec_len - (unsigned long) CGL_PAL_BYTES,
                    dst, dst_cap);
}
```

Also update `cgl.c`'s file header `Description:` to say it now carries the codec and the CGL record layout on top of it, and its `Globals:` line is unchanged (`g_ring`).

- [ ] **Step 4: Prove the refactor changed nothing**

Run the existing CGL test:
```bash
gcc -O2 -I saturn/src -I saturn/tests -o /tmp/tcgl saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/tcgl
```
Expected: `all pass`. This is the regression gate for the extraction — all 75 frames still decode identically.

- [ ] **Step 5: Write `oitem.h`**

```c
/*----------------------
 | oitem.h
 | Description: Decoder for the Zork I (Saturn, Japan) item-picture container.
 |   OITEM.CZ is a flat chain of 4-byte-aligned Okumura-LZSS records: nineteen
 |   64x80 8bpp pictures followed by nineteen 256-entry RGB555 CLUTs, one per
 |   picture. Pure logic: no SRL, no disc, no VDP2, so the host tests link it
 |   with plain gcc and the port can be proved before it ever runs on hardware.
 |   The record offsets are generated rather than scanned -- see
 |   scene/oitem_records.inc -- because scanning means decompressing every
 |   earlier record to reach record n.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef OITEM_H
#define OITEM_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OITEM_PIC_N
/*----------------------
 | OITEM_PIC_N / OITEM_PIC_BYTES / OITEM_PAL_BYTES / OITEM_WIDTH / OITEM_HEIGHT
 | Description: The picture count and geometry from oitem_records.inc, copied
 |   here so a caller can size a buffer without pulling the record table into
 |   its translation unit. Must equal the .inc's own values; the block is
 |   guarded because oitem.c includes the .inc ahead of this header, which is
 |   the one place both ever meet.
 | Author: suinevere
 ----------------------*/
#define OITEM_PIC_N     19
#define OITEM_PIC_BYTES 5120
#define OITEM_PAL_BYTES 512
#endif /* OITEM_PIC_N */

#define OITEM_WIDTH 64
#define OITEM_HEIGHT 80

/*----------------------
 | oitem_count
 | Description: How many item pictures the container holds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: OITEM_PIC_N
 ----------------------*/
int oitem_count(void);

/*----------------------
 | oitem_decode
 | Description: Expands one picture and its own palette out of the archive.
 |   Refuses rather than half-filling: a null argument, an index outside
 |   0..OITEM_PIC_N-1, a record that runs past the archive's end, or either
 |   stream expanding to the wrong length all return 0 with pixels and clut
 |   untouched -- which every caller reads as "hold the picture already
 |   showing".
 | Author: suinevere
 | Dependencies: cgl.h, scene/oitem_records.inc
 | Globals: OITEM_RECORDS
 | Params: archive -- the whole OITEM.CZ; archive_len -- its byte length;
 |   picture -- 0..OITEM_PIC_N-1; pixels -- receives OITEM_PIC_BYTES;
 |   clut -- receives 256 Saturn CRAM words
 | Returns: 1 on success, 0 on refusal
 ----------------------*/
int oitem_decode(const unsigned char *archive, unsigned long archive_len,
                 int picture, unsigned char *pixels, unsigned short *clut);

#ifdef __cplusplus
}
#endif
#endif /* OITEM_H */
```

- [ ] **Step 6: Write `oitem.c`**

```c
/*----------------------
 | oitem.c
 | Description: See oitem.h. The container layout; the codec itself is
 |   cgl_lzss and the palette conversion is cgl_palette, both reused unchanged
 |   because the two formats share them exactly.
 | Author: suinevere
 | Dependencies: oitem.h, cgl.h, scene/oitem_records.inc
 | Globals: OITEM_RECORDS
 ----------------------*/
#include "scene/oitem_records.inc"
#include "video/oitem.h"
#include "video/cgl.h"

/*----------------------
 | oitem_count
 | Description: See oitem.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: OITEM_PIC_N
 ----------------------*/
int oitem_count(void) { return OITEM_PIC_N; }

/*----------------------
 | rec_ok
 | Description: Whether a record index names a record wholly inside the
 |   archive. Checked before either decode rather than trusting the generated
 |   table against whatever bytes were actually read off the disc.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: OITEM_RECORDS
 | Params: idx -- record index; archive_len -- bytes available
 | Returns: 1 when the record fits
 ----------------------*/
static int rec_ok(int idx, unsigned long archive_len) {
    if (idx < 0 || idx >= OITEM_RECORD_N) return 0;
    return OITEM_RECORDS[idx].offset + OITEM_RECORDS[idx].length <= archive_len;
}

/*----------------------
 | oitem_decode
 | Description: See oitem.h.
 | Author: suinevere
 | Dependencies: cgl.h
 | Globals: OITEM_RECORDS
 | Params: archive, archive_len, picture, pixels, clut -- see oitem.h
 | Returns: 1 on success, 0 on refusal
 ----------------------*/
int oitem_decode(const unsigned char *archive, unsigned long archive_len,
                 int picture, unsigned char *pixels, unsigned short *clut) {
    static unsigned char pal[OITEM_PAL_BYTES];
    const OitemRecord *pr;
    const OitemRecord *cr;

    if (archive == 0 || pixels == 0 || clut == 0) return 0;
    if (picture < 0 || picture >= OITEM_PIC_N) return 0;
    if (!rec_ok(picture, archive_len)) return 0;
    if (!rec_ok(picture + OITEM_PIC_N, archive_len)) return 0;

    pr = &OITEM_RECORDS[picture];
    cr = &OITEM_RECORDS[picture + OITEM_PIC_N];

    if (cgl_lzss(archive + cr->offset, cr->length, pal,
                 (unsigned long) OITEM_PAL_BYTES)
        != (unsigned long) OITEM_PAL_BYTES) return 0;

    if (cgl_lzss(archive + pr->offset, pr->length, pixels,
                 (unsigned long) OITEM_PIC_BYTES)
        != (unsigned long) OITEM_PIC_BYTES) return 0;

    cgl_palette(pal, clut);
    return 1;
}
```

The palette is decoded **before** the pixels deliberately: a failed palette must not leave a half-written picture in the caller's buffer, and the picture is the larger of the two.

- [ ] **Step 7: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -I saturn/tests -o /tmp/toitem saturn/tests/test_oitem.c saturn/src/video/oitem.c saturn/src/video/cgl.c && /tmp/toitem
```
Expected: `all pass`.

- [ ] **Step 8: Type-check for the target, both configurations**

Run:
```bash
cd saturn && sh syntax-check.sh src/video/oitem.c && NETBIN=1 sh syntax-check.sh src/video/oitem.c
```
Expected: exit 0 for both. `oitem.c` will be picked up by the CD build's `find` glob and is harmless in the netbin (nothing calls it there, and the linker drops it), but it must at least parse.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/oitem.c saturn/src/video/oitem.h saturn/src/video/cgl.c saturn/src/video/cgl.h saturn/tests/test_oitem.c
git commit -m "Decode the disc's nineteen item pictures on the host, by lifting the LZSS out of the CGL record layout so the item container can share the codec that was already proved against all seventy-five room frames."
```

---

## Task 5: Bind the nineteen pictures to Zork I's nineteen treasures

**Files:**
- Create: `tools/assets/zork1_items.json`
- Create: `tools/gen_items.py`
- Create: `saturn/src/scene/game_items.inc` (generated)
- Create: `saturn/src/scene/items.h`, `saturn/src/scene/items.c`
- Create: `saturn/tests/test_items.c`
- Create: `tools/tests/test_gen_items.py`

**Interfaces:**
- Consumes: `tools/assets/Z3/ZORK1.Z3`, `tools/zstory.py`
- Produces:
  - `game_items.inc` defining `typedef struct { unsigned short obj; unsigned char picture; } ItemPicture;`, `typedef struct { unsigned short release; const char *serial; const ItemPicture *items; unsigned short count; } GameItemMap;`, `#define ITEM_GAME_N 1`, and `static const GameItemMap GAME_ITEM_MAP[ITEM_GAME_N]`
  - `int items_available(unsigned int release, const char *serial);` — 1 when the story has a table
  - `int items_picture_of(unsigned int release, const char *serial, unsigned int obj);` — the 0-based picture index, or -1

- [ ] **Step 1: Write the binding**

Create `tools/assets/zork1_items.json`. Keys are picture indices as strings, values are the object's exact short name in `ZORK1.Z3`:

```json
{
  "0": "sceptre",
  "1": "gold coffin",
  "2": "sapphire-encrusted bracelet",
  "3": "golden clockwork canary",
  "4": "platinum bar",
  "5": "leather bag of coins",
  "6": "huge diamond",
  "7": "jewel-encrusted egg",
  "8": "large emerald",
  "9": "jade figurine",
  "10": "pot of gold",
  "11": "painting",
  "12": "beautiful jeweled scarab",
  "13": "beautiful brass bauble",
  "14": "chalice",
  "15": "crystal skull",
  "16": "torch",
  "17": "crystal trident",
  "18": "trunk of jewels"
}
```

Names, not object numbers, because a name is checkable by a human reading the diff. `broken jewel-encrusted egg` and `broken clockwork canary` are deliberately absent — a broken egg is not a picture of an unbroken one, and they take the blank plate.

- [ ] **Step 2: Write the generator's tests first**

Create `tools/tests/test_gen_items.py`:

```python
#!/usr/bin/env python3
"""gen_items.py's refusals and its byte-identical regeneration.

The refusals are the point of the module. A zero or a silently dropped row
would show up only as a pane that never changes -- indistinguishable from an
item that legitimately has no picture -- so the generator has to raise instead.
"""
import json
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_items  # noqa: E402


def test_shipped_binding_resolves():
    rows = gen_items.resolve(gen_items.load_binding(), gen_items.story())
    assert len(rows) == 19
    assert sorted(p for _, p in rows) == list(range(19))
    assert len(set(o for o, _ in rows)) == 19


def test_every_bound_object_is_a_treasure():
    rows = gen_items.resolve(gen_items.load_binding(), gen_items.story())
    names = {o.num: o.name for o in gen_items.story().objects}
    for obj, _ in rows:
        assert obj in names


def test_refuses_unknown_name():
    with pytest.raises(SystemExit):
        gen_items.resolve({0: "no such object anywhere"}, gen_items.story())


def test_refuses_ambiguous_name():
    with pytest.raises(SystemExit):
        gen_items.resolve({0: "Coal Mine"}, gen_items.story())


def test_refuses_duplicate_object():
    with pytest.raises(SystemExit):
        gen_items.resolve({0: "chalice", 1: "chalice"}, gen_items.story())


def test_refuses_index_out_of_range():
    with pytest.raises(SystemExit):
        gen_items.resolve({19: "chalice"}, gen_items.story())
    with pytest.raises(SystemExit):
        gen_items.resolve({-1: "chalice"}, gen_items.story())


def test_refuses_wrong_story():
    with pytest.raises(SystemExit):
        gen_items.check_identity(87, "999999")


def test_regenerates_byte_identically():
    out = ROOT / "saturn" / "src" / "scene" / "game_items.inc"
    before = out.read_bytes()
    gen_items.main([])
    assert out.read_bytes() == before
```

`"Coal Mine"` is the ambiguity case because `ZORK1.Z3` genuinely holds four objects with that name (16–19) — a real collision rather than an invented one.

A duplicate *picture index* cannot be constructed through a JSON object (keys are unique), which is why there is no test for it; the generator still checks it, because `resolve` is also called from the test above with a hand-built dict.

- [ ] **Step 3: Run to verify it fails**

Run: `python -m pytest tools/tests/test_gen_items.py -v`
Expected: collection error — `ModuleNotFoundError: No module named 'gen_items'`.

- [ ] **Step 4: Write the generator**

Create `tools/gen_items.py`:

```python
#!/usr/bin/env python3
"""/*----------------------
 | gen_items.py
 | Description: GENERATES saturn/src/scene/game_items.inc -- which of the
 |     Japanese Zork I disc's nineteen item pictures each of Zork I's objects
 |     gets, keyed by release and serial.
 |
 |     The binding is authored rather than measured, because there is nothing
 |     to measure: the Saturn build is a native reimplementation, not a
 |     Z-machine interpreter, so its GAME.DAT carries no object numbers to
 |     recover. What IS measured is the set -- 1dungeon.zil gives 22 objects
 |     carrying a TVALUE, and dropping SWORD (TVALUE 0) and the two damaged
 |     variants leaves exactly nineteen treasures against exactly nineteen
 |     pictures.
 |
 |     tools/assets/zork1_items.json carries object NAMES rather than numbers,
 |     because a name is checkable by a human reading the diff and a number is
 |     not. This module resolves them and refuses rather than writing a zero
 |     on: a name matching zero objects or more than one, a duplicate picture
 |     index or object, an index outside 0..18, or a story whose release and
 |     serial are not 88 / 840726. A zero would show up only as a pane that
 |     silently fails to change.
 | Author: suinevere
 | Dependencies: json, pathlib, sys, zstory
 | Globals: ROOT, Z3, BINDING, OUT, RELEASE, SERIAL, PIC_N
 ----------------------*/"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import zstory  # noqa: E402

Z3 = ROOT / "tools" / "assets" / "Z3" / "ZORK1.Z3"
BINDING = ROOT / "tools" / "assets" / "zork1_items.json"
OUT = ROOT / "saturn" / "src" / "scene" / "game_items.inc"

RELEASE = 88
SERIAL = "840726"
PIC_N = 19


def story():
    """/*----------------------
     | story
     | Description: ZORK1.Z3 decoded, after checking its identity.
     | Author: suinevere
     | Dependencies: zstory
     | Globals: Z3, RELEASE, SERIAL
     | Params: N/A
     | Returns: a zstory.Story
     ----------------------*/"""
    raw = Z3.read_bytes()
    check_identity((raw[2] << 8) | raw[3], raw[0x12:0x18].decode("ascii", "replace"))
    return zstory.Story(Z3)


def check_identity(release, serial):
    """/*----------------------
     | check_identity
     | Description: Refuses any story but Zork I release 88 / serial 840726.
     |     The picture set is portraits of THAT story's objects; another
     |     release's object numbers would bind pictures to the wrong things
     |     silently.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RELEASE, SERIAL
     | Params: release -- Z-machine release; serial -- 6-char serial
     | Returns: N/A
     ----------------------*/"""
    if release != RELEASE or serial != SERIAL:
        raise SystemExit(f"expected release {RELEASE} serial {SERIAL}, "
                         f"got release {release} serial {serial}")


def load_binding():
    """/*----------------------
     | load_binding
     | Description: zork1_items.json as {picture index: object name}.
     | Author: suinevere
     | Dependencies: json
     | Globals: BINDING
     | Params: N/A
     | Returns: dict of int -> str
     ----------------------*/"""
    return {int(k): v for k, v in json.loads(BINDING.read_text()).items()}


def resolve(binding, st):
    """/*----------------------
     | resolve
     | Description: Every (object number, picture index) pair the binding names,
     |     sorted by object number so the emitted table can be searched in
     |     order. Raises SystemExit on any of the five refusals.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: PIC_N
     | Params: binding -- {picture index: object name}; st -- a zstory.Story
     | Returns: list of (obj, picture), sorted by obj
     ----------------------*/"""
    rows, seen_obj, seen_pic = [], set(), set()
    for pic, name in sorted(binding.items()):
        if pic < 0 or pic >= PIC_N:
            raise SystemExit(f"picture index {pic} is outside 0..{PIC_N - 1}")
        if pic in seen_pic:
            raise SystemExit(f"picture index {pic} bound twice")
        seen_pic.add(pic)
        hits = [o for o in st.objects if (o.name or "").strip() == name]
        if len(hits) != 1:
            raise SystemExit(f"\"{name}\" matches {len(hits)} objects, expected exactly 1")
        obj = hits[0].num
        if obj in seen_obj:
            raise SystemExit(f"object {obj} (\"{name}\") bound twice")
        seen_obj.add(obj)
        rows.append((obj, pic))
    return sorted(rows)


def emit(rows):
    """/*----------------------
     | emit
     | Description: The generated .inc text for one game's rows.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RELEASE, SERIAL
     | Params: rows -- (obj, picture) pairs, sorted by obj
     | Returns: the file text
     ----------------------*/"""
    lines = [
        "/*----------------------",
        " | game_items.inc",
        " | Description: GENERATED FILE -- do not edit by hand; produced by",
        " |   tools/gen_items.py. Which of OITEM.CZ's nineteen item pictures",
        " |   each object gets, keyed by release and serial. picture is a",
        " |   0-based index into the container; an object with no row has no",
        " |   picture and takes the blank plate. Zork I is the only game with a",
        " |   table: these are portraits of its own treasures, not a pool other",
        " |   stories could draw from.",
        " | Author: suinevere",
        " ----------------------*/",
        "typedef struct {",
        "    unsigned short obj;",
        "    unsigned char  picture;",
        "} ItemPicture;",
        "typedef struct {",
        "    unsigned short     release;",
        "    const char        *serial;",
        "    const ItemPicture *items;",
        "    unsigned short     count;",
        "} GameItemMap;",
        "#define ITEM_GAME_N 1",
        f"#define ITEM_PIC_N {PIC_N}",
        f"static const ItemPicture ITEMS_ZORK1[{len(rows)}] = {{",
    ]
    for obj, pic in rows:
        lines.append(f"    {{ {obj}, {pic} }},")
    lines += [
        "};",
        "static const GameItemMap GAME_ITEM_MAP[ITEM_GAME_N] = {",
        f"    {{ {RELEASE}, \"{SERIAL}\", ITEMS_ZORK1, {len(rows)} }},",
        "};",
    ]
    return "\n".join(lines) + "\n"


def main(argv):
    """/*----------------------
     | main
     | Description: Resolves the binding and writes game_items.inc.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: OUT
     | Params: argv -- unused
     | Returns: 0
     ----------------------*/"""
    rows = resolve(load_binding(), story())
    OUT.write_text(emit(rows), newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {len(rows)} objects bound")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
```

Check `tools/zstory.py`'s `Story` before writing: this plan assumes `st.objects` is a list of objects with `.num` and `.name`, which is what `tools/gen_room_inventory.py` relies on. If the attribute is named differently, follow that file's usage rather than this one.

- [ ] **Step 5: Run the generator**

Run: `python tools/gen_items.py`
Expected: `saturn/src/scene/game_items.inc: 19 objects bound`.

- [ ] **Step 6: Eyeball the table against the spec**

Run: `cat saturn/src/scene/game_items.inc`
Expected: 19 rows. Spot-check three against the spec's table — object 87 → picture 7 (jewelled egg), object 209 → picture 0 (sceptre), object 231 → picture 15 (crystal skull).

- [ ] **Step 7: Run the generator tests**

Run: `python -m pytest tools/tests/test_gen_items.py -v`
Expected: 8 passed.

- [ ] **Step 8: Write the runtime lookup's failing test**

Create `saturn/tests/test_items.c`:

```c
/*----------------------
 | test_items.c
 | Description: The runtime lookup over game_items.inc. Run from the
 |   repository root:
 |   gcc -O2 -I saturn/src -o /tmp/titems \
 |       saturn/tests/test_items.c saturn/src/scene/items.c && /tmp/titems
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "scene/items.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    check(items_available(88, "840726") == 1, "Zork I has a table");
    check(items_available(88, "999999") == 0, "a wrong serial has none");
    check(items_available(42, "840726") == 0, "a wrong release has none");

    check(items_picture_of(88, "840726", 87) == 7, "the jewelled egg is picture 7");
    check(items_picture_of(88, "840726", 209) == 0, "the sceptre is picture 0");
    check(items_picture_of(88, "840726", 231) == 15, "the crystal skull is picture 15");
    check(items_picture_of(88, "840726", 101) == 18, "the trunk of jewels is picture 18");

    check(items_picture_of(88, "840726", 86) == -1, "the broken egg is unbound");
    check(items_picture_of(88, "840726", 110) == -1, "the sword is unbound");
    check(items_picture_of(88, "840726", 0) == -1, "object 0 is unbound");
    check(items_picture_of(88, "840726", 65535) == -1, "an absurd object is unbound");
    check(items_picture_of(88, "999999", 87) == -1, "a wrong serial binds nothing");

    printf(fails ? "%d FAILURES\n" : "all pass\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 9: Run to verify it fails**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/titems saturn/tests/test_items.c saturn/src/scene/items.c && /tmp/titems
```
Expected: FAIL — `saturn/src/scene/items.c: No such file or directory`.

- [ ] **Step 10: Write `items.h`**

```c
/*----------------------
 | items.h
 | Description: Runtime lookup for the per-object item-picture table generated
 |   by tools/gen_items.py. Zork I is the only game with a table: OITEM.CZ's
 |   nineteen pictures are portraits of its own treasures, not a pool other
 |   stories could be assigned from. A game with no table has no pane at all,
 |   which is what keeps thirty games from carrying eight columns of dead
 |   black.
 |     game_items.inc itself is included only by items.c, the way
 |   game_presentation.inc is included only by presentation.c.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef ITEMS_H
#define ITEMS_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | items_available
 | Description: Whether this story has an authored item-picture table. The one
 |   call that decides whether the inventory overlay takes its tall geometry
 |   with a picture pane or its plain one.
 | Author: suinevere
 | Dependencies: game_items.inc
 | Globals: GAME_ITEM_MAP
 | Params: release -- Z-machine release; serial -- 6-char serial, not
 |   guaranteed NUL-terminated
 | Returns: 1 when the story has a table, 0 otherwise
 ----------------------*/
int items_available(unsigned int release, const char *serial);

/*----------------------
 | items_picture_of
 | Description: The picture one object gets. An unbound object -- which is most
 |   of them, and deliberately includes the broken egg and the broken canary --
 |   returns -1, which the pane reads as "blank plate" rather than as an error.
 | Author: suinevere
 | Dependencies: game_items.inc
 | Globals: GAME_ITEM_MAP
 | Params: release, serial -- the story identity; obj -- the object number
 | Returns: the 0-based picture index, or -1
 ----------------------*/
int items_picture_of(unsigned int release, const char *serial, unsigned int obj);

#ifdef __cplusplus
}
#endif
#endif /* ITEMS_H */
```

- [ ] **Step 11: Write `items.c`**

```c
/*----------------------
 | items.c
 | Description: See items.h.
 | Author: suinevere
 | Dependencies: items.h, game_items.inc
 | Globals: GAME_ITEM_MAP
 ----------------------*/
#include "scene/game_items.inc"
#include "scene/items.h"

/*----------------------
 | game_index
 | Description: The GAME_ITEM_MAP row for one story, by release and 6-char
 |   serial. The serial is compared over exactly six characters rather than as
 |   a string, because the caller's copy comes out of a story header and is not
 |   guaranteed NUL-terminated.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GAME_ITEM_MAP
 | Params: release -- Z-machine release; serial -- 6-char serial
 | Returns: the row index, or -1
 ----------------------*/
static int game_index(unsigned int release, const char *serial) {
    int i, j;
    if (serial == 0) return -1;
    for (i = 0; i < ITEM_GAME_N; i++) {
        if (GAME_ITEM_MAP[i].release != release) continue;
        for (j = 0; j < 6; j++)
            if (GAME_ITEM_MAP[i].serial[j] != serial[j]) break;
        if (j == 6) return i;
    }
    return -1;
}

/*----------------------
 | items_available
 | Description: See items.h.
 | Author: suinevere
 | Dependencies: game_index
 | Globals: N/A
 | Params: release, serial -- the story identity
 | Returns: 1 when the story has a table
 ----------------------*/
int items_available(unsigned int release, const char *serial) {
    return game_index(release, serial) >= 0 ? 1 : 0;
}

/*----------------------
 | items_picture_of
 | Description: See items.h.
 | Author: suinevere
 | Dependencies: game_index
 | Globals: GAME_ITEM_MAP
 | Params: release, serial, obj -- see items.h
 | Returns: the 0-based picture index, or -1
 ----------------------*/
int items_picture_of(unsigned int release, const char *serial, unsigned int obj) {
    int g = game_index(release, serial);
    int i;
    if (g < 0) return -1;
    for (i = 0; i < (int) GAME_ITEM_MAP[g].count; i++)
        if (GAME_ITEM_MAP[g].items[i].obj == obj)
            return (int) GAME_ITEM_MAP[g].items[i].picture;
    return -1;
}
```

- [ ] **Step 12: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/titems saturn/tests/test_items.c saturn/src/scene/items.c && /tmp/titems
```
Expected: `all pass`.

- [ ] **Step 13: Type-check for the target, both configurations**

Run:
```bash
cd saturn && sh syntax-check.sh src/scene/items.c && NETBIN=1 sh syntax-check.sh src/scene/items.c
```
Expected: exit 0 for both.

- [ ] **Step 14: Commit**

```bash
git add tools/assets/zork1_items.json tools/gen_items.py tools/tests/test_gen_items.py saturn/src/scene/game_items.inc saturn/src/scene/items.c saturn/src/scene/items.h saturn/tests/test_items.c
git commit -m "Bind the disc's nineteen item pictures to Zork I's nineteen treasures through a name-keyed table the generator resolves against the story and refuses to guess at, since the Saturn build is a reimplementation and has no object numbers to recover."
```

---

## Task 6: The runtime — read, decode, and put a picture on NBG1

**Files:**
- Create: `saturn/src/video/item_art.h`, `saturn/src/video/item_art.cxx`
- Modify: `saturn/tests/test_lwram_budget.py`

**Interfaces:**
- Consumes: `oitem_decode`, `oitem_count` (Task 4); `items_available`, `items_picture_of` (Task 5); `cd_enter_root`, `cd_restore_z3` from `video/title.h`
- Produces:
  - `void item_art_set_game(unsigned int release, const char *serial);`
  - `int item_art_available(void);` — 1 when the running story has a table
  - `int item_art_open(void);` — makes the archive resident; 1 on success
  - `void item_art_close(void);` — frees the archive and blanks the pane
  - `int item_art_show(unsigned int obj);` — 1 when a picture is on the pane, 0 when the pane was blanked or held
  - `void item_art_hide(void);` — blanks the pane, keeps the archive

- [ ] **Step 1: Add the LWRAM budget term first**

In `saturn/tests/test_lwram_budget.py`, add beside the existing reserves:

```python
# What the inventory overlay's item pane wants while it is open: the whole
# OITEM.CZ container plus one decoded 64x80 picture and its palette. Held only
# between item_art_open and item_art_close, but that window sits inside the
# in-game pairing -- trie + area archive + scratch -- so it has to fit on top
# of all three.
ITEM_ART_RESERVE = 40840 + 5120 + 512
```

and a test:

```python
def test_item_pane_fits_beside_the_in_game_claimants():
    """The pane opens mid-game, on top of the trie, the largest area archive
    and the save scratch. If it does not fit, item_art_open refuses and the
    pane is silently blank for the rest of the session -- which looks exactly
    like an unbound item."""
    biggest = max(p.stat().st_size for p in BG_DIR.glob("*.CGL"))
    total = (TRIE_RESERVE + SCRATCH_RESERVE + biggest
             + 320 * 240 + ITEM_ART_RESERVE)
    assert total <= LWRAM_TOTAL, (
        f"in-game peak with the item pane open is {total} bytes, "
        f"over LWRAM's {LWRAM_TOTAL}"
    )
```

Read the file's existing helpers before writing this — `BG_DIR`, `TRIE_RESERVE`, `SCRATCH_RESERVE` and `LWRAM_TOTAL` are already defined there, and the existing tests show the house style for how the decode target (`320 * 240`) is expressed. Match whatever is already there rather than the literal above if they differ.

- [ ] **Step 2: Run it**

Run: `python -m pytest saturn/tests/test_lwram_budget.py -v`
Expected: all pass, including the new one. **If the new test fails, stop and report** — that is a real budget problem and the answer is not to raise the ceiling. The likely fix would be to free the area archive while the overlay is open, which is a design change and needs the owner.

- [ ] **Step 3: Commit the budget**

```bash
git add saturn/tests/test_lwram_budget.py
git commit -m "Hold the Low Work RAM budget against the item pane's 46 KB, which opens on top of the trie, the resident area archive and the save scratch."
```

- [ ] **Step 4: Write `item_art.h`**

```c
/*----------------------
 | item_art.h
 | Description: The item pictures' hardware half: reading OITEM.CZ off the
 |   disc, decompressing one 64x80 picture and putting it on NBG1 in the
 |   inventory overlay's pane. The decoding is in oitem.c and the binding in
 |   scene/items.h; this is only the policy and the SRL calls, the way
 |   room_art.h is for the room backgrounds.
 |
 |   The archive is 40,840 bytes and is resident only between item_art_open
 |   and item_art_close -- the window the overlay is up -- because that window
 |   sits inside the in-game pairing of typeahead trie, resident area archive
 |   and save scratch. saturn/tests/test_lwram_budget.py is where the
 |   arithmetic is held.
 |
 |   Zork I only. items_available is what gates it, and a story without a
 |   table never opens the archive at all.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef ITEM_ART_H
#define ITEM_ART_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | ITEM_ART_X / ITEM_ART_Y
 | Description: Where the picture's top-left pixel sits on screen: the pane's
 |   own origin, column 27 and row 18 of the 40x30 text grid. Written into the
 |   NBG1 container at this offset with the layer positioned at (0,0), so there
 |   is no scroll arithmetic and the rest of the container stays index 0 --
 |   which VDP2 reads as transparent.
 | Author: suinevere
 ----------------------*/
#define ITEM_ART_X 216
#define ITEM_ART_Y 144

/*----------------------
 | item_art_set_game
 | Description: Tells the module which story is running, once, when it is
 |   selected. Held rather than passed per call because the overlay renderer
 |   runs where the story identity is not in scope. Passing a story with no
 |   authored items is how the pane is turned off again.
 | Author: suinevere
 | Dependencies: scene/items.h
 | Globals: g_release, g_serial, g_have_game
 | Params: release -- Z-machine release; serial -- 6-char serial
 | Returns: N/A
 ----------------------*/
void item_art_set_game(unsigned int release, const char *serial);

/*----------------------
 | item_art_available
 | Description: Whether the story set by item_art_set_game has item pictures.
 |   The one call that decides whether the inventory overlay takes its tall
 |   geometry with a pane or its plain one.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_have_game
 | Params: N/A
 | Returns: 1 when the story has a table, 0 otherwise
 ----------------------*/
int item_art_available(void);

/*----------------------
 | item_art_open / item_art_close
 | Description: Make the archive resident and free it again. open is called
 |   when the overlay is raised and close when it is lowered; close also blanks
 |   the pane, since nothing should be left on a layer whose owner has gone.
 |   Both are idempotent, so a renderer that cannot tell whether it has already
 |   opened may call either every frame.
 |
 |   open restores the CD to the story directory before returning whenever it
 |   stepped out of it, which is the obligation every post-selection detour
 |   owes.
 | Author: suinevere
 | Dependencies: SRL, title.h
 | Globals: g_archive, g_archive_len
 | Params: N/A
 | Returns: open returns 1 when the archive is resident, 0 on any refusal
 ----------------------*/
int  item_art_open(void);
void item_art_close(void);

/*----------------------
 | item_art_show
 | Description: Puts one object's picture on the pane. An object with no bound
 |   picture blanks the pane and returns 0 -- the empty black plate -- which is
 |   a success from the player's point of view and is deliberately not
 |   distinguished from it in the return value, because no caller has anything
 |   different to do.
 *
 |   Skips the decode and the upload when the picture is the one already
 |   showing, so walking the cursor up and down a list of the same item costs
 |   nothing.
 |
 |   Every failure -- no game set, an archive that will not open, a stream that
 |   will not decode -- holds the picture already showing and says nothing on
 |   screen. Art is decoration; a failed load must never blank the screen or
 |   stop the game.
 | Author: suinevere
 | Dependencies: SRL, oitem.h, scene/items.h
 | Globals: g_archive, g_archive_len, g_cur_picture
 | Params: obj -- the carried object's number
 | Returns: 1 when a picture is on the pane, 0 when it was blanked or held
 ----------------------*/
int item_art_show(unsigned int obj);

/*----------------------
 | item_art_hide
 | Description: Blanks the pane without freeing the archive, for every frame
 |   the overlay is not up. One thing is on this layer at a time, the contract
 |   dash_map already holds for NBG2.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cur_picture
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void item_art_hide(void);

#ifdef __cplusplus
}
#endif
#endif /* ITEM_ART_H */
```

- [ ] **Step 5: Write `item_art.cxx`**

Model it on `room_art.cxx` — read that file first, particularly `load_area` (`:126-178`) and `art_dir_restore` (`:89-92`), and copy their structure exactly rather than inventing a second way to read a file off `/BG`.

Skeleton, with the two layer paths marked. **Take the path Task 1 decided.**

```cpp
/*----------------------
 | item_art.cxx
 | Description: See item_art.h.
 | Author: suinevere
 | Dependencies: SRL, oitem.h, item_art.h, title.h, scene/items.h
 | Globals: g_release, g_serial, g_have_game, g_archive, g_archive_len,
 |   g_pixels, g_clut, g_cur_picture, g_layer_up
 ----------------------*/
#include <srl.hpp>
#include "video/oitem.h"
#include "video/item_art.h"
#include "video/title.h"
#include "scene/items.h"

#define ITEM_DIR "BG"
#define ITEM_FILE "OITEM.CZ"

static unsigned int   g_release = 0;
static char           g_serial[7] = { 0 };
static bool           g_have_game = false;
static unsigned char *g_archive = nullptr;
static unsigned long  g_archive_len = 0;
static unsigned char *g_pixels = nullptr;
static unsigned short g_clut[256];
static int            g_cur_picture = -1;
static bool           g_layer_up = false;
```

`item_art_set_game` copies six serial characters and sets `g_have_game = items_available(release, serial) != 0`, exactly as `room_art_set_game` does at `room_art.cxx:189`.

`item_art_open` mirrors `load_area`: `cd_enter_root()`, `SRL::Cd::ChangeDir(ITEM_DIR)`, open `SRL::Cd::File`, check `Exists()`, check `SRL::Memory::LowWorkRam::GetFreeSpace()` against `bytes + OITEM_PIC_BYTES + 4096`, `Malloc`, check the pointer is long-aligned (`((unsigned int) p & 3) == 0`) because `LoadBytes` requires it, `LoadBytes`, and restore the directory on **every** exit path including the successful one. Return early with 1 if `g_archive != nullptr` already.

**Layer path A — bitmap (take this if Task 1 confirmed NBG1 as a bitmap):**

```cpp
static void layer_ensure(void) {
    if (g_layer_up) return;
    /* One 512x256 8bpp container -- 131072 bytes, one VRAM bank. NBG0's
       wallpaper holds A0 and the dashboard cells hold part of B0, so the
       allocator lands this in A1. */
    static uint8_t blank[64 * 80];
    for (int i = 0; i < 64 * 80; i++) blank[i] = 0;
    /* Load once with a blank picture and a placeholder palette to claim the
       bank and set the bitmap registers; every later picture is a direct write
       into the container plus a palette reload, which is far cheaper than
       LoadBitmap's allocate-and-blank path. */
    ...
    slPriorityNbg1(3);
    SRL::VDP2::NBG1::ScrollEnable();
    g_layer_up = true;
}
```

Write the picture into the container at `(ITEM_ART_Y * 512) + ITEM_ART_X`, one 64-byte row at a time with a stride of 512:

```cpp
static void put_pixels(const unsigned char *px) {
    volatile uint8_t *base = (volatile uint8_t *) SRL::VDP2::NBG1::CellAddress;
    base += (ITEM_ART_Y * 512) + ITEM_ART_X;
    for (int y = 0; y < OITEM_HEIGHT; y++) {
        for (int x = 0; x < OITEM_WIDTH; x++) base[x] = px ? px[y * OITEM_WIDTH + x] : 0;
        base += 512;
    }
}
```

`px == nullptr` blanks the region, which is what `item_art_hide` and an unbound object both want.

The palette goes to `SRL::VDP2::NBG1::TilePalette.Load(...)`. It takes `SRL::Types::HighColor`, so convert from `g_clut` the way `title_bg_show_raw` does at `title.cxx:465-467`.

**Layer path B — tilemap (take this if Task 1 found the cycle pattern refuses a second bitmap):**

Allocate 5,120 bytes of cell data and a map with `SRL::VDP2::VRAM::Allocate`, set them with `SRL::VDP2::NBG1::SetCellAddress` / `SetMapAddress`, and rearrange the linear 64x80 picture into 80 8x8 tiles on the way in:

```cpp
static void put_pixels(const unsigned char *px) {
    volatile uint8_t *cells = (volatile uint8_t *) g_cells;
    for (int ty = 0; ty < 10; ty++)
        for (int tx = 0; tx < 8; tx++) {
            volatile uint8_t *t = cells + ((ty * 8 + tx) * 64);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    t[y * 8 + x] = px ? px[(ty * 8 + y) * OITEM_WIDTH + (tx * 8 + x)] : 0;
        }
}
```

The map is written once at bring-up: the 8x10 block of pattern names at the pane's cell coordinates (columns 27–34, rows 18–27), everything else the blank tile. `dash_view.cxx:196-205` is the working example of writing an NBG2 map by hand; follow its pattern-name format and its `g_char_base` arithmetic.

Everything else — `item_art_show`'s lookup, the `g_cur_picture` short-circuit, the refusals — is identical between the two paths.

`item_art_show`:

```cpp
int item_art_show(unsigned int obj) {
    int pic;
    if (!g_have_game) return 0;
    pic = items_picture_of(g_release, g_serial, obj);
    if (pic == g_cur_picture) return pic >= 0 ? 1 : 0;
    if (pic < 0) { item_art_hide(); return 0; }
    if (g_archive == nullptr && !item_art_open()) return 0;
    if (g_pixels == nullptr) {
        g_pixels = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(OITEM_PIC_BYTES);
        if (g_pixels == nullptr) return 0;
    }
    if (!oitem_decode(g_archive, g_archive_len, pic, g_pixels, g_clut)) return 0;
    layer_ensure();
    put_palette(g_clut);
    put_pixels(g_pixels);
    g_cur_picture = pic;
    return 1;
}
```

`item_art_hide` sets `g_cur_picture = -1` and calls `put_pixels(nullptr)` if the layer is up. `item_art_close` calls `item_art_hide`, frees `g_archive` and `g_pixels`, and nulls them.

Note the deliberate asymmetry: `g_cur_picture` is compared **before** the archive is opened, so an already-showing picture costs one table lookup and no disc access. And `item_art_hide` is called on the unbound path rather than returning early, because the previous item's picture must not stay up under a new item's name.

- [ ] **Step 6: Type-check both configurations**

Run:
```bash
cd saturn && sh syntax-check.sh src/video/item_art.cxx && NETBIN=1 sh syntax-check.sh src/video/item_art.cxx
```
Expected: exit 0 for both. `item_art.cxx` is not in the netbin source list so it will never link there, but a file that does not parse in one configuration is exactly the bug `syntax-check.sh` exists to catch.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/video/item_art.cxx saturn/src/video/item_art.h
git commit -m "Read the item container off the disc only while the inventory overlay is open, and put one decoded picture on NBG1 at the pane's own screen offset so the rest of the layer stays transparent."
```

---

## Task 7: The overlay's tall geometry and its pane

**Files:**
- Modify: `saturn/src/video/dash_map.h`, `saturn/src/video/dash_map.c`
- Modify: `saturn/tests/test_dash_map.c`
- Modify: `saturn/src/video/command_view.cxx`
- Create: `saturn/tests/test_overlay_layout.c`

**Interfaces:**
- Consumes: `item_art_available` (Task 6)
- Produces: `DASH_OVERLAY_TALL`; and in `command_view.h`, `#define CV_OVERLAY_X 2`, `CV_OVERLAY_W 34`, `CV_OVERLAY_TALL_ROWS 12`, `CV_OVERLAY_LIST_W 23`, `CV_OVERLAY_PANE_X 27`, `CV_OVERLAY_PANE_W 8` — moved out of `command_view.cxx` so the layout test can include them

- [ ] **Step 1: Write the failing layout test**

Create `saturn/tests/test_overlay_layout.c`:

```c
/*----------------------
 | test_overlay_layout.c
 | Description: The inventory overlay's two geometries -- the plain nine-row
 |   box every story gets and the twelve-row box with a picture pane that only
 |   a story with item art gets -- checked for the arithmetic that is easy to
 |   get wrong by one and impossible to see wrong on screen. Run from the
 |   repository root:
 |   gcc -O2 -I saturn/src -o /tmp/tovl saturn/tests/test_overlay_layout.c && /tmp/tovl
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "video/panel_layout.h"
#include "video/item_art.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    int left   = CV_OVERLAY_X;
    int right  = CV_OVERLAY_X + CV_OVERLAY_W - 1;
    int list0  = left + 1;
    int listN  = list0 + CV_OVERLAY_LIST_W - 1;
    int paneN  = CV_OVERLAY_PANE_X + CV_OVERLAY_PANE_W - 1;

    check(CV_OVERLAY_X == 2 && CV_OVERLAY_W == 34, "the box is columns 2..35");
    check(right == 35, "the right border is column 35");

    check(listN < CV_OVERLAY_PANE_X - 1, "a divider column sits between list and pane");
    check(CV_OVERLAY_PANE_X - 1 == listN + 1, "the divider is exactly one column");
    check(paneN == right - 1, "the pane ends against the right border");
    check(CV_OVERLAY_LIST_W + 1 + CV_OVERLAY_PANE_W == CV_OVERLAY_W - 2,
          "list, divider and pane fill the interior exactly");

    check(CV_OVERLAY_PANE_W * 8 == 64, "the pane is exactly the picture's width");
    check((CV_OVERLAY_TALL_ROWS - 2) * 8 == 80, "the interior is exactly the picture's height");

    check(CV_OVERLAY_PANE_X * 8 == ITEM_ART_X, "the pane's column matches the picture's x");

    check(CV_OVERLAY_TALL_ROWS == 12, "the tall box is twelve rows");
    check(CV_OVERLAY_ROWS == 10, "the tall box lists ten items");
    check(CV_OVERLAY_SHORT_ROWS == CV_STRIP_ROWS, "the plain box is the strip's content height");
    check(CV_OVERLAY_RISE == CV_OVERLAY_TALL_ROWS - CV_STRIP_ROWS, "the rise is the height the box gained");

    printf(fails ? "%d FAILURES\n" : "all pass\n", fails);
    return fails ? 1 : 0;
}
```

The `ITEM_ART_Y` check is deliberately absent here: the box's top row depends on `console_height()`, which pulls in SRL, so it is checked on screen in Task 8 rather than pretended at here.

- [ ] **Step 2: Run to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/tovl saturn/tests/test_overlay_layout.c && /tmp/tovl`
Expected: FAIL — `video/panel_layout.h: No such file or directory`.

- [ ] **Step 3: Create `saturn/src/video/panel_layout.h` and move the geometry into it**

`command_view.h` declares `render_command_panel(const CommandPanel &p, ...)` — C++ references — and its five includes resolve only under the real build's per-subdirectory `-I` flags. A plain-C host test cannot include it. So the panel's cell arithmetic gets its own header with no includes and no declarations, which both `command_view.h` and the test can take.

Create `saturn/src/video/panel_layout.h`. Move `CV_TRAVEL_X`, `CV_WORD_X`, `CV_CMD_X` and `CV_STRIP_ROWS` into it verbatim, with their existing header block; cut `CV_OVERLAY_X` / `CV_OVERLAY_W` and their block out of `command_view.cxx:755-763` and `CV_OVERLAY_ROWS` out of `command_view.cxx:817-826`; and add the new ones. Then `command_view.h` includes `panel_layout.h` in place of the four defines it lost, so `console_view.cxx`'s use of `CV_STRIP_ROWS` keeps working untouched.

The file needs the standard `#ifndef PANEL_LAYOUT_H` guard, no `extern "C"` block (it declares nothing), and this content:

```c
/*----------------------
 | CV_OVERLAY_X / CV_OVERLAY_W / CV_OVERLAY_TALL_ROWS / CV_OVERLAY_ROWS
 | CV_OVERLAY_LIST_W / CV_OVERLAY_PANE_X / CV_OVERLAY_PANE_W
 | Description: The inventory overlay's box and the split inside it. 34 columns
 |   starting at column 2, unchanged from when the overlay was list-only. The
 |   interior is a 23-column item list, one divider column, and an 8-column
 |   picture pane -- 64 pixels, exactly one item picture wide.
 |     The box is twelve rows when the story has item art and nine when it does
 |   not, because the ten interior rows the tall one carries are exactly the
 |   picture's 80 pixels and a story with no pictures should not pay for them.
 |   CV_OVERLAY_ROWS is the tall box's list height; the plain box keeps the
 |   five rows it always had, derived from the strip as before.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_X          2
#define CV_OVERLAY_W          34
#define CV_OVERLAY_TALL_ROWS  12
#define CV_OVERLAY_ROWS       (CV_OVERLAY_TALL_ROWS - 2)
#define CV_OVERLAY_LIST_W     23
#define CV_OVERLAY_PANE_X     27
#define CV_OVERLAY_PANE_W     8
#define CV_OVERLAY_SHORT_ROWS CV_STRIP_ROWS
#define CV_OVERLAY_SHORT_LIST (CV_STRIP_ROWS - 2)
```

Delete the old `#define CV_OVERLAY_ROWS (CV_STRIP_ROWS - 2)` at `command_view.cxx:826` and its header block; the replacement lives in the header now.

- [ ] **Step 4: Run the layout test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/tovl saturn/tests/test_overlay_layout.c && /tmp/tovl`
Expected: `all pass`.

- [ ] **Step 5: Add the tall dashboard variant**

In `dash_map.h`, extend the enum — insert **before** `DASH_BOX`, which is what `g_geom`'s size is taken from:

```c
enum { DASH_NONE = 0, DASH_PANEL, DASH_GAMEKB, DASH_OVERLAY, DASH_OVERLAY_TALL,
       DASH_BOX, DASH_VARIANT_MAP, DASH_VARIANT_N };
```

and update that enum's header block: `OVERLAY` is PANEL without its dividers, and `OVERLAY_TALL` is OVERLAY five rows taller, for the overlay's own picture pane.

In `dash_map.c`, add the row to `g_geom` after the `DASH_OVERLAY` row:

```c
    { 14, 0, 39, 0, {  0, 0 }, -1 }
```

and update `g_geom`'s header block, which currently says "Each is the nine-row box the ASCII chrome drew" — that is no longer true of all of them.

At `dash_map.c:350`, add the variant to the claim test:

```c
    return (g_variant == DASH_PANEL || g_variant == DASH_GAMEKB
            || g_variant == DASH_OVERLAY || g_variant == DASH_OVERLAY_TALL) ? 1 : 0;
```

- [ ] **Step 6: Cover the new variant in `test_dash_map.c`**

Beside the existing `dash_build(DASH_OVERLAY, 19)` case at `:105`, add:

```c
    dash_build(DASH_OVERLAY_TALL, 16);
    check(dash_dirty_top() == 16, "the tall overlay's dirty span starts at its base");
    check(dash_dirty_bottom() == 29, "the tall overlay is fourteen rows");
    check(dash_claimed() == 1, "the tall overlay claims the layer");
```

Read the file's existing helpers before writing this — the accessor names above are guesses at what `test_dash_map.c` already uses. Use whatever it actually calls.

- [ ] **Step 7: Run the dash tests**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tdash saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdash
```
Expected: `all pass`.

- [ ] **Step 8: Commit the geometry**

```bash
git add saturn/src/video/dash_map.c saturn/src/video/dash_map.h saturn/src/video/command_view.h saturn/src/video/command_view.cxx saturn/tests/test_dash_map.c saturn/tests/test_overlay_layout.c
git commit -m "Give the inventory overlay a twelve-row shape with a picture pane beside its list, sized so the interior is exactly one item picture tall and the pane exactly one wide."
```

- [ ] **Step 9: Rewrite `cv_draw_overlay` and the panel's layout**

In `command_view.cxx`:

`cv_overlay_row_text` — change `field_w` from `CV_OVERLAY_W - 3` to a parameter, so the tall box's rows are `CV_OVERLAY_LIST_W` wide and the plain box's stay `CV_OVERLAY_W - 3`. The right end of a tall row is the divider `|` rather than the box's own border:

```c
static void cv_overlay_row_text(const RoomModel &m, int idx, int field_w, char *out) {
    char word[8] = {0};
    char full[16] = {0};
    int i, wl;
    if (idx >= 0 && idx < m.ncarried && room_model_object_word(m.carried[idx], word, sizeof word))
        room_model_full_word(m.carried[idx], word, full, sizeof full);
    wl = 0;
    while (wl < (int) sizeof(full) - 1 && full[wl] != '\0') wl++;
    out[0] = '|';
    out[1] = ' ';
    for (i = 0; i < field_w; i++) out[2 + i] = (i < wl) ? full[i] : ' ';
    out[2 + field_w] = '|';
    out[3 + field_w] = '\0';
}
```

`cv_draw_overlay` gains a `tall` flag. When tall, it draws `CV_OVERLAY_ROWS` rows of the narrow list, and the pane's columns are left as spaces — the picture is not text and NBG1 draws it, but the cells must be blank or the text layer would show through the picture.

`render_command_panel` computes the box from the flag:

```c
    int tall = (p.overlay && item_art_available()) ? 1 : 0;
    int input_row = base - (tall ? CV_OVERLAY_RISE : 0);
    int border_top = input_row + 1;
    int content0 = border_top + 1;
    int border_bottom = content0 + (tall ? CV_OVERLAY_TALL_ROWS : CV_STRIP_ROWS);
```

`CV_OVERLAY_RISE` is 5, defined in `panel_layout.h` with the rest. Check it against the numbers before writing anything, because the whole feature's placement rests on them:

| | plain | tall |
|---|---|---|
| `base` = `CV_TOP_MARGIN + console_height()` | 20 | 20 |
| `input_row` | 20 | 15 |
| `border_top` (strip's own top border) | 21 | 16 |
| `content0` | 22 | 17 |
| `border_bottom` (strip's bottom border) | 29 | 29 |
| the overlay's own box | 22–28 | 17–28 |
| box interior | 23–27 (5) | 18–27 (10) |

The bottom border does not move in either case — that is the whole point, and it is the one number to check on screen. `CV_OVERLAY_RISE` is exactly `CV_OVERLAY_TALL_ROWS - CV_STRIP_ROWS`, so define it that way rather than as a bare 5 and let the layout test assert it.

Note the existing code computes `border_bottom = content0 + CR_ROWS` with `CR_ROWS == CV_STRIP_ROWS == 7`; the substitution above is deliberate, because in the tall case the rose is not drawn and its row count is not what bounds the box.

`dash_set` takes the new variant: `dash_set(p.overlay ? (tall ? DASH_OVERLAY_TALL : DASH_OVERLAY) : DASH_PANEL, border_top);`

`image_window_box(0, border_top, 40, border_bottom - border_top + 1)` already derives from those two, so it follows for free — but confirm it, because the black fallback behind the box is what stops the wallpaper showing through when the dashboard is not ready.

Guard the `item_art` include and its two call sites:

```c
#ifndef NETBIN
#include "video/item_art.h"
#endif
```

and in `render_command_panel`, `#ifndef NETBIN` around `item_art_show(...)` / `item_art_hide()`, with `tall` forced to 0 in the netbin so its overlay keeps today's shape.

- [ ] **Step 10: Type-check both configurations**

Run:
```bash
cd saturn && sh syntax-check.sh src/video/command_view.cxx && NETBIN=1 sh syntax-check.sh src/video/command_view.cxx
```
Expected: exit 0 for both. The netbin check is the one that matters here — it proves the guards are right.

- [ ] **Step 11: Re-run the layout and dash tests**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/tovl saturn/tests/test_overlay_layout.c && /tmp/tovl
gcc -O2 -I saturn/src -o /tmp/tdash saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdash
gcc -O2 -I saturn/src -o /tmp/tcp saturn/tests/test_command_panel.c saturn/src/input/command_panel.c && /tmp/tcp
```
Expected: `all pass` for all three. The third is the regression gate — the overlay's input handling must not have moved.

- [ ] **Step 12: Commit**

```bash
git add saturn/src/video/command_view.cxx saturn/src/video/command_view.h
git commit -m "Draw the inventory overlay as a narrower list beside an empty picture pane when the story has item art, raising the input line with the box so the transcript rather than the command being typed is what the taller box covers."
```

---

## Task 8: Wire it up, and see it

**Files:**
- Modify: `saturn/src/main.cxx:619`

**Interfaces:**
- Consumes: everything above
- Produces: a disc with the pane working

- [ ] **Step 1: Set the game**

In `saturn/src/main.cxx`, beside `room_art_set_game(game_release, game_serial);` at `:619`:

```cpp
        item_art_set_game(game_release, game_serial);
```

and add `#include "video/item_art.h"` beside the existing `room_art.h` include.

Note what deliberately does **not** happen here: there is no `display_set_authored`-style flag for items. The pane is gated on `item_art_available()` read live by the renderer, because unlike the room art it does not interact with the Palette setting at all.

- [ ] **Step 2: Close the archive when the game ends**

Find the two `room_art_release()` calls at `main.cxx:405` and `:445` and add `item_art_close();` beside each. Read the surrounding lines first — those are the paths back to the title screen, and leaving 40 KB of Low Work RAM held across a game change is the exact class of leak `room_art_release` exists to prevent.

- [ ] **Step 3: Open and close the archive with the overlay**

The overlay is raised by `cp_overlay_open` and lowered by `cp_overlay_close` in `command_panel.c` — but that file is in the netbin source list and is pure logic with no SRL, so it must **not** call `item_art_open`. Instead, have `render_command_panel` call `item_art_open()` on the first frame the overlay is up and `item_art_close()` on the first frame it is not, tracked by a file-scope `static int g_overlay_was_up` in `command_view.cxx`, behind `#ifndef NETBIN`.

Both calls are idempotent, so a missed edge costs nothing worse than a frame's delay.

- [ ] **Step 4: Type-check both configurations across everything touched**

Run:
```bash
cd saturn
sh syntax-check.sh src/main.cxx src/video/command_view.cxx src/video/item_art.cxx src/video/oitem.c src/scene/items.c
NETBIN=1 sh syntax-check.sh src/video/command_view.cxx src/video/oitem.c src/scene/items.c
```
Expected: exit 0 for every invocation.

- [ ] **Step 5: Run the whole test suite**

Run:
```bash
python -m pytest saturn/tests tools/tests -q
gcc -O2 -I saturn/src -I saturn/tests -o /tmp/toitem saturn/tests/test_oitem.c saturn/src/video/oitem.c saturn/src/video/cgl.c && /tmp/toitem
gcc -O2 -I saturn/src -I saturn/tests -o /tmp/tcgl saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/tcgl
gcc -O2 -I saturn/src -o /tmp/titems saturn/tests/test_items.c saturn/src/scene/items.c && /tmp/titems
gcc -O2 -I saturn/src -o /tmp/tovl saturn/tests/test_overlay_layout.c && /tmp/tovl
gcc -O2 -I saturn/src -o /tmp/tdash saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdash
```
Expected: all green. `test_netbin_sources.py` is the one to watch — it pins the netbin source list exactly, and a failure there means a guard is missing and a CD-only source has been pulled into the netbin build.

- [ ] **Step 6: Build the disc**

Run the full CD build (`compile-cd.bat`, or `update.bat` if the `/BG` staging needs refreshing — `bg.bat` → `games.bat` → `music.bat`, in that order, because `games.bat` injects what `bg.bat` stages and `music.bat` promotes the resulting data track).
Expected: zero errors, a `.cue` and a 32-track image in `BuildDrop/`.

- [ ] **Step 7: See it on screen**

Boot in Mednafen, start Zork I, take something, open the inventory overlay.

Check, and write down each answer:
1. Does the box sit at rows 17–28 with the input line visible above it at row 16?
2. Does the list show ten rows?
3. Does the picture appear in the pane, at the right place, not clipped and not shifted?
4. Does the picture change as the cursor walks the list?
5. Does an unbound item (the sword, or the leaflet) give an empty black plate rather than the previous item's picture?
6. Does the pane vanish when the overlay closes?
7. Is the console text still drawing, and the marble strip?
8. Start a different game — say Enchanter — and open its overlay. Does it show the old nine-row, five-item box with no pane?

- [ ] **Step 8: Commit**

```bash
git add saturn/src/main.cxx saturn/src/video/command_view.cxx
git commit -m "Hand the running story to the item-picture module and hold its archive only while the inventory overlay is open, so a game change never leaves forty kilobytes of Low Work RAM behind."
```

- [ ] **Step 9: Write the handoff**

Per the working agreement, session handoffs go in `mem/`, never a temp directory. Match the format of the memories already there — frontmatter with `name`, `description` and `metadata.type`, `[[wikilinks]]` to related entries — and add a one-line pointer to `mem/MEMORY.md`.

Reference the spec and this plan by path rather than restating them. What the handoff should carry that the commits cannot: the answer Task 1's spike gave and which layer path was taken; the eight screen answers from Step 7; whether the `#00`/`#13` sceptre/bauble call looked right once a real sceptre was in hand; and anything the LWRAM budget test had to be told.

Mark nothing as superseded — this is new ground and supersedes no earlier memory.

---

## Self-Review

**Spec coverage.** Every section of the design maps to a task: asset path → Task 2; record offsets → Task 3; decoder → Task 4; binding, generator, refusals → Task 5; memory, failure, layer, the spike's fallback → Tasks 1 and 6; overlay geometry, conditional-on-story, layer ownership, netbin → Tasks 7 and 8; every listed test → the task that introduces the code it covers. The spec's "what deliberately does not change" (the 36px wallpaper shift, the untouched `here`/`nhere`) is covered by omission — no task touches `dash_view.cxx:88` or `RoomModel`.

**Known soft spots, called out rather than hidden.** Three steps say "read the existing file first and match what is there rather than what is written here": Task 5 Step 4 (`zstory.Story`'s attribute names), Task 6 Step 1 (`test_lwram_budget.py`'s existing constants), and Task 7 Step 6 (`test_dash_map.c`'s accessor names). Those are real uncertainties in files this plan did not fully read, and an implementer following the literal text over the actual file would produce a broken test rather than a broken feature. Task 7 Step 9 contains a deliberate arithmetic trap with the correction spelled out — the naive `border_bottom` derivation is off by one, and the step says so.

**Type consistency.** `oitem_decode` / `oitem_count` / `cgl_lzss` / `items_available` / `items_picture_of` / `item_art_*` / `ITEM_ART_X` / `ITEM_ART_Y` / `CV_OVERLAY_*` / `DASH_OVERLAY_TALL` are each defined in exactly one task and used with the same signature everywhere after it. `OITEM_PIC_N`, `OITEM_PIC_BYTES` and `OITEM_PAL_BYTES` are defined in the generated `.inc` and mirrored under a `#ifndef` guard in `oitem.h`, matching the pattern `presentation.h` already uses for `PRES_FRAME_N`.
