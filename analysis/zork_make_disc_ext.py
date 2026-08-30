#!/usr/bin/env python3
"""Zork I (Saturn JP) — relocate+extend 0ZORK.BIN to unlock a big translation pool.

Instead of shifting 23k files, append an EXTENDED copy of 0ZORK.BIN (original + 128 KB English
pool) at the end of track 1 and repoint its root-directory entry (LBA + size). If the BIOS loads
the first file by its directory entry (1st-read-size=0 implies it), it maps the extended file —
giving the pool as fresh end-of-image RAM. Only ~370 sectors written + a directory patch.

Output: game_patched/zork1_ext/  (test: does it boot with translations from the new location?)
"""
import os, struct, shutil
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage
import zork_translate
import zork_zvoctbl_patch
import zork_cgz
import zork_tabs

ROOT = os.path.join(os.path.dirname(__file__), "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_ext")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (ext) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (ext).cue")
SS, DO = 2352, 16
ROOTDIR_LBA = 20


def bcd(n): return ((n // 10) << 4) | (n % 10)

def make_sector(lba, data2048):
    s = bytearray(SS)
    s[0] = 0x00; s[1:11] = b"\xff" * 10; s[11] = 0x00          # sync
    a = lba + 150
    s[12] = bcd(a // (75 * 60)); s[13] = bcd((a // 75) % 60); s[14] = bcd(a % 75)  # MSF
    s[15] = 0x01                                                # mode 1
    s[DO:DO + 2048] = data2048
    ecc.fix_sector(s)                                          # EDC + ECC P/Q
    return bytes(s)


def patch_file_in_disc(tb, name, patch_fn, verbose=False):
    """Read file from disc, apply patch_fn(bytes)->bytes (same size), write back with ECC."""
    fo, lba, size = find_dir_entry(tb, name)
    nsec = -(-size // 2048)
    data = bytearray()
    for s in range(nsec):
        data += tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048]
    patched = patch_fn(bytes(data[:size]))
    if len(patched) != size:
        raise ValueError("%s: patched size %d != original %d" % (name, len(patched), size))
    for s in range(nsec):
        chunk = bytearray(patched[s * 2048:min((s + 1) * 2048, size)])
        chunk += b"\x00" * (2048 - len(chunk))
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] = chunk
        sec = bytearray(tb[(lba + s) * SS:(lba + s) * SS + SS])
        ecc.fix_sector(sec)
        tb[(lba + s) * SS:(lba + s) * SS + SS] = sec
    if verbose:
        print("%s patched in place: lba=%d size=%d (%d sectors)" % (name, lba, size, nsec))


def find_dir_entry(track_bytes, name):
    """Return (file_offset_of_entry, lba, size) for `name` in the root directory."""
    base = ROOTDIR_LBA * SS + DO
    raw = b"".join(track_bytes[(ROOTDIR_LBA + s) * SS + DO:(ROOTDIR_LBA + s) * SS + DO + 2048]
                   for s in range(2))
    i = 0
    while i < len(raw):
        ln = raw[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048; continue
        nlen = raw[i + 32]; nm = raw[i + 33:i + 33 + nlen]
        if nm.split(b";")[0] == name.encode():
            # map raw offset i back to file offset (account for the 2-sector split)
            sec = i // 2048; off_in = i % 2048
            fo = (ROOTDIR_LBA + sec) * SS + DO + off_in
            lba = struct.unpack("<I", raw[i + 2:i + 6])[0]
            size = struct.unpack("<I", raw[i + 10:i + 14])[0]
            return fo, lba, size
        i += ln
    raise KeyError(name)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    patched = zork_translate.build_patched_0zork(verbose=True)   # original + 128 KB pool
    nsec = -(-len(patched) // 2048)
    print("extended 0ZORK: %d bytes = %d sectors" % (len(patched), nsec))

    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    track_sectors = len(tb) // SS
    new_lba = track_sectors                                       # append at current end
    print("track has %d sectors; new 0ZORK lba=%d..%d" % (track_sectors, new_lba, new_lba + nsec - 1))

    # append extended 0ZORK as fresh MODE1/2352 sectors
    out = bytearray()
    for s in range(nsec):
        chunk = patched[s * 2048:(s + 1) * 2048]
        chunk = chunk + b"\x00" * (2048 - len(chunk))
        out += make_sector(new_lba + s, chunk)
    tb += out

    # patch root-dir entry for 0ZORK.BIN (LBA + size, LE & BE), then re-ECC the dir sectors
    fo, old_lba, old_size = find_dir_entry(tb, "0ZORK.BIN")
    print("0ZORK dir entry @file 0x%x  old lba=%d size=%d" % (fo, old_lba, old_size))
    struct.pack_into("<I", tb, fo + 2, new_lba); struct.pack_into(">I", tb, fo + 6, new_lba)
    struct.pack_into("<I", tb, fo + 10, len(patched)); struct.pack_into(">I", tb, fo + 14, len(patched))
    for s in range(2):                                            # re-ECC root-dir sectors
        lba = ROOTDIR_LBA + s
        sec = bytearray(tb[lba * SS:lba * SS + SS]); ecc.fix_sector(sec)
        tb[lba * SS:lba * SS + SS] = sec

    # --- SINIT2.SLD tab repaint: OBJECT / ACTION / ITEMS / MOVE ---
    # The 4 mode-tab labels are baked 4bpp 48x16 plates in SINIT2.SLD (not text glyphs).
    # Decompress -> paint English plates -> recompress -> patch in place (same file size ±).
    def _patch_sinit2(raw):
        dec = bytearray(zork_cgz.decompress(raw))
        patched_dec = zork_tabs.build_patched_dec(dec)
        recomp = zork_cgz.compress(patched_dec)
        # must fit in same sector span (recompressed is always smaller than original)
        if len(recomp) > len(raw):
            raise RuntimeError("SINIT2 recompressed larger than original (%d > %d)" % (len(recomp), len(raw)))
        out = bytearray(len(raw))
        out[:len(recomp)] = recomp
        return bytes(out)
    patch_file_in_disc(tb, "SINIT2.SLD", _patch_sinit2, verbose=True)

    # --- ZVOCTBL translation DISABLED (was Path B1) ---
    # DISCOVERY (2026-06-29): selection UI feeds ORIGINAL JAPANESE readings to parser.
    # Translating ZVOCTBL keywords deletes those readings -> command fails.
    # Leave ZVOCTBL.DAT untouched; zork_zvoctbl_patch.py kept for reference.

    open(OUT_T1, "wb").write(tb)

    # cue: data track + hardlinked audio
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

    # verify
    v = SaturnImage.from_file(OUT_T1); e = v.find("/0ZORK.BIN")
    print("verify: dir now lba=%d size=%d ; extract matches: %s" %
          (e.lba, e.size, v.extract("/0ZORK.BIN") == patched))
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
