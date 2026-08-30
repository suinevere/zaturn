#!/usr/bin/env python3
"""Zork I (Saturn JP) string enumerator for 0ZORK.BIN.

Reads the string pointer tables, decodes each entry (recursively expanding the
0x0e/0x1e -> 234-dict abbreviations), and prints/saves an inventory. String ends are
taken as the next-higher pointer in the combined sorted target set (the prose is stored
contiguously; entries are delimited by the pointer table). Decoding is for ALIGNMENT to
the English original; reinsertion will relocate+repoint, so exact terminators don't matter.

Codec (see docs/ZORK1_STRINGS_RECON.md): SJIS text; 0x0e XX / 0x1e XX = dict word #XX
(234-table @0x06099a68); single control bytes 0x01..0x1d = grammar/format codes (shown raw).
"""
import struct, os, sys

BASE = 0x06004000
TABLES = {                      # name: (file_offset, n_entries)
    "rooms98":  (0x8b074, 98),
    "tbl33":    (0x8b2f4, 33),
    "dict234":  (0x95a68, 234),
    "msg777":   (0x99be8, 777),
}
DICT_OFF = 0x95a68              # 234-entry abbreviation dictionary

def load():
    here = os.path.dirname(__file__)
    return open(os.path.join(here, "..", "work", "zork1", "0ZORK.BIN"), "rb").read()

def u32(d, fo): return struct.unpack(">I", d[fo:fo + 4])[0]

def ptrs(d, off, n): return [u32(d, off + k * 4) for k in range(n)]

def decode(d, start, end, dict_ptrs, depth=0):
    out = []; i = start
    while i < end - 1 if end > start + 1 else i < end:
        b = d[i]
        if b in (0x0e, 0x1e) and i + 1 < len(d):
            ix = d[i + 1]
            if depth < 6 and ix < len(dict_ptrs):
                pp = dict_ptrs[ix] - BASE
                e = d.index(b"\x00", pp) if pp < len(d) else pp
                out.append(decode(d, pp, e, dict_ptrs, depth + 1))
            else:
                out.append("<%02x:%02x>" % (b, ix))
            i += 2; continue
        if b < 0x20:
            out.append({0x0f: "[YOU]", 0x01: "/", 0x0c: "\\n", 0x1c: "|"}.get(b, "{%02x}" % b))
            i += 1; continue
        if 0x81 <= b <= 0x9f or 0xe0 <= b <= 0xef:
            out.append(d[i:i + 2].decode("shift_jis", "replace")); i += 2; continue
        out.append(chr(b) if 32 <= b < 127 else "?"); i += 1
    return "".join(out)

def main():
    d = load()
    dict_ptrs = ptrs(d, DICT_OFF, 234)
    # combined sorted unique targets -> boundaries
    allt = set()
    for off, n in TABLES.values():
        allt.update(ptrs(d, off, n))
    sorted_t = sorted(p - BASE for p in allt if 0 <= p - BASE < len(d))
    def nextb(fo):
        import bisect
        j = bisect.bisect_right(sorted_t, fo)
        return sorted_t[j] if j < len(sorted_t) else min(fo + 200, len(d))
    out = []
    for name, (off, n) in TABLES.items():
        for idx, p in enumerate(ptrs(d, off, n)):
            fo = p - BASE
            if not (0 <= fo < len(d)): continue
            end = min(nextb(fo), fo + 300)
            txt = decode(d, fo, end, dict_ptrs)
            out.append((name, idx, p, txt))
    # report
    print("total entries:", len(out))
    for nm in TABLES:
        print("  %-9s %d entries" % (nm, sum(1 for r in out if r[0] == nm)))
    # save full dump
    here = os.path.dirname(__file__)
    dp = os.path.join(here, "zork_strings_dump.txt")
    with open(dp, "w", encoding="utf-8") as f:
        for nm, idx, p, txt in out:
            f.write("%-9s [%3d] 0x%08x  %s\n" % (nm, idx, p, txt[:200]))
    print("wrote", dp)
    # sample rooms
    print("\n--- sample rooms98 ---")
    for nm, idx, p, txt in out:
        if nm == "rooms98" and idx in (13, 15, 16, 11, 17):
            print("[%2d] %s" % (idx, txt[:120]))

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
