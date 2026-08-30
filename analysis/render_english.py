#!/usr/bin/env python3
"""Render English text as Steamgear-format 16x16 glyphs (one row per word) to a PNG.
Proves saturn_translate.sgfont produces legible glyphs in the game's font format.
Usage: render_english.py <out.png> "WORD ONE" "WORD TWO" ...
"""
import sys, os, struct, zlib
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saturn_translate import sgfont

out = sys.argv[1]
words = sys.argv[2:] or ["START", "OPTIONS", "STEAMGEAR MASH"]
SC, GP = 6, 16
maxlen = max(len(w) for w in words)
W = 1 + maxlen * (GP + 2)
H = 1 + len(words) * (GP + 2)
img = [[(0, 0, 90)] * W for _ in range(H)]
for wi, word in enumerate(words):
    gy = 1 + wi * (GP + 2)
    for ci, ch in enumerate(word):
        grid = sgfont.decode_glyph(sgfont.english_glyph(ch))
        gx = 1 + ci * (GP + 2)
        for y in range(16):
            for x in range(16):
                if grid[y][x] == sgfont.STROKE_NIBBLE:
                    img[gy + y][gx + x] = (255, 255, 255)
rows = []
for y in range(H):
    line = bytearray()
    for x in range(W):
        line += bytes(img[y][x]) * SC
    for _ in range(SC):
        rows.append(b"\x00" + bytes(line))
Wo, Ho = W * SC, H * SC
raw = b"".join(rows)
def chunk(t, dd):
    c = t + dd
    return struct.pack(">I", len(dd)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", Wo, Ho, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
open(out, "wb").write(png)
print(f"wrote {out} {Wo}x{Ho} ({len(words)} words)")
