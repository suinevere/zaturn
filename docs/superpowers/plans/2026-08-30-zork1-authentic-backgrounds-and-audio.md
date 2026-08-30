# Zork I Authentic Backgrounds and Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In Zork I, every room shows the background the Sega Saturn release showed and plays the CD-DA track it played, with no other game changing behaviour.

**Architecture:** One generated object-indexed table carries each room's picture, track and SE bank, keyed by Z-machine release and serial. The eleven original `.CGL` archives ship verbatim; the current area's archive stays resident in Low Work RAM and one frame is decompressed per room by a host-testable LZSS decoder. The screen moves to 320x240 so frames are used pixel-for-pixel. `MIX_DYNAMIC` gains one branch that reads the table instead of drawing from a scene pool.

**Tech Stack:** C99 and C++ for SH-2 via SaturnRingLib/SGL, Python 3 for generators, gcc for host tests, pytest for generator tests.

**Spec:** `docs/superpowers/specs/2026-08-30-zork1-authentic-backgrounds-and-audio-design.md`

## Global Constraints

- **Author of record is `suinevere`.** Every file, method and constant gets the project's header block (see `CLAUDE.md`). No comments inside function bodies. Tests and generated files get a file header only.
- **Commits are one sentence.** No body, no bullets, no trailers, and no mention of Claude, AI or a session.
- **`/src` layout:** the entry point is the only file in `saturn/src` root; everything else lives in a subfolder named for its concern, with headers beside their source.
- **Build with `saturn/compile-cd.bat`, not bare `make`.** The CD build's `SOURCES` comes from `find src/ -name '*.c'`, which returns nothing under git-bash and produces a silent link failure. See `memory/zaturn-make-from-git-bash-drops-c-sources.md`.
- **Syntax-only check:** `saturn/syntax-check.sh <file>...`, and `NETBIN=1 saturn/syntax-check.sh <file>...` for the netbin configuration.
- **Generated files under `saturn/src/scene/` are pinned to `eol=lf`** by `.gitattributes`. Generators must write with `newline="\n"`.
- **Story identity for Zork I is release `88`, serial `"840726"`.** Any table binding to it must check both.
- **75 frames exist in the archives; 74 are referenced by rooms.** `BBAR_01` belongs to the barrow ending sequence and no room names it. The decoder fixture covers all 75; the presentation table covers the 74.
- **`SRL_MAX_CD_FILES = 256`** (`saturn/makefile:7`). The disc's file count must stay under this.
- **Any code that steps out of `/Z3` must call `cd_restore_z3()` before returning** — from the moment `game_select()` returns, a bare `SRL::Cd::File("XXX.Z3")` has to resolve.
- **The netbin build must not grow.** Its `SOURCES` in `saturn/makefile` is an explicit list, so new files are excluded by default; `saturn/tests/test_netbin_sources.py` gates it.
- **Never print via SRL debug.** Failures on these paths are silent and hold the current picture.

---

### Task 1: Port the CGL decoder to C

The archives are the one asset format the Saturn side cannot already read. This task produces a decoder provably identical to the Python one before any of it runs on hardware.

**Files:**
- Create: `saturn/src/video/cgl.c`
- Create: `saturn/src/video/cgl.h`
- Create: `tools/gen_cgl_fixture.py`
- Create: `saturn/tests/fixtures/cgl_sums.inc`
- Create: `saturn/tests/test_cgl.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `unsigned long cgl_decode(const unsigned char *rec, unsigned long rec_len, unsigned char *dst, unsigned long dst_cap);` — returns bytes written, 0 on refusal.
  - `void cgl_palette(const unsigned char *rec, unsigned short *out);` — writes 256 Saturn RGB555 words.
  - `#define CGL_PAL_BYTES 512`, `CGL_WIDTH 320`, `CGL_HEIGHT 240`, `CGL_FRAME_BYTES`, `CGL_RING 4096`

- [ ] **Step 1: Write the fixture generator**

Create `tools/gen_cgl_fixture.py`. It reads the record offsets from the archives themselves rather than from the room table, because the room table only names the 74 frames rooms use and the decoder should be proved against all 75. The palette checksum is taken from the **raw CLUT bytes**, never through `zork_cgl.load_clut`, whose 5-bit-to-8-bit expansion does not round-trip:

```python
#!/usr/bin/env python3
"""/*----------------------
 | gen_cgl_fixture.py
 | Description: GENERATES saturn/tests/fixtures/cgl_sums.inc -- one row per
 |     frame of every B*.CGL archive, carrying the record's offset and length
 |     and FNV-1a checksums of the pixels and the palette the Python decoder
 |     produces. test_cgl.c decodes the same records with the C port and
 |     compares, which is what makes the port provable off hardware.
 |
 |     Offsets come from walking the archives, not from
 |     room_backgrounds.csv: the CSV names only the 74 frames rooms reference
 |     and the decoder should be proved against all 75.
 |
 |     The palette checksum is taken over the raw CLUT bytes converted straight
 |     to Saturn words. It deliberately does not go through zork_cgl.load_clut,
 |     whose expansion to 8-bit channels is lossy in the low bits and would not
 |     match what the C side computes.
 | Author: suinevere
 | Dependencies: pathlib, sys, analysis.zork_cgl
 | Globals: ROOT, RAW, OUT, ARCHIVES
 ----------------------*/"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
RAW = ROOT / "analysis" / "zork_bg" / "raw"
OUT = ROOT / "saturn" / "tests" / "fixtures" / "cgl_sums.inc"

sys.path.insert(0, str(ROOT / "analysis"))
import zork_cgl  # noqa: E402

ARCHIVES = ["BBAR", "BCEL", "BDAM", "BDED", "BHUS", "BMAZ",
            "BMIN", "BMIR", "BRIV", "BTMP", "BWOD"]


def fnv1a(data):
    """/*----------------------
     | fnv1a
     | Description: 32-bit FNV-1a, matching test_cgl.c's own implementation.
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


def saturn_palette(buf, pos):
    """/*----------------------
     | saturn_palette
     | Description: The record's CLUT as the 512 bytes the Saturn will hold --
     |     each little-endian RGB555 word with the opaque bit forced on. The two
     |     formats share a channel layout, so there is no channel arithmetic.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: buf -- archive bytes; pos -- offset of the record
     | Returns: 512 bytes
     ----------------------*/"""
    out = bytearray()
    for i in range(pos, pos + zork_cgl.PAL_BYTES, 2):
        v = (buf[i] | (buf[i + 1] << 8)) & 0x7FFF
        v |= 0x8000
        out.append(v & 0xFF)
        out.append((v >> 8) & 0xFF)
    return bytes(out)


def main(argv):
    """/*----------------------
     | main
     | Description: Writes one fixture row per frame of every archive.
     | Author: suinevere
     | Dependencies: pathlib, zork_cgl
     | Globals: RAW, OUT, ARCHIVES
     | Params: argv -- command-line arguments (unused; accepted for test calls)
     | Returns: 0
     ----------------------*/"""
    lines = ["/*----------------------",
             " | cgl_sums.inc",
             " | Description: GENERATED FILE -- do not edit by hand; produced by",
             " |   tools/gen_cgl_fixture.py. One CglExpect row per CGL frame.",
             " | Author: suinevere",
             " ----------------------*/"]
    total = 0
    for name in ARCHIVES:
        buf = (RAW / f"{name}.CGL").read_bytes()
        found = [(idx, pos, data) for idx, pos, _pal, data in zork_cgl.records(buf)]
        for n, (idx, pos, data) in enumerate(found):
            end = found[n + 1][1] if n + 1 < len(found) else len(buf)
            lines.append(
                f'    {{ "{name}.CGL", {idx}, {pos}UL, {end - pos}UL, '
                f'{fnv1a(data[:zork_cgl.FRAME_BYTES])}UL, '
                f'{fnv1a(saturn_palette(buf, pos))}UL }},')
            total += 1
    if total != 75:
        raise SystemExit(f"{total} frames found, expected 75")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

Run it:

```bash
tools/.venv/Scripts/python.exe tools/gen_cgl_fixture.py
wc -l saturn/tests/fixtures/cgl_sums.inc
```

Expected: exits 0; the file holds 6 header lines plus 75 rows.

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_cgl.c`:

```c
/*----------------------
 | test_cgl.c
 | Description: The C port of the CGL decoder against checksums taken from the
 |   Python decoder in analysis/zork_cgl.py, over the real archives in
 |   analysis/zork_bg/raw. Run from the repository root:
 |   gcc -O2 -I saturn/src -o /tmp/tcgl \
 |       saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/tcgl
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    const char   *archive;
    int           frame;
    unsigned long offset;
    unsigned long length;
    unsigned long pixel_sum;
    unsigned long pal_sum;
} CglExpect;

static const CglExpect EXPECT[] = {
#include "fixtures/cgl_sums.inc"
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
    static unsigned char pixels[CGL_FRAME_BYTES];
    static unsigned char palbytes[512];
    unsigned short pal[256];
    char path[256];
    int i;

    check(EXPECT_N == 75, "the fixture covers all 75 frames in the archives");

    for (i = 0; i < EXPECT_N; i++) {
        unsigned long flen = 0, got;
        unsigned char *file;
        int j;

        sprintf(path, "analysis/zork_bg/raw/%s", EXPECT[i].archive);
        file = slurp(path, &flen);
        if (!file) { printf("FAIL cannot read %s\n", path); fails++; continue; }
        check(EXPECT[i].offset + EXPECT[i].length <= flen,
              "the frame record lies inside the archive");

        got = cgl_decode(file + EXPECT[i].offset, EXPECT[i].length,
                         pixels, sizeof(pixels));
        check(got == CGL_FRAME_BYTES, "a frame decodes to exactly 320x240 bytes");
        check(fnv1a(pixels, CGL_FRAME_BYTES) == EXPECT[i].pixel_sum,
              "decoded pixels match the Python decoder");

        cgl_palette(file + EXPECT[i].offset, pal);
        for (j = 0; j < 256; j++) {
            palbytes[j * 2]     = (unsigned char) (pal[j] & 0xff);
            palbytes[j * 2 + 1] = (unsigned char) ((pal[j] >> 8) & 0xff);
        }
        check(fnv1a(palbytes, sizeof(palbytes)) == EXPECT[i].pal_sum,
              "palette words match the Python decoder");
        for (j = 0; j < 256; j++) {
            if ((pal[j] & 0x8000u) == 0) {
                check(0, "every palette word is opaque");
                break;
            }
        }

        free(file);
    }

    {
        unsigned char junk[16];
        memset(junk, 0, sizeof(junk));
        check(cgl_decode(junk, sizeof(junk), pixels, sizeof(pixels)) == 0,
              "a record shorter than a palette plus header is refused");
    }
    {
        static unsigned char rec[CGL_PAL_BYTES + 8];
        memset(rec, 0, sizeof(rec));
        rec[CGL_PAL_BYTES + 2] = 0x10;
        check(cgl_decode(rec, sizeof(rec), pixels, 16) == 0,
              "a declared size larger than the destination is refused");
    }
    {
        static unsigned char rec[CGL_PAL_BYTES + 8];
        memset(rec, 0, sizeof(rec));
        check(cgl_decode(rec, sizeof(rec), pixels, sizeof(pixels)) == 0,
              "a declared size of zero is refused");
    }
    check(cgl_decode(0, 100, pixels, sizeof(pixels)) == 0, "a null record is refused");

    printf(fails ? "%d FAILED\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/tcgl saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/tcgl
```

