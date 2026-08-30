#!/usr/bin/env python3
"""Render a region of a file as raw big-endian RGB555 (Saturn) to PNG.
Usage: raw16.py <file> <start_hex> <width> <height> <out>"""
import struct, zlib, sys
path, start, width, height, out = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
with open(path,"rb") as f:
    f.seek(start); data = f.read(width*height*2)
words = struct.unpack(f">{len(data)//2}H", data)
rows=[]
for y in range(height):
    row=bytearray()
    for x in range(width):
        i=y*width+x
        v = words[i] if i < len(words) else 0
        r=(v&0x1f)*255//31; g=((v>>5)&0x1f)*255//31; b=((v>>10)&0x1f)*255//31
        row += bytes((r,g,b))
    rows.append(b"\x00"+bytes(row))
raw=b"".join(rows)
def chunk(t,d):
    c=t+d; return struct.pack(">I",len(d))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b"")
open(out,"wb").write(png); print(f"wrote {out} {width}x{height} from 0x{start:X}")
