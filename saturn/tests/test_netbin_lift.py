#!/usr/bin/env python3
"""Assert netbin_pages.cxx's lifted bodies match their menu_pages.cxx originals.

The netbin links a three-screen slice of menu_pages.cxx rather than the whole
51.7 KB file. That slice is a verbatim move, so any divergence is either a
transcription error or an undocumented edit -- both worth failing on.

network_page is deliberately NOT compared: it is renamed to netbin_dial_page
and its row set changes (Cancel out, Controls in). Those checks are inline
in main() below instead.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

def body(path, sig):
    r"""Return the brace-balanced body of the DEFINITION whose signature is sig.

    [^;{]* rather than \s* between the signature and the brace, for two
    reasons. It spans menu_digit_row's multi-line parameter list, and it
    refuses to cross a `;`, which is what makes it skip forward declarations --
    netbin_pages.cxx forward-declares controls_dispatch above
    netbin_dial_page, and a naive find() would latch onto that `;` and then
    walk into the wrong function's braces.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(re.escape(sig) + r"[^;{]*\{", text)
    assert m, f"no definition of {sig!r} in {path.name}"
    i = text.index("{", m.start())
    depth, j = 0, i
    while True:
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0: return text[i:j+1]
        j += 1

def normalize(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", " ", s)
    return re.sub(r"\s+", "", s)

def main():
    old = SRC / "menu" / "menu_pages.cxx"
    new = SRC / "net" / "netbin_pages.cxx"
    pairs = [
        ("static bool controls_page(void)",           "static bool controls_page(void)"),
        ("bool keyboard_controls_page(void)",         "bool keyboard_controls_page(void)"),
        ("static void controls_dispatch(void)",       "static void controls_dispatch(void)"),
        ("static bool menu_digit_row(",               "static bool menu_digit_row("),
    ]
    fails = 0
    for a, b in pairs:
        if normalize(body(old, a)) != normalize(body(new, b)):
            print(f"MISMATCH: {a}", file=sys.stderr); fails += 1

    # The label tables move verbatim too.
    for tbl in ("FACE_LABEL", "CHORD_LABEL"):
        oa = normalize(re.search(rf"{tbl}\[[A-Z_]+\]\s*=\s*\{{.*?\}};",
                                 old.read_text(encoding='utf-8'), re.S).group(0))
        nb = normalize(re.search(rf"{tbl}\[[A-Z_]+\]\s*=\s*\{{.*?\}};",
                                 new.read_text(encoding='utf-8'), re.S).group(0))
        if oa != nb:
            print(f"MISMATCH: {tbl}", file=sys.stderr); fails += 1

    # The dialer keeps its validation contract but not its Cancel row.
    dial = body(new, "void netbin_dial_page(void)")
    for must in ("valid_dialnum", "options_save", "controls_dispatch"):
        if must not in dial:
            print(f"MISSING in netbin_dial_page: {must}", file=sys.stderr); fails += 1
    if "Cancel" in dial:
        print("netbin_dial_page still offers a Cancel row", file=sys.stderr); fails += 1

    if fails:
        print(f"test_netbin_lift: {fails} FAILED", file=sys.stderr); sys.exit(1)
    print("test_netbin_lift: OK")

main()
