#!/usr/bin/env python3
"""Emit saturn/src/video/dash_tiles.c: the input dashboard's 178 8x8 4bpp tiles
and its 16-entry RGB555 palette. Deterministic -- the marble is seeded noise and
the one imported bitmap is a file in the tree, so re-running reproduces the same
file byte for byte.

Needs Pillow, and only for the knight: KNIGHT_PNG is read rather than transcribed
into a literal here so the drawing and the tiles cannot drift apart.

Usage: python3 tools/gen_dash_tiles.py > saturn/src/video/dash_tiles.c
"""
import random

from PIL import Image

N = 201
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
    (28, 2, 4),    # 14 accent -- see PAL_ACCENT
    (27, 27, 29),  # 15 specular edge
]

# Each module is its own box, and a pixel's colour is its depth from the nearest
# edge of that box: a rim, two groove entries, then the highlight, with the
# marble field carrying on immediately behind it. Taking the minimum depth over
# every edge a pixel is near is what mitres the corners on the diagonal.
# Entry 14 is the accent slot rather than the next step of the ramp. Nothing on
# the marble can reach it -- marble() caps its veins at 12 and its body runs 5..9,
# and no frame, rule or mark names it -- so it is the one index that can carry a
# colour of its own without changing a pixel of the stone. dash_view's
# write_palette copies it straight to CRAM instead of bending it toward the
# background's hue and brightness the way it does every other entry; that is what
# keeps it red on a tan sheet, and it is why the value here is a colour rather
# than a point on the grey ramp. Kept in step with DASH_PAL_ACCENT in
# video/dash_tiles.h by tests/test_dash_accent.py.
PAL_ACCENT = 14

# The four seats' colours: the local player's first, then the other three. The
# accent is not among them -- it stays the crosshair's alone, always red, so the
# cursor is one thing on the screen whose colour never depends on the sheet or
# on who else is playing.
#
# All four are BORROWED rather than reserved. They stay ordinary points of the
# ramp in dash_palette and are only ever a colour for as long as the map screen
# is up: map_view calls dash_map_party after dash_tint, and the dash_tint on the
# way out puts the ramp back.
#
# The borrow is safe because the map paints no stone. Outside these four and
# PAL_FILL its own tiles reach only entries 0, 1, 2, 12, 13, 14 and 15 --
# dash_map_begin clears every other cell to DT_BLANK and the screen draws no box
# -- so 3..11 are unreachable for as long as it is drawn, and these are four of
# them. Entry 4 is reachable by nothing at all, anywhere; 3 is a groove and 5 and
# 6 are marble body, which is why the restore on the way out is part of the
# design and not tidiness.
PAL_PARTY = (3, 4, 5, 6)

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

# The inventory overlay's picture module carries a second frame inside the
# first, drawn hard against the 64x80 item picture so the picture sits in a
# bead of its own rather than floating on the module's field. Its bead faces
# inward -- the ramp is measured from the edge the picture is on -- and its
# ring is the module interior's outermost cells: rows 17 and 28, columns 29 and
# 38 (see CV_OVERLAY_PANE_X in video/panel_layout.h). Those four fix the
# phases the same way EDGE_RP and DIV_CP fix the outer frame's; a moved overlay
# needs them regenerated.
PIC_TOP_RP = 17 & 3
PIC_BOTTOM_RP = 28 & 3
PIC_LEFT_CP = 29 & 3
PIC_RIGHT_CP = 38 & 3

# The map's marks and grooves are drawn on transparency, not on the marble
# field, because the ground they sit on is not on this layer any more: the map
# screen puts a parchment picture on NBG0 and paints NBG2 with DT_BLANK, so a
# mark carrying its own opaque field would be an 8x8 patch of marble on a sheet
# of paper. Only the ink is opaque; everything around it is index 0, which VDP2
# reads as transparent and shows the parchment through.
#
# The values are unchanged from when they were drawn on marble. dash_view's
# half-tint compresses the ramp until neighbouring entries are about a
# thirty-first of a channel apart, so a mark painted at 4 or 9 would have been
# indistinguishable from the old ground; every entry here is at least four steps
# outside 5-8, and they still have to be, because DT_GROUND remains in the set
# for the fallback and nothing guarantees which one a given screen is over.
# The dark entry doubles as the link colour, so a trail reads as one groove
# running from room core to room core.
MARK_DARK = 1
MARK_RING = 13
MARK_HERE_RING = 15
MARK_HERE_CORE = 12

