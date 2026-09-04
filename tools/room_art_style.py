#!/usr/bin/env python3
"""/*----------------------
 | room_art_style.py
 | Description: Puts a new picture into the house style, measured off the
 |     picture it is replacing rather than described.
 |
 |     Three things make a Zork I plate look like one, and only the first is
 |     obvious:
 |
 |       the tint     each frame is close to monochromatic, a single hue ramp
 |                    from black to a light. Reproduced by mapping luminance
 |                    through the reference frame's own colours.
 |       the darkness the plates live in a narrow dark band -- the Dam Control
 |                    Room runs 0 to 68 with a mean of 9 out of 255. A new
 |                    picture normalised to full range comes out mid-grey and
 |                    reads as a different medium however well it is tinted, so
 |                    the tonal distribution is histogram-matched to the
 |                    reference instead of stretched.
 |       the flatness the frames use TWENTY-SIX colours, not 256. BDAM's run 24
 |                    to 32. That posterisation is as much of the look as the
 |                    tint is, and a 255-colour picture beside them is instantly
 |                    the new one.
 |
 |     The reference is a FRAME, never an area. BCEL holds both brown rooms and
 |     blue ones -- its hues spread over 71 degrees, BDED's over 107 -- so
 |     "grade it like the cellar archive" does not name a colour. What does name
 |     one is the picture this room is replacing, which the presentation table
 |     already records for every room of every game.
 | Author: suinevere
 | Dependencies: csv, numpy, pathlib, PIL, re, zork_cgl
 | Globals: ROOT, BG, CSV, INC, WIDTH, HEIGHT
 ----------------------*/"""
import csv
import pathlib
import re
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "analysis"))

import zork_cgl

ROOT = pathlib.Path(__file__).resolve().parent.parent
BG = ROOT / "saturn" / "cd" / "data" / "BG"
CSV = ROOT / "analysis" / "zork_bg" / "room_backgrounds.csv"
INC = ROOT / "saturn" / "src" / "scene" / "game_presentation.inc"
WIDTH, HEIGHT = 320, 240


def frame_table():
    """/*----------------------
     | frame_table
     | Description: IMAGE_FRAME index -> (archive filename, byte offset), read
     |     out of the generated table and the extraction CSV together. The .inc
     |     holds the index every room record stores; the CSV holds which
     |     archive and offset that index means.
     | Author: suinevere
     | Dependencies: csv, re
     | Globals: INC, CSV
     | Params: N/A
     | Returns: {index: (archive, offset)}
     ----------------------*/"""
    inc = INC.read_text(encoding="utf-8")
    areas = re.findall(r'"([A-Z]+)"', re.search(
        r"PRES_AREA\[PRES_AREA_N\] = \{(.*?)\n\};", inc, re.S).group(1))
    frames = re.search(r"IMAGE_FRAME\[PRES_FRAME_N\] = \{(.*?)\n\};", inc, re.S).group(1)
    by_key = {}
    for i, (a, off, _ln) in enumerate(
            re.findall(r"\{\s*(\d+),\s*(\d+)UL,\s*(\d+)UL\s*\}", frames), 1):
        by_key[(areas[int(a)], int(off))] = i

    out = {}
    with CSV.open(newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            key = (r["area_archive"][:4], int(r["frame_offset"]))
            if key in by_key:
                out[by_key[key]] = (r["area_archive"], int(r["frame_offset"]))
    return out


def reference(index):
    """/*----------------------
     | reference
     | Description: One existing frame, by its IMAGE_FRAME index.
     | Author: suinevere
     | Dependencies: zork_cgl
     | Globals: BG, WIDTH, HEIGHT
     | Params: index -- the 1-based picture index
     | Returns: (rgb array HxWx3, palette colour count)
     ----------------------*/"""
    archive, offset = frame_table()[index]
    buf = (BG / archive).read_bytes()
    for _idx, off, pal, pix in zork_cgl.records(buf):
        if off != offset:
            continue
        raw = np.frombuffer(pix, dtype=np.uint8)
        rgb = np.array(pal, dtype=np.uint8)[raw].reshape(HEIGHT, WIDTH, 3)
        return rgb, int(np.unique(raw).size)
    raise SystemExit(f"room_art_style: no record at offset {offset} in {archive}")


def luminance(rgb):
    """Rec.601 luma of an HxWx3 array."""
    a = np.asarray(rgb, dtype=np.float64)
    return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]


