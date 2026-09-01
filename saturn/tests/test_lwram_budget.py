#!/usr/bin/env python3
"""Hold the Low Work RAM budget against the assets actually shipped.

One megabyte, and four claimants that overlap in pairs:

  * the boot jingle (SPLASH.PCM), resident from splash_show() until
    title_and_seed() stops it -- so across the whole title screen;
  * one CGL area archive plus its 76.8 KB decode target, held by room_art.cxx.
    The title screen shows a randomly picked frame from one of these, which is
    what puts it beside the jingle; in game it is beside the trie instead;
  * the running game's typeahead trie, built at game start and held for the
    session, and OITEM.CZ beside it on the one story that has item pictures,
    read at game start and held the same way;
  * the save/restore scratch, ~47 KB while a save is written;
  * the map's parchment (MAP.TGA), decoded at game start and held for the
    session so opening the map never touches the drive -- a seek stops the
    CD-DA track, and an unheld track is restarted from the top rather than
    resumed. It was in the C heap first; a session-long 78 KB there is 78 KB
    the next story image cannot have (tests/test_hwram_budget.py).

The pairs that actually coexist are jingle+archive (title screen) and
trie+archive+scratch (in game). Neither is checked anywhere at runtime in a way
a player would see: room_art's load_area refuses an archive that will not fit
and says nothing, so the failure mode is a title screen or a room with no
picture and no error -- which is exactly the kind of thing that gets shipped.

Four things here can fail silently:

  1. The largest archive not fitting beside the jingle. The title screen would
     then show no wallpaper whenever its random pick landed in that area, and
     would look fine the rest of the time.
  2. The largest archive not fitting beside the largest trie. Whole areas of a
     game would then draw no background, deterministically, from the first
     room in them.
  3. The jingle alone not fitting, which would come back to a silent title
     screen after a soft reset.
  4. A CGL archive whose declared frame offsets run past its own end -- a
     generator/injection mismatch, which room_art refuses one frame at a time
     and so shows up as scattered missing pictures rather than as a failure.

Run as a human-readable report: python saturn/tests/test_lwram_budget.py
Run as tests: pytest saturn/tests/test_lwram_budget.py
"""
import re
import sys
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"
BG_DIR = ROOT / "saturn" / "cd" / "data" / "BG"
MSC_DIR = ROOT / "saturn" / "cd" / "data" / "MSC"

# LWRAM's size. Not a constant in the source any more -- the cache box that
# declared it went with the TGA cache -- so it is written down here, where the
# budget it bounds is computed.
LWRAM_TOTAL = 1024 * 1024

# What the largest shipped game's typeahead trie wants from the same zone
# (~318 KB for Wishbringer). A property of the vocabulary, measured rather than
# declared, so it lives here rather than in the source.
TRIE_RESERVE = 318 * 1024

# The save/restore scratch, plus a margin. load_area's own headroom check asks
# for the archive, the frame and 4 KB; this is what has to survive on top of it
# for a save written mid-session not to fail.
SCRATCH_RESERVE = 64 * 1024

# What item_art_open demands of Low Work RAM before it will read the archive:
# the whole OITEM.CZ container (40,840), one decoded 64x80 picture
# (OITEM_PIC_BYTES, 5,120) and 4,096 bytes of slack. That is its literal gate --
# item_art.cxx asks for `bytes + OITEM_PIC_BYTES + 4096` -- so this models the
# refusal rather than the allocation. The 512-byte palette is NOT in it: g_clut
# is a static in item_art.cxx, not an LWRAM claim. Read once when the story is
# selected and held for the session, which does not change this figure: it was
# always checked against the whole in-game pairing -- trie + area archive +
# scratch -- because the window it used to be resident in sat inside all three.
ITEM_ART_RESERVE = 40840 + 5120 + 4096

# The map's parchment, decoded once at game start (map_view_preload) and held
# for the session so opening the map costs no disc access -- a seek would stop
# the CD-DA track, and an unheld track is restarted from the top rather than
# resumed. Measured off the shipped file the way the archives are, not declared:
# the pixel plane plus the leading partial sector tga_decode reads and shifts
# off, plus the 256-entry palette. tga_decode's own gate asks for 4096 more on
# top, and that slack is what this models, since a refusal is what would be seen.
#
# It is here rather than in the C heap on purpose. Held for a whole session it is
# 78 KB the next story image cannot have, and the heap is ~194 KB against stories
# of up to 129 KB -- see tests/test_hwram_budget.py.
PARCHMENT_RESERVE = None      # filled by compute_budget from the shipped TGA


