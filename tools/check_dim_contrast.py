#!/usr/bin/env python3
"""Is brightness a usable selection marker, on each of the two backdrops?

The menus mark the selected row by drawing it at the player's text colour and
every other row in a dimmed ink (text_print_dim, CRAM entry TEXT_DIM_CRAM, mixed
five-eighths of the way from the background toward the text -- see text_dim_rgb
in options.cxx). The same treatment was tried for the command panel, the compass
rose and the on-screen keyboards and was NOT shipped, because those sit on the
NBG2 marble rather than on the flat back colour. This is the measurement that
decided it, kept so the question does not have to be re-derived.

Two numbers matter for any backdrop, and they do NOT want the same floor:

    dim/backdrop   can an unselected entry be read at all      -- READ_FLOOR
    text/dim       can the selected one be told apart          -- SEP_FLOOR

Reading a glyph against the surface behind it is the harder task and wants the
higher number. Telling two runs of the same glyphs apart, sitting side by side
in the same list, needs much less -- the eye is comparing two samples, not
resolving one against a ground. Using one floor for both marks the shipped menu
behaviour as failing, which it is not. WCAG ratios are the yardstick because
they are cheap and monotonic, not because a Saturn is a web page -- treat them
as ordering, not as a pass mark.

What it found, and why in-game selection stayed reverse video:

  * On the flat background the menus have room: every preset but one clears 2.48
    for legibility and 1.44 for separation. VIC-20 is the single failure (1.15
    and 1.08), and that preset's own text/background contrast is 1.25, so it has
    no room for ANY brightness cue -- nothing here can fix that one.
  * On the marble there is no room to divide. text/marble -- the whole headroom
    available -- is 2.09 or less on eight of the sixteen presets, because the
    marble's mid-tone sits BETWEEN the background and the text and eats the band
    the dim ink would need. Mixing the dim toward the marble instead of the
    background does not rescue it: at 5/8 nine presets fall under a floor, and
    pushing the strength either way only trades legibility for separation. The
    seven that do pass are the two blue grounds and the five black-text-on-light
    ones -- and those five are exactly the presets that had the black-on-black
    reverse-video bug, which is now fixed, so they are the ones that least need
    a different marker.

The only way to open that headroom would be to darken the marble's field band
under the panel, which changes the look of the stone; render it through
preview_dash_tint.py before believing it.

Reads the real tables -- display.c's presets, dash_tiles.c's palette and tile
data, and dash_view.cxx's tint arithmetic mirrored below -- so it cannot drift
from what the build ships.

Usage: python tools/check_dim_contrast.py
"""
import pathlib
import re
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parent.parent
DISPLAY_C = ROOT / "saturn/src/video/display.c"
DISPLAY_H = ROOT / "saturn/src/video/display.h"
TILES_C = ROOT / "saturn/src/video/dash_tiles.c"
MAP_H = ROOT / "saturn/src/video/dash_map.h"

# text_dim_rgb() in options.cxx: five parts ink to three parts backdrop.
DIM_NUM, DIM_DEN = 5, 8
# DASH_TINT_NUM / DASH_TINT_DEN in dash_view.cxx.
TINT_NUM, TINT_DEN = 1, 2
# A chunky bitmap font on a CRT. Reading the dim ink against what is behind it
# is the harder task; separating it from the selected ink beside it is easier.
READ_FLOOR = 1.6
SEP_FLOOR = 1.35

src = DISPLAY_C.read_text(encoding="utf-8", errors="replace")
head = DISPLAY_H.read_text(encoding="utf-8", errors="replace")
tiles_src = TILES_C.read_text(encoding="utf-8", errors="replace")
map_src = MAP_H.read_text(encoding="utf-8", errors="replace")


def colour_table(name):
    """A DISP_RGB555 table from display.c, as 5-bit (r, g, b) triples."""
    body = re.search(name + r"\[[^\]]*\] = \{(.*?)\n\};", src, re.S).group(1)
    return [(int(r, 16) >> 3, int(g, 16) >> 3, int(b, 16) >> 3) for r, g, b in re.findall(
        r"DISP_RGB555\(0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2})\)", body)]


def enum_index(kind):
    """DISP_BG_* / DISP_TEXT_* in declaration order, which is the table order."""
    seen = []
    for sym in re.findall(r"\b(%s\w+)\b" % kind, head):
        if sym.endswith("_N") or sym.endswith("_COLOR_N") or sym in seen:
            continue
        seen.append(sym)
    return {s: i for i, s in enumerate(seen)}


