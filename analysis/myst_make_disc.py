#!/usr/bin/env python3
"""Assemble a testable Myst (USA) disc with the 3D-pad analog patch.

Steps: build A.patched.BIN -> copy Track 01 -> overwrite A.BIN's extent (LBA 72)
with the patched bytes and recompute EDC/ECC per sector -> flip IP peripheral
string 'JM'->'JME' in sector 0 (+ECC) -> copy the audio track -> write .cue.
Output: game_patched/Myst (USA) (3D-Pad analog v0.1 Suinevere)/
"""
import os, shutil, struct
import myst_build_3dpad as B
from saturn_translate.iso import SaturnImage
from saturn_translate import ecc

ROOT = os.path.join(os.path.dirname(__file__), "..")
ORIG = os.path.join(ROOT, "game_originals", "Myst (USA)")
SRC_T1 = os.path.join(ORIG, "Myst (USA) (Track 1).bin")
SRC_T2 = os.path.join(ORIG, "Myst (USA) (Track 2).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "Myst (USA) (3D-Pad analog v0.1 Suinevere)")
OUT_T1 = os.path.join(OUTDIR, "Myst (USA) (3D-Pad) (Track 1).bin")
OUT_T2 = os.path.join(OUTDIR, "Myst (USA) (3D-Pad) (Track 2).bin")
OUT_CUE = os.path.join(OUTDIR, "Myst (USA) (3D-Pad).cue")
SS, DO = 2352, 16


def patch_sector(f, lba, mutate):
    """Read raw sector @lba, apply mutate(bytearray user_data), recompute ECC, write."""
    f.seek(lba * SS)
    sec = bytearray(f.read(SS))
    ud = bytearray(sec[DO:DO + 2048])
    mutate(ud)
    sec[DO:DO + 2048] = ud
    ecc.fix_sector(sec)
    f.seek(lba * SS)
    f.write(sec)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    # 1. build patched A.BIN
    B.main()
    patched = open(os.path.join(ROOT, "work", "myst", "A.patched.BIN"), "rb").read()
    orig_abin = open(os.path.join(ROOT, "work", "myst", "A.BIN"), "rb").read()
    diffs = [i for i in range(len(patched)) if patched[i] != orig_abin[i]]
    print("A.BIN changed bytes: %d (ranges: %s)" %
          (len(diffs), _ranges(diffs)))

    # locate A.BIN extent
    img = SaturnImage.from_file(SRC_T1)
    e = img.find("/A.BIN")
    nsec = -(-e.size // 2048)
    print("A.BIN lba=%d size=%d sectors=%d" % (e.lba, e.size, nsec))

    # 2. copy Track 01
    print("copying Track 01 (585 MB)...")
    shutil.copyfile(SRC_T1, OUT_T1)

    # 3. write patched A.BIN sectors + ECC (only sectors that changed)
    changed_secs = sorted({d // 2048 for d in diffs})
    print("A.BIN sectors to rewrite:", changed_secs)
    with open(OUT_T1, "r+b") as f:
        for s in changed_secs:
            chunk = patched[s * 2048:(s + 1) * 2048]
            def mut(ud, chunk=chunk):
                ud[0:len(chunk)] = chunk
            patch_sector(f, e.lba + s, mut)
        # 4. flip IP peripheral string JM -> JME at IP offset 0x50 (sector 0)
        def flip_ip(ud):
            per = ud[0x50:0x60]
            assert per[:2] == b"JM", "unexpected periph %r" % per
            if per[:3] != b"JME":
                ud[0x52] = ord('E')
        patch_sector(f, 0, flip_ip)
    # 5. copy audio track
    print("copying Track 02 (audio)...")
    shutil.copyfile(SRC_T2, OUT_T2)
    # 6. cue
    open(OUT_CUE, "w", newline="\r\n").write(
        'FILE "%s" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
        'FILE "%s" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:01\n'
        % (os.path.basename(OUT_T1), os.path.basename(OUT_T2)))

    # 7. verify
    print("\n--- verify ---")
    v = SaturnImage.from_file(OUT_T1)
    got = v.extract("/A.BIN")
    print("patched A.BIN matches builder output:", got == patched)
    print("IP peripherals now:", repr(_periph(OUT_T1)))
    # ECC validity on the changed sectors
    with open(OUT_T1, "rb") as f:
        ok = True
        for lba in [0] + [e.lba + s for s in changed_secs]:
            f.seek(lba * SS); ok &= ecc.sector_is_valid(f.read(SS))
    print("EDC/ECC valid on all patched sectors:", ok)
    print("\nOUTPUT:", OUTDIR)


def _periph(path):
    with open(path, "rb") as f:
        f.seek(DO + 0x50); return f.read(8).decode("ascii", "replace").rstrip()


def _ranges(idxs):
    if not idxs: return "[]"
    out = []; s = p = idxs[0]
    for i in idxs[1:]:
        if i == p + 1: p = i
        else: out.append((s, p)); s = p = i
    out.append((s, p))
    return ", ".join("0x%x-0x%x" % (a, b) for a, b in out)


if __name__ == "__main__":
    main()
