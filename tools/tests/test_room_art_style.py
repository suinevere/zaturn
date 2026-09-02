#!/usr/bin/env python3
"""Putting a new picture into the house style, measured against a real frame."""
import pathlib
import sys
import unittest

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "analysis"))

import cgl_encode
import room_art_style as style
import zork_cgl

CONTROL_ROOM = 51          # BDAM_00, the reference the first plate was graded to


def bright_test_image():
    """A deliberately wrong input: bright, saturated, full-range."""
    x = np.linspace(0, 255, 512, dtype=np.float64)
    g = np.stack(np.meshgrid(x, np.linspace(60, 255, 384)), axis=-1)
    rgb = np.stack([g[..., 0], g[..., 1], np.full(g.shape[:2], 200.0)], axis=-1)
    return Image.fromarray(rgb.astype(np.uint8))


class ReferenceTest(unittest.TestCase):
    def test_a_frame_can_be_fetched_by_its_index(self):
        rgb, ncol = style.reference(CONTROL_ROOM)
        self.assertEqual(rgb.shape, (240, 320, 3))
        self.assertGreater(ncol, 4)

    def test_the_originals_are_posterised_not_full_colour(self):
        """The finding this whole module rests on: the plates hold a couple of
        dozen colours, not 256, and a 255-colour picture beside them is
        instantly the new one."""
        counts = [style.reference(i)[1] for i in (51, 33, 20, 3, 70)]
        for n in counts:
            self.assertLess(n, 64, f"a frame with {n} colours is not posterised")

    def test_the_originals_live_in_a_narrow_dark_band(self):
        ref, _n = style.reference(CONTROL_ROOM)
        lum = style.luminance(ref)
        self.assertLess(lum.mean(), 60, "the reference is not dark")
        self.assertLess(lum.max(), 200, "the reference has full-range highlights")

    def test_every_frame_index_resolves(self):
        table = style.frame_table()
        self.assertEqual(sorted(table)[0], 1)
        self.assertGreaterEqual(len(table), 74)


class StyliseTest(unittest.TestCase):
    def setUp(self):
        self.ref, self.ncol = style.reference(CONTROL_ROOM)
        self.out, self.pal = style.stylise(bright_test_image(), CONTROL_ROOM)

    def test_the_result_is_the_screen_size(self):
        self.assertEqual(self.out.size, (320, 240))

    def test_the_result_holds_the_references_colour_count(self):
        used = np.unique(np.asarray(self.out))
        self.assertLessEqual(used.size, self.ncol)

    def test_a_bright_input_comes_out_as_dark_as_the_reference(self):
        """Normalising to full range was the first attempt and produced a
        mid-grey picture: right hue, wrong medium."""
        rgb = np.array(self.pal, dtype=np.uint8)[np.asarray(self.out)]
        got, want = style.luminance(rgb).mean(), style.luminance(self.ref).mean()
        self.assertLess(abs(got - want), 12, f"mean luma {got:.0f} against {want:.0f}")

    def test_the_result_is_close_to_monochromatic(self):
        rgb = np.array(self.pal, dtype=np.uint8)[np.asarray(self.out)].astype(float)
        hue_span = []
        for c in np.unique(rgb.reshape(-1, 3), axis=0):
            mx, mn = c.max(), c.min()
            if mx - mn > 12:
                hue_span.append(np.argmax(c))
        self.assertLessEqual(len(set(hue_span)), 2,
                             "a styled plate should sit on one or two hues")

    def test_the_styled_frame_encodes_and_decodes(self):
        """The whole point: it has to survive the trip onto the disc."""
        rec = cgl_encode.record(self.pal, self.out.tobytes())
        back = list(zork_cgl.records(rec))
        self.assertEqual(len(back), 1)
        self.assertEqual(back[0][3], self.out.tobytes())

    def test_the_styled_frame_is_no_bigger_than_the_one_it_replaces(self):
        """Posterising to the reference's colour count is what buys this; at
        255 colours the record came out more than twice the size."""
        rec = cgl_encode.record(self.pal, self.out.tobytes())
        self.assertLess(len(rec), 40000)


class ToneTest(unittest.TestCase):
    def test_matching_reproduces_the_reference_distribution(self):
        rng = np.random.default_rng(7)
        src = rng.uniform(0, 255, (64, 64))
        ref = rng.normal(20, 8, (64, 64)).clip(0, 255)
        got = style.match_tone(src, ref)
        self.assertLess(abs(got.mean() - ref.mean()), 2)
        self.assertLess(abs(np.median(got) - np.median(ref)), 3)

    def test_matching_preserves_order(self):
        """A rank mapping may darken everything; it may never swap two pixels'
        relative brightness, or the picture stops being the picture."""
        src = np.array([[10.0, 40.0], [80.0, 200.0]])
        ref = np.array([[0.0, 5.0], [9.0, 60.0]])
        got = style.match_tone(src, ref)
        self.assertTrue(np.all(np.argsort(got.ravel()) == np.argsort(src.ravel())))


if __name__ == "__main__":
    unittest.main(verbosity=2)
