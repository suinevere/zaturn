#!/usr/bin/env python3
"""Audit English-translation coverage of the Zork I (Saturn) prose tables.

For rooms98 / dict234 / msg777: compare the translation data dicts (ROOMS/DICT/MSGS) against the
ORIGINAL 0ZORK.BIN table entries. An entry is:
  * TRANSLATED  - index present in the data dict
  * TODO        - not translated AND its original pointer targets a NON-EMPTY, unique string
                  (these render as garbage in the engflag +0x1F build until translated)
  * empty/dup   - not translated but points to an empty string or shares a pointer with another
                  entry (no separate translation needed)
Prints a summary and writes the TODO list (index + JP preview + byte length) to a CSV.
"""
import os, sys, struct, csv
HERE = os.path.dirname(__file__)
sys.path.insert(0, HERE)
from zork_data.dict_words import DICT
from zork_data.rooms import ROOMS
from zork_data.messages import MSGS

BASE = 0x06004000
ZORK = os.path.join(HERE, "..", "cd",
                    "Zork I - The Great Underground Empire (Japan)", "0ZORK.BIN")
TBL = {"rooms98": (0x8b074, 98, ROOMS), "dict234": (0x95a68, 234, DICT), "msg777": (0x99be8, 777, MSGS)}
OUT_CSV = os.path.join(HERE, "zork_coverage_todo.csv")


def load():
    return open(ZORK, "rb").read()


def follow(a, ptr, maxlen=160):
    fo = ptr - BASE
    if not (0 <= fo < len(a)):
        return None
    end = a.find(b"\x00", fo)
    if end < 0:
        end = min(fo + maxlen, len(a))
    return a[fo:end]


def preview(raw):
    # render tokens explicitly; SJIS-decode the text runs between them (no cross-token concatenation)
    out = []; run = bytearray(); i = 0
    def flush():
        if run:
            out.append(run.decode("shift_jis", "replace").replace("�", ".")); run.clear()
    while i < len(raw):
        b = raw[i]
        if b == 0x0e and i + 1 < len(raw):   flush(); out.append("{obj%d}" % raw[i + 1]); i += 2; continue
        if b == 0x1e and i + 1 < len(raw):   flush(); out.append("{room%d}" % raw[i + 1]); i += 2; continue
        if b == 0x0f:                        flush(); out.append("\\n"); i += 1; continue
        if b < 0x20:                         flush(); out.append("<%02x>" % b); i += 1; continue
        run.append(b); i += 1
    flush()
    return "".join(out)[:60]


def real_text_len(raw):
    # count only SJIS text bytes (exclude tokens/control), to tell real prose from stubs
    return sum(1 for b in raw if b >= 0x20)


def main():
    a = load()
    rows = []
    # dict234 low indices are runtime grammar-glue / conditional expanders, never translated as words
    GLUE = {"dict234": {1, 4, 5, 6, 7, 8, 12}}
    for tname, (toff, n, data) in TBL.items():
        ptrs = [struct.unpack(">I", a[toff + i * 4:toff + i * 4 + 4])[0] for i in range(n)]
        lens = [len(follow(a, p) or b"") for p in ptrs]
        from collections import Counter
        pc = Counter(ptrs)
        def is_subwindow(i):
            pi, li = ptrs[i], lens[i]
            for j in range(n):
                if j != i and lens[j] > li and ptrs[j] <= pi < ptrs[j] + lens[j]:
                    return True     # starts inside a longer sibling -> aggregate sub-window
            return False
        translated = real = stub = empty = dup = glue = sub = 0
        for i, p in enumerate(ptrs):
            if i in data:
                translated += 1; continue
            raw = follow(a, p)
            if raw is None or len(raw) == 0:
                empty += 1; continue
            if pc[p] > 1:
                dup += 1; continue
            if i in GLUE.get(tname, ()):
                glue += 1; rows.append((tname, i, real_text_len(raw), "glue", preview(raw))); continue
            if is_subwindow(i):
                sub += 1; rows.append((tname, i, real_text_len(raw), "subwindow", preview(raw))); continue
            tlen = real_text_len(raw)
            kind = "real" if tlen >= 4 else "stub"
            if kind == "real": real += 1
            else:              stub += 1
            rows.append((tname, i, tlen, kind, preview(raw)))
        print("%-8s  total %3d  translated %3d  REAL-TODO %3d  subwindow %3d  glue %3d  stub %3d  empty %3d  shared %3d"
              % (tname, n, translated, real, sub, glue, stub, empty, dup))
    order = {"real": 0, "stub": 1, "subwindow": 2, "glue": 3}
    rows.sort(key=lambda r: (r[0], order.get(r[3], 9), r[1]))
    with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(["table", "index", "text_len", "kind", "jp_preview"])
        w.writerows(rows)
    nreal = sum(1 for r in rows if r[3] == "real")
    print("\nreal untranslated prose: %d  (+ %d stubs)  ->  %s"
          % (nreal, len(rows) - nreal, os.path.basename(OUT_CSV)))


if __name__ == "__main__":
    main()
