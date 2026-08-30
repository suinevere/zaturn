#!/usr/bin/env python3
"""Inject the English-tab SINIT2.SLD into a Zork Track-01 image, in place.

Patched SINIT2.SLD (English OBJECT/ACTION/ITEMS/MOVE plates) = 115912 B, which is
smaller than the original 116110 B, so it reuses the same LBA/sector allocation.
We overwrite the user-data of each occupied sector (zero-padding the tail) and
recompute Mode-1 EDC/ECC so Terraonion MODE etc. accept it. The ISO directory size
is left at the original value (the LZSS decoder stops at its 4-byte size header, so
the ~198 trailing pad bytes are ignored).
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from saturn_translate import ecc
from saturn_translate.iso import SaturnImage
import zork_cgz as z, zork_tabs as zt

SS, DO, USER = 2352, 16, 2048


def build_patched_sinit2(orig_sld_path="work/zork1/SINIT2.SLD"):
    dec = z.decompress(open(orig_sld_path, "rb").read())
    return z.compress(bytes(zt.build_patched_dec(dec)))


def inject(track1_path, new_sld):
    img = SaturnImage.from_file(track1_path)
    ent = img.find("/SINIT2.SLD")
    assert ent is not None, "SINIT2.SLD not in image"
    lba, size = ent.lba, ent.size
    nsec = math.ceil(size / USER)
    assert len(new_sld) <= size, f"patched {len(new_sld)} > slot {size}"
    img_bytes = bytearray(open(track1_path, "rb").read())
    padded = new_sld + b"\x00" * (nsec * USER - len(new_sld))
    for s in range(nsec):
        base = (lba + s) * SS
        sec = bytearray(img_bytes[base:base + SS])
        sec[DO:DO + USER] = padded[s * USER:(s + 1) * USER]
        ecc.fix_sector(sec)
        img_bytes[base:base + SS] = sec
    open(track1_path, "wb").write(img_bytes)
    return lba, nsec


def verify(track1_path):
    img = SaturnImage.from_file(track1_path)
    ent = img.find("/SINIT2.SLD")
    expect = build_patched_sinit2()                 # exact bytes we meant to write
    raw = img.read_extent(ent.lba, ent.size)
    ok = raw[:len(expect)] == expect
    img_bytes = open(track1_path, "rb").read()
    nsec = math.ceil(ent.size / USER)
    ecc_ok = all(ecc.sector_is_valid(img_bytes[(ent.lba + s) * SS:(ent.lba + s + 1) * SS])
                 for s in range(nsec))
    return ok, ecc_ok


if __name__ == "__main__":
    t1 = sys.argv[1] if len(sys.argv) > 1 else \
        "game_patched/zork1_ext/Zork1 (ext) (Track 01).bin"
    sld = build_patched_sinit2()
    print(f"patched SINIT2.SLD = {len(sld)} bytes")
    lba, nsec = inject(t1, sld)
    print(f"injected at LBA {lba} ({nsec} sectors) into {t1}")
    tabs_ok, ecc_ok = verify(t1)
    print(f"verify: english_tabs={tabs_ok}  ecc_valid={ecc_ok}")
