#!/usr/bin/env python3
"""Which scenes each CD-DA track belongs to, and the masks that compiles to.

Description: Owns tools/assets/tracks.json and turns it into the per-scene
    bitmasks gen_scene_tables writes into saturn/src/scene/game_tracks.inc.

    Keyed by track, not by scene, because that is the decision being made: a
    person listens to track 17 and says where it belongs. The generator wants
    the inverse and computes it; storing it the other way round would put the
    file and the page that edits it in different shapes for no gain.

    There is no per-game dimension. Art is duplicated per game because a
    picture is small; the thirty-one CD-DA tracks are already most of the disc
    and every story shares them, so a scene sounds the same in every game and
    one row of masks describes the whole library.

    A scene named by exactly one track is static music: the mask has a single
    bit, the engine's draw has nothing to choose between, and that scene always
    sounds the same. Naming it in several tracks is the only place any
    randomness enters the room music.
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

Description: The disc's CD-DA range. Track 1 is the data track; 2..32 are the
    thirty-one audio tracks in saturn/cd/music/tracklist. A number outside it
    is a typo, not an intention, so validate() reports it rather than letting
    it compile to a bit nothing will ever play.
Author: suinevere
"""

MASK_BITS = 32
"""MASK_BITS

Description: The width of one scene's mask. Bit i is track i + TRACK_MIN,
    which is what music_track_from_mask decodes, so the thirty-one real tracks
    occupy bits 0..30 and one bit is spare.
Author: suinevere
"""


def tracks():
    """Every CD-DA track number on the disc, ascending.

    Description: The rows of the authoring page, and the only track numbers
        that may appear in the document.
    Author: suinevere
    Dependencies: N/A
    Globals: TRACK_MIN, TRACK_MAX
    Params: N/A
    Returns: a list of track numbers
    """
    return list(range(TRACK_MIN, TRACK_MAX + 1))


def load(root):
    """Read the authored track -> scenes assignments.

    Description: A missing or unreadable file is "nothing authored yet", which
        compiles to an all-zero table -- the sentinel the runtime already
        reads as "fall back to the neutral pool". Keys arrive from JSON as
        strings and leave here as ints, so no caller has to remember which.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: TRACKS_PATH
    Params: root -- repo root
    Returns: dict mapping track number to a list of scene names
    """
    path = pathlib.Path(root) / TRACKS_PATH
    if not path.exists():
        return {}
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}
    out = {}
    for key, scenes in (raw or {}).items():
        try:
            out[int(key)] = list(scenes or ())
        except (TypeError, ValueError):
            continue
    return out


def save(root, data):
    """Write the assignments, sorted so its diffs stay readable.

    Description: Drops tracks with no scenes on the way out. An empty list and
        an absent key compile to the same nothing, so keeping both spellings
        would only invite the question of which one means something.
    Author: suinevere
    Dependencies: pathlib, json
    Globals: TRACKS_PATH
    Params: root -- repo root; data -- track -> scenes
    Returns: N/A
    """
    clean = {str(t): sorted(set(s)) for t, s in sorted(data.items()) if s}
    path = pathlib.Path(root) / TRACKS_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(clean, indent=1, sort_keys=True) + "\n",
                    encoding="utf-8")


def by_scene(data):
    """The inverse: scene -> the tracks that named it, ascending.

    Description: What the generator and the runtime think in. Built here so
        the one place that knows the file's shape is the one place that
        translates out of it.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: data -- track -> scenes
    Returns: dict mapping scene name to a sorted list of track numbers
    """
    out = {}
    for track, scenes in data.items():
        for scene in scenes:
            out.setdefault(scene, []).append(track)
    return {scene: sorted(set(found)) for scene, found in out.items()}


def mask_of(track_numbers):
    """The bitmask for a list of track numbers.

    Description: Refuses to shift by a bit that does not exist -- a mask wider
        than the word would corrupt the neighbouring scenes' columns rather
        than fail. validate() is where a bad number is reported.
    Author: suinevere
    Dependencies: N/A
    Globals: TRACK_MIN, MASK_BITS
    Params: track_numbers -- an iterable of CD-DA track numbers
    Returns: an int mask
    """
    mask = 0
    for track in track_numbers:
        bit = int(track) - TRACK_MIN
        if 0 <= bit < MASK_BITS:
            mask |= 1 << bit
    return mask


