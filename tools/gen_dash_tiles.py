#!/usr/bin/env python3
"""Emit saturn/src/video/dash_tiles.c: the input dashboard's 69 8x8 4bpp tiles
and its 16-entry RGB555 palette. Deterministic -- the marble is seeded noise, so
re-running reproduces the same file byte for byte.

Usage: python3 tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
"""
import random

N = 69
SEED = 20260828

# Palette index by role. The blue channel runs two steps above red and green
# throughout, which is what makes the grey read as stone.
PALETTE = [
    None,          # 0  transparent
    (2, 2, 3),     # 1  drop shadow
    (4, 4, 6),     # 2  groove, deep
    (6, 6, 8),     # 3  groove
    (8, 8, 10),    # 4  shadow face
    (10, 10, 12),  # 5  stone body
    (11, 11, 13),  # 6
    (12, 12, 14),  # 7  frame rim
    (14, 14, 16),  # 8
    (15, 15, 17),  # 9
    (16, 16, 18),  # 10 veining
    (18, 18, 20),  # 11
    (20, 20, 22),  # 12
    (22, 22, 24),  # 13 bevel highlight
    (24, 24, 26),  # 14
    (27, 27, 29),  # 15 specular edge
]

# Each module is its own box, and a pixel's colour is its depth from the nearest
# edge of that box: a rim, two groove entries, then the highlight, with the
# marble field carrying on immediately behind it. Taking the minimum depth over
# every edge a pixel is near is what mitres the corners on the diagonal.
FRAME = [7, 3, 2, 13]

# The rule inside a module -- the one horizontal divider GAMEKB draws -- stays a
# plain centred groove, since it splits one box rather than ending it.
RULE = [3, 2, 13]
RULE_OFF = (8 - len(RULE) + 1) // 2

# Modules are separated by two pixels of nothing, so ten pixels sit between one
# module's stone and the next: two frames and the gap. That does not fit one
# cell, so the divider cell holds the left module's frame and the gap and the
# module after it opens with a frame of its own in the cell that follows.
DIV_M1_EDGE = 5
DIV_GAP = (6, 7)

# The panel is anchored with its top row at screen row 19 and is nine rows deep,
# so its edge rows are 19 and 27 and 19 & 3 == 27 & 3 == 3, and GAMEKB's rule row
# is 22, & 3 == 2. Column phases are fixed the same way: the panel's left frame
# column is x & 3 == 0, its right one (39 on PANEL and OVERLAY) is 3, the divider
# columns 14 and 30 are 2, and a module opening after a divider is 3. A moved
# panel needs these regenerated.
EDGE_RP = 3
DIV_CP = 2
MODLEFT_CP = 3
RULE_RP = 2

# The map marks sit on the ground tile -- the marble field at EDGE_RP/0, which
# carries entries 5 to 8 and nothing else. dash_view's half-tint compresses the
# ramp until neighbouring entries are about a thirty-first of a channel apart,
# so a mark painted at 4 or 9 differs from the ground byte for byte and is
# invisible on hardware; every entry below is at least four steps outside 5-8.
# The dark entry doubles as the link colour, so a trail reads as one groove
# running from room core to room core.
MARK_DARK = 1
MARK_RING = 13
MARK_HERE_RING = 15
MARK_HERE_CORE = 12


def rgb555(c):
    if c is None:
        return 0x0000
    r, g, b = c
    return 0x8000 | (b << 10) | (g << 5) | r


