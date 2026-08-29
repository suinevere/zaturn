#!/usr/bin/env python3
"""Hold the background-art cache's constants against the art actually shipped.

The cache is filled in two places. splash_show() calls title_preload_art(),
walking the shared TITLE/ pictures (TITLE_ART_N of them) around the
still-resident boot jingle and before any typeahead trie exists, so it is
capped to a couple of slots and its real job is the title's own picture.
In game the cache fills on demand, one picture per room that asks for one, and
that is the fill that matters: the jingle is freed by then and main.cxx has
already built the game's trie, so what is left of the zone is what the art
actually gets.

Five things here can fail silently:

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

  3. The cache holding zero slots in game. Every wallpaper change would then
     re-read the disc, however recently that picture was shown.

  4. The boot jingle alone not fitting LWRAM even with the art cache emptied,
     which would come back to a silent title screen after a soft reset.

  5. A full cache leaving the game's typeahead trie short. main.cxx builds the trie
     before warming the cache so the cache cannot take its space -- but the two
     still have to fit together, and a trie that cannot be built is a prompt with
     no completions rather than a crash. TRIE_RESERVE below is the figure to clear.

The online trie is deliberately absent from this budget: ensure_online_typeahead
only runs once the player has dialled, so an ordinary session never holds it.

Art is walked recursively under cd/data/TGA: every background lives in a
per-game subfolder (cd/data/TGA/<GAME>/NN.TGA), and only the splash logo
(SUINE.TGA) still sits at the TGA root.

Run as a human-readable report: python saturn/tests/test_lwram_splash_budget.py
Run as tests: pytest saturn/tests/test_lwram_splash_budget.py
"""
import re
import sys
import pathlib

import pytest

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


def compute_budget():
    """Read title.cxx's cache constants and the shipped art, and derive every
    figure the checks below need. Raises RuntimeError on a setup problem
    (missing constants, missing/empty art directory) rather than exiting, so a
    problem here fails whichever test needed it instead of the whole run."""
    title = SRC / "video" / "title.cxx"
    d = cdefines(title)
    d.update(cdefines(SRC / "sound" / "boot_music.cxx"))

    need = ["LWRAM_TOTAL", "TGA_CACHE_SLOTS", "TGA_PLANE_MAX",
            "TGA_PAL_BYTES", "TGA_SLOT_BYTES", "TGA_CACHE_FLOOR",
            "BOOT_MUSIC_MAX_BYTES"]
    missing = [n for n in need if n not in d]
    if missing:
        raise RuntimeError(f"could not read {', '.join(missing)} from title.cxx")

    lwram = d["LWRAM_TOTAL"]
    slots = d["TGA_CACHE_SLOTS"]
    plane = d["TGA_PLANE_MAX"]
    slot_bytes = d["TGA_SLOT_BYTES"]
    floor = d["TGA_CACHE_FLOOR"]
    # What the cue actually takes, not what its cap allows: the loader allocates
    # min(file size, cap), so a cap the file does not reach is headroom rather than
    # residency. A missing file falls back to the cap, which is the safe direction.
    jingle = pcm_bytes("SPLASH.PCM", d["BOOT_MUSIC_MAX_BYTES"])

    # Mirror title_preload_art(): the splash walks TITLE_ART_N shared TITLE/
    # pictures around the still-resident jingle, stopping at whichever of
    # TGA_CACHE_SLOTS or the zone's free space runs out first -- a no-op at
    # TITLE_ART_N 0, but the slot arithmetic below is unchanged either way,
    # since it caps at TGA_CACHE_SLOTS regardless of how many pictures exist
    # to walk.
    free_now, splash_slots = lwram - jingle, 0
    while splash_slots < slots and free_now >= slot_bytes + floor:
        free_now -= slot_bytes
        splash_slots += 1

    # At game start the jingle is freed and the
    # trie is already built, so the only reserve left is the floor.
    free_now, game_slots = lwram - TRIE_RESERVE, 0
    while game_slots < slots and free_now >= slot_bytes + floor:
        free_now -= slot_bytes
        game_slots += 1

    if not TGA_DIR.is_dir():
        raise RuntimeError("no cd/data/TGA to measure")
    # Recursive: every background lives in a per-game subfolder
    # (cd/data/TGA/<GAME>/NN.TGA); only the splash logo sits at
    # the TGA root. A flat iterdir() here would silently see zero art.
    art = sorted(p for p in TGA_DIR.rglob("*")
                 if p.is_file() and p.suffix.upper() == ".TGA"
                 and p.name.upper() != SPLASH_LOGO)
    if not art:
        raise RuntimeError("no background art found under cd/data/TGA")

    # span == the file's byte length: pixoff lands inside the first sector, so
    # tga_decode's leading-partial-sector skip puts the read span back on the
    # file size exactly.
    biggest = max(art, key=lambda p: p.stat().st_size)
    biggest_span = biggest.stat().st_size
    resident = slots * slot_bytes

    return {
        "lwram": lwram, "slots": slots, "plane": plane, "slot_bytes": slot_bytes,
        "floor": floor, "pal_bytes": d["TGA_PAL_BYTES"], "jingle": jingle,
        "splash_slots": splash_slots, "game_slots": game_slots, "art": art,
        "biggest": biggest, "biggest_span": biggest_span, "resident": resident,
    }


