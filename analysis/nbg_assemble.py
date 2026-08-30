#!/usr/bin/env python3
"""Assemble a VDP2 NBG cell plane into an image: read 64x64 pattern names from a plane
base, fetch each 8x8 4bpp cell (char# * 0x20) from VRAM, apply CRAM palette.
Tries 1-word and 2-word pattern-name formats. Usage: nbg_assemble.py <plane_hex> <pnfmt 1|2> <palbank> <out>"""
import struct, zlib, sys
vram=open(r"analysis\vram_1.bin","rb").read()
cram=open(r"analysis\cram.bin","rb").read()
plane=int(sys.argv[1],16); pnfmt=int(sys.argv[2]); palbank=int(sys.argv[3]); out=sys.argv[4]

def color(idx):
    if idx==0: return (0,0,0)
    v=struct.unpack_from(">H",cram,(idx*2)%len(cram))[0]
    return ((v&0x1f)*255//31, ((v>>5)&0x1f)*255//31, ((v>>10)&0x1f)*255//31)

CELL=64*8  # 64 cells * 8px = 512
img=[[ (0,0,0) ]*CELL for _ in range(CELL)]
step = 4 if pnfmt==2 else 2
for cy in range(64):
    for cx in range(64):
        po = plane + (cy*64+cx)*step
        if pnfmt==2:
            w0=struct.unpack_from(">H",vram,po)[0]; w1=struct.unpack_from(">H",vram,po+2)[0]
            char = w1 & 0x7FFF
            pal = (w0>>4)&0x7F
        else:
            w=struct.unpack_from(">H",vram,po)[0]
            char = w & 0x3FF
            pal = (w>>12)&0xF
        cellbase = (char*0x20)
        for yy in range(8):
            for xx in range(0,8,2):
                if cellbase+yy*4+xx//2 >= len(vram): continue
                b=vram[cellbase+yy*4+xx//2]
                for k,nib in enumerate(((b>>4)&0xF, b&0xF)):
                    ci = (palbank*16 + pal*16 + nib) if nib else 0
                    img[cy*8+yy][cx*8+xx+k]=color(ci)
rows=[]
for y in range(CELL):
    row=bytearray()
    for x in range(CELL):
        r,g,b=img[y][x]; row+=bytes((r,g,b))
    rows.append(b"\x00"+bytes(row))
raw=b"".join(rows)
def chunk(t,d): c=t+d; return struct.pack(">I",len(d))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",CELL,CELL,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b"")
open(out,"wb").write(png); print(f"wrote {out} (plane 0x{plane:X} pnfmt={pnfmt})")
