"""Gate the disc-side contract the mood folders rest on.

This is a source and layout test, not a hardware test -- whether CD-DA actually
survives a directory change can only be observed on real hardware (or in an
accurate emulator), which is outside what a host-side pytest run can check.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TITLE = (REPO / "saturn" / "src" / "video" / "title.cxx").read_text()
MAIN = (REPO / "saturn" / "src" / "main.cxx").read_text()

# Generous enough to span the two multi-line cleanup blocks in tga_decode
# (a failed allocation or a failed CD read frees a buffer or two, then
# restores, then returns three lines down); tight enough that a restore
# belonging to a different exit can't be mistaken for this one's.
RETURN_LOOKBACK_LINES = 2


def extract_function_body(source, name):
    """The braced body of the C/C++ function `name`, found by brace matching
    from its first `{`. Starts at the opening brace, not the signature or the
    header comment above it -- so prose like "Returns: true on success" in
    the function's own header block can never be mistaken for a `return`
    statement in its body."""
    sig = re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", source)
    assert sig, f"{name}(...) not found in title.cxx"
    start = sig.end() - 1
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError(f"unbalanced braces reading {name}'s body")


def test_boot_scan_is_gone():
    assert "display_scan_images" not in TITLE
    assert "display_scan_images" not in MAIN


def test_cd_enter_mood_is_used():
    assert re.search(r"\bcd_enter_mood\s*\(", TITLE), "cd_enter_mood is not used"


def test_every_return_in_tga_decode_is_preceded_by_cd_enter_root():
    """tga_decode is the picture-loading function: the only place that opens
    an SRL::Cd::File and the only caller of cd_enter_mood. Every one of its
    `return` statements -- the cd_enter_mood-failed guard, every format/size
    validation, every allocation or read failure, and the final success path
    -- must restore the working directory first, or the CD is left standing
    in /TGA or /TGA/HORROR for whatever runs next (game_catalog.cxx's
    GFS_LoadDir on /Z3, most visibly).

    This walks every `return` in the function's body and checks a
    cd_enter_root() call appears before it: on the same line, or on one of
    the RETURN_LOOKBACK_LINES lines immediately above. A single occurrence
    count (as the previous version of this test used) cannot fail when one
    of several call sites drops its restore -- this walks every site
    individually so it can. It is still a source-level check: it proves the
    call is textually present ahead of each return, not that the CD is
    actually back at root when the SH-2 runs it.
    """
    body = extract_function_body(TITLE, "tga_decode")
    lines = body.split("\n")
    unguarded = []
    for i, line in enumerate(lines):
        for m in re.finditer(r"\breturn\b", line):
            before_on_this_line = line[:m.start()]
            lookback = lines[max(0, i - RETURN_LOOKBACK_LINES):i]
            guarded = ("cd_enter_root(" in before_on_this_line
                       or any("cd_enter_root(" in w for w in lookback))
            if not guarded:
                unguarded.append((i, line.strip()))
    assert not unguarded, (
        "return(s) in tga_decode with no cd_enter_root() in the preceding "
        f"{RETURN_LOOKBACK_LINES} lines: "
        + "; ".join(f"line {i}: {text!r}" for i, text in unguarded))


def test_no_flat_tga_names_remain():
    assert not re.search(r'"[A-Z]{4,8}\d\.TGA"', TITLE), \
        "a flat, pre-folder TGA filename is still hard-coded"
