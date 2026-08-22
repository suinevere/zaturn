#!/usr/bin/env python3
"""Convert PNG backgrounds into the 8bpp paletted TGAs the Saturn disc expects.

Usage:
    python tools/make_tga.py                          # regenerate everything
    python tools/make_tga.py <src-dir> <dst-dir>       # batch, custom roots
    python tools/make_tga.py <src.png> <dst.tga>       # single file

Two constraints make this script necessary instead of a plain image-editor
export:

1. **8bpp paletted, never truecolor.** SRL's VRAM::AutoAllocateBmp doubles the
   VDP2 bitmap container size for RGB555, so the 512x256 container becomes 256KB
   and spans the A0/A1 VRAM bank boundary. Bank-spanning bitmaps render as
   static: slBitMapNbg0 never reserves the second bank in VDP2_RAMCTL, and SRL's
   allocator tracks banks only in software (see srl_vdp2.hpp:11-15). At 8bpp the
   container is exactly 128KB and fits one bank.

2. **Palette index 0 must be unused.** VDP2 treats index 0 on a scroll screen as
   transparent, which would punch back-color holes through the image. We
   quantize to 255 colors and shift every index up by one.

The TGA is written by hand because PIL re-optimizes the palette on save, which
silently undoes constraint 2.

Sources that are not exactly 320x224 are reported and skipped rather than
aborting the run -- the build calls this on every compile and one bad file
must not stop the rest.

Each game now owns a flat disc folder rather than sharing a mood folder with
every other game (tools/assets/png/<GAME>/<SCENE>/*.png ->
saturn/cd/data/TGA/<GAME>/01.TGA..NN.TGA): convert_game_tree converts every
game's scenes in scene_vocab.SCENES order, assigning consecutive indices from
1 within that game's own 1..99 range, and write_scene_inc emits GAME_DIR and
GAME_SCENE from the result -- see game_scenes.inc's own header for what those
tables mean to display.c. A count in that table can never name a picture the
disc lacks, because it is only ever the count of files that actually
converted, never the count requested.

The title screen and menu need a wallpaper at boot, before any game is
selected, so tools/assets/png/TITLE/*.png is a separate small shared folder
outside the per-game machinery: convert_title writes it flat to
saturn/cd/data/TGA/TITLE/01.TGA..NN.TGA and write_title_inc emits the
TITLE_ART_N count C iterates it by.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

import gen_scene_tables
import scene_vocab as vocab

REPO = Path(__file__).resolve().parent.parent
WIDTH, HEIGHT = 320, 224
MAX_STEM = 8  # ISO9660 8.3; the build passes --norock to xorrisofs
SOURCE_EXT = (".png", ".jpg", ".jpeg")


def encode_tga(im):
    """
    ----------------------
    | encode_tga
    | Description: Pack a 320x224 RGB image into a complete 8bpp paletted TGA
    |   file image, index 0 reserved (VDP2 reads it as transparent on a scroll
    |   screen) and colormap entries stored BGR, per the TGA spec.
    | Author: suinevere
    | Dependencies: PIL.Image, struct
    | Globals: N/A
    | Params: im -- a 320x224 PIL Image, any mode
    | Returns: the complete TGA file, as bytes
    ----------------------
    """
    w, h = im.size
    q = im.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    idx = q.tobytes()
    ncolors = max(idx) + 1

    flat = q.getpalette()[: ncolors * 3]
    rgb = [tuple(flat[i * 3 : i * 3 + 3]) for i in range(ncolors)]

    # Reserve index 0: shift colors up one slot, pixels follow.
    palette = [(0, 0, 0)] + rgb
    pixels = bytes(b + 1 for b in idx)
    if 0 in pixels:
        raise AssertionError("index 0 must stay unused (VDP2 reads it as transparent)")
    if not (max(pixels) < len(palette) <= 256):
        raise AssertionError("palette overflow: indices must fit the colormap")

    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,              # no image ID
        1,              # colormap present
        1,              # uncompressed paletted
        0,              # colormap start
        len(palette),   # colormap length
        24,             # colormap entry depth
        0, 0,           # origin x/y
        w, h,
        8,              # 8bpp indices
        0x00,           # bottom-left origin, no alpha bits
    )
    cmap = b"".join(bytes((b, g, r)) for (r, g, b) in palette)  # TGA colormaps are BGR
    rows = [pixels[y * w : (y + 1) * w] for y in range(h)]
    body = b"".join(reversed(rows))  # bottom-left origin: rows bottom-to-top
    return header + cmap + body


def convert_one(src, dst):
    """
    ----------------------
    | convert_one
    | Description: Convert a single PNG at src to a TGA at dst, for the
    |   single-file CLI form. Reports a size mismatch as a skip rather than
    |   raising, matching the batch form's one-bad-file tolerance.
    | Author: suinevere
    | Dependencies: PIL.Image, encode_tga
    | Globals: WIDTH, HEIGHT
    | Params: src -- source picture path; dst -- destination .TGA path
    | Returns: (status, message), status 'wrote' or 'skip'
    ----------------------
    """
    im = Image.open(src).convert("RGB")
    w, h = im.size
    if (w, h) != (WIDTH, HEIGHT):
        return ("skip", f"{src.name}: expected {WIDTH}x{HEIGHT}, got {w}x{h}")

    blob = encode_tga(im)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(blob)
    return ("wrote", f"{dst.name}: {w}x{h} 8bpp, index 0 reserved, {len(blob)} bytes")


def _convert_source(src, dst):
    """
    ----------------------
    | _convert_source
    | Description: Convert one source picture straight to dst, open, encode and
    |   write as a single guarded unit so one bad file costs one picture, not
    |   the run. AssertionError (a genuine encoder bug) is left to propagate.
    | Author: suinevere
    | Dependencies: PIL.Image, encode_tga
    | Globals: WIDTH, HEIGHT
    | Params: src -- source picture path; dst -- destination .TGA path
    | Returns: True if dst was written, False if src was skipped
    ----------------------
    """
    try:
        im = Image.open(src).convert("RGB")
        if im.size != (WIDTH, HEIGHT):
            print(f"  skipped {src}: {im.size} is not {WIDTH}x{HEIGHT}")
            return False
        dst.write_bytes(encode_tga(im))
        return True
    except AssertionError:
        raise
    except Exception as exc:
        print(f"  skipped {src}: {exc}")
        return False


def convert_game_tree(src_root, dst_root):
    """Convert every source picture under src_root into dst_root/<GAME>/NN.TGA.

    Description: Walks src_root/<GAME>/<SCENE>/*.png, converting each game's
        scenes in scene_vocab.SCENES order and assigning consecutive indices
        from 1 within that game's own 1..99 range. Because the count
        recorded for a scene is only ever how many of its pictures actually
        converted, a range can never name a picture the disc lacks -- the
        property the old per-mood pipeline had, preserved here per game
        instead. Replaces each game's existing TGAs first, so a source PNG
        renamed or deleted leaves no stale file behind. A top-level
        directory not matching a known game stem is reported and skipped,
        as is a second-level directory not matching a known scene; TITLE is
        skipped silently here because convert_title owns it.
    Author: suinevere
    Dependencies: gen_scene_tables, scene_vocab, _convert_source
    Globals: SOURCE_EXT
    Params: src_root -- source PNG tree (tools/assets/png); dst_root --
        disc TGA tree (saturn/cd/data/TGA)
    Returns: dict mapping game stem to {scene: count}; both the outer and
        inner dicts are sparse -- a scene absent from the inner dict
        converted zero pictures
    """
    src_root, dst_root = Path(src_root), Path(dst_root)
    if not src_root.is_dir():
        print(f"  skip  {src_root} does not exist -- nothing to convert")
        return {}

    known_games = frozenset(stem for stem, _release, _serial in gen_scene_tables.GAMES)
    counts = {}
    for game in sorted(p.name for p in src_root.iterdir() if p.is_dir()):
        if game == "TITLE":
            continue
        if game not in known_games:
            print(f"  skip  {game}: not a known game stem "
                  f"(typo? see GAME_DIR in game_rooms.inc)")
            continue

        out_dir = dst_root / game
        out_dir.mkdir(parents=True, exist_ok=True)
        for old in out_dir.glob("*.TGA"):
            old.unlink()

        game_dir = src_root / game
        for sub in sorted(p.name for p in game_dir.iterdir() if p.is_dir()):
            if sub not in vocab.SCENE_INDEX:
                print(f"  skip  {game}/{sub}: not one of the known scenes "
                      f"(typo? see scene_vocab.SCENES)")

        scene_counts, n = {}, 0
        for scene in vocab.SCENES:
            scene_dir = game_dir / scene
            if not scene_dir.is_dir():
                continue
            sources = sorted(
                p for p in scene_dir.iterdir()
                if p.suffix.lower() in SOURCE_EXT
            )
            made = 0
            for src in sources:
                if n >= 99:
                    print(f"  {game}: more than 99 pictures, ignoring {src.name}")
                    continue
                if _convert_source(src, out_dir / f"{n + 1:02d}.TGA"):
                    n += 1
                    made += 1
            if made:
                scene_counts[scene] = made

        counts[game] = scene_counts
        summary = ", ".join(f"{s}={c}" for s, c in scene_counts.items())
        print(f"  {game}: {n} ({summary})")

    return counts


def convert_title(src_root, dst_root):
    """Convert tools/assets/png/TITLE/*.png into dst_root/TITLE/NN.TGA.

    Description: The title screen and menu need a wallpaper at boot, before
        any game is selected, so it cannot be routed through GAME_DIR /
        GAME_SCENE -- there is no game yet to index by. TITLE is a flat,
        gapless run addressed by literal filename in C, the same shape a
        mood folder used to be. Clears the folder's existing TGAs first, so
        a renamed or deleted source leaves no orphan behind. A missing
        tools/assets/png/TITLE converts nothing and returns 0 rather than
        raising, since the images may not exist yet.
    Author: suinevere
    Dependencies: _convert_source
    Globals: SOURCE_EXT
    Params: src_root -- source PNG tree (tools/assets/png); dst_root --
        disc TGA tree (saturn/cd/data/TGA)
    Returns: the number of pictures converted
    """
    title_src = Path(src_root) / "TITLE"
    out_dir = Path(dst_root) / "TITLE"
    if not title_src.is_dir():
        print("  skip  TITLE: tools/assets/png/TITLE does not exist -- "
              "nothing to convert")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.TGA"):
        old.unlink()

    sources = sorted(
        p for p in title_src.iterdir()
        if p.is_file() and p.suffix.lower() in SOURCE_EXT
    )
    n = 0
    for src in sources:
        if n >= 99:
            print(f"  TITLE: more than 99 pictures, ignoring {src.name}")
            continue
        if _convert_source(src, out_dir / f"{n + 1:02d}.TGA"):
            n += 1

    print(f"  TITLE: {n}")
    return n


def write_scene_inc(counts, path):
    """Write the generated GAME_DIR / GAME_SCENE tables consumed by display.c.

    Description: Row order is gen_scene_tables.GAMES (sorted by story stem)
        -- the exact order GAME_ROOM_MAP already uses in game_rooms.inc,
        which display.c reads by the same row index, so a mismatch would
        make every game resolve to another game's folder. Each row's
        {base, count} cells follow scene_vocab.SCENES order; a scene absent
        from counts' inner dict becomes {base, 0} at whatever base the
        scenes before it accumulated to, not a gap.
    Author: suinevere
    Dependencies: gen_scene_tables, scene_vocab
    Globals: N/A
    Params: counts -- {game_stem: {scene: count}} from convert_game_tree;
        path -- output .inc file path
    Returns: N/A
    """
    games = gen_scene_tables.GAMES
    dir_lines = [f'    "{stem}",' for stem, _release, _serial in games]

    scene_lines = []
    for stem, _release, _serial in games:
        game_counts = counts.get(stem, {})
        cells, base = [], 0
        for scene in vocab.SCENES:
            n = game_counts.get(scene, 0)
            cells.append(f"{{{base},{n}}}")
            base += n
        scene_lines.append("    { " + ", ".join(cells) + " },")

    text = (
        "/*----------------------\n"
        " | game_scenes.inc\n"
        " | Description: GENERATED FILE -- do not edit by hand; produced by\n"
        " |   tools/make_tga.py. GAME_DIR holds each story's 8.3-safe\n"
        " |   per-game folder name (the story stem); GAME_SCENE[game][scene]\n"
        " |   is where that scene's pictures sit inside the game's own\n"
        " |   1..99 index range, as {base, count} -- base is 0-based, so the\n"
        " |   nth picture of a scene is index base + n + 1. Row order matches\n"
        " |   GAME_ROOM_MAP in game_rooms.inc (gen_scene_tables.GAMES, sorted\n"
        " |   by stem); column order is scene_vocab.SCENES.\n"
        " | Author: suinevere\n"
        " ----------------------*/\n"
        "static const char *const GAME_DIR[GAME_N] = {\n"
        + "\n".join(dir_lines) + "\n"
        "};\n"
        "static const GameScene GAME_SCENE[GAME_N][SCENE_N] = {\n"
        + "\n".join(scene_lines) + "\n"
        "};\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def write_title_inc(n, path):
    """Write TITLE_ART_N, the picture count for the shared title-screen folder.

    Description: TITLE deliberately gets no row in GAME_DIR or GAME_SCENE --
        it is not a game -- so this is a sibling of write_scene_inc rather
        than a row inside it. C addresses the pictures by literal filename
        (TITLE/01.TGA..NN.TGA) and uses this constant only to iterate them.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: n -- picture count from convert_title; path -- output .inc file
        path
    Returns: N/A
    """
    text = (
        "/*----------------------\n"
        " | title_art.inc\n"
        " | Description: GENERATED FILE -- do not edit by hand; produced by\n"
        " |   tools/make_tga.py. The picture count for the shared TITLE/\n"
        " |   folder (saturn/cd/data/TGA/TITLE/01.TGA..NN.TGA), addressed by\n"
        " |   literal filename since the title screen has no game to route\n"
        " |   it through GAME_DIR/GAME_SCENE.\n"
        " | Author: suinevere\n"
        " ----------------------*/\n"
        f"#define TITLE_ART_N {n}\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main(argv):
    """CLI entry point.

    Description: With no extra arguments, regenerates the whole disc from
        the repo's real asset tree -- every game folder under
        tools/assets/png plus TITLE -- straight into saturn/cd/data/TGA,
        saturn/src/scene/game_scenes.inc and saturn/src/scene/title_art.inc.
        With two arguments where the destination does not end in .tga, runs
        the same pipeline against a caller-chosen source and disc root (the
        two .inc files still land at their real repo locations; a caller
        that wants an isolated .inc calls convert_game_tree/write_scene_inc
        directly instead, as the tests do). With two arguments where the
        destination ends in .tga, dispatches to the single-file form.
    Author: suinevere
    Dependencies: convert_game_tree, convert_title, write_scene_inc,
        write_title_inc, convert_one
    Globals: REPO
    Params: argv -- sys.argv: [prog] or [prog, src, dst]
    Returns: process exit code (0 ok, 2 on bad usage)
    """
    scene_inc = REPO / "saturn" / "src" / "scene" / "game_scenes.inc"
    title_inc = REPO / "saturn" / "src" / "scene" / "title_art.inc"

    if len(argv) == 1:
        png_root = REPO / "tools" / "assets" / "png"
        tga_root = REPO / "saturn" / "cd" / "data" / "TGA"
        counts = convert_game_tree(png_root, tga_root)
        write_scene_inc(counts, scene_inc)
        n_title = convert_title(png_root, tga_root)
        write_title_inc(n_title, title_inc)
        return 0

    if len(argv) != 3:
        print(__doc__)
        return 2

    src, dst = Path(argv[1]), Path(argv[2])
    if dst.suffix.lower() == ".tga":
        status, message = convert_one(src, dst)
        print(f"  {'wrote' if status == 'wrote' else 'skip '} {message}")
        return 0

    counts = convert_game_tree(src, dst)
    write_scene_inc(counts, scene_inc)
    n_title = convert_title(src, dst)
    write_title_inc(n_title, title_inc)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
