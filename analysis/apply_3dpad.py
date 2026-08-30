#!/usr/bin/env python3
"""Steamgear Mash — Saturn 3D Control Pad (analog stick) support. SEPARATE from the English
translation (patches the ORIGINAL 0.BIN). ITERATION 2.

Root cause (proven by OREG savestate analysis, docs/STEAMGEAR_3DPAD.md "ITERATION 2"): the
analog 3D pad reports ID 0x16 with clean axes in the SMPC OREG mirror, but the game's SMPC
driver normalizes every pad to a 2-byte digital record and DISCARDS the analog axes. The live
"current pad" state lives at 4 records in HWRAM (d0 = digital direction byte, active-low):
  0x060EFBE0, 0x060EFBEC, 0x060EFBFE, 0x060EFC06   (confirmed: their d1 byte reflects L/R
  shoulders in real time across the analog savestates).
Analog axes are readable at fixed SMPC OREG addresses: X=0x20100029, Y=0x2010002B
  (X 0x00=left / 0x80=center / 0xFF=right ; Y 0x00=up / 0x80=center / 0xFF=down).

Mechanism: hook the SMPC driver's normal rts exit (0x060DC522). At that point the 4 records
are filled for this frame and OREG holds the current analog report. The hook reads X/Y, builds
an active-low d-pad clear mask (Up 0x10 / Down 0x20 / Left 0x40 / Right 0x80), and AND-clears
those bits into d0 of all 4 records, then runs the relocated epilogue. So pushing the stick
drives the player exactly like the d-pad.

The driver is dispatched via a function-pointer table (no static caller to hook), so the rts
exit is the clean per-frame anchor. iter-1 (read 0x060EFB50 staging) was INERT — wrong buffer.
UNTESTED on hardware/emu: if no effect, the analog completion may take the tail-call exit
(0x060DC50E) instead — retarget there. Hand-assembled then disassembly-verified by build().
"""
import struct, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

LOAD      = 0x06004000
HOOK_OFF  = 0xEA580                 # zero pad; HWRAM 0x060EE580
HOOK_ADDR = LOAD + HOOK_OFF
SITE      = 0xD8522                 # SMPC driver rts epilogue (HWRAM 0x060DC522)
SITE_ADDR = LOAD + SITE            # 0x060DC522

OREG_X = 0x20100029
OREG_Y = 0x2010002B
RECS   = [0x060EFBE0, 0x060EFBEC, 0x060EFBFE, 0x060EFC06]   # d0 of the 4 live pad records

# original epilogue bytes at 0xDC522 (relocated verbatim into the hook tail)
EPILOGUE = bytes.fromhex("7f084f2668f669f66af66bf66cf66df6000b6ef6")

# ---------- tiny two-pass SH-2 assembler (only the ops we need) ----------
class Asm:
    def __init__(self):
        self.items = []          # ('op', halfword) or ('lit',name,value) or ('label',name) or ('pcrel',name,reg) or ('branch',cond,name)
        self.labels = {}
    def op(self, hw):            self.items.append(('op', hw & 0xFFFF))
    def label(self, n):         self.items.append(('label', n))
    def pcrel(self, reg, name): self.items.append(('pcrel', name, reg))   # mov.l @(d,pc),reg
    def branch(self, cond, name): self.items.append(('branch', cond, name))  # cond 't'/'f'
    def lit(self, name, value): self.items.append(('lit', name, value))
    def assemble(self, base):
        # pass 1: assign offsets (each op/pcrel/branch = 2 bytes; lit = 4, 4-aligned)
        off = 0; layout = []
        for it in self.items:
            if it[0] == 'label':
                self.labels[it[1]] = base + off
            elif it[0] == 'lit':
                if off % 4: off += 2                    # align literal
                self.labels[it[1]] = base + off
                layout.append((off, it)); off += 4
            else:
                layout.append((off, it)); off += 2
        # pass 2: emit
        out = bytearray(off)
        for off_i, it in layout:
            if it[0] == 'op':
                struct.pack_into(">H", out, off_i, it[1])
            elif it[0] == 'lit':
                struct.pack_into(">I", out, off_i, it[2] & 0xFFFFFFFF)
            elif it[0] == 'pcrel':
                name, reg = it[1], it[2]
                tgt = self.labels[name]; pc = base + off_i
                disp = (tgt - ((pc + 4) & ~3)) // 4
                assert 0 <= disp <= 255, f"pcrel disp {disp} out of range"
                struct.pack_into(">H", out, off_i, 0xD000 | (reg << 8) | disp)
            elif it[0] == 'branch':
                cond, name = it[1], it[2]
                tgt = self.labels[name]; pc = base + off_i
                disp = (tgt - (pc + 4)) // 2
                assert -128 <= disp <= 127, f"branch disp {disp} out of range"
                base_op = 0x8900 if cond == 't' else 0x8B00
                struct.pack_into(">H", out, off_i, base_op | (disp & 0xFF))
        return bytes(out)

