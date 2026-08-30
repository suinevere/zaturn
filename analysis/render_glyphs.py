#!/usr/bin/env python3
"""Render arbitrary font glyph indices (one row per label) from VDP2 VRAM 0x14000.
Stroke = nibble 3 (white), other nonzero = gray. 16x16 glyphs, scale.
Usage: render_glyphs.py <slot> <out.png> <label0_csv> <label1_csv> ...
  each labelN_csv = comma-separated glyph indices, e.g. 230,274,286,261
"""
import gzip, struct, sys, zlib
base=(r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server"
      r"\mednafen-1.32.1-win64\mcs\Steamgear Mash (Japan).710edf3c0b4b1174322cc4b4e6588862.mc")
def fld(d,name,occ=0):
    m=bytes([len(name)])+name; i=-1
    for _ in range(occ+1): i=d.find(m,i+1)
    sz=struct.unpack_from("<I",d,i+1+len(name))[0]; o=i+1+len(name)+4
    return d[o:o+sz]
slot=sys.argv[1]; out=sys.argv[2]
labels=[[int(x) for x in a.split(",") if x!=""] for a in sys.argv[3:]]
d=gzip.decompress(open(base+slot,"rb").read()); v=fld(d,b"VRAM",1)
FONT=0x14000; SC=6; GP=16
maxlen=max(len(l) for l in labels)
W=1+maxlen*(GP+2); H=1+len(labels)*(GP+2)
img=[[(0,0,90)]*W for _ in range(H)]
def cell(addr,px,py):
    for yy in range(8):
        for xx in range(0,8,2):
            if addr+yy*4+xx//2>=len(v): return
            b=v[addr+yy*4+xx//2]
            for k,nib in enumerate(((b>>4)&0xF,b&0xF)):
                c=(255,255,255) if nib else (0,0,0)
                img[py+yy][px+xx+k]=c
for li,refs in enumerate(labels):
    gy=1+li*(GP+2)
    for gi,g in enumerate(refs):
        gx=1+gi*(GP+2); a=FONT+g*0x80
        for k,(cx,cy) in enumerate([(0,0),(8,0),(0,8),(8,8)]):
            cell(a+k*0x20,gx+cx,gy+cy)
rows=[]
for y in range(H):
    line=bytearray()
    for x in range(W): line+=bytes(img[y][x])*SC
    for _ in range(SC): rows.append(b"\x00"+bytes(line))
Wo,Ho=W*SC,H*SC; raw=b"".join(rows)
def ch(t,dd): c=t+dd; return struct.pack(">I",len(dd))+c+struct.pack(">I",zlib.crc32(c)&0xffffffff)
png=b"\x89PNG\r\n\x1a\n"+ch(b"IHDR",struct.pack(">IIBBBBB",Wo,Ho,8,2,0,0,0))+ch(b"IDAT",zlib.compress(raw,9))+ch(b"IEND",b"")
open(out,"wb").write(png); print(f"wrote {out} {Wo}x{Ho} ({len(labels)} labels)")
