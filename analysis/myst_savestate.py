#!/usr/bin/env python3
"""Myst (Saturn) Mednafen-savestate RAM helper.

Mednafen savestates (mcs/*.mcN) are gzip-compressed MDFNSVST containers; the Saturn
HWRAM is stored 16-bit byte-swapped. We anchor HWRAM by locating a known A.BIN code
signature (the SMPC input routine at A.BIN file offset 0x37fa0 = addr 0x06047fa0).

    load_hwram(path) -> bytes   # 1 MB, index i == Saturn addr 0x06000000 + i (big-endian)

Findings (2026-06-19): cursor X=0x06078000 (u16 0..319), Y=0x06078002 (u16 0..223).
"""
import gzip, struct, os

ABIN = os.path.join(os.path.dirname(__file__), "..", "work", "myst", "A.BIN")


def _swap16(b: bytes) -> bytes:
    o = bytearray(b)
    o[0::2], o[1::2] = b[1::2], b[0::2]
    return bytes(o)


def load_hwram(path: str, abin_path: str = ABIN) -> bytes:
    """Return corrected (big-endian) 1 MB HWRAM image; index = addr - 0x06000000."""
    a = open(abin_path, "rb").read()
    sig = a[0x37FA0:0x37FC0]                      # unique input-routine bytes
    d = gzip.open(path, "rb").read()
    i = d.find(_swap16(sig))
    if i < 0:
        raise ValueError("A.BIN signature not found (wrong game/state?)")
    hwbase = i - 0x47FA0                          # 0x06047fa0 - 0x06000000
    return _swap16(d[hwbase:hwbase + 0x100000])


def u16(ram: bytes, addr: int) -> int:
    o = addr - 0x06000000
    return (ram[o] << 8) | ram[o + 1]


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        ram = load_hwram(p)
        print("%s  cursorX=%d cursorY=%d" % (
            os.path.basename(p), u16(ram, 0x06078000), u16(ram, 0x06078002)))
