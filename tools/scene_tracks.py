#!/usr/bin/env python3
"""The authored scene-to-CD-DA-track assignments, and the masks they compile to.

Description: Which music a room gets is a human decision about that scene in
    that game, exactly as its picture is. This module owns the file that
    decision lives in -- tools/assets/tracks.json -- and turns it into the
    per-game, per-scene bitmasks gen_scene_tables writes into
    saturn/src/scene/game_tracks.inc.

    A scene's entry is a list of track numbers. One entry means static music:
    the mask has a single bit, so the engine's random draw has nothing to
    choose between and that scene always sounds the same. Several entries mean
    the engine picks among them, which is the only place any randomness now
    enters the room music at all.

    Two layers, defaults and per-game overrides. Thirty-one stories share one
    disc of thirty-one tracks; authoring every game separately would be
    thirty-one times the work to say the same thing about a forest. A game's
    own entry replaces the default for that scene outright rather than adding
    to it -- merging two lists would make "fewer tracks here" impossible to
    express, and narrowing is the main reason to override at all.
Author: suinevere
Dependencies: json, pathlib, scene_vocab
Globals: TRACKS_PATH, TRACK_MIN, TRACK_MAX, MASK_BITS
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_vocab as vocab

TRACKS_PATH = pathlib.Path("tools") / "assets" / "tracks.json"

TRACK_MIN = 2
TRACK_MAX = 32
"""TRACK_MIN / TRACK_MAX

Description: The disc's CD-DA range. Track 1 is the data track, and 2..32 are
    the thirty-one audio tracks listed in saturn/cd/music/tracklist. Authoring
    a number outside this is a typo, not an intention, so load() reports it
    rather than compiling a mask bit for a track that will never sound.
Author: suinevere
"""

MASK_BITS = 32
"""MASK_BITS

Description: The width of one scene's mask. Bit i is track i + TRACK_MIN, which
    is what music_track_from_mask decodes, so the thirty-one real tracks occupy
    bits 0..30 and one bit is spare.
