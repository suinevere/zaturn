#!/usr/bin/env python3
"""Find Saturn button-combo "cheat" checks in an SH-2 overlay.

A cheat/debug check reads the live controller word from a global, then compares
it (cmp/eq) or masks it (tst) against a constant whose bits are all valid pad
buttons. We:
  1. find candidate input-word globals: longwords in the literal pool that look
     like RAM pointers AND are dereferenced and then compared/tested against a
     pad-bit constant;
  2. for every cmp/eq or tst whose other operand traces to such a read, decode
     the constant into button names.

Usage: python cheat_scan.py <file.BIN> <load_base_hex>   e.g. ... A.BIN 0x06054000
"""
import sys, struct

PAD = {0x8000:'Right',0x4000:'Left',0x2000:'Down',0x1000:'Up',0x0800:'Start',
       0x0400:'A',0x0200:'C',0x0100:'B',0x0080:'R',0x0040:'X',0x0020:'Y',
       0x0010:'Z',0x0008:'L'}
PADBITS = sum(PAD)  # 0xFFF8

def decode(m):
    return '+'.join(n for b,n in PAD.items() if m & b)

def main(path, base):
    data = open(path,'rb').read()
    n = len(data)
    def be16(i): return (data[i]<<8)|data[i+1]
    def be32(i): return struct.unpack('>I', data[i:i+4])[0]

    # Pass 1: every mov.l literal that loads a plausible HWRAM/LWRAM pointer,
    # remember which reg holds an input-word read. We approximate dataflow by a
    # small sliding window: track the last mov.l-from-pointer per register.
    # Pass 2: at each cmp/eq Rm,Rn or tst Rm,Rn, if one reg was recently loaded
    # via @Rk where Rk held an input pointer, and the other holds a pad const,
    # report it.
    hits = []
    def is_ram_ptr(v):
        return 0x06000000 <= v <= 0x06100000 or 0x00200000 <= v <= 0x00300000 \
            or 0x20200000 <= v <= 0x20300000

    # High-precision per-register dataflow with provenance + "age" (how many
    # instructions ago it was set). A real held-input check has the form
    #   ...load &input -> Rp; deref Rp -> Rv; load mask -> Rc;
    #   cmp/eq Rc,Rv  (or tst Rc,Rv);  bt/bf
    # so we require: at the compare, one operand is a freshly-deref'd input word
    # and the other a freshly-loaded pad constant, AND the next opcode is a
    # conditional branch. Freshness window = 4 instructions.
    WINDOW = 4
    reg = [None]*16          # each: dict(kind=, val=, age=int)
    def setreg(rn, kind, val):
        reg[rn] = {'kind':kind, 'val':val, 'age':0}
    def fresh(r):
        return r is not None and r['age'] <= WINDOW
    for i in range(0, n-1, 2):
        for r in reg:
            if r is not None: r['age'] += 1
        w = be16(i); op = w >> 12
        nxt = be16(i+2) if i+2 < n-1 else 0
        rn=(w>>8)&0xF; rm=(w>>4)&0xF
        if op == 0xD:           # mov.l @(disp,PC),Rn
            disp=w&0xFF; lit=((i+4)&~3)+disp*4
            if lit+4<=n:
                v=be32(lit)
                if is_ram_ptr(v): setreg(rn,'ptr',v)
                elif (v & ~PADBITS)==0 and bin(v).count('1')>=2: setreg(rn,'const',v)
                else: setreg(rn,'lit',v)
        elif op == 0x9:         # mov.w @(disp,PC),Rn
            disp=w&0xFF; lit=(i+4)+disp*2
            if lit+2<=n:
                v=be16(lit)
                if (v & ~PADBITS)==0 and bin(v).count('1')>=2: setreg(rn,'const',v)
                elif is_ram_ptr(v): setreg(rn,'ptr',v)  # 16-bit ptr (HWRAM page)
                else: setreg(rn,'lit',v)
        elif op==0x6 and (w&0xF)==0x2:   # mov.l @Rm,Rn
            setreg(rn,'inputval',reg[rm]['val']) if (reg[rm] and reg[rm]['kind']=='ptr') else setreg(rn,'mem',None)
        elif op==0x6 and (w&0xF)==0x1:   # mov.w @Rm,Rn  (pad word is 16-bit)
            setreg(rn,'inputval',reg[rm]['val']) if (reg[rm] and reg[rm]['kind']=='ptr') else setreg(rn,'mem',None)
        elif op==0x5:                    # mov.l @(disp,Rm),Rn
            setreg(rn,'inputval',reg[rm]['val']) if (reg[rm] and reg[rm]['kind']=='ptr') else setreg(rn,'mem',None)
        elif op==0x8 and ((w>>8)&0xF) in (0x5,0x1):  # mov.w @(disp,Rm),Rn (8500..)
            base_r=(w>>4)&0xF
            setreg(rn,'inputval',reg[base_r]['val']) if (reg[base_r] and reg[base_r]['kind']=='ptr') else None
        elif (op==0x3 and (w&0xF)==0x0) or (op==0x2 and (w&0xF)==0x8):  # cmp/eq | tst Rm,Rn
            kind='cmp/eq' if op==0x3 else 'tst'
            a,b=reg[rn],reg[rm]
            isbranch = (nxt & 0xF900)==0x8900  # bt/bf/bt.s/bf.s family (0x89/0x8b/0x8d/0x8f)
            for x,y in ((a,b),(b,a)):
                if fresh(x) and x['kind']=='inputval' and fresh(y) and y['kind']=='const':
                    hits.append((i,kind,y['val'],x['val'],isbranch)); break
            setreg(rn,'flag',None)
        else:
            # opcodes that write Rn we don't model precisely -> mark Rn unknown
            # (be conservative: only clobber for common ALU/mov-imm forms)
            if op in (0xE,):                       # mov #imm,Rn
                v=w&0xFF; setreg(rn,'imm',v)
    print(f'== {path}  base={base:#010x}  size={n} ==')
    if not hits:
        print('  (no input-word button-combo checks found)')
    seen=set()
    for off,kind,const,ptr,isbranch in hits:
        k=(off,const)
        if k in seen: continue
        seen.add(k)
        tag = '' if isbranch else '   (no branch follows - low conf)'
        print(f'  {base+off:#010x}  {kind:7s} input[{ptr:#010x}] vs {const:#06x}  = {decode(const)}{tag}')

if __name__=='__main__':
    main(sys.argv[1], int(sys.argv[2],16))