Expected: FAIL — `saturn/src/video/cgl.c: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `saturn/src/video/cgl.h`:

```c
/*----------------------
 | cgl.h
 | Description: Decoder for the Zork I (Saturn, Japan) room-background archives.
 |   A B*.CGL is a chain of 4-byte-aligned records, each a 256-entry RGB555
 |   little-endian CLUT followed by an Okumura-LZSS stream that expands to one
 |   320x240 8bpp frame. Pure logic: no SRL, no disc, no VDP2, so the host tests
 |   link it with plain gcc and the port can be proved before it ever runs on
 |   hardware.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef CGL_H
#define CGL_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CGL_PAL_BYTES / CGL_WIDTH / CGL_HEIGHT / CGL_FRAME_BYTES / CGL_RING
 | Description: The fixed geometry of a CGL record and the size of the LZSS
 |   ring. Every frame on the disc is 320x240, which is why the client runs at
 |   320x240 rather than cropping.
 | Author: suinevere
 ----------------------*/
#define CGL_PAL_BYTES   512
#define CGL_WIDTH       320
#define CGL_HEIGHT      240
#define CGL_FRAME_BYTES (CGL_WIDTH * CGL_HEIGHT)
#define CGL_RING        4096

/*----------------------
 | cgl_decode
 | Description: Decompresses one record's LZSS stream into dst. Refuses rather
 |   than truncating: a null argument, a record too short to hold a palette and
 |   a size header, a declared size of zero, or a declared size larger than
 |   dst_cap all return 0 with dst untouched, which every caller reads as "hold
 |   the picture already showing".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ring
 | Params: rec -- the record, starting at its CLUT; rec_len -- its byte length;
 |   dst -- destination for the 8bpp pixels; dst_cap -- capacity of dst
 | Returns: bytes written, or 0 on refusal
 ----------------------*/
unsigned long cgl_decode(const unsigned char *rec, unsigned long rec_len,
                         unsigned char *dst, unsigned long dst_cap);

/*----------------------
 | cgl_palette
 | Description: Converts a record's 256-entry CLUT to Saturn CRAM words. The two
 |   formats share a channel layout -- red in bits 0-4, green 5-9, blue 10-14 --
 |   so the whole conversion is a little-endian read and the opaque bit, with no
 |   channel arithmetic at all.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: rec -- the record, starting at its CLUT; out -- 256 words to write
 | Returns: N/A
 ----------------------*/
void cgl_palette(const unsigned char *rec, unsigned short *out);

#ifdef __cplusplus
}
#endif
#endif /* CGL_H */
```

- [ ] **Step 5: Write the implementation**

Create `saturn/src/video/cgl.c`:

```c
/*----------------------
 | cgl.c
 | Description: See cgl.h. The LZSS is the Okumura variant the disc uses for
 |   *.CGZ and *.SLD alike, ported from analysis/zork_cgl.py.
 | Author: suinevere
 | Dependencies: cgl.h
 | Globals: g_ring
 ----------------------*/
#include "cgl.h"

/*----------------------
 | g_ring
 | Description: The LZSS window. A file-scope static rather than a local because
 |   4 KiB is more stack than a Saturn frame should carry, and nothing here is
 |   re-entrant.
 | Author: suinevere
 ----------------------*/
static unsigned char g_ring[CGL_RING];

/*----------------------
 | cgl_decode
 | Description: See cgl.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_ring
 | Params: rec, rec_len, dst, dst_cap -- see cgl.h
 | Returns: bytes written, or 0 on refusal
 ----------------------*/
unsigned long cgl_decode(const unsigned char *rec, unsigned long rec_len,
                         unsigned char *dst, unsigned long dst_cap) {
    unsigned long size, out = 0, i;
    unsigned int flags = 0, nbits = 0, r = CGL_RING - 18;

    if (rec == 0 || dst == 0) return 0;
    if (rec_len < (unsigned long) CGL_PAL_BYTES + 4) return 0;

    size = (unsigned long) rec[CGL_PAL_BYTES]
         | ((unsigned long) rec[CGL_PAL_BYTES + 1] << 8)
         | ((unsigned long) rec[CGL_PAL_BYTES + 2] << 16)
         | ((unsigned long) rec[CGL_PAL_BYTES + 3] << 24);
    if (size == 0 || size > dst_cap) return 0;

    for (i = 0; i < CGL_RING; i++) g_ring[i] = 0;

    i = (unsigned long) CGL_PAL_BYTES + 4;
    while (i < rec_len && out < size) {
        unsigned int bit;
        if (nbits == 0) {
            flags = rec[i++];
            nbits = 8;
            if (i >= rec_len) break;
        }
        bit = flags & 1u; flags >>= 1; nbits--;
        if (bit) {
            unsigned char c = rec[i++];
            dst[out++] = c;
            g_ring[r] = c; r = (r + 1u) & (CGL_RING - 1u);
        } else {
            unsigned int off, len, k;
            if (i + 1 >= rec_len) break;
            off = ((unsigned int) (rec[i + 1] & 0xf0u) << 4) | (unsigned int) rec[i];
            len = (unsigned int) (rec[i + 1] & 0x0fu) + 3u;
            i += 2;
            for (k = 0; k < len && out < size; k++) {
                unsigned char c = g_ring[(off + k) & (CGL_RING - 1u)];
                dst[out++] = c;
                g_ring[r] = c; r = (r + 1u) & (CGL_RING - 1u);
            }
        }
    }
    return out;
}

/*----------------------
 | cgl_palette
 | Description: See cgl.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: rec -- the record; out -- 256 words to write
 | Returns: N/A
 ----------------------*/
void cgl_palette(const unsigned char *rec, unsigned short *out) {
    int i;
    if (rec == 0 || out == 0) return;
    for (i = 0; i < 256; i++) {
        unsigned int v = (unsigned int) rec[i * 2]
                       | ((unsigned int) rec[i * 2 + 1] << 8);
        out[i] = (unsigned short) (0x8000u | (v & 0x7fffu));
    }
}
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/tcgl saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/tcgl
```

Expected: `ok`

If a `pixel_sum` mismatches, the fault is the flag-bit handling or the offset computation, not the ring — compare against `analysis/zork_cgl.py:_lzss` line by line before changing anything. The one deliberate difference is that the C match loop stops at `size` where the Python one does not; real records never overshoot, so this cannot change correct output.

- [ ] **Step 7: Verify SH-2 compilation in both configurations**

```bash
saturn/syntax-check.sh saturn/src/video/cgl.c
NETBIN=1 saturn/syntax-check.sh saturn/src/video/cgl.c
```

Expected: clean, zero warnings, both times.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/video/cgl.c saturn/src/video/cgl.h \
        tools/gen_cgl_fixture.py saturn/tests/fixtures/cgl_sums.inc \
        saturn/tests/test_cgl.c
git commit -m "Port the Zork I room-background decoder to C as pure logic the host tests link with plain gcc, and prove it against checksums the Python decoder produces for all 75 frames of the eleven archives, so the SH-2 port is known correct before it runs on hardware once."
```

---

### Task 2: Put the archives on the disc and take the stale art off

The archives must exist on the disc before anything can read them, and the 157 mood-era TGAs have been unreachable since `GAME_SCENE` went all-zero. Doing both at once keeps the file-count check meaningful.

**Files:**
- Create: `saturn/cd/data/BG/BBAR.CGL` … `BWOD.CGL` (11 files, copied)
- Delete: the twelve mood directories under `saturn/cd/data/TGA/`

**Interfaces:**
- Consumes: nothing.
- Produces: disc path `BG/<STEM>.CGL`, uppercase 8.3, read by Task 7.

- [ ] **Step 1: Copy the archives onto the disc tree**

```bash
mkdir -p saturn/cd/data/BG
for f in BBAR BCEL BDAM BDED BHUS BMAZ BMIN BMIR BRIV BTMP BWOD; do
  cp "analysis/zork_bg/raw/$f.CGL" "saturn/cd/data/BG/$f.CGL"
done
ls saturn/cd/data/BG | wc -l
du -sh saturn/cd/data/BG
```

Expected: `11`, about 2.0 MB. `OVER.CGL` is deliberately not copied — no room references it and its contents are not characterised.

- [ ] **Step 2: Remove the unreachable mood art**

```bash
git rm -r -q saturn/cd/data/TGA/DESERT saturn/cd/data/TGA/DUNGN \
             saturn/cd/data/TGA/HORROR saturn/cd/data/TGA/HOUSE \
             saturn/cd/data/TGA/MAGIC saturn/cd/data/TGA/MYSTERY \
             saturn/cd/data/TGA/NAUTICAL saturn/cd/data/TGA/SCIFI \
             saturn/cd/data/TGA/TOWN saturn/cd/data/TGA/UNDRGRND \
             saturn/cd/data/TGA/WATER saturn/cd/data/TGA/WILDER
ls saturn/cd/data/TGA
```

Expected: only `SUINE.TGA` remains — the title and menu wallpaper, addressed by literal filename and still live.

- [ ] **Step 3: Verify the disc file count stays inside the limit**

```bash
find saturn/cd -type f | wc -l
grep -n "SRL_MAX_CD_FILES" saturn/makefile
```

Expected: the count is far below 256.

- [ ] **Step 4: Build the CD image to confirm the tree is still packable**

```bash
saturn/compile-cd.bat debug
```

Expected: build succeeds and `saturn/BuildDrop/` holds a fresh `.iso`.

- [ ] **Step 5: Commit**

```bash
git add -A saturn/cd/data/BG saturn/cd/data/TGA
git commit -m "Put the eleven original room-background archives on the disc and take off the 157 mood-era TGAs, which no code has been able to reach since the per-game scene table went all-zero, for a net saving of about ten megabytes."
```

---

### Task 3: Generate the per-room presentation table

**Files:**
- Create: `tools/assets/zork1_room_aliases.json`
- Create: `tools/gen_presentation.py`
- Create: `saturn/src/scene/game_presentation.inc` (generated)
- Create: `tools/tests/test_gen_presentation.py`

