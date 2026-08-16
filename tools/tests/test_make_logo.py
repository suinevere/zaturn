#!/usr/bin/env python3
"""Host tests for tools/make_logo.py. Run: python tools/tests/test_make_logo.py

The logo is 4bpp art on the VDP2 text layer, which is a much narrower target
than a background TGA: it has to fit sixteen palette entries, three of which
belong to the console font and one of which is transparency, and it has to
survive being cut into 8x8 character patterns and reassembled by hardware. Those
failures are invisible in the preview PNG and only show up on a Saturn.

The rest of what is checked here is about the art being TRACED rather than
drawn. Its stones are sampled out of tools/assets/logo/zork-logo.png, so a
source window nudged a few pixels can walk off the edge of that picture and
hand back bare paper -- which draws as a letter quietly missing a leg, and
reads in the preview as a design decision.

check() raises as well as recording, matching test_make_tga.py: a failed
assertion has to fail under pytest, which is the gate this project runs, while
main() swallows the raise per test so a direct run still reaches every test.
"""
import re
import sys
from pathlib import Path

import numpy as np

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


def owner():
    """The word as a map of stone numbers, the way render() builds it."""
    strokes, _letters = make_logo.place_glyphs()
    return make_logo.renumber(strokes)


def test_the_reference_traces_into_stones():
    print("test_the_reference_traces_into_stones")
    ref, form = make_logo.trace()
    ids = np.unique(ref[ref > 0])

    # Peeling the black stroke off by distance recovers about a hundred stones.
    # Peeling it as "the ink that touches the background" instead takes the
    # stones that touch it too, and the count falls into the eighties -- which
    # is what quietly cost the Z a third of its masonry.
    check(len(ids) > 90, "the reference yields its stones (%d)" % len(ids))
    check(form.sum() > ref.astype(bool).sum(),
          "the letterform closes over the joints between them")


def test_every_stroke_samples_inside_the_reference():
    print("test_every_stroke_samples_inside_the_reference")
    # stroke_stones raises if a source window ran off the picture; this is the
    # test that the raise never fires for the sources actually in the table.
    try:
        make_logo.place_glyphs()
        ok, why = True, ""
    except ValueError as e:
        ok, why = False, str(e)
    check(ok, "no stroke samples bare paper (%s)" % (why or "all inside"))


def test_no_piece_of_a_letter_is_left_floating():
    print("test_no_piece_of_a_letter_is_left_floating")
    lab, n = make_logo.label(owner() > 0)
    sizes = sorted(int((lab == i).sum()) for i in range(1, n + 1))

    # Strokes are laid to overlap their neighbours by most of a course, so a
    # letter is one piece. Anything small and separate is a leg that lost its
    # grip -- the failure a bad source window produces, and the one that looks
    # deliberate in the preview.
    check(all(s > 150 for s in sizes),
          "nothing is stranded (smallest piece %d px)" % sizes[0])


def glyph_origin(ch):
    """Where a letter's local (0, 0) lands on the canvas."""
    widths = [make_logo.GLYPHS[c][0] for c in make_logo.WORD]
    pen = (make_logo.WIDTH - sum(widths)
           - make_logo.TRACK * (len(make_logo.WORD) - 1)) // 2
    for c, w in zip(make_logo.WORD, widths):
        if c == ch:
            return pen, make_logo.CAP_TOP
        pen += w + make_logo.TRACK
    raise KeyError(ch)


# One point well inside each counter, in glyph-local coordinates. Every one of
# them is only a few pixels wide once the channel and the stroke have both grown
# inwards -- four pixels a side, eight off the width -- so narrowing a letter by
# two closes one without changing anything the eye would flag in the preview.
# The A's went shut exactly that way when the letters were rebalanced to make
# room for the shaded faces, and it read as a solid wedge.
COUNTERS = (("A", 20, 68), ("U", 20, 34), ("R", 21, 24), ("N", 19, 68))


def test_counters_stay_open():
    print("test_counters_stay_open")
    px = np.array(make_logo.render())
    for ch, lx, ly in COUNTERS:
        ox, oy = glyph_origin(ch)
        check(px[oy + ly, ox + lx] == make_logo.TRANS,
              "the %s's counter is still open at local (%d, %d)" % (ch, lx, ly))


