#!/usr/bin/env python3
"""The category -> picture table in display.c must describe the actual disc.

A name with no file behind it is invisible at runtime: display.c resolves it to
"no slot", display_set_dynamic_category holds the previous picture, and that room
mood simply never gets its art. Nothing about it shows on screen -- the wallpaper
just stops changing for one category -- which is the same class of silent failure
test_lwram_splash_budget.py exists to catch.

Four properties, all of them data rather than logic, which is why they live in a
script that reads the table instead of in the C tests that exercise it:

  present   every name is a file in cd/data/TGA
  rotatable every pool has at least two pictures, or the room-count rotation
            moves the track under an unchanged wallpaper
  disjoint  no picture appears in two pools -- rotating onto art the player was
            just looking at in another mood reads as the rotation being broken
  named     every name follows <FOLDER truncated to 7><1..N>.TGA, which is what
            ties a flat disc file back to the category that owns it

Run: python saturn/tests/test_category_art.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "video" / "display.c"
TGA = ROOT / "cd" / "data" / "TGA"
PNG = ROOT.parent / "tools" / "assets" / "png"

# The boot splash logo is not a room background and is deliberately skipped
# by the TGA scan in title.cxx, so it is not a legal target for the table either.
NOT_A_BACKGROUND = {"SUINE.TGA"}

# title.cxx stops registering after this many files, in ISO9660 (alphabetical)
# order, so a folder past it has a silently unreachable tail.
DISP_IMAGE_MAX = 40

NAME_RE = re.compile(r"^([A-Z]{2,7})([0-9]+)\.TGA$")


def pools(text):
    """Each category's picture pool, as {IMG_NAME: [files]}, in table order."""
    found = {}
    for m in re.finditer(
        r"static\s+const\s+char\s*\*\s*const\s+(IMG_\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};",
        text, re.S,
    ):
        found[m.group(1)] = re.findall(r'"([^"]+)"', m.group(2))
    return found


def main():
    text = SRC.read_text(encoding="utf-8", errors="replace")
    if not re.search(r"CATEGORY_IMAGE\s*\[[^\]]*\]\s*=\s*\{", text):
        print("FAIL: no CATEGORY_IMAGE table in display.c")
        return 1

    pool = pools(text)
    if not pool:
        print("FAIL: no IMG_* picture pools in display.c")
        return 1

    names = [f for files in pool.values() for f in files]
    if not names:
        print("FAIL: the picture pools name no pictures at all")
        return 1

    fails = 0

    # A pool that cannot rotate leaves the track changing under a fixed picture,
    # which is indistinguishable from the art side being broken.
    thin = sorted("%s(%d)" % (k, len(v)) for k, v in pool.items() if len(v) < 2)
    if thin:
        print(f"FAIL: pool(s) with fewer than two pictures: {', '.join(thin)}")
        fails += 1

    # Sharing a picture between two moods defeats the rotation and would also let
    # one CD read serve two categories, hiding a cache problem behind a fluke.
    owner = {}
    shared = []
    for cat, files in pool.items():
        for f in files:
            if f in owner and owner[f] != cat:
                shared.append(f"{f} in both {owner[f]} and {cat}")
            elif f in owner:
                shared.append(f"{f} twice in {cat}")
            owner[f] = cat
    if shared:
        print("FAIL: pictures claimed more than once:")
        for s in sorted(shared):
            print(f"       {s}")
        fails += 1

    # The naming rule is what makes a flat disc legible: without it, a file in
    # /TGA gives no clue which mood it belongs to.
    if PNG.is_dir():
        stems = {d.name.upper()[:7] for d in PNG.iterdir() if d.is_dir()}
        stray = []
        for n in sorted(set(names)):
            m = NAME_RE.match(n)
            if not m:
                stray.append(f"{n}: not <STEM><N>.TGA")
            elif m.group(1) not in stems:
                stray.append(f"{n}: stem {m.group(1)} matches no folder under "
                             f"tools/assets/png")
        if stray:
            print("FAIL: names that do not follow the folder rule:")
            for s in stray:
                print(f"       {s}")
            fails += 1

    if not TGA.is_dir():
        print(f"FAIL: no TGA folder at {TGA}")
        return 1

    present = {p.name.upper() for p in TGA.iterdir() if p.suffix.upper() == ".TGA"}
    selectable = present - NOT_A_BACKGROUND

    missing = sorted(n for n in set(names) if n.upper() not in present)
    if missing:
        print(f"FAIL: named in display.c but not in cd/data/TGA: {', '.join(missing)}")
        fails += 1

    # A category pointing at the boot logo would compile and resolve to nothing,
    # since the scan never registers it.
    logo = sorted(n for n in set(names) if n.upper() in NOT_A_BACKGROUND)
    if logo:
        print(f"FAIL: {', '.join(logo)} is not a room background")
        fails += 1

    # The scan stops at DISP_IMAGE_MAX in alphabetical order, so it is the TAIL of
    # the folder that vanishes -- not the newest file, and with no error either way.
    if len(present) > DISP_IMAGE_MAX:
        lost = sorted(present)[DISP_IMAGE_MAX:]
        print(f"FAIL: {len(present)} TGAs but only {DISP_IMAGE_MAX} register; "
              f"unreachable: {', '.join(lost)}")
        print("      raise DISP_IMAGE_MAX in saturn/src/video/display.h")
        fails += 1

    # A fallback naming a category with no pictures would be invisible at
    # runtime -- display_set_dynamic_category resolves it to "no slot" and holds
    # the previous picture, which is exactly the behaviour the fallback exists to
    # replace. Same class of silent failure as the checks above.
    DATA = ROOT / "src" / "classify" / "room_class_data.c"
    if DATA.is_file():
        data = DATA.read_text(encoding="utf-8", errors="replace")
        table = re.search(r"GAME_GENRE\[\]\s*=\s*\{(.*?)\n\};", data, re.S)
        if not table:
            print("FAIL: no GAME_GENRE table in room_class_data.c")
            fails += 1
        else:
            rows = re.findall(r"\{\s*\d+,\s*\"\d+\",\s*[^,]+,\s*(TC_\w+)\s*\}",
                              table.group(1))
            if not rows:
                print("FAIL: GAME_GENRE rows carry no fallback column")
                fails += 1
            bad = []
            for cat in sorted(set(rows)):
                if cat == "TC_NEUTRAL":
                    continue
                img = "IMG_" + cat[len("TC_"):]
                if img not in pool or not pool[img]:
                    bad.append(f"{cat}: no pictures in {img}")
            if bad:
                print("FAIL: fallback categories with no picture pool:")
                for b in bad:
                    print(f"       {b}")
                fails += 1

    if fails:
        return 1

    unused = sorted(selectable - {n.upper() for n in names})
    print(f"test_category_art: OK ({len(set(names))} pictures across "
          f"{len(pool)} categories, all pools disjoint and rotatable)")
    if unused:
        print(f"  no category claims: {', '.join(unused)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
