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
