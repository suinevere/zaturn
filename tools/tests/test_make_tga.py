#!/usr/bin/env python3
"""Host tests for tools/make_tga.py. Run: python tools/tests/test_make_tga.py

check() raises as well as recording, so a failed assertion fails the test under
pytest -- which is the gate this project actually runs. Recording alone made
pytest blind to everything except exceptions. main() swallows the raise per test
so a direct run still reaches every test rather than stopping at the first.

The tree shape under test is the per-game one make_tga.py now walks
(tools/assets/png/<GAME>/<SCENE>/*.png), not the retired per-mood one. Tests
that need a real, known game or scene stem use ZORK1/ADVENT and FOREST/CAVE --
the corpus these come from (gen_scene_tables.GAMES, scene_vocab.SCENES) is the
real repo data, not a fixture, since convert_game_tree validates both against
it and there is no injection seam.
"""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PIL import Image

import make_tga

FAILURES = []


def check(cond, label):
    print(("  ok   " if cond else "  FAIL ") + label)
    if not cond:
        FAILURES.append(label)
        raise AssertionError(label)


def gradient(w=make_tga.WIDTH, h=make_tga.HEIGHT):
    """A deterministic multi-hue gradient -- quantizing a flat color is a weak test."""
    im = Image.new("RGB", (w, h))
    im.putdata([((x * 7) % 256, (y * 5) % 256, ((x + y) * 3) % 256)
                for y in range(h) for x in range(w)])
    return im


def make_png(path, w=make_tga.WIDTH, h=make_tga.HEIGHT):
    gradient(w, h).save(path)


def test_encode_tga_structure():
    print("test_encode_tga_structure")
    blob = make_tga.encode_tga(gradient())

    idlen, cmaptype, imgtype = blob[0], blob[1], blob[2]
    cmaplen = blob[5] | (blob[6] << 8)
    cmapdepth = blob[7]
    width = blob[12] | (blob[13] << 8)
    height = blob[14] | (blob[15] << 8)
    bpp, desc = blob[16], blob[17]

    check(idlen == 0, "no image ID field")
    check(cmaptype == 1, "colormap present")
    check(imgtype == 1, "uncompressed paletted")
    check(cmapdepth == 24, "24-bit colormap entries")
    check(width == 320 and height == 224, "320x224")
    check(bpp == 8, "8bpp indices")
    check(desc == 0x00, "bottom-left origin")
    check(cmaplen <= 256, "colormap fits 256 entries")

    body = blob[18 + cmaplen * 3:]
    check(len(body) == 320 * 224, "body is exactly one byte per pixel")
    check(len(blob) == 18 + cmaplen * 3 + 320 * 224, "no trailing bytes")
    check(0 not in body, "index 0 never appears in pixel data")
    check(max(body) < cmaplen, "every index is inside the colormap")


def four_quadrants(w=make_tga.WIDTH, h=make_tga.HEIGHT):
    """Four exactly-distinguishable quadrant colors -- quantize losslessly so
    decoded pixels can be checked for exact RGB equality, no tolerance."""
    im = Image.new("RGB", (w, h))
    red, green, blue, white = (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)

    def color_at(x, y):
        left = x < w // 2
        top = y < h // 2
        if top and left:
            return red
        if top and not left:
            return green
        if not top and left:
            return blue
        return white

    im.putdata([color_at(x, y) for y in range(h) for x in range(w)])
    return im


def test_encode_tga_pixel_roundtrip():
    print("test_encode_tga_pixel_roundtrip")
    w, h = make_tga.WIDTH, make_tga.HEIGHT
    blob = make_tga.encode_tga(four_quadrants(w, h))

    cmaplen = blob[5] | (blob[6] << 8)
    cmap_start = 18
    body_start = cmap_start + cmaplen * 3

    def colormap_entry(index):
        off = cmap_start + index * 3
        b, g, r = blob[off], blob[off + 1], blob[off + 2]
        return (r, g, b)

    def pixel_at(x, y):
        row = h - 1 - y
        idx = blob[body_start + row * w + x]
        return colormap_entry(idx)

    check(pixel_at(80, 56) == (255, 0, 0), "top-left quadrant decodes to pure red")
    check(pixel_at(240, 56) == (0, 255, 0), "top-right quadrant decodes to pure green")
    check(pixel_at(80, 168) == (0, 0, 255), "bottom-left quadrant decodes to pure blue")
    check(pixel_at(240, 168) == (255, 255, 255), "bottom-right quadrant decodes to pure white")


