#!/usr/bin/env python3
"""Hold the High Work RAM budget against the stories actually shipped.

HWRAM is the C heap -- malloc/free/new in saturn_compat.cxx route there -- and
it is not a megabyte. sgl.linker puts __heap_start immediately after .bss and
__heap_end at the SGL work area (0x060C0000), so the heap is whatever the
program image leaves behind: about 194 KB in the shipped build. The single
largest thing in it is the running story image, and the shipped stories run
from 47 KB (MZORKI) to 129 KB (LURKING, the Lurking Horror).

That leaves no room for two of them, nor for one of them beside the map's held
parchment, which title_bg_hold keeps decoded in the same heap for the rest of
the run. The rule this file exists to hold is:

  nothing a finished session allocated may still be resident when the next
  story is read -- the story image itself, and the held parchment beside it.

mojozork's initStory frees the outgoing image, but only after main.cxx has
already allocated the incoming one -- so on its own it makes a game switch
demand both at once. soft_reset_to_title() is what closes that window, by
releasing the outgoing image on the way back to the title screen, where the
heap then reads as it does on a cold boot.

Without that release the failure is silent and looks like a hang: the story
malloc returns NULL, main's retry loop spends 300 attempts x 8 fields under a
held-black fade -- roughly forty seconds of black screen -- and only then says
"Could not load". ZORK1 -> HORROR is the pair that cannot ever fit; the pairs
that do fit leave the outgoing image stranded mid-heap and fragment it, so the
switch after that one fails instead.

Run as a human-readable report: python saturn/tests/test_hwram_budget.py
Run as tests: pytest saturn/tests/test_hwram_budget.py
"""
import re
import sys
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"
MAP_FILE = (ROOT / "saturn" / "BuildDrop"
            / "Zaturn (USA) (Netlink Edition).map")


def heap_bytes():
    """The HWRAM C heap, measured off the link map.

    __heap_start moves with .bss, so this is a property of the build and not a
    constant anything declares. Returns None when there is no map to read --
    BuildDrop is not committed, so a clean checkout has none.
    """
    if not MAP_FILE.is_file():
        return None
    text = MAP_FILE.read_text(encoding="utf-8", errors="replace")
    start = re.search(r"(?m)^\s*(0x[0-9a-fA-F]+)\s+__heap_start\s*=", text)
    end = re.search(r"(?m)^\s*(0x[0-9a-fA-F]+)\s+__heap_end\s*=", text)
    if start is None or end is None:
        return None
    return int(end.group(1), 16) - int(start.group(1), 16)


def stories():
    """Every story present in this checkout and its size, largest first."""
    if not Z3_DIR.is_dir():
        raise RuntimeError("no cd/data/Z3 to measure")
    found = [(p.name, p.stat().st_size) for p in Z3_DIR.iterdir()
             if p.is_file() and p.suffix.upper() == ".Z3"]
    if not found:
        raise RuntimeError("no .Z3 stories found under cd/data/Z3")
    return sorted(found, key=lambda s: -s[1])


def declared_stories():
    """Every .Z3 GAME.INF names -- the disc as shipped.

    GAME.INF is tracked; the stories themselves are not (.gitignore excludes
    cd/data/Z3/*.z3), so a clean checkout has three of the thirty-one. This is
    how the arithmetic below can tell it is looking at a partial library rather
    than at a disc whose worst case has genuinely shrunk.
    """
    inf = Z3_DIR / "GAME.INF"
    if not inf.is_file():
        return []
    return sorted(set(re.findall("[A-Z0-9]{1,8}[.]Z3",
                                 inf.read_text(encoding="utf-8", errors="replace"))))


def require_full_library():
    """Skip rather than pass on an incomplete checkout.

    Every pairing below is about the worst case the disc can present. Measured
    against three stories instead of thirty-one it would report that two of them
    fit and call the release optional -- a green run asserting the opposite of
    the truth, which is worse than no run at all.
    """
    declared = declared_stories()
    if not declared:
        pytest.skip("cd/data/Z3/GAME.INF is missing -- cannot tell the disc's "
                    "worst case from this checkout's")
    missing = [n for n in declared if not (Z3_DIR / n).is_file()]
    if missing:
        pytest.skip(f"{len(missing)} of {len(declared)} stories GAME.INF names "
                    "are not in this checkout (cd/data/Z3/*.z3 is gitignored), "
                    "so the disc's worst case cannot be measured here")


