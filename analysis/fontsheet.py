#!/usr/bin/env python3
"""Render a region of VDP2 VRAM (from the Steamgear title-menu savestate) as a grid
of 8x8 4bpp tiles, using a CRAM palette bank. For locating the kana font block.
Usage: fontsheet.py <vram_off_hex> <ntiles> <palbank> <out.png>
"""
import gzip, struct, sys, zlib

SS = (r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server"
      r"\mednafen-1.32.1-win64\mcs\Steamgear Mash (Japan).710edf3c0b4b1174322cc4b4e6588862.mc0")
data = gzip.decompress(open(SS, "rb").read())

def field(name, occ=0):
    m = bytes([len(name)]) + name
    i = -1
    for _ in range(occ + 1):
        i = data.find(m, i + 1)
    sz = struct.unpack_from("<I", data, i + 1 + len(name))[0]
    off = i + 1 + len(name) + 4
    return data[off:off + sz]

vram = field(b"VRAM", 1)   # 2nd VRAM field = VDP2 VRAM
cram = field(b"CRAM", 0)

off    = int(sys.argv[1], 16)
ntiles = int(sys.argv[2])
palbank= int(sys.argv[3])
out    = sys.argv[4]
# optional: glyph cells-per-side (1=8x8, 2=16x16) and integer upscale
GS    = int(sys.argv[5]) if len(sys.argv) > 5 else 1   # cells per glyph side
SCALE = int(sys.argv[6]) if len(sys.argv) > 6 else 1

def color(idx):
    if idx == 0:
        return (0, 0, 0)
    v = struct.unpack_from(">H", cram, (idx * 2) % len(cram))[0]
    return ((v & 0x1f) * 255 // 31, ((v >> 5) & 0x1f) * 255 // 31, ((v >> 10) & 0x1f) * 255 // 31)

def draw_cell(img, char_off, px, py):
    for yy in range(8):
        for xx in range(0, 8, 2):
            bo = char_off + yy * 4 + xx // 2
            if bo >= len(vram):
                continue
            b = vram[bo]
            for k, nib in enumerate(((b >> 4) & 0xF, b & 0xF)):
                ci = palbank * 16 + nib if nib else 0
                img[py + yy][px + xx + k] = color(ci)

gpx = GS * 8                       # glyph pixel size
nglyph = ntiles // (GS * GS)
GCOLS = max(1, 256 // gpx)
grows = (nglyph + GCOLS - 1) // GCOLS
W, H = GCOLS * gpx, grows * gpx
img = [[(0, 0, 0)] * W for _ in range(H)]
for g in range(nglyph):
    gx, gy = (g % GCOLS) * gpx, (g // GCOLS) * gpx
    for c in range(GS * GS):           # cells row-major within the glyph
        char_off = off + (g * GS * GS + c) * 0x20
        draw_cell(img, char_off, gx + (c % GS) * 8, gy + (c // GS) * 8)

rows = []
for y in range(H):
    row = bytearray()
    for x in range(W):
        px = img[y][x]
        row += bytes(px) * SCALE
    line = bytes(row)
    for _ in range(SCALE):
        rows.append(b"\x00" + line)
W *= SCALE
H *= SCALE
raw = b"".join(rows)

def chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9))
       + chunk(b"IEND", b""))
open(out, "wb").write(png)
print(f"wrote {out}  off=0x{off:X} ntiles={ntiles} pal={palbank} {W}x{H}")
