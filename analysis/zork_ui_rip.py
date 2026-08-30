#!/usr/bin/env python3
"""/*----------------------
 | zork_ui_rip.py
 | Description: Extract Zork I (Saturn, JP) UI art - item pictures and VDP1 cel sprites.
 | Author: suinevere
 | Dependencies: zork_cgl, zork_cels, PIL, gzip, struct
 | Globals: POOL_BASE, ITEM_W, ITEM_H, STATE_BLOCKS
 ----------------------*/

Two different sources, because the UI art lives in two different places.

* ``OITEM.CZ`` is a real container on the disc: 19 LZSS streams of 5120 bytes
  (64x80 8bpp item pictures) followed by 19 streams of 512 bytes (their RGB555
  CLUTs). These extract straight off the disc.

* Everything else the HUD draws - the four mode tabs, the movement compass, the
  command buttons, the text plates, the map screen - is **render-to-texture**.
  The pixels are composed at runtime into VDP1 VRAM and never exist as bytes in
  any file. The ``ETC\\MAPBG.CGD`` / ``ETC\\WINDOW.CGD`` style names in
  0ZORK.BIN are dev-build source paths with no matching file in the ISO.
  The only way to recover them is a Mednafen savestate: the texture pool loads
  at VDP1 VRAM ``cel_charaddr + 0x10000``, and the geometry comes from the
  45-entry cel table at 0x06078b74 (see zork_cels.py).

  A cel is only meaningful in a savestate where that screen has actually been
  drawn; otherwise the slice is stale data from whatever used the VRAM before.
"""
import gzip
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import zork_cgl
import zork_cels
from PIL import Image

# /*----------------------
#  | Rip constants
#  | Description: VRAM pool base and the OITEM picture geometry.
#  | Author: suinevere
#  ----------------------*/
POOL_BASE = 0x10000
ITEM_W = 64
ITEM_H = 80


def savestate_blocks(path):
    """/*----------------------
     | savestate_blocks
     | Description: Index a Mednafen savestate into name -> (offset, size).
     | Author: suinevere
     | Dependencies: gzip, struct
     | Globals: N/A
     | Params: path -- .mcN savestate file
     | Returns: list of (name, offset, size), in file order
     ----------------------*/"""
    d = gzip.decompress(open(path, "rb").read())
    assert d[:8] == b"MDFNSVST", "not a Mednafen savestate"
    out = []
    i = 0
    while i < len(d) - 5:
        nl = d[i]
        if 1 <= nl <= 31:
            name = d[i + 1:i + 1 + nl]
            if all(32 <= c < 127 for c in name):
                sz = struct.unpack_from("<I", d, i + 1 + nl)[0]
                doff = i + 1 + nl + 4
                if 0 < sz <= 0x100000 and doff + sz <= len(d):
                    out.append((name.decode(), doff, sz))
                    i = doff + sz
                    continue
        i += 1
    return d, out


def vdp1_vram(path):
    """/*----------------------
     | vdp1_vram
     | Description: Pull the VDP1 VRAM block (the first 512 KB VRAM var) from a savestate.
     | Author: suinevere
     | Dependencies: savestate_blocks
     | Globals: N/A
     | Params: path -- .mcN savestate file
     | Returns: (vdp1 bytes, cram bytes)
     ----------------------*/"""
    d, blocks = savestate_blocks(path)
    vrams = [b for b in blocks if b[0] == "VRAM" and b[2] == 0x80000]
    cram = [b for b in blocks if b[0] == "CRAM"]
    v1 = d[vrams[0][1]:vrams[0][1] + 0x80000]
    cr = d[cram[0][1]:cram[0][1] + cram[0][2]] if cram else b""
    return v1, cr