# The map names two of its own entries and sets them per sheet, the way it sets
# the party colours: the ink every line, arrow, glyph and stub is drawn in, and
# the fill in the middle of an ordinary location mark.
#
# They have to be two entries and not one. Infocom's sheets are paper of four
# different colours -- tan, cream, white, black -- and what reads as a drawing
# on one does not on the next, so the passages take dark brown on the two
# parchments, grey on the white sheet and the player's own font colour on the
# black one; the locations stay filled solid whatever the passages do. Sharing
# MARK_DARK, which is what DT_ROOM's core used to be, made the two the same
# colour by construction.
#
# PAL_LINE is a genuine map entry -- the grooves are drawn in it and always
# were -- and is merely written per sheet instead of taken off the tinted ramp.
# PAL_FILL is borrowed like the party slots: entry 7 is the marble's frame rim,
# which the map never paints, and dash_tint puts it back on the way out.
PAL_LINE = MARK_DARK
PAL_FILL = 7
MARK_FILL = PAL_FILL

# The map now has four kinds of mark to tell apart on one greyscale ramp bent to
# a single tan, which is one more than lightness alone carries, so the two new
# ones differ in shape as well as in value: the crosshair's pick inverts the
# ordinary mark -- dark ring, brightest core -- and another player's room keeps
# the here-mark's bright ring but takes a dark pupil out of its core. Both stay
# clear of entries 4 and 9 for the reason above.
MARK_SEL_RING = 2
MARK_SEL_CORE = 15
MARK_PEER_PUPIL = 1

# The crosshair is four corner brackets in the cells diagonally around the mark,
# so the picked room sits inside a reticle rather than wearing a colour a reader
# would have to have been told about. XHAIR_ARM is how many pixels each arm runs
# from the corner it turns at.
#
# Drawn in the accent rather than on the ramp. It was the ramp's brightest entry,
# which is a pale near-white, and on a sheet of tan paper carrying a drawing in
# dark ink that is the one value with nothing to read against -- the reticle was
# reported as hard to find on screen. Every other mark stays on the ramp; the
# cursor is the one thing that has to be found without being looked for.
MARK_XHAIR = PAL_ACCENT
XHAIR_ARM = 5

# The figure standing beside the player. 16x24 is exactly two tiles by three,
# which is why the drawing is that size: a sprite whose bounds did not divide
# would need a clipping rule of its own in a renderer that can only place cells.
KNIGHT_PNG = "tools/assets/png/KNIGHT.PNG"
KNIGHT_W = 2
KNIGHT_H = 3
# Drawn in the first party slot, which is the colour the map gives the local
# player: black on the two parchments, red on the dark sheet, and their own font
# colour on the fourth. It used to be the grooves' own ink, which said nothing
# about whose figure it was -- and now that the other seats stand on the map
# too, whose figure it is is the only thing the drawing has to say.
KNIGHT_INK = PAL_PARTY[0]

# One figure per seat, and the four quadrants of the shield the figure is
# already carrying in the same order: the local player, then the three others.
# The upper-left quadrant is always the player's own colour and the other three
# are fixed to a seat, so a room holding two people says which two rather than
# only that it holds more than one.
#
# The quadrants are the four blank 2x2 interiors of the grid drawn on the
# shield's face in KNIGHT.PNG, given in the 16x24 drawing's own coordinates. The
# grid is why the drawing has one: a quartered shield is where a party of four
# can be named without a second figure and without colouring the room mark,
# which has four other things to say already.
SHIELD_INK = PAL_PARTY
SHIELD_QUAD = ((1, 12), (4, 12), (1, 15), (4, 15))

# The two cells of the six the shield falls across -- the lower-left pair, since
# the shield is on the figure's left arm and spans the bar between them. Only
# these two get a copy per mask; the other four are the same however many people
# are standing in the room.
SHIELD_CELLS = (2, 4)

# The ink a shared room's figure is drawn in. Not a seat colour: with two or
# more people on one cell there is one figure between them and its shield is
# what says whose, so the body takes the map's own passage ink, which is chosen
# per sheet to be readable on the paper and belongs to nobody.
KNIGHT_PARTY_INK = PAL_LINE


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


