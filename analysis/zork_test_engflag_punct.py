#!/usr/bin/env python3
"""engflag + mirror-fix + lowercase prose + PUNCTUATION via safe font-remap.

RE (ground truth from screenshots): the room-prose renderer does toUpper(letter) then draws
glyph = font[(byte + 0x1F)] from the ASCII font (INIT2/SINIT2). Reachable input window is 0x30-0x5F
(-> glyph 0x4F-0x7E); <0x30 and >=0x60 are control (the 0x60+ charset build proved this by breaking
globally). Reachable glyphs = O-Z, []^_`, a-z, {|}~. a-z reachable by feeding (lower-0x1F)=0x42-0x5B
(not a lowercase byte, so it escapes toUpper). Uppercase A-N / digits / punctuation are unreachable
directly. TRANSCODE (0x0604608a, full charset) drives a DIFFERENT text element, not the room body.

FIX: the ASCII menu uses only glyphs 0x20-0x7E for its all-caps labels; glyph slots 0x5B-0x60
([\]^_`) and 0x7B-0x7E ({|}~) are unused by both the menu and lowercase prose -> 10 FREE slots,
reachable via inputs 0x3C-0x41 and 0x5C-0x5F (all inside the safe 0x30-0x5F window). Paint the 10
punctuation glyph images into those slots (copied from their real ASCII positions) and map the
encoder. Net: readable lowercase English prose WITH punctuation. Menu untouched, zero code changes,
no unsafe bytes.

Output: game_patched/zork1_engflag_punct/
"""
import os, shutil, struct, math, sys
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from saturn_translate import ecc
import zork_cgz as z

ROOT = os.path.join(os.path.dirname(__file__), "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag_punct")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag punct) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag punct).cue")
SS, DO, USER, ROOTDIR_LBA = 2352, 16, 2048, 20

BASE = 0x06004000
MSG_TBL = 0x99be8
POOL = 0x64f34
ENGFLAG = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}
FONT_BYTES = 0x1000
GLYPH = 16
BITREV = [int("{:08b}".format(i)[::-1], 2) for i in range(256)]

# 10 free reachable glyph slots (unused by menu-caps and lowercase prose)
FREE_SLOTS = [0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x7B, 0x7C, 0x7D, 0x7E]
PUNCT      = ['.', ',', "'", '!', '?', '-', ':', ';', '(', ')']
CUSTOM = {}      # char -> input byte (slot - 0x1F)
FONTCOPY = {}    # dest glyph slot -> source ASCII glyph slot
for slot, ch in zip(FREE_SLOTS, PUNCT):
    CUSTOM[ch] = slot - 0x1F
    FONTCOPY[slot] = ord(ch)
    assert 0x30 <= slot - 0x1F <= 0x5F, "input for %r out of safe window" % ch

MSG_PATCH = {36: "west of house"}
LOOSE_PATCH = {0x06042d44:
               "you are standing in an open field, west of a white house.\n"
               "there's a small mailbox here (closed); open it! locked? no - go in:"}


def enc(s):
    out = bytearray()
    for ch in s:
        if ch == " ":
            out.append(0x20)
        elif ch == "\n":
            out.append(0x0c)
        elif ch in CUSTOM:
            out.append(CUSTOM[ch])
        elif ch.isalpha():
            out.append((ord(ch.lower()) - 0x1F) & 0xFF)   # lowercase -> input 0x42-0x5B (escapes toUpper)
        else:
            out.append(0x20)                              # unknown -> space
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
        b = enc(text); z0[cur:cur + len(b)] = b; addr = BASE + cur
        cur += len(b) + (len(b) & 1)
        print("    -> @0x%08x bytes=%s" % (addr, b.hex())); return addr
    for idx, eng in MSG_PATCH.items():
        print("  msg777[%d] -> %r" % (idx, eng)); struct.pack_into(">I", z0, MSG_TBL + idx * 4, put(eng))
    for addr, eng in LOOSE_PATCH.items():
        print("  loose@0x%08x -> %r" % (addr, eng)); struct.pack_into(">I", z0, addr - BASE, put(eng))
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
    for dst, src in FONTCOPY.items():
        img = dec[src * GLYPH:src * GLYPH + GLYPH]
        assert any(img), "source glyph 0x%02x is blank in %s" % (src, name)
        dec[dst * GLYPH:dst * GLYPH + GLYPH] = img
    comp = z.compress(bytes(dec))
    assert z.decompress(comp)[:len(dec)] == bytes(dec), "%s font round-trip mismatch" % name
    assert math.ceil(len(comp) / USER) == nsec, "%s recompress overflowed (%d>%d sec)" % (name, math.ceil(len(comp) / USER), nsec)
    padded = comp + b"\x00" * (nsec * USER - len(comp))
    for s in range(nsec):
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = padded[s * USER:(s + 1) * USER]
        reecc(tb, lba + s)
    struct.pack_into("<I", tb, fo + 10, len(comp)); struct.pack_into(">I", tb, fo + 14, len(comp))


def main():
    print("punctuation map (char -> input -> glyph slot):")
    for ch in PUNCT:
        print("    %r -> 0x%02x -> slot 0x%02x" % (ch, CUSTOM[ch], CUSTOM[ch] + 0x1F))
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
