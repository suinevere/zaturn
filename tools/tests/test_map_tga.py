#!/usr/bin/env python3
"""The map sheets: the one TGA shape the console reads, and the genre that picks one."""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import game_genre as genre_vocab
import gen_map_tga
import pres_store as store

TGA = ROOT / "saturn" / "cd" / "data" / "TGA"
INC = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"


class MapTgaTest(unittest.TestCase):
    def test_every_sheet_is_on_the_disc(self):
        for name in genre_vocab.MAP_FILES:
            self.assertTrue((TGA / name).is_file(), f"{name} is not in cd/data/TGA")

    def test_every_sheet_is_in_the_format_the_console_reads(self):
        """tga_decode takes uncompressed colour-mapped 8bpp and nothing else.
        It refuses anything else silently -- the map page just draws its back
        colour -- which is the hardest kind of wrong to notice."""
        for name in genre_vocab.MAP_FILES:
            d = gen_map_tga.describe(TGA / name)
            self.assertTrue(gen_map_tga.acceptable(d), f"{name} would not decode: {d}")

    def test_every_sheet_is_the_size_of_the_screen(self):
        for name in genre_vocab.MAP_FILES:
            d = gen_map_tga.describe(TGA / name)
            self.assertEqual((d["w"], d["h"]), (320, 240), f"{name} is {d['w']}x{d['h']}")

    def test_the_header_and_palette_fit_one_sector(self):
        """tga_decode reads one sector and refuses if the pixels do not start
        inside it."""
        for name in genre_vocab.MAP_FILES:
            d = gen_map_tga.describe(TGA / name)
            pixoff = 18 + d["idlen"] + d["cmaplen"] * (d["cmapbits"] // 8)
            self.assertLessEqual(pixoff, 2048, f"{name} starts its pixels at {pixoff}")


class MapChoiceTest(unittest.TestCase):
    def test_every_genre_names_a_sheet(self):
        for name, _note in genre_vocab.GENRES:
            self.assertIn(name, genre_vocab.GENRE_MAP, f"{name} names no sheet")
            self.assertLess(genre_vocab.GENRE_MAP[name], len(genre_vocab.MAP_FILES))

    def test_an_unfiled_game_falls_to_the_default(self):
        self.assertEqual(genre_vocab.map_bg("NOSUCHGAME"), 0)

    def test_every_game_on_the_disc_resolves_to_a_real_sheet(self):
        for stem in store.games() + [store.ZORK1_STEM]:
            i = genre_vocab.map_bg(stem)
            self.assertTrue((TGA / genre_vocab.MAP_FILES[i]).is_file(), stem)

    def test_the_generated_table_carries_the_same_choice(self):
        """The runtime has no other way to know what kind of story it runs."""
        text = INC.read_text(encoding="utf-8")
        self.assertIn(f"#define PRES_MAP_BG_N {len(genre_vocab.MAP_FILES)}", text)
        for name in genre_vocab.MAP_FILES:
            self.assertIn(f'    "{name}",', text)
        rows = re.findall(r'\{\s*(\d+), "(\d+)", GAME_PRES_(\w+), (\d+) \}', text)
        self.assertEqual(len(rows), 31, "not every game has a row")
        for _rel, _ser, stem, got in rows:
            self.assertEqual(int(got), genre_vocab.map_bg(stem),
                             f"{stem} names sheet {got} in the table")


if __name__ == "__main__":
    unittest.main(verbosity=2)
