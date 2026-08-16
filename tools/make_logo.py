#!/usr/bin/env python3
"""Draw the Z-ATURN masonry title logo and emit it as VDP2 text-layer tiles.

The logo restates the ZORK cover lettering in tools/assets/logo/zork-logo.png.
Six things make that lettering what it is, and all six are built here:

  * Solid black letters with the mortar drawn THROUGH them as white lines. The
    brickwork is line art over black, not a rendering of stone. Two colours.
  * A thick white outline round the word and a thin black keyline outside that.
  * Very fat strokes and small counters. The reference's letters are nearly as
    thick as they are open.
  * Stones of markedly unequal size -- one slab worth four of its neighbours --
    with the joints staggered course to course and hardly one of them plumb.
  * The courses of a diagonal running WITH the diagonal rather than across it.
  * A silhouette that steps. The cap line is not a straight edge: the courses
    wander, blocks stand above and below them, the outer letters throw spurs
    past the word's corners, and no two letters sit at quite the same height.

The letters are set nearly touching, at a track narrow enough that the white
outlines of neighbours merge and fill it. So the word carries ONE keyline the
whole way round, exactly as the reference does, and the boundary between two
letters reads as a joint in the wall rather than as a gap -- which is what the
reference gets by interlocking its letterforms, and seven letters across 304
pixels cannot.

It does NOT go on the disc as a picture. The title screen already spends NBG0
on a room photograph, so the logo rides the text layer (NBG3) instead: it is
cut into 8x8 4bpp cells, written into the free character block below the SGL
font, and printed as pattern names. Transparent outside the keyline, so the
photograph shows through around the word, and it fades with the rest of the
title because NBG3 is what the screen-wide fade already drives.

The two colours go in CRAM entries 3 and 4. NBG3 is COL_TYPE_16 on palette 0,
where entry 1 is the font colour the Options page recolours, 2 is the
reverse-video punch-out, 15 is the block cursor and 0 is transparent -- so
3 upward is what the art may use, and this design wants almost none of it.

Usage:
    python tools/make_logo.py            # write the .inc and the preview PNG
    python tools/make_logo.py --preview  # preview PNG only, nothing generated

Outputs:
    saturn/src/video/title_logo.inc      tiles, cell map and palette
    tools/assets/logo/zaturn_logo.png    a 4x preview to look at
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

REPO = Path(__file__).resolve().parent.parent
INC_PATH = REPO / "saturn" / "src" / "video" / "title_logo.inc"
PREVIEW_PATH = REPO / "tools" / "assets" / "logo" / "zaturn_logo.png"

CELL = 8                       # VDP2 cell size, both axes
CELLS_W, CELLS_H = 38, 10      # the logo's footprint on the 40x28 text grid
WIDTH, HEIGHT = CELLS_W * CELL, CELLS_H * CELL

# The line weights, in the proportions the reference draws them at: mortar and
# outline about the same, the keyline half that.
MORTAR_W = 2                   # white joint between two blocks
OUTLINE_W = 2                  # white ring round the whole word
KEYLINE_W = 1                  # black ring outside that

# Both rings grow inwards as well as out, so a counter loses
# 2*(OUTLINE_W + KEYLINE_W) of its width before any of it shows. Every counter
# below is drawn wide enough to survive that; anything narrower would close.
RINGS = OUTLINE_W + KEYLINE_W

CAP_TOP = RINGS + 8            # room above the cap line for the spurs and the
                               # ride that step out of it, plus the rings
CAP_H = 62                     # cap height in pixels

# How far each letter sits off the common cap line. The reference's do not share
# one: its O hangs below the Z and its K rides above the R, and that is most of
# what keeps four letters of near-identical weight from running together into a
# band. Seven need it more, not less -- with every cap on one line the bars of
# T, U, R and N read as a single lintel with legs under it.
#
# Kept inside +1 and -3, which is what the canvas has spare once the rings and
# the spurs above the cap line have taken theirs.
RIDE = {"Z": 0, "-": 0, "A": 1, "T": -3, "U": 1, "R": -1, "N": -3}

# Letters are set this far apart. The gap is narrower than two outlines, so the
# outlines meet inside it and fill it solid white: the word ends up under one
# keyline, and a letter boundary reads as a fat joint. Widen this past
# 2*OUTLINE_W and the keyline creeps in between the letters and breaks the word
# into seven separately-ringed pieces.
TRACK = 4

# The five courses, as the inclusive row range a block on that course occupies.
# Every glyph is built on these, so the courses run level right across the word
# the way they would in a wall -- letters whose bars sat between courses would
# read as separate pieces of masonry standing next to each other.
#
# Five and not six, and unequal: shallow at the cap and the baseline, deep
# through the middle, which is the reference's own pattern. Six courses of
# twelve-pixel stones with a two-pixel joint between every pair of them is
# more line than stone at the size this is actually seen -- 304 pixels across
# on a television -- and the word stopped reading before the wall did.
CY = [(0, 9), (10, 23), (24, 37), (38, 49), (50, 61)]

# Palette indices. 0 is VDP2 transparency and 1, 2 and 15 belong to the console
# font (see the module docstring), so the art starts at 3.
TRANS = 0
BLACK, WHITE = 3, 4

PALETTE = {
    BLACK: (0x00, 0x00, 0x00),
    WHITE: (0xff, 0xff, 0xff),
}


LEVEL = (0, 0, 0, 0)


def block(c, x0, x1, d=LEVEL):
    """
    ----------------------
    | block
    | Description: One upright block: course `c` between two inclusive
    |   glyph-local columns, with each of its four corners free to sit off the
    |   course line.
    |
    |   The four offsets are the difference between masonry and graph paper.
    |   Level top and bottom on every stone rules the wall into a grid however
    |   the vertical joints are staggered; the reference has no such line in it,
    |   its courses wander a couple of pixels a stone. The same offsets, used
    |   larger, are what steps the cap line and the baseline in and out.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: CY
    | Params: c -- course index; x0, x1 -- inclusive columns; d -- (top-left,
    |   top-right, bottom-right, bottom-left) offsets, negative being upward
    | Returns: the block as a four-point polygon
    ----------------------
    """
    y0, y1 = CY[c]
    return [(x0, y0 + d[0]), (x1, y0 + d[1]),
            (x1, y1 + d[2]), (x0, y1 + d[3])]


def span(c0, c1, x0, x1, d=LEVEL):
    """
    ----------------------
    | span
    | Description: One block running from course c0 to course c1, for the stones
    |   that are two courses deep.
    |
    |   A wall gets its texture from the stones being different sizes, and the
    |   reference's are markedly so -- one stone of the O's stem is worth two of
    |   its neighbours. A few of these among the single-course blocks is what
    |   stops the courses reading as ruled lines.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: CY
    | Params: c0, c1 -- first and last course; x0, x1 -- inclusive columns; d --
    |   corner offsets, as for block
    | Returns: the block as a four-point polygon
    ----------------------
    """
    y0 = CY[c0][0]
    y1 = CY[c1][1]
    return [(x0, y0 + d[0]), (x1, y0 + d[1]),
            (x1, y1 + d[2]), (x0, y1 + d[3])]


def slope(c, tx0, tx1, bx0, bx1):
    """
    ----------------------
    | slope
    | Description: One block cut to fit a diagonal: course `c` again, but with
    |   its top and bottom edges at different columns, so a diagonal stroke is a
    |   stack of stones leaning with it rather than a staircase of upright ones.
    |   This is the single biggest thing the reference does that a grid of
    |   rectangles cannot: its Z is bricked ALONG the bar, not across it.
    |
    |   Two of these side by side must OVERLAP by a column, not abut. A sloped
    |   edge lands between pixels, and the rasteriser is free to round the left
    |   block's edge down while rounding the right one's up -- which opens a
    |   one-pixel hole straight through the letter, a third of the way down,
    |   where nothing about the numbers suggests one. The later block wins the
    |   overlap, so the joint still ends up a pixel wide.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: CY
    | Params: c -- course index; tx0, tx1 -- inclusive columns at the top; bx0,
    |   bx1 -- inclusive columns at the bottom
    | Returns: the block as a four-point polygon
    ----------------------
    """
    y0, y1 = CY[c]
    return [(tx0, y0), (tx1, y0), (bx1, y1), (bx0, y1)]


def wedge(c, tx0, tx1, bx):
    """
    ----------------------
    | wedge
    | Description: The triangular offcut left where a diagonal runs out into a
    |   stem: full width at the top of course `c`, closing to a point at the
    |   bottom. Without it the letter keeps a notch out of its outer edge that
    |   the outline then traces, and the notch reads as damage rather than as
    |   the corner it is.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: CY
    | Params: c -- course index; tx0, tx1 -- inclusive columns at the top; bx --
    |   the column the wedge closes to
    | Returns: the block as a three-point polygon
    ----------------------
    """
    y0, y1 = CY[c]
    return [(tx0, y0), (tx1, y0), (bx, y1)]


def free(x0, y0, x1, y1):
    """
    ----------------------
    | free
    | Description: One block placed off the course ladder entirely, for the
    |   hyphen -- which sits between two courses because that is where the
    |   middle of the cap height falls.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: N/A
    | Params: x0, y0, x1, y1 -- inclusive glyph-local bounds
    | Returns: the block as a four-point polygon
    ----------------------
    """
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def bar(c, cuts, *corners, **kw):
    """
    ----------------------
    | bar
    | Description: A whole course of blocks across one horizontal stroke, given
    |   the columns the joints fall on. The last value is exclusive, so the
    |   blocks run edge to edge with nothing left over.
    |
    |   `lean` tilts the joints: one value per cut, the number of columns that
    |   joint moves by between the top of the course and the bottom. Barely a
    |   joint in the reference is plumb, and a bar of upright blocks is the one
    |   place the eye is quickest to notice it -- the outer two values stay zero
    |   so the letter's own edges do not move.
    |
    |   Two neighbours must be given the SAME corner offset where they meet, or
    |   the wandering course opens a notch between them that the outline pass
    |   then paints white, straight across the stroke.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: CY, LEVEL
    | Params: c -- course index; cuts -- joint columns, left to right; corners --
    |   one corner-offset tuple per block, in order; kw -- lean, one column
    |   offset per cut
    | Returns: a list of polygons
    ----------------------
    """
    lean = kw.get("lean", (0,) * len(cuts))
    last = len(cuts) - 2
    y0, y1 = CY[c]
    out = []
    for i in range(len(cuts) - 1):
        d = corners[i] if i < len(corners) else LEVEL
        # Every stone but the last runs a column INTO its right-hand neighbour.
        # A leaning joint lands between pixels, and the rasteriser is free to
        # round the left stone's edge one way and the right stone's the other,
        # which strands a pixel of background inside the wall. The neighbour is
        # drawn afterwards and wins the overlap, so the joint does not move.
        tx0, tx1 = cuts[i], cuts[i + 1] - (1 if i == last else 0)
        out.append([(tx0, y0 + d[0]), (tx1, y0 + d[1]),
                    (tx1 + lean[i + 1], y1 + d[2]), (tx0 + lean[i], y1 + d[3])])
    return out


# Every glyph is (width, blocks). The blocks ARE the letter -- there is no
# separate outline they are fitted into. Joints are not drawn here either: the
# blocks are laid touching, and draw_bricks cuts the white line back through the
# wall wherever two of them meet, which is what leaves a block flush with the
# letter's edge where it meets nothing.
#
# The joint columns are staggered course to course on purpose. Blocks stacked
# with their joints in line are a stack, not a bond, and the eye reads the seam
# before it reads the letter.
#
# The corner offsets follow one rule, and holes appear the moment it is broken:
# where a stroke carries on past a block, that block's edge is pushed INTO the
# neighbour, never away from it. A course that wanders upward at its underside
# while the stem beneath it starts on the course line leaves a row of stray
# pixels that the outline then paints white, straight across the letter.
BRICKS = {
    # A chevron, and the widest letter's worth of jaggedness: the top-left
    # corner rises well out of the cap line and the bottom-right drops back
    # inside the baseline, which is what stops the Z reading as a rectangle with
    # a stripe through it. The diagonal is bricked ALONG its own slope -- three
    # slabs leaning with it, not a staircase of upright stones.
    "Z": (42,
          bar(0, (0, 14, 28, 42),
              (-6, -5, -1, 1), (-5, -3, 1, -1), (-3, -2, 2, 1),
              lean=(0, 3, -2, 0))
          + [slope(1, 26, 41, 18, 33),
             slope(2, 18, 26, 10, 18), slope(2, 25, 33, 17, 25),
             slope(3, 10, 25, 4, 19)]
          + bar(4, (0, 14, 28, 42),
                (0, -1, 1, 3), (-1, 1, 2, 1), (1, 0, -5, 2),
                lean=(0, -3, 2, 0))),

    # Off the course ladder: the middle of the cap height falls between courses
    # 1 and 2, and a hyphen sitting on either would read as a dropped brick.
    "-": (16, [free(0, 24, 8, 38), free(9, 25, 15, 39)]),

    # Solid from the apex down through the crossbar. A counter up there would be
    # six pixels across before the rings ate their six, and the reference's own
    # letters keep their counters small and low for the same reason -- what says
    # "A" here is the splay of the legs and the white joint running up under the
    # apex, not a hole. Every stone is cut to the splay, so nothing in the letter
    # is square.
    "A": (50,
          [slope(0, 18, 31, 15, 34),
           slope(1, 15, 34, 11, 38),
           slope(2, 11, 24, 7, 23), slope(2, 23, 38, 22, 42),
           slope(3, 7, 18, 3, 14), slope(3, 34, 42, 37, 45),
           slope(4, 3, 14, 0, 11), slope(4, 37, 45, 41, 49)]),

    # The bar is pulled inside the letter box at both ends, so the cap line
    # breaks either side of it instead of running on into its neighbours, and it
    # steps down to the right. Under it the stem is one slab twenty-eight pixels
    # deep -- the biggest stone in the word, against the smallest in the foot.
    "T": (36,
          bar(0, (1, 12, 24, 35),
              (-4, -3, -1, 1), (-3, -1, 2, -1), (-1, 3, -1, 2),
              lean=(0, 2, -3, 0))
          + [span(1, 2, 11, 24, (-2, -1, 2, 3)),
             block(3, 11, 24, (-1, -2, 2, 1))]
          + bar(4, (9, 18, 27), (0, -1, 1, 2), (-1, 0, 2, 1),
                lean=(0, 3, 0))),

    # Piers of unequal bricking -- the left runs small, slab, single and the
    # right single, single, slab -- so the two sides of the counter are not each
    # other's reflection.
    "U": (40,
          [block(0, 0, 11, (0, -2, 2, 3)), span(1, 2, 0, 11, (-2, -1, 3, 2)),
           block(3, 0, 11, (-1, -2, 2, 3)),
           block(0, 28, 39, (-4, -3, 2, 3)), block(1, 28, 39, (-2, -1, 3, 2)),
           span(2, 3, 28, 39, (-2, -2, 2, 1))]
          + bar(4, (0, 14, 27, 40),
                (-1, 1, 2, 3), (1, -2, 1, 2), (-2, -1, -1, 1),
                lean=(0, -2, 3, 0))),

    # The bowl takes two of the five courses. One was enough to draw an R and
    # not enough to read as one: shut to eleven pixels by the rings, its counter
    # became a nick in a post, and with U, R and N standing six near-identical
    # piers in a row the word turned into URURN. The bowl is what tells them
    # apart, so it gets the height. The leg is one stone hung off the bar that
    # closes it and cut to its own slope, the way the reference hangs the R's.
    "R": (44,
          bar(0, (0, 15, 30, 44),
              (0, -1, 2, 1), (-1, -2, 1, 2), (-3, -3, 2, 1),
              lean=(0, 3, -2, 0))
          + [block(1, 0, 12, (-2, -1, 3, 2)), block(1, 31, 43, (-1, -2, 3, 2)),
             block(2, 0, 12, (-2, -1, 3, 2)), block(2, 31, 43, (-2, -2, 2, 2))]
          + bar(3, (0, 16, 31, 44),
                (-2, -1, 2, 3), (-1, 0, 2, 2), (0, -2, 2, 2),
                lean=(0, -3, 2, 0))
          + [block(4, 0, 12, (-1, -2, 0, 2)), slope(4, 24, 36, 30, 42)]),

    # The diagonal runs out into the right-hand stem from the third course down,
    # and below that the two are one piece of wall -- so they are laid as one,
    # each course a stone cut to the slope plus the offcut beside it. Laying a
    # stem block behind the diagonal instead leaves the stem as a wedge that
    # thins to nothing by the baseline. The top-right corner carries the spur
    # that answers the Z's.
    "N": (44,
          [block(0, 0, 10, (-1, -2, 2, 1)), span(1, 2, 0, 10, (-2, -1, 3, 2)),
           block(3, 0, 10, (-1, -2, 2, 1)), block(4, 0, 10, (-2, -2, -3, 1)),
           block(0, 33, 43, (-5, -4, 3, 2)), block(1, 33, 43, (-2, -1, 3, 2))]
          + [slope(0, 11, 21, 13, 24),
             slope(1, 13, 24, 18, 30),
             slope(2, 18, 30, 24, 36), slope(2, 30, 43, 36, 43),
             slope(3, 24, 36, 29, 41), slope(3, 36, 43, 41, 43),
             slope(4, 29, 41, 33, 43), wedge(4, 41, 43, 43)]),
}

WORD = "Z-ATURN"


def place_bricks():
    """
    ----------------------
    | place_bricks
    | Description: Lays the word out centred on the canvas and returns every
    |   block in canvas coordinates.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: BRICKS, WORD, RIDE, TRACK, CAP_TOP, WIDTH
    | Params: N/A
    | Returns: a list of polygons
    ----------------------
    """
    widths = [BRICKS[c][0] for c in WORD]
    pen = (WIDTH - sum(widths) - TRACK * (len(WORD) - 1)) // 2

    placed = []
    for ch, w in zip(WORD, widths):
        top = CAP_TOP + RIDE[ch]
        for poly in BRICKS[ch][1]:
            placed.append([(pen + x, y + top) for x, y in poly])
        pen += w + TRACK

    return placed


def rasterise(blocks):
    """
    ----------------------
    | rasterise
    | Description: Paints every block into one 'L' image holding its index plus
    |   one, so a pixel says which block owns it and zero means no stone. One
    |   image rather than a mask each, because the joints are cut by comparing a
    |   pixel's owner against its neighbour's.
    | Author: suinevere
    | Dependencies: PIL
    | Globals: WIDTH, HEIGHT
    | Params: blocks -- the placed polygons; at most 255 of them
    | Returns: the owner image
    ----------------------
    """
    if len(blocks) > 255:
        raise ValueError("more blocks than an 8-bit owner map can hold")
    owner = Image.new("L", (WIDTH, HEIGHT), 0)
    draw = ImageDraw.Draw(owner)
    for i, poly in enumerate(blocks):
        draw.polygon(poly, fill=i + 1)
    return owner


def grow(img, n):
    """
    ----------------------
    | grow
    | Description: Dilates a black-and-white mask by n pixels, square-cornered.
    |   Square rather than round because the reference's outline turns its
    |   corners square, and at this size a rounded one just looks eroded.
    | Author: suinevere
    | Dependencies: PIL
    | Globals: N/A
    | Params: img -- an 'L' mask, 255 where set; n -- pixels to grow by
    | Returns: the grown mask
    ----------------------
    """
    for _ in range(n):
        img = img.filter(ImageFilter.MaxFilter(3))
    return img


def draw_rings(px, solid):
    """
    ----------------------
    | draw_rings
    | Description: Lays the two rings the reference draws round the word: a
    |   thick white outline hugging the letters, and a thin black keyline
    |   outside that.
    |
    |   The keyline is not decoration. The outline is white and the title screen
    |   behind it is a photograph, much of which is pale -- without a dark edge
    |   the outline dissolves into the picture and the letters lose their shape.
    |   Painted outermost first so the white ring writes over its inner edge.
    | Author: suinevere
    | Dependencies: PIL
    | Globals: BLACK, WHITE, OUTLINE_W, KEYLINE_W
    | Params: px -- index buffer; solid -- the letterform mask
    | Returns: N/A
    ----------------------
    """
    outer = grow(solid, OUTLINE_W + KEYLINE_W).load()
    white = grow(solid, OUTLINE_W).load()
    s = solid.load()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if s[x, y]:
                continue
            if white[x, y]:
                px[x, y] = WHITE
            elif outer[x, y]:
                px[x, y] = BLACK


def draw_bricks(px, own, solid):
    """
    ----------------------
    | draw_bricks
    | Description: Fills the letters solid black, then cuts the mortar back
    |   through them in white wherever one block meets another.
    |
    |   The joint is drawn as a line through the wall rather than as a gap
    |   between two drawn stones, which is what the reference does and is the
    |   whole character of it: the brickwork is white line art over black, not a
    |   picture of stone. MORTAR_W is shared between the two blocks either side,
    |   so each gives up half of it and the joint stays centred on the seam.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: BLACK, WHITE, MORTAR_W, WIDTH, HEIGHT
    | Params: px -- index buffer; own -- owner pixel access; solid -- the
    |   letterform mask
    | Returns: N/A
    ----------------------
    """
    s = solid.load()
    reach = MORTAR_W // 2
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if s[x, y]:
                px[x, y] = BLACK

    for y in range(HEIGHT):
        for x in range(WIDTH):
            if not s[x, y]:
                continue
            mine = own[x, y]
            joint = False
            for dy in range(-reach, reach + 1):
                for dx in range(-reach, reach + 1):
                    nx, ny = x + dx, y + dy
                    if not (0 <= nx < WIDTH and 0 <= ny < HEIGHT):
                        continue
                    if own[nx, ny] and own[nx, ny] != mine:
                        joint = True
                        break
                if joint:
                    break
            if joint:
                px[x, y] = WHITE


def render():
    """
    ----------------------
    | render
    | Description: Builds the whole logo as a WIDTH x HEIGHT buffer of palette
    |   indices: the keyline and outline rings, then the black letters with the
    |   white mortar cut through them.
    | Author: suinevere
    | Dependencies: PIL
    | Globals: N/A
    | Params: N/A
    | Returns: a PIL 'P' image whose pixel values are palette indices
    ----------------------
    """
    img = Image.new("P", (WIDTH, HEIGHT), TRANS)
    px = img.load()

    owner = rasterise(place_bricks())
    solid = owner.point([0] + [255] * 255)

    draw_rings(px, solid)
    draw_bricks(px, owner.load(), solid)

    flat = [0] * 768
    for idx, (r, g, b) in PALETTE.items():
        flat[idx * 3:idx * 3 + 3] = [r, g, b]
    img.putpalette(flat)
    return img


def rgb555(r, g, b):
    """
    ----------------------
    | rgb555
    | Description: Packs a 24-bit colour into the Saturn's RGB555 CRAM word,
    |   top bit set so the entry is opaque.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: N/A
    | Params: r, g, b -- 0..255 channels
    | Returns: the 16-bit word
    ----------------------
    """
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def cut_tiles(img):
    """
    ----------------------
    | cut_tiles
    | Description: Slices the logo into 8x8 4bpp VDP2 character patterns,
    |   deduplicating as it goes -- the surround is all one blank tile. Tile 0 is
    |   forced blank so a cell with nothing in it has something to point at.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: CELLS_W, CELLS_H, CELL
    | Params: img -- the 'P' image from render()
    | Returns: (tiles, cells) -- a list of 32-byte tiles and a CELLS_H x
    |   CELLS_W grid of indices into it
    ----------------------
    """
    px = img.load()
    blank = bytes(32)
    tiles = [blank]
    index = {blank: 0}
    cells = []

    for cy in range(CELLS_H):
        row = []
        for cx in range(CELLS_W):
            data = bytearray(32)
            for y in range(CELL):
                for x in range(0, CELL, 2):
                    hi = px[cx * CELL + x, cy * CELL + y] & 0x0f
                    lo = px[cx * CELL + x + 1, cy * CELL + y] & 0x0f
                    data[y * 4 + x // 2] = (hi << 4) | lo
            key = bytes(data)
            if key not in index:
                index[key] = len(tiles)
                tiles.append(key)
            row.append(index[key])
        cells.append(row)

    return tiles, cells


def write_inc(tiles, cells):
    """
    ----------------------
    | write_inc
    | Description: Emits title_logo.inc -- the palette words, the tile table and
    |   the cell grid -- in the form title_logo.cxx expects.
    | Author: suinevere
    | Dependencies: N/A
    | Globals: INC_PATH, PALETTE, CELLS_W, CELLS_H
    | Params: tiles -- the 32-byte patterns; cells -- the index grid
    | Returns: N/A
    ----------------------
    """
    out = []
    out.append("/*----------------------\n"
               " | title_logo.inc\n"
               " | Description: The Z-ATURN masonry logo as VDP2 4bpp character\n"
               " |   patterns, the cell grid that arranges them, and the two\n"
               " |   CRAM words they are coloured by. GENERATED by\n"
               " |   tools/make_logo.py -- do not edit.\n"
               " | Author: suinevere\n"
               " ----------------------*/\n")
    out.append("#define LOGO_CELLS_W %d\n#define LOGO_CELLS_H %d\n"
               "#define LOGO_TILE_N  %d\n#define LOGO_PAL_FIRST %d\n"
               "#define LOGO_PAL_N   %d\n\n"
               % (CELLS_W, CELLS_H, len(tiles), min(PALETTE), len(PALETTE)))

    out.append("static const unsigned short LOGO_PAL[LOGO_PAL_N] = {\n   ")
    for i in sorted(PALETTE):
        out.append(" 0x%04x," % rgb555(*PALETTE[i]))
    out.append("\n};\n\n")

    out.append("static const unsigned char LOGO_TILES[LOGO_TILE_N][32] = {\n")
    for t in tiles:
        out.append("    {" + ",".join("0x%02x" % b for b in t) + "},\n")
    out.append("};\n\n")

    out.append("static const unsigned short LOGO_CELL[LOGO_CELLS_H][LOGO_CELLS_W] = {\n")
    for row in cells:
        out.append("    {" + ",".join("%3d" % c for c in row) + "},\n")
    out.append("};\n")

    # Platform-default line endings, matching make_tga.py's write_inc: the
    # generated .inc is committed, and forcing LF on a CRLF checkout makes every
    # regeneration look like a whole-file rewrite.
    INC_PATH.write_text("".join(out), encoding="utf-8")


def main(argv):
    img = render()

    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    # Over mid-grey rather than white or black: the logo's own keyline and its
    # transparent surround both have to be visible in the preview.
    backdrop = Image.new("RGB", (WIDTH, HEIGHT), (72, 76, 84))
    rgb = img.convert("RGB")
    # From the raw indices, not img.point(): point() on a 'P' image keeps the
    # palette, so converting its result to 'L' would grade by colour instead of
    # by index and the transparent surround would not come out as zero.
    alpha = Image.frombytes("L", (WIDTH, HEIGHT),
                            bytes(0 if v == TRANS else 255
                                  for v in img.tobytes()))
    backdrop.paste(rgb, (0, 0), alpha)
    backdrop.resize((WIDTH * 4, HEIGHT * 4), Image.NEAREST).save(PREVIEW_PATH)

    if "--preview" in argv:
        print("preview only: %s" % PREVIEW_PATH)
        return 0

    tiles, cells = cut_tiles(img)
    write_inc(tiles, cells)
    print("%s: %d cells, %d unique tiles (%d bytes)"
          % (INC_PATH.name, CELLS_W * CELLS_H, len(tiles), len(tiles) * 32))
    print("preview: %s" % PREVIEW_PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