**Interfaces:**
- Consumes: `analysis/zork_bg/room_backgrounds.csv`, `tools/assets/rooms/ZORK1.json`, `cd/Zork I - The Great Underground Empire (Japan)/zork1/1dungeon.zil`.
- Produces, in `game_presentation.inc`:
  - `typedef struct { unsigned char image, track, se_bank; } Presentation;`
  - `typedef struct { unsigned char area; unsigned long offset, length; } PresFrame;`
  - `typedef struct { unsigned short release; const char *serial; const Presentation *rooms; } GamePresMap;`
  - `#define PRES_GAME_N 1`, `#define PRES_FRAME_N 74`, `#define PRES_AREA_N 11`
  - `static const char *const PRES_AREA[PRES_AREA_N];`
  - `static const PresFrame IMAGE_FRAME[PRES_FRAME_N];`
  - `static const Presentation GAME_PRES_ZORK1[256];`
  - `static const GamePresMap GAME_PRES_MAP[PRES_GAME_N];`

- [ ] **Step 1: Write the alias table**

Create `tools/assets/zork1_room_aliases.json`. Keys are the story file's titles, values the Saturn titles, both upper-cased:

```json
{
  "_comment": "Story-file room title -> Saturn room title, for the fourteen rooms whose title differs between the two. Hand-checked against 1dungeon.zil's exits. CAVE/SHAFT and STRANGE PASSAGE/NARROW PASSAGE share no words and cannot be inferred from the names alone.",
  "SMELLY ROOM": "FOUL ROOM",
  "ROCKY LEDGE": "LEDGE",
  "TWISTING PASSAGE": "CURVED PASSAGE",
  "STRANGE PASSAGE": "NARROW PASSAGE",
  "CAVE": "SHAFT",
  "THE TROLL ROOM": "TROLL ROOM",
  "DAM BASE": "BASE OF DAM",
  "EGYPTIAN ROOM": "EGYPT ROOM",
  "STONE BARROW": "BARROW ENTRANCE",
  "MAINTENANCE ROOM": "CONTROL ROOM",
  "DAM": "FLOOD CONTROL DAM",
  "LAND OF THE DEAD": "LAND OF THE LIVING DEAD"
}
```

- [ ] **Step 2: Write the generator**

Create `tools/gen_presentation.py`:

```python
#!/usr/bin/env python3
"""/*----------------------
 | gen_presentation.py
 | Description: GENERATES saturn/src/scene/game_presentation.inc -- Zork I's
 |     per-room picture, CD-DA track and sound-effect bank, indexed by
 |     Z-machine object number and keyed by release and serial, plus the frame
 |     offsets that let a late frame be reached without decompressing every
 |     earlier one.
 |
 |     Joins the Saturn room table to the story file's rooms by title, through
 |     a hand-checked alias table for the fourteen rooms that were renamed, and
 |     resolves same-title groups by pairing story object order against Saturn
 |     room-index order. Refuses rather than guessing: any room left
 |     unresolved, any Saturn row claimed twice, or a story whose release and
 |     serial are not 88 / 840726 raises instead of writing a zero, because a
 |     zero would show up only as a background that silently fails to change.
 | Author: suinevere
 | Dependencies: csv, json, pathlib, re, sys
 | Globals: ROOT, CSV, ROOMS, ZIL, ALIASES, OUT, AREAS, SE_BANKS, RELEASE, SERIAL
 ----------------------*/"""
import csv
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CSV = ROOT / "analysis" / "zork_bg" / "room_backgrounds.csv"
ROOMS = ROOT / "tools" / "assets" / "rooms" / "ZORK1.json"
ZIL = (ROOT / "cd" / "Zork I - The Great Underground Empire (Japan)"
            / "zork1" / "1dungeon.zil")
ALIASES = ROOT / "tools" / "assets" / "zork1_room_aliases.json"
OUT = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"

AREAS = ["BBAR", "BCEL", "BDAM", "BDED", "BHUS", "BMAZ",
         "BMIN", "BMIR", "BRIV", "BTMP", "BWOD"]
SE_BANKS = ["SEALL", "SEMINA", "SEMINB", "SEMIR", "SEDAM", "SECEL",
            "SEHDS", "SERIV", "SEWOD", "SEMAZ", "SEBAR"]

RELEASE = 88
SERIAL = "840726"


def zil_rooms():
    """/*----------------------
     | zil_rooms
     | Description: The room names 1dungeon.zil declares, in declaration order.
     |     Read only as a count check: the story file and the Saturn table are
     |     the two sides actually joined, and the ZIL is the third witness that
     |     both describe the same 110 rooms.
     | Author: suinevere
     | Dependencies: re, pathlib
     | Globals: ZIL
     | Params: N/A
     | Returns: a list of room names
     ----------------------*/"""
    text = ZIL.read_text(encoding="utf-8", errors="replace")
    return re.findall(r"^<ROOM\s+([A-Z0-9\-]+)", text, re.MULTILINE)


def load_saturn():
    """/*----------------------
     | load_saturn
     | Description: The Saturn presentation rows, in room-index order.
     | Author: suinevere
     | Dependencies: csv
     | Globals: CSV
     | Params: N/A
     | Returns: a list of dicts, one per Saturn room 0..109
     ----------------------*/"""
    with CSV.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    rows.sort(key=lambda r: int(r["room"]))
    return rows


def build_join():
    """/*----------------------
     | build_join
     | Description: Maps each Z-machine object number to its Saturn row. Groups
     |     both sides by title -- story titles first passed through the alias
     |     table -- checks the group sizes agree, and pairs within a group by
     |     object order against Saturn room-index order.
     | Author: suinevere
     | Dependencies: json
     | Globals: ROOMS, ALIASES, RELEASE, SERIAL
     | Params: N/A
     | Returns: {object number: saturn row dict}
     ----------------------*/"""
    story = json.loads(ROOMS.read_text(encoding="utf-8"))
    if story["release"] != RELEASE or story["serial"] != SERIAL:
        raise SystemExit(
            f"ZORK1.Z3 is release {story['release']} serial {story['serial']}, "
            f"not {RELEASE} / {SERIAL}; the table would bind to the wrong objects")

    alias = {k: v for k, v in json.loads(
        ALIASES.read_text(encoding="utf-8")).items() if not k.startswith("_")}
    saturn = load_saturn()
    story_rooms = sorted(story["rooms"], key=lambda r: r["obj"])

    if len(story_rooms) != len(saturn):
        raise SystemExit(f"{len(story_rooms)} story rooms against "
                         f"{len(saturn)} Saturn rooms")
    if len(zil_rooms()) != len(saturn):
        raise SystemExit(f"{len(zil_rooms())} ZIL rooms against "
                         f"{len(saturn)} Saturn rooms")

    def title_of(room):
        t = room["title"].strip().upper()
        return alias.get(t, t)

    by_title = {}
    for r in story_rooms:
        by_title.setdefault(title_of(r), []).append(r)
    sat_by_title = {}
    for r in saturn:
        sat_by_title.setdefault(r["title"].strip().upper(), []).append(r)

    lopsided = set(by_title) ^ set(sat_by_title)
    if lopsided:
        raise SystemExit(f"titles present on one side only: {sorted(lopsided)}")

    join = {}
    claimed = set()
    for title, group in by_title.items():
        sat_group = sat_by_title[title]
        if len(group) != len(sat_group):
            raise SystemExit(f"{title}: {len(group)} story rooms against "
                             f"{len(sat_group)} Saturn rooms")
        for room, sat in zip(sorted(group, key=lambda r: r["obj"]),
                             sorted(sat_group, key=lambda r: int(r["room"]))):
            if int(sat["room"]) in claimed:
                raise SystemExit(f"Saturn room {sat['room']} claimed twice")
            claimed.add(int(sat["room"]))
            join[room["obj"]] = sat

    if len(join) != len(saturn):
        raise SystemExit(f"{len(join)} rooms resolved of {len(saturn)}")
    return join


def frame_table():
    """/*----------------------
     | frame_table
     | Description: One row per distinct frame a room references: its area, byte
     |     offset and byte length inside that area's archive, in first-seen
     |     order. There are 74 of these, not 75 -- BBAR_01 belongs to the barrow
     |     ending sequence and no room names it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: AREAS
     | Params: N/A
     | Returns: (list of (area index, offset, length), {(archive, frame): index})
     ----------------------*/"""
    seen = {}
    rows = []
    for r in load_saturn():
        key = (r["area_archive"], int(r["frame"]))
        if key in seen:
            continue
        area = AREAS.index(r["area_archive"].replace(".CGL", ""))
        seen[key] = len(rows)
        rows.append((area, int(r["frame_offset"]), int(r["frame_length"])))
    return rows, seen


def main(argv):
    """/*----------------------
     | main
     | Description: Writes game_presentation.inc.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: OUT, AREAS, SE_BANKS, RELEASE, SERIAL
     | Params: argv -- command-line arguments (unused; accepted for test calls)
     | Returns: 0
     ----------------------*/"""
    join = build_join()
    frames, index_of = frame_table()

    pres = [(0, 0, 0)] * 256
    for obj, sat in join.items():
        if obj >= 256:
            raise SystemExit(f"object {obj} is outside the 256-entry table")
        image = index_of[(sat["area_archive"], int(sat["frame"]))] + 1
        track = int(sat["cd_track"])
        if track != 0 and not (2 <= track <= 32):
            raise SystemExit(f"object {obj} names track {track}, "
                             f"which is neither silence nor a disc track")
        pres[obj] = (image, track, SE_BANKS.index(sat["se_bank"]))

    lines = ["/*----------------------",
             " | game_presentation.inc",
             " | Description: GENERATED FILE -- do not edit by hand; produced by",
             " |   tools/gen_presentation.py. Zork I's per-room picture, CD-DA",
             " |   track and sound-effect bank indexed by object number, the",
             " |   frame offsets inside each archive, and the table that keys",
             " |   them by release and serial. image is 1-based so 0 means",
             " |   unauthored; track 0 means silence, which ten rooms want.",
             " | Author: suinevere",
             " ----------------------*/",
             "typedef struct {",
             "    unsigned char image;",
             "    unsigned char track;",
             "    unsigned char se_bank;",
             "} Presentation;",
             "typedef struct {",
             "    unsigned char area;",
             "    unsigned long offset;",
             "    unsigned long length;",
             "} PresFrame;",
             "typedef struct {",
             "    unsigned short release;",
             "    const char *serial;",
             "    const Presentation *rooms;",
             "} GamePresMap;",
             "#define PRES_GAME_N 1",
             f"#define PRES_FRAME_N {len(frames)}",
             f"#define PRES_AREA_N {len(AREAS)}",
             "static const char *const PRES_AREA[PRES_AREA_N] = {"]
    for a in AREAS:
        lines.append(f'    "{a}",')
    lines.append("};")
    lines.append("static const PresFrame IMAGE_FRAME[PRES_FRAME_N] = {")
    for area, off, ln in frames:
        lines.append(f"    {{ {area}, {off}UL, {ln}UL }},")
    lines.append("};")
    lines.append("static const Presentation GAME_PRES_ZORK1[256] = {")
    for i in range(0, 256, 4):
        chunk = ", ".join(f"{{ {a}, {b}, {c} }}" for a, b, c in pres[i:i + 4])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("static const GamePresMap GAME_PRES_MAP[PRES_GAME_N] = {")
    lines.append(f'    {{ {RELEASE}, "{SERIAL}", GAME_PRES_ZORK1 }},')
    lines.append("};")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 3: Write the failing generator test**

Create `tools/tests/test_gen_presentation.py`:

```python
"""Zork I's generated presentation table: complete, bounded, byte-identical."""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import gen_presentation as g

