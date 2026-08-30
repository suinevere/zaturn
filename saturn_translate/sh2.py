"""Hitachi SH-2 disassembler (big-endian, 16-bit fixed instructions).

The Sega Saturn's CPUs are SH-2. To reverse asset codecs (compression, font/script
loaders) inside a Saturn executable without Ghidra, this module decodes SH-2
instructions to assembly text. A companion interpreter (:mod:`saturn_translate.sh2cpu`)
can then *execute* a routine to recover its output byte-exact.

Coverage is the full common SH-2 user ISA: MOV family (immediate / register /
displacement / indexed / pre-dec / post-inc), arithmetic, logic, shifts/rotates,
compares, branches (incl. delayed), system-register moves, and MAC/MUL. Unknown
encodings disassemble as ``.word 0xXXXX`` rather than failing.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class Insn:
    addr: int
    word: int
    text: str
    # control-flow hints (for analysis)
    is_branch: bool = False
    is_call: bool = False
    is_ret: bool = False
    delayed: bool = False
    target: int | None = None  # absolute target for PC-relative branches


def _s8(v: int) -> int:
    return v - 0x100 if v & 0x80 else v


def _s12(v: int) -> int:
    return v - 0x1000 if v & 0x800 else v


def disasm(word: int, addr: int = 0) -> Insn:
    """Decode one 16-bit SH-2 instruction at ``addr``."""
    n = (word >> 8) & 0xF
    m = (word >> 4) & 0xF
    d = word & 0xF
    d8 = word & 0xFF
    d12 = word & 0xFFF
    op = word >> 12

    def I(text, **kw):
        return Insn(addr, word, text, **kw)

    if op == 0x0:
        if word == 0x0008: return I("clrt")
        if word == 0x0009: return I("nop")
        if word == 0x000b: return I("rts", is_ret=True, delayed=True)
        if word == 0x0018: return I("sett")
        if word == 0x0019: return I("div0u")
        if word == 0x001b: return I("sleep")
        if word == 0x0028: return I("clrmac")
        if word == 0x002b: return I("rte", is_ret=True, delayed=True)
        if d == 0x2:
            sys = {0:"sr",1:"gbr",2:"vbr"}.get(m)
            if sys: return I(f"stc {sys},r{n}")
        if d == 0x3:
            if m == 0x0: return I(f"bsrf r{n}", is_call=True, delayed=True)
            if m == 0x2: return I(f"braf r{n}", is_branch=True, delayed=True)
        if d == 0x4: return I(f"mov.b r{m},@(r0,r{n})")
        if d == 0x5: return I(f"mov.w r{m},@(r0,r{n})")
        if d == 0x6: return I(f"mov.l r{m},@(r0,r{n})")
        if d == 0x7: return I(f"mul.l r{m},r{n}")
        if d == 0xa:
            srcs = {0:"mach",1:"macl",2:"pr"}.get(m)
            if srcs: return I(f"sts {srcs},r{n}")
        if word & 0xF0FF == 0x0029: return I(f"movt r{n}")
        if d == 0xc: return I(f"mov.b @(r0,r{m}),r{n}")
        if d == 0xd: return I(f"mov.w @(r0,r{m}),r{n}")
        if d == 0xe: return I(f"mov.l @(r0,r{m}),r{n}")
        if d == 0xf: return I(f"mac.l @r{m}+,@r{n}+")
        return I(f".word 0x{word:04x}")

    if op == 0x1:
        return I(f"mov.l r{m},@({d*4},r{n})")

    if op == 0x2:
        sub = {0:f"mov.b r{m},@r{n}",1:f"mov.w r{m},@r{n}",2:f"mov.l r{m},@r{n}",
               4:f"mov.b r{m},@-r{n}",5:f"mov.w r{m},@-r{n}",6:f"mov.l r{m},@-r{n}",
               7:f"div0s r{m},r{n}",8:f"tst r{m},r{n}",9:f"and r{m},r{n}",
               0xa:f"xor r{m},r{n}",0xb:f"or r{m},r{n}",0xc:f"cmp/str r{m},r{n}",
               0xd:f"xtrct r{m},r{n}",0xe:f"mulu.w r{m},r{n}",0xf:f"muls.w r{m},r{n}"}
        return I(sub.get(d, f".word 0x{word:04x}"))

    if op == 0x3:
        sub = {0:f"cmp/eq r{m},r{n}",2:f"cmp/hs r{m},r{n}",3:f"cmp/ge r{m},r{n}",
               4:f"div1 r{m},r{n}",5:f"dmulu.l r{m},r{n}",6:f"cmp/hi r{m},r{n}",
               7:f"cmp/gt r{m},r{n}",8:f"sub r{m},r{n}",0xa:f"subc r{m},r{n}",
               0xb:f"subv r{m},r{n}",0xc:f"add r{m},r{n}",0xd:f"dmuls.l r{m},r{n}",
               0xe:f"addc r{m},r{n}",0xf:f"addv r{m},r{n}"}
        return I(sub.get(d, f".word 0x{word:04x}"))

    if op == 0x4:
        # shifts / rotates / system regs, decided by low byte
        lo = word & 0xFF
        one = {0x00:f"shll r{n}",0x01:f"shlr r{n}",0x02:f"sts.l mach,@-r{n}",
               0x03:f"stc.l sr,@-r{n}",0x04:f"rotl r{n}",0x05:f"rotr r{n}",
               0x06:f"lds.l @r{n}+,mach",0x07:f"ldc.l @r{n}+,sr",0x08:f"shll2 r{n}",
               0x09:f"shlr2 r{n}",0x0a:f"lds r{n},mach",0x0e:f"ldc r{n},sr",
               0x10:f"dt r{n}",0x11:f"cmp/pz r{n}",0x12:f"sts.l macl,@-r{n}",
               0x13:f"stc.l gbr,@-r{n}",0x15:f"cmp/pl r{n}",0x16:f"lds.l @r{n}+,macl",
               0x17:f"ldc.l @r{n}+,gbr",0x18:f"shll8 r{n}",0x19:f"shlr8 r{n}",
               0x1a:f"lds r{n},macl",0x1b:f"tas.b @r{n}",0x1e:f"ldc r{n},gbr",
               0x20:f"shal r{n}",0x21:f"shar r{n}",0x22:f"sts.l pr,@-r{n}",
               0x23:f"stc.l vbr,@-r{n}",0x24:f"rotcl r{n}",0x25:f"rotcr r{n}",
               0x26:f"lds.l @r{n}+,pr",0x27:f"ldc.l @r{n}+,vbr",0x28:f"shll16 r{n}",
               0x29:f"shlr16 r{n}",0x2a:f"lds r{n},pr",0x2e:f"ldc r{n},vbr"}
        if lo == 0x0b: return I(f"jsr @r{n}", is_call=True, delayed=True)
        if lo == 0x2b: return I(f"jmp @r{n}", is_branch=True, delayed=True)
        if lo in one: return I(one[lo])
        if d == 0xc: return I(f"shad r{m},r{n}")
        if d == 0xd: return I(f"shld r{m},r{n}")
        if d == 0xf: return I(f"mac.w @r{m}+,@r{n}+")
        return I(f".word 0x{word:04x}")

    if op == 0x5:
        return I(f"mov.l @({d*4},r{m}),r{n}")

    if op == 0x6:
        sub = {0:f"mov.b @r{m},r{n}",1:f"mov.w @r{m},r{n}",2:f"mov.l @r{m},r{n}",
               3:f"mov r{m},r{n}",4:f"mov.b @r{m}+,r{n}",5:f"mov.w @r{m}+,r{n}",
               6:f"mov.l @r{m}+,r{n}",7:f"not r{m},r{n}",8:f"swap.b r{m},r{n}",
               9:f"swap.w r{m},r{n}",0xa:f"negc r{m},r{n}",0xb:f"neg r{m},r{n}",
               0xc:f"extu.b r{m},r{n}",0xd:f"extu.w r{m},r{n}",0xe:f"exts.b r{m},r{n}",
               0xf:f"exts.w r{m},r{n}"}
        return I(sub.get(d, f".word 0x{word:04x}"))

    if op == 0x7:
        return I(f"add #{_s8(d8)},r{n}")

    if op == 0x8:
        return _op8(word, addr)

    if op == 0x9:
        tgt = addr + 4 + (d8 * 2)
        return I(f"mov.w @(0x{tgt:08x}),r{n}  ; pc-rel", target=tgt)

    if op == 0xa:
        tgt = addr + 4 + _s12(d12) * 2
        return I(f"bra 0x{tgt:08x}", is_branch=True, delayed=True, target=tgt)

    if op == 0xb:
        tgt = addr + 4 + _s12(d12) * 2
        return I(f"bsr 0x{tgt:08x}", is_call=True, delayed=True, target=tgt)

    if op == 0xc:
        sub = n  # high nibble of low byte selects
        table = {
            0x0: f"mov.b r0,@({d8},gbr)", 0x1: f"mov.w r0,@({d8},gbr)",
            0x2: f"mov.l r0,@({d8},gbr)", 0x3: f"trapa #{d8}",
            0x4: f"mov.b @({d8},gbr),r0", 0x5: f"mov.w @({d8},gbr),r0",
            0x6: f"mov.l @({d8},gbr),r0", 0x7: f"mova @(0x{addr+4+d8*4:08x}),r0",
            0x8: f"tst #{d8},r0", 0x9: f"and #{d8},r0", 0xa: f"xor #{d8},r0",
            0xb: f"or #{d8},r0", 0xc: f"tst.b #{d8},@(r0,gbr)",
            0xd: f"and.b #{d8},@(r0,gbr)", 0xe: f"xor.b #{d8},@(r0,gbr)",
            0xf: f"or.b #{d8},@(r0,gbr)",
        }
        return I(table.get(n, f".word 0x{word:04x}"))

    if op == 0xd:
        tgt = addr + 4 + (d8 * 4)
        return I(f"mov.l @(0x{tgt & ~3:08x}),r{n}  ; pc-rel", target=tgt & ~3)

    if op == 0xe:
        return I(f"mov #{_s8(d8)},r{n}")

    if op == 0xf:
        return I(f".word 0x{word:04x}  ; fpu?")

    return I(f".word 0x{word:04x}")


def _op8(word: int, addr: int) -> Insn:
    n = (word >> 8) & 0xF   # subop
    m = (word >> 4) & 0xF
    d = word & 0xF
    if n == 0x0: return Insn(addr, word, f"mov.b r0,@({d},r{m})")
    if n == 0x1: return Insn(addr, word, f"mov.w r0,@({d*2},r{m})")
    if n == 0x4: return Insn(addr, word, f"mov.b @({d},r{m}),r0")
    if n == 0x5: return Insn(addr, word, f"mov.w @({d*2},r{m}),r0")
    if n == 0x8: return Insn(addr, word, f"cmp/eq #{_s8(word & 0xFF)},r0")
    if n == 0x9:
        tgt = addr + 4 + _s8(word & 0xFF) * 2
        return Insn(addr, word, f"bt 0x{tgt:08x}", is_branch=True, target=tgt)
    if n == 0xb:
        tgt = addr + 4 + _s8(word & 0xFF) * 2
        return Insn(addr, word, f"bf 0x{tgt:08x}", is_branch=True, target=tgt)
    if n == 0xd:
        tgt = addr + 4 + _s8(word & 0xFF) * 2
        return Insn(addr, word, f"bt/s 0x{tgt:08x}", is_branch=True, delayed=True, target=tgt)
    if n == 0xf:
        tgt = addr + 4 + _s8(word & 0xFF) * 2
        return Insn(addr, word, f"bf/s 0x{tgt:08x}", is_branch=True, delayed=True, target=tgt)
    return Insn(addr, word, f".word 0x{word:04x}")


def disasm_range(data: bytes, start: int, count: int, base: int = 0) -> list[Insn]:
    """Disassemble ``count`` instructions from file offset ``start``.

    ``base`` is the address the file is loaded at, so printed addresses and
    PC-relative targets are absolute load addresses.
    """
    out = []
    for k in range(count):
        off = start + k * 2
        if off + 2 > len(data):
            break
        word = (data[off] << 8) | data[off + 1]
        out.append(disasm(word, base + off))
    return out


def format_listing(insns: list[Insn]) -> str:
    return "\n".join(f"  {i.addr:08x}: {i.word:04x}  {i.text}" for i in insns)
