#!/usr/bin/env python3
"""Apply a filled-in translation worklist to 0.BIN -> patched 0.BIN.

Usage: apply_worklist.py <0.BIN> <worklist.json> <out_0.BIN>
Only entries with a non-empty 'translation' are changed; each is length-checked
against its byte budget (overflow aborts). Prints a summary of applied edits.
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saturn_translate import sgtext

src, wl_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()
spec = json.load(open(wl_path, encoding="utf-8"))
edits = [(e["offset"], e["translation"], e.get("budget"))
         for e in spec["strings"] if e.get("translation")]
patched = sgtext.apply_edits(data, edits)
assert len(patched) == len(data), "patch must not change file size"
with open(out, "wb") as f:
    f.write(patched)
print(f"applied {len(edits)} edit(s) -> {out} ({len(patched)} bytes, size preserved)")
for off, txt, _ in edits:
    print(f"  0x{off:06X} (HWRAM 0x{sgtext.LOAD_BASE + off:08X}): -> {txt!r}")
