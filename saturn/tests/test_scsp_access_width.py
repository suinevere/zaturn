#!/usr/bin/env python3
"""Sound RAM is written in 16-bit words, and this is what says so.

The SCSP sits on the Saturn's B-bus, which is sixteen bits wide. A byte write to
anything behind that bus is not narrowed on your behalf -- it is an access the
bus has no way to express, and what the chip does with it is not defined.

scsp.c copied waveforms into sound RAM one `signed char` at a time for as long as
the synth has existed. Every run that anyone called working was under Mednafen,
which simply performs the write; the first time it went over NetLink onto a real
Saturn it came back as a waveform table that was part right and part whatever had
been there before. That is not silence and it is not music -- it is a run of
bleeps, a different run of bleeps, and sometimes nothing, from one unchanged
binary. Two sessions were spent above the tracker looking for it.

Nothing on a host can execute this, and the emulator cannot fail it, so what is
left is to hold the shape of the code: the copy goes through a 16-bit pointer,
and no byte-wide store into sound RAM comes back.

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
        "is the write that made the netbin's music come back as bleeps on one "
        "run of real hardware and silence on the next.")


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
