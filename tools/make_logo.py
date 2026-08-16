#!/usr/bin/env python3
"""Draw the Z-ATURN masonry title logo and emit it as VDP2 text-layer tiles.

The ZORK cover lettering is not letters with holes in them. It is ONE SLAB OF
WALL with the word incised into it: measure the traced reference and the whole
four-letter mass encloses sixteen holes, none bigger than about thirty pixels
square on a 630 by 250 word -- the O's counter is packed with a portcullis, the
R's is packed with stone, and the K's arms are divided from its stem by a drawn
line and not by a gap. Every attempt at this that starts from outlined letters
with open counters comes out as ordinary lettering with a brick fill, which is
the one thing the reference is not. So the word is built here as a solid wall,
and the letterforms are cut into it as joints.

The stones are not invented either. tools/assets/logo/zork-logo.png is traced
at build time into a map of its stones, and every stone in Z-ATURN is one of
them, sampled out of the reference and cut to a stroke here. Their character --
no two the same size, hardly an edge parallel to another, joints that kink --
is what a table of hand-placed quads cannot hold, and is most of what the eye
recognises.

The reference is two inks on paper, and measuring it settles the three line
weights that matter. On its 250-pixel cap height the outer stroke is about six
pixels, the paper channel inside that stroke about six, and the joints between
stones about six: all the same. Scaled to this cap height they are two pixels
each -- so the black stroke round the word is as heavy as the mortar, which is
what gives the reference its weight and is the thing freehand attempts get
wrong first.

How big a stone is, though, is NOT the reference's business. Its stones are
drawn for a letter 250 pixels tall; sampled at their true relative size onto a
letter 76 pixels tall they come out at thirteen pixels with a two-pixel joint
between every pair, and the letter reads as a dense block. STONE_SCALE is the
knob for that, and it is set so about eight stones cover a letter -- the same
stones, the same shapes, laid bigger.

It does NOT go on the disc as a picture. The title screen already spends NBG0
on a room photograph, so the logo rides the text layer (NBG3) instead: it is
cut into 8x8 4bpp cells, written into the free character block below the SGL
font, and printed as pattern names. Transparent outside the stroke, so the
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

import numpy as np
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
REF_PATH = REPO / "tools" / "assets" / "logo" / "zork-logo.png"
INC_PATH = REPO / "saturn" / "src" / "video" / "title_logo.inc"
PREVIEW_PATH = REPO / "tools" / "assets" / "logo" / "zaturn_logo.png"

CELL = 8                       # VDP2 cell size, both axes
CELLS_W, CELLS_H = 38, 12      # the logo's footprint on the 40x28 text grid
WIDTH, HEIGHT = CELLS_W * CELL, CELLS_H * CELL

# All three the same, because the reference's are: see the module docstring.
MORTAR_W = 2                   # joint between two stones, in paper
CHANNEL_W = 2                  # paper between the wall and the stroke
STROKE_W = 2                   # the black stroke round the word

CAP_TOP = 12                   # room above the cap line for the rings and for
CAP_H = 76                     # the strokes that step out of it

# Reference pixels per target pixel when sampling stones. NOT the letter's own
# scale -- see the docstring. Bigger is fewer, larger stones.
STONE_SCALE = 2.5

# Letters touch. The reference's do more than touch -- its letterforms
# interlock, and the whole word carries one stroke round the outside -- and
# setting these flush is as near as seven letters get to it in 304 pixels. It
# also buys the width back: the letters are fat here because the tracking is
# not eating it.
TRACK = 0

# Palette indices. 0 is VDP2 transparency and 1, 2 and 15 belong to the console
# font (see the module docstring), so the art starts at 3.
TRANS = 0
BLACK, WHITE = 3, 4

PALETTE = {
    BLACK: (0x00, 0x00, 0x00),
    WHITE: (0xff, 0xff, 0xff),
}


def shift(m, dy, dx):
    """
    ----------------------
    | shift
    | Description: Translates an array by whole pixels, filling the vacated edge
    |   with zeros. The building block for dilate and erode.
    | Author: suinevere
    | Dependencies: numpy
    | Globals: N/A
    | Params: m -- the array; dy, dx -- the translation
    | Returns: the translated array
    ----------------------
    """
    o = np.zeros_like(m)
    ys = slice(max(dy, 0), m.shape[0] + min(dy, 0))
    yd = slice(max(-dy, 0), m.shape[0] + min(-dy, 0))
    xs = slice(max(dx, 0), m.shape[1] + min(dx, 0))
    xd = slice(max(-dx, 0), m.shape[1] + min(-dx, 0))
    o[ys, xs] = m[yd, xd]
    return o


def dilate(m, n=1):
    """
    ----------------------
    | dilate
    | Description: Grows a boolean mask by n pixels, four-connected.
    | Author: suinevere
    | Dependencies: shift
    | Globals: N/A
    | Params: m -- the mask; n -- pixels to grow by
    | Returns: the grown mask
    ----------------------
    """
    for _ in range(n):
        m = m | shift(m, 1, 0) | shift(m, -1, 0) | shift(m, 0, 1) | shift(m, 0, -1)
    return m


def erode(m, n=1):
    """
    ----------------------
    | erode
    | Description: Shrinks a boolean mask by n pixels, four-connected.
    | Author: suinevere
    | Dependencies: shift
    | Globals: N/A
    | Params: m -- the mask; n -- pixels to shrink by
    | Returns: the shrunken mask
    ----------------------
    """
    for _ in range(n):
        m = m & shift(m, 1, 0) & shift(m, -1, 0) & shift(m, 0, 1) & shift(m, 0, -1)
    return m


def label(m):
    """
    ----------------------
    | label
    | Description: Numbers the four-connected regions of a boolean mask, from 1.
    | Author: suinevere
    | Dependencies: numpy
    | Globals: N/A
    | Params: m -- the mask
    | Returns: (labels, count)
    ----------------------
    """
    h, w = m.shape
    lab = np.zeros((h, w), np.int32)
    cur = 0
    for sy in range(h):
        for sx in range(w):
            if not m[sy, sx] or lab[sy, sx]:
                continue
            cur += 1
            st = [(sy, sx)]
            lab[sy, sx] = cur
            while st:
                y, x = st.pop()
                for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if 0 <= ny < h and 0 <= nx < w and m[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = cur
                        st.append((ny, nx))
    return lab, cur


def grow_labels(lab, into, n):
    """
    ----------------------
    | grow_labels
    | Description: Spreads a label image outward into a mask, so pixels that
    |   were shaved off a region -- or that fell in a joint -- are given back to
    |   the nearest region that claims them.
    | Author: suinevere
    | Dependencies: shift
    | Globals: N/A
    | Params: lab -- the label image; into -- where labels may spread; n -- how
    |   many pixels to spread by
    | Returns: the spread label image
    ----------------------
    """
    for _ in range(n):
        nxt = lab.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = shift(lab, dy, dx)
            take = (nxt == 0) & (sh > 0) & into
            nxt[take] = sh[take]
        lab = nxt
    return lab


_TRACE = None


def trace():
    """
    ----------------------
    | trace
    | Description: Reads the ZORK reference and separates it into the map of
    |   stones this logo is built out of, plus the letterform they make up.
    |
    |   The reference is two inks: black, and the paper it is printed on. So the
    |   black is BOTH the stones and the stroke drawn round the whole word, and
    |   the paper is BOTH the background and every joint. The stroke is peeled
    |   off by distance rather than by connectivity -- it is about six pixels
    |   thick and the stones stand another six back from it behind the channel,
    |   so black within eight pixels of the background is stroke and nothing
    |   else. Peeling it as "the ink region that touches the background" instead
    |   loses whichever stones happen to touch the stroke, which cost the Z a
    |   third of its masonry.
    |
    |   The stones are then eroded before they are numbered, so that a joint
    |   thinned to nothing by the printing does not weld two of them together,
    |   and grown back afterwards.
    | Author: suinevere
    | Dependencies: PIL, numpy, REF_PATH
    | Globals: _TRACE
    | Params: N/A
    | Returns: (stones, form) -- the numbered stone map and the letterform mask,
    |   both at the reference's own resolution
    ----------------------
    """
    global _TRACE
    if _TRACE is not None:
        return _TRACE

    grey = np.array(Image.open(REF_PATH).convert("L")).astype(int)
    ink = grey < 128

    paper, _ = label(~ink)
    background = paper == paper[0, 0]

    stones = ink & ~dilate(background, 8)
    lab, _ = label(erode(stones, 2))
    lab = grow_labels(lab, stones, 4)
    lab[~stones] = 0

    ids, counts = np.unique(lab[lab > 0], return_counts=True)
    for i, c in zip(ids, counts):
        if c < 60:                      # printing crumbs, not stones
            lab[lab == i] = 0

    # Closing by more than a joint's width recovers the silhouette the stones
    # make up, joints and all, which is the letterform the outline follows.
    form = erode(dilate(lab > 0, 5), 5)
    _TRACE = (lab, form)
    return _TRACE


# Where in the reference to take stones from, by the kind of stroke they have to
# fill. A bar wants stones out of a bar and a diagonal wants stones out of a
# diagonal: the reference's courses run along its strokes, and sampling a
# diagonal out of a bar lays them across it instead. Each is the reference
# coordinate that the stroke's own top-left corner samples from.
FROM_BAR_TOP = (35, 30)         # the Z's top bar
FROM_BAR_FOOT = (35, 196)       # the Z's bottom bar
FROM_STEM = (352, 44)           # the R's left stem
FROM_STEM2 = (505, 28)          # the K's stem
FROM_DIAG_DOWN_LEFT = (62, 80)  # the Z's diagonal
FROM_DIAG_DOWN_RIGHT = (543, 146)   # the K's lower arm
FROM_FIELD = (404, 60)          # the R's bowl, for the wall between the strokes

# Every other letter is a set of strokes: a polygon in glyph-local target
# pixels, and the reference corner its stones come from. Cap height is CAP_H and
# a stroke may run outside that -- the reference's letters step over their own
# cap line and this is where that comes from.
#
# Strokes are laid in order and a later one wins the overlap, so a stem laid
# after a bar cuts its own joint against it.
GLYPHS = {
    # A chevron. The top bar's left end rises out of the cap line, the diagonal
    # is one lean slab off the bar's right end, and the foot drops below the
    # baseline -- the reference Z's own moves.
    "Z": (52, [
        ([(0, -4), (52, 1), (52, 21), (0, 27)], FROM_BAR_TOP),
        ([(34, 14), (52, 17), (21, 62), (0, 58)], FROM_DIAG_DOWN_LEFT),
        ([(0, 52), (52, 56), (52, 76), (0, 80)], FROM_BAR_FOOT),
    ]),

    "-": (14, [
        ([(0, 30), (14, 27), (14, 43), (0, 47)], FROM_FIELD),
    ]),

    # Solid from the apex down through the crossbar. A counter up there would be
    # nine pixels across before the rings ate their eight, and the reference
    # keeps its counters small and low for the same reason.
    "A": (44, [
        ([(13, 0), (31, -2), (36, 34), (7, 34)], FROM_STEM),
        ([(4, 26), (38, 28), (41, 50), (2, 48)], FROM_BAR_TOP),
        ([(2, 42), (20, 42), (13, 78), (-2, 76)], FROM_DIAG_DOWN_LEFT),
        ([(24, 42), (42, 42), (45, 76), (30, 78)], FROM_DIAG_DOWN_RIGHT),
    ]),

    # The bar oversails the stem at both ends and steps down to the right, which
    # keeps the top of the word moving across a letter that is otherwise a post.
    "T": (40, [
        ([(0, -3), (40, 2), (40, 22), (0, 25)], FROM_BAR_TOP),
        ([(12, 16), (29, 18), (31, 78), (10, 76)], FROM_STEM2),
    ]),

    # Piers off different stems of the reference, so the two sides of the
    # counter are not each other's reflection.
    "U": (42, [
        ([(0, -1), (14, 2), (13, 64), (0, 62)], FROM_STEM),
        ([(28, 2), (42, -2), (42, 62), (29, 64)], FROM_STEM2),
        ([(0, 54), (42, 57), (42, 76), (0, 79)], FROM_BAR_FOOT),
    ]),

    # Drawn after the reference's own R: a stem, a bowl of three short strokes
    # round a counter that is small and high, and a leg kicking out from under
    # it on its own slope. Every stroke overlaps its neighbour by most of a
    # course -- abutting them leaves the leg standing on nothing, because the
    # two get different stones and the joint between them cuts right through.
    "R": (50, [
        ([(0, -2), (15, 1), (16, 78), (0, 75)], FROM_STEM),
        ([(9, -1), (50, 3), (50, 17), (9, 15)], FROM_BAR_TOP),
        ([(35, 11), (50, 14), (50, 39), (35, 42)], FROM_STEM2),
        ([(9, 34), (50, 37), (49, 55), (9, 52)], FROM_BAR_FOOT),
        ([(22, 47), (42, 45), (50, 77), (34, 79)], FROM_DIAG_DOWN_RIGHT),
    ]),

    # The diagonal runs out into the right-hand pier about two-thirds down and
    # below that the two are one piece of wall, which is what the reference's K
    # does where its arms meet its stem.
    "N": (48, [
        ([(0, -2), (13, 1), (13, 78), (0, 75)], FROM_STEM),
        ([(35, 1), (48, -5), (48, 75), (35, 78)], FROM_STEM2),
        ([(9, -2), (24, 0), (48, 76), (33, 78)], FROM_DIAG_DOWN_RIGHT),
    ]),
}

WORD = "Z-ATURN"


def stroke_stones(poly, src, w, base):
    """
    ----------------------
    | stroke_stones
    | Description: Fills one stroke of a letter with masonry sampled out of the
    |   reference: the polygon says what shape the stroke is, and src says which
    |   corner of the reference the stones inside it are taken from.
    |
    |   Pixels the reference had a joint under come back as zero. They are not
    |   left as joints -- at this scale the reference's six-pixel joints sample
    |   down to one or two and come out ragged -- so they are given to whichever
    |   stone is nearest and the joints are re-cut to MORTAR_W later. What the
    |   reference supplies is WHERE the joints fall and what shape the stones
    |   are; the width of the line is drawn fresh.
    | Author: suinevere
    | Dependencies: numpy, PIL, trace
    | Globals: SCALE, CAP_H
    | Params: poly -- the stroke, in glyph-local pixels; src -- the reference
    |   corner to sample from; w -- the glyph's width; base -- what to add to
    |   every stone number so this stroke's stones are nobody else's
    | Returns: an int array, w wide and the glyph's full height, holding stone
    |   numbers and zero outside the stroke
    ----------------------
    """
    ref, _form = trace()
    rh, rw = ref.shape

    ys = [p[1] for p in poly]
    top, bot = min(ys), max(ys)
    h = bot - top + 1

    cut = Image.new("L", (w, h), 0)
    ImageDraw.Draw(cut).polygon([(x, y - top) for x, y in poly], fill=1)
    inside = np.array(cut).astype(bool)

    # src is the reference corner the STROKE's own top-left samples from, not
    # the glyph's -- a stroke that starts two-thirds of the way down a letter
    # would otherwise read two-thirds of the way down the reference from src as
    # well, which for the feet ran clean off the bottom of it and came back
    # empty.
    ty, tx = np.mgrid[0:h, 0:w]
    ry = np.clip((src[1] + ty * STONE_SCALE).astype(int), 0, rh - 1)
    rx = np.clip((src[0] + tx * STONE_SCALE).astype(int), 0, rw - 1)

    got = np.where(inside, ref[ry, rx], 0)
    # Generously, and then checked: a source window that runs off the edge of
    # the reference samples bare paper, and the stroke comes back with a hole
    # through it that detaches whatever hangs below. That is not visible in the
    # preview as anything but a letter quietly missing a leg.
    got = grow_labels(got, inside, max(h, w))
    if (inside & (got == 0)).any():
        raise ValueError("stroke at %r sampled outside the reference's stones"
                         % (src,))
    got[got > 0] += base
    return got, top


def place_glyphs():
    """
    ----------------------
    | place_glyphs
    | Description: Lays the word out centred on the canvas and returns both the
    |   stone map and the mask of the letterforms themselves.
    |
    |   The two are not the same thing, and that is the whole design: the stones
    |   go on to fill a solid wall, while the letterform mask is what gets
    |   incised into it.
    | Author: suinevere
    | Dependencies: numpy, stroke_stones
    | Globals: GLYPHS, WORD, TRACK, CAP_TOP, WIDTH, HEIGHT
    | Params: N/A
    | Returns: (owner, letters) -- the stone map and the letterform mask
    ----------------------
    """
    total = sum(GLYPHS[c][0] for c in WORD) + TRACK * (len(WORD) - 1)
    pen = (WIDTH - total) // 2

    owner = np.zeros((HEIGHT, WIDTH), np.int32)
    base = 0
    for ch in WORD:
        w = GLYPHS[ch][0]
        for poly, src in GLYPHS[ch][1]:
            got, top = stroke_stones(poly, src, w, base)
            base += 400
            y0 = CAP_TOP + top
            gh, gw = got.shape
            y1, x1 = min(HEIGHT, y0 + gh), min(WIDTH, pen + gw)
            if y0 >= y1 or pen >= x1:
                continue
            dst = owner[max(y0, 0):y1, max(pen, 0):x1]
            sub = got[max(-y0, 0):y1 - y0, max(-pen, 0):x1 - pen]
            dst[sub > 0] = sub[sub > 0]
        pen += w + TRACK

    return owner, owner > 0


def renumber(owner):
    """
    ----------------------
    | renumber
    | Description: Re-numbers the stone map so every CONTIGUOUS piece of stone
    |   is its own stone, then folds the specks away.
    |
    |   Both halves matter. A stone the reference used twice, or one a stroke's
    |   polygon cut into two pieces, would otherwise share a number across the
    |   letter and get no joint drawn between the pieces. And scaling down by
    |   three and a bit leaves slivers a pixel or two across along every cut
    |   edge, which draw as mortar dashes rather than as stones; each is given to
    |   whichever neighbour it touches most.
    | Author: suinevere
    | Dependencies: numpy, label, grow_labels
    | Globals: N/A
    | Params: owner -- the stone map
    | Returns: the re-numbered map
    ----------------------
    """
    solid = owner > 0
    out = np.zeros_like(owner)
    nxt = 0
    for v in np.unique(owner[solid]):
        piece, n = label(owner == v)
        piece[piece > 0] += nxt
        out[piece > 0] = piece[piece > 0]
        nxt += n

    ids, counts = np.unique(out[out > 0], return_counts=True)
    doomed = ids[counts < 14]
    if len(doomed):
        out[np.isin(out, doomed)] = 0
        out = grow_labels(out, solid, 8)
    return out


def draw_mortar(px, owner):
    """
    ----------------------
    | draw_mortar
    | Description: Fills the letters solid black, then cuts the joints back
    |   through them in paper wherever one stone meets another.
    |
    |   The joint is a line drawn THROUGH the wall, not a gap left between two
    |   drawn stones, which is what the reference does and is the whole character
    |   of it. MORTAR_W is shared between the two stones either side, so each
    |   gives up half and the joint stays centred on the seam.
    | Author: suinevere
    | Dependencies: numpy
    | Globals: BLACK, WHITE, MORTAR_W
    | Params: px -- the index buffer; owner -- the stone map
    | Returns: N/A
    ----------------------
    """
    solid = owner > 0
    px[solid] = BLACK

    reach = MORTAR_W // 2
    seam = np.zeros_like(solid)
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            if not dy and not dx:
                continue
            other = shift(owner, dy, dx)
            seam |= solid & (other > 0) & (other != owner)
    px[seam] = WHITE


def draw_rings(px, owner):
    """
    ----------------------
    | draw_rings
    | Description: Lays the two things the reference draws round its letters: a
    |   channel of bare paper hugging them, and the black stroke outside that.
    |
    |   The stroke is not a keyline. It is as heavy as the mortar -- measured off
    |   the reference, all three weights are the same -- and it is what holds the
    |   word together over a photograph: the channel is white and much of a room
    |   picture is pale, so without the stroke the channel dissolves into the
    |   picture and the letters lose their shape. Painted outermost first so the
    |   channel writes over its inner edge.
    | Author: suinevere
    | Dependencies: dilate
    | Globals: BLACK, WHITE, CHANNEL_W, STROKE_W
    | Params: px -- the index buffer; owner -- the stone map
    | Returns: N/A
    ----------------------
    """
    solid = owner > 0
    outer = dilate(solid, CHANNEL_W + STROKE_W)
    channel = dilate(solid, CHANNEL_W)
    px[outer & ~solid] = BLACK
    px[channel & ~solid] = WHITE


def render():
    """
    ----------------------
    | render
    | Description: Builds the whole logo as a HEIGHT x WIDTH buffer of palette
    |   indices: the stroke and the channel round the wall, the black stones
    |   with the paper joints cut through them, and the word incised into it.
    | Author: suinevere
    | Dependencies: numpy, PIL
    | Globals: N/A
    | Params: N/A
    | Returns: a PIL 'P' image whose pixel values are palette indices
    ----------------------
    """
    strokes, _letters = place_glyphs()
    owner = renumber(strokes)

    px = np.full((HEIGHT, WIDTH), TRANS, np.uint8)
    draw_rings(px, owner)
    draw_mortar(px, owner)

    img = Image.fromarray(px, "P")
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
    # Over mid-grey rather than white or black: the logo's own stroke and its
    # transparent surround both have to be visible in the preview.
    backdrop = Image.new("RGB", (WIDTH, HEIGHT), (72, 76, 84))
    rgb = img.convert("RGB")
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
