"""Score a candidate background on the three things that disqualify one.

Description: The game draws its text over the picture on VDP2 NBG0, at
    Priority::Layer1, with nothing between them. So a candidate fails for three
    reasons and they are not equally fixable:

      bright  -- the player's Dimming row can rescue this, so it is the softest
                 threshold of the three.
      busy    -- a uniform colour offset does not touch local contrast, so no
                 setting rescues a busy picture. Judged first, and hardest.
      banding -- the disc is 8bpp and make_tga.py quantises to 255 colours, so a
                 wide smooth gradient becomes visible steps. Cheaper to reject
                 here than to notice on hardware.

    THRESHOLDS is calibrated against the first real fetch: 524 Pixabay
    photographs scored by this module (tools/assets/art_manifest.json).
    Measured distribution, with each threshold's reject rate against it:
      luminance p50=105.2 max=227.1 -> threshold 165.0 rejects  7.6%
      busyness  p50= 26.5 max= 76.1 -> threshold  35.0 rejects 31.1%
      banding   p50=  3.2 max=  7.95 -> threshold  12.0 rejects  0.0%
    busyness_max was raised from an original 18.0, which was never validated
    against real input: measurement showed it rejected 68.3% of the 524
    photographs -- the median photograph in 8 of the 12 moods -- so 18.0 was
    mis-scaled, not correctly strict. 35.0 keeps the busiest ~31% out.
    Whether 35.0 is the right legibility limit is still unproven; the
    Task 6 Step 7 hardware check is what will settle that, not this number.
    banding_max is left at 12.0 on purpose: it has never fired against a
    real photograph, and there is no evidence a real photograph scoring 6-8
    looks bad on Saturn, so lowering it on no evidence would be worse than a
    quiet gate.

    Reference points from tools/tests/test_art_metrics.py's synthetic
    fixtures, useful for reasoning about a single metric in isolation:
      flat(120) mid-grey      -> luminance=120.0, busyness=0.0,   banding=0.0
      flat(240) bright        -> luminance=240.0, busyness=0.0,   banding=0.0
      checkerboard(4px cells) -> luminance=177.5, busyness=157.4, banding=0.0
      wide_gamut_gradient     -> luminance=126.9, busyness=1.3,   banding=4.12
Author: suinevere
Dependencies: PIL
Globals: THRESHOLDS
"""
from collections import namedtuple

from PIL import Image, ImageFilter

WIDTH, HEIGHT = 320, 224

Scores = namedtuple("Scores", "luminance busyness banding")

THRESHOLDS = {
    "luminance_max": 165.0,
    "busyness_max":  35.0,
    "banding_max":   12.0,
}


def crop(im):
    """Centre-crop and resize any image to exactly 320x224.

    Description: Scales the shorter side to fit, then takes the middle. Naive on
        purpose: a saliency crop would need a model, and human review is where a
        badly-framed picture gets rejected anyway.
    Author: suinevere
    Dependencies: PIL
    Globals: WIDTH, HEIGHT
    Params: im -- any PIL image
    Returns: a 320x224 RGB image
    """
    im = im.convert("RGB")
    w, h = im.size
    scale = max(WIDTH / w, HEIGHT / h)
    nw, nh = max(WIDTH, int(round(w * scale))), max(HEIGHT, int(round(h * scale)))
    im = im.resize((nw, nh), Image.LANCZOS)
    left, top = (nw - WIDTH) // 2, (nh - HEIGHT) // 2
    return im.crop((left, top, left + WIDTH, top + HEIGHT))


def score(im):
    """Measure luminance, busyness and quantisation banding on a cropped candidate.

    Description: On Pillow 12.3.0, FIND_EDGES (and Kernel filters generally)
        leaves the outermost 1px ring copied from the source instead of
        filtered, so a flat image's own brightness leaks into busyness. The
        crop before averaging discards that ring; a future Pillow could change
        this border behaviour, so re-check test_a_calm_dark_image_passes and
        the border-leak regression test if this metric's baseline drifts.
    Author: suinevere
    Dependencies: PIL
    Globals: N/A
    Params: im -- any PIL image; cropped internally
    Returns: a Scores triple
    """
    im = crop(im)
    grey = im.convert("L")

    px = grey.tobytes()
    luminance = sum(px) / float(len(px))

    edge_im = grey.filter(ImageFilter.FIND_EDGES)
    edges = edge_im.crop((1, 1, WIDTH - 1, HEIGHT - 1)).tobytes()
    busyness = sum(edges) / float(len(edges))

    q = im.quantize(colors=255, method=Image.Quantize.MEDIANCUT).convert("RGB")
    a, b = im.tobytes(), q.tobytes()
    total = 0
    for i in range(0, len(a), 97):
        d = a[i] - b[i]
        total += d * d
    banding = (total / float(len(range(0, len(a), 97)))) ** 0.5

    return Scores(luminance, busyness, banding)


def verdict(s):
    """Decide whether a candidate survives to human review.

    Description: Busyness is judged first because it is the only failure no
        player setting can undo -- the Dimming row shifts every pixel by the same
        constant, which leaves local contrast exactly where it was.
    Author: suinevere
    Dependencies: N/A
    Globals: THRESHOLDS
    Params: s -- a Scores triple
    Returns: "pass", or one of "busy", "bright", "banding"
    """
    if s.busyness > THRESHOLDS["busyness_max"]:   return "busy"
    if s.luminance > THRESHOLDS["luminance_max"]: return "bright"
    if s.banding > THRESHOLDS["banding_max"]:     return "banding"
    return "pass"
