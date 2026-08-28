#!/usr/bin/env python3
"""Generate GAME.INF: the disc's story catalogue, so the Saturn does not have to
read every game's header off the drive to build the selection menu.

Without it, preload_game_catalog opens one file per story and reads its first
sector just to recover the Z-machine release+serial that names it -- ~30 seeks
on a full disc, all of them during the intro. GAME.INF answers the same question
in one read.

Titles come out of the already-generated saturn/src/menu/game_titles.c rather
than from gen_titles.py's own tables: that file is what the Saturn compiles in
and falls back to, so reading it is what keeps the manifest and the fallback
from ever disagreeing.

The record for a story the table does not know carries the filename as its
label and category Other -- exactly what the runtime would have settled on after
reading the header -- so an unknown game costs no drive access either.

Usage:
  python gen_game_info.py --z3 ../../saturn/cd/data/Z3
  python gen_game_info.py --z3 Z3 --titles ../../saturn/src/menu/game_titles.c
"""

import argparse, glob, os, re, struct, sys

# Must match game_catalog.cxx: the magic it checks, the record size it insists
# on, and MENU_ROW_TEXT_MAX (31) plus its NUL. A record is
#   0..15  filename, NUL-padded      16..47  display label, NUL-padded
#   48     GAME_CAT_* id             49..51  reserved, zero
# behind a 16-byte header of magic, count, record size and reserved zeros. The
# whole file stays inside one 2048-byte sector at MAX_GAMES (32) entries.
MAGIC = b"ZGI1"
HDR_SIZE = 16
REC_SIZE = 52
NAME_MAX = 16
LABEL_MAX = 32
MAX_GAMES = 32
GAME_CAT_OTHER = 6

# One row of the generated table: { release, "serial", "title", cat },
ROW_RE = re.compile(r'\{\s*(\d+)\s*,\s*"([^"]*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\d+)\s*\}')


def load_titles(path):
    """Read game_titles.c into a (release, serial) -> (title, category) map."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    table = {}
    for rel, serial, title, cat in ROW_RE.findall(text):
        table[(int(rel), serial)] = (title.encode().decode("unicode_escape"), int(cat))
    return table


def describe(path, titles):
    """The (label, category) the Saturn would end up with for one story file."""
    name = os.path.basename(path).upper()
    fallback = (name, GAME_CAT_OTHER)
    with open(path, "rb") as f:
        head = f.read(0x1A)
    if len(head) < 0x1A or head[0] != 3:
        return fallback
    release = (head[2] << 8) | head[3]
    serial = bytes(head[0x12:0x18]).decode("latin1", "replace")
    return titles.get((release, serial), fallback)


def collect(z3dir, titles):
    """One record per .Z3 file in the folder, in the order the disc lists them."""
    paths = sorted(set(glob.glob(os.path.join(z3dir, "*.z3")) +
                       glob.glob(os.path.join(z3dir, "*.Z3"))),
                   key=lambda p: os.path.basename(p).upper())
    records = []
    for p in paths:
        name = os.path.basename(p).upper()
        if len(name) >= NAME_MAX:
            print(f"  skipped (name too long for the menu): {name}")
            continue
        label, cat = describe(p, titles)
        # The runtime clamps to MENU_ROW_TEXT_MAX before it draws, and a wider
        # label would overwrite the menu box's border.
        records.append((name, label[:LABEL_MAX - 1], cat))
    return records


def emit(records, out):
    blob = MAGIC + struct.pack(">HH", len(records), REC_SIZE) + bytes(HDR_SIZE - 8)
    for name, label, cat in records:
        blob += name.encode("ascii", "replace").ljust(NAME_MAX, b"\0")[:NAME_MAX]
        blob += label.encode("ascii", "replace").ljust(LABEL_MAX, b"\0")[:LABEL_MAX]
        blob += bytes([cat & 0xFF]) + bytes(REC_SIZE - NAME_MAX - LABEL_MAX - 1)
    with open(out, "wb") as f:
        f.write(blob)
    return len(blob)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_titles = os.path.join(here, "..", "..", "saturn", "src", "menu", "game_titles.c")

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--z3", required=True, help="folder of .Z3 story files bound for the disc")
    ap.add_argument("--titles", default=default_titles, help="generated game_titles.c to read titles from")
    ap.add_argument("--out", help="output file (default: GAME.INF inside --z3)")
    args = ap.parse_args()

    if not os.path.isdir(args.z3):
        print(f"ERROR: no such folder: {args.z3}", file=sys.stderr)
        return 1
    if not os.path.isfile(args.titles):
        print(f"ERROR: no title table: {args.titles}", file=sys.stderr)
        return 1

    titles = load_titles(args.titles)
    records = collect(args.z3, titles)
    if not records:
        print(f"ERROR: no .Z3 files in {args.z3}", file=sys.stderr)
        return 1
    if len(records) > MAX_GAMES:
        print(f"ERROR: {len(records)} games exceeds MAX_GAMES ({MAX_GAMES}); "
              f"raise it in game_catalog.cxx and here together", file=sys.stderr)
        return 1

    out = args.out or os.path.join(args.z3, "GAME.INF")
    size = emit(records, out)
    named = sum(1 for _, label, _ in records if not label.endswith(".Z3"))
    print(f"{len(records)} games ({named} named, {len(records) - named} by filename), "
          f"{size} bytes -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