def ramp_of(rgb):
    """/*----------------------
     | ramp_of
     | Description: A reference frame's luminance -> colour ramp, built from
     |     the colours it actually uses and interpolated across the gaps.
     |     Built from the palette rather than from pixel luminances: the frames
     |     hold about two dozen colours, so sampling by pixel gives the same
     |     two dozen points but weights them by area, which bends the ramp
     |     towards whatever the picture happens to be mostly made of.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: rgb -- the reference frame
     | Returns: a 256x3 array mapping luminance to colour
     ----------------------*/"""
    cols = np.unique(np.asarray(rgb).reshape(-1, 3), axis=0).astype(np.float64)
    lum = luminance(cols)
    order = np.argsort(lum)
    xs, keep = np.unique(lum[order], return_index=True)
    ys = cols[order][keep]
    idx = np.arange(256)
    return np.stack([np.interp(idx, xs, ys[:, c]) for c in range(3)], axis=1)


def match_tone(src_lum, ref_lum):
    """/*----------------------
     | match_tone
     | Description: Rewrites one luminance field to have the reference's
     |     distribution, by rank. Not a stretch and not a gamma: the shape of
     |     these histograms is the look, and only a rank mapping reproduces a
     |     shape.
     |
     |     The rank of a value is how many pixels are at or below it, not how
     |     many are strictly below. The difference is only visible where values
     |     tie, and in a rendered picture the brightest region is exactly where
     |     they do: 461 of this plate's pixels sat at its maximum, and counting
     |     strictly-below sent all 461 to the 99.4th percentile of the
     |     reference instead of the 100th. Everything the reference had above
     |     that -- its whole highlight, a quarter of its range -- was then
     |     unreachable, and every flat bright area in every generated plate came
     |     out dimmer than the frame it was graded against.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: src_lum -- the new picture's luminance; ref_lum -- the
     |     reference's
     | Returns: the rewritten luminance, same shape as src_lum
     ----------------------*/"""
    flat = src_lum.ravel()
    src_sorted = np.sort(flat)
    ref_sorted = np.sort(np.asarray(ref_lum).ravel())
    below = np.searchsorted(src_sorted, flat, side="right")
    pos = (below - 1).clip(0) / max(1, len(src_sorted) - 1) * (len(ref_sorted) - 1)
    return np.interp(pos, np.arange(len(ref_sorted)), ref_sorted).reshape(src_lum.shape)


def lift_gain(ref, lift):
    """/*----------------------
     | lift_gain
     | Description: The factor that opens a reference's ramp up beyond the
     |     colours the reference itself holds.
     |
     |     ramp_of can only ever return a colour the reference already uses, and
     |     the reference's brightest is dark: BMIN_07's is 5-bit r8 g10 b13 of
     |     31, which cgl_palette hands to the Saturn CLUT unscaled, so the
     |     console shows what the extraction shows. A plate matched faithfully
     |     to one of these is therefore capped at about a quarter of the range
     |     the hardware can display, however well it is matched -- which is
     |     correct, and is not always what the picture needs.
     |
     |     Scales the ramp's colours rather than the matched luminance, because
     |     scaling luminance cannot pass a cap that is in the ramp. Linear and
     |     per-channel, so the reference's hue and its black point both survive:
     |     0 stays 0, and the ratios between channels are untouched.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: N/A
     | Params: ref -- the reference frame; lift -- 0 for the reference's own
     |     range, 1 to put its brightest colour at full scale
     | Returns: the multiplier
     ----------------------*/"""
    if lift <= 0:
        return 1.0
    top = float(luminance(ref).max())
    if top <= 0:
        return 1.0
    return 1.0 + float(lift) * (255.0 / top - 1.0)


TARGET_MEAN = 24.0
"""TARGET_MEAN

Description: The mean luminance a finished plate is aimed at, and the one
    number that decides how bright the generated art is.

    Twenty-four, to sit inside the brightness the original disc actually has.
    Its 74 frames run from a mean of 7.4 to 74.2, with a mean-of-means of 18.8,
    a median of 14.3 and a p90 of 36.4 -- so 24 is squarely among them rather
    than above them, where 40 was. It is also exactly where the Advent river
    plate already sits, which is the one held up as looking right, so aiming
    here darkens the bright plates towards the disc without touching the dark
    blue ones: those are dark because their reference is dark and the lift is
    already at its ceiling, and lowering the target cannot push them further.
Author: suinevere
"""

