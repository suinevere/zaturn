#!/usr/bin/env python3
"""Turn blessed room scenes into the C tables the Saturn runtime reads.

Description: Task 8 of the scene-tagged-art plan. Reads scene_vocab.SCENES
    (the enum vocabulary) and every tools/assets/scenes/<STEM>.json (the
    blessed room-to-scene verdicts, keyed by object number) alongside its
    matching tools/assets/rooms/<STEM>.json (release and serial), and emits
    three files: game_rooms.inc (one 256-byte room map per game plus the
    GAME_ROOM_MAP lookup table), game_tracks.inc (the per-scene track
    table -- no tracks are authored yet, and zero is the documented "fall
    back to the neutral pool" sentinel), and the generated half of
    scene_map.h (the SC_* enum, SCENE_N, GAME_N, and SCENE_NAME).

    scene_map.h is only partly this file's: Task 9 hand-writes declarations
    around a pair of sentinel comments, and this generator only ever
    rewrites the text between them, so a rerun cannot clobber Task 9's
    additions. The sentinel block is written whole even when the rest of the
    file does not exist yet -- Task 8 runs first in this plan.

    A game with no blessed rooms at all (a story with a scenes file whose
    JSON is empty) still gets a row: every unmapped object reads as 0, which
    is "no scene authored," so leaving a game out of the table is never the
    right way to represent that -- it would make scene_game_index() report
    the game as unknown rather than merely unscened.

    Determinism: GAMES is built by sorting tools/assets/scenes/*.json stems,
    and room_bytes() sorts nothing that needs sorting (a fixed 256-slot
    array indexed by object number), so a rerun over an unchanged blessed
    corpus reproduces the same bytes -- test_regeneration_is_byte_identical
    is exactly this claim.
Author: suinevere
Dependencies: json, pathlib, sys, scene_vocab
Globals: ROOT, ROOMS_DIR, SCENES_DIR, OUT_DIR, ROOMS_OUT, TRACKS_OUT,
    MAP_OUT, SENTINEL_BEGIN, SENTINEL_END, GAMES
Run: tools/.venv/Scripts/python.exe tools/gen_scene_tables.py
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_tracks
import scene_vocab as vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROOMS_DIR = ROOT / "tools" / "assets" / "rooms"
SCENES_DIR = ROOT / "tools" / "assets" / "scenes"
OUT_DIR = ROOT / "saturn" / "src" / "scene"
ROOMS_OUT = OUT_DIR / "game_rooms.inc"
TRACKS_OUT = OUT_DIR / "game_tracks.inc"
MAP_OUT = OUT_DIR / "scene_map.h"

SENTINEL_BEGIN = "/* GENERATED SCENE ENUM -- BEGIN. Written by tools/gen_scene_tables.py; do not edit. */"
SENTINEL_END = "/* GENERATED SCENE ENUM -- END */"


def _discover_games():
    """(stem, release, serial) for every story with a blessed scenes file.

    Description: (stem, release, serial) for every story with a blessed
        scenes file, sorted by stem -- this is GAMES's own definition, split
        out so module import order (GAMES is built at import time) stays
        readable.
    Author: suinevere
    Dependencies: json, pathlib
    Globals: SCENES_DIR, ROOMS_DIR
    Params: N/A
    Returns: a list of (stem, release, serial) sorted by stem
    """
    games = []
    for path in sorted(SCENES_DIR.glob("*.json")):
        if path.stem.endswith(".review"):
            continue
        stem = path.stem
        rooms_path = ROOMS_DIR / (stem + ".json")
        if not rooms_path.exists():
            continue
        data = json.loads(rooms_path.read_text(encoding="utf-8"))
        games.append((stem, data["release"], data["serial"]))
    return games


GAMES = _discover_games()


def room_bytes(stem):
    """One game's 256-byte room map: index is the Z-machine object
    number, value is the scene index plus one, 0 meaning no scene authored.
    256 because a v3 object table holds at most 255 objects, so the whole
    map is one fixed array and a lookup needs no bounds table.

    Description: One game's 256-byte room map: index is the Z-machine object
        number, value is the scene index plus one, 0 meaning no scene
        authored. 256 because a v3 object table holds at most 255 objects,
        so the whole map is one fixed array and a lookup needs no bounds
        table.
    Author: suinevere
    Dependencies: json, scene_vocab
    Globals: SCENES_DIR
    Params: stem -- a story stem, e.g. "ZORK1"
    Returns: a list of 256 ints
    """
    path = SCENES_DIR / (stem + ".json")
    blessed = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    row = [0] * 256
    for obj, scene in blessed.items():
        n = int(obj)
        if 0 <= n < 256:
            row[n] = vocab.SCENE_INDEX[scene] + 1
    return row


def _c_string(s):
    """A C string literal for s.

    Description: A C string literal for s, escaping backslash and quote.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: s -- the string to quote
    Returns: a quoted, escaped C string literal
    """
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _write_game_rooms():
    """Writes game_rooms.inc: one GAME_ROOM_<STEM>[256] per game plus
    GAME_ROOM_MAP.

    Description: Writes game_rooms.inc: one GAME_ROOM_<STEM>[256] per game
        plus GAME_ROOM_MAP.
    Author: suinevere
    Dependencies: pathlib
    Globals: GAMES, ROOMS_OUT
    Params: N/A
    Returns: N/A
    """
    lines = []
    lines.append("/*----------------------")
    lines.append(" | game_rooms.inc")
    lines.append(" | Description: GENERATED FILE -- do not edit by hand; produced by")
    lines.append(" |   tools/gen_scene_tables.py. Per-game 256-byte object-to-scene maps")
    lines.append(" |   (value is scene index plus one, 0 meaning unmapped) and the")
    lines.append(" |   GAME_ROOM_MAP table that keys them by release and serial.")
    lines.append(" | Author: suinevere")
    lines.append(" ----------------------*/")
    lines.append("typedef struct {")
    lines.append("    unsigned short release;")
    lines.append("    const char *serial;")
    lines.append("    const unsigned char *rooms;")
    lines.append("} GameRoomMap;")
    for stem, _release, _serial in GAMES:
        rows = room_bytes(stem)
        lines.append(f"static const unsigned char GAME_ROOM_{stem}[256] = {{")
        for i in range(0, 256, 16):
            chunk = ", ".join(str(v) for v in rows[i:i + 16])
            lines.append(f"    {chunk},")
        lines.append("};")
    lines.append("static const GameRoomMap GAME_ROOM_MAP[GAME_N] = {")
    for stem, release, serial in GAMES:
        lines.append(f"    {{ {release}, {_c_string(serial)}, GAME_ROOM_{stem} }},")
    lines.append("};")
    ROOMS_OUT.parent.mkdir(parents=True, exist_ok=True)
    with ROOMS_OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def _write_game_tracks():
    """Writes game_tracks.inc: SCENE_TRACKS[GAME_N][SCENE_N] from tracks.json.

    Description: Compiles the authored scene->track assignments into one mask
        per (game, scene). A scene nobody has authored stays zero, which the
        runtime reads as "fall back to the neutral pool" -- a documented
        sentinel, not a placeholder to be confused with a bug. Row order is
        GAMES and column order is scene_vocab.SCENES, because both are index
        positions in the generated C.

        Prints the document's problems rather than raising on them: an unknown
        scene name or an out-of-range track compiles to nothing, and the run
        that would tell you so is the one you want to finish.
    Author: suinevere
    Dependencies: pathlib, scene_tracks
    Globals: GAMES, TRACKS_OUT, ROOT
    Params: N/A
    Returns: the number of (game, scene) pairs with at least one track
    """
    data = scene_tracks.load(ROOT)
    for problem in scene_tracks.validate(data):
        print(f"  tracks.json: {problem}")

    lines = []
    lines.append("/*----------------------")
    lines.append(" | game_tracks.inc")
    lines.append(" | Description: GENERATED FILE -- do not edit by hand; produced by")
    lines.append(" |   tools/gen_scene_tables.py from tools/assets/tracks.json. One")
    lines.append(" |   CD-DA track mask per game per scene; bit i is track")
    lines.append(" |   i + MUSIC_TRACK_MIN. A zero means no tracks authored, which the")
    lines.append(" |   runtime reads as \"fall back to the neutral pool.\" A mask with one")
    lines.append(" |   bit is static music: the engine's draw has nothing to choose.")
    lines.append(" | Author: suinevere")
    lines.append(" ----------------------*/")
    lines.append("static const unsigned long SCENE_TRACKS[GAME_N][SCENE_N] = {")
    authored = 0
    for stem, _release, _serial in GAMES:
        masks = scene_tracks.masks_for_game(data, stem)
        authored += sum(1 for m in masks if m)
        cells = ", ".join(("0" if m == 0 else f"0x{m:08X}UL") for m in masks)
        lines.append(f"    {{ {cells} }},")
    lines.append("};")
    TRACKS_OUT.parent.mkdir(parents=True, exist_ok=True)
    with TRACKS_OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return authored


def _sentinel_block():
    """The generated enum block's full text, sentinels included.

    Description: The generated enum block's full text, sentinels included --
        the SC_* enum in scene_vocab.SCENES order, SCENE_N, GAME_N, and the
        SCENE_NAME string table.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: GAMES, SENTINEL_BEGIN, SENTINEL_END
    Params: N/A
    Returns: the sentinel block as a single string, no trailing newline
    """
    lines = [SENTINEL_BEGIN]
    lines.append(f"#define SCENE_N {len(vocab.SCENES)}")
    lines.append(f"#define GAME_N {len(GAMES)}")
    lines.append("enum {")
    for i, name in enumerate(vocab.SCENES):
        lines.append(f"    SC_{name} = {i},")
    lines.append("};")
    lines.append(f"static const char *const SCENE_NAME[SCENE_N] = {{")
    for name in vocab.SCENES:
        lines.append(f"    {_c_string(name)},")
    lines.append("};")
    lines.append(SENTINEL_END)
    return "\n".join(lines)


def _write_scene_map_h():
    """Rewrites only the sentinel-delimited region of scene_map.h.

    Description: Rewrites only the sentinel-delimited region of
        scene_map.h, creating the file with just that block if it does not
        exist, and replacing only the text between the sentinels -- inclusive
        of the sentinel lines themselves -- if it does, so a hand-edit
        outside the block (Task 9's declarations) survives regeneration.
    Author: suinevere
    Dependencies: pathlib
    Globals: MAP_OUT, SENTINEL_BEGIN, SENTINEL_END
    Params: N/A
    Returns: N/A
    """
    block = _sentinel_block()
    if MAP_OUT.exists():
        text = MAP_OUT.read_text(encoding="utf-8")
        if SENTINEL_BEGIN in text and SENTINEL_END in text:
            start = text.index(SENTINEL_BEGIN)
            end = text.index(SENTINEL_END) + len(SENTINEL_END)
            text = text[:start] + block + text[end:]
        else:
            text = block + "\n"
    else:
        text = block + "\n"
    MAP_OUT.parent.mkdir(parents=True, exist_ok=True)
    with MAP_OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main(argv):
    """Regenerates game_rooms.inc, game_tracks.inc and scene_map.h's
    sentinel block.

    Description: Regenerates game_rooms.inc, game_tracks.inc and
        scene_map.h's sentinel block from the blessed scene corpus.
    Author: suinevere
    Dependencies: N/A
    Globals: GAMES, ROOMS_OUT, TRACKS_OUT, MAP_OUT
    Params: argv -- command-line arguments (unused; accepted for test-call
        symmetry with other generators in this plan)
    Returns: 0
    """
    _write_game_rooms()
    authored = _write_game_tracks()
    _write_scene_map_h()
    print(f"gen_scene_tables: {len(GAMES)} games -> {ROOMS_OUT}, {TRACKS_OUT}, {MAP_OUT}")
    print(f"  {authored} (game, scene) pair(s) have authored music")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