def dash_ok(v):
    """Whether a run's pixel at coordinate v is lit. Two on, two off, on a
    period of four -- which divides the 8-pixel tile, so a long run stays in
    phase across tile boundaries -- and phased so that both centre pixels, 3
    and 4, are lit and a dashed elbow joins at the middle of the cell."""
    return ((v + 1) & 3) < 2


def dashed(idx, x0, y0, x1, y1, axis, base=None):
    """solid(), stippled along 'v' for a vertical arm or 'h' for a horizontal."""
    t = base if base is not None else blank()
    return [[idx if (x0 <= x <= x1 and y0 <= y <= y1
                     and dash_ok(y if axis == "v" else x))
             else t[y][x] for x in range(8)] for y in range(8)]


def rot_cw(t):
    """An 8x8 grid turned a quarter turn clockwise, so one drawn arrowhead
    yields all four without four hand-placed copies to drift apart."""
    return [[t[7 - x][y] for x in range(8)] for y in range(8)]


def bitmap(rows, idx):
    """An 8x8 grid from eight strings of eight characters, '#' for ink."""
    return [[idx if rows[y][x] == "#" else 0 for x in range(8)] for y in range(8)]


GLYPH_U = ["........",
           ".#....#.",
           ".#....#.",
           ".#....#.",
           ".#....#.",
           ".#....#.",
           "..####..",
           "........"]

GLYPH_D = ["........",
           ".####...",
           ".#...#..",
           ".#....#.",
           ".#....#.",
           ".#...#..",
           ".####...",
           "........"]

LOOP = ["..####..",
        ".#....#.",
        "#......#",
        "#......#",
        "#......#",
        ".#....#.",
        "..####..",
        "...##..."]

