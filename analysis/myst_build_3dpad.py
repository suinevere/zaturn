#!/usr/bin/env python3
"""Myst (USA, Saturn) — 3D Control Pad analog-stick patch builder (DRAFT).

Adds analog-stick cursor control to Myst. Strategy (see docs/MYST_3DPAD_RECON.md):
  * Hook the per-frame cursor mover at its entry 0x0601d144 with a 12-byte trampoline
    that jumps to a relocated routine in free padding at 0x0602799c.
  * The routine reads the 3D-pad analog stick from SMPC OREG (X=0x20100029,
    Y=0x2010002b; present iff OREG1 @0x20100023 == 0x16), applies a deadzone + gain,
    adds the delta to the cursor working coords (X=0x06045844, Y=0x06045848; 8.8 FP),
    clamps to screen, and updates the display copies (X=0x06078000, Y=0x06078002).
  * Then it executes the 6 displaced prologue pushes and returns to 0x0601d150.
  * Also flips the IP peripheral string 'JM' -> 'JME' so the 3D pad is advertised.

Uses ONLY r0-r7 (caller-saved / about to be reloaded by the mover prologue), so the
mover's incoming callee-saved r8-r14 are untouched until its own pushes run.

This module ASSEMBLES + verifies by round-tripping through saturn_translate.sh2.
Tunables: DEADZONE, GAIN_SHIFT, XMAX, YMAX.
"""
import sys, os

BASE        = 0x06010000          # A.BIN load address
# Hook the controller POLL at its OREG-loop exit, where OREG holds live analog data
# (proven by read-watchpoint at 0x06048034). 10-byte trampoline avoids jump target 0x06048048.
HOOK        = 0x0604803e          # poll loop-exit
POLL_RESUME = 0x06048580          # where the displaced `bra` went
ADA0        = 0x0605ada0          # displaced: r2 = *(0x06048090) = 0x0605ada0
POLL_BUF_PTR = 0x0605adb0         # ptr to the buffer the poll loop fills ([6]=ID,[3]=X,[2]=Y)
RELOC       = 0x0602799c          # relocated routine (free in-image padding)
MOVER_HOOK   = 0x0601d144         # mover entry (proven per-frame)
MOVER_RESUME = 0x0601d150
DEBUG_MOVER  = int(os.environ.get("MYST_DEBUG_MOVER", "0"))
if DEBUG_MOVER:
    HOOK = MOVER_HOOK             # record OREG at the guaranteed-per-frame mover hook

# Two-hook production design (v0.4): live OREG is only valid INSIDE the poll loop, so a
# capture hook there latches analog (when OREG1==0x16) to scratch; the mover reads scratch.
LOOP_HOOK = 0x06048038            # poll loop tail (`mov #7,r3`); store @0x06048036 runs first
LOOP_CONT = 0x06048024            # loop-back target (replayed)
RELOC2    = 0x06027b00            # capture routine (free padding, separate from RELOC)
AX_SCRATCH = 0x06027c60           # latched analog X
AY_SCRATCH = 0x06027c61           # latched analog Y
FLAG_SCRATCH = 0x06027c62         # set to 1 once analog pad seen

OREG_ID = 0x20100023   # peripheral ID  (==0x16 for analog 3D pad)
OREG_X  = 0x20100029   # analog X (0x00 L .. 0x80 mid .. 0xff R)
OREG_Y  = 0x2010002b   # analog Y (0x00 U .. 0x80 mid .. 0xff D)
CUR_XW  = 0x06045844   # cursor working X, 8.8 FP
CUR_YW  = 0x06045848   # cursor working Y, 8.8 FP
CUR_XD  = 0x06078000   # display X (u16, integer)
CUR_YD  = 0x06078002   # display Y (u16, integer)

DIAG_DRIFT = int(os.environ.get("MYST_DIAG_DRIFT", "0"))  # !=0: ignore stick, drift X by this/frame (8.8 FP)
DEADZONE   = 0x18      # ignore |stick-0x80| <= this
GAIN_SHIFT = 3         # delta = axis << 3  (~4 px/frame at full deflection)
XMAX       = 0x013f00  # 319 << 8
YMAX       = 0x00df00  # 223 << 8  (tunable; reduce if cursor over-travels a status bar)