INC = REPO / "saturn" / "src" / "scene" / "game_presentation.inc"


def test_regeneration_is_byte_identical():
    before = INC.read_bytes()
    g.main([])
    assert INC.read_bytes() == before


def test_every_saturn_room_is_claimed_exactly_once():
    join = g.build_join()
    assert len(join) == 110
    assert sorted(int(r["room"]) for r in join.values()) == list(range(110))


def test_the_table_is_keyed_by_release_and_serial():
    text = INC.read_text(encoding="utf-8")
    assert '{ 88, "840726", GAME_PRES_ZORK1 }' in text


def test_rooms_reference_seventy_four_of_the_seventy_five_frames():
    frames, index_of = g.frame_table()
    assert len(frames) == 74
    assert len(index_of) == 74
    assert "#define PRES_FRAME_N 74" in INC.read_text(encoding="utf-8")


def test_every_room_has_a_picture():
    join = g.build_join()
    _frames, index_of = g.frame_table()
    for sat in join.values():
        assert (sat["area_archive"], int(sat["frame"])) in index_of


def test_ten_rooms_are_silent():
    join = g.build_join()
    assert len([s for s in join.values() if int(s["cd_track"]) == 0]) == 10


def test_west_of_house_lands_on_the_house_exterior():
    join = g.build_join()
    obj = next(o for o, s in join.items()
               if s["title"].strip().upper() == "WEST OF HOUSE")
    assert join[obj]["image"] == "BHUS_00.png"
    assert int(join[obj]["cd_track"]) == 10


def test_a_wrong_story_identity_is_refused(monkeypatch):
    monkeypatch.setattr(g, "RELEASE", 89)
    try:
        g.build_join()
    except SystemExit:
        return
    raise AssertionError("a mismatched release was accepted")
```

- [ ] **Step 4: Run the test to verify it fails**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/test_gen_presentation.py -v
```

Expected: FAIL — `game_presentation.inc` does not exist.

- [ ] **Step 5: Generate the table**

```bash
tools/.venv/Scripts/python.exe tools/gen_presentation.py
grep -c "^    { " saturn/src/scene/game_presentation.inc
```

Expected: exits 0 and writes the file.

If it exits with `titles present on one side only`, the alias table is wrong — fix `zork1_room_aliases.json`, do not relax the check. If it exits with a group-size mismatch, one alias maps into a group that is already full.

- [ ] **Step 6: Run the test to verify it passes**

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests/test_gen_presentation.py -v
```

Expected: 8 passed.

- [ ] **Step 7: Confirm the two uninferable aliases against the ZIL**

```bash
grep -n "^<ROOM CAVE\|^<ROOM SHAFT\|^<ROOM STRANGE\|^<ROOM NARROW" \
  "cd/Zork I - The Great Underground Empire (Japan)/zork1/1dungeon.zil"
```

Read each of those rooms' exits and confirm by hand that `CAVE`→`SHAFT` and `STRANGE PASSAGE`→`NARROW PASSAGE` connect the same places on both sides. These are the only two rows in the alias table that cannot be read off the names, and a wrong one puts a cave picture in a passage silently — no test will catch it.

- [ ] **Step 8: Commit**

```bash
git add tools/assets/zork1_room_aliases.json tools/gen_presentation.py \
        tools/tests/test_gen_presentation.py \
        saturn/src/scene/game_presentation.inc
git commit -m "Generate Zork I's per-room picture, track and sound-effect bank as one object-indexed table keyed by release and serial, joining the Saturn room records to the story file by title through a hand-checked alias table for the fourteen renamed rooms and by index order within the same-title groups, and refusing rather than writing a zero for anything it cannot resolve."
```

---

### Task 4: The runtime lookup

**Files:**
- Create: `saturn/src/scene/presentation.c`
- Create: `saturn/src/scene/presentation.h`
- Create: `saturn/tests/test_presentation.c`

**Interfaces:**
- Consumes: `game_presentation.inc` from Task 3.
- Produces:
  - `int pres_game_index(unsigned int release, const char *serial);`
  - `int pres_of_room(unsigned int release, const char *serial, unsigned int obj, Presentation *out);`
  - `int pres_frame(int image, int *area, unsigned long *offset, unsigned long *length);`
  - `const char *pres_area_name(int area);`

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_presentation.c`:

```c
/*----------------------
 | test_presentation.c
 | Description: Lookup, bounds and identity for the generated presentation
 |   table. Run from the repository root:
 |   gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tpres \
 |       saturn/tests/test_presentation.c saturn/src/scene/presentation.c && /tmp/tpres
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "scene/presentation.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

int main(void) {
    Presentation p;
    int area, authored = 0, silent = 0;
    unsigned long off, len;
    unsigned int obj;

    check(PRES_FRAME_N == 74, "rooms reference 74 of the 75 frames");
    check(PRES_AREA_N == 11, "there are eleven area archives");

    check(pres_game_index(88, "840726") == 0, "Zork I is the known game");
    check(pres_game_index(88, "999999") == -1, "a wrong serial is unknown");
    check(pres_game_index(999, "840726") == -1, "a wrong release is unknown");
    check(pres_game_index(88, 0) == -1, "a null serial is unknown");

    check(pres_of_room(999, "000000", 15, &p) == 0, "an unknown game has no room");
    check(pres_of_room(88, "840726", 999, &p) == 0, "an out-of-range object is refused");
    check(pres_of_room(88, "840726", 15, 0) == 0, "a null destination is refused");

    for (obj = 0; obj < 256; obj++) {
        if (!pres_of_room(88, "840726", obj, &p)) continue;
        authored++;
        check(p.image >= 1 && p.image <= PRES_FRAME_N, "an authored image is in range");
        check(p.track == 0 || (p.track >= 2 && p.track <= 32),
              "an authored track is silence or a real disc track");
        check(p.se_bank <= 10, "an authored SE bank is in range");
        if (p.track == 0) silent++;
        check(pres_frame((int) p.image, &area, &off, &len) == 1,
              "every authored image has a frame record");
        check(area >= 0 && area < PRES_AREA_N, "the frame's area is in range");
        check(len > 512, "a frame record is larger than its palette alone");
        check(pres_area_name(area) != 0, "the frame's area has a name");
    }
    check(authored == 110, "all 110 rooms are authored");
    check(silent == 10, "ten rooms are silent");

    check(pres_frame(0, &area, &off, &len) == 0, "image 0 has no frame record");
    check(pres_frame(PRES_FRAME_N + 1, &area, &off, &len) == 0,
          "an image past the table has no frame record");
    check(pres_frame(1, 0, 0, 0) == 1, "null outputs are tolerated");
    check(pres_area_name(-1) == 0, "a negative area has no name");
    check(pres_area_name(PRES_AREA_N) == 0, "an out-of-range area has no name");

    printf(fails ? "%d FAILED\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tpres \
    saturn/tests/test_presentation.c saturn/src/scene/presentation.c && /tmp/tpres
```

Expected: FAIL — `presentation.c: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/scene/presentation.h`:

```c
/*----------------------
 | presentation.h
 | Description: Runtime lookup for the per-room presentation table generated by
 |   tools/gen_presentation.py -- the picture, CD-DA track and sound-effect bank
 |   the original console release used for one room of one game. Games without a
 |   table are unaffected and keep the scene path in scene_map.h.
 | Author: suinevere
 | Dependencies: game_presentation.inc
 ----------------------*/
#ifndef PRESENTATION_H
#define PRESENTATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "game_presentation.inc"

/*----------------------
 | pres_game_index
 | Description: The GAME_PRES_MAP row for one game, by release and 6-char
 |   serial. The one call a caller needs to ask "does this story have an
 |   authored presentation at all".
 | Author: suinevere
 | Dependencies: string.h (memcmp), game_presentation.inc
 | Globals: GAME_PRES_MAP
 | Params: release -- Z-machine release; serial -- 6-char serial, not
 |   guaranteed NUL-terminated
 | Returns: the row index, or -1 when the game has no table
 ----------------------*/
int pres_game_index(unsigned int release, const char *serial);

/*----------------------
 | pres_of_room
 | Description: The authored presentation for one room. Fills *out and returns 1
 |   only when the room carries a picture; a room with no entry leaves *out
 |   untouched and returns 0, which every caller reads as "hold what is showing"
 |   rather than "show nothing".
 | Author: suinevere
 | Dependencies: game_presentation.inc
 | Globals: GAME_PRES_MAP
 | Params: release, serial -- the story identity; obj -- the room's object
 |   number; out -- filled on success
 | Returns: 1 when the room is authored, 0 otherwise
 ----------------------*/
int pres_of_room(unsigned int release, const char *serial, unsigned int obj,
                 Presentation *out);

/*----------------------
 | pres_frame
 | Description: Where one image's record lies inside its archive. The offset is
 |   generated rather than walked because a CGL record's end is only discovered
 |   by decompressing it, so reaching a late frame by walking would cost every
 |   earlier frame in the archive. Any of the outputs may be null.
 | Author: suinevere
 | Dependencies: game_presentation.inc
 | Globals: IMAGE_FRAME
 | Params: image -- 1-based image index; area, offset, length -- filled on
 |   success when non-null
 | Returns: 1 on success, 0 when image is out of range
 ----------------------*/
int pres_frame(int image, int *area, unsigned long *offset, unsigned long *length);

/*----------------------
 | pres_area_name
 | Description: The 4-character archive stem for one area, for building the disc
 |   path.
 | Author: suinevere
 | Dependencies: game_presentation.inc
 | Globals: PRES_AREA
 | Params: area -- 0..PRES_AREA_N-1
 | Returns: the stem, or NULL when area is out of range
 ----------------------*/
const char *pres_area_name(int area);

#ifdef __cplusplus
}
#endif
#endif /* PRESENTATION_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/scene/presentation.c`:

