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
        # lift=0 on purpose: these check that the TONE of the reference is
        # reproduced, which is a separate question from how bright the finished
        # plate is aimed. Left to derive its own lift, a dark reference is
        # deliberately opened up towards TARGET_MEAN and the tone check would be
        # measuring the aim instead.
        self.out, self.pal = style.stylise(bright_test_image(), CONTROL_ROOM, lift=0)

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


class LiftTest(unittest.TestCase):
    """The ramp can only return colours the reference already holds, and the
    reference's brightest is dark -- so without this a plate is capped at about
    a quarter of the range the hardware can show, however well it is matched."""

    REF = np.zeros((4, 4, 3), dtype=np.uint8)

    def setUp(self):
        self.REF = np.zeros((4, 4, 3), dtype=np.uint8)
        self.REF[0, 0] = (66, 82, 107)

    def test_no_lift_is_no_change(self):
        self.assertEqual(style.lift_gain(self.REF, 0.0), 1.0)

    def test_a_full_lift_puts_the_brightest_colour_at_full_scale(self):
        top = style.luminance(self.REF).max()
        self.assertAlmostEqual(top * style.lift_gain(self.REF, 1.0), 255.0, places=4)

    def test_a_black_reference_cannot_be_lifted(self):
        """Dividing by its maximum would be a division by zero, and there is no
        hue in it to preserve anyway."""
        self.assertEqual(style.lift_gain(np.zeros((4, 4, 3), np.uint8), 1.0), 1.0)

    def test_lifting_leaves_hue_and_the_black_point_alone(self):
        g = style.lift_gain(self.REF, 0.5)
        r, b = 66 * g, 107 * g
        self.assertAlmostEqual(r / b, 66 / 107, places=6)
        self.assertEqual(0 * g, 0)


class TargetTest(unittest.TestCase):
    """How bright a finished plate is aimed, which is one number and had been
    copied into every prompt sheet and every manifest entry -- so moving it left
    everything already drawn at the old brightness with nothing to say so."""

    def test_the_lift_aims_at_the_target(self):
        got, _pal = style.stylise(bright_test_image(), CONTROL_ROOM)
        rgb = np.array(_pal, dtype=np.uint8)[np.asarray(got)]
        mean = style.luminance(rgb).mean()
        self.assertLess(abs(mean - style.TARGET_MEAN), 6,
                        f"aimed at {style.TARGET_MEAN}, landed at {mean:.1f}")

    def test_a_reference_too_dark_to_reach_the_target_is_left_at_full_lift(self):
        """The dark blue plates are dark because their reference is dark and the
        lift is already at its ceiling; lowering the target must not push them
        further down, and cannot."""
        ref, _n = style.reference(CONTROL_ROOM)
        lum = style.luminance(ref)
        if lum.mean() * (255.0 / lum.max()) > style.TARGET_MEAN:
            self.skipTest("this reference can reach the target")
        self.assertEqual(style.lift_for(CONTROL_ROOM), 1.0)


class MarginTest(unittest.TestCase):
    """Diffusion models sign their work, and a signature is text -- the one
    thing a room background must not carry, because the game draws its own over
    it. Two of the first hundred plates came back signed along the bottom."""

    def plate(self, mark):
        """A flat grey plate, optionally with a bright mark on the bottom edge
        where a signature lands."""
        a = np.full((384, 512, 3), 90, dtype=np.uint8)
        if mark:
            a[366:383, 380:500] = 255
        return Image.fromarray(a)

    def test_a_mark_on_the_bottom_edge_is_cropped_away(self):
        got = np.asarray(style.stylise(self.plate(True), 1)[0].convert("RGB"))
        clean = np.asarray(style.stylise(self.plate(False), 1)[0].convert("RGB"))
        self.assertEqual(got.tobytes(), clean.tobytes(),
                         "the signature survived the margin")

    def test_without_the_margin_the_mark_survives(self):
        """Proves the test above is testing the margin and not the posterising."""
        got = np.asarray(style.stylise(self.plate(True), 1, margin=0)[0].convert("RGB"))
        clean = np.asarray(style.stylise(self.plate(False), 1, margin=0)[0].convert("RGB"))
        self.assertNotEqual(got.tobytes(), clean.tobytes())

    def test_the_margin_keeps_the_screen_aspect(self):
        q, _pal = style.stylise(self.plate(False), 1)
        self.assertEqual(q.size, (style.WIDTH, style.HEIGHT))


class ToneTest(unittest.TestCase):
    def test_matching_reproduces_the_reference_distribution(self):
        rng = np.random.default_rng(7)
        src = rng.uniform(0, 255, (64, 64))
        ref = rng.normal(20, 8, (64, 64)).clip(0, 255)
        got = style.match_tone(src, ref)
        self.assertLess(abs(got.mean() - ref.mean()), 2)
        self.assertLess(abs(np.median(got) - np.median(ref)), 3)

    def test_a_tied_maximum_still_reaches_the_reference_maximum(self):
        """The two tests above use continuous noise, where no two pixels share
        a value, which is why neither ever saw this. A rendered picture is
        nothing like that: 461 of the first generated plate's pixels sat at its
        maximum, and a rank that counted only strictly-darker pixels sent every
        one of them to the 99.4th percentile of the reference. Everything the
        reference had above that never appeared, so the brightest thing in the
        picture came out at three quarters of the brightness it was graded
        against."""
        src = np.concatenate([np.linspace(0, 90, 500), np.full(500, 100.0)])
        ref = np.linspace(0, 68, 1000)
        got = style.match_tone(src, ref)
        self.assertAlmostEqual(got.max(), ref.max(), places=6)

    def test_a_flat_region_maps_to_one_value(self):
        """Ties resolving to the top of their run is a choice; ties resolving
        to DIFFERENT values would be a worse bug than the one above, because a
        flat wall would come back with a gradient across it."""
        src = np.array([5.0, 5.0, 5.0, 5.0, 90.0])
        got = style.match_tone(src, np.linspace(0, 60, 100))
        self.assertEqual(len(set(got[:4].tolist())), 1)

    def test_matching_preserves_order(self):
        """A rank mapping may darken everything; it may never swap two pixels'
        relative brightness, or the picture stops being the picture."""
        src = np.array([[10.0, 40.0], [80.0, 200.0]])
        ref = np.array([[0.0, 5.0], [9.0, 60.0]])
        got = style.match_tone(src, ref)
        self.assertTrue(np.all(np.argsort(got.ravel()) == np.argsort(src.ravel())))


if __name__ == "__main__":
    unittest.main(verbosity=2)
