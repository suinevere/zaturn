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

There are two ways in. --z3 scans a folder of story files and reads each
header, which is what a locally staged disc wants. --versions reads
tools/assets/VERSIONS.ndjson instead, whose "version"/"release" fields are the
same header release and serial, and whose "title" is the filename the injection
step writes -- so the manifest for the downloaded set can be built with no story
files present at all, and committed once, beside the disc it describes.

That committed file is the only one there is, and nothing in the asset
pipeline touches it: it is built into the base ISO along with the rest of
saturn/cd/data, and xorriso's -map merges the downloaded stories into the
existing /Z3 rather than replacing the directory, so the manifest is already
on the disc the games are injected into. Which is the point -- the release kit
is bash/cmd, curl and two bundled binaries, and adding a Python dependency to a
disc-patching script users run on their own machines would be a poor trade for
a file whose contents are known before the pipeline starts.

Usage:
  python gen_game_info.py --versions tools/assets/VERSIONS.ndjson
  python gen_game_info.py --z3 some/other/Z3
"""

import argparse, glob, json, os, re, struct, sys

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
GAME_CAT_OTHER = 7

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


def collect_from_versions(path, titles):
    """One record per .Z3 line of VERSIONS.ndjson, without touching a story file.

    The fields are the same key game_titles.c is written against: "version" is
    the header release word and "release" the six-character serial.
    """
    records = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            name = str(row["title"]).upper()
            if not name.endswith(".Z3"):
                continue
            if len(name) >= NAME_MAX:
                print(f"  skipped (name too long for the menu): {name}")
                continue
            label, cat = titles.get((int(row["version"]), str(row["release"])),
                                    (name, GAME_CAT_OTHER))
            records.append((name, label[:LABEL_MAX - 1], cat))
    return sorted(records, key=lambda r: r[0])


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
    repo = os.path.join(here, "..", "..")
    default_titles = os.path.join(repo, "saturn", "src", "menu", "game_titles.c")
    default_out = os.path.join(repo, "saturn", "cd", "data", "Z3", "GAME.INF")

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--z3", help="folder of .Z3 story files bound for the disc")
    ap.add_argument("--versions", help="VERSIONS.ndjson to build the manifest from instead")
    ap.add_argument("--titles", default=default_titles, help="generated game_titles.c to read titles from")
    ap.add_argument("--out", help="output file (default: the staged disc's Z3/GAME.INF, "
                                  "or GAME.INF inside --z3 when scanning one)")
    args = ap.parse_args()

    if bool(args.z3) == bool(args.versions):
        print("ERROR: pass exactly one of --z3 or --versions", file=sys.stderr)
        return 1
    if args.z3 and not os.path.isdir(args.z3):
        print(f"ERROR: no such folder: {args.z3}", file=sys.stderr)
        return 1
    if args.versions and not os.path.isfile(args.versions):
        print(f"ERROR: no such file: {args.versions}", file=sys.stderr)
        return 1
    if not os.path.isfile(args.titles):
        print(f"ERROR: no title table: {args.titles}", file=sys.stderr)
        return 1
    titles = load_titles(args.titles)
    records = (collect(args.z3, titles) if args.z3
               else collect_from_versions(args.versions, titles))
    if not records:
        print(f"ERROR: no games found in {args.z3 or args.versions}", file=sys.stderr)
        return 1
    if len(records) > MAX_GAMES:
        print(f"ERROR: {len(records)} games exceeds MAX_GAMES ({MAX_GAMES}); "
              f"raise it in game_catalog.cxx and here together", file=sys.stderr)
        return 1

    out = args.out or (os.path.join(args.z3, "GAME.INF") if args.z3 else default_out)

    size = emit(records, out)
    named = sum(1 for _, label, _ in records if not label.endswith(".Z3"))
    print(f"{len(records)} games ({named} named, {len(records) - named} by filename), "
          f"{size} bytes -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
