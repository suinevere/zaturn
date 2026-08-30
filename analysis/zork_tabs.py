#!/usr/bin/env python3
"""Zork I (Saturn, JP) — the 4 mode-tab plates (baked kanji) in SINIT2.SLD.

SOLVED 2026-06-22 (static RE + live savestate). The persistent bottom-left tabs
オブジェクト/動作/持物/移動 (Object/Action/Inventory/Move) are NOT live font glyphs and
NOT repointable cels — they are **baked 4bpp 48x16 plates** stored in SINIT2.SLD.

Pipeline that puts them on screen:
  HUD frame 0x06009030 -> builder 0x06009fa4 block-copies the UI sprite region from the
  loaded SINIT2 (*0x0605e960 + 0xe9a8) into VDP1 sprite-texture pool 0x25C10000, applying a
  nibble-swap + 16-bit byte-swap. The DMA is why CPU write-watchpoints never catch the kanji
  write (only the prior clear). Draw phase 0x0600909c calls cel-draw 0x0601b95c for cel
  indices 13-16 (cel table 0x06078b74 -> analysis/zork_cels.py); pool base in VDP1 = 0x10000.

Plate locations in **decompressed SINIT2** (each 48x16 4bpp = 0x180 bytes):
  オブジェクト 0x6628 | 動作 0x6928 | 持物 0x6c28 | 移動 0x6f28
  (mapping: file_off = VDP1_addr - 0x11C58 ; VDP1_addr = cel charaddr + 0x10000)

On-disc encoding (file bytes) vs visible pixels:
  visible = nibbleswap(byteswap16(file))      # render
  file    = byteswap16(nibbleswap(visible))   # write back   (both ops self-inverse)
Per-plate palette banks: CMDCOLR 0x410/0x420/0x440/0x460 (16-colour).

Repaint route: decompress SINIT2.SLD (analysis/zork_cgz.py) -> paint English 48x16 4bpp
labels at the 4 offsets -> re-encode -> recompress (LZSS) -> inject Track 01 + fix ECC.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import zork_cgz  # LZSS codec (decompress)

W, H, BPP = 48, 16, 4
PLATE_BYTES = W * H * BPP // 8          # 0x180
# Each tab has TWO plates interleaved at +0x180: the normal one and the
# selected/highlight one (different palette bank). Both must be painted.
TABS = {                                 # name -> decompressed SINIT2 offset
    "object":       0x6628, "object_hi":    0x67a8,   # オブジェクト  (+ highlight)
    "action":       0x6928, "action_hi":    0x6aa8,   # 動作
    "inventory":    0x6c28, "inventory_hi": 0x6da8,   # 持物
    "move":         0x6f28, "move_hi":      0x70a8,   # 移動
}
LABELS = {
    "object": "OBJECT", "object_hi": "OBJECT",
    "action": "ACTION", "action_hi": "ACTION",
    "inventory": "ITEMS", "inventory_hi": "ITEMS",
    "move": "MOVE", "move_hi": "MOVE",
}
SLOTS = 6   # every label is laid out in 6 letter-cells; shorter words are centred


def _nibswap(b):  return bytes(((x >> 4) | (x << 4)) & 0xff for x in b)


def decode_plate(file_bytes):
    """file 0x180 bytes -> list of W*H palette indices (0..15).
    file->VRAM transform is a nibble-swap ONLY (verified on-screen; the byteswap
    seen in savestate VRAM is a Mednafen storage artifact, not the game's path)."""
    vis = _nibswap(file_bytes)
    idx = []
    for byte in vis:
        idx.append(byte >> 4); idx.append(byte & 0xf)
    return idx[:W * H]


def encode_plate(indices):
    """W*H palette indices (0..15) -> file 0x180 bytes (nibble-swap only)."""
    assert len(indices) == W * H
    vis = bytearray()
    for i in range(0, W * H, 2):
        vis.append(((indices[i] & 0xf) << 4) | (indices[i + 1] & 0xf))
    return _nibswap(bytes(vis))


def load_sinit2(path="work/zork1/SINIT2.SLD"):
    return bytearray(zork_cgz.decompress(open(path, "rb").read()))


# --- English repaint (chosen labels: OBJECT/ACTION/ITEMS/MOVE) -----------------
# Two hand-drawn 5-wide fonts with 1px strokes, rendered at NATIVE size (no scaling -> no
# doubled rows/cols, no dropped stems). NORMAL = 5x7; SELECTED = 5x8 (1px taller = "larger").
_FW = 5
_FONT_N = {
 'A': ["01110","10001","10001","11111","10001","10001","10001"],
 'B': ["11110","10001","10001","11110","10001","10001","11110"],
 'C': ["01110","10001","10000","10000","10000","10001","01110"],
 'J': ["00111","00010","00010","00010","10010","10010","01100"],
 'E': ["11111","10000","10000","11110","10000","10000","11111"],
 'I': ["11111","00100","00100","00100","00100","00100","11111"],
 'M': ["10001","11011","10101","10001","10001","10001","10001"],
 'N': ["10001","11001","10101","10101","10011","10001","10001"],
 'O': ["01110","10001","10001","10001","10001","10001","01110"],
 'S': ["01110","10001","10000","01110","00001","10001","01110"],
 'T': ["11111","00100","00100","00100","00100","00100","00100"],
 'V': ["10001","10001","10001","10001","01010","01010","00100"],
 'Y': ["10001","10001","01010","00100","00100","00100","00100"],
 ' ': ["00000"] * 7,
}
_FONT_H = {
 'A': ["01110","10001","10001","10001","11111","10001","10001","10001"],
 'B': ["11110","10001","10001","11110","10001","10001","10001","11110"],
 'C': ["01110","10001","10000","10000","10000","10000","10001","01110"],
 'J': ["00111","00010","00010","00010","00010","10010","10010","01100"],
 'E': ["11111","10000","10000","11110","10000","10000","10000","11111"],
 'I': ["11111","00100","00100","00100","00100","00100","00100","11111"],
 'M': ["10001","11011","10101","10101","10001","10001","10001","10001"],
 'N': ["10001","11001","10101","10101","10101","10011","10001","10001"],
 'O': ["01110","10001","10001","10001","10001","10001","10001","01110"],
 'S': ["01110","10001","10000","01110","00001","00001","10001","01110"],
 'T': ["11111","00100","00100","00100","00100","00100","00100","00100"],
 'V': ["10001","10001","10001","10001","10001","01010","01010","00100"],
 'Y': ["10001","10001","01010","00100","00100","00100","00100","00100"],
 ' ': ["00000"] * 8,
}


ADV = 7   # px per character (8px glyphs overlap 1px) -> 6 chars = 42px, leaves side padding

# Per-plate bevel palette indices, chosen by nearest luminance to the colours the user
# read off the original (normal: light/top-left 144, main 96, dark/bottom-right 108;
# selected: light 232, main 152, dark 108). Indices differ per plate because each tab
# uses its own CRAM bank. Derived once from the 4 original-disc savestates' CRAM.
BEVEL = {
    "object":       dict(light=3, lgrad=5, main=7, dgrad=7, dark=6),
    "object_hi":    dict(light=1, lgrad=2, main=5, dgrad=4, dark=7),
    "action":       dict(light=1, lgrad=5, main=7, dgrad=7, dark=6),
    "action_hi":    dict(light=1, lgrad=3, main=3, dgrad=4, dark=6),
    "inventory":    dict(light=2, lgrad=4, main=6, dgrad=6, dark=5),
    "inventory_hi": dict(light=1, lgrad=2, main=2, dgrad=5, dark=6),
    "move":         dict(light=2, lgrad=5, main=7, dgrad=7, dark=6),
    "move_hi":      dict(light=1, lgrad=2, main=2, dgrad=5, dark=6),
}


def _plate_palette(orig):
    """Derive (border, ink, base_gray, tex_gray) from an original plate.
    border = most common index on the box's top border row;
    ink = 15 (the kanji stroke colour in every bank);
    base/tex = the two most common interior background grays (the marble dither)."""
    from collections import Counter
    border = Counter(orig[2 * W + 1:2 * W + 28]).most_common(1)[0][0]
    ink = 15
    grays = [v for v, _ in Counter(
        orig[y * W + x] for y in range(3, 13) for x in range(2, 28)
        if orig[y * W + x] not in (ink, border, 0)).most_common(2)]
    base = grays[0] if grays else 7
    tex = grays[1] if len(grays) > 1 else base
    return border, ink, base, tex


def _mode(vals):
    from collections import Counter
    c = Counter(v for v in vals if v != 0)
    return c.most_common(1)[0][0] if c else 0


def paint_marble(name, orig=None):
    """English label on a beveled gray box (palette-free; indices from BEVEL[name]).
    Bevel: light top+left edge, darker bottom+right edge, 1px gradient ring, mid interior
    (ramp idx15=darkest -> dark text). NORMAL tab = inset box (2px transparent margins);
    SELECTED tab (_hi) = fills the full 48x16 sprite so it overflows the inset neighbours."""
    bev = BEVEL[name]
    if name.endswith("_hi"):
        L, R, T, B = 0, W - 1, 2, H - 3              # full WIDTH (overflows sides) but same HEIGHT
    else:
        L, R, T, B = 2, W - 3, 2, H - 3              # inset 2px (transparent margin = fade)
    g = [0] * (W * H)
    for y in range(T, B + 1):
        for x in range(L, R + 1):
            dl = min(y - T, x - L)                    # distance from top/left (light)
            dd = min(B - y, R - x)                    # distance from bottom/right (dark)
            g[y * W + x] = (bev['light'] if dl == 0 else bev['dark'] if dd == 0
                            else bev['lgrad'] if dl == 1 else bev['dgrad'] if dd == 1
                            else bev['main'])
    # Native render of a 5px font (no scaling -> 1px strokes never doubled/dropped). SELECTED
    # uses the 1px-taller 5x8 font and 1px more advance. Each glyph is 5 wide and advance is
    # 6/7, so there's >=1px between letters; the SLOTS(6)-cell field is centred in the box with
    # margin (>=1px from edges). Shorter words centre (MOVE -> 1 blank cell/side, ITEMS half).
    hi = name.endswith("_hi")
    font = _FONT_H if hi else _FONT_N
    fh = 8 if hi else 7
    adv = 7 if hi else 6                               # 5px glyph + 2px/1px gap
    text = LABELS[name]
    n = len(text)
    field = SLOTS * adv
    fx = L + ((R - L + 1) - field) // 2               # centre the 6-cell field in the box
    wx = fx + (SLOTS - n) * adv // 2                   # centre the n letters in the field
    y0 = 4 + (8 - fh) // 2                             # centre vertically in the main band
    for ci, c in enumerate(text):
        rows = font.get(c, font[' '])
        for oy in range(fh):
            for ox in range(_FW):
                if rows[oy][ox] == '1':
                    x = wx + ci * adv + ox; y = y0 + oy
                    if 0 <= x < W and 0 <= y < H:
                        g[y * W + x] = 11 + oy * 5 // fh   # 11..15 top->bottom gradient
    return g


def painted_plate(name, dec=None):
    return paint_marble(name)


def build_patched_dec(dec):
    """Write the 8 English plates into a copy of decompressed SINIT2; return it."""
    out = bytearray(dec)
    for name, off in TABS.items():
        out[off:off + PLATE_BYTES] = encode_plate(painted_plate(name, dec))
    return out


def extract_png(dec, outdir="analysis/zork_vram"):
    from PIL import Image, ImageOps
    for name, off in TABS.items():
        idx = decode_plate(bytes(dec[off:off + PLATE_BYTES]))
        im = Image.new("L", (W, H))
        im.putdata([v * 16 for v in idx])
        ImageOps.autocontrast(im).resize((W * 8, H * 8), Image.NEAREST).save(
            f"{outdir}/srctab_{name}.png")
        print(f"  {name:10s} @ {off:#06x} -> {outdir}/srctab_{name}.png")


if __name__ == "__main__":
    dec = load_sinit2(sys.argv[1] if len(sys.argv) > 1 else "work/zork1/SINIT2.SLD")
    print(f"SINIT2 decompressed = {len(dec)} bytes; tab plates:")
    extract_png(dec)