# ---------------------------------------------------------------- mini SH-2 asm
class Asm:
    def __init__(self, org):
        self.org = org; self.ins = []; self.labels = {}; self.longs = []
    def label(self, n): self.labels[n] = ('ins', len(self.ins))
    def emit(self, fn): self.ins.append(fn)            # fn(pc, A) -> 2 bytes
    # operand helpers
    def w(self, v): self.emit(lambda pc, A, v=v: bytes([(v >> 8) & 0xff, v & 0xff]))
    # --- instructions (each appends one 16-bit word) ---
    def mov_imm(self, imm, n):  self.w(0xE000 | (n << 8) | (imm & 0xff))
    def mov(self, m, n):        self.w(0x6003 | (n << 8) | (m << 4))
    def movb_at(self, m, n):    self.w(0x6000 | (n << 8) | (m << 4))   # mov.b @Rm,Rn (sign-ext)
    def movb_disp_r0(self, d, n): self.w(0x8400 | (n << 4) | (d & 0xf))  # mov.b @(d,Rn),R0
    def extub(self, m, n):      self.w(0x600C | (n << 8) | (m << 4))
    def movl_at(self, m, n):    self.w(0x6002 | (n << 8) | (m << 4))   # mov.l @Rm,Rn
    def movl_to(self, m, n):    self.w(0x2002 | (n << 8) | (m << 4))   # mov.l Rm,@Rn
    def movw_to(self, m, n):    self.w(0x2001 | (n << 8) | (m << 4))   # mov.w Rm,@Rn
    def movb_to(self, m, n):    self.w(0x2000 | (n << 8) | (m << 4))   # mov.b Rm,@Rn
    def movl_push(self, m):     self.w(0x2F06 | (m << 4))              # mov.l Rm,@-r15
    def movl_pop(self, n):      self.w(0x60F6 | (n << 8))              # mov.l @r15+,Rn
    def add_imm(self, imm, n):  self.w(0x7000 | (n << 8) | (imm & 0xff))
    def add(self, m, n):        self.w(0x300C | (n << 8) | (m << 4))   # add Rm,Rn
    def neg(self, m, n):        self.w(0x600B | (n << 8) | (m << 4))
    def shll(self, n):          self.w(0x4000 | (n << 8))
    def shlr8(self, n):         self.w(0x4019 | (n << 8))
    def cmp_eq_imm(self, imm):  self.w(0x8800 | (imm & 0xff))         # cmp/eq #imm,r0
    def cmp_pz(self, n):        self.w(0x4011 | (n << 8))
    def cmp_gt(self, m, n):     self.w(0x3007 | (n << 8) | (m << 4))  # T = Rn > Rm (signed)
    def cmp_hs(self, m, n):     self.w(0x3002 | (n << 8) | (m << 4))  # T = Rn >= Rm (unsigned)
    def jmp(self, n):           self.w(0x402B | (n << 8))
    def nop(self):              self.w(0x0009)
    def bf(self, lab):  self._brc(0x8B00, lab)
    def bt(self, lab):  self._brc(0x8900, lab)
    def bra(self, lab): self._bra(lab)
    def _brc(self, op, lab):
        idx = len(self.ins)
        def fn(pc, A, op=op, lab=lab, idx=idx):
            tgt = A.addr_of_ins(A.labels[lab][1]); disp = (tgt - (pc + 4)) >> 1
            assert -128 <= disp <= 127, "cond branch out of range"
            return bytes([op >> 8, disp & 0xff])
        self.emit(fn)
    def _bra(self, lab):
        def fn(pc, A, lab=lab):
            tgt = A.addr_of_ins(A.labels[lab][1]); disp = (tgt - (pc + 4)) >> 1
            assert -2048 <= disp <= 2047, "bra out of range"
            return bytes([0xA0 | ((disp >> 8) & 0x0f), disp & 0xff])
        self.emit(fn)
    # load a 32-bit constant into Rn via PC-relative literal pool
    def movl_lit(self, value, n):
        li = len(self.longs); self.longs.append(value)
        def fn(pc, A, n=n, li=li):
            tgt = A.long_addr(li); disp = (tgt - ((pc & ~3) + 4)) >> 2
            assert 0 <= disp <= 255, "literal out of range disp=%d" % disp
            return bytes([0xD0 | n, disp & 0xff])
        self.emit(fn)
    # ---- layout ----
    def addr_of_ins(self, i): return self.org + i * 2
    def long_addr(self, li):
        end = self.org + len(self.ins) * 2
        end = (end + 3) & ~3
        return end + li * 4
    def assemble(self):
        out = bytearray()
        for i, fn in enumerate(self.ins):
            out += fn(self.org + i * 2, self)
        while len(out) % 4: out += b'\x00\x00'   # align pool
        import struct
        for v in self.longs: out += struct.pack(">I", v)
        return bytes(out)

