#!/usr/bin/env python3
"""Zork I (Saturn) — pull VDP1 VRAM from a Mednafen savestate and render the tab plates.

Confirmed on live state (West-of-House, slot-1 savestate, 2026-06-22):
  * Mednafen savestate = gzip; header 'MDFNSVST'; vars = [namelen:1][name][size:4 LE][data].
    Two 'VRAM' blocks (1st = VDP1 VRAM, 2nd = VDP2 VRAM) + '&FB[0][0]' (VDP1 framebuffer).
  * Texture pool loads into VDP1 VRAM at **base 0x10000**:  VDP1_byte = cel_charaddr + 0x10000
    (cel table -> analysis/zork_cels.py; tab cels = idx 13-16, charaddr 0x8280/0x8580/0x8880/0x8b80).
  * The 4 tab plates (オブジェクト/動作/持物/移動) are **4bpp 16-colour 48x16** sprites at VDP1
    0x18400/0x18580/0x18880/0x18b80 (CMDPMOD=0x0080, CMDCOLR 0x410/0x420/0x440/0x460) -> baked kanji.
  * Body text = separate 256-colour 8bpp 16x16 glyph sprites (CMDPMOD=0x00a0), expanded from SJIS.CGD.
  * Plate pixels are render-to-texture (not byte-present in INIT1/INIT2/SINIT2/OITEM .dec).

Usage: python zork_state_vram.py "<path to .mcN>"   (auto-picks newest Zork .mc* if omitted)
"""
import gzip, struct, sys, glob, os
from PIL import Image, ImageOps

POOL_BASE = 0x10000          # VDP1 VRAM byte offset where the texture pool loads
TAB_CHARADDR = (0x8280, 0x8580, 0x8880, 0x8b80)   # cel idx 13-16


def load_state(path):
    data = gzip.decompress(open(path, "rb").read())
    assert data[:8] == b"MDFNSVST", "not a Mednafen savestate"
    return data


def find_vars(data, want_size=0x80000):
    """Yield (name, dataoffset, size) for vars whose payload size == want_size."""
    i = 0
    while i < len(data) - 5:
        nl = data[i]
        if 1 <= nl <= 31:
            name = data[i + 1:i + 1 + nl]
            if all(32 <= c < 127 for c in name):
                sz = struct.unpack_from("<I", data, i + 1 + nl)[0]
                doff = i + 1 + nl + 4
                if sz == want_size:
                    yield name.decode(), doff, sz
                    i = doff + sz
                    continue
        i += 1


def vdp1_vram(data):
    """First 0x80000 'VRAM' var = VDP1 VRAM."""
    for name, doff, sz in find_vars(data):
        if name == "VRAM":
            return data[doff:doff + sz]
    raise RuntimeError("VDP1 VRAM not found")


def render_plate_4bpp(vram, vdp1_addr, w=48, h=16, scale=6):
    n = w * h // 2
    px = bytearray()
    for b in vram[vdp1_addr:vdp1_addr + n]:
        px.append(b >> 4); px.append(b & 0xf)
    im = Image.frombytes("L", (w, h), bytes(v * 16 for v in px[:w * h]))
    return ImageOps.autocontrast(im).resize((w * scale, h * scale), Image.NEAREST)


def main():
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        cands = sorted(glob.glob("mednafen-1.32.1-win64/mcs/Zork*.mc*"),
                       key=os.path.getmtime, reverse=True)
        if not cands:
            sys.exit("no Zork savestate found")
        path = cands[0]
    print("state:", path)
    vram = vdp1_vram(load_state(path))
    imgs = [render_plate_4bpp(vram, POOL_BASE + c) for c in TAB_CHARADDR]
    W = sum(i.width for i in imgs) + 40
    sheet = Image.new("L", (W, imgs[0].height), 60)
    x = 0
    for im in imgs:
        sheet.paste(im, (x, 0)); x += im.width + 10
    outp = "analysis/zork_vram/tab_plates.png"
    sheet.save(outp)
    print("wrote", outp, "(tabs idx13-16 @ VDP1",
          ", ".join(hex(POOL_BASE + c) for c in TAB_CHARADDR), ")")


if __name__ == "__main__":
    main()
