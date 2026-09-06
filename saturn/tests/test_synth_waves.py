#!/usr/bin/env python3
"""The waveform tables are built on the Saturn now, and this is what says they
are still the same bytes.

They were 5,120 bytes of .rodata emitted by tools/assets/genwaves.py. They are
built at boot by synth_waves.c instead, because .bss costs the netbin image no
file bytes and .rodata does -- PlanetWeb 4.0 is bounded by file size and refuses
an oversized image without saying why. See saturn/tests/test_netbin_budget.py.

That trade is only safe if the bytes are identical, and "identical" is not
something to take on the reading of two implementations side by side. So this
compiles synth_waves.c with a host compiler, runs it, and diffs all 5,120 bytes
against what genwaves.py produces. genwaves.py stays the authority; the C is a
port of it, and this is the only thing holding the port honest.

synth_waves.c is deliberately free of includes so that compiling it here needs
nothing but the file itself.

Skipped, not failed, when there is no host C compiler: the check is worth having
where one exists and is not worth blocking a run where one does not.

Run as tests: pytest saturn/tests/test_synth_waves.py
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "saturn" / "src" / "sound" / "synth_waves.c"

sys.path.insert(0, str(ROOT / "tools" / "assets"))
import genwaves                                  # noqa: E402  (path set above)


def cdefines(path):
    """Every plain integer #define in a file."""
    text = path.read_text(encoding="utf-8", errors="replace")
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^#define\s+(\w+)\s+(\d+)\s*$", text, re.M)}


def host_cc():
    for name in ("gcc", "cc", "clang"):
        found = shutil.which(name)
        if found:
            return found
    return None


HARNESS = r"""
#include <stdio.h>
extern signed char SYNTH_WAVE_TABLE[4][256];
extern signed char SYNTH_NOISE_TABLE[4096];
void synth_waves_build(void);
int main(void) {
    int w, i;
    synth_waves_build();
    for (w = 0; w < 4; w++)
        for (i = 0; i < 256; i++) printf("%d\n", (int) SYNTH_WAVE_TABLE[w][i]);
    for (i = 0; i < 4096; i++) printf("%d\n", (int) SYNTH_NOISE_TABLE[i]);
    return 0;
}
"""


def built_tables():
    """Compile synth_waves.c on the host and read back what it builds."""
    cc = host_cc()
    if cc is None:
        pytest.skip("no host C compiler on PATH -- this check needs one to run "
                    "the generator it is checking")
    with tempfile.TemporaryDirectory() as d:
        d = pathlib.Path(d)
        (d / "harness.c").write_text(HARNESS, encoding="utf-8")
        exe = d / ("h.exe" if sys.platform == "win32" else "h")
        r = subprocess.run([cc, "-O2", "-std=c99", "-o", str(exe),
                            str(d / "harness.c"), str(SRC)],
                           capture_output=True, text=True)
        assert r.returncode == 0, (
            "synth_waves.c does not compile on the host:\n" + r.stderr +
            "\nIt is meant to stay free of includes and of anything "
            "Saturn-specific, precisely so that it can be compiled and run here.")
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        assert out.returncode == 0, "the generator harness did not run"
        nums = [int(x) for x in out.stdout.split()]
    assert len(nums) == 4 * 256 + 4096, (
        f"the harness printed {len(nums)} values, not {4 * 256 + 4096}")
    return [nums[w * 256:(w + 1) * 256] for w in range(4)], nums[1024:]


def test_the_built_waves_match_the_generator():
    """All four tonal tables, sample for sample.

    The shipped voice is `nes`: three pulses and the 2A03's sixteen-level
    staircase triangle. `smooth` is the one that needs sin(), and it is not what
    is built here -- if it ever becomes what is wanted, this file is where that
    stops being a table the Saturn can afford to build for itself.
    """
    waves, _ = built_tables()
    for w in range(4):
        want = list(genwaves.build(w, "nes"))
        assert waves[w] == want, (
            f"wave {w} built on the Saturn differs from genwaves.build({w}, "
            f"'nes') at {sum(1 for a, b in zip(waves[w], want) if a != b)} of "
            f"{len(want)} samples. The synth would be uploading a different "
            "waveform to the chip than the one that was tuned against the NES "
            "original.")


def test_the_built_noise_matches_the_generator():
    """And the percussion table, which is the one that was calibrated by ear.

    Its warm-up and its amplitude are both the result of listening against a
    recording -- the register seeded with 1 is biased enough for its first few
    hundred outputs that every drum hit had an audible pitch. A port that got
    the warm-up wrong would sound exactly like that bug again.
    """
    _, noise = built_tables()
    want = list(genwaves.build_noise())
    assert noise == want, (
        f"the noise table built on the Saturn differs from genwaves.build_noise() "
        f"at {sum(1 for a, b in zip(noise, want) if a != b)} of {len(want)} "
        "samples.")


def test_the_constants_did_not_drift():
    """The four calibrations, held across two files that cannot see each other.

    Same reason test_noise_table.py pins scsp.h against genwaves.py: a number
    changed in one place and not the other is silent, and the symptom is a sound
    that is subtly wrong rather than a build that fails.
    """
    d = cdefines(SRC)
    assert d["WAVE_LEN"] == genwaves.LEN
    assert d["WAVE_AMP"] == int(genwaves.AMP)
    assert d["NOISE_LEN"] == genwaves.NOISE_LEN
    assert d["NOISE_OVERSAMPLE"] == genwaves.NOISE_OVERSAMPLE
    assert d["NOISE_WARMUP"] == genwaves.NOISE_WARMUP
    assert d["NOISE_AMP"] == int(round(genwaves.NOISE_AMP))


def test_the_tables_are_not_linked_constants():
    """The point of the exercise, guarded.

    If these go back to being const arrays with data in them, the netbin gets
    5,120 bytes heavier and test_netbin_budget is what notices -- but only after
    somebody has spent the headroom. This says it here instead.
    """
    text = SRC.read_text(encoding="utf-8", errors="replace")
    assert "const signed char SYNTH_WAVE_TABLE" not in text, (
        "SYNTH_WAVE_TABLE is a linked constant again. That is 1,024 bytes of "
        "netbin image for something the machine can build in a few thousand "
        "integer operations.")
    assert "const signed char SYNTH_NOISE_TABLE" not in text, (
        "SYNTH_NOISE_TABLE is a linked constant again -- 4,096 bytes of netbin "
        "image for a fifteen-bit shift register.")
