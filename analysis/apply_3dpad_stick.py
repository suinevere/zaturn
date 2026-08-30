#!/usr/bin/env python3
"""Steamgear Mash — 3D Control Pad ANALOG STICK support (movement + menus), COMPOSABLE build.

Adds analog-stick support on top of the analog-gate fix (NOP the two peripheral-type gate branches
so the high-level decoder at 0x06004090 decodes the analog pad). The decoder writes
0x060834CC = ~(padrec[+0]) "held" (->0x060769C4) and 0x060834CE = ~(padrec[+2]) "edge" (->0x060769C8).
Direction bits live in the HIGH byte: UP 0x1000 DOWN 0x2000 LEFT 0x4000 RIGHT 0x8000.

We inject BOTH:
  HELD : OR stick dirs into 0x060834CC every frame (in-game movement reads the held word).
  EDGE : OR (stick & ~prev) into 0x060834CE on the frame the stick newly enters a direction (one
         pulse per flick) so vertical menus step crisply and the settings controller-mode left/right
         (sole L/R reader 0x0603AD50, edge-only) toggles. 1-byte persistent `prev`.
HELD and EDGE use the SAME direction mapping (they feed the same game input word): stick-left ->
LEFT 0x40<<8=0x4000, stick-right -> RIGHT 0x80<<8=0x8000, up 0x1000, down 0x2000. (Earlier builds had
held and edge using opposite X senses, which made in-game and menu left/right disagree.)

COMPOSABILITY: the English translation patch rewrites 0.BIN sectors {19,20,138,146,147,181,182,183}
(it even reuses the old 0x0604D816 padding for text). So this patch keeps ALL its code in
translation-FREE sectors — gate/trampoline/scratch in sector 0, and the routine in three safe code
caves: 148 (mask), 141 (held+edge), 180 (swap+return). Result: 0.BIN sectors touched = {0,141,148,180},
disjoint from the translation -> apply_3dpad_stick.py works on EITHER 0.BIN (original or translated),
and the two track-level xdelta patches stack in any order.

Pieces (0.BIN, load base 0x06004000):
  gate NOPs 0x0C8/0x128 ; trampoline @0x0D4 -> Cave-mask ; Cave-A addr literal @0x110 ; prev byte @0x114
  Cave mask @0x4A11C (0x0604E11C, sec148) -> Cave held/edge @0x46C90 (0x0604AC90, sec141)
  -> Cave swap/return @0x5A448 (0x0605E448, sec180) -> back to 0x060040DA.
"""
import struct, sys

LOAD      = 0x06004000
GATES     = [(0x0C8, 0x8B22), (0x128, 0x8B0D)]
NOP       = 0x0009
SITE      = 0x0D4
SITE_ADDR = LOAD + SITE
LITSLOT   = 0x110                                  # trampoline: Cave-mask address (4-aligned, dead)
SCRATCH   = 0x114                                  # persistent `prev` dir byte (4-aligned, dead)
RET_ADDR  = LOAD + 0x0DA
DISPLACED = [0x63E2, 0x8531, 0x6203]              # mov.l @r14,r3 ; mov.w @(2,r3),r0 ; mov r0,r2
OREG_X    = 0x20100029

CAVE_M_OFF = 0x4A11C; CAVE_M_ADDR = LOAD + CAVE_M_OFF; CAVE_M_MAX = 98   # 0x0604E11C sec148 (mask)
CAVE_H_OFF = 0x46C90; CAVE_H_ADDR = LOAD + CAVE_H_OFF; CAVE_H_MAX = 51   # 0x0604AC90 sec141 (held+edge)
CAVE_S_OFF = 0x5A448; CAVE_S_ADDR = LOAD + CAVE_S_OFF; CAVE_S_MAX = 48   # 0x0605E448 sec180 (swap+ret)

class Asm:
    def __init__(self): self.items=[]; self.labels={}
    def op(self,hw): self.items.append(('op',hw&0xFFFF))
    def label(self,n): self.items.append(('label',n))
    def pcrel(self,reg,name): self.items.append(('pcrel',name,reg))
    def branch(self,cond,name): self.items.append(('branch',cond,name))
    def lit(self,name,value): self.items.append(('lit',name,value))
    def assemble(self,base):
        off=0; layout=[]
        for it in self.items:
            if it[0]=='label': self.labels[it[1]]=base+off
            elif it[0]=='lit':
                if off%4: off+=2
                self.labels[it[1]]=base+off; layout.append((off,it)); off+=4
            else: layout.append((off,it)); off+=2
        out=bytearray(off)
        for o,it in layout:
            if it[0]=='op': struct.pack_into('>H',out,o,it[1])
            elif it[0]=='lit': struct.pack_into('>I',out,o,it[2]&0xFFFFFFFF)
            elif it[0]=='pcrel':
                disp=(self.labels[it[1]]-((base+o+4)&~3))//4
                assert 0<=disp<=255, f"pcrel {it[1]} disp {disp} @+{o}"
                struct.pack_into('>H',out,o,0xD000|(it[2]<<8)|disp)
            elif it[0]=='branch':
                disp=(self.labels[it[2]]-(base+o+4))//2
                assert -128<=disp<=127, f"branch {it[2]} disp {disp} @+{o}"
                struct.pack_into('>H',out,o,(0x8900 if it[1]=='t' else 0x8B00)|(disp&0xFF))
        return bytes(out)

