#!/usr/bin/env python3
"""Build the Steamgear 3D Control Pad release bundle (xdelta + IPS) into releases/.

IPS and xdelta both transform a CLEAN Japanese Track-01 image -> the 3D-pad image. The .ssp is
produced separately in Sega Saturn Patcher (its multi-file format isn't generated here).
"""
import sys, os, shutil
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from saturn_translate import ips

ORIG = os.path.join(ROOT, "game_originals", "Steamgear Mash (Japan)", "Steamgear Mash (Japan) (Track 01).bin")
PAD  = os.path.join(ROOT, "game_patched", "steamgear_mash_3dpad", "Steamgear Mash (Japan) (Track 01).bin")
XD   = PAD + ".xdelta"
REL  = os.path.join(ROOT, "releases", "Steamgear Mash (Japan) (3D Control Pad V1.0)")
os.makedirs(REL, exist_ok=True)

orig = open(ORIG, "rb").read()
pad  = open(PAD, "rb").read()
assert len(orig) == len(pad), "size mismatch"

# contiguous differing-byte runs -> IPS records
records = []
i = 0
while i < len(orig):
    if orig[i] != pad[i]:
        j = i
        while j < len(orig) and orig[j] != pad[j]:
            j += 1
        records.append(ips.Record(offset=i, data=pad[i:j]))
        i = j
    else:
        i += 1
patch = ips.encode(records)

# verify the IPS round-trips against ground truth (apply to clean -> patched)
def apply_ips(src, p):
    b = bytearray(src); assert p[:5] == b"PATCH"; o = 5
    while p[o:o+3] != b"EOF":
        off = (p[o] << 16) | (p[o+1] << 8) | p[o+2]; o += 3
        ln = (p[o] << 8) | p[o+1]; o += 2
        if ln == 0:  # RLE chunk
            rl = (p[o] << 8) | p[o+1]; o += 2; val = p[o]; o += 1
            b[off:off+rl] = bytes([val]) * rl
        else:
            b[off:off+ln] = p[o:o+ln]; o += ln
    return bytes(b)
assert apply_ips(orig, patch) == pad, "IPS does not round-trip!"

open(os.path.join(REL, "steamgear-3dpad.ips"), "wb").write(patch)
shutil.copyfile(XD, os.path.join(REL, "steamgear-3dpad.xdelta"))
print(f"records={len(records)}  ips={len(patch)}B  xdelta={os.path.getsize(XD)}B")
print("round-trip OK; wrote:", REL)
