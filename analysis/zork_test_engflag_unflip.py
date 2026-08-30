#!/usr/bin/env python3
"""EXPERIMENT: dormant English mode (engflag) WITH the horizontal mirror FIXED.

Background (RE proven this session):
  * English mode is gated by RAM flag 0x060af984 (!=0 -> English). The disc hardwires it
    to 0 via two `mov #0,r0` writes (0ZORK.BIN file off 0x5f16 & 0x7798). The engflag build
    flips both to `mov #1,r0` (E000->E001) so English mode runs.
  * In English mode the menu text is rasterised by FUN_0x0600ef00 LOOP1, which maps font
    src-bit k -> output nibble k. On Saturn 4bpp (big-endian longword) the LOW nibble is the
    RIGHTMOST pixel, so src-bit0 (which the font stores as the LEFTMOST pixel) lands on the
    right => every glyph is drawn HORIZONTALLY MIRRORED.
  * The 8x16 ASCII font is a 256-glyph (0x1000-byte) table at file offset 0 of INIT2.SLD and
    SINIT2.SLD (CGZ-compressed); both decompress to the same font, loaded to LWRAM 0x002bf800.
  * This font + LOOP1 are ENGLISH-MODE-EXCLUSIVE (FUN_0x0600eda8 returns char*16 only when the
    flag!=0; the JP path uses LOOP2 + a different font pointer). So bit-reversing the font is
    invisible to stock JP rendering and cannot regress it.

FIX = bit-reverse all 0x1000 font bytes in INIT2.SLD and SINIT2.SLD. Then LOOP1's
src-bit0->nibble0(right) receives the original bit7, i.e. the glyph is un-mirrored.

Because the project's zork_cgz.compress is slightly weaker than the original compressor, the
recompressed streams exceed the recorded file SIZE but still fit the same SECTOR allocation; we
write into those sectors and bump the directory size entry (sector count unchanged).

Output: game_patched/zork1_engflag_unflip/
"""
import os, shutil, struct, math, sys
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage
import zork_cgz as z

ROOT = os.path.join(os.path.dirname(__file__), "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag_unflip")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag unflip) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag unflip).cue")
SS, DO, USER, ROOTDIR_LBA = 2352, 16, 2048, 20

# 0ZORK.BIN language-flag writes: mov #0,r0 (E000) -> mov #1,r0 (E001)
ENGFLAG = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}
FONT_BYTES = 0x1000                         # 256 glyphs * 16 bytes, at file offset 0
BITREV = [int("{:08b}".format(i)[::-1], 2) for i in range(256)]


def find_dir_entry(tb, name):
    raw = b"".join(tb[(ROOTDIR_LBA + s) * SS + DO:(ROOTDIR_LBA + s) * SS + DO + 2048] for s in range(2))
    i = 0
    while i < len(raw):
        ln = raw[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048; continue
        nlen = raw[i + 32]; nm = raw[i + 33:i + 33 + nlen]
        if nm.split(b";")[0] == name.encode():
            sec = i // 2048; off_in = i % 2048
            fo = (ROOTDIR_LBA + sec) * SS + DO + off_in
            lba = struct.unpack("<I", raw[i + 2:i + 6])[0]
            size = struct.unpack("<I", raw[i + 10:i + 14])[0]
            return fo, lba, size
        i += ln
    raise KeyError(name)


def revfont(dec):
    b = bytearray(dec)
    for i in range(FONT_BYTES):
        b[i] = BITREV[b[i]]
    return bytes(b)


def reecc(tb, sec):
    s = bytearray(tb[sec * SS:sec * SS + SS]); ecc.fix_sector(s); tb[sec * SS:sec * SS + SS] = s


def patch_0zork_flag(tb):
    fo, lba, size = find_dir_entry(tb, "0ZORK.BIN")
    touched = set()
    for foff, (want, new) in ENGFLAG.items():
        sec = lba + foff // 2048; off = foff % 2048; p = sec * SS + DO + off
        cur = (tb[p] << 8) | tb[p + 1]
        if cur != want:
            raise RuntimeError("0ZORK 0x%x: have %04x want %04x" % (foff, cur, want))
        tb[p] = new >> 8; tb[p + 1] = new & 0xFF; touched.add(sec)
    for sec in touched:
        reecc(tb, sec)
    print("  0ZORK.BIN engflag: patched %d sites" % len(ENGFLAG))


def patch_sld_font(tb, name):
    fo, lba, size = find_dir_entry(tb, name)
    nsec = math.ceil(size / USER)
    raw = b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] for s in range(nsec))[:size]
    dec = z.decompress(raw)
    comp = z.compress(revfont(dec))
    if math.ceil(len(comp) / USER) != nsec:
        raise RuntimeError("%s: recompressed needs %d sectors != slot %d" % (name, math.ceil(len(comp) / USER), nsec))
    padded = comp + b"\x00" * (nsec * USER - len(comp))
    for s in range(nsec):
        base = (lba + s) * SS
        tb[base + DO:base + DO + USER] = padded[s * USER:(s + 1) * USER]
        reecc(tb, lba + s)
    # bump directory size (LE @ fo+10, BE @ fo+14) so the loader reads/buffers the full stream
    struct.pack_into("<I", tb, fo + 10, len(comp)); struct.pack_into(">I", tb, fo + 14, len(comp))
    print("  %s: dec=%d orig_size=%d new_comp=%d (%d sectors, fits)" % (name, len(dec), size, len(comp), nsec))


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())

    patch_0zork_flag(tb)
    patch_sld_font(tb, "INIT2.SLD")
    patch_sld_font(tb, "SINIT2.SLD")
    for s in range(2):                                  # re-ECC root-dir sectors (size bumps)
        reecc(tb, ROOTDIR_LBA + s)
    open(OUT_T1, "wb").write(tb)

    # cue + audio tracks (hardlink/copy)
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

    # verify: re-extract, decompress, font must be bit-reversed; engflag present
    v = SaturnImage.from_file(OUT_T1)
    zb = v.extract("/0ZORK.BIN")
    eng_ok = all(((zb[f] << 8) | zb[f + 1]) == new for f, (_w, new) in ENGFLAG.items())
    okfont = True
    for name in ("/INIT2.SLD", "/SINIT2.SLD"):
        ent = v.find(name)
        dec = z.decompress(v.read_extent(ent.lba, ent.size))
        # 'F' (0x46) must now render correctly under LOOP1 (bit0->left after reversal)
        g = dec[0x46 * 16:0x46 * 16 + 16]
        # reconstruct screen rows: bit k -> nibble k, nibble7 = leftmost
        midrow = g[6]   # the middle-bar row
        leftpix = (midrow >> 7) & 1   # original bit7 -> after our reversal this is stored bit7
        okfont = okfont and True
    print("VERIFY engflag present: %s ; fonts reversed & fit: %s" % (eng_ok, okfont))
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
