#!/usr/bin/env python3
"""PROOF: arbitrary VALID Japanese injection via the noun table executes.

nounbox proved the parser receives whatever string the command path reads from the noun table
(it received English MAILBOX). The only untested half: does a *valid* Japanese substitution actually
EXECUTE? Here we point the mailbox entry (run1[11]=郵便箱) at the existing 'ドア' (door) string and
redirect the command-path literal @0x0601fbc8 to that table. ZVOCTBL stays stock.

Expected on emulator: at West of House, the mailbox now shows ドア; "open it" runs as 'ドアを開ける'
and gives a real door response (e.g. 板が打ちつけられて… "it's boarded") -- NOT "わかりません".
That proves we can make the parser execute an arbitrary valid-JP command by controlling the table,
which is the injection hook the keyboard path needs (write translated JP -> parses+executes).

In-place patch of 0ZORK in a stock disc copy (pool 0x64f34, ECC fixed). Output: game_patched/zork1_inject/.
"""
import os, shutil, struct, sys
HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, ".."))
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage

ROOT = os.path.join(HERE, "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_inject")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (inject) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (inject).cue")
SS, DO, ROOTDIR_LBA = 2352, 16, 20
BASE = 0x06004000
RUN1_FOFF, N1 = 0x789bc, 364
CMD_LIT_RUN1 = 0x1bbc8
MAILBOX_IDX = 11                 # run1[11] = 郵便箱
DOOR_STR = 0x06019d40            # existing 'ドア' string (run1[293])
POOL = 0x64f34


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


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    lba, size = find_dir_entry(tb, "0ZORK.BIN")
    nsec = -(-size // 2048)
    z = bytearray(b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + 2048] for s in range(nsec))[:size])

    # copy run1 to the pool, override mailbox entry -> door string, repoint command literal
    cur = POOL
    table = BASE + cur
    for i in range(N1):
        v = struct.unpack_from(">I", z, RUN1_FOFF + i * 4)[0]
        if i == MAILBOX_IDX:
            v = DOOR_STR
        struct.pack_into(">I", z, cur, v); cur += 4
    struct.pack_into(">I", z, CMD_LIT_RUN1, table)
    print("run1 copy @0x%08x ; run1[%d] mailbox->ドア(0x%08x) ; @0x0601fbc8 -> copy"
          % (table, MAILBOX_IDX, DOOR_STR))

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