def cdefines(path):
    """Every simple #define in a file, as {name: int}, resolved in order.

    The values are small arithmetic expressions over earlier defines, so they
    are evaluated rather than pattern-matched -- a regex for a bare integer
    silently read (320 * 240) as 320, which is the kind of wrong that would
    make this test pass while proving nothing.
    """
    out = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r"(?m)^\s*#define\s+([A-Z_][A-Z0-9_]*)\s+(.+)$", text):
        name, expr = m.group(1), m.group(2)
        expr = expr.split("/*")[0].split("//")[0].strip()
        expr = re.sub(r"\b(\d+)[uU][lL]?[lL]?\b", r"\1", expr)
        expr = re.sub(r"\(\s*(?:uint32_t|int32_t|unsigned int|int)\s*\)", "", expr)
        if not re.fullmatch(r"[0-9A-Z_+\-*/() \t]+", expr):
            continue
        try:
            out[name] = int(eval(expr, {"__builtins__": {}}, dict(out)))
        except Exception:
            continue
    return out


def pcm_bytes(name, cap):
    """What the loader will allocate for a cue: min(file size, cap)."""
    p = MSC_DIR / name
    return min(p.stat().st_size, cap) if p.is_file() else cap


def compute_budget():
    """Read the CGL geometry and the shipped archives, and derive every figure
    the checks below need. Raises RuntimeError on a setup problem rather than
    exiting, so a problem here fails whichever test needed it instead of the
    whole run."""
    d = cdefines(SRC / "video" / "cgl.h")
    d.update(cdefines(SRC / "sound" / "boot_music.cxx"))

    need = ["CGL_FRAME_BYTES", "BOOT_MUSIC_MAX_BYTES"]
    missing = [n for n in need if n not in d]
    if missing:
        raise RuntimeError(f"could not read {', '.join(missing)} from the source")

    frame = d["CGL_FRAME_BYTES"]
    # What the cue actually takes, not what its cap allows: the loader allocates
    # min(file size, cap), so a cap the file does not reach is headroom rather
    # than residency. A missing file falls back to the cap, the safe direction.
    jingle = pcm_bytes("SPLASH.PCM", d["BOOT_MUSIC_MAX_BYTES"])

    if not BG_DIR.is_dir():
        raise RuntimeError("no cd/data/BG to measure")
    archives = sorted(p for p in BG_DIR.iterdir()
                      if p.is_file() and p.suffix.upper() == ".CGL")
    if not archives:
        raise RuntimeError("no CGL archives found under cd/data/BG")

    biggest = max(archives, key=lambda p: p.stat().st_size)
    # load_area's own arithmetic: the archive, the decode target, and 4 KB.
    resident = biggest.stat().st_size + frame + 4096

    return {
        "frame": frame, "jingle": jingle, "archives": archives,
        "biggest": biggest, "resident": resident,
        "parchment": tga_gate("MAP.TGA"),
    }


