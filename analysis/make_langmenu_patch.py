#!/usr/bin/env python3
"""Build the Waialae "always ask for language" patch.

Forces the boot dispatcher to always run the hidden language-select menu (the
one normally gated behind holding Down+C+X+L at startup), by NOP-ing the cheat's
conditional skip branch in A.BIN.

A.BIN file 0xee:  8b04 (bf 0x060540fa) -> 0009 (nop)
so the menu `bsr` always executes regardless of the held button combo.

Emits xdelta + ips (whole Track-1 image, with EDC/ECC recomputed for the touched
sector so Terraonion MODE accepts it) and a patched standalone A.BIN for rebuilding
a .ssp in Sega Saturn Patcher. Mirrors cli.cmd_force_language.
"""
import os, sys
from pathlib import Path
from saturn_translate import vcdiff, ips, ecc
from saturn_translate.iso import SaturnImage

IMAGE = ("game_originals/Waialae no Kiseki - Extra 36 Holes (Japan)/"
         "Waialae no Kiseki - Extra 36 Holes (Japan) (Track 1).bin")
A_FILE_OFF = 0xee
OLD = bytes.fromhex("8b04")
NEW = bytes.fromhex("0009")
OUT_STEM = "game_patched/waialae-langmenu"
SSP_DIR = "game_patched/ssp_source_langmenu"

def main():
    src = open(IMAGE, "rb").read()
    img = SaturnImage(src)
    a_base = img.file_byte_offset("/A.BIN")
    off = a_base + A_FILE_OFF
    if src[off:off+2] != OLD:
        print(f"ERROR: bytes at {off:#x} are {src[off:off+2].hex()}, expected {OLD.hex()}",
              file=sys.stderr)
        return 1
    print(f"A.BIN data @ {a_base:#x}; patch byte at image {off:#x} "
          f"({src[off:off+2].hex()} -> {NEW.hex()})")

    edits = [vcdiff.Edit(offset=off, old_len=2, data=NEW)]
    ips_records = [ips.Record(offset=off, data=NEW)]

    # EDC/ECC recompute for the touched sector (MODE1/2352).
    sec_idx = off // ecc.SECTOR_SIZE
    sec_start = sec_idx * ecc.SECTOR_SIZE
    sector = bytearray(src[sec_start:sec_start + ecc.SECTOR_SIZE])
    local = off - sec_start
    sector[local:local+2] = NEW
    ecc.fix_sector(sector)
    ecc_block = bytes(sector[ecc.EDC_OFFSET:0x930])
    ecc_off = sec_start + ecc.EDC_OFFSET
    edits.append(vcdiff.Edit(offset=ecc_off, old_len=len(ecc_block), data=ecc_block))
    ips_records.append(ips.Record(offset=ecc_off, data=ecc_block))
    print(f"fix-ecc: sector {sec_idx} (image {ecc_off:#x}, {len(ecc_block)} bytes)")

    patch = vcdiff.encode(src, edits)
    # self-verify
    expected = bytearray(src)
    for e in edits:
        expected[e.offset:e.offset + e.old_len] = e.data
    rebuilt = vcdiff.decode(src, patch)
    ok = len(rebuilt) == len(expected)
    step = 8 << 20
    if ok:
        for s in range(0, len(expected), step):
            if rebuilt[s:s+step] != bytes(expected[s:s+step]):
                ok = False; break
    if not ok:
        print("ERROR: xdelta self-verification failed.", file=sys.stderr)
        return 1

    Path("game_patched").mkdir(exist_ok=True)
    open(OUT_STEM + ".xdelta", "wb").write(patch)
    open(OUT_STEM + ".ips", "wb").write(ips.encode(ips_records))
    print(f"xdelta -> {OUT_STEM}.xdelta ({len(patch)} bytes, self-verified)")
    print(f"ips    -> {OUT_STEM}.ips")

    # patched standalone A.BIN for Sega Saturn Patcher
    a = bytearray(img.extract("/A.BIN"))
    a[A_FILE_OFF:A_FILE_OFF+2] = NEW
    Path(SSP_DIR).mkdir(parents=True, exist_ok=True)
    open(os.path.join(SSP_DIR, "A.BIN"), "wb").write(a)
    print(f"A.BIN  -> {SSP_DIR}/A.BIN ({len(a)} bytes)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
