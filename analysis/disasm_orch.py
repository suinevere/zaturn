#!/usr/bin/env python3
"""Disassemble an 0.BIN code range and resolve PC-relative literal-pool loads
(mov.l/mov.w @(disp,pc),Rn) so data pointers (font base, string tables) are visible."""
import struct, sys
sys.path.insert(0, r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server")
from saturn_translate import sh2

BASE = 0x06004000
data = open(r"C:\Users\saggl\IdeaProjects\AI Sega Saturn Translation MCP Server\game_originals\Steamgear Mash (Japan)\0.BIN","rb").read()
start = int(sys.argv[1],16); end = int(sys.argv[2],16)

def rd32(addr):
    o=addr-BASE; return struct.unpack(">I",data[o:o+4])[0]
def rd16(addr):
    o=addr-BASE; return struct.unpack(">H",data[o:o+2])[0]

a=start
while a < end:
    w = rd16(a)
    ins = sh2.disasm(w, a)
    note=""
    op=w>>12; n=(w>>8)&0xf; d8=w&0xff
    if op==0xd:   # mov.l @(disp,pc),Rn
        pool=(a & ~3)+4+d8*4; val=rd32(pool)
        note=f"  ; r{n} = [0x{pool:06X}] = 0x{val:08X}"
    elif op==0x9: # mov.w @(disp,pc),Rn
        pool=a+4+d8*2; val=rd16(pool)
        note=f"  ; r{n} = 0x{val:04X}"
    print(f"  {a:06X}: {w:04X}  {ins.text}{note}")
    a+=2