def tga_gate(name, sector=2048):
    """What tga_decode demands be FREE in its zone before it will decode.

    Its literal gate -- span + palette + 4096, where span is the pixel plane plus
    the leading partial sector it reads and shifts off. Read off the shipped
    file's own header, the same way the archives above are measured, because a
    refusal is silent: the map simply draws on its back colour.
    """
    p = ROOT / "saturn" / "cd" / "data" / "TGA" / name
    if not p.is_file():
        raise RuntimeError(f"no {name} under cd/data/TGA to measure")
    hdr = p.read_bytes()[:18]
    idlen, cmaptype, imgtype = hdr[0], hdr[1], hdr[2]
    cmaplen, cmapbits = hdr[5] | (hdr[6] << 8), hdr[7]
    w, h = hdr[12] | (hdr[13] << 8), hdr[14] | (hdr[15] << 8)
    if cmaptype != 1 or imgtype != 1 or hdr[16] != 8:
        raise RuntimeError(f"{name} is not the 8bpp uncompressed paletted TGA "
                           "tga_decode accepts")
    pixoff = 18 + idlen + cmaplen * (cmapbits // 8)
    return (pixoff % sector) + w * h + 256 * 2 + 4096


@pytest.fixture(scope="module")
def budget():
    try:
        return compute_budget()
    except RuntimeError as e:
        # The area archives and the boot PCM are generated, not committed
        # (.gitignore), so a clean checkout cannot measure this budget at all.
        # Skipping says so; erroring says the budget is broken, which is a
        # different and untrue thing. The direct-script path below still exits
        # non-zero, since a person running it wants the setup problem named.
        pytest.skip(str(e))


def test_biggest_archive_fits_beside_the_jingle(budget):
    resident, jingle = budget["resident"], budget["jingle"]
    over = resident + jingle - LWRAM_TOTAL
    assert resident + jingle <= LWRAM_TOTAL, (
        f"{budget['biggest'].name} plus its decode target ({resident} bytes) does "
        f"not fit beside the boot jingle ({jingle}): {over} bytes "
        f"({over / 1024.0:.1f} KB) over LWRAM. The title screen picks its "
        "wallpaper from these archives while the jingle is still resident, so "
        "load_area would refuse this one and the title would show no picture "
        "whenever the random pick landed in that area -- silently, and only "
        "sometimes. Lower BOOT_MUSIC_MAX_SECONDS or split the archive.")


def test_biggest_archive_fits_beside_the_largest_trie(budget):
    resident = budget["resident"]
    need = resident + TRIE_RESERVE + SCRATCH_RESERVE
    over = need - LWRAM_TOTAL
    assert need <= LWRAM_TOTAL, (
        f"{budget['biggest'].name} plus its decode target ({resident}), the "
        f"largest trie ({TRIE_RESERVE}) and the save scratch ({SCRATCH_RESERVE}) "
        f"are {over} bytes over LWRAM. The trie is built first, so the archive "
        "is what goes short -- every room in that area draws no background, and "
        "nothing says so. Split the archive or trim the vocabulary.")


def test_item_pane_fits_beside_the_in_game_claimants(budget):
    """The pane opens mid-game, on top of the trie, the largest area archive
    and the save scratch. If it does not fit, item_art_open refuses and the
    pane is silently blank for the rest of the session -- which looks exactly
    like an unbound item."""
    resident = budget["resident"]
    need = resident + TRIE_RESERVE + SCRATCH_RESERVE + ITEM_ART_RESERVE
    over = need - LWRAM_TOTAL
    assert need <= LWRAM_TOTAL, (
        f"{budget['biggest'].name} plus its decode target ({resident}), the "
        f"largest trie ({TRIE_RESERVE}), the save scratch ({SCRATCH_RESERVE}) "
        f"and the item pane's own claim ({ITEM_ART_RESERVE}) are {over} bytes "
        "over LWRAM. The pane opens on top of all three, so it is what goes "
        "short, and item_art_open's refusal is silent -- the pane looks "
        "exactly like an item with no bound picture. Free something before "
        "the pane opens, or trim the item container.")


def test_parchment_fits_beside_the_in_game_claimants(budget):
    """The map's paper is read at game start and held for the session, so it
    sits on top of everything the game already has resident. A refusal is
    silent -- tga_decode returns false and the map draws on its tan back colour,
    which looks exactly like a disc whose MAP.TGA would not read."""
    need = (budget["resident"] + TRIE_RESERVE + SCRATCH_RESERVE
            + ITEM_ART_RESERVE + budget["parchment"])
    over = need - LWRAM_TOTAL
    assert need <= LWRAM_TOTAL, (
        f"{budget['biggest'].name} plus its decode target ({budget['resident']}), "
        f"the largest trie ({TRIE_RESERVE}), the save scratch ({SCRATCH_RESERVE}), "
        f"the item pane ({ITEM_ART_RESERVE}) and the map's parchment "
        f"({budget['parchment']}) are {over} bytes over LWRAM. The parchment is "
        "read last of these, so it is what goes short, and the map then draws on "
        "flat tan for the whole session with nothing said. Shrink MAP.TGA -- a "
        "tilemap of its repeated middle would cost a tenth of this -- or move it "
        "back out of this zone.")


def test_parchment_is_released_at_the_title(budget):
    """And it has to go when the game does. g_held is a plain static, so it
    survives the longjmp; left resident it comes out of the next game's trie,
    which is the largest thing in this zone and the one with no headroom check
    of its own -- the build ran the heap dry part-way and the allocations that
    failed were unchecked."""
    text = (SRC / "main.cxx").read_text(encoding="utf-8", errors="replace")
    lines = []
    for line in text.splitlines():
        s = line.split("//", 1)[0].strip()
        if s:
            lines.append(s)
    assert any("title_bg_drop_held()" in l for l in lines), (
        "main.cxx never calls title_bg_drop_held(), so the map's parchment "
        "stays resident across the title screen and the next game builds its "
        "trie into what is left. Release it beside room_art_release() and "
        "item_art_close(), which answer for the other two claimants here.")


def test_jingle_fits_lwram_alone(budget):
    jingle = budget["jingle"]
    assert jingle <= LWRAM_TOTAL, (
        f"the jingle ({jingle} bytes) does not fit in LWRAM even with every "
        "archive released, so a soft-reset return comes back to a silent title "
        "screen. Lower BOOT_MUSIC_MAX_SECONDS.")


def test_every_frame_lies_inside_its_archive():
    """room_art refuses a frame whose offset+length runs past the archive it was
    generated against, one frame at a time and without a word. Catch the
    mismatch here, where it can name the frame."""
    # The archives are generated, not committed, so a clean checkout has none and
    # every frame would be reported as running past an archive that is simply not
    # there -- 74 failures naming a problem that does not exist.
    if not BG_DIR.is_dir() or not any(p.suffix.upper() == ".CGL"
                                      for p in BG_DIR.iterdir() if p.is_file()):
        pytest.skip("no CGL archives in this checkout -- run tools/assets/bg.bat "
                    "to generate them before this can say anything")
    inc = SRC / "scene" / "game_presentation.inc"
    text = inc.read_text(encoding="utf-8", errors="replace")

    areas = re.search(r"PRES_AREA\s*\[[^\]]*\]\s*=\s*\{(.*?)\n\}\s*;", text, re.S)
    frames = re.search(r"IMAGE_FRAME\s*\[[^\]]*\]\s*=\s*\{(.*?)\n\}\s*;", text, re.S)
    if areas is None or frames is None:
        raise RuntimeError("could not read PRES_AREA / IMAGE_FRAME from "
                           f"{inc.relative_to(ROOT)}")

    stems = re.findall(r'"([A-Z0-9]+)"', areas.group(1))
    sizes = {}
    for i, stem in enumerate(stems):
        p = BG_DIR / f"{stem}.CGL"
        sizes[i] = p.stat().st_size if p.is_file() else None

    bad = []
    records = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)UL\s*,\s*(\d+)UL\s*\}",
                         frames.group(1))
    for n, (area, off, length) in enumerate(records, start=1):
        area, off, length = int(area), int(off), int(length)
        size = sizes.get(area)
        if size is None:
            bad.append(f"frame {n}: area {area} has no archive on the disc")
        elif off + length > size:
            bad.append(f"frame {n}: {stems[area]}.CGL is {size} bytes but the "
                       f"frame ends at {off + length}")

    assert records, "IMAGE_FRAME parsed as empty -- the table's shape changed"
    assert not bad, (
        "the presentation table and the injected archives disagree:\n  "
        + "\n  ".join(bad)
        + "\nroom_art refuses these frames one at a time and says nothing, so "
          "the symptom is scattered rooms with no picture. Re-run "
          "tools/gen_presentation.py and tools/assets/bg.bat together.")


