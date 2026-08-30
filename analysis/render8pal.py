#!/usr/bin/env python3
"""Render a VRAM region as 8bpp paletted using CRAM (Saturn RGB555).
Usage: render8pal.py <vram.bin> <cram.bin> <start_hex> <width> <height> <out>"""
import struct, zlib, sys
vram=open(sys.argv[1],"rb").read(); cram=open(sys.argv[2],"rb").read()
start=int(sys.argv[3],16); W=int(sys.argv[4]); H=int(sys.argv[5]); out=sys.argv[6]
# build 256-colour palette from first 512 bytes of CRAM (RGB555 BE)
pal=[]
for i in range(256):
    v=struct.unpack_from(">H",cram,(i*2)%len(cram))[0]
    r=(v&0x1f)*255//31; g=((v>>5)&0x1f)*255//31; b=((v>>10)&0x1f)*255//31
    pal.append((r,g,b))
rows=[]
for y in range(H):
    row=bytearray()
    for x in range(W):
        idx=vram[start+y*W+x] if start+y*W+x < len(vram) else 0
        r,g,b=pal[idx]; row+=bytes((r,g,b))
    rows.append(b"\x00"+bytes(row))
raw=b"".join(rows)
def chunk(t,d): c=t+d; return struct.pack(">I",len(d))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",W,H,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b"")
open(out,"wb").write(png); print(f"wrote {out} {W}x{H}")
