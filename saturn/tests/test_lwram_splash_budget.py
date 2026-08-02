#!/usr/bin/env python3
"""Hold the background-art cache's constants against the art actually shipped.

This used to assert that the splash jingle and the WHOLE art cache fitted in Low
Work RAM at once, because splash.cxx loaded the jingle first and freed it last
with display_preload_images() running in between. Neither half of that is true
any more: the preload is gone (thirty-seven pictures will not fit a 1 MB zone at
~72 KB each), and title.cxx now keeps TGA_CACHE_SLOTS pictures filled on demand,
allocated after splash_show_once() has already called boot_music_stop().

The preload is back, but it is bounded rather than total: splash_show_once() calls
display_preload_categories() at peak brightness, which fills slots with one picture
per category and stops as soon as tga_cache_slot refuses to grow. It runs while the
boot jingle is still resident, so the jingle's ~409 KB is what decides how many get
in -- and if that number ever reaches zero the preload silently becomes a no-op and
the title screen goes back to reading the disc for its own picture.

What can still fail silently is different, and there are three of them:

  1. A picture whose read span exceeds TGA_PLANE_MAX can never take a cache slot.
     tga_decode refuses it -- correctly, since a short plane would render as a
     band of the previous picture -- and title_bg_show quietly falls through to
     the one-off High Work RAM path. The picture displays perfectly. It just
     reads the CD every single time it is shown, and a CD read stops CD-DA, so
     the symptom is that one room mood makes the music stutter and no other does.

  2. TGA_CACHE_SLOTS slots plus TGA_CACHE_FLOOR not fitting the zone. The cache
     would then stop growing before reaching its own stated slot count, so the
     constant would be describing a cache that cannot exist, and eviction would
     start thrashing earlier than anyone reading title.cxx would expect.

  3. The jingle growing until no cache slot can be granted beside it. The preload
     would still be called, still walk every category, and still cache nothing --
     no error, no log, just a title screen that reads the CD again and a splash
     that is over as soon as the trie is read.

None of the three shows up as an error at runtime, which is why they need a test
rather than a comment.

Run: python saturn/tests/test_lwram_splash_budget.py
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"
TGA_DIR = ROOT / "saturn" / "cd" / "data" / "TGA"

# The boot splash logo goes through title_bg_show_oneoff, which never touches
# the cache, so it is not held to the slot capacity.
SPLASH_LOGO = "SUINE.TGA"

# tga_decode() expands the file's colour map to a fixed 256-entry HighColor
# palette alongside the pixel plane.
HIGHCOLOR_BYTES = 2


def cdefines(path):
    """Every simple #define in a file, as {name: int}, resolved in order.

    The values are small arithmetic expressions over earlier defines
    (TGA_SLOT_BYTES is TGA_PLANE_MAX + TGA_PAL_BYTES), so they are evaluated
    rather than pattern-matched -- a regex for a bare integer silently read
    (320u * 224u + 2048u) as 320, which is the kind of wrong that would make
    this test pass while proving nothing.
    """
    out = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r"(?m)^\s*#define\s+([A-Z_][A-Z0-9_]*)\s+(.+)$", text):
        name, expr = m.group(1), m.group(2)
        expr = expr.split("/*")[0].split("//")[0].strip()
        # C spellings this evaluator has to understand to reach the constants
        # it cares about; anything else is skipped rather than guessed at.
        expr = expr.replace("sizeof(SRL::Types::HighColor)", str(HIGHCOLOR_BYTES))
        expr = re.sub(r"\b(\d+)[uU][lL]?[lL]?\b", r"\1", expr)
        expr = re.sub(r"\(\s*(?:uint32_t|int32_t|unsigned int|int)\s*\)", "", expr)
        if not re.fullmatch(r"[0-9A-Z_+\-*/() \t]+", expr):
            continue
        try:
            out[name] = int(eval(expr, {"__builtins__": {}}, dict(out)))
        except Exception:
            continue
    return out


def main():
    title = SRC / "video" / "title.cxx"
    d = cdefines(title)
    d.update(cdefines(SRC / "sound" / "boot_music.cxx"))

    need = ["LWRAM_TOTAL", "TGA_CACHE_SLOTS", "TGA_PLANE_MAX",
            "TGA_PAL_BYTES", "TGA_SLOT_BYTES", "TGA_CACHE_FLOOR",
            "BOOT_MUSIC_MAX_BYTES"]
    missing = [n for n in need if n not in d]
    if missing:
        print(f"could not read {', '.join(missing)} from title.cxx", file=sys.stderr)
        sys.exit(1)

    lwram = d["LWRAM_TOTAL"]
    slots = d["TGA_CACHE_SLOTS"]
    plane = d["TGA_PLANE_MAX"]
    slot_bytes = d["TGA_SLOT_BYTES"]
    floor = d["TGA_CACHE_FLOOR"]
    jingle = d["BOOT_MUSIC_MAX_BYTES"]

    # Mirror tga_cache_slot(): grow only while free >= one slot + the floor.
    free_now, grantable = lwram - jingle, 0
    while grantable < slots and free_now >= slot_bytes + floor:
        free_now -= slot_bytes
        grantable += 1

    if not TGA_DIR.is_dir():
        print("no cd/data/TGA to measure", file=sys.stderr)
        sys.exit(1)
    art = sorted(p for p in TGA_DIR.iterdir()
                 if p.suffix.upper() == ".TGA" and p.name.upper() != SPLASH_LOGO)
    if not art:
        print("no background art found in cd/data/TGA", file=sys.stderr)
        sys.exit(1)

    # span == the file's byte length: pixoff lands inside the first sector, so
    # tga_decode's leading-partial-sector skip puts the read span back on the
    # file size exactly.
    biggest = max(art, key=lambda p: p.stat().st_size)
    biggest_span = biggest.stat().st_size

    resident = slots * slot_bytes
    print("  LWRAM               %8d" % lwram)
    print("  %d cache slots       %8d  (%d each: %d plane + %d palette)"
          % (slots, resident, slot_bytes, plane, d["TGA_PAL_BYTES"]))
    print("  reserved floor      %8d" % floor)
    print("  ---------------------------")
    print("  total               %8d  of %d" % (resident + floor, lwram))
    print("  largest of %d TGAs   %8d  (%s), against a %d plane"
          % (len(art), biggest_span, biggest.name, plane))
    print("  boot jingle         %8d" % jingle)
    print("  -> slots grantable beside the jingle: %d of %d" % (grantable, slots))

    fails = 0

    if biggest_span > plane:
        print("\ntest_lwram_splash_budget: FAILED -- %s needs %d bytes but a cache "
              "slot's plane is %d.\nIt can never be cached: tga_decode refuses it "
              "and title_bg_show falls back to the one-off\npath, so that picture "
              "reads the CD every time it is shown and stops the music every time.\n"
              "Raise TGA_PLANE_MAX in title.cxx or ship the picture at 320x224."
              % (biggest.name, biggest_span, plane), file=sys.stderr)
        fails += 1

    if resident + floor > lwram:
        over = resident + floor - lwram
        print("\ntest_lwram_splash_budget: FAILED -- %d slots plus the floor are %d "
              "bytes (%.1f KB) over LWRAM.\nThe cache would stop growing before "
              "reaching TGA_CACHE_SLOTS, so that constant would be describing a\n"
              "cache that cannot exist. Lower TGA_CACHE_SLOTS or TGA_CACHE_FLOOR "
              "in title.cxx." % (slots, over, over / 1024.0), file=sys.stderr)
        fails += 1

    if grantable < 1:
        print("\ntest_lwram_splash_budget: FAILED -- the boot jingle (%d bytes) leaves "
              "no room for even one cache slot, so display_preload_categories()\n"
              "during the splash caches nothing and the title screen reads the disc "
              "for its own picture again.\nLower BOOT_MUSIC_MAX_SECONDS or "
              "TGA_CACHE_FLOOR." % jingle, file=sys.stderr)
        fails += 1

    if fails:
        sys.exit(1)

    print("\ntest_lwram_splash_budget: OK (%d bytes spare, %d bytes of plane headroom)"
          % (lwram - resident - floor, plane - biggest_span))


main()
