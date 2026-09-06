"""The pitched voices' release, and the model that has to agree with it.

    python -m pytest saturn/tests/test_release_envelope.py -q

Both of the pitched envelope's rates were 31 -- the field's maximum -- for as
long as this synth has existed, which is a note that stops rather than one that
ends. It was never caught because the offline preview had no release at all:
`amp[ch] = 0.0` on key-off, instantly, so the model reproduced the fault and
agreed with the chip about it. It took someone listening to hardware.

Two things are pinned. The number in the model is the number in the header --
they cannot be one constant, because a host cannot read a C header, so they are
two that a test holds together. And the model actually releases: a rest has to
be a note ending, not a note stopping, or the next person to turn this dial
turns it against a preview that cannot show the difference.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "assets"))
import mid2pat
import preview

SCSP_H = ROOT / "saturn" / "src" / "sound" / "scsp.h"


def _define(name):
    text = SCSP_H.read_text(encoding="utf-8")
    found = re.search(r"^#define\s+%s\s+(\d+)" % name, text, re.M)
    assert found, "%s is not defined in %s" % (name, SCSP_H)
    return int(found.group(1))


def test_the_model_uses_the_rate_the_chip_is_given():
    assert preview.SUSTAINED_RR == _define("SCSP_EG_SUSTAINED_RR"), (
        "preview.py models a release the chip is not being given. Whichever "
        "was meant, the other has to follow -- a preview that disagrees with "
        "the hardware is worse than no preview.")
    assert preview.PERC_D1R_RATE == _define("SCSP_EG_PERC_D1R"), (
        "the one measured point the release time is extrapolated from has "
        "moved in the header and not in the model")


def test_the_release_is_not_the_chips_maximum():
    # What was reported: "very sharp release, not rounded or soft release".
    # 31 is as sharp as the field goes.
    rate = _define("SCSP_EG_SUSTAINED_RR")
    assert rate < 31, "the release is back at the chip's fastest rate"
    assert rate >= 4, (
        "the bottom of a Yamaha rate field is where 'no change' lives, and a "
        "voice that never releases is a voice that never stops")


def test_a_rest_ends_a_note_rather_than_stopping_it():
    # The model's half of it. One voice, one note, then a rest: the samples
    # after the key-off must fall rather than vanish.
    rate = preview.SUSTAINED_RR
    halflife = preview.release_halflife(rate)
    assert 0.005 < halflife < 0.2, (
        "a release half-life of %.4f s is outside anything that would read as "
        "a release: under 5 ms is the click this replaced and over 200 ms "
        "smears one note into the next" % halflife)

    wave = mid2pat.CH_WAVE_TONAL[1]
    note = (40 + 2, (wave << 4) | 7)
    cells = [[note, (0, 0), (0, 0), (0, 0)]]
    cells += [[(0, 0)] * 4 for _ in range(3)]
    cells += [[(1, 0), (0, 0), (0, 0), (0, 0)]]
    cells += [[(0, 0)] * 4 for _ in range(8)]
    buf = preview.render(cells, 8, 0, mid2pat.CH_WAVE_TONAL, 5.0)

    rows = 8.0 / 60.0 * preview.RATE
    off = int(4 * rows)
    def peak(a, b):
        window = buf[int(a):int(b)]
        return max((abs(x) for x in window), default=0.0)

    held = peak(off - rows * 0.5, off)
    just_after = peak(off, off + 0.001 * preview.RATE)
    later = peak(off + halflife * 4 * preview.RATE,
                 off + halflife * 5 * preview.RATE)
    assert held > 0, "the note never sounded"
    assert just_after > 0.4 * held, (
        "the note is gone within a millisecond of the key-off -- the model is "
        "still cutting rather than releasing")
    assert later < 0.2 * held, (
        "the note is still at %.2f of its level four half-lives after the "
        "key-off, so the model is not releasing at the rate it claims"
        % (later / held))
