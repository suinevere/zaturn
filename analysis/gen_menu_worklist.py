#!/usr/bin/env python3
"""Generate the Steamgear 2-byte (menu) translation worklist JSON.

Scans 0.BIN for FUN_06005020 wide strings (16-bit codes = glyph_index*4) and writes a
worklist. Each entry's `text` shows ASCII glyphs directly and kana/kanji as {gNNN} (glyph
index); render those indices from a savestate's VRAM base 0 (analysis/render_menu.py) to
read them. Fill `translation` with English ASCII; apply with sgtext.apply_worklist_file()
(it encodes wide entries as code=ord(ch)*4). Budget = bytes (2/char + 2-byte NUL).

Usage: gen_menu_worklist.py [0.BIN] [out.json] [lo_hex] [hi_hex]
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saturn_translate import sgtext

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
zero = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    root, "game_originals", "Steamgear Mash (Japan)", "0.BIN")
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "steamgear_menu_worklist.json")
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x49C00
hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x49E20

data = open(zero, "rb").read()
wl = sgtext.wide_worklist(data, (lo, hi))
spec = {
    "_note": "Steamgear Mash 2-byte menu strings (FUN_06005020, code=glyph*4). Fill "
             "'translation' (ASCII). {gNNN}=kana glyph index — render with render_menu.py to "
             "read. Budget is BYTES (2/char + 2-byte NUL). kind=wide. Apply: "
             "saturn_translate.sgtext.apply_worklist_file().",
    "load_base": sgtext.LOAD_BASE,
    "strings": wl,
}
with open(out, "w", encoding="utf-8") as f:
    json.dump(spec, f, ensure_ascii=False, indent=2)
print(f"wrote {out}: {len(wl)} wide menu strings (0x{lo:X}-0x{hi:X})")
for e in wl:
    glyph_csv = ",".join(str(g) for g in e["glyphs"])
    print(f"  0x{e['offset']:05X} bud={e['budget']:2d}  {e['text']}")
    print(f"          render: python analysis/render_menu.py <ss.mc0> r.png 0 {glyph_csv}")
