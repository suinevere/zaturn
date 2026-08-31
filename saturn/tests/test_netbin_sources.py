#!/usr/bin/env python3
"""Assert the makefile's NETBIN source list is exactly the expected objects,
that the NETBIN block assigns SOURCES with a plain `=` (not `+=`, which would
silently merge in the CD build's find-globbed objects and defeat the object
scan below), and that the CD build's own find-based SOURCES lines
(makefile:33-34, outside the NETBIN block) exclude the netbin-only sources.
That last check is the regression test for the final-review CRITICAL 1
finding: an unguarded `find` glob pulled src/main_netbin.cxx and
src/net/netbin_pages.cxx into a plain `make all`, handing the linker
duplicate main()/soft-reset symbols. Comment lines inside the NETBIN block
are stripped before any of this is scanned, so a source commented out of the
list cannot still register as present.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXPECTED = {
    "src/main_netbin.cxx",
    "src/net/netbin_pages.cxx",
    "src/net/online.cxx",
    "src/net/net_connect.c",
    "src/net/term.c",
    "src/net/transport_uart.c",
    "src/video/console_view.cxx",
    "src/video/console.c",
    "src/video/display.c",
    "src/video/text_map.cxx",
    "src/video/glyph_invert.c",
    "src/video/dash_map.c",
    "src/video/dash_tiles.c",
    "src/video/dash_view.cxx",
    "src/video/command_rose.c",
    "src/video/rose_draw.cxx",
    "src/input/game_kb.c",
    "src/menu/menu.cxx",
    "src/menu/menu_layout.c",
    "src/menu/options.cxx",
    "src/input/input.cxx",
    "src/input/saturn_keyboard.cxx",
    "src/input/keyboard.c",
    "src/input/numpad.c",
    "src/input/typeahead.c",
    "src/input/typeahead_extract.c",
    "src/input/typeahead_solution_zork1.c",
    "src/input/netbin_story.c",
    "src/system/saturn_backup.cxx",
    "src/system/netbin_nocd.c",
    "src/engine/room_model.c",
    "src/engine/map_model.c",
    "src/engine/map_atlas.c",
    "src/video/map_view.cxx",
    "src/input/command_panel.c",
    "src/video/command_view.cxx",
    "src/engine/app_state.cxx",
}

# The files that only the netbin links -- the CD build's find-globbed
# SOURCES must exclude all of them (CRITICAL 1).
NETBIN_ONLY = {"src/main_netbin.cxx", "src/net/netbin_pages.cxx",
               "src/input/typeahead_solution_zork1.c",
               "src/system/netbin_nocd.c"}


def strip_comments(text):
    """Drop whole `#`-comment lines before scanning for source paths, so a
    file commented out of the NETBIN list cannot still register as present
    (final-review IMPORTANT 3, gap 1)."""
    return "\n".join(ln for ln in text.splitlines() if not ln.strip().startswith("#"))


def main():
    mk = (ROOT / "makefile").read_text(encoding="utf-8", errors="replace")
    block = re.search(r"ifeq \(\$\(strip \$\(NETBIN\)\),1\)(.*?)\nendif", mk, re.S)
    assert block, "no NETBIN block in makefile"
    body = strip_comments(block.group(1))

    # cxx BEFORE c in the alternation: `(?:c|cxx)` matches the leading "c" of
    # ".cxx" and stops, silently turning src/menu/menu.cxx into src/menu/menu.c.
    found = set(re.findall(r"src/[\w/]+\.(?:cxx|c)\b", body))
    missing, extra = EXPECTED - found, found - EXPECTED
    fails = 0
    for m in sorted(missing):
        print(f"MISSING from NETBIN sources: {m}", file=sys.stderr); fails += 1
    for e in sorted(extra):
        print(f"UNEXPECTED in NETBIN sources: {e}", file=sys.stderr); fails += 1

    # Every listed file must actually exist.
    for f in sorted(found):
        if not (ROOT / f).exists():
            print(f"NONEXISTENT source listed: {f}", file=sys.stderr); fails += 1

    # The netbin must not link main.cxx.
    if re.search(r"\bsrc/main\.cxx\b", body):
        print("NETBIN sources include src/main.cxx", file=sys.stderr); fails += 1

    for need, why in [
        (r"-DNETBIN",                        "the -DNETBIN flag"),
        (r"SRL_USE_SGL_SOUND_DRIVER\s*=\s*0", "SRL_USE_SGL_SOUND_DRIVER = 0"),
    ]:
        if not re.search(need, body):
            print(f"NETBIN block is missing {why}", file=sys.stderr); fails += 1

    # gap 2: the NETBIN SOURCES assignment must be a plain `=`. With `+=` it
    # would append to whatever the CD build's own find-globs already put in
    # SOURCES, and the `found` scan above could not tell an appended list
    # from a wholesale replacement.
    if re.search(r"(?m)^\s*SOURCES\s*\+=", body):
        print("NETBIN SOURCES uses += -- would merge with the CD build's glob",
              file=sys.stderr)
        fails += 1
    if not re.search(r"(?m)^\s*SOURCES\s*=(?!=)", body):
        print("NETBIN SOURCES is not assigned with a plain '='", file=sys.stderr)
        fails += 1

    # gap 3 / CRITICAL 1 regression: this test's stated purpose is "the guard
    # against the list drifting", but until now it never looked outside the
    # ifeq block -- exactly where CRITICAL 1 lived. The CD build's two
    # find-based SOURCES lines (makefile:33-34) must filter out the
    # netbin-only sources, or a plain `make all` compiles src/main_netbin.cxx
    # into the CD image alongside src/engine/soft_reset.cxx and hands the
    # linker six `multiple definition of` errors.
    pre_block = mk[: block.start()]
    cd_lines = [ln for ln in pre_block.splitlines()
                if re.search(r"find src/ -name '\*\.(?:c|cxx)'", ln)]
    if len(cd_lines) != 2:
        print(f"expected 2 CD find-glob SOURCES lines before the NETBIN block, "
              f"found {len(cd_lines)}", file=sys.stderr)
        fails += 1

    m2 = re.search(r"(?m)^NETBIN_ONLY_SOURCES\s*=\s*(.+)$", mk)
    if not m2:
        print("no NETBIN_ONLY_SOURCES definition in makefile "
              "(CD glob has no way to exclude the netbin-only sources)",
              file=sys.stderr)
        fails += 1
    else:
        excl_value = m2.group(1)
        for f in sorted(NETBIN_ONLY):
            if f not in excl_value:
                print(f"NETBIN_ONLY_SOURCES is missing {f}", file=sys.stderr)
                fails += 1

    for ln in cd_lines:
        if "filter-out" not in ln or "NETBIN_ONLY_SOURCES" not in ln:
            print(f"CD glob line does not exclude the netbin-only sources: {ln.strip()}",
                  file=sys.stderr)
            fails += 1

    if fails:
        print(f"test_netbin_sources: {fails} FAILED", file=sys.stderr); sys.exit(1)
    print("test_netbin_sources: OK")

main()