def field_band():
    """The palette indices the marble field is actually made of, and their weights.

    Taken from the DT_FIELD tiles' pixels rather than assumed, since the mid-tone
    the panel's text sits on is whatever those indices average to.
    """
    rows = re.findall(r"\{([^{}]*)\}", re.search(
        r"dash_tile_data\[\d+\]\[32\] = \{(.*)\n\};", tiles_src, re.S).group(1))
    data = [[int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", r)] for r in rows]

    order = []
    for n in re.findall(r"\b(DT_[A-Z0-9_]+)\b",
                        re.search(r"enum\s*\{(.*?)\}", map_src, re.S).group(1)):
        if n not in order:
            order.append(n)
    base = order.index("DT_FIELD0")

    hist = Counter()
    for t in range(base, base + 16):
        for byte in data[t]:
            hist[byte >> 4] += 1
            hist[byte & 15] += 1
    total = sum(hist.values())
    return {i: n for i, n in hist.items() if n / total >= 0.05}, hist, total


PALETTE = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", re.search(
    r"dash_palette\[16\] = \{(.*?)\};", tiles_src, re.S).group(1))]
BG_RGB, TEXT_RGB = colour_table("BG_RGB"), colour_table("TEXT_RGB")
BG_IDX, TEXT_IDX = enum_index("DISP_BG_"), enum_index("DISP_TEXT_")
PRESETS = re.findall(r'\{ "([^"]*)",\s*(DISP_BG_\w+),\s*(DISP_TEXT_\w+)\s*\}',
                     re.search(r"PRESETS\[[^\]]*\] = \{(.*?)\n\};", src, re.S).group(1))
BAND, HIST, HIST_TOTAL = field_band()


def unpack(rgb555):
    return rgb555 & 31, (rgb555 >> 5) & 31, (rgb555 >> 10) & 31


def mix(ink, backdrop, num=DIM_NUM, den=DIM_DEN):
    """text_dim_rgb(): `num`/`den` of the way from the backdrop to the ink."""
    return tuple((i * num + b * (den - num)) // den for i, b in zip(ink, backdrop))


def tint(rgb555, bg):
    """write_palette() in dash_view.cxx, to the integer."""
    peak = max(bg)
    ch = unpack(rgb555)
    if not peak:
        return ch
    return tuple((c * ((TINT_DEN - TINT_NUM) * peak + TINT_NUM * b)) // (TINT_DEN * peak)
                 for c, b in zip(ch, bg))


def marble_mid(bg):
    """The tinted field's weighted mid-tone -- what panel text actually sits on."""
    idx = sorted(BAND)
    cols = [tint(PALETTE[i], bg) for i in idx]
    wts = [BAND[i] for i in idx]
    return tuple(sum(c[k] * w for c, w in zip(cols, wts)) // sum(wts) for k in range(3))


def luminance(c):
    return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 31.0


def ratio(a, b):
    la, lb = luminance(a) + 0.05, luminance(b) + 0.05
    return max(la, lb) / min(la, lb)


def preset_colours(bgsym, txsym):
    return BG_RGB[BG_IDX[bgsym]], TEXT_RGB[TEXT_IDX[txsym]]


def report(title, backdrop_of, dim_of, note=""):
    """One table: legibility of the dim ink, and separation from the selection."""
    print(f"--- {title}")
    if note:
        print(f"    {note}")
    print(f"    {'preset':18} {'text/back':>9} {'dim/back':>9} {'text/dim':>9}")
    worst_read = worst_sep = 99.0
    bad = []
    for name, bgsym, txsym in PRESETS:
        bg, text = preset_colours(bgsym, txsym)
        back = backdrop_of(bg)
        dim = dim_of(text, bg, back)
        read, sep = ratio(dim, back), ratio(text, dim)
        worst_read, worst_sep = min(worst_read, read), min(worst_sep, sep)
        if read < READ_FLOOR or sep < SEP_FLOOR:
            bad.append(f"{name} ({'read' if read < READ_FLOOR else 'sep'})")
        print(f"    {name:18} {ratio(text,back):9.2f} {read:9.2f} {sep:9.2f}")
    print(f"    worst legibility {worst_read:.2f}, worst separation {worst_sep:.2f}")
    print(f"    under floor (read {READ_FLOOR}, sep {SEP_FLOOR}): "
          f"{', '.join(bad) if bad else 'none'}\n")


def main():
    print("marble field-tile palette indices (share of the field's pixels):")
    for i, n in sorted(HIST.items()):
        mark = "  <- band" if i in BAND else ""
        print(f"  index {i:2d}  {100.0 * n / HIST_TOTAL:5.1f}%{mark}")
    print()

    report("A menu row, over the flat back colour  [SHIPPED]",
           lambda bg: bg,
           lambda text, bg, back: mix(text, back),
           "This is what menu_row does. The dim is mixed toward the background,")

    report("A panel row, over the marble, dim mixed toward the BACKGROUND",
           marble_mid,
           lambda text, bg, back: mix(text, bg),
           "The naive port of the menu treatment: the ink is right for a menu box"
           " but\n    the text is not sitting on a menu box.")

    for num in (5, 6, 7):
        report(f"A panel row, over the marble, dim mixed {num}/8 toward the MARBLE",
               marble_mid,
               lambda text, bg, back, n=num: mix(text, back, n, 8),
               "Mixing toward what the text is really on. Still no strength that"
               " clears\n    the floor on both counts -- there is not enough headroom"
               " to divide.")


if __name__ == "__main__":
    main()