```c
/*----------------------
 | presentation.c
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: string.h, presentation.h
 | Globals: GAME_PRES_MAP, IMAGE_FRAME, PRES_AREA
 ----------------------*/
#include <string.h>
#include "presentation.h"

/*----------------------
 | pres_game_index
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_PRES_MAP
 | Params: release, serial -- the story identity
 | Returns: the row index, or -1
 ----------------------*/
int pres_game_index(unsigned int release, const char *serial) {
    int i;
    if (serial == 0) return -1;
    for (i = 0; i < PRES_GAME_N; i++) {
        const GamePresMap *g = &GAME_PRES_MAP[i];
        if (g->release == release && memcmp(g->serial, serial, 6) == 0) return i;
    }
    return -1;
}

/*----------------------
 | pres_of_room
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GAME_PRES_MAP
 | Params: release, serial, obj, out -- see presentation.h
 | Returns: 1 when the room is authored, 0 otherwise
 ----------------------*/
int pres_of_room(unsigned int release, const char *serial, unsigned int obj,
                 Presentation *out) {
    int g = pres_game_index(release, serial);
    if (g < 0 || obj >= 256 || out == 0) return 0;
    {
        const Presentation *p = &GAME_PRES_MAP[g].rooms[obj];
        if (p->image == 0) return 0;
        *out = *p;
        return 1;
    }
}

/*----------------------
 | pres_frame
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: IMAGE_FRAME
 | Params: image, area, offset, length -- see presentation.h
 | Returns: 1 on success, 0 when image is out of range
 ----------------------*/
int pres_frame(int image, int *area, unsigned long *offset, unsigned long *length) {
    if (image < 1 || image > PRES_FRAME_N) return 0;
    if (area)   *area   = (int) IMAGE_FRAME[image - 1].area;
    if (offset) *offset = IMAGE_FRAME[image - 1].offset;
    if (length) *length = IMAGE_FRAME[image - 1].length;
    return 1;
}

/*----------------------
 | pres_area_name
 | Description: See presentation.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: PRES_AREA
 | Params: area -- 0..PRES_AREA_N-1
 | Returns: the stem, or NULL
 ----------------------*/
const char *pres_area_name(int area) {
    if (area < 0 || area >= PRES_AREA_N) return 0;
    return PRES_AREA[area];
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tpres \
    saturn/tests/test_presentation.c saturn/src/scene/presentation.c && /tmp/tpres
```

Expected: `ok`

- [ ] **Step 6: Verify SH-2 compilation in both configurations**

```bash
saturn/syntax-check.sh saturn/src/scene/presentation.c
NETBIN=1 saturn/syntax-check.sh saturn/src/scene/presentation.c
```

Expected: clean both times.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/scene/presentation.c saturn/src/scene/presentation.h \
        saturn/tests/test_presentation.c
git commit -m "Add the runtime lookup for the per-room presentation table, refusing an unknown story, an out-of-range object and an unauthored room alike so every caller sees one answer meaning hold what is showing."
```

---

### Task 5: Move the screen to 320x240

This is visible on its own and everything after it depends on the taller screen, so it lands before any picture code.

**Files:**
- Modify: `saturn/src/main.cxx:352-357`
- Modify: `saturn/src/video/title.cxx:319`
- Modify: `saturn/src/video/console_view.cxx:34`

**Interfaces:**
- Consumes: nothing.
- Produces: a 320x240 screen with 30 text rows, content on rows 1–29.

- [ ] **Step 1: Take the resolution to 240 and rewrite the comment that argued for 224**

In `saturn/src/main.cxx`, replace the comment and call at lines 352–357:

```c
    // 320x240, SRL's own NTSC default. The client used to narrow this to 224
    // because every layer it painted was 224 lines tall and the surplus showed
    // the back-plane as a band under everything. Zork I's backgrounds are
    // 320x240 on the original disc, so the surplus now carries picture, and the
    // text grid grew to meet it rather than leaving a band.
    SRL::Core::Initialize(HighColor::Colors::Black, SRL::TV::Resolutions::Normal320x240);
```

- [ ] **Step 2: Turn off index-0 transparency on the wallpaper layer**

In `saturn/src/main.cxx`, immediately after `border_use_black()`:

```c
    // The room backgrounds use all 256 CLUT entries, index 0 among them, and
    // VDP2 would otherwise punch that colour through to the back-plane. The
    // image window console_view aims at NBG0 is what still punches holes, and
    // it is unaffected by this.
    SRL::VDP2::NBG0::TransparentDisable();
```

- [ ] **Step 3: Grow the wallpaper cache slot to match**

In `saturn/src/video/title.cxx:319`:

```c
#define TGA_PLANE_MAX      (320u * 240u + 2048u)
```

A slot goes from 74.2 KB to 79.4 KB, so nine slots is 715 KB — still inside the 1 MB Low Work RAM with the 96 KB save floor.

- [ ] **Step 4: Grow the text grid**

In `saturn/src/video/console_view.cxx:34`:

```c
static const int SCREEN_ROWS = 30;
```

`TEXT_ROWS` in `text_map.h` is already 32 and NBG3's plane is 64x32 cells, so nothing else has to move for the rows to exist. `TOP_MARGIN` still reserves row 0 against overscan, so content runs 1–29 and the console gains two rows.

- [ ] **Step 5: Run every host C test that knows the geometry**

```bash
gcc -O2 -I saturn/src -o /tmp/tcon saturn/tests/test_console.c saturn/src/video/console.c && /tmp/tcon
gcc -O2 -I saturn/src -o /tmp/tdm  saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/tdm
gcc -O2 -I saturn/src -o /tmp/tml  saturn/tests/test_menu_layout.c saturn/src/menu/menu_layout.c && /tmp/tml
```

Expected: all `ok`. A failure names the renderer that assumed 28 rows; fix that renderer to derive from `console_height()` rather than hard-coding a row count.

- [ ] **Step 6: Build and look at it**

```bash
saturn/compile-cd.bat debug
saturn/run_with_mednafen.bat
```

Check by eye: no coloured band below the content; the title screen sits where it did; menus are not clipped; the bottom text row is fully on screen. If the bottom row is cut on a real set later, the fix is a bottom margin in `console_view.cxx` costing one of the two new rows back.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/main.cxx saturn/src/video/title.cxx saturn/src/video/console_view.cxx
git commit -m "Take the screen to 320x240, SRL's own NTSC default, so the original room backgrounds can be shown at their real size with no crop and no scale, growing the text grid to thirty rows to fill the sixteen lines that used to show the back-plane and telling the wallpaper layer to treat palette index 0 as opaque, which those backgrounds use as a colour."
```

---

### Task 6: A seam for showing an already-decoded picture

`title.cxx` owns NBG0 — the blit, the priority, the scroll enable and the record of what is loaded. The archive path must go through it rather than around it, or the fade, dim and pin machinery will disagree about what is on screen.

**Files:**
- Modify: `saturn/src/video/title.cxx`
- Modify: `saturn/src/video/title.h`

**Interfaces:**
- Consumes: `tga_blit_nbg0`'s `RawBitmap`, `nbg0_note_loaded` (both file-static in `title.cxx`).
- Produces: `bool title_bg_show_raw(const unsigned char *pixels, const unsigned short *clut, int w, int h, const char *tag);`

- [ ] **Step 1: Declare it**

Add to `saturn/src/video/title.h`, after `title_bg_show_oneoff`:

```c
/*----------------------
 | title_bg_show_raw
 | Description: Shows an already-decoded 8bpp picture on VDP2 NBG0, for callers
 |   that produced their pixels themselves rather than reading a TGA off the
 |   disc. Touches no CD, so it is safe to call with music playing -- which is
 |   the whole point for the room backgrounds, whose archive is already resident
 |   and whose per-room change must not interrupt a track.
 |
 |   tag is recorded as the loaded-file name so title_bg_loaded_file and the
 |   Dynamic pin's short-circuit keep working. It is a label, not a path, and
 |   nothing ever reopens it.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: pixels -- w*h 8bpp bytes; clut -- 256 Saturn RGB555 words; w, h --
 |   the picture's size; tag -- a name to record, truncated to the cache's name
 |   field
 | Returns: true if the picture was applied, false if an argument was bad or the
 |   palette could not be made
 ----------------------*/
bool title_bg_show_raw(const unsigned char *pixels, const unsigned short *clut,
                       int w, int h, const char *tag);
```

- [ ] **Step 2: Implement it**

Add to `saturn/src/video/title.cxx`, immediately after `title_bg_show_oneoff`:

```c
/*----------------------
 | title_bg_show_raw
 | Description: See title.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: pixels, clut, w, h, tag -- see title.h
 | Returns: true on success
 ----------------------*/
bool title_bg_show_raw(const unsigned char *pixels, const unsigned short *clut,
                       int w, int h, const char *tag) {
    if (pixels == nullptr || clut == nullptr || w <= 0 || h <= 0) return false;

    SRL::Types::HighColor *colors = new SRL::Types::HighColor[256];
    if (colors == nullptr) return false;
    for (int i = 0; i < 256; i++) colors[i] = SRL::Types::HighColor(clut[i]);

    RawBitmap bmp;
    bmp.Pixels = const_cast<uint8_t *>(pixels);
    bmp.W      = (uint16_t) w;
    bmp.H      = (uint16_t) h;
    bmp.Pal    = new SRL::Bitmap::Palette(colors, 256);
    if (bmp.Pal == nullptr) { delete[] colors; return false; }

    SRL::VDP2::NBG0::LoadBitmap(&bmp);
    SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);
    nbg0_note_loaded(tag ? tag : "");
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}
```

- [ ] **Step 3: Verify SH-2 compilation**

```bash
saturn/syntax-check.sh saturn/src/video/title.cxx
```

Expected: clean, zero warnings.

- [ ] **Step 4: Build to confirm nothing else regressed**

```bash
saturn/compile-cd.bat debug
```

