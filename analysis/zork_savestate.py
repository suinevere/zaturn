#!/usr/bin/env python3
"""Zork I (Saturn JP) Mednafen-savestate RAM helper.

Mednafen savestates (mcs/*.mcN) are gzip-compressed MDFNSVST containers. The Saturn
WorkRAM regions are stored as named variables, 16-bit byte-swapped:
    WorkRAMH -> HWRAM 0x06000000..0x060fffff (1 MB)
    WorkRAML -> LWRAM 0x00200000..0x002fffff (1 MB)  (a.k.a. 0x20200000 mirror)

    load_ram(path, "WorkRAMH") -> (base_addr, bytes)

We locate a named var by its ASCII name; the 4 bytes preceding the name are the LE
size; data follows the name. (Validated by size == 0x100000 for the 1 MB regions.)
"""
import gzip, struct, os, sys


def _swap16(b: bytes) -> bytes:
    o = bytearray(b)
    o[0::2], o[1::2] = b[1::2], b[0::2]
    return bytes(o)


def _raw(path: str) -> bytes:
    with open(path, "rb") as f:
        head = f.read(2)
    if head == b"\x1f\x8b":
        return gzip.open(path, "rb").read()
    return open(path, "rb").read()


def find_var(path: str, name: str):
    """Return (size, data_bytes) for the named savestate variable (data still swapped)."""
    d = _raw(path)
    nb = name.encode("ascii")
    i = 0
    while True:
        i = d.find(nb, i)
        if i < 0:
            raise ValueError("var %r not found" % name)
        # mednafen layout: [name padded to 32][size u32 LE][data]
        # try a few candidate framings; accept the one whose size is sane.
        for namelen in (32, 16, len(nb)):
            szoff = i + namelen
            if szoff + 4 <= len(d):
                sz = struct.unpack_from("<I", d, szoff)[0]
                if 0 < sz <= 0x200000 and szoff + 4 + sz <= len(d):
                    return sz, d[szoff + 4: szoff + 4 + sz]
        i += 1


def load_ram(path: str, name: str = "WorkRAMH"):
    sz, data = find_var(path, name)
    base = {"WorkRAMH": 0x06000000, "WorkRAML": 0x00200000}.get(name, 0)
    return base, _swap16(data)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        try:
            base, ram = load_ram(p, "WorkRAMH")
            # confirm anchor: find SJIS 小さな郵便箱 somewhere in HWRAM
            needle = "小さな郵便箱".encode("shift_jis")
            hits = []
            o = ram.find(needle)
            while o >= 0 and len(hits) < 8:
                hits.append(base + o)
                o = ram.find(needle, o + 1)
            print("%s  HWRAM %d B; 小さな郵便箱 @ %s" % (
                os.path.basename(p), len(ram), [hex(h) for h in hits]))
        except Exception as e:
            print("%s  ERR %s" % (os.path.basename(p), e))
