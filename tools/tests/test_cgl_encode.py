#!/usr/bin/env python3
"""The CGL writer, checked against the decoder that actually runs on the console."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "analysis"))

import cgl_encode
import zork_cgl

BG = ROOT / "saturn" / "cd" / "data" / "BG"
CGL_C = ROOT / "saturn" / "src" / "video" / "cgl.c"

HARNESS = """
#include <stdio.h>
#include <stdlib.h>
#include "cgl.h"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *rec = malloc(n);
    if (fread(rec, 1, n, f) != (size_t) n) return 4;
    fclose(f);
    unsigned char *dst = malloc(CGL_FRAME_BYTES);
    unsigned long got = cgl_decode(rec, (unsigned long) n, dst, CGL_FRAME_BYTES);
    if (got == 0) return 5;
    FILE *o = fopen(argv[2], "wb"); fwrite(dst, 1, got, o); fclose(o);
    return 0;
}
"""


def one_frame():
    """The first record of the forest archive, as (palette, pixels)."""
    buf = (BG / "BWOD.CGL").read_bytes()
    _idx, _off, pal, pix = next(iter(zork_cgl.records(buf)))
    return pal, pix


def roundtrip(pal, pix):
    """Encode then decode with the Python reader; returns what came back."""
    rec = cgl_encode.record(pal, pix)
    out = list(zork_cgl.records(rec))
    return (out[0][2], out[0][3]) if out else (None, None)


class CompressTest(unittest.TestCase):
    """The codec, on shapes chosen to break it."""

    PAL = [(((i % 32) * 255 // 31),) * 3 for i in range(256)]

    def check(self, data):
        pal, got = roundtrip(self.PAL, bytes(data))
        self.assertEqual(got, bytes(data))
        return pal

    def test_a_single_byte_survives(self):
        self.check(b"\x07")

    def test_one_byte_repeated_survives(self):
        """The case that only works if an overlapping match is allowed: the
        decoder reads bytes the same copy is still writing."""
        self.check(b"\x2a" * 5000)

    def test_a_short_pattern_repeated_survives(self):
        self.check(b"abcd" * 4000)

    def test_incompressible_data_survives(self):
        import random
        rnd = random.Random(1234)
        self.check(bytes(rnd.randrange(256) for _ in range(20000)))

    def test_data_longer_than_the_ring_survives(self):
        """A match can only reach 4096 bytes back; the encoder must not emit
        an offset that means something else by the time it is read."""
        import random
        rnd = random.Random(99)
        block = bytes(rnd.randrange(256) for _ in range(9000))
        self.check(block + block)

    def test_the_palette_survives_the_round_trip(self):
        """Exactly-representable colours only: a CLUT entry is RGB555, so each
        channel is quantised to five bits and (1, 1, 1) comes back black. The
        test palette is built from values that survive that on purpose --
        asserting otherwise would be asserting the format is something it is
        not."""
        pal = self.check(b"\x00" * 64)
        self.assertEqual(pal[:16], self.PAL[:16])

    def test_colour_is_quantised_to_five_bits_per_channel(self):
        rec = cgl_encode.record([(255, 130, 8)], b"\x00")
        self.assertEqual(zork_cgl.load_clut(rec, 0)[0], (255, 131, 8))

    def test_a_record_is_four_byte_aligned(self):
        """The next record in an archive starts on the boundary after this
        one; a misaligned record silently swallows its neighbour."""
        for n in range(1, 8):
            rec = cgl_encode.record(self.PAL, b"\x11" * n)
            self.assertEqual(len(rec) % 4, 0, f"{n} bytes gave {len(rec)}")


class RealFrameTest(unittest.TestCase):
    def test_a_real_frame_round_trips(self):
        pal, pix = one_frame()
        got_pal, got = roundtrip(pal, pix[:16000])
        self.assertEqual(got, pix[:16000])
        self.assertEqual(got_pal, pal)

    def test_the_stream_is_smaller_than_the_pixels(self):
        """A codec that grew the data would still decode; it would just make
        the archive bigger than storing the frame raw."""
        pal, pix = one_frame()
        rec = cgl_encode.record(pal, pix)
        self.assertLess(len(rec), len(pix))


class ConsoleDecoderTest(unittest.TestCase):
    """The only check that counts: the C the Saturn runs, on this output."""

    @classmethod
    def setUpClass(cls):
        if shutil.which("gcc") is None:
            raise unittest.SkipTest("gcc not available to build the host harness")
        cls.tmp = pathlib.Path(tempfile.mkdtemp())
        src = cls.tmp / "harness.c"
        src.write_text(HARNESS, encoding="utf-8")
        cls.exe = cls.tmp / "harness.exe"
        r = subprocess.run(["gcc", "-O2", "-o", str(cls.exe), str(src), str(CGL_C),
                            "-I", str(CGL_C.parent)], capture_output=True, text=True)
        if r.returncode != 0:
            raise unittest.SkipTest(f"harness would not build: {r.stderr[:200]}")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_console_decoder_reads_what_this_writes(self):
        pal, pix = one_frame()
        rec = self.tmp / "rec.bin"
        out = self.tmp / "out.raw"
        rec.write_bytes(cgl_encode.record(pal, pix))
        r = subprocess.run([str(self.exe), str(rec), str(out)], capture_output=True)
        self.assertEqual(r.returncode, 0, "cgl_decode refused the record")
        self.assertEqual(out.read_bytes(), pix,
                         "the console decoder produced different pixels")


if __name__ == "__main__":
    unittest.main(verbosity=2)
