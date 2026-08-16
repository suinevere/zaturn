#!/usr/bin/env python3
"""Host tests for tools/make_logo.py. Run: python tools/tests/test_make_logo.py

The logo is 4bpp art on the VDP2 text layer, which is a much narrower target
than a background TGA: it has to fit sixteen palette entries, three of which
belong to the console font and one of which is transparency, and it has to
survive being cut into 8x8 character patterns and reassembled by hardware. Both
of those failures are invisible in the preview PNG and only show up on a Saturn,
so they are checked here.

check() raises as well as recording, matching test_make_tga.py: a failed
assertion has to fail under pytest, which is the gate this project runs, while
main() swallows the raise per test so a direct run still reaches every test.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import make_logo

FAILURES = []

# The console owns these three entries of palette 0: the font colour the
# Options page rewrites, the reverse-video punch-out, and the block cursor.
# See saturn/src/menu/options.cxx:text_set_color.
CONSOLE_ENTRIES = (1, 2, 15)


def check(cond, label):
    print(("  ok   " if cond else "  FAIL ") + label)
    if not cond:
        FAILURES.append(label)
        raise AssertionError(label)


def parse_inc(text):
    """The generated .inc, back into (defines, tiles, cells)."""
    defines = {k: int(v) for k, v in
               re.findall(r"#define (LOGO_\w+)\s+(\d+)", text)}
    tiles = re.search(r"LOGO_TILES\[LOGO_TILE_N\]\[32\] = \{(.*?)\n\};",
                      text, re.S).group(1)
    tiles = [[int(b, 16) for b in row.split(",")]
             for row in re.findall(r"\{([^}]*)\}", tiles)]
    cells = re.search(r"LOGO_CELL\[LOGO_CELLS_H\]\[LOGO_CELLS_W\] = \{(.*?)\n\};",
                      text, re.S).group(1)
    cells = [[int(v) for v in row.split(",")]
             for row in re.findall(r"\{([^}]*)\}", cells)]
    return defines, tiles, cells


def test_render_stays_inside_the_free_palette_entries():
    print("test_render_stays_inside_the_free_palette_entries")
    used = set(make_logo.render().tobytes())

    check(max(used) < 16, "every index fits a 4bpp cell")
    check(not (used & set(CONSOLE_ENTRIES)),
          "no index collides with the console's entries 1, 2 and 15")
    check(make_logo.TRANS in used, "index 0 is used, so the logo has a surround")
    check(used - {make_logo.TRANS} <= set(make_logo.PALETTE),
          "every non-transparent index is one PALETTE declares")


def test_render_is_deterministic():
    print("test_render_is_deterministic")
    # Nothing here is random any more, but the .inc is committed and the check
    # is cheap: anything that reintroduced jitter would rewrite several KB of
    # generated source on every invocation.
    check(make_logo.render().tobytes() == make_logo.render().tobytes(),
          "two renders of the same source produce the same pixels")


def solid_mask():
    """The word as a plain set of stone pixels, straight off the owner map."""
    own = make_logo.rasterise(make_logo.place_bricks()).load()
    return {(x, y)
            for y in range(make_logo.HEIGHT)
            for x in range(make_logo.WIDTH)
            if own[x, y]}


def glyph_origin(ch):
    """Where a letter's local (0, 0) lands on the canvas -- place_bricks's sum."""
    widths = [make_logo.BRICKS[c][0] for c in make_logo.WORD]
    pen = (make_logo.WIDTH - sum(widths)
           - make_logo.TRACK * (len(make_logo.WORD) - 1)) // 2
    for c, w in zip(make_logo.WORD, widths):
        if c == ch:
            return pen, make_logo.CAP_TOP + make_logo.RIDE[c]
        pen += w + make_logo.TRACK
    raise KeyError(ch)


# One point well inside each counter, in glyph-local coordinates. These are the
# four holes that tell U, R and N apart from one another and the A from a
# triangle, and every one of them is only a handful of pixels wide once the
# white outline and the black keyline have both grown inwards. Widening a stroke
# or deepening a course by two pixels closes one without touching anything the
# eye would flag in the preview, so they are probed by coordinate.
COUNTERS = (("A", 25, 56), ("U", 19, 30), ("R", 21, 26), ("N", 20, 56))


def test_counters_stay_open():
    print("test_counters_stay_open")
    px = make_logo.render().load()
    for ch, lx, ly in COUNTERS:
        ox, oy = glyph_origin(ch)
        check(px[ox + lx, oy + ly] == make_logo.TRANS,
              "the %s's counter is still open at local (%d, %d)" % (ch, lx, ly))

    # The R's bowl is the one hole the word closes the whole way round. If a
    # second appears, a stroke has met something it should not have.
    check(flood_enclosed(solid_mask()) == 1,
          "the R's bowl is the only counter the word encloses")


def flood_enclosed(stone):
    """How many transparent regions the word completely encloses."""
    w, h = make_logo.WIDTH, make_logo.HEIGHT

    def open_at(n):
        return (0 <= n[0] < w and 0 <= n[1] < h and n not in stone)

    def flood(seed, seen):
        stack = [seed]
        seen.add(seed)
        while stack:
            x, y = stack.pop()
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if open_at(n) and n not in seen:
                    seen.add(n)
                    stack.append(n)

    seen = set()
    flood((0, 0), seen)                 # the surround, from a corner

    regions = 0
    for y in range(h):
        for x in range(w):
            if (x, y) in stone or (x, y) in seen:
                continue
            regions += 1
            flood((x, y), seen)
    return regions


def test_stems_are_not_pinched():
    print("test_stems_are_not_pinched")
    stone = solid_mask()

    # A stroke thinning to a few pixels is the specific failure this catches --
    # the A's legs did exactly that when its crossbar was cut out as a hole.
    #
    # Measured as a horizontal run, but a run being short is not enough on its
    # own: the last row of any corner the offsets have tilted is legitimately a
    # pixel or two wide, and so is the point a wedge closes to. A pinch is a
    # thin run that PERSISTS -- so the test is how far down the word a column
    # can stay inside a short run, not how short the shortest run is.
    thin = set()
    for y in range(make_logo.HEIGHT):
        run = []
        for x in range(make_logo.WIDTH + 1):
            if (x, y) in stone:
                run.append(x)
                continue
            if 0 < len(run) < 8:
                thin.update((rx, y) for rx in run)
            run = []

    worst, where = 0, None
    for x in range(make_logo.WIDTH):
        depth = 0
        for y in range(make_logo.HEIGHT):
            depth = depth + 1 if (x, y) in thin else 0
            if depth > worst:
                worst, where = depth, (x, y)
    check(worst < 5,
          "no stroke stays under 8px for five rows (worst was %d at %s)"
          % (worst, where))


def test_every_block_sits_on_the_course_ladder():
    print("test_every_block_sits_on_the_course_ladder")
    starts = {y0 for y0, _y1 in make_logo.CY}
    ends = {y1 for _y0, y1 in make_logo.CY}
    # The corner offsets move an edge by a few pixels and the cap and baseline
    # spurs by up to six, so a block no longer lands exactly on a course line.
    # What must still hold is that it lands NEAR one at both ends: a block that
    # started or finished halfway down a course would put a joint across the
    # middle of its neighbour and break the bond right along the word.
    reach = 6
    bad = []
    for ch, (_w, blocks) in make_logo.BRICKS.items():
        if ch == "-":
            continue                    # declared off the ladder; see BRICKS
        for i, poly in enumerate(blocks):
            ys = [y for _x, y in poly]
            near_top = min(abs(min(ys) - s) for s in starts)
            near_bot = min(abs(max(ys) - e) for e in ends)
            if near_top > reach or near_bot > reach:
                bad.append("%s block %d (%d..%d)" % (ch, i, min(ys), max(ys)))
    check(not bad, "every block starts and finishes on a course (%s)"
          % (", ".join(bad) if bad else "all"))


def test_the_keyline_backs_the_whole_outline():
    print("test_the_keyline_backs_the_whole_outline")
    px = make_logo.render().load()

    # White against a pale photograph is invisible, so no white pixel may face
    # transparency directly -- the black keyline has to be behind all of it.
    # This is the one failure that would look fine in the preview, which is
    # drawn over a mid grey, and disappear on the actual title screen.
    leaks = 0
    for y in range(make_logo.HEIGHT):
        for x in range(make_logo.WIDTH):
            if px[x, y] != make_logo.WHITE:
                continue
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                off = not (0 <= nx < make_logo.WIDTH
                           and 0 <= ny < make_logo.HEIGHT)
                if off or px[nx, ny] == make_logo.TRANS:
                    leaks += 1
    check(leaks == 0, "no white pixel faces transparency (%d did)" % leaks)


def test_the_mortar_is_drawn_through_the_letters():
    print("test_the_mortar_is_drawn_through_the_letters")
    img = make_logo.render()
    px = img.load()
    solid = solid_mask()
    used = set(img.tobytes())

    check(used == {make_logo.TRANS, make_logo.BLACK, make_logo.WHITE},
          "the art is transparent, black and white and nothing else")

    inside_white = sum(1 for p in solid if px[p[0], p[1]] == make_logo.WHITE)
    inside_black = sum(1 for p in solid if px[p[0], p[1]] == make_logo.BLACK)
    # The joints are white lines cut through the black, not gaps between drawn
    # stones: inside the letterform there has to be a good deal of both.
    check(inside_white > 0 and inside_black > inside_white,
          "the letters are mostly black with white joints through them "
          "(%d black, %d white)" % (inside_black, inside_white))


def test_tiles_reassemble_into_the_rendered_image():
    print("test_tiles_reassemble_into_the_rendered_image")
    img = make_logo.render()
    tiles, cells = make_logo.cut_tiles(img)
    px = img.load()

    mismatch = 0
    for cy, row in enumerate(cells):
        for cx, ti in enumerate(row):
            tile = tiles[ti]
            for y in range(8):
                for x in range(0, 8, 2):
                    byte = tile[y * 4 + x // 2]
                    if px[cx * 8 + x, cy * 8 + y] != byte >> 4:
                        mismatch += 1
                    if px[cx * 8 + x + 1, cy * 8 + y] != (byte & 0x0f):
                        mismatch += 1

    check(mismatch == 0, "unpacking the tiles reproduces the render exactly")
    check(tiles[0] == bytes(32), "tile 0 is blank, so an empty cell has a target")
    check(len(tiles) < sum(len(r) for r in cells),
          "identical tiles were deduplicated")


def test_tile_table_fits_the_free_character_block():
    print("test_tile_table_fits_the_free_character_block")
    tiles, _cells = make_logo.cut_tiles(make_logo.render())
    # title_logo.cxx writes these at character number 64 upward; 640 is where
    # glyph_invert's scratch slots begin. Its static_assert says the same thing,
    # but a compile error is a slow way to learn the art grew too big.
    check(64 + len(tiles) <= 640,
          "the tiles fit below the SGL font's character block")


def test_generated_inc_matches_the_current_art():
    print("test_generated_inc_matches_the_current_art")
    if not make_logo.INC_PATH.exists():
        check(False, "title_logo.inc exists")
        return

    defines, tiles, cells = parse_inc(make_logo.INC_PATH.read_text())
    want_tiles, want_cells = make_logo.cut_tiles(make_logo.render())

    check(defines["LOGO_CELLS_W"] == make_logo.CELLS_W
          and defines["LOGO_CELLS_H"] == make_logo.CELLS_H,
          "the committed footprint matches the generator")
    check(defines["LOGO_TILE_N"] == len(tiles) == len(want_tiles),
          "LOGO_TILE_N agrees with the table it describes")
    check(defines["LOGO_PAL_FIRST"] == min(make_logo.PALETTE)
          and defines["LOGO_PAL_N"] == len(make_logo.PALETTE),
          "the palette window matches PALETTE")
    check([bytes(t) for t in tiles] == list(want_tiles),
          "the committed tiles are what the generator produces today")
    check(cells == want_cells,
          "the committed cell grid is what the generator produces today")


def test_rgb555_packs_opaque():
    print("test_rgb555_packs_opaque")
    check(make_logo.rgb555(0, 0, 0) == 0x8000, "black is opaque, not zero")
    check(make_logo.rgb555(255, 255, 255) == 0xFFFF, "white saturates")
    check(make_logo.rgb555(255, 0, 0) == 0x801F, "red is the low five bits")
    check(make_logo.rgb555(0, 0, 255) == 0xFC00, "blue is the high five bits")


def main():
    for t in (test_render_stays_inside_the_free_palette_entries,
              test_render_is_deterministic,
              test_counters_stay_open,
              test_stems_are_not_pinched,
              test_every_block_sits_on_the_course_ladder,
              test_the_keyline_backs_the_whole_outline,
              test_the_mortar_is_drawn_through_the_letters,
              test_tiles_reassemble_into_the_rendered_image,
              test_tile_table_fits_the_free_character_block,
              test_generated_inc_matches_the_current_art,
              test_rgb555_packs_opaque):
        try:
            t()
        except AssertionError:
            pass
    print()
    if FAILURES:
        print(f"FAILED {len(FAILURES)} check(s):")
        for f in FAILURES:
            print("  - " + f)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