def rip_items(cz_bytes, out_dir):
    """/*----------------------
     | rip_items
     | Description: Decode OITEM.CZ into per-item PNGs using each item's own CLUT.
     | Author: suinevere
     | Dependencies: zork_cgl, PIL
     | Globals: ITEM_W, ITEM_H
     | Params: cz_bytes -- raw OITEM.CZ; out_dir -- directory for the PNGs
     | Returns: number of items written
     ----------------------*/"""
    os.makedirs(out_dir, exist_ok=True)
    pics, pals = [], []
    pos = 0
    while pos + 4 <= len(cz_bytes):
        size = struct.unpack_from("<I", cz_bytes, pos)[0]
        if size == 0 or size > 1 << 20:
            break
        data, nxt = zork_cgl._lzss(cz_bytes, pos)
        if len(data) != size:
            break
        (pics if size == ITEM_W * ITEM_H else pals).append(data)
        pos = (nxt + 3) & ~3
    for i, (px, pal) in enumerate(zip(pics, pals)):
        im = Image.frombytes("P", (ITEM_W, ITEM_H), px)
        flat = []
        for j in range(256):
            v = pal[j * 2] | (pal[j * 2 + 1] << 8)
            flat += [(v & 0x1f) * 255 // 31, ((v >> 5) & 0x1f) * 255 // 31,
                     ((v >> 10) & 0x1f) * 255 // 31]
        im.putpalette(flat)
        im.convert("RGB").save(os.path.join(out_dir, f"item_{i:02d}.png"))
    return len(pics)


def rip_cels(zork_bin, v1, out_dir, bpp=4):
    """/*----------------------
     | rip_cels
     | Description: Slice every sized cel out of a VDP1 VRAM image as a grayscale PNG.
     | Author: suinevere
     | Dependencies: zork_cels, PIL
     | Globals: POOL_BASE
     | Params: zork_bin -- 0ZORK.BIN bytes; v1 -- VDP1 VRAM; out_dir -- output dir
     | Returns: number of cels written
     ----------------------*/"""
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    for c in zork_cels.parse_table(zork_bin):
        if not (c["w"] and c["h"] and c["charaddr"]):
            continue
        a = c["charaddr"] + POOL_BASE
        need = c["w"] * c["h"] // 2 if bpp == 4 else c["w"] * c["h"]
        if a + need > len(v1):
            continue
        raw = v1[a:a + need]
        if bpp == 4:
            px = bytearray()
            for b in raw:
                px.append((b >> 4) * 17)
                px.append((b & 15) * 17)
        else:
            px = bytearray(raw)
        im = Image.frombytes("L", (c["w"], c["h"]), bytes(px[:c["w"] * c["h"]]))
        im.save(os.path.join(out_dir, f"cel{c['idx']:02d}_{c['w']}x{c['h']}.png"))
        n += 1
    return n


def main():
    """/*----------------------
     | main
     | Description: Rip OITEM item pictures, and cels from a savestate if one is given.
     | Author: suinevere
     | Dependencies: rip_items, rip_cels, vdp1_vram
     | Globals: N/A
     | Params: N/A (argv[1] optionally names a savestate)
     | Returns: N/A
     ----------------------*/"""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    from saturn_translate.iso import SaturnImage
    disc = os.path.join(root, "cd",
                        "Zork I - The Great Underground Empire (Japan)",
                        "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
    img = SaturnImage.from_file(disc)
    n = rip_items(img.extract("/OITEM.CZ"), os.path.join(here, "zork_ui", "items"))
    print(f"{n} item pictures -> analysis/zork_ui/items/")
    if len(sys.argv) > 1:
        zb = open(os.path.join(root, "cd",
                               "Zork I - The Great Underground Empire (Japan)",
                               "0ZORK.BIN"), "rb").read()
        v1, _ = vdp1_vram(sys.argv[1])
        m = rip_cels(zb, v1, os.path.join(here, "zork_ui", "cels"))
        print(f"{m} cels -> analysis/zork_ui/cels/  (from {os.path.basename(sys.argv[1])})")


if __name__ == "__main__":
    main()