Author: suinevere
"""


def mask_of(tracks):
    """The bitmask for a list of track numbers.

    Description: Silently drops nothing -- validate() is where a bad number is
        reported -- but refuses to shift by a bit that does not exist, because
        a mask wider than the word would corrupt the neighbouring scenes'
        columns rather than fail.
    Author: suinevere
    Dependencies: N/A
    Globals: TRACK_MIN, MASK_BITS
    Params: tracks -- an iterable of CD-DA track numbers
    Returns: an int mask
    """
    mask = 0
    for track in tracks:
        bit = int(track) - TRACK_MIN
        if 0 <= bit < MASK_BITS:
            mask |= 1 << bit
    return mask


def tracks_of(mask):
    """The track numbers a mask names, ascending.

    Description: mask_of's inverse, so a generated table can be read back and
        compared against the file that produced it -- which is how the review
        server knows the C tables have fallen behind.
    Author: suinevere
    Dependencies: N/A
    Globals: TRACK_MIN, MASK_BITS
    Params: mask -- an int mask
    Returns: a list of track numbers
    """
    return [i + TRACK_MIN for i in range(MASK_BITS) if mask & (1 << i)]


def empty():
    """The document a first run writes.

    Description: Both layers present and empty, so an editor never has to
        create a key before it can add one.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: N/A
    Returns: a fresh tracks document
    """
    return {"default": {}, "games": {}}


def load(root):
    """Read the authored assignments.

    Description: A missing or unreadable file is "nothing authored yet", which
        compiles to the all-zero table the runtime already treats as "fall
        back to the neutral pool" -- the same everything-degrades rule the
        rest of these tools follow.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: TRACKS_PATH
    Params: root -- repo root
    Returns: a tracks document with both layers present
    """
    path = pathlib.Path(root) / TRACKS_PATH
    if not path.exists():
        return empty()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return empty()
    return {"default": dict(data.get("default") or {}),
            "games": {g: dict(s or {}) for g, s in (data.get("games") or {}).items()}}


def save(root, data):
    """Write the authored assignments, sorted so its diffs stay readable.

    Description: Drops empty lists on the way out. An explicit "this scene has
        no tracks" and an absent scene compile to the same zero mask, so
        keeping both spellings in the file would only invite the question of
        which one means something.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: TRACKS_PATH
    Params: root -- repo root; data -- a tracks document
    Returns: N/A
    """
    clean = {"default": {k: sorted(v) for k, v in sorted(data.get("default", {}).items()) if v},
             "games": {}}
    for game, scenes in sorted((data.get("games") or {}).items()):
        rows = {k: sorted(v) for k, v in sorted(scenes.items()) if v}
        if rows:
            clean["games"][game] = rows
    path = pathlib.Path(root) / TRACKS_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(clean, indent=1, sort_keys=True) + "\n",
                    encoding="utf-8")


def for_game(data, game):
    """One game's effective scene -> tracks, defaults and overrides resolved.

    Description: The game's own row wins outright where it exists; elsewhere
        the default stands. Scenes with nothing authored are absent rather
        than empty, so a caller can tell "not authored" from "authored as
        silence" without inspecting list lengths.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: data -- a tracks document; game -- a story stem
    Returns: dict mapping scene name to a list of track numbers
    """
    out = {k: list(v) for k, v in (data.get("default") or {}).items() if v}
    for scene, tracks in ((data.get("games") or {}).get(game) or {}).items():
        if tracks:
            out[scene] = list(tracks)
        else:
            out.pop(scene, None)
    return out


def masks_for_game(data, game):
    """One game's row of the SCENE_TRACKS table, in scene_vocab.SCENES order.

    Description: Column order is the scene vocabulary's own order because that
        order is the C enum value; building the row here rather than in the
        generator keeps the one place that knows the encoding next to the one
        place that reads the file.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: data -- a tracks document; game -- a story stem
    Returns: a list of int masks, one per scene
    """
    resolved = for_game(data, game)
    return [mask_of(resolved.get(scene, ())) for scene in vocab.SCENES]


def validate(data):
    """Every complaint the document deserves, as plain sentences.

    Description: Reports rather than raises, and reports all of them at once:
        a generator run that stopped at the first bad track number would hide
        the other four. An unknown scene name is the important one -- it
        compiles to nothing and would otherwise be indistinguishable from a
        scene nobody has authored yet.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: TRACK_MIN, TRACK_MAX
    Params: data -- a tracks document
    Returns: a list of message strings, empty when the document is sound
    """
    problems = []
    layers = [("default", data.get("default") or {})]
    for game, scenes in sorted((data.get("games") or {}).items()):
        layers.append((game, scenes or {}))
    for where, scenes in layers:
        for scene, tracks in sorted(scenes.items()):
            if scene not in vocab.SCENE_INDEX:
                problems.append(f"{where}: {scene} is not a scene")
                continue
            for track in tracks:
                if not isinstance(track, int) or not TRACK_MIN <= track <= TRACK_MAX:
                    problems.append(
                        f"{where}/{scene}: track {track!r} is outside "
                        f"{TRACK_MIN}..{TRACK_MAX}")
    return problems


def main(argv):
    """Print the authored assignments, or the problems with them.

    Description: A read-only view for the command line; the editing surface is
        the review server's tracks page.
    Author: suinevere
    Dependencies: json, pathlib
    Globals: N/A
    Params: argv -- optionally one story stem to resolve for
    Returns: 0, or 1 when the document does not validate
    """
    root = pathlib.Path(__file__).resolve().parent.parent
    data = load(root)
    problems = validate(data)
    for line in problems:
        print(f"  {line}")
    if argv:
        resolved = for_game(data, argv[0])
        for scene in vocab.SCENES:
            tracks = resolved.get(scene)
            if tracks:
                print(f"  {argv[0]:9} {scene:<10} {tracks}")
    else:
        print(f"  default scenes authored: {len(data['default'])}")
        for game, scenes in sorted(data["games"].items()):
            print(f"  {game}: {len(scenes)} override(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))


def generated_rows(root):
    """The masks currently compiled into game_tracks.inc, one row per game.

    Description: Parses the generated table back out so the authored document
        can be compared against it. That comparison is the only honest way to
        say "the C is behind the JSON": a timestamp would call a regenerated
        but unchanged file stale, and a flag would survive a git checkout that
        moved the file underneath it.
    Author: suinevere
    Dependencies: pathlib, re
    Globals: N/A
    Params: root -- repo root
    Returns: a list of rows, each a list of int masks; empty when the file is
        absent or unparsable
    """
    import re
    path = (pathlib.Path(root) / "saturn" / "src" / "scene" / "game_tracks.inc")
    if not path.exists():
        return []
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return []
    rows = []
    for line in re.findall(r"^\s*\{ ([^}]*) \},\s*$", text, re.M):
        try:
            rows.append([int(cell.strip().rstrip("UL"), 0)
                         for cell in line.split(",")])
        except ValueError:
            return []
    return rows


def games_in(root):
    """The story stems the generated table has a row for, in its own order.

    Description: Rediscovered from the same scenes directory gen_scene_tables
        walks, rather than imported from it: that module builds its GAMES list
        at import time against the real checkout, so a caller pointing this at
        a test tree would be comparing one repository's table against another
        repository's game list.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: root -- repo root
    Returns: a sorted list of story stems
    """
    scenes = pathlib.Path(root) / "tools" / "assets" / "scenes"
    rooms = pathlib.Path(root) / "tools" / "assets" / "rooms"
    if not scenes.is_dir():
        return []
    return [p.stem for p in sorted(scenes.glob("*.json"))
            if not p.stem.endswith(".review") and (rooms / (p.stem + ".json")).exists()]


def stale_games(root, data=None):
    """The games whose compiled masks no longer match the authored document.

    Description: Names them rather than answering yes or no, so a warning can
        say which story to look at. A generated file that cannot be parsed, or
        whose shape does not match the game list, counts as stale in full --
        regenerating is cheap and being wrong the other way ships silence.
        Nothing authored and nothing generated is agreement, not staleness.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: root -- repo root; data -- an already-loaded document, or None
    Returns: a list of story stems
    """
    data = load(root) if data is None else data
    games = games_in(root)
    expected = {stem: masks_for_game(data, stem) for stem in games}
    rows = generated_rows(root)
    if not rows:
        return [stem for stem in games if any(expected[stem])]
    if len(rows) != len(games):
        return list(games)
    return [stem for stem, row in zip(games, rows) if row != expected[stem]]
