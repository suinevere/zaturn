#!/usr/bin/env python3
"""Render dialogue worklist lines (grayscale, faithful AA) from a savestate's VRAM base 0.
Usage: render_dialogue.py <savestate.mcX> <worklist.json> <out_prefix> [per_img=13]
Writes <out_prefix>NN.png batches; prints which line indices are in each.
"""
import gzip, struct, sys, json, zlib
ss, wl_path, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
PER = int(sys.argv[4]) if len(sys.argv) > 4 else 13
d = gzip.decompress(open(ss, "rb").read())
i = d.find(b"\x04VRAM", 0x1B0000); v = d[i + 9:i + 9 + 0x80000]
lines = json.load(open(wl_path, encoding="utf-8"))["strings"]
SC, GP = 5, 16

def render(batch, out):
    maxlen = max(len(e["glyphs"]) for e in batch)
    W = 1 + maxlen * (GP + 2); H = 1 + len(batch) * (GP + 2)
    img = [[(0, 0, 70)] * W for _ in range(H)]
    for li, e in enumerate(batch):
        gy = 1 + li * (GP + 2)
        for gi, g in enumerate(e["glyphs"]):
            gx = 1 + gi * (GP + 2); a = g * 0x80
            for k, (cx, cy) in enumerate([(0, 0), (8, 0), (0, 8), (8, 8)]):
                ca = a + k * 0x20
                for yy in range(8):
                    for xx in range(0, 8, 2):
                        o = ca + yy * 4 + xx // 2
                        if o >= len(v): continue
                        b = v[o]
                        for kk, nib in enumerate(((b >> 4) & 0xF, b & 0xF)):
                            gg = min(255, nib * 20)
                            img[gy + cy + yy][gx + cx + xx + kk] = (gg, gg, gg)
    rows = []
    for y in range(H):
        line = bytearray()
        for x in range(W): line += bytes(img[y][x]) * SC
        for _ in range(SC): rows.append(b"\x00" + bytes(line))
    Wo, Ho = W * SC, H * SC; raw = b"".join(rows)
    def ch(t, dd): c = t + dd; return struct.pack(">I", len(dd)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", Wo, Ho, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b"")
    open(out, "wb").write(png)

for b in range((len(lines) + PER - 1) // PER):
    batch = lines[b * PER:(b + 1) * PER]
    out = f"{prefix}{b:02d}.png"
    render(batch, out)
    print(f"{out}: lines L{b*PER:02d}-L{b*PER+len(batch)-1:02d}")
