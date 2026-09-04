#!/usr/bin/env python3
"""/*----------------------
 | gen_art_refs.py
 | Description: GENERATES the composition references in tools/assets/art/refs
 |     -- crude geometric guides that a plate is drawn over when a sentence
 |     cannot pin its geometry down.
 |
 |     Drawn rather than photographed on purpose. A photograph brings its own
 |     subject, its own light and its own copyright; what is wanted here is
 |     only the arrangement of the frame, and the arrangement is a handful of
 |     converging lines and two flat bands. At a denoise of 0.6 the model keeps
 |     that arrangement and repaints everything else, so a guide this crude is
 |     worth more than a good photograph of the wrong crawl space.
 |
 |     One entry so far. Eight rewordings of Hollywood Hijinx's crawl space
 |     each produced exactly what they asked for and none of it was a crawl
 |     space: "brick piers" drew pillars, "a shaft of light" drew a skylight,
 |     "slivers of light between the boards" drew a hole through them, and
 |     "floorboards" drew boarded-up windows with daylight behind them. None of
 |     those are hallucinations. They are all the picture the words asked for,
 |     and words are the wrong instrument for a geometry.
 | Author: suinevere
 | Dependencies: pathlib, random, sys, PIL
 | Globals: ROOT, REFS, WIDTH, HEIGHT
 ----------------------*/"""
import pathlib
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
REFS = ROOT / "tools" / "assets" / "art" / "refs"
WIDTH, HEIGHT = 512, 384
"""ROOT / REFS / WIDTH / HEIGHT

Description: Where the guides go and how big they are -- the same 512x384 the
    plates themselves are generated at, so the server never has to rescale one
    and crop the composition it was brought in for.
Author: suinevere
"""


def crawlspace():
    """/*----------------------
     | crawlspace
     | Description: The underfloor void: joists receding overhead across the
     |     top of the frame, packed dirt below, and no light in it.
     |
     |     The horizon sits at 55% rather than halfway. A crawl space is not a
     |     room bisected -- the floor above is the whole subject and the dirt
     |     is the strip you are lying on, so the ceiling plane takes more of
     |     the frame than the ground does.
     | Author: suinevere
     | Dependencies: PIL
     | Globals: WIDTH, HEIGHT
     | Params: N/A
     | Returns: the guide as an Image
     ----------------------*/"""
    im = Image.new("RGB", (WIDTH, HEIGHT), (8, 7, 6))
    d = ImageDraw.Draw(im)
    horizon = int(HEIGHT * 0.55)
    vx, vy = WIDTH // 2, horizon

    # The dirt: a flat band, lighter than the void but barely.
    d.rectangle([0, horizon, WIDTH, HEIGHT], fill=(34, 28, 22))
    for i in range(1, 7):
        y = horizon + int((HEIGHT - horizon) * (i / 7) ** 1.6)
        d.line([(0, y), (WIDTH, y)], fill=(26, 21, 17), width=2)

    # Stones and clods, bigger and further apart towards the viewer. The flat
    # band on its own gave the model nothing to hold on to down here and it
    # painted the whole lower half as empty dark space -- the ground has to
    # have a surface in the guide before it can have one in the plate.
    rng = random.Random(20260904)
    for _ in range(2200):
        t = rng.random() ** 0.55
        y = horizon + int((HEIGHT - horizon) * t) + 2
        if y >= HEIGHT:
            continue
        x = rng.uniform(-20, WIDTH + 20)
        r = 0.7 + t * 3.4
        v = rng.randint(20, 52) if rng.random() < 0.62 else rng.randint(8, 20)
        d.ellipse([x - r, y - r * 0.55, x + r, y + r * 0.55],
                  fill=(v + 7, v + 1, v - 5))

    # The joists: parallel timbers running away overhead, so they converge on
    # the vanishing point. Wider apart towards the viewer, as they are.
    for i in range(-7, 8):
        x = vx + i * 78
        d.line([(vx, vy), (x, -40)], fill=(46, 36, 27), width=9)
        d.line([(vx, vy), (x, -40)], fill=(20, 15, 11), width=3)

    # The boarding they carry, crossing them, spaced by perspective.
    for i in range(1, 9):
        y = vy - int(vy * (i / 9) ** 1.5)
        d.line([(0, y), (WIDTH, y)], fill=(38, 30, 23), width=2)

    # Dark at the edges: the void has no light in it and the corners are the
    # furthest thing from the viewer.
    vign = Image.new("L", (WIDTH, HEIGHT), 0)
    ImageDraw.Draw(vign).ellipse(
        [-WIDTH // 3, -HEIGHT // 3, WIDTH + WIDTH // 3, HEIGHT + HEIGHT // 3],
        fill=255)
    vign = vign.filter(ImageFilter.GaussianBlur(90))
    im = Image.composite(im, Image.new("RGB", (WIDTH, HEIGHT), (3, 3, 3)), vign)
    return im.filter(ImageFilter.GaussianBlur(1.2))


GUIDES = {"crawlspace.png": crawlspace}
"""GUIDES

Description: The guides, by the filename a room's override names.
Author: suinevere
"""


def main():
    """/*----------------------
     | main
     | Description: Writes every guide.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: REFS, GUIDES
     | Params: N/A
     | Returns: 0
     ----------------------*/"""
    REFS.mkdir(parents=True, exist_ok=True)
    for name, make in GUIDES.items():
        path = REFS / name
        make().save(path)
        print(f"wrote {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
