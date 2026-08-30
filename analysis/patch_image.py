#!/usr/bin/env python3
"""Reinject a patched file into a Saturn data-track image, fix EDC/ECC, emit xdelta.

Usage: patch_image.py <track01.bin> <iso_path e.g. /0.BIN> <patched_file> <out_track.bin>
Writes the patched track (ECC-corrected for changed sectors) and an xdelta3-style
VCDIFF patch (<out>.xdelta) transforming the original track -> patched track.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saturn_translate import iso, ecc, vcdiff

track_path, iso_name, patched_path, out_path = sys.argv[1:5]
raw = bytearray(open(track_path, "rb").read())
img = iso.SaturnImage(bytes(raw))
print(f"layout: sector_size={img.sector_size} data_offset={img.data_offset} "
      f"({len(raw)//img.sector_size} sectors)")

entry = img.find(iso_name)
assert entry, f"{iso_name} not found in image"
orig_file = img.read_extent(entry.lba, entry.size)
new_file = open(patched_path, "rb").read()
assert len(new_file) == len(orig_file), \
    f"size mismatch: image has {len(orig_file)}, patched {len(new_file)}"
print(f"{iso_name}: LBA={entry.lba} size={entry.size} (verified extract matches)")

# changed file offsets
changed = [o for o in range(len(orig_file)) if orig_file[o] != new_file[o]]
print(f"changed bytes in file: {len(changed)} "
      f"(0x{min(changed):X}..0x{max(changed):X})" if changed else "no changes")

# write each changed byte into the raw image; collect modified raw sectors
mod_sectors = set()
for o in changed:
    sec = entry.lba + o // iso.SECTOR_USER
    in_sec = o % iso.SECTOR_USER
    rawoff = sec * img.sector_size + img.data_offset + in_sec
    raw[rawoff] = new_file[o]
    mod_sectors.add(sec)
print(f"raw sectors touched: {sorted(mod_sectors)}")

# verify originals were valid Mode-1, fix ECC, re-verify
for sec in sorted(mod_sectors):
    start = sec * img.sector_size
    ecc.fix_image_at(raw, start)
    assert ecc.sector_is_valid(raw[start:start + img.sector_size]), f"ECC fix failed @sec {sec}"
print(f"EDC/ECC recomputed + verified for {len(mod_sectors)} sector(s)")

# sanity: re-extract the file from the patched image == patched file
img2 = iso.SaturnImage(bytes(raw))
assert img2.read_extent(entry.lba, entry.size) == new_file, "round-trip extract mismatch"
print("round-trip: patched image extracts the patched file byte-exact")

with open(out_path, "wb") as f:
    f.write(raw)
print(f"wrote patched track: {out_path} ({len(raw)} bytes)")

# xdelta (VCDIFF) original track -> patched track
orig_track = bytes(open(track_path, "rb").read())
edits = []
for sec in sorted(mod_sectors):
    start = sec * img.sector_size
    edits.append(vcdiff.Edit(offset=start, old_len=img.sector_size,
                             data=bytes(raw[start:start + img.sector_size])))
patch = vcdiff.encode(orig_track, edits)
assert vcdiff.decode(orig_track, patch) == bytes(raw), "xdelta does not round-trip"
xd = out_path + ".xdelta"
open(xd, "wb").write(patch)
print(f"wrote xdelta: {xd} ({len(patch)} bytes, verified round-trip)")