def tga_gate(name, sector=2048):
    """What tga_decode demands be FREE before it will decode one wallpaper.

    Its literal gate -- `span + palsize + 4096`, where span is the pixel plane
    plus the leading partial sector it reads and shifts off, and palsize is the
    256-entry HighColor palette. Modelled off the shipped file's own header
    rather than assumed, the same way test_lwram_budget measures its archives.
    """
    p = ROOT / "saturn" / "cd" / "data" / "TGA" / name
    if not p.is_file():
        raise RuntimeError(f"no {name} under cd/data/TGA to measure")
    hdr = p.read_bytes()[:18]
    idlen, cmaptype, imgtype = hdr[0], hdr[1], hdr[2]
    cmaplen = hdr[5] | (hdr[6] << 8)
    cmapbits = hdr[7]
    w = hdr[12] | (hdr[13] << 8)
    h = hdr[14] | (hdr[15] << 8)
    if cmaptype != 1 or imgtype != 1 or hdr[16] != 8:
        raise RuntimeError(f"{name} is not the 8bpp uncompressed paletted TGA "
                           "tga_decode accepts -- it would be refused on format "
                           "before any of this arithmetic mattered")
    pixoff = 18 + idlen + cmaplen * (cmapbits // 8)
    span = (pixoff % sector) + w * h
    palsize = 256 * 2                    # sizeof(SRL::Types::HighColor)
    return span + palsize + 4096


def source(*parts):
    return (SRC.joinpath(*parts)).read_text(encoding="utf-8", errors="replace")


def code_lines(src):
    """Statement lines only, comments stripped -- the comments here name the
    very calls being searched for, so a plain substring match finds the prose
    that explains a call rather than the call."""
    out, in_block = [], False
    for line in src.splitlines():
        s = line.strip()
        if in_block:
            if "*/" in s:
                in_block = False
                s = s.split("*/", 1)[1].strip()
            else:
                continue
        while "/*" in s:
            head, rest = s.split("/*", 1)
            if "*/" in rest:
                s = head + rest.split("*/", 1)[1]
            else:
                s, in_block = head, True
                break
        s = s.split("//", 1)[0].strip()
        if s:
            out.append(s)
    return out


def function_body(text, name):
    """The body of a brace-balanced C/C++ function definition, or None.

    Walked rather than regexed because the bodies here contain braces of their
    own, and a lazy match would stop at the first inner one and read as empty.
    """
    m = re.search(r"(?m)^[A-Za-z_][\w :*&]*\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", text)
    if m is None:
        return None
    depth, i = 0, m.end() - 1
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def test_soft_reset_releases_the_story():
    """The regression guard: the outgoing image goes back before the title.

    A source check rather than a runtime one because there is nowhere on the
    Saturn to observe it from -- the failure is a heap that will not satisfy
    the next load, forty seconds later, with no message until it gives up.
    """
    body = function_body(source("engine", "soft_reset.cxx"), "soft_reset_to_title")
    assert body is not None, (
        "soft_reset_to_title is not where this test can find it -- if it moved "
        "or changed shape, move this check with it rather than deleting it.")
    assert "mojo_release" in body, (
        "soft_reset_to_title() does not release the story image. The outgoing "
        "story then stays in the C heap across the title screen, and mojozork's "
        "initStory does not free it until main.cxx has already allocated the "
        "incoming one -- so a game switch demands both at once in a heap that "
        "cannot hold two. See this file's header for the sizes.")


def test_release_actually_frees_the_image():
    """mojo_release has to drop the buffer, not just forget about it."""
    text = (ROOT / "saturn" / "src" / "engine" / "mojozork_saturn.c").read_text(
        encoding="utf-8", errors="replace")
    body = function_body(text, "mojo_release")
    assert body is not None, (
        "mojo_release is not defined in mojozork_saturn.c -- it is the only "
        "translation unit that can see mojozork.c's GState.")
    tight = re.sub(r"\s+", "", body)
    assert "free(GState->story)" in tight, (
        "mojo_release does not free the story image, which is the whole reason "
        "it exists.")
    assert "GState->story=NULL" in tight, (
        "mojo_release frees the story without clearing GState->story, so the "
        "next initStory frees the same pointer twice and saturn_story_data "
        "hands the typeahead a dangling image.")


def test_two_stories_do_not_fit():
    """Why the release is mandatory rather than tidy.

    Skipped rather than guessed at when BuildDrop has no map: the heap size is
    a property of the built image, and a made-up number here would make this
    pass while proving nothing.
    """
    heap = heap_bytes()
    if heap is None:
        pytest.skip("no link map in BuildDrop -- build once to measure the heap")
    require_full_library()
    ranked = stories()
    (n1, s1), (n2, s2) = ranked[0], ranked[1]
    assert s1 + s2 > heap, (
        f"the two largest stories ({n1} {s1} + {n2} {s2} = {s1 + s2}) now fit "
        f"in the {heap}-byte heap together. That does not make the release "
        "optional -- the outgoing image still strands mid-heap and fragments "
        "it -- but this check has stopped saying anything, so re-derive it "
        "against the pairing that is tightest now.")


def test_held_map_parchment_is_released_at_the_title():
    """The map's parchment is the second thing that outlives a session.

    title_bg_hold keeps MAP.TGA decoded in the same C heap for the rest of the
    run, and g_held is a plain static, so it survives the longjmp exactly as the
    story image used to. Left resident it comes straight out of the next story's
    allocation.
    """
    lines = code_lines((SRC / "main.cxx").read_text(encoding="utf-8", errors="replace"))
    drops = [l for l in lines if "title_bg_drop_held()" in l]
    assert drops, (
        "main.cxx never calls title_bg_drop_held(), so the map's parchment "
        "stays in the C heap across the title screen and the next story is "
        "loaded into what is left of it. Release it beside room_art_release() "
        "and item_art_close(), which answer for exactly the same thing in the "
        "other zone.")
    releases = [i for i, l in enumerate(lines) if "room_art_release()" in l]
    for i in releases:
        assert any("title_bg_drop_held()" in l for l in lines[i:i + 4]), (
            "one of main.cxx's session-teardown points releases the area "
            "archive but not the held parchment. Both are a finished session's "
            "art; they go together or the next one is missed.")


def test_held_map_and_the_largest_story_do_not_fit():
    """Why that release is mandatory rather than tidy, in bytes."""
    heap = heap_bytes()
    if heap is None:
        pytest.skip("no link map in BuildDrop -- build once to measure the heap")
    require_full_library()
    resident = tga_gate("MAP.TGA") - 4096      # the gate less its own headroom
    name, size = stories()[0]
    assert size + resident > heap, (
        f"{name} ({size}) and the held parchment ({resident}) now both fit in "
        f"{heap} bytes. That does not make the release optional -- it is still "
        "a whole session's allocation stranded across the title -- but this "
        "check has stopped saying anything, so re-derive it.")
    free = heap - resident
    stranded = [n for n, s in stories() if s > free]
    assert stranded, (
        "no shipped story is larger than what a held parchment leaves free, so "
        "this check no longer names a consequence. Re-derive it.")


def test_each_claimant_fits_the_heap_alone():
    """Neither of the two big HWRAM claimants is oversized by itself. They are
    only ever meant to be resident one at a time -- the story during a session,
    the decoded wallpaper at the title -- so this is the shape the release
    produces, and it has to hold or nothing works at all."""
    heap = heap_bytes()
    if heap is None:
        pytest.skip("no link map in BuildDrop -- build once to measure the heap")
    require_full_library()
    name, size = stories()[0]
    assert size <= heap, (
        f"{name} ({size} bytes) does not fit the {heap}-byte C heap at all. "
        "Nothing can load it. Trim the program image (__heap_start moves with "
        ".bss) or drop the story.")
    gate = tga_gate("TITLE.TGA")
    assert gate <= heap, (
        f"the title wallpaper asks tga_decode for {gate} free bytes of a "
        f"{heap}-byte heap. The title screen would be black on a cold boot.")


def test_title_wallpaper_needs_the_story_gone():
    """The second half of why the release is mandatory, and the half that is
    visible without picking anything.

    tga_decode refuses outright when HighWorkRam has less free than
    span + palette + 4096, and says nothing. Come back to the title with the
    outgoing story still resident and that gate is what fails: no logo, no
    wallpaper, a black title screen -- before the player has even chosen the
    next game.
    """
    heap = heap_bytes()
    if heap is None:
        pytest.skip("no link map in BuildDrop -- build once to measure the heap")
    require_full_library()
    name, size = stories()[0]
    gate = tga_gate("TITLE.TGA")
    assert size + gate > heap, (
        f"{name} ({size}) and the title wallpaper's gate ({gate}) now both fit "
        f"in {heap} bytes. That does not make the release optional -- the "
        "outgoing image still strands mid-heap and fragments what the next "
        "story needs contiguously -- but this check has stopped saying "
        "anything, so re-derive it against what is tightest now.")


def _print_report():
    heap = heap_bytes()
    ranked = stories()
    print("  HWRAM C heap             %8s  (%s)"
          % (heap if heap is not None else "unknown",
             "measured from BuildDrop map" if heap is not None
             else "no map -- build once"))
    print("  stories shipped          %8d" % len(ranked))
    for name, size in ranked[:3]:
        print("    %-14s       %8d" % (name, size))
    print("    ...")
    for name, size in ranked[-2:]:
        print("    %-14s       %8d" % (name, size))
    if heap is not None:
        (n1, s1), (n2, s2) = ranked[0], ranked[1]
        print("  ------------------------------------")
        print("  two largest at once      %8d  of %d  (%s)"
              % (s1 + s2, heap, "OVER" if s1 + s2 > heap else "fits"))
        gate = tga_gate("TITLE.TGA")
        print("  title wallpaper gate     %8d  (what tga_decode wants free)"
              % gate)
        print("  largest + that gate      %8d  of %d  (%s)"
              % (s1 + gate, heap, "OVER" if s1 + gate > heap else "fits"))
        held = tga_gate("MAP.TGA") - 4096
        print("  map parchment held       %8d  (kept for the session once opened)"
              % held)
        print("  largest + held parchment %8d  of %d  (%s)"
              % (s1 + held, heap, "OVER" if s1 + held > heap else "fits"))
        print("  stories over what a held parchment leaves free: %d of %d"
              % (len([1 for _, s in ranked if s > heap - held]), len(ranked)))


def main():
    try:
        _print_report()
    except RuntimeError as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)

    checks = [
        (test_soft_reset_releases_the_story, "soft reset releases the story"),
        (test_release_actually_frees_the_image, "mojo_release frees the image"),
        (test_two_stories_do_not_fit, "two stories do not fit"),
        (test_held_map_parchment_is_released_at_the_title,
         "held parchment released at the title"),
        (test_held_map_and_the_largest_story_do_not_fit,
         "held parchment and largest story do not fit"),
        (test_each_claimant_fits_the_heap_alone, "each claimant fits the heap alone"),
        (test_title_wallpaper_needs_the_story_gone,
         "title wallpaper needs the story gone"),
    ]
    fails = 0
    for fn, label in checks:
        try:
            fn()
        except AssertionError as e:
            print(f"\ntest_hwram_budget: FAILED -- {label}\n{e}", file=sys.stderr)
            fails += 1
        except Exception as e:                      # a skip outside pytest
            print(f"\ntest_hwram_budget: SKIPPED -- {label} ({e})")

    if fails:
        sys.exit(1)
    print("\ntest_hwram_budget: OK")


if __name__ == "__main__":
    main()
