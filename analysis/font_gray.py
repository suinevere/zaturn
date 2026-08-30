#!/usr/bin/env python3
"""Render a range of 16x16 kana glyphs from VDP2 VRAM 0x14000 as a grayscale sheet.
Each glyph = 4 consecutive 0x20-byte 4bpp cells (TL,TR,BL,BR). 16 glyphs per row.
Usage: font_gray.py <slot> <start_glyph> <count> <out.png> [scale=4] [perrow=16]
"""
import gzip, struct, sys, zlib
base = (r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server"
        r"\mednafen-1.32.1-win64\mcs\Steamgear Mash (Japan).710edf3c0b4b1174322cc4b4e6588862.mc")
def fld(d,name,occ=0):
    m=bytes([len(name)])+name; i=-1
    for _ in range(occ+1): i=d.find(m,i+1)
    sz=struct.unpack_from("<I",d,i+1+len(name))[0]; off=i+1+len(name)+4
    return d[off:off+sz]
slot=sys.argv[1]; start=int(sys.argv[2]); count=int(sys.argv[3]); out=sys.argv[4]
SC=int(sys.argv[5]) if len(sys.argv)>5 else 4
PR=int(sys.argv[6]) if len(sys.argv)>6 else 16
FONT=0x14000
d=gzip.decompress(open(base+slot,"rb").read()); v=fld(d,b"VRAM",1)
rows_n=(count+PR-1)//PR
GP=16  # glyph px
# 1px separators between glyphs
W=PR*(GP+1)+1; H=rows_n*(GP+1)+1
img=[[(0,0,80)]*W for _ in range(H)]   # blue grid background
def cell(addr,px,py):
    for yy in range(8):
        for xx in range(0,8,2):
            b=v[addr+yy*4+xx//2]
            for k,nib in enumerate(((b>>4)&0xF,b&0xF)):
                if nib==3:   col3=(255,255,255)   # main stroke
                elif nib:    col3=(70,70,70)       # outline/shadow
                else:        col3=(0,0,0)
                img[py+yy][px+xx+k]=col3
for gi in range(count):
    g=start+gi
    base_addr=FONT+g*0x80
    col=gi%PR; row=gi//PR
    gx=1+col*(GP+1); gy=1+row*(GP+1)
    for k,(cx,cy) in enumerate([(0,0),(8,0),(0,8),(8,8)]):
        cell(base_addr+k*0x20, gx+cx, gy+cy)
# upscale nearest + PNG
rows=[]
for y in range(H):
    line=bytearray()
    for x in range(W): line+=bytes(img[y][x])*SC
    for _ in range(SC): rows.append(b"\x00"+bytes(line))
Wo,Ho=W*SC,H*SC; raw=b"".join(rows)
def chunk(t,dd):
    c=t+dd; return struct.pack(">I",len(dd))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
png=(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",Wo,Ho,8,2,0,0,0))
     +chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b""))
open(out,"wb").write(png)
print(f"wrote {out} glyphs {start}..{start+count-1} {Wo}x{Ho} ({PR}/row)")
