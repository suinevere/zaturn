#!/usr/bin/env python3
"""Hold the background-art cache's constants against the art actually shipped.

The cache is filled in two places. splash_show() calls
display_preload_categories() around the still-resident boot jingle and before any
typeahead trie exists, so it is capped to a couple of slots and its real job is the
title's own house picture. display_warm_cache_random() at game start is the fill
that matters: both PCM cues are freed by then and main.cxx has already built the
game's trie, so it reads an honest free-space figure and spends the rest on art.

Three things here can fail silently:

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

  3. A full cache leaving the game's typeahead trie short. main.cxx builds the trie
     before warming the cache so the cache cannot take its space -- but the two
     still have to fit together, and a trie that cannot be built is a prompt with
     no completions rather than a crash. TRIE_RESERVE below is the figure to clear.

The online trie is deliberately absent from this budget: ensure_online_typeahead
only runs once the player has dialled, so an ordinary session never holds it.

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

# What the largest shipped game's typeahead trie wants from the same zone
# (~318 KB for Wishbringer, per the LWRAM box in title.cxx). Not a constant in the
# source -- it is a property of the vocabulary, measured rather than declared -- so
# it is written down here, where the budget it has to survive is computed.
TRIE_RESERVE = 318 * 1024

MSC_DIR = ROOT / "saturn" / "cd" / "data" / "MSC"


def pcm_bytes(name, cap):
    """What the loader will allocate for a cue: min(file size, cap)."""
    p = MSC_DIR / name
    return min(p.stat().st_size, cap) if p.is_file() else cap

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
    d.update(cdefines(SRC / "sound" / "loading_music.cxx"))

    need = ["LWRAM_TOTAL", "TGA_CACHE_SLOTS", "TGA_PLANE_MAX",
            "TGA_PAL_BYTES", "TGA_SLOT_BYTES", "TGA_CACHE_FLOOR",
            "BOOT_MUSIC_MAX_BYTES", "LOADING_MUSIC_MAX_BYTES"]
    missing = [n for n in need if n not in d]
    if missing:
        print(f"could not read {', '.join(missing)} from title.cxx", file=sys.stderr)
        sys.exit(1)

    lwram = d["LWRAM_TOTAL"]
    slots = d["TGA_CACHE_SLOTS"]
    plane = d["TGA_PLANE_MAX"]
    slot_bytes = d["TGA_SLOT_BYTES"]
    floor = d["TGA_CACHE_FLOOR"]
    # What each cue actually takes, not what its cap allows: both loaders allocate
    # min(file size, cap), so a cap the file does not reach is headroom rather than
    # residency. Missing files fall back to the cap, which is the safe direction.
    jingle = pcm_bytes("SPLASH.PCM", d["BOOT_MUSIC_MAX_BYTES"])
    cue = pcm_bytes("LOADCD.PCM", d["LOADING_MUSIC_MAX_BYTES"])
    resident_pcm = jingle + cue

    # Mirror display_preload_categories(): the splash fills around the still-resident
    # jingle, capped by its caller rather than by the zone.
    free_now, splash_slots = lwram - jingle, 0
    while splash_slots < slots and free_now >= slot_bytes + floor:
        free_now -= slot_bytes
        splash_slots += 1

    # Mirror display_warm_cache_random(): at game start both cues are freed and the
    # trie is already built, so the only reserve left is the floor.
    free_now, warm_slots = lwram - TRIE_RESERVE, 0
    while warm_slots < slots and free_now >= slot_bytes + floor:
        free_now -= slot_bytes
        warm_slots += 1

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
    print("  in-game floor       %8d" % floor)
    print("  game trie (measured)%8d" % TRIE_RESERVE)
    print("  ---------------------------")
    print("  total               %8d  of %d" % (resident + floor, lwram))
    print("  largest of %d TGAs   %8d  (%s), against a %d plane"
          % (len(art), biggest_span, biggest.name, plane))
    print("  boot jingle         %8d  (splash + title only)" % jingle)
    print("  loading cue         %8d  (during a load only)" % cue)
    print("  -> slots at the splash, beside the jingle: %d of %d"
          % (splash_slots, slots))
    print("  -> slots at game start, beside the trie:   %d of %d"
          % (warm_slots, slots))
    print("  -> full cache + largest trie: %8d  of %d"
          % (resident + TRIE_RESERVE, lwram))
    print("  -> jingle reload after cache release: %8d  of %d" % (jingle, lwram))

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

    if warm_slots < 1:
        print("\ntest_lwram_splash_budget: FAILED -- display_warm_cache_random can take "
              "no slots at all at game start,\nwith both cues freed and only the trie and "
              "the floor in its way. Every wallpaper change would then read the\ndisc "
              "over a playing track. Lower TGA_CACHE_FLOOR.", file=sys.stderr)
        fails += 1

    if jingle > lwram:
        print("\ntest_lwram_splash_budget: FAILED -- the jingle (%d bytes) does not fit "
              "in LWRAM even with the cache\nemptied, so a soft-reset return comes back "
              "to a silent title screen however much art\ntitle_bg_cache_release hands "
              "over. Lower BOOT_MUSIC_MAX_SECONDS." % jingle, file=sys.stderr)
        fails += 1

    if resident + TRIE_RESERVE > lwram:
        over = resident + TRIE_RESERVE - lwram
        print("\ntest_lwram_splash_budget: FAILED -- a full cache (%d) and the largest "
              "trie (%d) are %d bytes over LWRAM.\nThe cache is filled at game start and "
              "the trie is built on the first turn, so the cache wins and the trie\nis "
              "what goes short -- a prompt with no completions, and no error anywhere.\n"
              "Lower TGA_CACHE_SLOTS." % (resident, TRIE_RESERVE, over), file=sys.stderr)
        fails += 1

    if fails:
        sys.exit(1)

    print("\ntest_lwram_splash_budget: OK (%d bytes spare, %d bytes of plane headroom)"
          % (lwram - resident - floor, plane - biggest_span))


main()