Expected: success. Nothing calls the new function yet, so behaviour is unchanged.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/video/title.cxx saturn/src/video/title.h
git commit -m "Give title.cxx a way to put an already-decoded picture on the wallpaper layer, so the room-background path goes through the one module that owns NBG0's blit, priority, scroll and loaded-name record rather than reaching around it and leaving the fade and pin machinery disagreeing about what is on screen."
```

---

### Task 7: The resident-area loader, and the picture on screen

**Files:**
- Create: `saturn/src/video/room_art.cxx`
- Create: `saturn/src/video/room_art.h`
- Modify: `saturn/src/sound/music.h`, `saturn/src/sound/music.c`
- Modify: `saturn/src/main.cxx`

**Interfaces:**
- Consumes: `pres_of_room`, `pres_frame`, `pres_area_name` (Task 4); `cgl_decode`, `cgl_palette` (Task 1); `title_bg_show_raw`, `cd_enter_root`, `cd_restore_z3` (Task 6 and `title.h`).
- Produces:
  - `void room_art_set_game(unsigned int release, const char *serial);`
  - `int  room_art_available(void);`
  - `int  room_art_needs_disc(unsigned int obj);`
  - `int  room_art_show(unsigned int obj);`
  - `void room_art_release(void);`
  - `void music_set_room_fn(void (*fn)(unsigned int obj));`

**Design note — the game identity is set once, not passed per call.** `on_text_room` runs in `main.cxx`, which does not hold the story's release and serial — those are `music.c` statics. Rather than widen the callback or duplicate the identity in `main.cxx`, `room_art` is told the game once when it is selected, the same way `music_set_game` already works.

**Design note — why the picture changes without a fade.** The existing art path is announced from `commit_pending`, at the bottom of a transition fade, because it may read the disc and a disc read stops CD-DA. On this path a room change inside an area reads nothing: the archive is resident, the cost is one LZSS decode of 76.8 KB, and the only visible moment is the blit. So a room change inside an area is a **cut**, taken immediately, and only an area change — which is also a track change, and the one disc read — rides the existing fade. `room_art_needs_disc` exists so a caller can tell the two apart before deciding.

- [ ] **Step 1: Write the header**

Create `saturn/src/video/room_art.h`:

```c
/*----------------------
 | room_art.h
 | Description: The room backgrounds' hardware half: which area archive is
 |   resident, reading the next one off the disc, decompressing one frame and
 |   putting it on NBG0. The decoding is in cgl.c and the data in
 |   scene/presentation.h; this is only the policy and the SRL calls.
 |
 |   One archive is resident at a time. All eleven are 2.0 MB together and Low
 |   Work RAM is 1 MB, so holding more is not on the table; the largest single
 |   archive is 408.5 KB, which with the 76.8 KB decode target and the palette
 |   peaks around 486 KB. That only fits because title_bg_cache_release() drops
 |   the nine TGA cache slots when a game with authored art starts -- the two
 |   art paths never hold memory at the same time.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef ROOM_ART_H
#define ROOM_ART_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | room_art_set_game
 | Description: Tells the loader which story is running, once, when it is
 |   selected. Held rather than passed per call because the room subscriber runs
 |   where the story identity is not in scope. Passing a story with no authored
 |   art is how the loader is turned off again.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_release, g_serial, g_have_game
 | Params: release -- Z-machine release; serial -- 6-char serial
 | Returns: N/A
 ----------------------*/
void room_art_set_game(unsigned int release, const char *serial);

/*----------------------
 | room_art_available
 | Description: Whether the story set by room_art_set_game has authored room
 |   art. The one call that decides whether a game takes this path or the scene
 |   path.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_have_game
 | Params: N/A
 | Returns: 1 when the story has a presentation table, 0 otherwise
 ----------------------*/
int room_art_available(void);

/*----------------------
 | room_art_needs_disc
 | Description: Whether showing this room would have to read the disc, which
 |   stops CD-DA. True exactly when the room's frame lives in an archive other
 |   than the resident one. The caller uses this to decide whether to fade: a
 |   room change inside an area is a cut, an area change is a fade.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_area, g_release, g_serial
 | Params: obj -- the room's object number
 | Returns: 1 when a disc read is required, 0 otherwise
 ----------------------*/
int room_art_needs_disc(unsigned int obj);

/*----------------------
 | room_art_show
 | Description: Puts one room's original background on NBG0, reading its area
 |   archive first if a different one is resident. Every failure -- no game set,
 |   an unauthored room, an archive that will not open, a read that comes up
 |   short, a stream that will not decode -- holds the picture already showing
 |   and says nothing on screen. Art is decoration; a failed load must never be
 |   able to blank the screen or stop the game.
 |
 |   Restores the CD to the story directory before returning whenever it stepped
 |   out of it, which is the obligation every post-selection detour owes.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, scene/presentation.h, title.h
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_clut
 | Params: obj -- the room's object number
 | Returns: 1 when a new picture was applied, 0 when nothing changed
 ----------------------*/
int room_art_show(unsigned int obj);

/*----------------------
 | room_art_release
 | Description: Frees the resident archive and the decode target and forgets the
 |   game, for leaving back to the menus where the TGA cache wants the memory
 |   again.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_have_game
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_art_release(void);

#ifdef __cplusplus
}
#endif
#endif /* ROOM_ART_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/video/room_art.cxx`:

```c
/*----------------------
 | room_art.cxx
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, room_art.h, title.h, scene/presentation.h
 | Globals: g_release, g_serial, g_have_game, g_area, g_archive, g_archive_len,
 |   g_pixels, g_clut
 ----------------------*/
#include <srl.hpp>
#include "video/cgl.h"
#include "video/room_art.h"
#include "video/title.h"
#include "scene/presentation.h"

/*----------------------
 | g_release / g_serial / g_have_game
 | Description: The running story's identity, and whether it carries authored
 |   art at all. Held here so the room subscriber does not have to.
 | Author: suinevere
 ----------------------*/
static unsigned int g_release = 0;
static char         g_serial[7] = { 0 };
static bool         g_have_game = false;

/*----------------------
 | g_area / g_archive / g_archive_len
 | Description: The resident area (-1 when none), its bytes and their length.
 |   Plain statics, so a soft reset returns to what the longjmp left intact --
 |   the same property title.cxx's cache relies on.
 | Author: suinevere
 ----------------------*/
static int            g_area = -1;
static unsigned char *g_archive = nullptr;
static unsigned long  g_archive_len = 0;

/*----------------------
 | g_pixels / g_clut
 | Description: The decode target and the palette the current frame produced.
 |   One target, reused: only one picture is ever on screen, and a second buffer
 |   would cost 76.8 KB the biggest archive needs.
 | Author: suinevere
 ----------------------*/
static unsigned char  *g_pixels = nullptr;
static unsigned short  g_clut[256];

/*----------------------
 | ART_DIR
 | Description: The disc directory holding the area archives.
 | Author: suinevere
 ----------------------*/
#define ART_DIR "BG"

/*----------------------
 | frame_of
 | Description: Resolves one room to its frame: the area, and where the record
 |   lies inside that area's archive.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_have_game, g_release, g_serial
 | Params: obj -- the room's object number; area, offset, length -- filled on
 |   success
 | Returns: true when the room is authored
 ----------------------*/
static bool frame_of(unsigned int obj, int *area,
                     unsigned long *offset, unsigned long *length) {
    Presentation p;
    if (!g_have_game) return false;
    if (!pres_of_room(g_release, g_serial, obj, &p)) return false;
    return pres_frame((int) p.image, area, offset, length) == 1;
}

/*----------------------
 | load_area
 | Description: Reads one area's archive into Low Work RAM, replacing whatever
 |   was resident. Leaves g_area at -1 and frees nothing new on any failure, so
 |   a failed read cannot leave a half-loaded archive claiming to be an area.
 | Author: suinevere
 | Dependencies: SRL, title.h (cd_enter_root, cd_restore_z3)
 | Globals: g_area, g_archive, g_archive_len
 | Params: area -- the area index to make resident
 | Returns: true when the archive is resident
 ----------------------*/
static bool load_area(int area) {
    const char *stem = pres_area_name(area);
    char name[16];
    int i = 0;

    if (stem == nullptr) return false;

    if (g_archive != nullptr) {
        SRL::Memory::LowWorkRam::Free(g_archive);
        g_archive = nullptr;
        g_archive_len = 0;
    }
    g_area = -1;

    while (stem[i] != '\0' && i < 8) { name[i] = stem[i]; i++; }
    name[i++] = '.'; name[i++] = 'C'; name[i++] = 'G'; name[i++] = 'L';
    name[i] = '\0';

    cd_enter_root();
    if (SRL::Cd::ChangeDir(ART_DIR) < 0) { cd_restore_z3(); return false; }

    {
        SRL::Cd::File f(name);
        if (!f.Exists()) { cd_restore_z3(); return false; }
        {
            const uint32_t bytes = (uint32_t) f.Size.Bytes;
            if (bytes == 0 || SRL::Memory::LowWorkRam::GetFreeSpace()
                              < bytes + CGL_FRAME_BYTES + 4096) {
                cd_restore_z3();
                return false;
            }
            g_archive = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(bytes);
            if (g_archive == nullptr) { cd_restore_z3(); return false; }
            if (f.LoadBytes(0, (int32_t) bytes, g_archive) <= 0) {
                SRL::Memory::LowWorkRam::Free(g_archive);
                g_archive = nullptr;
                cd_restore_z3();
                return false;
            }
            g_archive_len = bytes;
            g_area = area;
        }
    }
    cd_restore_z3();
    return true;
}

/*----------------------
 | room_art_set_game
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_release, g_serial, g_have_game
 | Params: release, serial -- the story identity
 | Returns: N/A
 ----------------------*/
void room_art_set_game(unsigned int release, const char *serial) {
    int i;
    g_release = release;
    for (i = 0; i < 6; i++) g_serial[i] = serial ? serial[i] : '\0';
    g_serial[6] = '\0';
    g_have_game = (serial != nullptr) && (pres_game_index(release, g_serial) >= 0);
}

/*----------------------
 | room_art_available
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_have_game
 | Params: N/A
 | Returns: 1 when the story has authored art
 ----------------------*/
int room_art_available(void) { return g_have_game ? 1 : 0; }

/*----------------------
 | room_art_needs_disc
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_area
 | Params: obj -- the room's object number
 | Returns: 1 when a disc read is required
 ----------------------*/
int room_art_needs_disc(unsigned int obj) {
    int area;
    unsigned long off, len;
    if (!frame_of(obj, &area, &off, &len)) return 0;
    return (area == g_area) ? 0 : 1;
}

/*----------------------
 | room_art_show
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, title.h
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_clut
 | Params: obj -- the room's object number
 | Returns: 1 when a new picture was applied
 ----------------------*/
int room_art_show(unsigned int obj) {
    int area;
    unsigned long off, len;

    if (!frame_of(obj, &area, &off, &len)) return 0;
    if (area != g_area && !load_area(area)) return 0;
    if (g_archive == nullptr || off + len > g_archive_len) return 0;

    if (g_pixels == nullptr) {
        g_pixels = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(CGL_FRAME_BYTES);
        if (g_pixels == nullptr) return 0;
    }

    cgl_palette(g_archive + off, g_clut);
    if (cgl_decode(g_archive + off, len, g_pixels, CGL_FRAME_BYTES)
        != (unsigned long) CGL_FRAME_BYTES) return 0;

    return title_bg_show_raw(g_pixels, g_clut, CGL_WIDTH, CGL_HEIGHT,
                             pres_area_name(area)) ? 1 : 0;
}