def cave_mask():   # build stick dir mask r7 (gated on analog flag), then jmp held/edge cave
    a=Asm()
    a.op(0xE700)                                   # mov #0,r7
    a.op(0x61E2); a.op(0x8414); a.op(0x600C); a.op(0x2008)   # padrec flag -> r0
    a.branch('t','after')                          # bt after (digital -> r7=0)
    a.pcrel(1,'litO'); a.op(0x6010); a.op(0x600C)  # r1=OREG ; r0=X ; extu
    a.op(0xE260); a.op(0xE3A0); a.op(0x633C)       # r2=0x60 ; r3=0xA0 (extu)
    a.op(0x3022); a.branch('t','nL'); a.op(0xE640); a.op(0x276B)  # X<0x60 (stick left)  -> LEFT 0x40
    a.label('nL'); a.op(0x3032); a.branch('f','nR'); a.op(0xE680); a.op(0x276B)  # X>=0xA0 (stick right) -> RIGHT 0x80
    a.label('nR'); a.op(0x8412); a.op(0x600C)      # r0=Y ; extu
    a.op(0x3022); a.branch('t','nU'); a.op(0xE610); a.op(0x276B)  # Y<0x60 -> UP 0x10
    a.label('nU'); a.op(0x3032); a.branch('f','nD'); a.op(0xE620); a.op(0x276B)  # Y>=0xA0 -> DOWN 0x20
    a.label('nD'); a.label('after')
    a.pcrel(1,'litH'); a.op(0x412B); a.op(0x0009)  # jmp held/edge cave
    a.lit('litO',OREG_X); a.lit('litH',CAVE_H_ADDR)
    return a.assemble(CAVE_M_ADDR)

def cave_held():   # HELD -> 0x060834CC ; edge_low = r7 & ~prev ; prev=r7 ; jmp swap cave
    a=Asm()
    a.op(0x6173); a.op(0x4118); a.op(0x6341); a.op(0x231B); a.op(0x2431)  # 0x060834CC |= r7<<8
    a.pcrel(2,'litP'); a.op(0x6020); a.op(0x6307); a.op(0x6073); a.op(0x2039); a.op(0x2270)  # r0 = r7 & ~prev ; prev=r7
    a.pcrel(1,'litS'); a.op(0x412B); a.op(0x0009)  # jmp swap cave
    a.lit('litP',LOAD+SCRATCH); a.lit('litS',CAVE_S_ADDR)
    return a.assemble(CAVE_H_ADDR)

def cave_swap():   # fold edge (r0) into 0x060834CE via displaced r2, return  (held==edge: no L/R swap)
    a=Asm()
    a.op(0x4018); a.op(0x6603)                     # shll8 r0 ; r6 = edge<<8
    a.op(DISPLACED[0]); a.op(DISPLACED[1]); a.op(DISPLACED[2])  # displaced originals
    a.op(0x6667); a.op(0x2269)                     # not r6,r6 ; r2 &= ~(edge<<8)
    a.pcrel(1,'litR'); a.op(0x412B); a.op(0x0009)  # return to 0x060040DA
    a.lit('litR',RET_ADDR)
    return a.assemble(CAVE_S_ADDR)

def build(in_bin,out_bin):
    b=bytearray(open(in_bin,'rb').read())
    rd=lambda o: struct.unpack_from('>H',b,o)[0]
    for off,exp in GATES:
        assert rd(off)==exp, f"gate @0x{off:X}: want {exp:04X} got {rd(off):04X} — wrong 0.BIN?"
        struct.pack_into('>H',b,off,NOP)
    for i,hw in enumerate(DISPLACED):
        assert rd(SITE+2*i)==hw, f"displaced @0x{SITE+2*i:X}: want {hw:04X} got {rd(SITE+2*i):04X}"
    assert rd(LITSLOT)==0xE200, f"dead-slot @0x{LITSLOT:X}: want E200 got {rd(LITSLOT):04X}"
    caves=[(CAVE_M_OFF,cave_mask(),CAVE_M_MAX,'mask'),
           (CAVE_H_OFF,cave_held(),CAVE_H_MAX,'held/edge'),
           (CAVE_S_OFF,cave_swap(),CAVE_S_MAX,'swap/ret')]
    for off,code,mx,name in caves:
        assert len(code)<=mx, f"cave {name} too big: {len(code)}>{mx}"
        assert b[off:off+len(code)]==b"\x00"*len(code), f"cave {name} region @0x{off:X} not zero (translation overlap?)"
        b[off:off+len(code)]=code
    struct.pack_into('>I',b,SCRATCH,0)             # zero persistent `prev`
    disp=((LOAD+LITSLOT)-((SITE_ADDR+4)&~3))//4
    assert disp==0x0E and (LOAD+LITSLOT)%4==0
    struct.pack_into('>H',b,SITE+0,0xD100|disp)    # mov.l @(disp,pc),r1
    struct.pack_into('>H',b,SITE+2,0x412B)         # jmp @r1
    struct.pack_into('>H',b,SITE+4,0x0009)         # nop
    struct.pack_into('>I',b,LITSLOT,CAVE_M_ADDR)   # literal = Cave-mask address
    open(out_bin,'wb').write(b)
    print(f"gate NOPed; tramp->mask 0x{CAVE_M_ADDR:08X}({len(caves[0][1])}B) ->held 0x{CAVE_H_ADDR:08X}"
          f"({len(caves[1][1])}B) ->swap 0x{CAVE_S_ADDR:08X}({len(caves[2][1])}B); prev@0x{SCRATCH:X}. "
          f"wrote {out_bin} ({len(b)}B).")
    return b

if __name__=='__main__':
    src=sys.argv[1] if len(sys.argv)>1 else "game_originals/Steamgear Mash (Japan)/0.BIN"
    dst=sys.argv[2] if len(sys.argv)>2 else "game_patched/steamgear_mash_3dpad/0_3dpad.BIN"
    build(src,dst)
