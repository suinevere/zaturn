#!/usr/bin/env python3
"""Render PNG previews of the NBG2 tile layer: the menu border and the gamepad
strip, as the Saturn would compose them.

The palette and the tiles come out of saturn/src/video/dash_tiles.c and the tile
names out of saturn/src/video/dash_map.h, so this cannot drift from what the
build actually ships -- regenerate the tiles and re-run it. Cells are placed on
the same 8x8 grid text_map uses, which makes the border geometry exact.

The TEXT is not exact. The Saturn draws SRL's built-in NBG3 font, which is not
in this repo; this substitutes whatever monospace face it can find so the
mock-ups read as menus. Judge the chrome here, not the glyphs.

Requires Pillow.

Usage: python3 tools/preview_dash.py OUTDIR
"""
import os
import pathlib
import re
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("preview_dash: needs Pillow -- pip install Pillow")

ROOT = pathlib.Path(__file__).resolve().parent.parent
TILES_C = ROOT / "saturn/src/video/dash_tiles.c"
MAP_H = ROOT / "saturn/src/video/dash_map.h"

COLS, ROWS = 40, 28          # the 320x224 text grid
BG = (8, 8, 14)              # the flat backdrop a netbin menu sits on
INK = (222, 226, 235)        # NBG3 text
PAPER = (18, 18, 22)         # the contact sheet behind each panel