def _print_report(b):
    print("  LWRAM                    %8d" % LWRAM_TOTAL)
    print("  boot jingle              %8d  (splash + title only)" % b["jingle"])
    print("  largest of %2d archives   %8d  (%s)"
          % (len(b["archives"]), b["biggest"].stat().st_size, b["biggest"].name))
    print("  + decode target + 4K     %8d  (what load_area asks for)" % b["resident"])
    print("  game trie (measured)     %8d" % TRIE_RESERVE)
    print("  save scratch             %8d" % SCRATCH_RESERVE)
    print("  ------------------------------------")
    print("  title:  archive + jingle          %8d  of %d"
          % (b["resident"] + b["jingle"], LWRAM_TOTAL))
    print("  map parchment            %8d  (held for the session)" % b["parchment"])
    print("  game:   archive + trie + scratch  %8d  of %d"
          % (b["resident"] + TRIE_RESERVE + SCRATCH_RESERVE, LWRAM_TOTAL))
    print("  game:   + item pane + parchment   %8d  of %d"
          % (b["resident"] + TRIE_RESERVE + SCRATCH_RESERVE + ITEM_ART_RESERVE
             + b["parchment"], LWRAM_TOTAL))


def main():
    """Human-readable report plus a hard pass/fail, for the direct-script entry
    point named in the module docstring. The pytest functions above carry the
    same checks for CI; this exists so `python test_lwram_budget.py` still
    prints the numbers a person reading it wants to see."""
    try:
        b = compute_budget()
    except RuntimeError as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)

    _print_report(b)

    checks = [
        (lambda: test_biggest_archive_fits_beside_the_jingle(b),
         "biggest archive fits beside the jingle"),
        (lambda: test_biggest_archive_fits_beside_the_largest_trie(b),
         "biggest archive fits beside the largest trie"),
        (lambda: test_parchment_fits_beside_the_in_game_claimants(b),
         "parchment fits beside the in-game claimants"),
        (lambda: test_parchment_is_released_at_the_title(b),
         "parchment released at the title"),
        (lambda: test_jingle_fits_lwram_alone(b), "jingle fits LWRAM alone"),
        (test_every_frame_lies_inside_its_archive,
         "every frame lies inside its archive"),
    ]
    fails = 0
    for fn, label in checks:
        try:
            fn()
        except AssertionError as e:
            print(f"\ntest_lwram_budget: FAILED -- {label}\n{e}", file=sys.stderr)
            fails += 1

    if fails:
        sys.exit(1)

    print("\ntest_lwram_budget: OK (%d bytes spare on the tightest pairing)"
          % (LWRAM_TOTAL - max(b["resident"] + b["jingle"],
                               b["resident"] + TRIE_RESERVE + SCRATCH_RESERVE
                               + ITEM_ART_RESERVE + b["parchment"])))


if __name__ == "__main__":
    main()
