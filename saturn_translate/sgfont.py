"""Steamgear Mash (Japan) font codec + English 16x16 glyph generator.

The game's kana/kanji font lives in VDP2 VRAM at 0x14000 as 16x16 glyphs.
Each glyph = 2x2 arrangement of 8x8 cells in order TL, TR, BL, BR (row-major),
4 consecutive 0x20-byte cells => 0x80 bytes per glyph. Each cell is 4bpp, 2px
per byte (high nibble = left pixel). The stroke colour is palette nibble 3
(outline/anti-alias uses 4; 0 = transparent). Confirmed byte-exact against the
font rendered from the savestate (see analysis/fs16b.png).

This module provides:
  decode_glyph(data)  -> 16x16 grid of palette nibbles
  encode_glyph(grid)  -> 0x80 bytes in the game's cell layout
  english_glyph(ch)   -> 0x80 bytes rendering an ASCII char as a 16x16 glyph
  english_run(text)   -> list of (char, glyph_bytes)

Used by route-B reinsertion: overwrite chosen glyph slots in the decompressed
font (or post-copy in VRAM at 0x14000) with English glyphs.
"""

GLYPH_BYTES = 0x80
CELL_BYTES = 0x20
STROKE_NIBBLE = 3
# 2x2 cell origins within the 16x16 glyph, in storage order TL, TR, BL, BR
_CELLS = ((0, 0), (8, 0), (0, 8), (8, 8))


def decode_glyph(data):
    """Decode 0x80 bytes -> 16x16 list of rows of palette nibbles (0..15)."""
    if len(data) < GLYPH_BYTES:
        raise ValueError("glyph needs %d bytes, got %d" % (GLYPH_BYTES, len(data)))
    grid = [[0] * 16 for _ in range(16)]
    for ci, (cx, cy) in enumerate(_CELLS):
        base = ci * CELL_BYTES
        for yy in range(8):
            for xx in range(0, 8, 2):
                b = data[base + yy * 4 + xx // 2]
                grid[cy + yy][cx + xx] = (b >> 4) & 0xF
                grid[cy + yy][cx + xx + 1] = b & 0xF
    return grid


def encode_glyph(grid):
    """Encode a 16x16 nibble grid -> 0x80 bytes in the game's TL,TR,BL,BR layout."""
    out = bytearray(GLYPH_BYTES)
    for ci, (cx, cy) in enumerate(_CELLS):
        base = ci * CELL_BYTES
        for yy in range(8):
            for xx in range(0, 8, 2):
                hi = grid[cy + yy][cx + xx] & 0xF
                lo = grid[cy + yy][cx + xx + 1] & 0xF
                out[base + yy * 4 + xx // 2] = (hi << 4) | lo
    return bytes(out)


# --- compact 5x7 ASCII font (rows top->bottom, '#' = ink) -------------------
_F = {
    "A": ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    "B": "####.|#...#|#...#|####.|#...#|#...#|####.",
    "C": ".###.|#...#|#....|#....|#....|#...#|.###.",
    "D": "####.|#...#|#...#|#...#|#...#|#...#|####.",
    "E": "#####|#....|#....|####.|#....|#....|#####",
    "F": "#####|#....|#....|####.|#....|#....|#....",
    "G": ".###.|#...#|#....|#.###|#...#|#...#|.###.",
    "H": "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    "I": ".###.|..#..|..#..|..#..|..#..|..#..|.###.",
    "J": "..###|...#.|...#.|...#.|#..#.|#..#.|.##..",
    "K": "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    "L": "#....|#....|#....|#....|#....|#....|#####",
    "M": "#...#|##.##|#.#.#|#...#|#...#|#...#|#...#",
    "N": "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#",
    "O": ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    "P": "####.|#...#|#...#|####.|#....|#....|#....",
    "Q": ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
    "R": "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    "S": ".####|#....|#....|.###.|....#|....#|####.",
    "T": "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    "U": "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
    "V": "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
    "W": "#...#|#...#|#...#|#...#|#.#.#|##.##|#...#",
    "X": "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
    "Y": "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    "Z": "#####|....#|...#.|..#..|.#...|#....|#####",
    "0": ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    "1": "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
    "2": ".###.|#...#|....#|...#.|..#..|.#...|#####",
    "3": "####.|....#|....#|.###.|....#|....#|####.",
    "4": "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    "5": "#####|#....|####.|....#|....#|#...#|.###.",
    "6": ".###.|#....|#....|####.|#...#|#...#|.###.",
    "7": "#####|....#|...#.|..#..|.#...|.#...|.#...",
    "8": ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    "9": ".###.|#...#|#...#|.####|....#|....#|.###.",
    " ": ".....|.....|.....|.....|.....|.....|.....",
    ".": ".....|.....|.....|.....|.....|.##..|.##..",
    ",": ".....|.....|.....|.....|.##..|.##..|.#...",
    "!": "..#..|..#..|..#..|..#..|..#..|.....|..#..",
    "?": ".###.|#...#|....#|..##.|..#..|.....|..#..",
    "'": "..#..|..#..|.#...|.....|.....|.....|.....",
    "-": ".....|.....|.....|#####|.....|.....|.....",
    ":": ".....|.##..|.##..|.....|.##..|.##..|.....",
    "(": "..#..|.#...|#....|#....|#....|.#...|..#..",
    ")": "..#..|...#.|....#|....#|....#|...#.|..#..",
    "/": "....#|...#.|...#.|..#..|.#...|.#...|#....",
}


def english_glyph(ch, scale=2, ox=3, oy=1):
    """Render an ASCII char as a 16x16 game-format glyph (0x80 bytes).

    The 5x7 cell at `scale` (default 2 -> 10x14) is placed at (ox, oy).
    Unknown chars render blank. Uppercase-folds letters.
    """
    grid = [[0] * 16 for _ in range(16)]
    pat = _F.get(ch) or _F.get(ch.upper())
    if pat:
        rows = pat.split("|")
        for ry, row in enumerate(rows):
            for rx, c in enumerate(row):
                if c == "#":
                    for dy in range(scale):
                        for dx in range(scale):
                            x, y = ox + rx * scale + dx, oy + ry * scale + dy
                            if 0 <= x < 16 and 0 <= y < 16:
                                grid[y][x] = STROKE_NIBBLE
    return encode_glyph(grid)


def english_run(text, **kw):
    """Return [(char, 0x80-byte glyph), ...] for each char in text."""
    return [(c, english_glyph(c, **kw)) for c in text]


def glyph_ascii(grid_or_bytes):
    """Debug: render a glyph as ASCII art (stroke nibble -> '#')."""
    grid = grid_or_bytes
    if isinstance(grid_or_bytes, (bytes, bytearray)):
        grid = decode_glyph(grid_or_bytes)
    return "\n".join(
        "".join("#" if n == STROKE_NIBBLE else ("+" if n else ".") for n in row)
        for row in grid
    )