def test_convert_game_tree_walks_scenes_in_vocabulary_order():
    print("test_convert_game_tree_walks_scenes_in_vocabulary_order")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        (src / "ZORK1" / "CAVE").mkdir(parents=True)
        make_png(src / "ZORK1" / "FOREST" / "F1.PNG")
        make_png(src / "ZORK1" / "FOREST" / "F2.PNG")
        make_png(src / "ZORK1" / "CAVE" / "C1.PNG")
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {"ZORK1": {"FOREST": 2, "CAVE": 1}},
              "one game, two scenes, counted separately")
        check((dst / "ZORK1" / "01.TGA").exists(), "first FOREST source claims 01.TGA")
        check((dst / "ZORK1" / "02.TGA").exists(), "second FOREST source claims 02.TGA")
        check((dst / "ZORK1" / "03.TGA").exists(),
              "CAVE (later in scene_vocab.SCENES) claims the next index")
        check(not (dst / "FOREST").exists(), "output is not flattened to the root")


def test_convert_game_tree_skips_offsize_but_continues():
    print("test_convert_game_tree_skips_offsize_but_continues")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        make_png(src / "ZORK1" / "FOREST" / "ANCIENT.PNG", w=640, h=480)
        make_png(src / "ZORK1" / "FOREST" / "CLIFF.PNG")
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {"ZORK1": {"FOREST": 1}}, "only the correctly-sized image converted")
        check((dst / "ZORK1" / "01.TGA").exists(), "the surviving image claims 01.TGA")
        check(not (dst / "ZORK1" / "02.TGA").exists(), "a skipped image leaves no gap")


def test_convert_game_tree_skips_unreadable_source():
    print("test_convert_game_tree_skips_unreadable_source")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        make_png(src / "ZORK1" / "FOREST" / "CLIFF.PNG")
        (src / "ZORK1" / "FOREST" / "BROKEN.png").write_bytes(b"not a real png")
        make_tga.convert_game_tree(src, dst)

        check((dst / "ZORK1" / "01.TGA").exists(), "valid file produced a TGA")
        check(not (dst / "ZORK1" / "02.TGA").exists(), "unreadable source produced no TGA")


def test_convert_game_tree_skips_unknown_game_folder():
    print("test_convert_game_tree_skips_unknown_game_folder")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        (src / "NOTAGAME" / "FOREST").mkdir(parents=True)
        make_png(src / "ZORK1" / "FOREST" / "F1.PNG")
        make_png(src / "NOTAGAME" / "FOREST" / "F1.PNG")
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {"ZORK1": {"FOREST": 1}}, "the unknown game is skipped, not converted")
        check(not (dst / "NOTAGAME").exists(), "no output folder for an unknown game")


def test_convert_game_tree_skips_unknown_scene_folder():
    print("test_convert_game_tree_skips_unknown_scene_folder")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        (src / "ZORK1" / "NOTASCENE").mkdir(parents=True)
        make_png(src / "ZORK1" / "FOREST" / "F1.PNG")
        make_png(src / "ZORK1" / "NOTASCENE" / "X1.PNG")
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {"ZORK1": {"FOREST": 1}}, "the unknown scene is skipped, not converted")
        check(sum(counts["ZORK1"].values()) == 1, "the typo'd scene contributed no pictures")


def test_convert_game_tree_skips_title_folder():
    print("test_convert_game_tree_skips_title_folder")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "TITLE").mkdir(parents=True)
        make_png(src / "TITLE" / "T1.PNG")
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {}, "TITLE is not a game -- convert_title owns it")
        check(not (dst / "TITLE").exists(), "convert_game_tree writes nothing for TITLE")


def test_convert_game_tree_missing_src_root_is_a_noop():
    print("test_convert_game_tree_missing_src_root_is_a_noop")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {}, "a missing source tree converts nothing")
        check(not dst.exists(), "no destination tree is created")


def test_convert_game_tree_clears_stale_tgas():
    print("test_convert_game_tree_clears_stale_tgas")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        make_png(src / "ZORK1" / "FOREST" / "F1.PNG")
        make_png(src / "ZORK1" / "FOREST" / "F2.PNG")
        make_png(src / "ZORK1" / "FOREST" / "F3.PNG")
        make_tga.convert_game_tree(src, dst)
        check((dst / "ZORK1" / "03.TGA").exists(), "all three sources convert on the first run")

        (src / "ZORK1" / "FOREST" / "F2.PNG").unlink()
        (src / "ZORK1" / "FOREST" / "F3.PNG").unlink()
        counts = make_tga.convert_game_tree(src, dst)

        check(counts == {"ZORK1": {"FOREST": 1}}, "only the remaining source converts")
        check(not (dst / "ZORK1" / "02.TGA").exists(), "a stale TGA from the first run is removed")
        check(not (dst / "ZORK1" / "03.TGA").exists(), "a stale TGA from the first run is removed")