# ---------------------------------------------------------------- the routine
def build_routine():
    """Runs inside the controller poll (OREG valid). Reads 3D-pad analog from OREG,
    moves the cursor, then replays the 5 displaced poll instructions and jumps back."""
    a = Asm(RELOC)
    if DEBUG_MOVER:
        # DEBUG at mover hook (proven per-frame): record OREG1/4/5 + counter to 0x06027c60..63
        for oreg, dst in ((OREG_ID, 0x06027c60), (OREG_X, 0x06027c61), (OREG_Y, 0x06027c62)):
            a.movl_lit(oreg, 1); a.movb_at(1, 0); a.movl_lit(dst, 1); a.movb_to(0, 1)
        a.movl_lit(0x06027c63, 1); a.movb_at(1, 0); a.add_imm(1, 0); a.movb_to(0, 1)
        for r in (14, 13, 12, 11, 10, 9): a.movl_push(r)   # 6 displaced mover pushes
        a.movl_lit(MOVER_RESUME, 0); a.jmp(0); a.nop()
        return a.assemble()
    if int(os.environ.get("MYST_DEBUG_BUF", "0")):
        # DEBUG at poll hook: inspect buffer *(0x0605adb0). Scratch 0x06027c60..66:
        # [60]=buf[6] last ID  [61]=buf[3] last X  [62]=buf[2] last Y  [63]=fire counter
        # [64]=count of buf[6]==0x16  [65]=latched X@0x16  [66]=latched Y@0x16
        a.movl_push(0); a.movl_push(1); a.movl_push(2)
        a.movl_lit(POLL_BUF_PTR, 1); a.movl_at(1, 1)        # r1 = *(0x0605adb0)
        for boff, dst in ((6, 0x06027c60), (3, 0x06027c61), (2, 0x06027c62)):
            a.movb_disp_r0(boff, 1); a.movl_lit(dst, 2); a.movb_to(0, 2)
        a.movl_lit(0x06027c63, 2); a.movb_at(2, 0); a.add_imm(1, 0); a.movb_to(0, 2)
        a.movb_disp_r0(6, 1); a.extub(0, 0); a.cmp_eq_imm(0x16); a.bf('nanalog')
        a.movl_lit(0x06027c64, 2); a.movb_at(2, 0); a.add_imm(1, 0); a.movb_to(0, 2)
        a.movb_disp_r0(3, 1); a.movl_lit(0x06027c65, 2); a.movb_to(0, 2)
        a.movb_disp_r0(2, 1); a.movl_lit(0x06027c66, 2); a.movb_to(0, 2)
        a.label('nanalog')
        a.movl_pop(2); a.movl_pop(1); a.movl_pop(0)
        a.movl_lit(ADA0, 2); a.movb_to(13, 2); a.mov_imm(2, 1); a.movb_to(1, 11)
        a.movl_lit(POLL_RESUME, 0); a.jmp(0); a.nop()
        return a.assemble()
    if int(os.environ.get("MYST_DEBUG_POLL", "0")):
        # DEBUG: every time the hook fires, record what OREG holds, to scratch 0x06027c60..63.
        # [0x60]=OREG1(ID) [0x61]=OREG4(X) [0x62]=OREG5(Y) [0x63]=fire counter
        a.movl_push(0); a.movl_push(1)
        for oreg, dst in ((OREG_ID, 0x06027c60), (OREG_X, 0x06027c61), (OREG_Y, 0x06027c62)):
            a.movl_lit(oreg, 1); a.movb_at(1, 0); a.movl_lit(dst, 1); a.movb_to(0, 1)
        a.movl_lit(0x06027c63, 1); a.movb_at(1, 0); a.add_imm(1, 0); a.movb_to(0, 1)
        a.movl_pop(1); a.movl_pop(0)
        a.movl_lit(ADA0, 2); a.movb_to(13, 2); a.mov_imm(2, 1); a.movb_to(1, 11)
        a.movl_lit(POLL_RESUME, 0); a.jmp(0); a.nop()
        return a.assemble()
    # === v0.4 MOVER routine (hook 0x0601d144): read latched analog scratch -> move cursor ===
    SAVE = (0, 1, 2, 3, 4)
    for r in SAVE: a.movl_push(r)
    a.movl_lit(FLAG_SCRATCH, 1); a.movb_at(1, 0); a.extub(0, 0)
    a.cmp_eq_imm(0); a.bt('mskip')                      # FLAG==0 (no analog seen) -> skip

    def axis(tag, ascr, wptr, dptr, vmax):
        a.movl_lit(ascr, 1); a.movb_at(1, 0); a.extub(0, 0); a.add_imm(-128, 0)  # r0 = scr-0x80
        a.mov(0, 3); a.cmp_pz(0); a.bt(f'p_{tag}'); a.neg(0, 3)
        a.label(f'p_{tag}')
        a.mov_imm(DEADZONE, 2); a.cmp_gt(2, 3); a.bf(f'd_{tag}')
        for _ in range(GAIN_SHIFT): a.shll(0)
        a.bra(f'a_{tag}'); a.nop()
        a.label(f'd_{tag}'); a.mov_imm(0, 0)
        a.label(f'a_{tag}')
        a.movl_lit(wptr, 1); a.movl_at(1, 2); a.add(0, 2)
        a.cmp_pz(2); a.bt(f'lo_{tag}'); a.mov_imm(0, 2)
        a.label(f'lo_{tag}')
        a.movl_lit(vmax, 4); a.cmp_gt(4, 2); a.bf(f'hi_{tag}'); a.mov(4, 2)
        a.label(f'hi_{tag}')
        a.movl_to(2, 1); a.mov(2, 0); a.shlr8(0)
        a.movl_lit(dptr, 1); a.movw_to(0, 1)

    axis('x', AX_SCRATCH, CUR_XW, CUR_XD, XMAX)
    axis('y', AY_SCRATCH, CUR_YW, CUR_YD, YMAX)

    a.label('mskip')
    for r in reversed(SAVE): a.movl_pop(r)
    for r in (14, 13, 12, 11, 10, 9): a.movl_push(r)    # 6 displaced mover pushes
    a.movl_lit(MOVER_RESUME, 0); a.jmp(0); a.nop()
    return a.assemble()


