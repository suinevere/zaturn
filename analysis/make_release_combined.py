#!/usr/bin/env python3
"""Build the combined Steamgear release: English translation + 3D Control Pad, both as xdelta and IPS.

Both patches transform a CLEAN Japanese Track-01 image. They edit disjoint disc sectors, so they can
be applied independently or stacked (apply one, then the other, in any order). This emits all four
patch files into one release folder and verifies every round-trip plus the stacked combination.
"""
import sys, os, shutil
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from saturn_translate import ips, vcdiff, iso, ecc

ORIG  = os.path.join(ROOT, "game_originals", "Steamgear Mash (Japan)", "Steamgear Mash (Japan) (Track 01).bin")
TRANS = os.path.join(ROOT, "game_patched", "steamgear_mash_translation", "Steamgear Mash (Japan) (Track 01).bin")
PAD   = os.path.join(ROOT, "game_patched", "steamgear_mash_3dpad", "Steamgear Mash (Japan) (Track 01).bin")
REL   = os.path.join(ROOT, "releases", "Steamgear Mash (Japan) (English + 3D Control Pad V1.0)")
SS = 2352

def diff_runs(a, b):
    out = []; i = 0
    while i < len(a):
        if a[i] != b[i]:
            j = i
            while j < len(a) and a[j] != b[j]: j += 1
            out.append((i, b[i:j])); i = j
        else:
            i += 1
    return out

def apply_ips(src, p):
    b = bytearray(src); assert p[:5] == b"PATCH"; o = 5
    while p[o:o+3] != b"EOF":
        off = (p[o]<<16)|(p[o+1]<<8)|p[o+2]; o += 3
        ln = (p[o]<<8)|p[o+1]; o += 2
        if ln == 0:
            rl = (p[o]<<8)|p[o+1]; o += 2; v = p[o]; o += 1; b[off:off+rl] = bytes([v])*rl
        else:
            b[off:off+ln] = p[o:o+ln]; o += ln
    return bytes(b)

def make_ips(a, b):
    p = ips.encode([ips.Record(offset=o, data=d) for o, d in diff_runs(a, b)])
    assert apply_ips(a, p) == b, "IPS round-trip failed"
    return p

def make_xdelta(a, b):
    edits = [vcdiff.Edit(offset=s*SS, old_len=SS, data=b[s*SS:(s+1)*SS])
             for s in range(len(a)//SS) if a[s*SS:(s+1)*SS] != b[s*SS:(s+1)*SS]]
    p = vcdiff.encode(a, edits)
    assert vcdiff.decode(a, p) == b, "xdelta round-trip failed"
    return p

orig = open(ORIG, "rb").read(); trans = open(TRANS, "rb").read(); pad = open(PAD, "rb").read()

eng_ips, eng_xd = make_ips(orig, trans), make_xdelta(orig, trans)
pad_ips, pad_xd = make_ips(orig, pad),   make_xdelta(orig, pad)

# composition: sectors disjoint, stacking in either order yields a valid combined disc
tsec = [s for s in range(len(orig)//SS) if orig[s*SS:(s+1)*SS] != trans[s*SS:(s+1)*SS]]
psec = [s for s in range(len(orig)//SS) if orig[s*SS:(s+1)*SS] != pad[s*SS:(s+1)*SS]]
assert not (set(tsec) & set(psec)), "sector overlap!"
comb = bytearray(orig)
for s in tsec: comb[s*SS:(s+1)*SS] = trans[s*SS:(s+1)*SS]
for s in psec: comb[s*SS:(s+1)*SS] = pad[s*SS:(s+1)*SS]
assert all(ecc.sector_is_valid(comb[s*SS:(s+1)*SS]) for s in tsec+psec), "combined ECC invalid"
assert vcdiff.decode(trans, pad_xd) == bytes(comb), "3d xdelta on translated != combined"
assert vcdiff.decode(pad, eng_xd) == bytes(comb), "english xdelta on 3d != combined"
assert apply_ips(apply_ips(orig, eng_ips), pad_ips) == bytes(comb), "stacked IPS != combined"

os.makedirs(REL, exist_ok=True)
open(os.path.join(REL, "steamgear-english.xdelta"), "wb").write(eng_xd)
open(os.path.join(REL, "steamgear-english.ips"), "wb").write(eng_ips)
open(os.path.join(REL, "steamgear-3dpad.xdelta"), "wb").write(pad_xd)
open(os.path.join(REL, "steamgear-3dpad.ips"), "wb").write(pad_ips)
print(f"english: xdelta {len(eng_xd)}B / ips {len(eng_ips)}B   3dpad: xdelta {len(pad_xd)}B / ips {len(pad_ips)}B")
print(f"sectors  english={tsec}  3dpad={psec}  (disjoint, combined ECC OK, stacking verified both orders)")
print("wrote:", REL)
