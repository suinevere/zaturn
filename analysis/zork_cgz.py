#!/usr/bin/env python3
"""Zork I (Saturn, JP) graphics codec — CGZ / CZ / SLD decompressor.

All compressed graphics on the disc (``*.CGZ``, ``*.CZ``, ``*.SLD``) use a
classic Haruhiko-Okumura LZSS:

  * 4-byte little-endian header = the *decompressed* size, then the bit stream.
  * 4 KiB ring buffer, initialised to 0, write pointer starts at n-18 (=4078).
  * Each control byte carries 8 flags, LSB first.
      - flag bit = 1  -> one literal byte.
      - flag bit = 0  -> back reference, 2 bytes (b0, b1):
                            off = ((b1 & 0xf0) << 4) | b0   (12-bit ring offset)
                            len = (b1 & 0x0f) + 3
  Verified byte-exact: TITLE.CGZ -> the "ZORK I" title (320x240 8bpp),
  ACTLOGO.CGZ -> the ACTIVISION logo, INIT2.SLD -> the gameplay UI sprite sheet
  (mode-tab buttons / compass / Roland-Sound-Space logo / item icons) at 128 wide.

Uncompressed companions: ``*.COL`` = 256-entry RGB555 little-endian CLUT;
``*.CGD`` / ``*.CGL`` = raw 8bpp or 4bpp pixels (e.g. OVER.CGL = "GAME OVER").
"""
import struct


def decompress(buf, size=None):
    """LZSS-decompress *buf*. Size is taken from the 4-byte LE header unless given."""
    if size is None:
        size = struct.unpack_from("<I", buf, 0)[0]
    n = 4096
    ring = bytearray(n)
    r = n - 18
    out = bytearray()
    i = 4
    L = len(buf)
    flags = 0
    nbits = 0
    while i < L and len(out) < size:
        if nbits == 0:
            flags = buf[i]; i += 1; nbits = 8
        bit = flags & 1; flags >>= 1; nbits -= 1
        if bit:                                   # literal
            if i >= L:
                break
            c = buf[i]; i += 1
            out.append(c); ring[r] = c; r = (r + 1) & (n - 1)
        else:                                     # back reference
            if i + 1 >= L:
                break
            b0 = buf[i]; b1 = buf[i + 1]; i += 2
            off = ((b1 & 0xf0) << 4) | b0
            ln = (b1 & 0x0f) + 3
            for k in range(ln):
                c = ring[(off + k) & (n - 1)]
                out.append(c); ring[r] = c; r = (r + 1) & (n - 1)
    return bytes(out)


def compress(data):
    """LZSS-compress to the same format ``decompress`` reads (4-byte LE size hdr +
    flag/literal/backref bitstream; 4 KiB ring init 0, write ptr n-18). Greedy matcher.
    Verified: decompress(compress(x)) == x."""
    from collections import defaultdict
    n = len(data)
    out = bytearray(struct.pack("<I", n))
    N = 4096; MINM = 3; MAXM = 18
    table = defaultdict(list)
    flag = 0; fcount = 0; fpos = -1

    def add_op(bit, payload):
        # Reserve a flag byte before each group of 8 ops; fill it after the 8th.
        nonlocal flag, fcount, fpos
        if fcount == 0:
            fpos = len(out); out.append(0)
        if bit:
            flag |= (1 << fcount)
        out.extend(payload)
        fcount += 1
        if fcount == 8:
            out[fpos] = flag; flag = 0; fcount = 0

    i = 0
    while i < n:
        best_len = 0; best_j = -1
        if i + MINM <= n:
            cands = table.get(bytes(data[i:i + 3]))
            if cands:
                lo = i - (N - 1); scan = 0
                for j in reversed(cands):
                    if j < lo or scan >= 256:
                        break
                    scan += 1
                    L = 0
                    while L < MAXM and i + L < n and data[j + L] == data[i + L]:
                        L += 1
                    if L > best_len:
                        best_len = L; best_j = j
                        if L == MAXM:
                            break
        if best_len >= MINM:
            off = (N - 18 + best_j) & (N - 1)
            add_op(0, bytes((off & 0xff,
                             ((off >> 4) & 0xf0) | ((best_len - 3) & 0x0f))))
            end = i + best_len
            while i < end:
                if i + 3 <= n:
                    table[bytes(data[i:i + 3])].append(i)
                i += 1
        else:
            add_op(1, bytes((data[i],)))
            if i + 3 <= n:
                table[bytes(data[i:i + 3])].append(i)
            i += 1
    if fcount > 0:
        out[fpos] = flag  # flush partial final group
    return bytes(out)


def load_clut(buf):
    """Parse a .COL file -> list of 256 (r,g,b) tuples from RGB555 LE entries."""
    pal = []
    for i in range(0, min(len(buf), 512), 2):
        v = buf[i] | (buf[i + 1] << 8)
        pal.append(((v & 0x1f) << 3, ((v >> 5) & 0x1f) << 3, ((v >> 10) & 0x1f) << 3))
    while len(pal) < 256:
        pal.append((0, 0, 0))
    return pal


if __name__ == "__main__":
    import sys
    src = sys.argv[1]
    data = open(src, "rb").read()
    dec = decompress(data)
    out = sys.argv[2] if len(sys.argv) > 2 else src + ".dec"
    open(out, "wb").write(dec)
    print(f"{src}: {len(data)} -> {len(dec)} bytes (hdr size {struct.unpack_from('<I', data, 0)[0]}) -> {out}")
