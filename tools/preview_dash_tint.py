"""Render the tinted marble border on every Display preset, untinted beside it.

Mirrors write_palette() in dash_view.cxx exactly, so what this draws is what the
Saturn will put in CRAM. Tiles and palette come from dash_tiles.c through
tools/preview_dash.py, so this cannot drift from the build either. The TEXT is a
substituted monospace face, not SRL's font -- judge the chrome, not the glyphs.

The tint strength is DASH_TINT_NUM/DASH_TINT_DEN in dash_view.cxx; pass a
different pair here to see what another one would look like before changing it.

Usage: python tools/preview_dash_tint.py OUTDIR [--changed] [num den]
       --changed  only the presets whose ground has a hue, at 2x
"""
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
if not (ROOT / "saturn").is_dir():
    sys.exit("preview_dash_tint: expected to live in <repo>/tools")

spec = importlib.util.spec_from_file_location("preview_dash", ROOT / "tools/preview_dash.py")
pd = importlib.util.module_from_spec(spec)
sys.modules["preview_dash"] = pd
spec.loader.exec_module(pd)

from PIL import Image, ImageDraw  # noqa: E402

_nums = [a for a in sys.argv[2:] if a.isdigit()]
NUM = int(_nums[0]) if _nums else 1
DEN = int(_nums[1]) if len(_nums) > 1 else 2

src = (ROOT / "saturn/src/video/display.c").read_text(encoding="utf-8", errors="replace")
head = (ROOT / "saturn/src/video/display.h").read_text(encoding="utf-8", errors="replace")


def table(name):
    m = re.search(name + r"\[[^\]]*\] = \{(.*?)\n\};", src, re.S)
    return [(int(r, 16), int(g, 16), int(b, 16)) for r, g, b in re.findall(
        r"DISP_RGB555\(0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2})\)", m.group(1))]


BG8, TEXT8 = table("BG_RGB"), table("TEXT_RGB")
presets = re.findall(r'\{ "([^"]*)",\s*(DISP_BG_\w+),\s*(DISP_TEXT_\w+)\s*\}',
                     re.search(r"PRESETS\[[^\]]*\] = \{(.*?)\n\};", src, re.S).group(1))
IDX = {}
for kind in ("DISP_BG_", "DISP_TEXT_"):
    seen = []
    for sym in re.findall(r"\b(%s\w+)\b" % kind, head):
        if sym.endswith("_N") or sym.endswith("_COLOR_N") or sym in seen:
            continue
        seen.append(sym)
    IDX[kind] = {s: i for i, s in enumerate(seen)}


def to555(c8):
    r, g, b = (v >> 3 for v in c8)
    return 0x8000 | (b << 10) | (g << 5) | r


def tint_palette(pal555, bg555):
    """write_palette() from dash_view.cxx, to the integer."""
    br, bg, bb = bg555 & 31, (bg555 >> 5) & 31, (bg555 >> 10) & 31
    peak = max(br, bg, bb)
    out = []
    for srcv in pal555:
        if srcv == 0:
            out.append(0)
            continue
        r, g, b = srcv & 31, (srcv >> 5) & 31, (srcv >> 10) & 31
        if peak:
            r = (r * ((DEN - NUM) * peak + NUM * br)) // (DEN * peak)
            g = (g * ((DEN - NUM) * peak + NUM * bg)) // (DEN * peak)
            b = (b * ((DEN - NUM) * peak + NUM * bb)) // (DEN * peak)
        out.append(0x8000 | (b << 10) | (g << 5) | r)
    return out


ROWS_TXT = ["1) Resume", "2) Display", "3) Gameplay", "4) Controls", "5) Restart"]

# The gamepad strip's own labels, as in preview_dash.py's in_context().
STRIP = ((4, 21, "N  NE"), (2, 22, "W  *  E"), (4, 23, "S  SW"),
         (17, 21, "A Accept   C Type"), (17, 22, "B Back     X Space"),
         (17, 23, "L/R Module  Y Swap"), (32, 21, "PANEL"), (32, 22, "Caps"),
         (32, 23, "Ins"))


def panel(pal555, bg8, ink8, sel=1):
    """A menu box AND the marbled gamepad strip, on this preset's ground.

    The box's bevel is laid over transparency and carries no marble at all --
    the field tiles are where the stone actually is -- so a tint preview that
    showed only the box would be judging the one part of the layer the texture
    does not reach.
    """
    pd.PAL555[:] = pal555
    pd.BG = tuple(bg8)
    pd.INK = tuple(ink8)
    dim = tuple((i * 5 + b * 3) // 8 for i, b in zip(ink8, bg8))
    s = pd.Screen()
    x0, y0, w, h = 9, 6, 22, 10
    s.box_tiles(x0, y0, w, h)
    s.puts(x0 + (w - 6) // 2, y0 + 1, "PAUSED")
    for i, r in enumerate(ROWS_TXT):
        s.puts(x0 + 6, y0 + 3 + i, r)
    s.panel_tiles(0, 19, 40, 9, divs=(14, 30))
    for cx, cy, txt in STRIP:
        s.puts(cx, cy, txt)
    img = s.render()
    # Repaint the menu rows at full/dim strength, since Screen.render uses one INK.
    d = ImageDraw.Draw(img)
    for i, r in enumerate(ROWS_TXT):
        col = ink8 if i == sel else dim
        for j, ch in enumerate(r):
            d.text(((x0 + 6 + j) * 8, (y0 + 3 + i) * 8 - 1), ch,
                   font=pd.MONO8, fill=tuple(col))
    return img


CHANGED_ONLY = "--changed" in sys.argv
ZOOM = 2 if CHANGED_ONLY else 1


def sheet():
    cards = []
    seen_bg = set()
    for name, bgsym, txsym in presets:
        bg8 = BG8[IDX["DISP_BG_"][bgsym]]
        tx8 = TEXT8[IDX["DISP_TEXT_"][txsym]]
        bg555 = to555(bg8)
        tinted = tint_palette(RAW, bg555)
        if CHANGED_ONLY:
            if tinted == list(RAW) or bgsym in seen_bg:
                continue          # a colourless ground, or a ground already shown
            seen_bg.add(bgsym)
        before = panel(list(RAW), bg8, tx8)
        after = panel(tinted, bg8, tx8)
        if ZOOM != 1:
            before = before.resize((before.width * ZOOM, before.height * ZOOM), Image.NEAREST)
            after = after.resize((after.width * ZOOM, after.height * ZOOM), Image.NEAREST)
        row = pd.hstack([pd.label(before, "grey marble (today)"),
                         pd.label(after, f"tinted {NUM}/{DEN} toward the ground")])
        cards.append(pd.label(row, name, f"background {bgsym[8:].lower()}, "
                                         f"text {txsym[10:].lower()}"))
    W = max(c.width for c in cards)
    H = sum(c.height + 18 for c in cards) + 18
    out = Image.new("RGB", (W, H), pd.PAPER)
    y = 18
    for c in cards:
        out.paste(c, ((W - c.width) // 2, y))
        y += c.height + 18
    return out


RAW = list(pd.PAL555)
outdir = pathlib.Path(sys.argv[1])
outdir.mkdir(parents=True, exist_ok=True)
p = outdir / f"marble-tint-{NUM}-{DEN}.png"
sheet().save(p)
print(p)
