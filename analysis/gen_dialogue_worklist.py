#!/usr/bin/env python3
"""Extract the Steamgear dialogue script (0.BIN ~0x5AF40-0x5BBC0) into a worklist + layout.

Lines are fixed-width, split on 0xFFFF/0x0000. {gNNN} = dialogue-font glyph index (read off
the game screenshots / render_menu.py). Fill `translation` (ASCII, <= ncodes chars; English
= code*4). Apply: sgtext.apply_worklist_file().
"""
import sys, os, json
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, root)
from saturn_translate import sgtext

zero = os.path.join(root, "game_originals", "Steamgear Mash (Japan)", "0.BIN")
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "steamgear_dialogue_worklist.json")
LO, HI = 0x5AF40, 0x5BBC0
data = open(zero, "rb").read()
wl = sgtext.dialogue_worklist(data, LO, HI)
json.dump({"_note": "Steamgear dialogue (FUN_06005020, code=glyph*4, dialogue font 470-730). "
                    "Fixed-width: translation <= ncodes ASCII chars. {gNNN}=kana glyph. "
                    "kind=dialogue. Apply: sgtext.apply_worklist_file().",
           "load_base": sgtext.LOAD_BASE, "strings": wl},
          open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
print(f"wrote {out}: {len(wl)} dialogue lines (0x{LO:X}-0x{HI:X})\n")
# layout: pair lines into boxes (a 0x0000 ends a box; blocks are space-padded)
for i, e in enumerate(wl):
    g = e["glyphs"]
    print(f"L{i:02d} 0x{e['offset']:05X} ({e['ncodes']:2d}c): {e['text']}")
