#!/usr/bin/env python3
"""EXPERIMENT: force the dormant English-mode flag (0x060af984) ON.

The Saturn JP Zork ships with a full original-Infocom English noun vocabulary (run2 @0x0607cf6c)
and a language-select flag at 0x060af984 read at ~51 sites (flag!=0 -> English path, ==0 -> JP).
The flag is only ever WRITTEN to 0 (two sites: file 0x5f16 and 0x7798, each `mov #0,r0`=0xE000),
so English mode is never reachable. This patches those two writes to `mov #1,r0` (0xE001) so the
flag becomes 1 in the relevant states -> the command/input path takes the English branch.

In-place 2-byte-each patch of 0ZORK.BIN inside a copy of the stock disc (size unchanged, ECC fixed).
Output: game_patched/zork1_engflag/  -- load this and see if the command system goes English.
This is STOCK otherwise (no translations / Option C) to show the native English mode cleanly.
"""
import os, shutil, struct
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage

ROOT = os.path.join(os.path.dirname(__file__), "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag).cue")
SS, DO, ROOTDIR_LBA = 2352, 16, 20

# 0ZORK.BIN file offsets of the two `mov #0,r0` (E000) language-flag writes -> patch to E001.
PATCHES = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}


def find_dir_entry(tb, name):
    raw = b"".join(tb[(ROOTDIR_LBA + s) * SS + DO:(ROOTDIR_LBA + s) * SS + DO + 2048] for s in range(2))
    i = 0
    while i < len(raw):
        ln = raw[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048; continue
        nlen = raw[i + 32]; nm = raw[i + 33:i + 33 + nlen]
        if nm.split(b";")[0] == name.encode():
            lba = struct.unpack("<I", raw[i + 2:i + 6])[0]
            size = struct.unpack("<I", raw[i + 10:i + 14])[0]
            return lba, size
        i += ln
    raise KeyError(name)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    lba, size = find_dir_entry(tb, "0ZORK.BIN")
    print("0ZORK.BIN lba=%d size=%d" % (lba, size))

    touched_sectors = set()
    for foff, (want, new) in PATCHES.items():
        sec = lba + foff // 2048
        off = foff % 2048
        p = sec * SS + DO + off
        cur = (tb[p] << 8) | tb[p + 1]
        if cur != want:
            raise RuntimeError("file 0x%x: have %04x, expected %04x" % (foff, cur, want))
        tb[p] = new >> 8; tb[p + 1] = new & 0xFF
        touched_sectors.add(sec)
        print("  patched file 0x%x (sector %d): %04x -> %04x" % (foff, sec, want, new))

    for sec in touched_sectors:                       # recompute EDC/ECC for changed sectors
        s = bytearray(tb[sec * SS:sec * SS + SS]); ecc.fix_sector(s)
        tb[sec * SS:sec * SS + SS] = s
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

    v = SaturnImage.from_file(OUT_T1)
    z = v.extract("/0ZORK.BIN")
    ok = all(((z[f] << 8) | z[f + 1]) == new for f, (_w, new) in PATCHES.items())
    print("verify: both patches present in extracted 0ZORK.BIN: %s" % ok)
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