def build_capture_routine():
    """v0.4 capture (hook 0x06048038, inside poll loop): if live OREG1==0x16 latch analog
    X/Y to scratch, then replay loop control + the loop-exit code. Loop reloads r0-r3 each
    iteration so we only must preserve r4 (counter), r11, r13 (used by the exit code)."""
    a = Asm(RELOC2)
    # DIAG: total fire counter @0x06027c63 (++ every loop iteration through this hook)
    a.movl_lit(0x06027c63, 5); a.movb_at(5, 1); a.add_imm(1, 1); a.movb_to(1, 5)
    # DIAG: record live OREG1 -> 0x06027c65, OREG4 -> 0x06027c66 (last seen at this hook)
    a.movl_lit(OREG_ID, 5); a.movb_at(5, 1); a.movl_lit(0x06027c65, 5); a.movb_to(1, 5)
    a.movl_lit(OREG_X, 5); a.movb_at(5, 1); a.movl_lit(0x06027c66, 5); a.movb_to(1, 5)
    # latch when OREG1==0x16
    a.movl_lit(OREG_ID, 5); a.movb_at(5, 0); a.extub(0, 0); a.cmp_eq_imm(0x16); a.bf('ncap')
    a.movl_lit(0x06027c64, 5); a.movb_at(5, 1); a.add_imm(1, 1); a.movb_to(1, 5)  # DIAG: 0x16 count
    a.movl_lit(OREG_X, 5); a.movb_at(5, 1); a.movl_lit(AX_SCRATCH, 5); a.movb_to(1, 5)
    a.movl_lit(OREG_Y, 5); a.movb_at(5, 1); a.movl_lit(AY_SCRATCH, 5); a.movb_to(1, 5)
    a.movl_lit(FLAG_SCRATCH, 5); a.mov_imm(1, 1); a.movb_to(1, 5)
    a.label('ncap')
    # replay loop control (orig 0x06048038: mov #7,r3 ; cmp/hs r3,r4 ; bf LOOP_CONT)
    a.mov_imm(7, 3); a.cmp_hs(3, 4); a.bt('texit')      # r4>=7 -> exit
    a.movl_lit(LOOP_CONT, 5); a.jmp(5); a.nop()         # else loop back
    a.label('texit')
    # replay exit (orig 0x0604803e): r2=0x0605ada0 ; *r2=r13 ; r1=2 ; *r11=r1 ; bra 0x06048580
    a.movl_lit(ADA0, 2); a.movb_to(13, 2)
    a.mov_imm(2, 1); a.movb_to(1, 11)
    a.movl_lit(POLL_RESUME, 5); a.jmp(5); a.nop()
    return a.assemble()

