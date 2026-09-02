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
    rank = np.clip(np.searchsorted(src_sorted, flat, side="left"), 0, len(src_sorted) - 1)
    pos = (rank / max(1, len(src_sorted) - 1)) * (len(ref_sorted) - 1)
    return np.interp(pos, np.arange(len(ref_sorted)), ref_sorted).reshape(src_lum.shape)


def stylise(image, index, grain=0.0):
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
     | Author: suinevere
     | Dependencies: numpy, PIL
     | Globals: WIDTH, HEIGHT
     | Params: image -- a PIL image; index -- the reference picture index;
     |     grain -- standard deviation of noise added before quantising
     | Returns: (paletted PIL image, palette as 256 RGB triples)
     ----------------------*/"""
    ref, ncol = reference(index)
    small = np.asarray(image.convert("RGB").resize((WIDTH, HEIGHT), Image.LANCZOS))
    lum = match_tone(luminance(small), luminance(ref))
    if grain > 0:
        lum = lum + np.random.default_rng(0).normal(0, grain, lum.shape)
    out = ramp_of(ref)[np.clip(lum, 0, 255).astype(np.uint8)]
    q = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).quantize(
        colors=ncol, method=Image.MEDIANCUT)
    pal = list(q.getpalette() or [])
    pal += [0] * (256 * 3 - len(pal))
    return q, [(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]) for i in range(256)]