/*----------------------
 | room_art_release
 | Description: See room_art.h.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_have_game
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_art_release(void) {
    if (g_archive != nullptr) SRL::Memory::LowWorkRam::Free(g_archive);
    if (g_pixels != nullptr)  SRL::Memory::LowWorkRam::Free(g_pixels);
    g_archive = nullptr;
    g_pixels = nullptr;
    g_archive_len = 0;
    g_area = -1;
    g_have_game = false;
}
```

- [ ] **Step 3: Add the room subscriber to music.h**

In `saturn/src/sound/music.h`, beside `music_set_category_fn`:

```c
/*----------------------
 | music_set_room_fn
 | Description: Subscribes to every room change, for stories with an authored
 |   per-room presentation. set_category_fn cannot serve this: on that path the
 |   category is the track, so two rooms sharing a track are one category and
 |   the picture would never change between them. The picture needs the room,
 |   which is what this hands over.
 |
 |   Fired on the room change itself rather than at the debounced commit,
 |   because the picture must not lag the text -- the area's archive is resident
 |   and the change costs a decompress, not a disc read.
 | Author: suinevere
 ----------------------*/
void music_set_room_fn(void (*fn)(unsigned int obj));
```

- [ ] **Step 4: Fire it from music.c**

Add the storage and setter beside `g_cat_fn` in `saturn/src/sound/music.c`:

```c
/*----------------------
 | g_room_fn / music_set_room_fn
 | Description: The per-room subscriber. Separate from g_cat_fn because a
 |   category on the authored path is a track, and rooms sharing a track share a
 |   category while needing different pictures.
 | Author: suinevere
 ----------------------*/
static void (*g_room_fn)(unsigned int) = 0;
void music_set_room_fn(void (*fn)(unsigned int obj)) { g_room_fn = fn; }
```

and fire it inside `music_on_turn`'s existing `if (room_changed)` block, after `g_cur_room` is updated:

```c
        if (g_room_fn) g_room_fn(obj);
```

- [ ] **Step 5: Wire it in main.cxx**

Add `on_text_room` beside `on_text_category`:

```c
/*----------------------
 | on_text_room
 | Description: The authored art's half of a room change, for stories that carry
 |   a per-room presentation. Unlike on_text_category this fires on every room,
 |   because every room has its own picture, and it takes that picture
 |   immediately rather than at the bottom of a fade: within an area the
 |   archive is already resident, so the change costs a decompress and touches
 |   no CD.
 |
 |   An area change does read the disc. room_art_show performs the read itself
 |   and the fade around it is the music's own, since an area change is also a
 |   track change.
 | Author: suinevere
 | Dependencies: room_art.h, options.h
 | Globals: g_display
 | Params: obj -- the room's object number
 | Returns: N/A
 ----------------------*/
static void on_text_room(unsigned int obj) {
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    room_art_show(obj);
}
```

Then, where `music_set_category_fn` is registered, add `music_set_room_fn(on_text_room);`. Where the story's release and serial become known at game start, add:

```c
    room_art_set_game(release, serial);
    if (room_art_available()) title_bg_cache_release();
```

and call `room_art_release()` on the way back out to the menus, beside whatever already tears the game down.

- [ ] **Step 6: Verify SH-2 compilation**

```bash
saturn/syntax-check.sh saturn/src/video/room_art.cxx saturn/src/sound/music.c saturn/src/main.cxx
NETBIN=1 saturn/syntax-check.sh saturn/src/sound/music.c
```

Expected: clean. `room_art.cxx` is deliberately not checked under NETBIN — it must not be in that build at all.

- [ ] **Step 7: Confirm the netbin did not take the new files**

```bash
tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_netbin_sources.py -v
```

Expected: passes unchanged. A failure means `room_art.cxx` or `cgl.c` was added to the netbin `SOURCES` list — take it out; that build has no disc.

- [ ] **Step 8: Build and look at it**

```bash
saturn/compile-cd.bat debug
saturn/run_with_mednafen.bat
```

Start Zork I and walk West of House → North of House → Forest Path → up a tree. Check: the picture changes on every move; there is no stall or flicker between rooms; the music does not restart within the area; going into the house and down to the cellar reads the disc once and continues.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/video/room_art.cxx saturn/src/video/room_art.h \
        saturn/src/sound/music.c saturn/src/sound/music.h saturn/src/main.cxx
git commit -m "Show each Zork I room's original background by holding its area's archive resident and decompressing one frame per room, which costs no disc access while the player stays inside an area and so lets the picture change on every move without interrupting the music, and give the music engine a room subscriber because the category on this path is the track and two rooms sharing a track would otherwise share a picture."
```

---

### Task 8: Authentic music

**Files:**
- Modify: `saturn/src/sound/music.c`
- Create: `saturn/tests/test_music_presentation.c`

**Interfaces:**
- Consumes: `pres_of_room` (Task 4), `music_set_room_fn` (Task 7).
- Produces: `CAT_KIND_ROOM` behaviour inside `MIX_DYNAMIC`.

**Design note — the category is the track.** On this path `g_base_cat` holds the room's *track number*, not a scene index. Two rooms with the same track are therefore the same category, and the engine's existing "target unchanged, do nothing" branch is what stops the music restarting on every step — no new comparison is needed. Track 0 is a legitimate category meaning silence, and `play_dyn(0, …)` already stops the drive.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_music_presentation.c`. Read `saturn/tests/test_music_scene.c` first and reuse its backend-stub shape:

```c
/*----------------------
 | test_music_presentation.c
 | Description: Dynamic mode over a story with an authored per-room table.
 |   Run from the repository root:
 |   gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tmpres \
 |       saturn/tests/test_music_presentation.c saturn/src/sound/music.c \
 |       saturn/src/scene/scene_map.c saturn/src/scene/presentation.c \
 |       saturn/src/sound/event_scan.c saturn/src/sound/music_data.c && /tmp/tmpres
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include "sound/music.h"
#include "scene/presentation.h"

static int fails = 0;
static int g_issued[64];
static int g_issued_n = 0;
static unsigned int g_last_room = 0;
static int g_room_calls = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static void play_stub(int track, int loop) {
    (void) loop;
    if (g_issued_n < 64) g_issued[g_issued_n++] = track;
}

static void room_stub(unsigned int obj) { g_last_room = obj; g_room_calls++; }

static int obj_titled(const char *want_track_room);

static void settle(void) {
    int i;
    for (i = 0; i < 8; i++) music_tick();
}

static void reset_all(void) {
    g_issued_n = 0; g_room_calls = 0;
    music_reset();
    music_set_backend(play_stub);
    music_set_room_fn(room_stub);
    music_set_debounce_frames(0);
    music_set_mix(MIX_DYNAMIC, 0);
    music_set_game(88, "840726");
}

int main(void) {
    Presentation p;
    unsigned int a = 0, b = 0, silent = 0, other = 0;
    unsigned int obj;

    for (obj = 0; obj < 256; obj++) {
        if (!pres_of_room(88, "840726", obj, &p)) continue;
        if (p.track == 0) { if (!silent) silent = obj; continue; }
        if (!a) { a = obj; continue; }
        if (!b) {
            Presentation q;
            pres_of_room(88, "840726", a, &q);
            if (p.track == q.track) { b = obj; continue; }
        }
        if (!other) {
            Presentation q;
            pres_of_room(88, "840726", a, &q);
            if (p.track != q.track) other = obj;
        }
    }
    check(a && b && silent && other, "the table offers the four rooms this needs");

    reset_all();
    music_on_turn(a);
    settle();
    check(g_issued_n == 1, "entering the first room issues one track");
    check(g_room_calls == 1, "the room subscriber fired once");
    check(g_last_room == a, "the room subscriber was told which room");

    music_on_turn(b);
    settle();
    check(g_issued_n == 1, "a room sharing the track does not re-issue it");
    check(g_room_calls == 2, "but the picture is still told to change");

    music_on_turn(other);
    settle();
    check(g_issued_n == 2, "a room with a different track issues it");

    music_on_turn(silent);
    settle();
    check(g_issued_n == 3 && g_issued[2] == 0, "a silent room stops the drive");

    reset_all();
    music_on_turn(a);
    settle();
    music_on_turn(b);
    settle();
    music_on_turn(a);
    settle();
    music_on_turn(b);
    settle();
    check(g_issued_n == 1, "walking a whole area never rotates off its track");

    reset_all();
    music_set_game(999, "000000");
    music_on_turn(a);
    settle();
    check(g_room_calls >= 1, "a story with no table still announces rooms");

    printf(fails ? "%d FAILED\n" : "ok\n", fails);
    return fails ? 1 : 0;
}

static int obj_titled(const char *want) { (void) want; return 0; }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tmpres \
    saturn/tests/test_music_presentation.c saturn/src/sound/music.c \
    saturn/src/scene/scene_map.c saturn/src/scene/presentation.c \
    saturn/src/sound/event_scan.c saturn/src/sound/music_data.c && /tmp/tmpres
```

Expected: FAIL — the room's track is not chosen.

- [ ] **Step 3: Add the room kind and the base-kind state**

In `saturn/src/sound/music.c`, add `CAT_KIND_ROOM` to the existing kind enum and a static beside `g_base_cat`:

```c
/*----------------------
 | CAT_KIND_ROOM / g_base_kind
 | Description: The kind of the room's own category. On CAT_KIND_SCENE the
 |   category is an SC_* index and a pool supplies the track; on CAT_KIND_ROOM it
 |   IS the track, taken from the story's authored table. Making the track the
 |   category is what lets the existing unchanged-target branch stop the music
 |   restarting between two rooms that share one.
 | Author: suinevere
 ----------------------*/
static int g_base_kind = CAT_KIND_SCENE;
```

- [ ] **Step 4: Resolve the room's track in music_on_turn**

Replace the body of the `room_changed` block so the authored table wins where it exists:

```c
    if (room_changed) {
        Presentation p;
        int base;
        if (pres_of_room(g_release, g_serial, obj, &p)) {
            base = (int) p.track;
            g_base_kind = CAT_KIND_ROOM;
        } else {
            base = scene_of_room(g_release, g_serial, obj);
            g_base_kind = CAT_KIND_SCENE;
        }
        g_cur_room = obj; g_have_room = 1; g_base_cat = base; g_event_cat = -1;
        if (g_room_fn) g_room_fn(obj);
    }
```

and take `target_kind`'s scene branch from `g_base_kind` rather than the literal:

```c
    int target_kind = (g_event_cat >= 0) ? CAT_KIND_EVENT
                     : (g_base_cat  >= 0) ? g_base_kind : CAT_KIND_NONE;
```

Add `#include "scene/presentation.h"` beside the existing `scene/scene_map.h` include, and name it in the file header's Dependencies line.

- [ ] **Step 5: Make the track pick trivial for the room kind**

At the top of `pick_dynamic_track`:

