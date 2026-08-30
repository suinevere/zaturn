#!/usr/bin/env python3
"""EXPERIMENT: English NOUN in the command box, parser untouched (parallel display table).

Watchpoints proved the command box (060ae000/060ae080) is DISPLAY-ONLY; the parser uses a
pre-stored id from the resident selection scan, which reads the real noun table 0607c9bc via its
OWN reference (FUN_0601fcaa). The selection handler FUN_0601fb40 reads the noun table via the
literal @0601fbc8 (=0607c9bc) only to copy the word string into the display box.

So: build a PARALLEL English noun-pointer table (index-aligned to run1; entry i -> English of
run1[i], full-width SJIS so it renders via the JP font path; untranslated entries keep the stock JP
pointer) and repoint ONLY @0601fbc8 to it. Prediction: the command box shows English nouns while the
scan still reads stock JP 0607c9bc -> commands execute. STOCK otherwise (no translations / Option C).

In-place: the table + strings go in 0ZORK's free pool (0x64f34, zero-filled), one literal repointed;
size unchanged, ECC fixed. Output: game_patched/zork1_nounbox/.
"""
import os, shutil, struct
sys_path = os.path.dirname(__file__)
import sys; sys.path.insert(0, os.path.join(sys_path, "..")); sys.path.insert(0, sys_path)
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage
from zork_zvoctbl_patch import READING_EN

ROOT = os.path.join(sys_path, "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_nounbox")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (nounbox) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (nounbox).cue")
SS, DO, ROOTDIR_LBA = 2352, 16, 20
BASE = 0x06004000
RUN1_FOFF = 0x789bc            # file offset of noun table run1 (addr 0x0607c9bc), 364 entries
N1 = 364
CMD_LIT_RUN1 = 0x1bbc8        # file offset of literal @0x0601fbc8 (= 0x0607c9bc)
POOL = 0x64f34                # free zero region inside 0ZORK (addr 0x06068f34)
POOL_END = POOL + 0xD000


def enc_fw(s):                # full-width SJIS, NUL-terminated (renders via JP 2-byte font path)
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


def build_eng_table(z):
    """z = 0ZORK bytes (mutable). Returns (eng_table_addr, n_translated). Writes into the pool."""
    cur = POOL
    str_addr = {}            # english word -> pool addr (dedup)
    n_tr = 0
    ptrs = []
    for i in range(N1):
        v = struct.unpack_from(">I", z, RUN1_FOFF + i * 4)[0]
        f = v - BASE
        en = None
        if 0 <= f < len(z):
            e = f
            while e < len(z) and z[e] != 0:
                e += 1
            try:
                en = READING_EN.get(z[f:e].decode("shift_jis"))
            except Exception:
                en = None
        if en:
            if en not in str_addr:
                blob = enc_fw(en)
                if cur + len(blob) > POOL_END:
                    raise RuntimeError("pool full")
                z[cur:cur + len(blob)] = blob
                str_addr[en] = BASE + cur
                cur += len(blob)
            ptrs.append(str_addr[en]); n_tr += 1
        else:
            ptrs.append(v)                      # keep stock JP pointer
    cur = (cur + 3) & ~3
    table_addr = BASE + cur
    for p in ptrs:
        struct.pack_into(">I", z, cur, p); cur += 4
    return table_addr, n_tr


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    lba, size = find_dir_entry(tb, "0ZORK.BIN")
    print("0ZORK.BIN lba=%d size=%d" % (lba, size))

    # pull 0ZORK out, patch it, write back
    z = bytearray(b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] for s in range(-(-size // 2048)))[:size])
    table_addr, n_tr = build_eng_table(z)
    struct.pack_into(">I", z, CMD_LIT_RUN1, table_addr)
    print("ENG noun table @0x%08x (%d translated, %d kept JP); @0x0601fbc8 -> 0x%08x"
          % (table_addr, n_tr, N1 - n_tr, table_addr))

    nsec = -(-size // 2048)
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
