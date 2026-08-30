#!/usr/bin/env python3
"""Zork I (Saturn, JP) UI cel system — parser for the code-side cel table.

The on-screen UI (the 4 mode tabs, command buttons, save-screen icons, compass,
etc.) is NOT live font rendering and NOT stored in the .SLD files as an index.
It is a **draw-cel-by-index** system whose geometry lives in a code-side table
inside 0ZORK.BIN.

Draw function  : 0x0601b95c   signature  draw_cel(r4=index, r5=y, r6=x)
Cel table      : 0x06078b74   (file offset 0x74b74)
Record size    : 28 bytes (0x1c)   -> ~45 cels (index 0..44)
Sprite-attr    : table 0x0605e984, 148-byte (0x94) stride per active sprite,
                 cleared via 0x06056cc4 before each draw.

Per the disassembly of 0x0601b95c the record fields used are:
  +0  word0  = sprite-attribute slot number (slot = 0x0605e984 + word0*148)
  +2  word1  = width   (px)
  +4  word2  = height  (px)
  +6  word3  = (display w)   +8 word4 = (display h)   copied to cmd +76/+78
  +12 long   = VDP1 character source address (word6:word7) -> cmd char-addr field
  ...         (remaining words: palette/flags; word11 often 0xffff)

THE 4 MODE TABS = cel indices 13,14,15,16.
They are emitted by the gameplay HUD routine at 0x060090a2 at y=-128 (screen top),
x = 60,72,84,96.  Their char addresses are 0x8280, 0x8580, 0x8880, 0x8b80
(contiguous, +0x300 = 48*16 @ 8bpp each).  So translating the tabs means editing
the 48x16 cel pixels in the loaded texture pool (repaint) OR repointing the four
records' char-address long at table+13*28+12 .. +16*28+12 to English cels.

Other notable draw clusters (index @ y / x):
  save screen 0x060124f6 : idx 4,10,11 (icons, x=-112) + 34,35 (x=74,94)
  sub-button row 0x0602592c: idx 20,21,22,23 (24x16, y=0, x=59/71/84/97)
"""
import struct
import sys

BASE = 0x06004000
CEL_TABLE = 0x06078b74
DRAW_FN = 0x0601b95c
REC = 28
TAB_INDICES = (13, 14, 15, 16)


def parse_table(data, n=45):
    """Return list of dicts for cel records 0..n-1."""
    cels = []
    for i in range(n):
        off = (CEL_TABLE - BASE) + i * REC
        w = struct.unpack_from(">14H", data, off)
        cels.append({
            "idx": i,
            "slot": w[0],
            "w": w[1], "h": w[2],
            "dw": w[3], "dh": w[4],
            "charaddr": (w[6] << 16) | w[7],
            "pal": w[10], "flag": w[11],
            "ptr": (w[12] << 16) | w[13],
            "raw": w,
        })
    return cels


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "work/zork1/0ZORK.BIN"
    data = open(path, "rb").read()
    cels = parse_table(data)
    print(f"cel table @ {CEL_TABLE:#08x}  draw_fn @ {DRAW_FN:#08x}  ({len(cels)} cels)")
    print(f"{'idx':>3} {'slot':>4} {'wxh':>8} {'charaddr':>9} {'pal':>4} {'flag':>5} {'extptr':>9}  note")
    for c in cels:
        note = "<<< TAB" if c["idx"] in TAB_INDICES else ""
        ext = f"{c['ptr']:08x}" if c["ptr"] else ""
        print(f"{c['idx']:>3} {c['slot']:>4} {c['w']:>3}x{c['h']:<4} "
              f"{c['charaddr']:>9x} {c['pal']:>4x} {c['flag']:>5x} {ext:>9}  {note}")


if __name__ == "__main__":
    main()