def marble(n=32):
    rnd = random.Random(SEED)
    f = [[rnd.random() for _ in range(n)] for _ in range(n)]
    for _ in range(2):
        f = [[sum(f[(y + dy) % n][(x + dx) % n]
                  for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9.0
              for x in range(n)] for y in range(n)]
    lo = min(min(r) for r in f)
    hi = max(max(r) for r in f)
    span = (hi - lo) or 1.0
    out = [[0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            v = (f[y][x] - lo) / span
            vein = (f[(y + x) % n][x] - lo) / span
            if vein > 0.72:
                p = 10 + int((vein - 0.72) * 10.0)
                out[y][x] = min(12, p)
            else:
                out[y][x] = 5 + min(4, int(v * 5.0))
    return out


def blank():
    return [[0] * 8 for _ in range(8)]


def solid(idx, x0, y0, x1, y1, base=None):
    """An 8x8 nibble grid with the inclusive box (x0,y0)-(x1,y1) set to idx."""
    t = base if base is not None else blank()
    return [[idx if (x0 <= x <= x1 and y0 <= y <= y1) else t[y][x]
             for x in range(8)] for y in range(8)]


def build():
    tiles = [blank()]

    patch = marble()

    def field(rp, cp):
        return [[patch[rp * 8 + y][cp * 8 + x] for x in range(8)]
                for y in range(8)]

    def framed(rp, cp, left=None, right=None, top=None, bottom=None,
               base=None):
        t = base if base is not None else field(rp, cp)
        for y in range(8):
            for x in range(8):
                d = []
                if left is not None:
                    d.append(x - left)
                if right is not None:
                    d.append(right - x)
                if top is not None:
                    d.append(y - top)
                if bottom is not None:
                    d.append(bottom - y)
                if d and min(d) < len(FRAME):
                    t[y][x] = FRAME[min(d)]
        return t

    def divider(rp, top=None, bottom=None):
        t = framed(rp, DIV_CP, right=DIV_M1_EDGE, top=top, bottom=bottom)
        for y in range(8):
            for x in DIV_GAP:
                t[y][x] = 0
        return t

    def ruled(cp, left=None, right=None):
        """The groove goes on first and the module's frame over it, so a frame
        pixel that happens to match the stone underneath is not mistaken for
        one the rule may overwrite."""
        t = field(RULE_RP, cp)
        for i, v in enumerate(RULE):
            for x in range(8):
                t[RULE_OFF + i][x] = v
        return framed(RULE_RP, cp, left=left, right=right, base=t)

    for rp in range(4):
        for cp in range(4):
            tiles.append(field(rp, cp))                         # 1-16

    for cp in range(4):
        tiles.append(framed(EDGE_RP, cp, top=0))                # 17-20 top
    for cp in range(4):
        tiles.append(framed(EDGE_RP, cp, bottom=7))             # 21-24 bottom
    for rp in range(4):
        tiles.append(framed(rp, 0, left=0))                     # 25-28 left
    for rp in range(4):
        tiles.append(framed(rp, 3, right=7))                    # 29-32 right
    for rp in range(4):
        tiles.append(framed(rp, MODLEFT_CP, left=0))            # 33-36 mod left
    for rp in range(4):
        tiles.append(divider(rp))                               # 37-40

    tiles.append(framed(EDGE_RP, 0, left=0, top=0))             # 41 corner TL
    tiles.append(framed(EDGE_RP, 3, right=7, top=0))            # 42 corner TR
    tiles.append(framed(EDGE_RP, 0, left=0, bottom=7))          # 43 corner BL
    tiles.append(framed(EDGE_RP, 3, right=7, bottom=7))         # 44 corner BR

    tiles.append(framed(EDGE_RP, MODLEFT_CP, left=0, top=0))    # 45
    tiles.append(framed(EDGE_RP, MODLEFT_CP, left=0, bottom=7)) # 46
    tiles.append(divider(EDGE_RP, top=0))                       # 47
    tiles.append(divider(EDGE_RP, bottom=7))                    # 48

    for cp in range(4):
        tiles.append(ruled(cp))                                 # 49-52
    tiles.append(ruled(MODLEFT_CP, left=0))                     # 53
    tiles.append(ruled(2, right=7))                             # 54

    # The menu-box frame: the same FRAME ramp laid over transparency instead of
    # over marble, so a box drawn with these is a four-pixel bevel and nothing
    # else, and whatever the menu already puts inside it shows through
    # unchanged. One tile per edge rather than four -- the phases exist only to
    # keep the field's 32-pixel repeat in register behind the bevel, and there
    # is no field behind these.
    for kw in ({'top': 0}, {'bottom': 7}, {'left': 0}, {'right': 7},
               {'left': 0, 'top': 0}, {'right': 7, 'top': 0},
               {'left': 0, 'bottom': 7}, {'right': 7, 'bottom': 7}):
        tiles.append(framed(0, 0, base=blank(), **kw))           # 55-62

    ground = field(EDGE_RP, 0)                                  # DT_GROUND
    tiles.append(ground)

    room = solid(MARK_RING, 1, 1, 6, 6, base=ground)
    room = solid(MARK_DARK, 2, 2, 5, 5, base=room)
    tiles.append(room)                                          # DT_ROOM

    here = solid(MARK_HERE_RING, 1, 1, 6, 6, base=ground)
    here = solid(MARK_HERE_CORE, 2, 2, 5, 5, base=here)
    tiles.append(here)                                          # DT_ROOM_HERE

    tiles.append(solid(MARK_DARK, 0, 3, 7, 4, base=ground))     # DT_LINK_H
    tiles.append(solid(MARK_DARK, 3, 0, 4, 7, base=ground))     # DT_LINK_V

    stair = solid(MARK_DARK, 3, 0, 4, 1, base=ground)
    stair = solid(MARK_DARK, 3, 3, 4, 4, base=stair)
    stair = solid(MARK_DARK, 3, 6, 4, 7, base=stair)
    tiles.append(stair)                                         # DT_LINK_STAIR

    assert len(tiles) == N, len(tiles)
    return tiles


def emit(tiles):
    print("/*----------------------")
    print(" | dash_tiles.c")
    print(" | Description: The input dashboard's 69 8x8 4bpp tiles and its")
    print(" |   16-entry RGB555 palette for CRAM entries 16..31. Generated by")
    print(" |   tools/gen_dash_tiles.py -- edit that, not this.")
    print(" | Author: suinevere")
    print(" | Dependencies: dash_tiles.h")
    print(" ----------------------*/")
    print('#include "dash_tiles.h"')
    print()
    print("const unsigned short dash_palette[16] = {")
    print("    " + ", ".join("0x%04X" % rgb555(c) for c in PALETTE))
    print("};")
    print()
    print("const unsigned char dash_tile_data[%d][32] = {" % N)
    for t in tiles:
        row = []
        for y in range(8):
            for x in range(0, 8, 2):
                row.append((t[y][x] << 4) | t[y][x + 1])
        print("    { " + ", ".join("0x%02X" % b for b in row) + " },")
    print("};")


if __name__ == "__main__":
    emit(build())
