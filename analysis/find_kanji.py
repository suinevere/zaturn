#!/usr/bin/env python3
"""Locate Japanese (kana/kanji) text in Steamgear Mash files.

Steamgear's on-screen text is "wide" text drawn by FUN_06005020: a 0x0000-terminated
run of big-endian u16 codes where code == glyph_index * 4 (the font glyphs live at VDP2
VRAM base 0). ASCII glyphs are the thin menu font (glyph == ASCII, 0x20..0x7E) and the
chunky dialogue font (glyph == ASCII + 438, i.e. 470..563). Any glyph OUTSIDE those two
ranges is a kana/kanji glyph -> untranslated Japanese.

`find_wide_strings` over-matches: structured record/tilemap tables (e.g. FIELD_S*.BIN)
also satisfy code%4==0 and look like long runs. Real text is distinguished by NOT being
a low-entropy record table: it has glyph variety and does not use a fixed separator glyph
on a regular cadence. This tool adds those filters so only genuine text is reported.

Usage:
  python3 analysis/find_kanji.py <disc.bin>            # scan every file on the disc
  python3 analysis/find_kanji.py <disc.bin> /0.BIN     # scan one file
  python3 analysis/find_kanji.py --raw <file.BIN>      # scan an already-extracted file
"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from saturn_translate import sgtext
from saturn_translate.iso import SaturnImage

ASCII = lambda g: 0x20 <= g < 0x7F          # thin menu font
DLG = lambda g: 470 <= g < 564              # chunky dialogue font (ASCII+438)
def is_jp(g):
    return not (ASCII(g) or DLG(g))

def looks_like_record_table(glyphs):
    """Reject structured data masquerading as text: a record/tilemap table tends to
    (a) reuse a single 'separator' glyph on a regular cadence, or (b) cycle a tiny
    alphabet. Real prose has higher glyph variety and no metronomic separator."""
    n = len(glyphs)
    if n < 4:
        return False
    counts = collections.Counter(glyphs)
    top, topn = counts.most_common(1)[0]
    # one glyph is >=30% of the run (e.g. ';' every other cell) -> record table
    if topn >= max(4, n * 0.30):
        return True
    # tiny alphabet relative to length -> cycling record data
    if len(counts) <= max(6, n // 6) and n >= 24:
        return True
    return False

def scan(data, min_codes=3):
    runs = sgtext.find_wide_strings(data, min_codes=min_codes, gmin=0x20, gmax=0x400)
    out = []
    for off, codes in runs:
        gs = [c // 4 for c in codes]
        if not any(is_jp(g) for g in gs):
            continue                       # pure ASCII (already English / not JP)
        if looks_like_record_table(gs):
            continue
        out.append((off, codes, sum(1 for g in gs if is_jp(g))))
    return out

def report(name, data):
    hits = scan(data)
    if not hits:
        return 0
    print(f"=== {name} ({len(data)} B): {len(hits)} JP text run(s) ===")
    for off, codes, njp in sorted(hits, key=lambda r: r[0]):
        print(f"  0x{off:05X} ({len(codes):2d} codes, {njp} JP) {sgtext.decode_wide_codes(codes)}")
    return len(hits)

def main():
    args = sys.argv[1:]
    if args and args[0] == "--raw":
        data = open(args[1], "rb").read()
        report(args[1], data)
        return
    disc = args[0]
    img = SaturnImage.from_file(disc)
    if len(args) > 1:
        report(args[1], img.extract(args[1]))
        return
    total = 0
    for f in img.list_files():
        if "CDDA" in f.name or f.name.endswith((".CPK", ".TXT", ".TSK")):
            continue
        try:
            data = img.extract("/" + f.name.lstrip("/"))
        except Exception:
            continue
        total += report(f.name, data)
    print(f"\n{total} JP text run(s) across the disc (record tables / graphics filtered out).")

if __name__ == "__main__":
    main()