MARGIN = 0.06
"""MARGIN

Description: How much of each edge of a generated plate is thrown away before
    it is styled.

    Diffusion models sign their work. Two of the first hundred plates came back
    with a fake artist's signature along the bottom edge -- text, which is the
    one thing a room background must not carry, since the game draws its own
    over it -- and the negative prompt cannot be relied on to stop it at the
    CFG 2.5 the checkpoint requires. The mark measured 18 pixels of 384, inside
    the bottom 4.9%, so six per cent clears it with room to spare. Taken off
    every edge rather than the bottom alone so the 4:3 the screen wants
    survives, and it costs a border a background does not need.
Author: suinevere
"""


def lift_for(index, cache={}):
    """/*----------------------
     | lift_for
     | Description: The lift that lands a plate graded against one reference at
     |     TARGET_MEAN.
     |
     |     Derived here rather than written into each plate's manifest entry,
     |     which is where it used to live. A stored lift is a copy of an answer
     |     to a question the target asks, so changing the target left every
     |     plate already drawn at the old brightness with nothing to say so --
     |     and the only way to re-grade them was to rewrite the manifest. As a
     |     function of the reference it is recomputed on every rebuild, and the
     |     fingerprint that decides whether a plate needs restyling includes
     |     TARGET_MEAN, so moving the target restyles exactly what it changes.
     | Author: suinevere
     | Dependencies: numpy
     | Globals: TARGET_MEAN
     | Params: index -- the reference picture index
     | Returns: a lift in 0..1
     ----------------------*/"""
    if index not in cache:
        ref, _n = reference(index)
        lum = luminance(ref)
        mean, top = float(lum.mean()), float(lum.max())
        span = 255.0 / top - 1.0 if top > 0 else 0.0
        cache[index] = (0.0 if mean <= 0 or span <= 0
                        else round(max(0.0, min(1.0,
                                                (TARGET_MEAN / mean - 1.0) / span)), 3))
    return cache[index]


def stylise(image, index, grain=0.0, lift=None, margin=MARGIN):
    """/*----------------------
     | stylise
     | Description: One new picture, put into the style of the frame it
     |     replaces: resized to the screen, tone-matched, mapped through the
     |     reference's ramp and posterised to the reference's own colour count.
     |
     |     Grain is optional and off by default. The originals are photographs
     |     and carry sensor noise the ramp cannot invent; a little added before
     |     quantising survives posterisation and reads as film, while too much
     |     simply spends colours on speckle.
     |
     |     Lift is optional and off by default, and is the one thing here that
     |     deliberately does not reproduce the original: see lift_gain.
     | Author: suinevere
     | Dependencies: numpy, PIL
     | Globals: WIDTH, HEIGHT
     | Params: image -- a PIL image; index -- the reference picture index;
     |     grain -- standard deviation of noise added before quantising;
     |     lift -- 0 to hold the reference's own range, 1 to open it to full
     |     scale, None to derive it from TARGET_MEAN
     | Returns: (paletted PIL image, palette as 256 RGB triples)
     ----------------------*/"""
    ref, ncol = reference(index)
    if lift is None:
        lift = lift_for(index)
    src = image.convert("RGB")
    if margin > 0:
        dx, dy = int(src.width * margin), int(src.height * margin)
        src = src.crop((dx, dy, src.width - dx, src.height - dy))
    small = np.asarray(src.resize((WIDTH, HEIGHT), Image.LANCZOS))
    lum = match_tone(luminance(small), luminance(ref))
    if grain > 0:
        lum = lum + np.random.default_rng(0).normal(0, grain, lum.shape)
    out = ramp_of(ref)[np.clip(lum, 0, 255).astype(np.uint8)] * lift_gain(ref, lift)
    q = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).quantize(
        colors=ncol, method=Image.MEDIANCUT)
    pal = list(q.getpalette() or [])
    pal += [0] * (256 * 3 - len(pal))
    return q, [(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]) for i in range(256)]
