#!/usr/bin/env python3
"""Render VDP2 NBG planes from a Steamgear savestate.
Decodes 16x16 (2x2-cell) patterns: char# -> cellbase = char*4, cells TL,TR,BL,BR.
Usage: render_plane.py <slot> <plane_hex> <out.png> [npatt_x=32] [npatt_y=32] [scale=2]
"""
import gzip, struct, sys, zlib

base = (r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server"
        r"\mednafen-1.32.1-win64\mcs\Steamgear Mash (Japan).710edf3c0b4b1174322cc4b4e6588862.mc")

def fld(d, name, occ=0):
    m = bytes([len(name)]) + name; i = -1
    for _ in range(occ + 1): i = d.find(m, i + 1)
    sz = struct.unpack_from("<I", d, i + 1 + len(name))[0]
    off = i + 1 + len(name) + 4
    return d[off:off + sz]

slot   = sys.argv[1]
plane  = int(sys.argv[2], 16)
out    = sys.argv[3]
NX     = int(sys.argv[4]) if len(sys.argv) > 4 else 32
NY     = int(sys.argv[5]) if len(sys.argv) > 5 else 32
SCALE  = int(sys.argv[6]) if len(sys.argv) > 6 else 2

d = gzip.decompress(open(base + slot, "rb").read())
vram = fld(d, b"VRAM", 1)
cram = fld(d, b"CRAM", 0)
VM = len(vram) - 1

GRAY = "--gray" in sys.argv
def color(idx):
    if idx == 0: return (40, 0, 40)   # transparent shown as dark magenta
    v = struct.unpack_from(">H", cram, (idx * 2) % len(cram))[0]
    return ((v & 0x1f) * 255 // 31, ((v >> 5) & 0x1f) * 255 // 31, ((v >> 10) & 0x1f) * 255 // 31)

def gcolor(nib):
    g = nib * 17
    return (g, g, g)

W, H = NX * 16, NY * 16
img = [[(0, 0, 0)] * W for _ in range(H)]

def draw_cell(cell_addr, palbank, px, py):
    for yy in range(8):
        for xx in range(0, 8, 2):
            bo = (cell_addr + yy * 4 + xx // 2) & VM
            b = vram[bo]
            for k, nib in enumerate(((b >> 4) & 0xF, b & 0xF)):
                if GRAY:
                    img[py + yy][px + xx + k] = gcolor(nib)
                else:
                    ci = palbank * 16 + nib if nib else 0
                    img[py + yy][px + xx + k] = color(ci)

for py in range(NY):
    for px in range(NX):
        off = plane + (py * NX + px) * 4
        if off + 4 > len(vram): continue
        w0 = struct.unpack_from(">H", vram, off)[0]
        w1 = struct.unpack_from(">H", vram, off + 2)[0]
        ch = w1 & 0x7FFF
        pal = w0 & 0x7F
        cellbase = (ch * 4) * 0x20
        gx, gy = px * 16, py * 16
        for k, (cx, cy) in enumerate([(0, 0), (8, 0), (0, 8), (8, 8)]):
            draw_cell((cellbase + k * 0x20), pal, gx + cx, gy + cy)

# upscale + PNG
rows = []
for y in range(H):
    line = bytearray()
    for x in range(W):
        line += bytes(img[y][x]) * SCALE
    for _ in range(SCALE):
        rows.append(b"\x00" + bytes(line))
Wo, Ho = W * SCALE, H * SCALE
raw = b"".join(rows)
def chunk(t, dd):
    c = t + dd
    return struct.pack(">I", len(dd)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", Wo, Ho, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9))
       + chunk(b"IEND", b""))
open(out, "wb").write(png)
print(f"wrote {out} slot{slot} plane=0x{plane:05X} {NX}x{NY}patt {Wo}x{Ho}")
