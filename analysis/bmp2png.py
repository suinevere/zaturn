#!/usr/bin/env python3
"""Convert a 24-bit BMP to PNG using only the stdlib (zlib)."""
import struct, zlib, sys, os

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "titlepic.bmp")
dst = os.path.splitext(src)[0] + ".png"

with open(src, "rb") as f:
    b = f.read()
off = struct.unpack_from("<I", b, 10)[0]
w, h = struct.unpack_from("<ii", b, 18)
row_bytes = (w * 3 + 3) & ~3
rows = []
for y in range(h - 1, -1, -1):          # BMP bottom-up -> top-down
    start = off + y * row_bytes
    raw = b[start:start + w * 3]
    out = bytearray()
    for x in range(w):
        bb, gg, rr = raw[x*3], raw[x*3+1], raw[x*3+2]
        out += bytes((rr, gg, bb))      # PNG is RGB
    rows.append(b"\x00" + bytes(out))   # filter byte 0 per scanline
raw = b"".join(rows)


def chunk(tag, data):
    c = tag + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(raw, 9))
png += chunk(b"IEND", b"")
with open(dst, "wb") as f:
    f.write(png)
print(f"wrote {dst} ({w}x{h})")
