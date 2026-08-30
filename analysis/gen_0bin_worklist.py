#!/usr/bin/env python3
"""Generate the Steamgear 0.BIN translation worklist JSON.

Scans 0.BIN for null-bounded ASCII strings, classifies UI text vs asset filenames,
and writes a worklist the user fills in (`translation` fields). Apply later with
saturn_translate.sgtext.apply_worklist_file().

Usage: gen_0bin_worklist.py [path/to/0.BIN] [out.json]
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saturn_translate import sgtext

zero = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "game_originals", "Steamgear Mash (Japan)", "0.BIN")
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "steamgear_0bin_worklist.json")

data = open(zero, "rb").read()
wl = sgtext.worklist(data, (0, len(data)), min_len=3, bounded=True)


def looks_english(s):
    s = s.strip()
    if len(s) < 3:
        return False
    low = s.lower()
    vowels = sum(low.count(v) for v in "aeiou")
    return (" " in s and vowels >= 1) or (vowels >= 2 and sum(c.isalpha() for c in s) >= 4)


ui = [e for e in wl if e["kind"] == "ui" and looks_english(e["text"])]
spec = {
    "_note": "Steamgear Mash 0.BIN UI strings. Fill 'translation' (ASCII, <= budget-1 "
             "chars). offset=0.BIN file offset; addr=HWRAM (=0x06004000+offset). Blank "
             "keeps original. Apply: saturn_translate.sgtext.apply_worklist_file().",
    "load_base": sgtext.LOAD_BASE,
    "strings": ui,
}
with open(out, "w", encoding="utf-8") as f:
    json.dump(spec, f, ensure_ascii=False, indent=2)
print(f"wrote {out}: {len(ui)} translatable UI strings "
      f"({len([e for e in wl if e['kind']=='filename'])} filenames excluded)")
for e in ui:
    print(f"  0x{e['offset']:06X} budget={e['budget']:3d}  {e['text']!r}")
