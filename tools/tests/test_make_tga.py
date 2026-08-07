#!/usr/bin/env python3
"""Host tests for tools/make_tga.py. Run: python tools/tests/test_make_tga.py

check() raises as well as recording, so a failed assertion fails the test under
pytest -- which is the gate this project actually runs. Recording alone made
pytest blind to everything except exceptions. main() swallows the raise per test
so a direct run still reaches every test rather than stopping at the first.
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
    """A deterministic multi-hue gradient — quantizing a flat color is a weak test."""
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


def test_convert_tree_root_level_naming_and_case_insensitivity():
    print("test_convert_tree_root_level_naming_and_case_insensitivity")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "SUINE.PNG")
        make_png(src / "cmplab.png")
        counts = make_tga.convert_tree(src, dst)

        check(counts == {}, "root-level sources are not counted toward any mood")
        check((dst / "SUINE.TGA").exists(), "uppercase source -> SUINE.TGA")
        check((dst / "CMPLAB.TGA").exists(), "lowercase source -> CMPLAB.TGA")


def test_convert_tree_skips_offsize_but_continues():
    print("test_convert_tree_skips_offsize_but_continues")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "WILDER").mkdir(parents=True)
        make_png(src / "WILDER" / "ANCIENT.PNG", w=640, h=480)
        make_png(src / "WILDER" / "CLIFF.PNG")
        counts = make_tga.convert_tree(src, dst)

        check(counts == {"WILDER": 1}, "only the correctly-sized image converted")
        check((dst / "WILDER" / "01.TGA").exists(), "the surviving image claims 01.TGA")
        check(not (dst / "WILDER" / "02.TGA").exists(), "a skipped image leaves no gap")


def test_convert_tree_root_level_skips_long_stems():
    print("test_convert_tree_root_level_skips_long_stems")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "TYPEWRTR.png")
        make_png(src / "TOOLONGNAME.png")
        make_tga.convert_tree(src, dst)

        check((dst / "TYPEWRTR.TGA").exists(), "exactly 8 characters is allowed")
        check(not (dst / "TOOLONGNAME.TGA").exists(), "over 8 characters is skipped")


def test_convert_tree_warns_past_the_per_mood_cap():
    print("test_convert_tree_warns_past_the_per_mood_cap")
    import contextlib
    import io

    def run_with(n):
        with tempfile.TemporaryDirectory() as td:
            src, dst = Path(td) / "png", Path(td) / "tga"
            (src / "WILDER").mkdir(parents=True)
            for i in range(n):
                make_png(src / "WILDER" / f"BG{i:03d}.png")
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                counts = make_tga.convert_tree(src, dst)
            return counts, buf.getvalue()

    counts99, out99 = run_with(99)
    check(counts99 == {"WILDER": 99}, "exactly 99 images all convert")
    check("ignoring" not in out99, "no warning at exactly 99 images")

    counts100, out100 = run_with(100)
    check(counts100 == {"WILDER": 99}, "the 100th image is not counted")
    check("ignoring" in out100, "warning fires past 99 images in one mood")


def test_convert_tree_skips_unreadable_source():
    print("test_convert_tree_skips_unreadable_source")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "CLIFF.PNG")
        (src / "BROKEN.png").write_bytes(b"not a real png, just garbage bytes")

        make_tga.convert_tree(src, dst)

        check((dst / "CLIFF.TGA").exists(), "valid file produced a TGA")
        check(not (dst / "BROKEN.TGA").exists(), "unreadable source produced no TGA")


def test_convert_tree_walks_mood_folders_and_root():
    print("test_convert_tree_walks_mood_folders_and_root")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "WILDER").mkdir(parents=True)
        (src / "TOWN").mkdir(parents=True)
        make_png(src / "WILDER" / "WILDER1.PNG")
        make_png(src / "WILDER" / "WILDER2.PNG")
        make_png(src / "TOWN" / "TOWN1.PNG")
        make_png(src / "SUINE.PNG")
        counts = make_tga.convert_tree(src, dst)

        check(counts == {"WILDER": 2, "TOWN": 1}, "every mood folder is walked")
        check((dst / "WILDER" / "01.TGA").exists(), "first WILDER source claims 01.TGA")
        check((dst / "WILDER" / "02.TGA").exists(), "second WILDER source claims 02.TGA")
        check((dst / "TOWN" / "01.TGA").exists(), "TOWN gets its own 01.TGA")
        check((dst / "SUINE.TGA").exists(), "root-level source becomes the boot splash")
        check(not (dst / "WILDER1.TGA").exists(), "output is not flattened to the root")


def test_convert_tree_root_level_accepts_jpeg_sources():
    print("test_convert_tree_root_level_accepts_jpeg_sources")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        gradient().save(src / "SCIFI3.jpg")
        make_tga.convert_tree(src, dst)

        check((dst / "SCIFI3.TGA").exists(), "JPEG source -> SCIFI3.TGA")


def test_convert_tree_skips_unknown_mood_folder():
    print("test_convert_tree_skips_unknown_mood_folder")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "WILDER").mkdir(parents=True)
        (src / "SPOOKY").mkdir(parents=True)
        make_png(src / "WILDER" / "WILDER1.PNG")
        make_png(src / "SPOOKY" / "SPOOKY1.PNG")
        counts = make_tga.convert_tree(src, dst)

        check(counts == {"WILDER": 1}, "the typo'd mood is skipped, not converted")
        check(not (dst / "SPOOKY").exists(), "no output folder for an unknown mood")


def test_convert_tree_missing_src_root_is_a_noop():
    print("test_convert_tree_missing_src_root_is_a_noop")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        counts = make_tga.convert_tree(src, dst)

        check(counts == {}, "a missing source tree converts nothing")
        check(not dst.exists(), "no destination tree is created")


def test_convert_tree_clears_stale_mood_tgas():
    print("test_convert_tree_clears_stale_mood_tgas")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        (src / "WILDER").mkdir(parents=True)
        make_png(src / "WILDER" / "WILDER1.PNG")
        make_png(src / "WILDER" / "WILDER2.PNG")
        make_png(src / "WILDER" / "WILDER3.PNG")
        make_tga.convert_tree(src, dst)
        check((dst / "WILDER" / "03.TGA").exists(), "all three sources convert on the first run")

        (src / "WILDER" / "WILDER2.PNG").unlink()
        (src / "WILDER" / "WILDER3.PNG").unlink()
        counts = make_tga.convert_tree(src, dst)

        check(counts == {"WILDER": 1}, "only the remaining source converts")
        check(not (dst / "WILDER" / "02.TGA").exists(), "a stale TGA from the first run is removed")
        check(not (dst / "WILDER" / "03.TGA").exists(), "a stale TGA from the first run is removed")


def test_convert_tree_clears_stale_root_level_tgas():
    print("test_convert_tree_clears_stale_root_level_tgas")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "png", Path(td) / "tga"
        src.mkdir()
        make_png(src / "SUINE.PNG")
        make_tga.convert_tree(src, dst)
        check((dst / "SUINE.TGA").exists(), "the boot splash converts on the first run")

        (src / "SUINE.PNG").unlink()
        make_tga.convert_tree(src, dst)

        check(not (dst / "SUINE.TGA").exists(), "a renamed or deleted source leaves no orphan TGA")


def main():
    for t in (test_encode_tga_structure,
              test_encode_tga_pixel_roundtrip,
              test_convert_tree_root_level_naming_and_case_insensitivity,
              test_convert_tree_walks_mood_folders_and_root,
              test_convert_tree_skips_unknown_mood_folder,
              test_convert_tree_root_level_accepts_jpeg_sources,
              test_convert_tree_skips_offsize_but_continues,
              test_convert_tree_root_level_skips_long_stems,
              test_convert_tree_warns_past_the_per_mood_cap,
              test_convert_tree_skips_unreadable_source,
              test_convert_tree_missing_src_root_is_a_noop,
              test_convert_tree_clears_stale_mood_tgas,
              test_convert_tree_clears_stale_root_level_tgas):
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
