#!/usr/bin/env python3
"""Decode Zork I (Saturn JP) msg777 / rooms98 entries with tokens resolved, for translation.

msg777 messages embed: 0x0e XX / 0x1e XX = dict234 word #XX (object names / room titles), 0x0f =
player ("you"), plus single-byte abbreviation/format codes (shown as <xx>). This walks an entry's
bytes and renders [WORD] for tokens so the message is legible. Resolver uses ORIGINAL 0ZORK.BIN
dict234 (table @0x95a68 -> packed JP word blob)."""
import struct, os

BASE = 0x06004000
DICT_TBL = 0x95a68          # dict234 table (file offset)
MSG_TBL  = 0x99be8          # msg777 table
ROOM_TBL = 0x8b074          # rooms98 table

def _load():
    here = os.path.dirname(__file__)
    return open(os.path.join(here, "..", "work", "zork1", "0ZORK.BIN"), "rb").read()

def _all_ptrs(a):
    """Sorted unique set of all string pointers across the 4 tables (string boundaries)."""
    pts = set()
    for tbl, n in ((ROOM_TBL, 98), (0x8b2f4, 33), (DICT_TBL, 234), (MSG_TBL, 777)):
        for k in range(n):
            v = struct.unpack(">I", a[tbl + k*4: tbl + k*4 + 4])[0]
            if BASE <= v < BASE + len(a):
                pts.add(v)
    return sorted(pts)

_PTRS = None
def _next_ptr(a, addr):
    """File offset of the next-higher string pointer after `addr` (a string's end boundary)."""
    global _PTRS
    if _PTRS is None: _PTRS = _all_ptrs(a)
    import bisect
    i = bisect.bisect_right(_PTRS, addr)
    return (_PTRS[i] - BASE) if i < len(_PTRS) else len(a)

def dict_word(a, idx):
    """JP word for dict234[idx] (first word, bounded by next pointer or 0x00)."""
    p = struct.unpack(">I", a[DICT_TBL + idx*4: DICT_TBL + idx*4 + 4])[0]
    fo = p - BASE; end = _next_ptr(a, p)
    nul = a.find(b"\x00", fo, end)
    return a[fo:(nul if nul >= 0 else end)].decode("shift_jis", "replace")

def decode(a, addr_or_off, tbl=None, idx=None):
    if tbl is not None:
        p = struct.unpack(">I", a[tbl + idx*4: tbl + idx*4 + 4])[0]; fo = p - BASE
    else:
        fo = addr_or_off; p = fo + BASE
    end = _next_ptr(a, p)                      # bound at next string pointer
    out = []; i = fo
    while i < end:
        c = a[i]
        if c == 0x00: break
        if c in (0x0e, 0x1e) and i+1 < len(a):
            w = dict_word(a, a[i+1]); out.append(("{%s}" if c == 0x1e else "[%s]") % w); i += 2; continue
        if c == 0x0f: out.append("<YOU>"); i += 1; continue
        if c < 0x20: out.append("<%02x>" % c); i += 1; continue
        if 0x81 <= c <= 0x9f or 0xe0 <= c <= 0xef:        # SJIS double-byte
            out.append(a[i:i+2].decode("shift_jis", "replace")); i += 2; continue
        out.append(chr(c)); i += 1
    return "".join(out)

def decode_segments(a, idx):
    """Structured decode of msg777[idx]: list of ('room',i)/('obj',i)/('text',s)/('ctrl',c)."""
    p = struct.unpack(">I", a[MSG_TBL + idx*4: MSG_TBL + idx*4 + 4])[0]; fo = p - BASE
    end = _next_ptr(a, p)
    segs = []; i = fo; buf = []
    def flush():
        if buf: segs.append(("text", "".join(buf))); buf.clear()
    while i < end:
        c = a[i]
        if c == 0x00: break
        if c in (0x0e, 0x1e) and i+1 < end:
            flush(); segs.append(("room" if c == 0x1e else "obj", a[i+1])); i += 2; continue
        if c < 0x20:
            flush(); segs.append(("ctrl", c)); i += 1; continue
        if 0x81 <= c <= 0x9f or 0xe0 <= c <= 0xef:
            buf.append(a[i:i+2].decode("shift_jis", "replace")); i += 2; continue
        buf.append(chr(c)); i += 1
    flush()
    return segs

if __name__ == "__main__":
    import sys
    a = _load()
    if sys.argv[1:2] == ["-s"]:
        for ix in [int(x) for x in sys.argv[2:]]:
            print("[%3d] %s" % (ix, " ".join(
                ("{r%02x=%s}" % (v, dict_word(a, v))) if k == "room" else
                ("[o%02x=%s]" % (v, dict_word(a, v))) if k == "obj" else
                ("<%02x>" % v) if k == "ctrl" else repr(v) for k, v in decode_segments(a, ix))))
    else:
        idxs = [int(x) for x in sys.argv[1:]] if len(sys.argv) > 1 else range(0, 80)
        for ix in idxs:
            print("[%3d] %s" % (ix, decode(a, None, MSG_TBL, ix)))
