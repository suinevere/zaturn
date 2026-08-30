#!/usr/bin/env python3
"""EXPERIMENT v3: English command that EXECUTES = English noun table + English ZVOCTBL together.

v1/v2 proved the command box content and the parser input are the SAME string: any English in the
box -> parser receives English -> rejects it against the Japanese ZVOCTBL ("わかりません"). The
missing half is Path B1 (zork_zvoctbl_patch.py): translate ZVOCTBL keywords to English IN PLACE
(same id, same POS) so the parser ACCEPTS the English word. B1 alone did nothing (box still fed JP);
the two only work together.

This build does BOTH, in place (sizes unchanged, ECC fixed):
  * 0ZORK.BIN: repoint the noun-table literal @0x0601fbc8 -> a parallel English noun-pointer table
    (entry i = English of run1[i], full-width SJIS). Box AND parse word become English.
  * ZVOCTBL.DAT: patch_zvoctbl() -> English keywords (same ids) so the parser matches the English.
Both English strings use the same full-width-SJIS enc_fw, so box word == dictionary key byte-for-byte.
Output: game_patched/zork1_nounbox3/.
"""
import os, shutil, struct, sys
HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..")); sys.path.insert(0, HERE)
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage
from zork_zvoctbl_patch import READING_EN, patch_zvoctbl

ROOT = os.path.join(HERE, "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_nounbox3")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (nounbox3) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (nounbox3).cue")
SS, DO, ROOTDIR_LBA = 2352, 16, 20
BASE = 0x06004000
RUN1_FOFF, N1 = 0x789bc, 364
CMD_LIT_RUN1 = 0x1bbc8
POOL, POOL_END = 0x64f34, 0x64f34 + 0xD000


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


def dir_entries(tb):
    raw = b"".join(tb[(ROOTDIR_LBA + s) * SS + DO:(ROOTDIR_LBA + s) * SS + DO + 2048] for s in range(2))
    out = {}; i = 0
    while i < len(raw):
        ln = raw[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048; continue
        nlen = raw[i + 32]; nm = raw[i + 33:i + 33 + nlen].split(b";")[0].decode()
        out[nm] = (struct.unpack("<I", raw[i + 2:i + 6])[0], struct.unpack("<I", raw[i + 10:i + 14])[0])
        i += ln
    return out


def read_file(tb, lba, size):
    n = -(-size // 2048)
    return bytearray(b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] for s in range(n))[:size])


def write_file(tb, lba, data):
    n = -(-len(data) // 2048)
    for s in range(n):
        chunk = bytearray(data[s * 2048:min((s + 1) * 2048, len(data))]); chunk += b"\x00" * (2048 - len(chunk))
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] = chunk
        sec = bytearray(tb[(lba + s) * SS:(lba + s) * SS + SS]); ecc.fix_sector(sec)
        tb[(lba + s) * SS:(lba + s) * SS + SS] = sec


def build_eng_table(z):
    cur = POOL; str_addr = {}; ptrs = []; n_tr = 0
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
    table = BASE + cur
    for p in ptrs:
        struct.pack_into(">I", z, cur, p); cur += 4
    if cur > POOL_END: raise RuntimeError("pool overflow")
    return table, n_tr


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    ents = dir_entries(tb)

    # 1) 0ZORK: English noun table + repoint command-path noun literal
    zlba, zsize = ents["0ZORK.BIN"]
    z = read_file(tb, zlba, zsize)
    table, n_tr = build_eng_table(z)
    struct.pack_into(">I", z, CMD_LIT_RUN1, table)
    write_file(tb, zlba, z)
    print("0ZORK: English noun table @0x%08x (%d/%d English); @0x0601fbc8 -> table" % (table, n_tr, N1))

    # 2) ZVOCTBL: English keywords in place (same ids)
    vlba, vsize = ents["ZVOCTBL.DAT"]
    v = read_file(tb, vlba, vsize)
    v2 = bytearray(patch_zvoctbl(bytes(v)))
    assert len(v2) == vsize, "ZVOCTBL size changed"
    write_file(tb, vlba, v2)
    print("ZVOCTBL: patched to English keywords in place (size %d)" % vsize)

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