# Infocom's narrow-passageway (baggage limit) mark: three short bars struck
# through the passage line. The shaft is the same two-pixel groove every link
# tile draws on rows 3 and 4, kept solid underneath the bars so a baggage
# exit never shows a gap where the mark sits; the bars are three one-pixel
# ticks, two rows tall above and below the shaft, so the mark reads as
# crossing the line rather than replacing it.
BAGGAGE_H = ["........",
             "..#.#.#.",
             "..#.#.#.",
             "########",
             "########",
             "..#.#.#.",
             "..#.#.#.",
             "........"]


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

    # DT_GROUND is the opaque tan field the map used to be paved with. Nothing
    # paints it now -- the parchment behind NBG2 is the ground, and where there
    # is no parchment the VDP2 back colour is -- but it stays in the set for the
    # same reason the DT_BOX_* tiles did: every index after it is a literal in
    # dash_tiles.c and removing one would renumber the rest for 32 bytes.
    ground = field(EDGE_RP, 0)                                  # DT_GROUND
    tiles.append(ground)

    room = solid(MARK_RING, 1, 1, 6, 6)
    room = solid(MARK_FILL, 2, 2, 5, 5, base=room)
    tiles.append(room)                                          # DT_ROOM

    here = solid(MARK_HERE_RING, 1, 1, 6, 6)
    here = solid(MARK_HERE_CORE, 2, 2, 5, 5, base=here)
    tiles.append(here)                                          # DT_ROOM_HERE

    # The sixteen link tiles, indexed by which of the four sides the groove
    # leaves through: N=1, E=2, S=4, W=8. Each side present draws an arm from
    # the tile's centre to that edge, so a mask of two opposite sides is a
    # straight run, two adjacent sides an elbow, three a T and all four a
    # crossing -- one rule, and the renderer never has to decide which shape a
    # cell wants. Mask 0 is blank ground and is never painted; it exists so the
    # mask can index the set directly.
    for mask in range(16):
        t = blank()
        if mask & 1: t = solid(MARK_DARK, 3, 0, 4, 4, base=t)   # north arm
        if mask & 2: t = solid(MARK_DARK, 3, 3, 7, 4, base=t)   # east arm
        if mask & 4: t = solid(MARK_DARK, 3, 3, 4, 7, base=t)   # south arm
        if mask & 8: t = solid(MARK_DARK, 0, 3, 4, 4, base=t)   # west arm
        tiles.append(t)                                         # DT_LINK0+mask

    stair = solid(MARK_DARK, 3, 0, 4, 1)
    stair = solid(MARK_DARK, 3, 3, 4, 4, base=stair)
    stair = solid(MARK_DARK, 3, 6, 4, 7, base=stair)
    tiles.append(stair)                                         # DT_LINK_STAIR

    # The picture frame. Same bead over the same marble as every other frame
    # here, but facing the other way: the ramp is measured from the edge the
    # picture is on, so the rim lands against the picture and the highlight
    # sits outside it. Top and bottom runs are indexed by x & 3 and the sides
    # by y & 3, as the outer frame's are; the fixed phase of the other axis is
    # the ring's own row or column.
    for cp in range(4):
        tiles.append(framed(PIC_TOP_RP, cp, bottom=7))          # DT_PIC_TOP0..3
    for cp in range(4):
        tiles.append(framed(PIC_BOTTOM_RP, cp, top=0))          # DT_PIC_BOTTOM0..3
    for rp in range(4):
        tiles.append(framed(rp, PIC_LEFT_CP, right=7))          # DT_PIC_LEFT0..3
    for rp in range(4):
        tiles.append(framed(rp, PIC_RIGHT_CP, left=0))          # DT_PIC_RIGHT0..3

    tiles.append(framed(PIC_TOP_RP, PIC_LEFT_CP, right=7, bottom=7))     # TL
    tiles.append(framed(PIC_TOP_RP, PIC_RIGHT_CP, left=0, bottom=7))     # TR
    tiles.append(framed(PIC_BOTTOM_RP, PIC_LEFT_CP, right=7, top=0))     # BL
    tiles.append(framed(PIC_BOTTOM_RP, PIC_RIGHT_CP, left=0, top=0))     # BR

    sel = solid(MARK_SEL_RING, 1, 1, 6, 6)
    sel = solid(MARK_SEL_CORE, 2, 2, 5, 5, base=sel)
    tiles.append(sel)                                           # DT_ROOM_SEL

    tiles.append(solid(MARK_PEER_PUPIL, 3, 3, 4, 4, base=here)) # DT_ROOM_PEER

    # The four reticle corners, each an L turning at the corner of its own cell
    # nearest the mark, so the four together read as one box drawn around it.
    # The cells they land in are diagonal from the mark and can hold a link; the
    # bracket overwrites it, because a cursor showing the map through itself is
    # harder to find than the room it is pointing at.
    for hx0, hy, vx, vy0 in ((1, 1, 1, 1),
                             (7 - XHAIR_ARM, 1, 6, 1),
                             (1, 6, 1, 7 - XHAIR_ARM),
                             (7 - XHAIR_ARM, 6, 6, 7 - XHAIR_ARM)):
        t = solid(MARK_XHAIR, hx0, hy, hx0 + XHAIR_ARM - 1, hy)
        t = solid(MARK_XHAIR, vx, vy0, vx, vy0 + XHAIR_ARM - 1, base=t)
        tiles.append(t)                  # DT_XHAIR_TL, TR, BL, BR

    # The knight, cut into cells row-major. Transparent where the drawing is
    # not: the hole it leaves is a hole through to the parchment, which is the
    # ground, so the figure stands on the paper rather than in a box cut out of
    # it.
    knight = Image.open(KNIGHT_PNG).convert("RGBA")
    assert knight.size == (KNIGHT_W * 8, KNIGHT_H * 8), knight.size

    def knight_cell(ink, cell, mask=0):
        """One cell of the figure, drawn in one palette entry, with the shield
        quadrants `mask` names filled in their own seats' colours. Cells are
        row-major, so cell = ty * KNIGHT_W + tx."""
        ty, tx = cell // KNIGHT_W, cell % KNIGHT_W
        t = blank()
        for y in range(8):
            for x in range(8):
                if knight.getpixel((tx * 8 + x, ty * 8 + y))[3] > 128:
                    t[y][x] = ink
        for bit in range(4):
            if not mask & (1 << bit):
                continue
            qx, qy = SHIELD_QUAD[bit]
            for y in range(qy, qy + 2):
                for x in range(qx, qx + 2):
                    # A quadrant straddles the cell boundary between the two
                    # shield cells, so each cell takes only the half of it that
                    # falls inside its own eight pixels.
                    if y // 8 == ty and x // 8 == tx:
                        t[y - ty * 8][x - tx * 8] = SHIELD_INK[bit]
        return t

    def knight_set(ink, mask=0):
        """The figure cut into cells row-major, drawn in one palette entry."""
        return [knight_cell(ink, cell, mask)
                for cell in range(KNIGHT_W * KNIGHT_H)]

    tiles.extend(knight_set(KNIGHT_INK))                        # DT_KNIGHT0+i

    # The dashed set, index for index with DT_LINK0. Same arms, stippled along
    # each arm's own axis, so a dashed run and a solid one meet cleanly where
    # one crosses the other.
    for mask in range(16):
        t = blank()
        if mask & 1: t = dashed(MARK_DARK, 3, 0, 4, 4, "v", base=t)
        if mask & 2: t = dashed(MARK_DARK, 3, 3, 7, 4, "h", base=t)
        if mask & 4: t = dashed(MARK_DARK, 3, 3, 4, 7, "v", base=t)
        if mask & 8: t = dashed(MARK_DARK, 0, 3, 4, 4, "h", base=t)
        tiles.append(t)                                         # DT_DASH0+mask

    # One arrowhead is drawn pointing east and turned for the other three. The
    # head stays solid in the dashed variant: a conditional passage still has
    # to say which way it runs.
    def arrowhead(base):
        t = solid(MARK_DARK, 5, 1, 5, 6, base=base)
        t = solid(MARK_DARK, 6, 2, 6, 5, base=t)
        return solid(MARK_DARK, 7, 3, 7, 4, base=t)

    for shaft in (solid(MARK_DARK, 0, 3, 4, 4),
                  dashed(MARK_DARK, 0, 3, 4, 4, "h")):
        east  = arrowhead(shaft)
        south = rot_cw(east)
        west  = rot_cw(south)
        north = rot_cw(west)
        for t in (north, east, south, west):        # DT_ARROW_N, E, S, W
            tiles.append(t)

    tiles.append(bitmap(GLYPH_U, MARK_DARK))                    # DT_GLYPH_U
    tiles.append(bitmap(GLYPH_D, MARK_DARK))                    # DT_GLYPH_D
    tiles.append(bitmap(LOOP, MARK_DARK))                       # DT_LOOP

    # The vertical baggage tile is taken as the horizontal one's quarter
    # turn, not hand-drawn, so the pair cannot drift apart -- the same
    # relationship rot_cw already gives the arrowheads.
    baggage_h = bitmap(BAGGAGE_H, MARK_DARK)
    tiles.append(baggage_h)                                     # DT_BAGGAGE_H
    tiles.append(rot_cw(baggage_h))                             # DT_BAGGAGE_V

    # The same figure again in each other seat's colour. Three copies of one
    # drawing rather than one copy recoloured, because two people can be on the
    # map at once and a tile carries its palette entry in its pixels.
    for ink in PAL_PARTY[1:]:
        tiles.extend(knight_set(ink))                           # DT_KNIGHT_PEER0

    # The here-mark with its two entries exchanged -- dark ring, bright core.
    # The local player's mark pulses between the two rather than between itself
    # and an empty room, so what says "you are here" is a mark turning inside
    # out on one cell instead of a mark that spends half its time gone, and it
    # stays legible with a figure beside it and the reticle around it.
    inv = solid(MARK_HERE_CORE, 1, 1, 6, 6)
    inv = solid(MARK_HERE_RING, 2, 2, 5, 5, base=inv)
    tiles.append(inv)                                           # DT_ROOM_HERE_INV

    # The figure a shared room gets: one drawing in the neutral ink, and then a
    # copy of each of the two cells its shield crosses per occupancy mask -- bit
    # 0 the local player, bits 1..3 the other three in seat order. Two cells per
    # mask rather than six, because the other four are the same whoever is
    # standing there.
    #
    # Mask 0 and the four single-bit masks are never painted -- one occupant
    # gets their own coloured figure and a blank shield -- but they exist so the
    # mask indexes the set directly, the same bargain DT_LINK0's mask 0 makes.
    tiles.extend(knight_set(KNIGHT_PARTY_INK))                  # DT_KNIGHT_PARTY0
    for cell in SHIELD_CELLS:
        for mask in range(16):
            tiles.append(knight_cell(KNIGHT_PARTY_INK, cell, mask))
                                                    # DT_SHIELD_HI0 / DT_SHIELD_LO0

    assert len(tiles) == N, len(tiles)
    return tiles


def emit(tiles):
    print("/*----------------------")
    print(" | dash_tiles.c")
    print(" | Description: The input dashboard's %d 8x8 4bpp tiles and its" % N)
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
