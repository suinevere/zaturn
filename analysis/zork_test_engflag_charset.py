#!/usr/bin/env python3
"""engflag + mirror-fix + FULL prose charset via custom font slots.

RE result (ground-truth from sample2/sample3 pixels): the SJIS prose renderer draws
    glyph = font[(stored_byte + 0x1F)]   for stored_byte in ~[0x30 .. 0x7F]
    stored_byte < 0x30 = CONTROL codes (uncontrollable -> garble)
So uppercase O-Z (stored 0x30-0x3B) and a-z (stored 0x42-0x5B) already render; uppercase A-N,
digits, and most punctuation need stored < 0x30 and are unreachable through the default charset.

UNLOCK (no code patch): the prose-reachable HIGH glyph slots 0x7F-0x9E (stored 0x60-0x7F) are NEVER
used by the raw-ASCII menu (menu uses only ASCII 0x20-0x7E). We already rewrite the font, so we COPY
the existing glyph images for A-N / 0-9 / punctuation into those high slots and map the encoder to the
matching stored bytes. Native chars (>= 'O', i.e. ord>=0x4F: O-Z, a-z, []^_`{|}~) keep stored=ord-0x1F.

Demo doubles as the upper-window probe (exercises stored 0x60-0x7F).
Output: game_patched/zork1_engflag_charset/
"""
import os, shutil, struct, math, sys
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from saturn_translate import ecc
import zork_cgz as z

ROOT = os.path.join(os.path.dirname(__file__), "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag_charset")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag charset) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag charset).cue")
SS, DO, USER, ROOTDIR_LBA = 2352, 16, 2048, 20

BASE = 0x06004000
MSG_TBL = 0x99be8
POOL = 0x64f34
ENGFLAG = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}
FONT_BYTES = 0x1000
GLYPH = 16  # bytes per glyph (8x16, 1bpp)
BITREV = [int("{:08b}".format(i)[::-1], 2) for i in range(256)]

# --- custom charset: chars that need stored < 0x30 in the default scheme get a HIGH glyph slot ---
# slot S (0x7F..) is reached by stored = S - 0x1F (0x60..). FONTCOPY[S] = source ASCII glyph to paint.
CUSTOM = {}     # char -> stored byte
FONTCOPY = {}   # dest glyph slot -> source glyph slot (ASCII index)
_slot = 0x7F
def _assign(ch):
    global _slot
    CUSTOM[ch] = _slot - 0x1F
    FONTCOPY[_slot] = ord(ch)        # copy the existing image of this ASCII char
    _slot += 1
for ch in "ABCDEFGHIJKLMN": _assign(ch)   # uppercase A-N  (O-Z are native via stored 0x30-0x3B)
for ch in "0123456789":    _assign(ch)   # digits
for ch in ".,'!?-:":       _assign(ch)   # punctuation (window = 32 slots: 0x7F-0x9E)
assert _slot <= 0x9F, "ran past the reachable high window"

MSG_PATCH = {36: "West of House"}
LOOSE_PATCH = {0x06042d44:
               "You are standing in an open field, west of a white house. Score: 0, Moves: 1."}


def enc(s):
    out = bytearray()
    for ch in s:
        if ch == " ":
            out.append(0x20)
        elif ch == "\n":
            out.append(0x0c)
        elif ord(ch) >= 0x4F and ord(ch) < 0x7F:      # native: O-Z, a-z, []^_`{|}~
            out.append((ord(ch) - 0x1F) & 0xFF)
        elif ch in CUSTOM:
            out.append(CUSTOM[ch])
        else:
            raise ValueError("char %r not mappable" % ch)
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
        ent = MSG_TBL + idx * 4
        print("  msg777[%d] -> %r" % (idx, eng)); struct.pack_into(">I", z0, ent, put(eng))
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
    # copy custom glyph images into the high reachable slots (post-bitrev domain; copying whole bytes)
    for dst, src in FONTCOPY.items():
        dec[dst * GLYPH:dst * GLYPH + GLYPH] = dec[src * GLYPH:src * GLYPH + GLYPH]
    comp = z.compress(bytes(dec))
    assert math.ceil(len(comp) / USER) == nsec, "%s recompress overflowed sector alloc" % name
    padded = comp + b"\x00" * (nsec * USER - len(comp))
    for s in range(nsec):
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = padded[s * USER:(s + 1) * USER]
        reecc(tb, lba + s)
    struct.pack_into("<I", tb, fo + 10, len(comp)); struct.pack_into(">I", tb, fo + 14, len(comp))


def main():
    print("custom charmap (char -> stored byte -> glyph slot):")
    for ch, st in CUSTOM.items():
        print("    %r -> 0x%02x -> slot 0x%02x" % (ch, st, st + 0x1F))
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
