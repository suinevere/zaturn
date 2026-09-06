#!/usr/bin/env python3
"""Hold the High Work RAM budget against the stories actually shipped.

HWRAM is the C heap -- malloc/free/new in saturn_compat.cxx route there -- and
it is not a megabyte. sgl.linker puts __heap_start immediately after .bss and
__heap_end at the SGL work area (0x060C0000), so the heap is whatever the
program image leaves behind: about 194 KB in the shipped build. The single
largest thing in it is the running story image, and the shipped stories run
from 47 KB (MZORKI) to 129 KB (LURKING, the Lurking Horror).

That leaves no room for two of them. The rule this file exists to hold is:

  a story image must be released before the next one is read.

The map's parchment used to be measured here too, and was the reason nine of the
thirty-one stories stopped loading once a player had opened the map. It is
decoded into Low Work RAM now, so that pairing moved to test_lwram_budget.py
with it -- which is also why the largest stories can hold it at all.

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
import os
import re
import subprocess
import sys
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "saturn" / "src"
Z3_DIR = ROOT / "saturn" / "cd" / "data" / "Z3"
MAP_FILE = (ROOT / "saturn" / "BuildDrop"
            / "Zaturn (USA) (Netlink Edition).map")
COMPILE = ROOT / "saturn" / "compile.bat"
TOOLCHAIN = ROOT / "SaturnRingLib" / "Compiler" / "sh2eb-elf" / "bin"


def outdated_map():
    """How many sources are newer than the link map.

    A map measures the build that produced it and nothing else, so one older
    than the code it is being read about describes an image nobody is running.
    That is not a heap regression and must not be reported as one: it cost an
    hour once, the map having been written 80 minutes before the commit that
    took the story heap from 135,616 to 192,800.
    """
    if not MAP_FILE.is_file() or not SRC.is_dir():
        return 0
    when = MAP_FILE.stat().st_mtime
    return sum(1 for f in SRC.rglob("*")
               if f.is_file() and f.stat().st_mtime > when)


NO_HEAP = ("no link map in BuildDrop, or one older than the sources it would "
           "be measuring, and no toolchain here to build one -- run "
           "saturn/compile.bat to measure the heap")

_BUILT = []


def build_map():
    """Build once, if that is what it takes to have a map worth reading.

    The heap is a property of the built image, so a heap test without a build
    behind it is measuring nothing -- it either reports the last build's number
    long after that build stopped existing, or it skips and the floor silently
    stops being held. Both happened. So the build is the test's dependency and
    is run on demand, at most once a session, and only where there is a
    toolchain to run it with; ZATURN_NO_BUILD=1 opts out for anywhere that
    would rather skip than spend the minutes.
    """
    if MAP_FILE.is_file() and not outdated_map():
        return True
    if _BUILT:
        return _BUILT[0]
    if (os.environ.get("ZATURN_NO_BUILD") or not COMPILE.is_file()
            or not TOOLCHAIN.is_dir()):
        return False
    print(f"\n{MAP_FILE.name} is stale; running {COMPILE.name} to measure the "
          "heap against the build it describes", flush=True)
    try:
        done = subprocess.run(["cmd", "/c", str(COMPILE)], cwd=str(COMPILE.parent),
                              capture_output=True, text=True, timeout=3600)
    except (OSError, subprocess.SubprocessError):
        _BUILT.append(False)
        return False
    ok = MAP_FILE.is_file() and not outdated_map()
    if not ok:
        print(done.stdout[-2000:], done.stderr[-2000:], flush=True)
    _BUILT.append(ok)
    return ok


def heap_bytes():
    """The HWRAM C heap, measured off the link map.

    __heap_start moves with .bss, so this is a property of the build and not a
    constant anything declares -- which is why a missing or stale map is built
    rather than worked around. Returns None only when there is no way to get
    one: no toolchain in this checkout, or ZATURN_NO_BUILD set.
    """
    if not build_map():
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
        pytest.skip(NO_HEAP)
    require_full_library()
    ranked = stories()
    (n1, s1), (n2, s2) = ranked[0], ranked[1]
    assert s1 + s2 > heap, (
        f"the two largest stories ({n1} {s1} + {n2} {s2} = {s1 + s2}) now fit "
        f"in the {heap}-byte heap together. That does not make the release "
        "optional -- the outgoing image still strands mid-heap and fragments "
        "it -- but this check has stopped saying anything, so re-derive it "
        "against the pairing that is tightest now.")




def test_each_claimant_fits_the_heap_alone():
    """Neither of the two big HWRAM claimants is oversized by itself. They are
    only ever meant to be resident one at a time -- the story during a session,
    the decoded wallpaper at the title -- so this is the shape the release
    produces, and it has to hold or nothing works at all."""
    heap = heap_bytes()
    if heap is None:
        pytest.skip(NO_HEAP)
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


STORY_HEADROOM = 16 * 1024


def test_the_largest_story_leaves_room_to_run_in():
    """Fitting is not the same as working, and this is the gap that let a
    regression through green.

    test_each_claimant_fits_the_heap_alone asks only whether the image fits at
    all. It went on passing while the heap fell far enough that the largest
    story left almost nothing behind it -- and a story that fits with nothing
    behind it is a game that loads and then cannot do anything that costs a
    byte: no PCM slice off a .BLB, no item picture, no save or restore scratch,
    which alone is roughly twelve kilobytes of dynamic memory.

    STORY_HEADROOM is a floor rather than a measurement of any one claimant on
    purpose. What is resident alongside the story changes with the screen; what
    does not change is that a build leaving less than this behind the biggest
    story is one where the next kilobyte added to the program image stops that
    story loading outright, and the symptom then is forty seconds of LOADING
    standing still (see main.cxx's oom_want).

    If this fails, the heap moved, not the story: __heap_start follows .bss, so
    it is the program image that grew. Find it in the link map rather than
    raising the floor.
    """
    heap = heap_bytes()
    if heap is None:
        pytest.skip(NO_HEAP)
    require_full_library()
    name, size = stories()[0]
    assert size + STORY_HEADROOM <= heap, (
        f"{name} ({size} bytes) leaves only {heap - size} bytes of the "
        f"{heap}-byte heap behind it, under the {STORY_HEADROOM}-byte floor. "
        "The program image has grown; __heap_start moves with it. Trim the "
        "image -- the link map names the .rodata that did it -- or this story "
        "loads with nothing left to run in.")


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
        pytest.skip(NO_HEAP)
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