```c
    if (kind == CAT_KIND_ROOM) return cat;
```

The table already named the track, so there is nothing to choose.

- [ ] **Step 6: Disable rotation on the room path**

Guard the rotation branch so an authored score is never treated as a pool to relieve:

```c
            } else if (g_active_kind != CAT_KIND_ROOM
                       && g_same_cat_rooms >= MUSIC_ROTATE_ROOMS) {
```

- [ ] **Step 7: Return to the room's track when a sting ends**

In `music_tick`'s `MIX_DYNAMIC` end-of-track branch:

```c
        } else if (g_mix_mode == MIX_DYNAMIC) {
            if (g_active_kind == CAT_KIND_EVENT && g_base_kind == CAT_KIND_ROOM) {
                g_event_cat = -1;
                g_active_kind = CAT_KIND_ROOM;
                g_active_cat = g_base_cat;
                play_dyn(g_base_cat, 1);
            } else if (g_dyn_pass < MUSIC_DYN_LOOPS) {
                play_dyn(g_active_track, g_dyn_pass + 1);
            } else {
                play_dyn(pick_dynamic_track(g_active_kind, g_active_cat), 1);
            }
        }
```

- [ ] **Step 8: Run the test to verify it passes**

```bash
gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/tmpres \
    saturn/tests/test_music_presentation.c saturn/src/sound/music.c \
    saturn/src/scene/scene_map.c saturn/src/scene/presentation.c \
    saturn/src/sound/event_scan.c saturn/src/sound/music_data.c && /tmp/tmpres
```

Expected: `ok`

- [ ] **Step 9: Run the existing music tests to prove other games are untouched**

```bash
for t in music_scene music_static music_pause; do
  gcc -O2 -I saturn/src -I saturn/src/scene -o "/tmp/t_$t" \
      "saturn/tests/test_$t.c" saturn/src/sound/music.c \
      saturn/src/scene/scene_map.c saturn/src/scene/presentation.c \
      saturn/src/sound/event_scan.c saturn/src/sound/music_data.c \
    && "/tmp/t_$t" || echo "FAILED $t"
done
```

Expected: three `ok` lines.

- [ ] **Step 10: Build and listen**

```bash
saturn/compile-cd.bat debug
saturn/run_with_mednafen.bat
```

Walk the above-ground rooms: the track must play continuously across West of House, North of House and Forest Path without restarting. Stone Barrow: silence. Go below ground: the track changes once, at the area boundary.

- [ ] **Step 11: Commit**

```bash
git add saturn/src/sound/music.c saturn/tests/test_music_presentation.c
git commit -m "Let Dynamic play Zork I's own score by making the room's authored track the category itself, so two rooms sharing a track are one category and the existing unchanged-target branch is what keeps the music from restarting on every step, with rotation disabled because an authored score is not a pool and a sting returning to the room's track rather than picking again inside the event pool."
```

---

### Task 9: Dark rooms draw black

The intent is settled; the signal is not. This task starts with an investigation whose outcome decides whether the rest of it ships.

**Files:**
- Modify: `saturn/src/engine/room_model.c`, `saturn/src/engine/room_model.h`
- Modify: `saturn/src/video/room_art.cxx`
- Modify: `saturn/tests/test_room_model.c`

**Interfaces:**
- Consumes: `room_art_show` (Task 7).
- Produces: `int room_model_is_lit(void);`

- [ ] **Step 1: Find out whether darkness is reachable**

```bash
grep -n "ONBIT\|RLANDBIT\|LIT" \
  "cd/Zork I - The Great Underground Empire (Japan)/zork1/gglobals.zil" | head -20
grep -n "attr\|attribute\|obj_attr\|test_attr" saturn/src/engine/room_model.c | head -20
grep -n "attr" saturn/src/engine/mojozork_saturn.c | head -20
```

Answer in writing, in the commit message:

- Does the interpreter already track a lit/unlit state per turn, or only the Z-code?
- In Zork I release 88, which attribute number is `ONBIT`, and is it set on the *room* object when lit?
- Can that attribute be read through the object-table access `room_model` already performs, without a second traversal?

**If darkness cannot be read reliably, stop here.** Record what was found, leave the picture showing, and let this ship as its own cycle. A darkness signal wrong in the false-positive direction blanks the screen during ordinary play, which is far worse than the fidelity it buys.

- [ ] **Step 2: Write the failing test**

Add to `saturn/tests/test_room_model.c`, using the fixture pattern already in that file:

```c
    check(room_model_is_lit() == 1, "a lit room reports lit");
    /* Re-seed the fixture with the room's light attribute cleared. */
    check(room_model_is_lit() == 0, "an unlit room reports unlit");
```

- [ ] **Step 3: Run it to verify it fails**

```bash
gcc -O2 -I saturn/src -o /tmp/trm saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm
```

Expected: FAIL — `room_model_is_lit` is not declared.

- [ ] **Step 4: Add the accessor**

Declare it in `room_model.h` and implement it in `room_model.c`, both with the project's header block, reading the attribute through the object-table access already performed. Its header must name the attribute number it reads and say that a story whose light state cannot be determined reports lit, because reporting unlit wrongly blanks the screen.

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -O2 -I saturn/src -o /tmp/trm saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/trm
```

Expected: `ok`

- [ ] **Step 6: Draw black when unlit**

In `room_art_show`, before the decode:

```c
    if (!room_model_is_lit()) {
        int i;
        for (i = 0; i < 256; i++) g_clut[i] = 0x8000u;
        if (g_pixels == nullptr) {
            g_pixels = (unsigned char *) SRL::Memory::LowWorkRam::Malloc(CGL_FRAME_BYTES);
            if (g_pixels == nullptr) return 0;
        }
        for (i = 0; i < CGL_FRAME_BYTES; i++) g_pixels[i] = 0;
        return title_bg_show_raw(g_pixels, g_clut, CGL_WIDTH, CGL_HEIGHT, "DARK") ? 1 : 0;
    }
```

An all-black palette rather than a black fill through the room's own palette: the room's index 0 is not guaranteed black, and this keeps one code path reaching NBG0.

- [ ] **Step 7: Verify on hardware**

```bash
saturn/compile-cd.bat debug
saturn/run_with_mednafen.bat
```

Go down to the Cellar without the lamp: black. Turn the lamp on: the Cellar's picture appears. Walk back up: the picture returns with no stale black frame.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/engine/room_model.c saturn/src/engine/room_model.h \
        saturn/src/video/room_art.cxx saturn/tests/test_room_model.c
git commit -m "Draw black instead of the room's picture when the room is unlit, reading the light attribute through the object-table access the room model already performs and defaulting to lit whenever it cannot be determined, since showing a fully rendered cave the player is being told they cannot see is the one place the original art fights the game's own fiction."
```

---

### Task 10: Retire what this supersedes

The scene-tagged art handoff still asks the owner to bless Zork I's 110 rooms and source art for its thirteen scenes. Both are discharged for this game by measurement, and a memory that disagrees with the code is worse than no memory.

**Files:**
- Modify: `mem/2026-08-22-scene-tagged-art-handoff.md`
- Modify: `mem/MEMORY.md`
- Create: `mem/2026-08-30-zork1-authentic-presentation-handoff.md`

- [ ] **Step 1: Mark the superseded half**

At the top of `mem/2026-08-22-scene-tagged-art-handoff.md`, under the existing staleness note, add a line saying its two Zork I owner tasks — "Bless Zork I" and "Source its art" — are discharged as of this work, that both still stand for the other thirty games, and linking `[[zork1-authentic-presentation-handoff]]`.

- [ ] **Step 2: Write the handoff**

Create `mem/2026-08-30-zork1-authentic-presentation-handoff.md` with the frontmatter the other entries use (`name`, `description`, `metadata.type: project`). It must record what the spec and plan do not: which commits landed, whether the owner has built and run it, the two alias rows that were confirmed by hand and how, the measured decode time per room if it was taken, and the sub-projects still open (item pictures, sound effects, the seven unattributed tracks, the ending art). Reference the spec and plan by path rather than restating them. Link `[[scene-tagged-art-handoff]]`.

- [ ] **Step 3: Add the pointer**

Add a one-line entry to `mem/MEMORY.md` in the same shape as the others.

- [ ] **Step 4: Commit**

```bash
git add mem/
git commit -m "Record the Zork I authentic-presentation handoff and mark the scene-tagged art handoff's two Zork I owner tasks discharged, since the authored table answers by measurement the question those tasks asked the owner to answer by judgement."
```

---

## Final verification

- [ ] Every host C test passes:

```bash
gcc -O2 -I saturn/src -o /tmp/v1 saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/v1
gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/v2 saturn/tests/test_presentation.c saturn/src/scene/presentation.c && /tmp/v2
gcc -O2 -I saturn/src -o /tmp/v3 saturn/tests/test_console.c saturn/src/video/console.c && /tmp/v3
gcc -O2 -I saturn/src -o /tmp/v4 saturn/tests/test_dash_map.c saturn/src/video/dash_map.c && /tmp/v4
gcc -O2 -I saturn/src -o /tmp/v5 saturn/tests/test_menu_layout.c saturn/src/menu/menu_layout.c && /tmp/v5
gcc -O2 -I saturn/src -o /tmp/v6 saturn/tests/test_room_model.c saturn/src/engine/room_model.c && /tmp/v6
gcc -O2 -I saturn/src -I saturn/src/scene -o /tmp/v7 saturn/tests/test_music_presentation.c saturn/src/sound/music.c saturn/src/scene/scene_map.c saturn/src/scene/presentation.c saturn/src/sound/event_scan.c saturn/src/sound/music_data.c && /tmp/v7
```

Expected: seven `ok` lines.

- [ ] The Python suite passes:

```bash
tools/.venv/Scripts/python.exe -m pytest tools/tests saturn/tests -q
```

- [ ] Both configurations build:

```bash
saturn/compile.bat debug
```

`compile.bat` builds the netbin first and the CD second, which is the order that leaves both artefacts valid.

- [ ] Generated files regenerate byte-identically:

```bash
tools/.venv/Scripts/python.exe tools/gen_presentation.py
tools/.venv/Scripts/python.exe tools/gen_cgl_fixture.py
tools/.venv/Scripts/python.exe tools/gen_scene_tables.py
git status --porcelain
```

Expected: no output.

- [ ] The netbin size gate is unmoved:

```bash
tools/.venv/Scripts/python.exe -m pytest saturn/tests/test_netbin_sources.py saturn/tests/test_netbin_lift.py -q
```

- [ ] A walkthrough in Mednafen or on hardware: above ground, into the house, down to the cellar, through the maze, out to the river, and into the coal mine. Pictures change on every room, music holds across an area and changes at boundaries, silence where it is authored, and nothing stutters between rooms.