# Tried in order; the first that exists wins. Judge the border, not the glyphs.
MONO_FONTS = [
    "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",
    "/System/Library/Fonts/Menlo.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
]
UI_FONTS = [
    "C:/Windows/Fonts/segoeui.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]


def load_font(cands, size):
    for c in cands:
        if pathlib.Path(c).exists():
            return ImageFont.truetype(c, size)
    return ImageFont.load_default()


def parse_tile_names():
    """The DT_* enum from dash_map.h, so the names here follow the header."""
    body = re.search(r"enum \{\s*(DT_BLANK.*?)\};", MAP_H.read_text(encoding="utf-8"),
                     re.S).group(1)
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
    names, nxt = {}, 0
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            nm, val = item.split("=")
            nxt = int(val.strip())
            item = nm.strip()
        names[item] = nxt
        nxt += 1
    return names


def parse_tiles(count):
    text = TILES_C.read_text(encoding="utf-8")
    pal_raw = re.search(r"dash_palette\[16\] = \{(.*?)\};", text, re.S).group(1)
    pal = [int(v.strip(), 16) for v in pal_raw.split(",") if v.strip()]

    tiles = []
    for row in re.findall(r"\{ ((?:0x[0-9A-F]{2}, ){31}0x[0-9A-F]{2}) \}", text):
        b = [int(v, 16) for v in row.split(",")]
        px = [[0] * 8 for _ in range(8)]
        for y in range(8):
            for x in range(0, 8, 2):
                v = b[y * 4 + (x >> 1)]
                px[y][x], px[y][x + 1] = v >> 4, v & 0x0F
        tiles.append(px)
    if len(tiles) != count:
        sys.exit("preview_dash: dash_tiles.c holds %d tiles, dash_map.h says DT_N is %d "
                 "-- regenerate with tools/gen_dash_tiles.py" % (len(tiles), count))
    return pal, tiles


DT = parse_tile_names()
PAL555, TILES = parse_tiles(DT["DT_N"])
MONO8 = load_font(MONO_FONTS, 9)


def rgb(i):
    """RGB555 as dash_tiles.c stores it (0x8000 | b<<10 | g<<5 | r) to 8-bit."""
    v = PAL555[i]
    r, g, b = v & 0x1F, (v >> 5) & 0x1F, (v >> 10) & 0x1F
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


class Screen:
    """One 320x224 screen: the NBG2 tile layer under the NBG3 text layer."""

    def __init__(self):
        self.img = Image.new("RGB", (COLS * 8, ROWS * 8), BG)
        self.cells, self.text = {}, {}

    def puts(self, cx, cy, s):
        for i, ch in enumerate(s):
            self.text[(cx + i, cy)] = ch

    def box_tiles(self, x0, y0, w, h):
        """What dash_box paints: a bevel, transparent inside."""
        x1, y1 = x0 + w - 1, y0 + h - 1
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                top, bot, lf, rt = y == y0, y == y1, x == x0, x == x1
                if   top and lf: t = DT["DT_BOX_TL"]
                elif top and rt: t = DT["DT_BOX_TR"]
                elif bot and lf: t = DT["DT_BOX_BL"]
                elif bot and rt: t = DT["DT_BOX_BR"]
                elif top:        t = DT["DT_BOX_TOP"]
                elif bot:        t = DT["DT_BOX_BOTTOM"]
                elif lf:         t = DT["DT_BOX_LEFT"]
                elif rt:         t = DT["DT_BOX_RIGHT"]
                else:            continue
                self.cells[(x, y)] = t

    def box_ascii(self, x0, y0, w, h):
        """What menu_frame prints when the layer is down -- still the fallback."""
        for r in range(h):
            edge = r in (0, h - 1)
            self.puts(x0, y0 + r, "".join(
                ("+" if c in (0, w - 1) else "-") if edge else
                ("|" if c in (0, w - 1) else " ") for c in range(w)))

    def panel_tiles(self, x0, y0, w, h, divs=()):
        """The gamepad strip: marble field, frame phased to stay in register."""
        x1, y1 = x0 + w - 1, y0 + h - 1
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                top, bot = y == y0, y == y1
                if x == x0:
                    t = (DT["DT_CORNER_TL"] if top else DT["DT_CORNER_BL"] if bot
                         else DT["DT_LEFT0"] + (y & 3))
                elif x == x1:
                    t = (DT["DT_CORNER_TR"] if top else DT["DT_CORNER_BR"] if bot
                         else DT["DT_RIGHT0"] + (y & 3))
                elif x in divs:
                    t = (DT["DT_TOP_DIVIDER"] if top else DT["DT_BOTTOM_DIVIDER"] if bot
                         else DT["DT_DIVIDER0"] + (y & 3))
                elif (x - 1) in divs:
                    t = (DT["DT_TOP_MODLEFT"] if top else DT["DT_BOTTOM_MODLEFT"] if bot
                         else DT["DT_MODLEFT0"] + (y & 3))
                elif top: t = DT["DT_TOP0"] + (x & 3)
                elif bot: t = DT["DT_BOTTOM0"] + (x & 3)
                else:     t = DT["DT_FIELD0"] + ((y & 3) << 2) + (x & 3)
                self.cells[(x, y)] = t

    def render(self, zoom=3):
        px = self.img.load()
        for (cx, cy), t in self.cells.items():
            tile = TILES[t]
            for y in range(8):
                for x in range(8):
                    v = tile[y][x]
                    if v:                       # palette entry 0 is transparent
                        px[cx * 8 + x, cy * 8 + y] = rgb(v)
        d = ImageDraw.Draw(self.img)
        for (cx, cy), ch in self.text.items():
            if ch != " ":
                d.text((cx * 8 + 1, cy * 8 - 1), ch, font=MONO8, fill=INK)
        return self.img.resize((self.img.width * zoom, self.img.height * zoom),
                               Image.NEAREST)


def label(img, caption, sub=""):
    pad = 34 if sub else 22
    out = Image.new("RGB", (img.width, img.height + pad), PAPER)
    out.paste(img, (0, pad))
    d = ImageDraw.Draw(out)
    d.text((8, 3), caption, font=load_font(UI_FONTS, 14), fill=(235, 235, 240))
    if sub:
        d.text((8, 19), sub, font=load_font(UI_FONTS, 11), fill=(150, 150, 160))
    return out


def hstack(imgs, gap=14):
    out = Image.new("RGB", (sum(i.width for i in imgs) + gap * (len(imgs) - 1),
                            max(i.height for i in imgs)), PAPER)
    x = 0
    for i in imgs:
        out.paste(i, (x, 0))
        x += i.width + gap
    return out


def tilesheet():
    """The box tiles at 22x on a checkerboard, so transparency is visible."""
    names = ["DT_BOX_TL", "DT_BOX_TOP", "DT_BOX_TR",
             "DT_BOX_LEFT", "DT_BLANK", "DT_BOX_RIGHT",
             "DT_BOX_BL", "DT_BOX_BOTTOM", "DT_BOX_BR"]
    Z, pad = 22, 26
    cell = 8 * Z
    img = Image.new("RGB", (3 * cell + 4 * pad, 3 * (cell + pad) + pad + 10), PAPER)
    d = ImageDraw.Draw(img)
    f = load_font(MONO_FONTS, 12)
    for i, nm in enumerate(names):
        r, c = divmod(i, 3)
        ox, oy = pad + c * (cell + pad), pad + r * (cell + pad)
        tile = TILES[DT[nm]]
        for y in range(8):
            for x in range(8):
                v = tile[y][x]
                col = rgb(v) if v else ((34, 34, 40) if (x // 2 + y // 2) % 2
                                        else (26, 26, 31))
                d.rectangle([ox + x * Z, oy + y * Z,
                             ox + x * Z + Z - 1, oy + y * Z + Z - 1], fill=col)
        d.rectangle([ox - 1, oy - 1, ox + cell, oy + cell], outline=(70, 70, 80))
        d.text((ox, oy + cell + 4), nm, font=f, fill=(160, 160, 172))
    return label(img, "The menu-box tiles, 8x8 at 22x",
                 "Real data from dash_tiles.c. Checkerboard = transparent.")


PAUSE = ["> 1) Resume", "  2) Display", "  3) Gameplay", "  4) Controls", "  5) Restart"]


def pause_menu(tiled):
    s = Screen()
    x0, y0, w, h = 10, 8, 20, 9
    (s.box_tiles if tiled else s.box_ascii)(x0, y0, w, h)
    s.puts(x0 + (w - 6) // 2, y0 + 1, "PAUSED")
    for i, r in enumerate(PAUSE):
        s.puts(x0 + 2, y0 + 3 + i, r)
    return s.render()


def before_after():
    return hstack([
        label(pause_menu(False), "BEFORE  -  printed +--+ chrome",
              "Still the fallback when the VRAM allocation fails."),
        label(pause_menu(True), "AFTER  -  NBG2 bevel",
              "Same cells, same text positions."),
    ])


def in_context():
    s = Screen()
    s.puts(0, 1, "West of House")
    s.puts(0, 3, "You are standing in an open field west")
    s.puts(0, 4, "of a white house, with a boarded front")
    s.puts(0, 5, "door.")
    s.puts(0, 7, "> open mailbox")
    s.panel_tiles(0, 19, 40, 9, divs=(14, 30))
    for cx, cy, txt in ((4, 21, "N  NE"), (2, 22, "W  *  E"), (4, 23, "S  SW"),
                        (16, 21, "mailbox"), (16, 22, "leaflet"), (16, 23, "door"),
                        (32, 21, "OPEN"), (32, 22, "TAKE"), (32, 23, "READ")):
        s.puts(cx, cy, txt)

    t = Screen()
    t.puts(0, 1, "West of House")
    t.puts(0, 3, "You are standing in an open field west")
    t.puts(0, 4, "of a white house, with a boarded front")
    x0, y0, w, h = 8, 9, 24, 9
    t.box_tiles(x0, y0, w, h)
    t.puts(x0 + (w - 7) // 2, y0 + 1, "DISPLAY")
    for i, r in enumerate(["> 1) Palette", "  2) Background", "  3) Text"]):
        t.puts(x0 + 2, y0 + 3 + i, r)
    t.puts(x0 + 2, y0 + 7, "  4) Ok")

    return hstack([
        label(s.render(), "The strip  -  marble field, phased frame",
              "Unchanged. This is the layer the border now shares."),
        label(t.render(), "A menu over the game  -  the same bevel",
              "Interior stays transparent; the window hides what is behind."),
    ])


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in (("border-1-tiles", tilesheet),
                     ("border-2-before-after", before_after),
                     ("border-3-in-context", in_context)):
        p = out / (name + ".png")
        fn().save(p)
        print(p)


if __name__ == "__main__":
    main()
