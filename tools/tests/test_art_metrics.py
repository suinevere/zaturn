"""Score synthetic images whose properties are known by construction."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import art_metrics


def flat(value, size=(640, 480)):
    return Image.new("RGB", size, (value, value, value))


def checkerboard(size=(640, 480), cell=4):
    im = Image.new("RGB", size, (0, 0, 0))
    d = ImageDraw.Draw(im)
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            if (x // cell + y // cell) % 2:
                d.rectangle([x, y, x + cell, y + cell], fill=(255, 255, 255))
    return im


def gradient(size=(640, 480)):
    im = Image.new("RGB", size)
    for x in range(size[0]):
        v = int(255 * x / (size[0] - 1))
        for y in range(size[1]):
            im.putpixel((x, y), (v, v // 2, 255 - v))
    return im


def test_crop_always_produces_the_disc_size():
    for size in [(640, 480), (1920, 1080), (300, 900), (320, 224)]:
        assert art_metrics.crop(Image.new("RGB", size)).size == (320, 224)


def test_luminance_tracks_brightness():
    assert art_metrics.score(flat(20)).luminance < 40
    assert art_metrics.score(flat(240)).luminance > 200


def test_busyness_separates_flat_from_checkerboard():
    assert art_metrics.score(flat(120)).busyness < 1.0
    assert art_metrics.score(checkerboard()).busyness > 20.0


def test_a_bright_image_is_rejected_as_bright():
    assert art_metrics.verdict(art_metrics.score(flat(250))) == "bright"


def test_a_busy_image_is_rejected_as_busy():
    assert art_metrics.verdict(art_metrics.score(checkerboard())) == "busy"


def test_a_calm_dark_image_passes():
    assert art_metrics.verdict(art_metrics.score(flat(70))) == "pass"


def test_busyness_is_judged_before_brightness():
    """A dim cannot fix busy, only bright -- so busy must win when both trip."""
    bright_and_busy = art_metrics.Scores(luminance=250.0, busyness=99.0, banding=0.0)
    assert art_metrics.verdict(bright_and_busy) == "busy"


def test_banding_is_measured_and_finite():
    s = art_metrics.score(gradient())
    assert s.banding >= 0.0
    assert s.banding < 255.0


def test_busyness_does_not_leak_border_brightness():
    """Regression: a flat mid-grey image must score near-zero busyness.

    Pillow's FIND_EDGES leaves the outer 1px ring unfiltered (copied from the
    source) rather than zeroed, so on a flat image that ring's own brightness
    used to leak straight into busyness -- coupling it to luminance, which
    defeats the point of judging them separately. If this regresses, someone
    dropped the border crop in art_metrics.score.
    """
    assert art_metrics.score(flat(120)).busyness < 0.1
