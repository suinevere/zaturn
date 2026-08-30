#!/usr/bin/env python3
"""Assemble English BUTTON-config labels from ONDATA.BIN's built-in ASCII font.

ONDATA.BIN (Steamgear Mash option/controller-config sprite data) stores an 8x8,
4bpp tile font that is ASCII-indexed: the tile for character C lives at byte offset
FONT_BASE + ord(C)*32 (verified: tiles 40-57 = '()*+,-./' then '0'-'9'; A-Z render
clean). Foreground nibble = 0xF (white), background = 0. The on-screen kanji action
labels (ショット/ジャンプ/ウエポン/武器選択1・2/視点変更/加速) are baked tile graphics;
to translate we overwrite each label's tiles in place with English letter tiles
copied from this same font (no pixel authoring, no palette change).

USAGE
  # show a string as ASCII art (preview the assembled tiles):
  python3 ondata_label.py preview "SHOT"
  # write English over a label: overwrite <ntiles> tiles at <dest_hex> in ONDATA
  #   with TEXT (space-padded/truncated to ntiles), output to a new file:
  python3 ondata_label.py patch analysis/ONDATA.BIN <dest_hex> <ntiles> "TEXT" out.BIN
  # render a string to PNG (sanity view):
  python3 ondata_label.py png "WEAPON 1" out.png

Once the watchpoint runbook (docs/STEAMGEAR_GHIDRA_RUNBOOK.md) gives each label's
tile-slot byte offset + width in ONDATA, drive `patch` once per label, then reinject
ONDATA via analysis/patch_image.py (separate file -> clean) and fix ECC like 0.BIN.
"""
import sys, struct, zlib

FONT_BASE = 0x410          # byte offset of tile 0 (== ASCII 0) in ONDATA.BIN
TILE_BYTES = 32            # 8x8 @ 4bpp

def font_tile(data, ch):
    """Return the 32-byte 4bpp tile for ASCII char `ch` from ONDATA `data`."""
    o = FONT_BASE + (ord(ch) & 0x7F) * TILE_BYTES
    return data[o:o + TILE_BYTES]

def label_tiles(data, text, ntiles):
    """List of `ntiles` tiles for `text` (space-padded / truncated)."""
    s = text[:ntiles].ljust(ntiles, " ")
    return [font_tile(data, c) for c in s]

def tile_ascii(tile):
    rows = []
    for y in range(8):
        s = ""
        for xb in range(4):
            b = tile[y * 4 + xb]
            for nib in (b >> 4, b & 0xF):
                s += "#" if nib >= 8 else ("." if nib == 0 else "+")
        rows.append(s)
    return rows

def _png(width, height, gray):
    raw = b"".join(b"\x00" + bytes(gray[y * width:(y + 1) * width]) for y in range(height))
    def ch(tag, d):
        c = tag + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n"
            + ch(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))

def main():
    cmd = sys.argv[1]
    if cmd == "preview":
        data = open("analysis/ONDATA.BIN", "rb").read()
        text = sys.argv[2]
        rows = [""] * 8
        for c in text:
            t = tile_ascii(font_tile(data, c))
            for y in range(8):
                rows[y] += t[y] + " "
        print("\n".join(rows))
    elif cmd == "png":
        data = open("analysis/ONDATA.BIN", "rb").read()
        text, out = sys.argv[2], sys.argv[3]
        tiles = [font_tile(data, c) for c in text]
        W = len(tiles) * 8
        gray = bytearray(W * 8)
        for i, t in enumerate(tiles):
            for y in range(8):
                for xb in range(4):
                    b = t[y * 4 + xb]
                    for k, nib in enumerate((b >> 4, b & 0xF)):
                        gray[y * W + i * 8 + xb * 2 + k] = nib * 17
        open(out, "wb").write(_png(W, 8, gray))
        print(f"wrote {out}: {W}x8 ({len(tiles)} tiles)")
    elif cmd == "patch":
        path, dest, ntiles, text, out = (
            sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4]), sys.argv[5], sys.argv[6])
        data = bytearray(open(path, "rb").read())
        tiles = label_tiles(bytes(data), text, ntiles)
        for i, t in enumerate(tiles):
            data[dest + i * TILE_BYTES: dest + (i + 1) * TILE_BYTES] = t
        open(out, "wb").write(data)
        print(f"wrote {out}: {ntiles} tiles at 0x{dest:X} <- {text!r}")
    else:
        raise SystemExit(__doc__)

if __name__ == "__main__":
    main()