def test_every_letter_shows_a_shaded_side_face():
    print("test_every_letter_shows_a_shaded_side_face")
    from PIL import Image, ImageDraw

    px = np.array(make_logo.render())
    own = owner()

    # The side faces are what stop this reading as flat brickwork: solid black,
    # no joint anywhere inside them, one per letter. They are cut OUT of the
    # letter rather than added to it, so a polygon that drifts off the letterform
    # leaves no face at all and changes nothing else -- silent in the preview.
    for ch in make_logo.WORD:
        ox, oy = glyph_origin(ch)
        w = make_logo.GLYPHS[ch][0]
        cut = Image.new("L", (make_logo.WIDTH, make_logo.HEIGHT), 0)
        ImageDraw.Draw(cut).polygon(
            [(ox + x, oy + y) for x, y in make_logo.SHADE[ch]], fill=1)
        area = np.array(cut).astype(bool) & (own > 0)

        # More than one piece is fine and expected -- the Z's right edge is the
        # end of two separate bars, so its face is in two. What must hold is
        # that no piece of it has a joint drawn inside.
        ids = [i for i in np.unique(own[area]).tolist() if i]
        # Sized against the letter, not against the cap height: the hyphen is
        # a thirteenth of the Z's width and its face is small in proportion.
        want = max(24, w * make_logo.CAP_H // 40)
        check(bool(ids) and area.sum() >= want,
              "the %s has a face of a useful size (%d px, wanted %d, %d piece(s))"
              % (ch, int(area.sum()), want, len(ids)))
        # Eroded first: the joint where the face meets the masonry is drawn
        # half on each side, so the face's own rim is legitimately white. What
        # would be wrong is a joint running through the middle of it.
        face = make_logo.erode(np.isin(own, ids), make_logo.MORTAR_W)
        check(not (face & (px == make_logo.WHITE)).any(),
              "the %s's face carries no joints through it (%d px)"
              % (ch, int(face.sum())))


def test_strokes_are_not_pinched():
    print("test_strokes_are_not_pinched")
    solid = owner() > 0

    # A run being short is not enough on its own -- the last row of a sloped
    # corner is legitimately a pixel or two wide. A pinch is a thin run that
    # PERSISTS down the letter.
    thin = np.zeros_like(solid)
    for y in range(make_logo.HEIGHT):
        run = []
        for x in range(make_logo.WIDTH + 1):
            if x < make_logo.WIDTH and solid[y, x]:
                run.append(x)
                continue
            if 0 < len(run) < 8:
                thin[y, run] = True
            run = []

    worst = 0
    for x in range(make_logo.WIDTH):
        depth = 0
        for y in range(make_logo.HEIGHT):
            depth = depth + 1 if thin[y, x] else 0
            worst = max(worst, depth)
    check(worst < 6, "no stroke stays under 8px for six rows (worst %d)" % worst)


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
    check(make_logo.render().tobytes() == make_logo.render().tobytes(),
          "two renders of the same source produce the same pixels")


def test_the_stroke_backs_the_whole_channel():
    print("test_the_stroke_backs_the_whole_channel")
    px = np.array(make_logo.render())

    # White against a pale photograph is invisible, so no white pixel may face
    # transparency directly -- the black stroke has to be behind all of it. This
    # is the one failure that looks fine in the preview, which is drawn over a
    # mid grey, and disappears on the actual title screen.
    white = px == make_logo.WHITE
    leak = np.zeros_like(white)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        leak |= white & (make_logo.shift(px, dy, dx) == make_logo.TRANS)
    edge = white.copy()
    edge[1:-1, 1:-1] = False
    check(not leak.any() and not edge.any(),
          "no white pixel faces transparency (%d did)" % int(leak.sum()))


def test_the_mortar_is_drawn_through_the_letters():
    print("test_the_mortar_is_drawn_through_the_letters")
    px = np.array(make_logo.render())
    solid = owner() > 0
    used = set(px.ravel().tolist())

    check(used == {make_logo.TRANS, make_logo.BLACK, make_logo.WHITE},
          "the art is transparent, black and white and nothing else")

    inside_white = int((solid & (px == make_logo.WHITE)).sum())
    inside_black = int((solid & (px == make_logo.BLACK)).sum())
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
          "the tiles fit below the SGL font's character block (%d)" % len(tiles))


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


def test_footprint_matches_the_header():
    print("test_footprint_matches_the_header")
    hdr = (make_logo.REPO / "saturn" / "src" / "video" / "title_logo.h")
    text = hdr.read_text()
    cols = int(re.search(r"#define TITLE_LOGO_COLS (\d+)", text).group(1))
    rows = int(re.search(r"#define TITLE_LOGO_ROWS (\d+)", text).group(1))
    # title_logo.cxx static_asserts this too, but a cross-compile is a slow way
    # to find out, and the title screen's own row numbers depend on it.
    check((cols, rows) == (make_logo.CELLS_W, make_logo.CELLS_H),
          "title_logo.h says %dx%d and the generator makes %dx%d"
          % (cols, rows, make_logo.CELLS_W, make_logo.CELLS_H))


def test_rgb555_packs_opaque():
    print("test_rgb555_packs_opaque")
    check(make_logo.rgb555(0, 0, 0) == 0x8000, "black is opaque, not zero")
    check(make_logo.rgb555(255, 255, 255) == 0xFFFF, "white saturates")
    check(make_logo.rgb555(255, 0, 0) == 0x801F, "red is the low five bits")
    check(make_logo.rgb555(0, 0, 255) == 0xFC00, "blue is the high five bits")


def main():
    for t in (test_the_reference_traces_into_stones,
              test_every_stroke_samples_inside_the_reference,
              test_no_piece_of_a_letter_is_left_floating,
              test_counters_stay_open,
              test_every_letter_shows_a_shaded_side_face,
              test_strokes_are_not_pinched,
              test_render_stays_inside_the_free_palette_entries,
              test_render_is_deterministic,
              test_the_stroke_backs_the_whole_channel,
              test_the_mortar_is_drawn_through_the_letters,
              test_tiles_reassemble_into_the_rendered_image,
              test_tile_table_fits_the_free_character_block,
              test_generated_inc_matches_the_current_art,
              test_footprint_matches_the_header,
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
