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
import gen_emit  # noqa: E402
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
    gen_emit.write_if_changed(OUT_INC, "\n".join(lines) + "\n")

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
    gen_emit.write_if_changed(OUT_FIX, "\n".join(fix) + "\n")

    print(f"{OUT_INC.relative_to(ROOT)}: {len(recs)} records")
    print(f"{OUT_FIX.relative_to(ROOT)}: {PIC_N} pictures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