def build_hook():
    a = Asm()
    a.op(0x2F06)                       # mov.l r0,@-r15      ; preserve return value
    a.pcrel(1, 'litX'); a.op(0x6110); a.op(0x611C)   # r1=*OREG_X (X), extu.b
    a.pcrel(2, 'litY'); a.op(0x6220); a.op(0x622C)   # r2=*OREG_Y (Y), extu.b
    a.op(0xE700)                       # mov #0,r7           ; clear-mask
    # LEFT: X < 0x60  -> set 0x40
    a.op(0xE360); a.op(0x633C); a.op(0x3132); a.branch('t', 'L1'); a.op(0xE340); a.op(0x273B)
    a.label('L1')
    # RIGHT: X >= 0xA0 -> set 0x80
    a.op(0xE3A0); a.op(0x633C); a.op(0x3132); a.branch('f', 'L2'); a.op(0xE380); a.op(0x273B)
    a.label('L2')
    # UP: Y < 0x60     -> set 0x10
    a.op(0xE360); a.op(0x633C); a.op(0x3232); a.branch('t', 'L3'); a.op(0xE310); a.op(0x273B)
    a.label('L3')
    # DOWN: Y >= 0xA0  -> set 0x20
    a.op(0xE3A0); a.op(0x633C); a.op(0x3232); a.branch('f', 'L4'); a.op(0xE320); a.op(0x273B)
    a.label('L4')
    a.op(0x6677)                       # not r7,r6           ; r6 = ~mask
    for i in range(4):                 # for each record: d0 &= ~mask
        a.pcrel(5, f'b{i}'); a.op(0x6350); a.op(0x2369); a.op(0x2530)  # r5=*; r3=@r5; and r6,r3; @r5=r3
    a.op(0x60F6)                       # mov.l @r15+,r0      ; restore r0
    for hw in struct.unpack(f">{len(EPILOGUE)//2}H", EPILOGUE):
        a.op(hw)                       # relocated epilogue (incl. rts + delay slot)
    a.lit('litX', OREG_X); a.lit('litY', OREG_Y)
    for i, r in enumerate(RECS):
        a.lit(f'b{i}', r)
    return a.assemble(HOOK_ADDR)

def build(in_bin, out_bin):
    b = bytearray(open(in_bin, "rb").read())
    assert b[SITE:SITE+len(EPILOGUE)] == EPILOGUE, "exit-site bytes changed — wrong 0.BIN?"
    hook = build_hook()
    assert len(hook) <= 0x100, f"hook too big: {len(hook)}"
    assert b[HOOK_OFF:HOOK_OFF+len(hook)] == b"\x00"*len(hook), "hook pad not zero"
    # write hook
    b[HOOK_OFF:HOOK_OFF+len(hook)] = hook
    # trampoline at SITE: mov.l @(1,pc),r1 ; jmp @r1 ; nop ; <HOOK_ADDR @ SITE+6, 4-aligned>
    # SITE=0xD8522, so (PC&~3)+4+1*4 = 0xDC528 = SITE+6 (4-aligned) is the literal slot.
    assert (SITE_ADDR + 6) % 4 == 0, "literal slot not 4-aligned"
    struct.pack_into(">H", b, SITE+0, 0xD101)
    struct.pack_into(">H", b, SITE+2, 0x412B)
    struct.pack_into(">H", b, SITE+4, 0x0009)
    struct.pack_into(">I", b, SITE+6, HOOK_ADDR)
    open(out_bin, "wb").write(b)
    print(f"iter-2 written: trampoline @0x{SITE:05X} (HWRAM 0x{SITE_ADDR:08X}) -> hook @0x{HOOK_OFF:05X} "
          f"(0x{HOOK_ADDR:08X}), {len(hook)} bytes; OREG X/Y -> d-pad bits in 4 records.")
    return b

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "analysis/_orig0.BIN"
    dst = sys.argv[2] if len(sys.argv) > 2 else "game_patched/steamgear_mash_3dpad/0_3dpad.BIN"
    build(src, dst)
