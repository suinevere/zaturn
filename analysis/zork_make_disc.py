#!/usr/bin/env python3
"""Zork I (Saturn JP) — Tier-1 PROOF disc: repoint 3 dict words to English.

Tests the whole pipeline (relocate + repoint + ASCII render) on the highest-leverage
strings: the 234-word abbreviation dictionary. `白い家`->WHITE HOUSE, `ドア`->DOOR,
`板`->BOARD. These are referenced everywhere via 0x0e/0x1e, so success shows English in
the opening rooms. English strings go in free padding at end-of-image; dict pointers
(table @file 0x95a68, BE abs ptrs) are rewritten. Then inject changed sectors into a copy
of Track 01 + recompute EDC/ECC. Output: game_patched/zork1_tier1_test/.
"""
import os, struct, shutil
from saturn_translate.iso import SaturnImage
from saturn_translate import ecc
import zork_translate

ROOT = os.path.join(os.path.dirname(__file__), "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
CUE_SRC = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan).cue")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_tier1_test")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (Tier1 test) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (Tier1 test).cue")

SS, DO = 2352, 16


def patch_sector(f, lba, newdata_for_sector):
    f.seek(lba * SS); sec = bytearray(f.read(SS))
    sec[DO:DO + 2048] = newdata_for_sector
    ecc.fix_sector(sec)
    f.seek(lba * SS); f.write(sec)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    patched = zork_translate.build_patched_0zork(verbose=True)
    orig = open(os.path.join(ROOT, "work", "zork1", "0ZORK.BIN"), "rb").read()
    diffs = [i for i in range(len(patched)) if patched[i] != orig[i]]
    changed_secs = sorted({i // 2048 for i in diffs})
    img = SaturnImage.from_file(SRC_T1); e = img.find("/0ZORK.BIN")
    print("0ZORK lba=%d ; changed 0ZORK sectors=%s -> track sectors=%s" %
          (e.lba, changed_secs, [e.lba + s for s in changed_secs]))
    print("copying Track 01 (56 MB)...")
    shutil.copyfile(SRC_T1, OUT_T1)
    with open(OUT_T1, "r+b") as f:
        for s in changed_secs:
            chunk = patched[s * 2048:(s + 1) * 2048]
            chunk = chunk + b"\x00" * (2048 - len(chunk))
            patch_sector(f, e.lba + s, chunk)
    # cue: patched data track + audio tracks as bare filenames (Mednafen blocks slashed paths).
    # Hardlink the original audio tracks into OUTDIR (instant, same volume, no extra space).
    lines = ['FILE "%s" BINARY' % os.path.basename(OUT_T1),
             '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00']
    for n in range(2, 33):
        fn = "Zork I - The Great Underground Empire (Japan) (Track %02d).bin" % n
        dst = os.path.join(OUTDIR, fn)
        if not os.path.exists(dst):
            try: os.link(os.path.join(ZDIR, fn), dst)
            except OSError: shutil.copyfile(os.path.join(ZDIR, fn), dst)
        lines += ['FILE "%s" BINARY' % fn, '  TRACK %02d AUDIO' % n,
                  '    INDEX 00 00:00:00', '    INDEX 01 00:02:00' if n == 2 else '    INDEX 01 00:01:74']
    open(OUT_CUE, "w", newline="\r\n").write("\n".join(lines) + "\n")
    # verify
    v = SaturnImage.from_file(OUT_T1); got = v.extract("/0ZORK.BIN")
    print("patched 0ZORK in disc matches builder:", got == patched)
    with open(OUT_T1, "rb") as f:
        ok = all((f.seek((e.lba + s) * SS), ecc.sector_is_valid(f.read(SS)))[1] for s in changed_secs)
    print("EDC/ECC valid on changed sectors:", ok)
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
