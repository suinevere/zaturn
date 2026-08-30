#!/usr/bin/env python3
"""Crude tile/bitmap viewer for locating graphics in a binary (ROM-hacker style).
Renders a byte range of a file as 4bpp (or 1bpp) grayscale to PNG so glyph/sprite
data is visible. Usage: gfxview.py <file> <start_hex> <len_hex> <bpp> <width_px> <out>"""
import struct, zlib, sys

path, start, length, bpp, width, out = (
    sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16),
    int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])

with open(path, "rb") as f:
    f.seek(start)
    data = f.read(length)

# expand to grayscale pixel values
px = []
if bpp == 4:
    for byte in data:
        px.append((byte >> 4) * 17)
        px.append((byte & 0xF) * 17)
elif bpp == 1:
    for byte in data:
        for i in range(7, -1, -1):
            px.append(255 if (byte >> i) & 1 else 0)
elif bpp == 8:
    px = list(data)
else:
    raise SystemExit("bpp must be 1, 4 or 8")

height = (len(px) + width - 1) // width
px += [0] * (width * height - len(px))

rows = []
for y in range(height):
    row = bytes(px[y * width:(y + 1) * width])
    rows.append(b"\x00" + row)            # filter 0; grayscale
raw = b"".join(rows)


def chunk(tag, d):
    c = tag + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))  # 0 = grayscale
png += chunk(b"IDAT", zlib.compress(raw, 9))
png += chunk(b"IEND", b"")
open(out, "wb").write(png)
print(f"wrote {out}: {width}x{height} from 0x{start:X}+0x{length:X} bpp{bpp}")