@pytest.fixture(scope="module")
def budget():
    return compute_budget()


def test_biggest_picture_fits_a_cache_slot(budget):
    biggest, biggest_span, plane = budget["biggest"], budget["biggest_span"], budget["plane"]
    assert biggest_span <= plane, (
        f"{biggest.name} needs {biggest_span} bytes but a cache slot's plane is "
        f"{plane}. It can never be cached: tga_decode refuses it and title_bg_show "
        "falls back to the one-off path, so that picture reads the CD every time it "
        "is shown and stops the music every time. Raise TGA_PLANE_MAX in title.cxx "
        "or ship the picture at 320x224.")


def test_cache_slots_fit_within_lwram(budget):
    resident, floor, lwram, slots = (budget["resident"], budget["floor"],
                                      budget["lwram"], budget["slots"])
    over = resident + floor - lwram
    assert resident + floor <= lwram, (
        f"{slots} slots plus the floor are {over} bytes ({over / 1024.0:.1f} KB) "
        "over LWRAM. The cache would stop growing before reaching TGA_CACHE_SLOTS, "
        "so that constant would be describing a cache that cannot exist. Lower "
        "TGA_CACHE_SLOTS or TGA_CACHE_FLOOR in title.cxx.")


def test_cache_can_take_at_least_one_slot_in_game(budget):
    assert budget["game_slots"] >= 1, (
        "the art cache can hold nothing at all in game, with the jingle freed "
        "and only the trie and the floor in its way. Every wallpaper change would "
        "then re-read the disc, however recently that picture was shown. Lower "
        "TGA_CACHE_FLOOR.")


def test_jingle_fits_lwram_alone(budget):
    jingle, lwram = budget["jingle"], budget["lwram"]
    assert jingle <= lwram, (
        f"the jingle ({jingle} bytes) does not fit in LWRAM even with the cache "
        "emptied, so a soft-reset return comes back to a silent title screen "
        "however much art title_bg_cache_release hands over. Lower "
        "BOOT_MUSIC_MAX_SECONDS.")


def test_full_cache_and_largest_trie_fit_lwram(budget):
    resident, lwram = budget["resident"], budget["lwram"]
    over = resident + TRIE_RESERVE - lwram
    assert resident + TRIE_RESERVE <= lwram, (
        f"a full cache ({resident}) and the largest trie ({TRIE_RESERVE}) are "
        f"{over} bytes over LWRAM. The cache is filled at game start and the trie "
        "is built on the first turn, so the cache wins and the trie is what goes "
        "short -- a prompt with no completions, and no error anywhere. Lower "
        "TGA_CACHE_SLOTS.")


def _print_report(b):
    lwram, slots, plane, slot_bytes, floor = (
        b["lwram"], b["slots"], b["plane"], b["slot_bytes"], b["floor"])
    resident = b["resident"]
    print("  LWRAM               %8d" % lwram)
    print("  %d cache slots       %8d  (%d each: %d plane + %d palette)"
          % (slots, resident, slot_bytes, plane, b["pal_bytes"]))
    print("  in-game floor       %8d" % floor)
    print("  game trie (measured)%8d" % TRIE_RESERVE)
    print("  ---------------------------")
    print("  total               %8d  of %d" % (resident + floor, lwram))
    print("  largest of %d TGAs   %8d  (%s), against a %d plane"
          % (len(b["art"]), b["biggest_span"], b["biggest"].name, plane))
    print("  boot jingle         %8d  (splash + title only)" % b["jingle"])
    print("  -> slots at the splash, beside the jingle: %d of %d"
          % (b["splash_slots"], slots))
    print("  -> slots in game, beside the trie:        %d of %d"
          % (b["game_slots"], slots))
    print("  -> full cache + largest trie: %8d  of %d"
          % (resident + TRIE_RESERVE, lwram))
    print("  -> jingle reload after cache release: %8d  of %d" % (b["jingle"], lwram))


def main():
    """Human-readable report plus a hard pass/fail, for the direct-script entry
    point named in the module docstring. The pytest functions above carry the
    same checks for CI; this exists so `python test_lwram_splash_budget.py`
    still prints the numbers a person reading it wants to see."""
    try:
        b = compute_budget()
    except RuntimeError as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)

    _print_report(b)

    checks = [
        (test_biggest_picture_fits_a_cache_slot, "biggest picture fits a cache slot"),
        (test_cache_slots_fit_within_lwram, "cache slots fit within LWRAM"),
        (test_warm_cache_can_take_at_least_one_slot, "warm cache can take a slot"),
        (test_jingle_fits_lwram_alone, "jingle fits LWRAM alone"),
        (test_full_cache_and_largest_trie_fit_lwram, "full cache + trie fit LWRAM"),
    ]
    fails = 0
    for fn, label in checks:
        try:
            fn(b)
        except AssertionError as e:
            print(f"\ntest_lwram_splash_budget: FAILED -- {label}\n{e}", file=sys.stderr)
            fails += 1

    if fails:
        sys.exit(1)

    print("\ntest_lwram_splash_budget: OK (%d bytes spare, %d bytes of plane headroom)"
          % (b["lwram"] - b["resident"] - b["floor"], b["plane"] - b["biggest_span"]))


if __name__ == "__main__":
    main()
