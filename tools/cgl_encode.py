#!/usr/bin/env python3
"""/*----------------------
 | cgl_encode.py
 | Description: Writes B*.CGL records -- the other direction from
 |     analysis/zork_cgl.py, which has only ever read them.
 |
 |     A record is a 256-entry RGB555 little-endian CLUT, then a 4-byte
 |     little-endian decompressed length, then an Okumura LZSS stream, padded
 |     to a 4-byte boundary. The LZSS is the one saturn/src/video/cgl.c
 |     decodes: a 4096-byte ring initialised to zero with the write pointer at
 |     4078, flag bytes carrying eight codes LSB first, a set bit meaning one
 |     literal byte and a clear bit meaning two bytes holding a 12-bit ring
 |     offset and a length of three to eighteen.
 |
 |     Matches are verified by simulation before they are emitted. The decoder
 |     reads ring[(off + k) & 4095] while writing ring[r], so a match whose
 |     source overlaps its own destination reads bytes the copy is in the
 |     middle of writing -- that is how a run of one repeated byte compresses,
 |     and it means a match cannot be checked by comparing against the input
 |     alone. Every candidate here is run through a copy of the ring exactly as
 |     cgl.c would, and kept only if it reproduces the bytes it claims to.
 |     Slower, and the alternative is a stream that decodes to something else
 |     on the console and nowhere before it.
 | Author: suinevere
 | Dependencies: struct
 | Globals: RING, MAX_MATCH, MIN_MATCH, START, PAL_BYTES
 ----------------------*/"""
import struct

RING = 4096
MIN_MATCH = 3
MAX_MATCH = 18
START = RING - MAX_MATCH
PAL_BYTES = 512
"""RING / MIN_MATCH / MAX_MATCH / START / PAL_BYTES

Description: The LZSS geometry cgl.c decodes with, and the CLUT size that
    precedes each stream. START is where the ring's write pointer begins, which
    is what makes the first 4078 ring positions readable as zeros before
    anything has been written.
Author: suinevere
"""


def clut(palette):
    """/*----------------------
     | clut
     | Description: 256 RGB triples as the 512-byte RGB555 little-endian table
     |     a record opens with. A short palette is padded with black rather
     |     than refused -- a picture using 200 colours is a picture, and the
     |     entries past its end are never indexed.
     | Author: suinevere
     | Dependencies: struct
     | Globals: PAL_BYTES
     | Params: palette -- a sequence of (r, g, b) 0..255 triples
     | Returns: 512 bytes
     ----------------------*/"""
    out = bytearray()
    for i in range(256):
        r, g, b = palette[i] if i < len(palette) else (0, 0, 0)
        v = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)
        out += struct.pack("<H", v)
    return bytes(out)


def _copy_matches(ring, r, off, length, src, at):
    """/*----------------------
     | _copy_matches
     | Description: Whether the decoder, copying `length` bytes from ring
     |     offset `off`, would produce exactly src[at:at + length]. Run against
     |     a throwaway ring so an overlapping match is judged the way cgl.c
     |     would actually execute it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RING
     | Params: ring -- the current ring; r -- the write pointer; off -- the
     |     source offset; length -- how many bytes; src -- the input; at --
     |     where in the input the match is claimed
     | Returns: True when the copy reproduces the input
     ----------------------*/"""
    tmp = bytearray(ring)
    p = r
    for k in range(length):
        c = tmp[(off + k) & (RING - 1)]
        if c != src[at + k]:
            return False
        tmp[p] = c
        p = (p + 1) & (RING - 1)
    return True


def compress(src, candidates=48):
    """/*----------------------
     | compress
     | Description: One LZSS stream, without the length header. Greedy longest
     |     match, candidate positions found through a three-byte hash chain and
     |     capped so a byte repeated across a whole picture does not turn the
     |     search quadratic.
     |
     |     The cap costs compression and never correctness: a match that is not
     |     looked at is simply not taken, and the bytes go out as literals.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RING, MIN_MATCH, MAX_MATCH, START
     | Params: src -- the bytes to compress; candidates -- how many chain
     |     entries to try per position
     | Returns: the compressed stream
     ----------------------*/"""
    ring = bytearray(RING)
    r = START
    out = bytearray()
    flags = bytearray()
    bit = 0
    flagpos = 0
    chain = {}
    i = 0
    n = len(src)

    def emit_flag_slot():
        nonlocal bit, flagpos
        if bit == 0:
            flagpos = len(out)
            out.append(0)
        return flagpos

    while i < n:
        best_len, best_off = 0, 0
        if i + MIN_MATCH <= n:
            key = bytes(src[i:i + MIN_MATCH])
            for pos in reversed(chain.get(key, ())[-candidates:]):
                limit = min(MAX_MATCH, n - i)
                if limit < MIN_MATCH or limit <= best_len:
                    continue
                length = limit
                while length >= MIN_MATCH:
                    if length > best_len and _copy_matches(ring, r, pos, length, src, i):
                        best_len, best_off = length, pos
                        break
                    length -= 1
                if best_len == MAX_MATCH:
                    break

        slot = emit_flag_slot()
        if best_len >= MIN_MATCH:
            out.append(best_off & 0xFF)
            out.append(((best_off >> 4) & 0xF0) | (best_len - MIN_MATCH))
            take = best_len
        else:
            out[slot] |= (1 << bit)
            out.append(src[i])
            take = 1

        for k in range(take):
            c = src[i + k]
            if i + k + MIN_MATCH <= n:
                chain.setdefault(bytes(src[i + k:i + k + MIN_MATCH]), []).append(r)
            ring[r] = c
            r = (r + 1) & (RING - 1)
        i += take
        bit = (bit + 1) & 7

    del flags
    return bytes(out)


def record(palette, pixels):
    """/*----------------------
     | record
     | Description: One complete CGL record: CLUT, decompressed length,
     |     stream, padded to the 4-byte boundary the next record starts on.
     | Author: suinevere
     | Dependencies: struct
     | Globals: N/A
     | Params: palette -- 256 RGB triples; pixels -- the 8bpp frame
     | Returns: the record bytes
     ----------------------*/"""
    body = compress(pixels)
    out = bytearray(clut(palette))
    out += struct.pack("<I", len(pixels))
    out += body
    while len(out) & 3:
        out.append(0)
    return bytes(out)
