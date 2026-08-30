#!/usr/bin/env python3
"""EXPERIMENT: engflag (English mode) + mirror-fix + a SAMPLE English PROSE injection.

Goal: prove English room text renders in the engflag build. In English mode (flag 0x060af984!=0)
ALL text goes through the char*16 ASCII glyph path (FUN_0x0600eda8 -> LOOP1 in FUN_0x0600ef00),
which we un-mirrored by bit-reversing the 8x16 ASCII font. So prose must be encoded as PLAIN ASCII
(space=0x20, '\n'=0x0c paragraph, 0x00 terminator) -- NOT the full-width SJIS the flag=0 translation
pipeline uses.

Sample = two plain-ASCII literals on the opening "West of House" page (no dict tokens, for an
unambiguous render test):
  * msg777[36]  (room-title header)             -> "WEST OF HOUSE"
  * loose @0x06042d44 (room body description)   -> "YOU ARE STANDING IN AN OPEN FIELD ..."
Strings relocate into the in-image stable pool (file 0x64f34 / addr 0x06068f34) and the table/code
pointers are repointed. 0ZORK.BIN size is unchanged (in-place). Also applies the engflag flag-flip
and the INIT2/SINIT2 font bit-reversal (mirror fix).

Output: game_patched/zork1_engflag_sample/
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
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag_sample")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag sample) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag sample).cue")
SS, DO, USER, ROOTDIR_LBA = 2352, 16, 2048, 20

BASE = 0x06004000
MSG_TBL = 0x99be8                        # msg777 pointer table (file offset)
POOL = 0x64f34                           # in-image stable pool (addr 0x06068f34)
POOL_SIZE = 0xD000
ENGFLAG = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}
FONT_BYTES = 0x1000
BITREV = [int("{:08b}".format(i)[::-1], 2) for i in range(256)]

# --- the sample (plain ASCII literals) ---
MSG_PATCH = {36: "WEST OF HOUSE"}        # msg777 index -> english
LOOSE_PATCH = {0x06042d44: "YOU ARE STANDING IN AN OPEN FIELD WEST OF A WHITE HOUSE, "
                           "WITH A BOARDED FRONT DOOR. THERE IS A SMALL MAILBOX HERE."}


def enc(s):
    out = bytearray()
    for ch in s:
        out.append(0x0c if ch == "\n" else (ord(ch) & 0xFF))
    out.append(0x00)
    return bytes(out)


def build_patched_0zork(orig):
    z0 = bytearray(orig)
    # engflag flag writes
    for foff, (want, new) in ENGFLAG.items():
        cur = (z0[foff] << 8) | z0[foff + 1]
        assert cur == want, "0ZORK 0x%x have %04x want %04x" % (foff, cur, want)
        z0[foff] = new >> 8; z0[foff + 1] = new & 0xFF
    # verify targets currently hold the expected JP, then relocate sample strings into the pool
    cur = POOL
    def put(text):
        nonlocal cur
        b = enc(text)
        if cur + len(b) > POOL + POOL_SIZE:
            raise RuntimeError("pool overflow")
        z0[cur:cur + len(b)] = b
        addr = BASE + cur
        cur += len(b) + (len(b) & 1)             # 2-byte align next
        return addr
    # msg777
    for idx, eng in MSG_PATCH.items():
        ent = MSG_TBL + idx * 4
        old = struct.unpack(">I", z0[ent:ent + 4])[0]
        jp = orig[old - BASE: orig.find(b"\x00", old - BASE)]
        print("  msg777[%d] old=0x%08x JP=%r -> %r" % (idx, old, jp.decode("shift_jis", "replace"), eng))
        struct.pack_into(">I", z0, ent, put(eng))
    # loose code pointers
    for addr, eng in LOOSE_PATCH.items():
        po = addr - BASE
        old = struct.unpack(">I", z0[po:po + 4])[0]
        jp = orig[old - BASE: orig.find(b"\x00", old - BASE)]
        print("  loose@0x%08x old=0x%08x JP=%r -> %r" % (addr, old, jp.decode("shift_jis", "replace")[:30], eng[:30] + "..."))
        struct.pack_into(">I", z0, po, put(eng))
    assert len(z0) == len(orig)
    return bytes(z0)


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
            return fo, struct.unpack("<I", raw[i + 2:i + 6])[0], struct.unpack("<I", raw[i + 10:i + 14])[0]
        i += ln
    raise KeyError(name)


def reecc(tb, sec):
    s = bytearray(tb[sec * SS:sec * SS + SS]); ecc.fix_sector(s); tb[sec * SS:sec * SS + SS] = s


def write_file_in_disc(tb, name, newbytes):
    fo, lba, size = find_dir_entry(tb, name)
    assert len(newbytes) == size, "%s size changed %d!=%d" % (name, len(newbytes), size)
    nsec = math.ceil(size / USER)
    for s in range(nsec):
        chunk = bytearray(newbytes[s * USER:(s + 1) * USER]); chunk += b"\x00" * (USER - len(chunk))
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = chunk
        reecc(tb, lba + s)


def patch_sld_font(tb, name):
    fo, lba, size = find_dir_entry(tb, name)
    nsec = math.ceil(size / USER)
    raw = b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] for s in range(nsec))[:size]
    dec = bytearray(z.decompress(raw))
    for i in range(FONT_BYTES):
        dec[i] = BITREV[dec[i]]
    comp = z.compress(bytes(dec))
    assert math.ceil(len(comp) / USER) == nsec, "%s sector count changed" % name
    padded = comp + b"\x00" * (nsec * USER - len(comp))
    for s in range(nsec):
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = padded[s * USER:(s + 1) * USER]
        reecc(tb, lba + s)
    struct.pack_into("<I", tb, fo + 10, len(comp)); struct.pack_into(">I", tb, fo + 14, len(comp))
    print("  %s font reversed: comp=%d (%d sectors)" % (name, len(comp), nsec))


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())

    _fo, zlba, zsize = find_dir_entry(tb, "0ZORK.BIN")
    nsec = math.ceil(zsize / USER)
    orig0 = bytes(b"".join(tb[(zlba + s) * SS + DO:(zlba + s) * SS + DO + USER] for s in range(nsec))[:zsize])
    new0 = build_patched_0zork(orig0)
    write_file_in_disc(tb, "0ZORK.BIN", new0)
    patch_sld_font(tb, "INIT2.SLD")
    patch_sld_font(tb, "SINIT2.SLD")
    for s in range(2):
        reecc(tb, ROOTDIR_LBA + s)
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

    # verify re-extracted 0ZORK has the english at the repointed pointers
    v = SaturnImage.from_file(OUT_T1)
    zb = v.extract("/0ZORK.BIN")
    p = struct.unpack(">I", zb[MSG_TBL + 36 * 4:MSG_TBL + 36 * 4 + 4])[0]
    got = zb[p - BASE: zb.find(b"\x00", p - BASE)].decode("ascii", "replace")
    print("VERIFY msg777[36] now -> %r" % got)
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