import struct

def tramp12(reloc):
    # mov.l @(1,pc),r0 ; jmp @r0 ; nop ; nop ; .long reloc   (PC must be 4-aligned)
    return bytes([0xD0, 0x01, 0x40, 0x2B, 0x00, 0x09, 0x00, 0x09]) + struct.pack(">I", reloc)

def tramp10(reloc):
    # mov.l @(1,pc),r0 ; jmp @r0 ; nop ; .long reloc   (does NOT touch 0x06048048)
    return bytes([0xD0, 0x01, 0x40, 0x2B, 0x00, 0x09]) + struct.pack(">I", reloc)

DEBUG_ANY = DEBUG_MOVER or int(os.environ.get("MYST_DEBUG_POLL", "0")) or int(os.environ.get("MYST_DEBUG_BUF", "0"))

# ---------------------------------------------------------------- apply/verify
def _place(a, addr, blob, label):
    o = addr - BASE
    assert all(b == 0 for b in a[o:o + len(blob)]), "%s region not free @0x%08x!" % (label, addr)
    a[o:o + len(blob)] = blob

def main():
    here = os.path.dirname(__file__)
    src = os.path.join(here, "..", "work", "myst", "A.BIN")
    a = bytearray(open(src, "rb").read())
    if DEBUG_ANY:
        routine = build_routine()
        _place(a, RELOC, routine, "reloc")
        tramp = tramp12(RELOC) if DEBUG_MOVER else tramp10(RELOC)
        a[HOOK - BASE:HOOK - BASE + len(tramp)] = tramp
        print("DEBUG routine %d B @0x%08x ; tramp @0x%08x" % (len(routine), RELOC, HOOK))
    else:
        # v0.4 two-hook production: capture (poll loop) latches analog -> mover reads it
        mover = build_routine(); cap = build_capture_routine()
        _place(a, RELOC, mover, "mover")
        _place(a, RELOC2, cap, "capture")
        a[MOVER_HOOK - BASE:MOVER_HOOK - BASE + 12] = tramp12(RELOC)
        a[LOOP_HOOK - BASE:LOOP_HOOK - BASE + 12] = tramp12(RELOC2)
        print("mover %d B @0x%08x (hook 0x%08x) ; capture %d B @0x%08x (hook 0x%08x)" %
              (len(mover), RELOC, MOVER_HOOK, len(cap), RELOC2, LOOP_HOOK))
    out = os.path.join(here, "..", "work", "myst", "A.patched.BIN")
    open(out, "wb").write(a)
    print("wrote", out)

if __name__ == "__main__":
    main()
