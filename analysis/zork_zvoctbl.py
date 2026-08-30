#!/usr/bin/env python3
"""ZVOCTBL.DAT (Saturn JP Zork parser vocabulary) decoder + Infocom cross-reference.

Layout: ~986-byte header = (start_record_index, count) bucket pairs for first-char-class lookup,
then 23-byte records from 0x3da: [id u16-LE][POS flag][SJIS keyword <=20B, NUL-padded].
POS flags: 0x80 noun, 0x20 adjective, 0x02 verb(dict), 0x40 verb(te-form).

Goal: translate noun+adjective keywords to English (cross-referenced to the Infocom ZIL vocabulary
-- SYNONYM/ADJECTIVE words), then re-encode + rebuild the bucket index. This module decodes and
emits analysis/zork_zvoctbl_xref.csv for review."""
import struct, os, re, glob, csv

REC0 = 0x3da; STRIDE = 23
POS = {0x80: "noun", 0x20: "adj", 0x02: "verb", 0x40: "verb-te"}

def load():
    return open(os.path.join(os.path.dirname(__file__), "..", "work", "zork1", "ZVOCTBL.DAT"), "rb").read()

def records(v):
    o = REC0
    while o + STRIDE <= len(v):
        idv = struct.unpack("<H", v[o:o+2])[0]; flag = v[o+2]
        e = v.find(b"\x00", o+3, o+STRIDE)
        kw = v[o+3:(e if e >= 0 else o+STRIDE)].decode("shift_jis", "replace")
        yield (o, idv, flag, kw)
        o += STRIDE

def infocom_vocab():
    """All SYNONYM + ADJECTIVE words from the Infocom ZIL object defs (the canonical English)."""
    words = set()
    for f in glob.glob(os.path.join(os.path.dirname(__file__), "..", "cd",
                                    "Zork I - The Great Underground Empire (Japan)", "zork1", "*.zil")):
        z = open(f, encoding="latin-1").read()
        for prop in ("SYNONYM", "ADJECTIVE"):
            for m in re.finditer(r"\(" + prop + r"\s+([^)]*)\)", z):
                for w in m.group(1).split():
                    if re.fullmatch(r"[A-Za-z\-]+", w): words.add(w.upper())
    return sorted(words)

if __name__ == "__main__":
    v = load()
    vocab = infocom_vocab()
    rows = [(idv, POS.get(f, hex(f)), kw) for _, idv, f, kw in records(v) if f in (0x80, 0x20)]
    here = os.path.join(os.path.dirname(__file__))
    with open(os.path.join(here, "zork_zvoctbl_xref.csv"), "w", newline="", encoding="utf-8-sig") as fh:
        w = csv.writer(fh); w.writerow(["zvoc_id", "pos", "japanese_keyword", "english_TODO"])
        w.writerows([[i, p, k, ""] for i, p, k in rows])
    with open(os.path.join(here, "infocom_vocab.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(vocab))
    print("nouns+adj records: %d -> zork_zvoctbl_xref.csv" % len(rows))
    print("Infocom vocab words: %d -> infocom_vocab.txt" % len(vocab))
    print("sample Infocom vocab:", vocab[:25])
