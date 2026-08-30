#!/usr/bin/env python3
"""EXPERIMENT v2: English NOUN in the command box WITH a working parser (split the two reads).

v1 (zork_test_nounbox.py) redirected the noun-table literal @0x0601fbc8 -> English: the box showed
MAILBOX but "open MAILBOX" -> parser rejects ("わかりません"). Proof that FUN_0601fb40 uses that
table for BOTH the parse word (1st read 0x601fb6c -> FUN_0601b6f8) AND the box display (2nd read
0x601fb84 -> builder FUN_0601ae92). So we must split them.

Fix: keep @0x0601fbc8 = stock JP (so the parse read stays Japanese -> parser works), and redirect
ONLY the display call. The display call loads the builder address from literal @0x0601fbd0 (=0x601ae92)
then `jsr`. We repoint THAT literal to a tiny TRAMPOLINE in the pool that overrides r4 with the
English string then tail-jumps to the real builder:

    mov r2,r0          ; r0 = noun index (r2 still holds it here)
    shll2 r0           ; r0 = index*4
    mov.l @(eng),r3    ; r3 = English noun-pointer table base
    mov.l @(r0,r3),r4  ; r4 = ENG_TABLE[index]  (override the JP r4)
    mov.l @(bld),r0    ; r0 = 0x0601ae92 (real builder)
    jmp @r0            ; tail-call; builder rts -> back into 0601fb40
    nop
    eng: .long ENG_TABLE
    bld: .long 0x0601ae92

In-place patch of 0ZORK in a stock disc copy (pool 0x64f34, ECC fixed). Output: game_patched/zork1_nounbox2/.
"""
import os, shutil, struct, sys
HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..")); sys.path.insert(0, HERE)
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage
from zork_zvoctbl_patch import READING_EN

ROOT = os.path.join(HERE, "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_nounbox2")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (nounbox2) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (nounbox2).cue")
SS, DO, ROOTDIR_LBA = 2352, 16, 20
BASE = 0x06004000
RUN1_FOFF, N1 = 0x789bc, 364
LIT_BUILDER = 0x1bbd0          # file off of literal @0x0601fbd0 (= 0x0601ae92)
BUILDER = 0x0601ae92
POOL = 0x64f34
POOL_END = POOL + 0xD000


def enc_fw(s):
    out = bytearray()
    for c in s.upper():
        o = ord(c)
        if c == " ":            out += b"\x81\x40"
        elif "0" <= c <= "9":   out += bytes((0x82, 0x4f + o - 0x30))
        elif "A" <= c <= "Z":   out += bytes((0x82, 0x60 + o - 0x41))
        else:                   out += b"\x81\x40"
    out.append(0x00)
    return bytes(out)


def find_dir_entry(tb, name):
    raw = b"".join(tb[(ROOTDIR_LBA + s) * SS + DO:(ROOTDIR_LBA + s) * SS + DO + 2048] for s in range(2))
    i = 0
    while i < len(raw):
        ln = raw[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048; continue
        nlen = raw[i + 32]; nm = raw[i + 33:i + 33 + nlen]
        if nm.split(b";")[0] == name.encode():
            return struct.unpack("<I", raw[i + 2:i + 6])[0], struct.unpack("<I", raw[i + 10:i + 14])[0]
        i += ln
    raise KeyError(name)


def patch_zork(z):
    cur = POOL
    # 1) English strings + index-aligned pointer table
    str_addr = {}; ptrs = []; n_tr = 0
    for i in range(N1):
        v = struct.unpack_from(">I", z, RUN1_FOFF + i * 4)[0]; f = v - BASE; en = None
        if 0 <= f < len(z):
            e = f
            while e < len(z) and z[e] != 0: e += 1
            try: en = READING_EN.get(z[f:e].decode("shift_jis"))
            except Exception: en = None
        if en:
            if en not in str_addr:
                blob = enc_fw(en); z[cur:cur + len(blob)] = blob; str_addr[en] = BASE + cur; cur += len(blob)
            ptrs.append(str_addr[en]); n_tr += 1
        else:
            ptrs.append(v)
    cur = (cur + 3) & ~3
    eng_table = BASE + cur
    for p in ptrs:
        struct.pack_into(">I", z, cur, p); cur += 4
    # 2) trampoline (4-byte aligned)
    cur = (cur + 3) & ~3
    tramp = BASE + cur
    code = bytes([0x60, 0x23,   # mov r2,r0
                  0x40, 0x08,   # shll2 r0
                  0xd3, 0x02,   # mov.l @(2,pc),r3  -> eng  (tramp+16)
                  0x04, 0x3e,   # mov.l @(r0,r3),r4
                  0xd0, 0x02,   # mov.l @(2,pc),r0  -> bld  (tramp+20)
                  0x40, 0x2b,   # jmp @r0
                  0x00, 0x09,   # nop (delay slot)
                  0x00, 0x09])  # nop (pad to align data)
    z[cur:cur + len(code)] = code; cur += len(code)
    struct.pack_into(">I", z, cur, eng_table); cur += 4   # eng @ tramp+16
    struct.pack_into(">I", z, cur, BUILDER);   cur += 4   # bld @ tramp+20
    # 3) repoint the display-call builder literal to the trampoline (parse read 0x601fbc8 stays JP)
    struct.pack_into(">I", z, LIT_BUILDER, tramp)
    if cur > POOL_END:
        raise RuntimeError("pool overflow")
    return eng_table, tramp, n_tr


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    lba, size = find_dir_entry(tb, "0ZORK.BIN")
    nsec = -(-size // 2048)
    z = bytearray(b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] for s in range(nsec))[:size])

    assert struct.unpack_from(">I", z, LIT_BUILDER)[0] == BUILDER, "builder literal mismatch"
    eng_table, tramp, n_tr = patch_zork(z)
    print("ENG table @0x%08x ; trampoline @0x%08x ; %d/%d nouns English ; @0x0601fbd0 -> trampoline"
          % (eng_table, tramp, n_tr, N1))

    for s in range(nsec):
        chunk = bytearray(z[s * 2048:min((s + 1) * 2048, size)]); chunk += b"\x00" * (2048 - len(chunk))
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] = chunk
        sec = bytearray(tb[(lba + s) * SS:(lba + s) * SS + SS]); ecc.fix_sector(sec)
        tb[(lba + s) * SS:(lba + s) * SS + SS] = sec
    open(OUT_T1, "wb").write(tb)

    lines = ['FILE "%s" BINARY' % os.path.basename(OUT_T1), '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00']
    for n in range(2, 33):
        fn = "Zork I - The Great Underground Empire (Japan) (Track %02d).bin" % n
        dst = os.path.join(OUTDIR, fn)
        if not os.path.exists(dst):
            try: os.link(os.path.join(ZDIR, fn), dst)
            except OSError: shutil.copyfile(os.path.join(ZDIR, fn), dst)
        lines += ['FILE "%s" BINARY' % fn, '  TRACK %02d AUDIO' % n,
                  '    INDEX 00 00:00:00', '    INDEX 01 00:02:00' if n == 2 else '    INDEX 01 00:01:74']
    open(OUT_CUE, "w", newline="\r\n").write("\n".join(lines) + "\n")
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