def tracks_of(mask):
    """The track numbers a mask names, ascending.

    Description: mask_of's inverse, so a generated table can be read back and
        compared against the file that produced it -- which is how the review
        server knows the C table has fallen behind.
    Author: suinevere
    Dependencies: N/A
    Globals: TRACK_MIN, MASK_BITS
    Params: mask -- an int mask
    Returns: a list of track numbers
    """
    return [i + TRACK_MIN for i in range(MASK_BITS) if mask & (1 << i)]


def masks(data):
    """The whole SCENE_TRACKS row, in scene_vocab.SCENES order.

    Description: Column order is the scene vocabulary's own order because that
        order is the C enum value.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: N/A
    Params: data -- track -> scenes
    Returns: a list of int masks, one per scene
    """
    inverted = by_scene(data)
    return [mask_of(inverted.get(scene, ())) for scene in vocab.SCENES]


def validate(data):
    """Every complaint the document deserves, as plain sentences.

    Description: Reports rather than raises, and reports all of them at once:
        a generator run that stopped at the first bad number would hide the
        other four. An unknown scene name is the important one -- it compiles
        to nothing and would otherwise look exactly like a scene nobody has
        authored yet.
    Author: suinevere
    Dependencies: scene_vocab
    Globals: TRACK_MIN, TRACK_MAX
    Params: data -- track -> scenes
    Returns: a list of message strings, empty when the document is sound
    """
    problems = []
    for track, scenes in sorted(data.items()):
        if not isinstance(track, int) or not TRACK_MIN <= track <= TRACK_MAX:
            problems.append(
                f"track {track!r} is outside {TRACK_MIN}..{TRACK_MAX}")
            continue
        for scene in scenes:
            if scene not in vocab.SCENE_INDEX:
                problems.append(f"track {track}: {scene} is not a scene")
    return problems


def generated_masks(root):
    """The masks currently compiled into game_tracks.inc.

    Description: Parses the generated table back out so the authored document
        can be compared against it. That comparison is the only honest way to
        say "the C is behind the JSON": a timestamp would call a regenerated
        but unchanged file stale, and a flag would survive a git checkout that
        moved the file underneath it.
    Author: suinevere
    Dependencies: pathlib, re
    Globals: N/A
    Params: root -- repo root
    Returns: a list of int masks, or None when absent or unparsable
    """
    import re
    path = pathlib.Path(root) / "saturn" / "src" / "scene" / "game_tracks.inc"
    if not path.exists():
        return None
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    found = re.search(r"SCENE_TRACKS\[SCENE_N\]\s*=\s*\{([^}]*)\}", text, re.S)
    if not found:
        return None
    try:
        return [int(cell.strip().rstrip("UL"), 0)
                for cell in found.group(1).split(",") if cell.strip()]
    except ValueError:
        return None


def is_stale(root, data=None):
    """Whether the compiled table no longer matches the authored document.

    Description: Nothing authored and nothing generated is agreement, not
        staleness. An unparsable table counts as stale: regenerating is cheap
        and being wrong the other way ships silence.
    Author: suinevere
    Dependencies: N/A
    Globals: N/A
    Params: root -- repo root; data -- an already-loaded document, or None
    Returns: True when tools/gen_scene_tables.py needs re-running
    """
    data = load(root) if data is None else data
    want = masks(data)
    have = generated_masks(root)
    if have is None:
        return any(want)
    return have != want


def main(argv):
    """Print what each scene plays, and any problems with the document.

    Description: A read-only view for the command line; the editing surface is
        the review server's music page.
    Author: suinevere
    Dependencies: pathlib
    Globals: N/A
    Params: argv -- unused
    Returns: 0, or 1 when the document does not validate
    """
    root = pathlib.Path(__file__).resolve().parent.parent
    data = load(root)
    problems = validate(data)
    for line in problems:
        print(f"  {line}")
    inverted = by_scene(data)
    for scene in vocab.SCENES:
        found = inverted.get(scene)
        print(f"  {scene:<10} {found if found else '(neutral pool)'}")
    print(f"  stale: {is_stale(root, data)}")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
