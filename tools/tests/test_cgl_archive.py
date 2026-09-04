#!/usr/bin/env python3
"""The archive packer, checked the only way placements can be: by decoding the
packed bytes back at the offsets it claims, including through the C the console
actually runs."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "analysis"))

import cgl_archive
import cgl_encode
import zork_cgl

BG = ROOT / "saturn" / "cd" / "data" / "BG"
CGL_C = ROOT / "saturn" / "src" / "video" / "cgl.c"

HARNESS = """
#include <stdio.h>
#include <stdlib.h>
#include "cgl.h"
/* argv: archive, offset, length, out -- exactly what pres_frame hands
   room_art.cxx, so the harness reaches a frame the way the console does. */
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    unsigned long off = strtoul(argv[2], 0, 10);
    unsigned long len = strtoul(argv[3], 0, 10);
    if (!f) return 3;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t) n) return 4;
    fclose(f);
    if (off + len > (unsigned long) n) return 6;
    unsigned char *dst = malloc(CGL_FRAME_BYTES);
    unsigned long got = cgl_decode(buf + off, len, dst, CGL_FRAME_BYTES);
    if (got == 0) return 5;
    FILE *o = fopen(argv[4], "wb"); fwrite(dst, 1, got, o); fclose(o);
    return 0;
}
"""

PAL = [(((i % 32) * 255 // 31),) * 3 for i in range(256)]


def frames(n, size=4000):
    """n frames of distinguishable, mildly compressible pixels."""
    return [(PAL, bytes((i * 37 + j // 7) & 0xFF for j in range(size)))
            for i in range(n)]


class PackTest(unittest.TestCase):
    def test_offsets_are_the_running_length(self):
        recs = [cgl_encode.record(p, x) for p, x in frames(4)]
        archives, placements = cgl_archive.pack(recs)
        self.assertEqual(len(archives), 1)
        run = 0
        for (a, off, length), rec in zip(placements, recs):
            self.assertEqual((a, off, length), (0, run, len(rec)))
            run += len(rec)
        self.assertEqual(len(archives[0]), run)

    def test_every_offset_is_four_byte_aligned(self):
        """A misaligned record start swallows its neighbour: the decoder reads
        the length word from the wrong place."""
        recs = [cgl_encode.record(p, x) for p, x in frames(6, 3333)]
        _archives, placements = cgl_archive.pack(recs)
        for _a, off, _ln in placements:
            self.assertEqual(off % 4, 0)

    def test_the_cap_rolls_to_a_new_archive(self):
        recs = [cgl_encode.record(p, x) for p, x in frames(6)]
        cap = len(recs[0]) * 2 + 1
        archives, placements = cgl_archive.pack(recs, cap=cap)
        self.assertGreater(len(archives), 1)
        for buf in archives:
            self.assertLessEqual(len(buf), cap)
        self.assertEqual([a for a, _o, _l in placements], [0, 0, 1, 1, 2, 2])

    def test_a_record_bigger_than_the_cap_gets_an_archive_to_itself(self):
        """Refusing would lose a frame; sharing would drag a neighbour past the
        budget with it."""
        recs = [cgl_encode.record(p, x) for p, x in frames(3)]
        archives, placements = cgl_archive.pack(recs, cap=1)
        self.assertEqual(len(archives), 3)
        self.assertEqual([off for _a, off, _l in placements], [0, 0, 0])

    def test_an_unaligned_record_is_refused(self):
        with self.assertRaises(ValueError):
            cgl_archive.pack([b"\x00" * 5])

    def test_no_archives_from_no_frames(self):
        self.assertEqual(cgl_archive.pack([]), ([], []))


class KeyTest(unittest.TestCase):
    """An archive is read whole and held resident, so what it costs is paid back
    only if the rooms that share it are rooms the player walks between. Zork I
    got that by making each archive a place and giving each place its own track;
    all 54 of its archive crossings are track changes."""

    def test_an_archive_never_spans_two_keys(self):
        recs = [cgl_encode.record(p, x) for p, x in frames(6)]
        keys = ["cellar", "cellar", "maze", "maze", "maze", "river"]
        archives, placements = cgl_archive.pack(recs, keys=keys)
        seen = {}
        for (a, _o, _l), k in zip(placements, keys):
            self.assertEqual(seen.setdefault(a, k), k,
                             f"archive {a} holds two areas")
        self.assertEqual(len(archives), 3)

    def test_the_cap_still_splits_one_key(self):
        """A key too big for one archive is split rather than allowed to grow:
        the budget is a hard limit and the area is a preference."""
        recs = [cgl_encode.record(p, x) for p, x in frames(4)]
        archives, placements = cgl_archive.pack(
            recs, cap=len(recs[0]) * 2 + 1, keys=["maze"] * 4)
        self.assertEqual(len(archives), 2)
        self.assertEqual([a for a, _o, _l in placements], [0, 0, 1, 1])

    def test_no_keys_packs_purely_by_size(self):
        recs = [cgl_encode.record(p, x) for p, x in frames(4)]
        self.assertEqual(cgl_archive.pack(recs), cgl_archive.pack(recs, keys=None))


class StemTest(unittest.TestCase):
    def test_stems_are_lettered(self):
        self.assertEqual(cgl_archive.stems("GEN", 3), ["GENAA", "GENAB", "GENAC"])

    def test_a_stem_too_long_for_load_area_is_refused(self):
        with self.assertRaises(ValueError):
            cgl_archive.stems("GENERATED", 1)

    def test_the_suffix_rolls_past_the_first_letter(self):
        """A picture per room is about 140 archives; a single letter ran out at
        34 and stopped a run that had already been drawn."""
        self.assertEqual(cgl_archive.stems("GEN", 27)[26], "GENBA")
        self.assertEqual(len(set(cgl_archive.stems("GEN", 200))), 200)

    def test_running_out_of_names_is_refused(self):
        with self.assertRaises(ValueError):
            cgl_archive.stems("GEN", 677)


class BuildTest(unittest.TestCase):
    def test_build_verifies_and_names_every_frame(self):
        blobs, rows, sums = cgl_archive.build(frames(3), "GEN")
        self.assertEqual(list(blobs), ["GENAA"])
        self.assertEqual([r["archive"] for r in rows], ["GENAA"] * 3)
        self.assertEqual(set(sums), {"GENAA"})

    def test_build_is_deterministic(self):
        """The manifest is committed and the archive is not, so a second run on
        another machine has to produce the same bytes or the offsets in the
        table point into the wrong records."""
        a = cgl_archive.build(frames(4), "GEN")
        b = cgl_archive.build(frames(4), "GEN")
        self.assertEqual(a[0], b[0])
        self.assertEqual(a[1], b[1])
        self.assertEqual(a[2], b[2])

    def test_verify_catches_a_wrong_offset(self):
        want = frames(2)
        recs = [cgl_encode.record(p, x) for p, x in want]
        archives, placements = cgl_archive.pack(recs)
        bad = [(0, placements[1][1] + 4, placements[1][2]), placements[1]]
        with self.assertRaises(ValueError):
            cgl_archive.verify(archives, bad, [x for _p, x in want])

    def test_verify_catches_a_frame_running_past_its_archive(self):
        want = frames(1)
        recs = [cgl_encode.record(p, x) for p, x in want]
        archives, placements = cgl_archive.pack(recs)
        a, off, length = placements[0]
        with self.assertRaises(ValueError):
            cgl_archive.verify(archives, [(a, off, length + 64)],
                               [x for _p, x in want])


class RealFrameTest(unittest.TestCase):
    """Packing frames off the original disc, which is what the generated ones
    will look like once they are in the house style."""

    def setUp(self):
        if not (BG / "BWOD.CGL").is_file():
            self.skipTest("no BWOD.CGL in this checkout -- run tools/assets/bg.bat")

    def real(self, n):
        buf = (BG / "BWOD.CGL").read_bytes()
        out = []
        for _idx, _off, pal, pix in zork_cgl.records(buf):
            out.append((pal, pix))
            if len(out) == n:
                break
        return out

    def test_real_frames_survive_the_round_trip_at_their_offsets(self):
        got = self.real(3)
        blobs, rows, _sums = cgl_archive.build(got, "GEN")
        for row, (_pal, pix) in zip(rows, got):
            buf = blobs[row["archive"]]
            rec = buf[row["offset"]:row["offset"] + row["length"]]
            back = list(zork_cgl.records(rec))
            self.assertEqual(back[0][3], pix)


class ConsoleDecoderTest(unittest.TestCase):
    """The check that counts: the placements read by the C the Saturn runs."""

    @classmethod
    def setUpClass(cls):
        if shutil.which("gcc") is None:
            raise unittest.SkipTest("gcc not available to build the host harness")
        cls.tmp = pathlib.Path(tempfile.mkdtemp())
        src = cls.tmp / "harness.c"
        src.write_text(HARNESS, encoding="utf-8")
        cls.exe = cls.tmp / "harness.exe"
        r = subprocess.run(["gcc", "-O2", "-o", str(cls.exe), str(src), str(CGL_C),
                            "-I", str(CGL_C.parent)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise unittest.SkipTest(f"harness would not build: {r.stderr[:200]}")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_console_decoder_reaches_every_frame_at_its_offset(self):
        want = frames(5, 76800)
        blobs, rows, _sums = cgl_archive.build(want, "GEN")
        for name, blob in blobs.items():
            (self.tmp / f"{name}.CGL").write_bytes(blob)
        for i, row in enumerate(rows):
            out = self.tmp / f"out{i}.bin"
            stem = row["archive"]
            r = subprocess.run([str(self.exe), str(self.tmp / f"{stem}.CGL"),
                                str(row["offset"]), str(row["length"]), str(out)],
                               capture_output=True)
            self.assertEqual(r.returncode, 0,
                             f"frame {i} at {stem} offset {row['offset']} would "
                             f"not decode (exit {r.returncode})")
            self.assertEqual(out.read_bytes(), want[i][1])


if __name__ == "__main__":
    unittest.main()