def test_convert_game_tree_caps_at_99_across_scenes_not_per_scene():
    print("test_convert_game_tree_caps_at_99_across_scenes_not_per_scene")
    import contextlib
    import io

    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "ZORK1" / "FOREST").mkdir(parents=True)
        (src / "ZORK1" / "CAVE").mkdir(parents=True)
        for i in range(70):
            make_png(src / "ZORK1" / "FOREST" / f"F{i:03d}.png")
        for i in range(50):
            make_png(src / "ZORK1" / "CAVE" / f"C{i:03d}.png")

        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            counts = make_tga.convert_game_tree(src, dst)

        check(counts["ZORK1"] == {"FOREST": 70, "CAVE": 29},
              "all 70 FOREST pictures convert, only 29 of 50 CAVE fit before 99")
        check(sum(counts["ZORK1"].values()) == 99, "counts sum to exactly the game's cap")
        check("ignoring" in buf.getvalue(), "warning fires past 99 pictures in one game")
        made = list((dst / "ZORK1").glob("*.TGA"))
        check(len(made) == 99, "exactly 99 files were actually written, not 120")


def test_convert_title_writes_a_flat_gapless_run():
    print("test_convert_title_writes_a_flat_gapless_run")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "TITLE").mkdir(parents=True)
        make_png(src / "TITLE" / "SPLASH1.PNG")
        make_png(src / "TITLE" / "SPLASH2.PNG")
        n = make_tga.convert_title(src, dst)

        check(n == 2, "convert_title returns the count converted")
        check((dst / "TITLE" / "01.TGA").exists(), "first source claims 01.TGA")
        check((dst / "TITLE" / "02.TGA").exists(), "second source claims 02.TGA")


def test_convert_title_missing_folder_converts_nothing():
    print("test_convert_title_missing_folder_converts_nothing")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        n = make_tga.convert_title(src, dst)

        check(n == 0, "no TITLE source folder converts zero pictures")
        check(not (dst / "TITLE").exists(), "no TITLE output folder is created")


def test_convert_title_clears_stale_tgas():
    print("test_convert_title_clears_stale_tgas")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "TITLE").mkdir(parents=True)
        make_png(src / "TITLE" / "OLDNAME.png")
        make_tga.convert_title(src, dst)
        check((dst / "TITLE" / "01.TGA").exists(), "the boot splash converts on the first run")

        (src / "TITLE" / "OLDNAME.png").unlink()
        make_png(src / "TITLE" / "NEWNAME.png")
        make_tga.convert_title(src, dst)

        check((dst / "TITLE" / "01.TGA").exists(), "the replacement still claims 01.TGA")
        check(not (dst / "TITLE" / "02.TGA").exists(), "a renamed/deleted source leaves no orphan")


def test_write_scene_inc_emits_bases_that_follow_the_scenes_before_them():
    print("test_write_scene_inc_emits_bases_that_follow_the_scenes_before_them")
    import scene_vocab as vocab

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "game_scenes.inc"
        make_tga.write_scene_inc({"ZORK1": {"FOREST": 3, "CAVE": 2}}, out)
        text = out.read_text()

        forest_i = vocab.SCENES.index("FOREST")
        cave_i = vocab.SCENES.index("CAVE")
        check(forest_i < cave_i, "FOREST precedes CAVE in scene_vocab.SCENES")
        check("{0,3}" in text, "FOREST's base is 0")
        check("{3,2}" in text, "CAVE's base follows FOREST's count")
        check('"ZORK1"' in text, "ZORK1 is one of the real, known games")
        check("GAME_DIR[GAME_N]" in text, "the generated table names GAME_N")


def test_write_title_inc_emits_the_define():
    print("test_write_title_inc_emits_the_define")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "title_art.inc"
        make_tga.write_title_inc(7, out)
        text = out.read_text()

        check("#define TITLE_ART_N 7" in text, "TITLE_ART_N carries the given count")
        check("GAME_DIR[GAME_N]" not in text, "TITLE never gets a row in GAME_DIR")
        check("GAME_SCENE[GAME_N]" not in text, "TITLE never gets a row in GAME_SCENE")


def main():
    for t in (test_encode_tga_structure,
              test_encode_tga_pixel_roundtrip,
              test_convert_game_tree_walks_scenes_in_vocabulary_order,
              test_convert_game_tree_skips_offsize_but_continues,
              test_convert_game_tree_skips_unreadable_source,
              test_convert_game_tree_skips_unknown_game_folder,
              test_convert_game_tree_skips_unknown_scene_folder,
              test_convert_game_tree_skips_title_folder,
              test_convert_game_tree_missing_src_root_is_a_noop,
              test_convert_game_tree_clears_stale_tgas,
              test_convert_game_tree_caps_at_99_across_scenes_not_per_scene,
              test_convert_title_writes_a_flat_gapless_run,
              test_convert_title_missing_folder_converts_nothing,
              test_convert_title_clears_stale_tgas,
              test_write_scene_inc_emits_bases_that_follow_the_scenes_before_them,
              test_write_title_inc_emits_the_define):
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
