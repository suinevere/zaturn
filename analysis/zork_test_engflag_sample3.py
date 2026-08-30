#!/usr/bin/env python3
"""engflag + mirror-fix + SAMPLE English prose -- ENCODING v2 (charset = char - 0x1F).

Screenshot of v1 (raw ASCII) proved the prose path renders glyph = font[(stored_byte + 0x1F)*16]
(e.g. stored 'W'=0x57 -> glyph 0x76 'v'; "eqnms cnnq" -0x1F = "FRONT DOOR"). So the dormant English
MESSAGE charset is ASCII - 0x1F (keeps letters above the 0x00-0x21 token range). This build emits
that: printable char -> (char-0x1F); SPACE -> 0x60 (-> glyph 0x7F, a blank, control-safe). Sample
kept punctuation-free to isolate the letter+space mapping.

Output: game_patched/zork1_engflag_sample2/
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
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag_sample3")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag sample3) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag sample3).cue")
SS, DO, USER, ROOTDIR_LBA = 2352, 16, 2048, 20

BASE = 0x06004000
MSG_TBL = 0x99be8
POOL = 0x64f34
POOL_SIZE = 0xD000
ENGFLAG = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}
FONT_BYTES = 0x1000
BITREV = [int("{:08b}".format(i)[::-1], 2) for i in range(256)]

# ENCODING v3: target LOWERCASE glyphs. Empirics: stored bytes 0x30-0x5F render cleanly as
# glyph=font[(byte+0x1F)*16]; bytes <0x30 hit token/control handling and garble. Lowercase output
# (font 0x61-0x7A) needs stored 0x42-0x5B -- all inside the clean range. Space=0x20 (renders as a
# real space, confirmed in v1). So: letters -> lowercase, stored = (lower - 0x1F); space -> 0x20.
MSG_PATCH = {36: "west of house"}
LOOSE_PATCH = {0x06042d44: "you are standing in an open field west of a white house"}


def enc(s):
    out = bytearray()
    for ch in s:
        if ch == " ":
            out.append(0x20)
        elif ch == "\n":
            out.append(0x0c)
        else:
            out.append((ord(ch.lower()) - 0x1F) & 0xFF)   # lowercase target -> clean range
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
            sec = i // 2048; off_in = i % 2048
            fo = (ROOTDIR_LBA + sec) * SS + DO + off_in
            return fo, struct.unpack("<I", raw[i + 2:i + 6])[0], struct.unpack("<I", raw[i + 10:i + 14])[0]
        i += ln
    raise KeyError(name)


def reecc(tb, sec):
    s = bytearray(tb[sec * SS:sec * SS + SS]); ecc.fix_sector(s); tb[sec * SS:sec * SS + SS] = s


def build_patched_0zork(orig):
    z0 = bytearray(orig)
    for foff, (want, new) in ENGFLAG.items():
        assert (z0[foff] << 8) | z0[foff + 1] == want
        z0[foff] = new >> 8; z0[foff + 1] = new & 0xFF
    cur = POOL
    def put(text):
        nonlocal cur
        b = enc(text)
        z0[cur:cur + len(b)] = b
        addr = BASE + cur
        cur += len(b) + (len(b) & 1)
        print("    -> @0x%08x bytes=%s" % (addr, b.hex()))
        return addr
    for idx, eng in MSG_PATCH.items():
        ent = MSG_TBL + idx * 4
        print("  msg777[%d] -> %r" % (idx, eng)); struct.pack_into(">I", z0, ent, put(eng))
    for addr, eng in LOOSE_PATCH.items():
        po = addr - BASE
        print("  loose@0x%08x -> %r" % (addr, eng)); struct.pack_into(">I", z0, po, put(eng))
    assert len(z0) == len(orig)
    return bytes(z0)


def write_file_in_disc(tb, name, newbytes):
    fo, lba, size = find_dir_entry(tb, name)
    assert len(newbytes) == size
    for s in range(math.ceil(size / USER)):
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
    assert math.ceil(len(comp) / USER) == nsec
    padded = comp + b"\x00" * (nsec * USER - len(comp))
    for s in range(nsec):
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = padded[s * USER:(s + 1) * USER]
        reecc(tb, lba + s)
    struct.pack_into("<I", tb, fo + 10, len(comp)); struct.pack_into(">I", tb, fo + 14, len(comp))


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    _fo, zlba, zsize = find_dir_entry(tb, "0ZORK.BIN")
    nsec = math.ceil(zsize / USER)
    orig0 = bytes(b"".join(tb[(zlba + s) * SS + DO:(zlba + s) * SS + DO + USER] for s in range(nsec))[:zsize])
    write_file_in_disc(tb, "0ZORK.BIN", build_patched_0zork(orig0))
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
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
