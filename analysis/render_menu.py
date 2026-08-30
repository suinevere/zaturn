#!/usr/bin/env python3
"""Render decoded menu strings (glyph indices) from a savestate's VDP2 VRAM.
Tries font base 0 (glyph g at g*0x80 = char g*4) — the FUN_06005020 char-number scheme.
Usage: render_menu.py <savestate.mcX> <out.png> <base_hex> <idx,idx,..> ...
"""
import gzip, struct, sys, zlib
ss=sys.argv[1]; out=sys.argv[2]; BASE=int(sys.argv[3],16)
labels=[[int(x) for x in a.split(",") if x!=""] for a in sys.argv[4:]]
d=gzip.decompress(open(ss,"rb").read())
i=d.find(b"\x04VRAM",0x1B0000); v=d[i+9:i+9+0x80000]   # 2nd VRAM = VDP2
SC,GP=5,16
maxlen=max(len(l) for l in labels)
W=1+maxlen*(GP+2); H=1+len(labels)*(GP+2)
img=[[(0,0,90)]*W for _ in range(H)]
def cell(addr,px,py):
    for yy in range(8):
        for xx in range(0,8,2):
            a=addr+yy*4+xx//2
            if a>=len(v): return
            b=v[a]
            for k,nib in enumerate(((b>>4)&0xF,b&0xF)):
                g=min(255,nib*20)   # grayscale by intensity (faithful AA)
                img[py+yy][px+xx+k]=(g,g,g)
for li,idxs in enumerate(labels):
    gy=1+li*(GP+2)
    for gi,g in enumerate(idxs):
        gx=1+gi*(GP+2); a=BASE+g*0x80
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
open(out,"wb").write(png); print(f"wrote {out} base=0x{BASE:X}")
