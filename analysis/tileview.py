#!/usr/bin/env python3
"""Render a binary as a grid of NxM 4bpp tiles (de-tiled) so font glyphs are legible.
Usage: tileview.py <file> <start_hex> <len_hex> <tilew> <tileh> <tiles_per_row> <out>"""
import struct, zlib, sys
path,start,length,tw,th,per = sys.argv[1],int(sys.argv[2],16),int(sys.argv[3],16),int(sys.argv[4]),int(sys.argv[5]),int(sys.argv[6])
out = sys.argv[7]
with open(path,"rb") as f:
    f.seek(start); data=f.read(length)
tile_bytes = tw*th//2                       # 4bpp
ntiles = len(data)//tile_bytes
rows_of_tiles = (ntiles+per-1)//per
W = per*tw; H = rows_of_tiles*th
img = bytearray(W*H)                          # grayscale
for t in range(ntiles):
    tx=(t%per)*tw; ty=(t//per)*th
    base=t*tile_bytes
    for yy in range(th):
        for xx in range(0,tw,2):
            byte=data[base + yy*(tw//2) + xx//2]
            for k,nib in enumerate(((byte>>4)&0xF, byte&0xF)):
                px=(ty+yy)*W + tx+xx+k
                img[px]=nib*17
rows=[]
for y in range(H):
    rows.append(b"\x00"+bytes(img[y*W:(y+1)*W]))
raw=b"".join(rows)
def chunk(tag,d):
    c=tag+d; return struct.pack(">I",len(d))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",W,H,8,0,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b"")
open(out,"wb").write(png); print(f"wrote {out} {W}x{H} ({ntiles} tiles {tw}x{th})")
