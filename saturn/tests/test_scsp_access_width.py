#!/usr/bin/env python3
"""Sound RAM is written in 16-bit words, and this is what says so.

The SCSP sits on the Saturn's B-bus, which is sixteen bits wide. A byte write to
anything behind that bus is not narrowed on your behalf -- it is an access the
bus has no way to express, and what the chip does with it is not defined.

scsp.c copied waveforms into sound RAM one `signed char` at a time for as long as
the synth existed, and it was changed on the theory that this was what silenced
the netbin on real hardware.

**That theory was wrong**, and this file should not be read as recording a fault.
Measured from a netbin over NetLink, a region written by bytes and a region
written by words both read back 256 of 256 correct. The netbin's silence was
MEM4MB -- bit 9 of the SCSP's common register, which tells the chip sound RAM is
512 KB. The SH-2 reaches all of it regardless, so a read-back check passes either
way; the chip's own sample fetch is what honours the bit, and with it clear every
slot keyed onto an address nothing had been written to.

The word write stays because a word is the access the sixteen-bit B-bus is built
for and it costs nothing. What this file holds is that shape, so nobody
reintroduces a byte loop into sound RAM by accident -- not a claim that doing so
was ever observed to break anything.

Run as tests: pytest saturn/tests/test_scsp_access_width.py
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCSP = ROOT / "saturn" / "src" / "sound" / "scsp.c"
PROBE = ROOT / "tools" / "scspfx" / "src" / "main.cxx"


def code(path):
    """Source with comments stripped -- the comments here discuss the very
    construct being searched for, so a plain match finds the prose about it."""
    text = path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(l.split("//", 1)[0] for l in text.splitlines())


def test_the_engine_writes_sound_ram_in_words():
    src = code(SCSP)
    assert "g_wave16" in src, (
        "scsp.c no longer keeps a 16-bit view of sound RAM. The waveform copy "
        "has to go through one; see this file's header for what a byte-wide "
        "copy does on the machine and does not do under the emulator.")
    assert re.search(r"g_wave16\s*\[[^\]]+\]\s*=", src), (
        "nothing assigns through scsp.c's 16-bit sound RAM pointer, so whatever "
        "is uploading waveforms now is not doing it in words.")
    assert not re.search(r"g_wave\s*\[[^\]]+\]\s*=", src), (
        "scsp.c assigns through g_wave, the byte-wide view of sound RAM. That "
        "is the access the sixteen-bit B-bus is not built for; see this file's "
        "header for why that is a shape to hold and not a fault that was observed.")


def test_the_probe_writes_its_tone_in_words():
    """The probe has to be at least as correct as the thing it measures.

    It exists to say which SCSP slots are usable. One that laid its own test
    tone down through a byte loop would answer that question about a waveform
    the chip may never have received, and would do it convincingly -- the
    emulator would agree with it every time.
    """
    src = code(PROBE)
    assert not re.search(r"TONE_RAM\s*\[[^\]]+\]\s*=", src), (
        "tools/scspfx writes its tone through TONE_RAM, the byte-wide pointer. "
        "Every sweep it has ever reported was run under an emulator, where that "
        "works; on hardware it would be measuring slots against a waveform that "
        "was only partly written.")
    assert re.search(r"unsigned short\s*\*\s*\w+\s*=\s*\(\s*volatile unsigned short\s*\*\s*\)", src), (
        "tools/scspfx no longer takes a 16-bit view of sound RAM to write its "
        "tone through.")
