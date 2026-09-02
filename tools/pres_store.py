#!/usr/bin/env python3
"""/*----------------------
 | pres_store.py
 | Description: The per-game room -> (picture, track) assignment store that the
 |     review app writes and gen_presentation.py reads.
 |
 |     One file per game under tools/assets/presentation/, keyed by Z-machine
 |     object number as a string, because that is the key the runtime looks a
 |     room up by and JSON has no integer keys. Zork I is deliberately NOT in
 |     this store: its 110 rooms are measured off the original disc by
 |     gen_presentation.py and are not a human's to bless. Anything here would
 |     be an opinion competing with a measurement.
 |
 |     Every verdict is read-modify-written immediately rather than held in
 |     memory and flushed at exit, and every write pushes the prior value onto
 |     an undo stack, matching what the retired scene_server.py did and for the
 |     same reason: a session's verdicts are exactly the kind of state this
 |     project has lost before, and a crash must cost at most the one decision
 |     in flight.
 | Author: suinevere
 | Dependencies: json, pathlib
 | Globals: ROOT, STORE, POOL, MOOD, ROOMS, SCENES, ZORK1_STEM, NEUTRAL_POOL
 ----------------------*/"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import scene_vocab as vocab

ROOT = pathlib.Path(__file__).resolve().parent.parent
STORE = ROOT / "tools" / "assets" / "presentation"
POOL = ROOT / "tools" / "assets" / "zork1_pool.json"
MOOD = ROOT / "tools" / "assets" / "track_mood.json"
ROOMS = ROOT / "tools" / "assets" / "rooms"
SCENES = ROOT / "tools" / "assets" / "scenes"

ZORK1_STEM = "ZORK1"
"""ZORK1_STEM

Description: The one game excluded from this store. Its table is measured, not
    blessed -- see the module docstring.
Author: suinevere
"""

NEUTRAL_POOL = (0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 18, 20, 31)
"""NEUTRAL_POOL

Description: The only tracks a room may be given, mirroring P_NEUTRAL in
    saturn/src/sound/music_data.c with silence added. The disc's other fifteen
    tracks are spoken for -- the villain and danger cues, the death sting, the
    eight rank fanfares, the take sting, the ending theme and its muted
    duplicate -- and the runtime re-decides those every turn from the cue
    table, so a room that named one would either be overridden or would
    announce something that did not happen. Offering them at all would be
    offering a choice the engine does not honour.
Author: suinevere
"""


def pool():
    """/*----------------------
     | pool
     | Description: The picture and track catalogue written by gen_pool.py.
     | Author: suinevere
     | Dependencies: json
     | Globals: POOL
     | Params: N/A
     | Returns: the catalogue dict
     ----------------------*/"""
    if not POOL.is_file():
        raise SystemExit("pres_store: tools/assets/zork1_pool.json missing -- "
                         "run python tools/gen_pool.py first")
    return json.loads(POOL.read_text(encoding="utf-8"))


def tracks(p=None):
    """/*----------------------
     | tracks
     | Description: The tracks a room may actually be given -- the catalogue
     |     filtered to NEUTRAL_POOL, in disc order.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: NEUTRAL_POOL
     | Params: p -- an already-read catalogue, or None to read one
     | Returns: a list of track records
     ----------------------*/"""
    return [t for t in (p or pool())["tracks"] if t["track"] in NEUTRAL_POOL]


def mood():
    """/*----------------------
     | mood
     | Description: What each track sounds like, measured off the ripped audio
     |     by gen_track_mood.py. Returns an empty map when the file is absent
     |     rather than refusing: the mood words decorate a menu, and a review
     |     app that will not start because an optional measurement is missing
     |     has confused decoration with data.
     | Author: suinevere
     | Dependencies: json
     | Globals: MOOD
     | Params: N/A
     | Returns: {track number: measurements with a "mood" word list}
     ----------------------*/"""
    if not MOOD.is_file():
        return {}
    d = json.loads(MOOD.read_text(encoding="utf-8")).get("tracks", {})
    return {int(k): v for k, v in d.items()}


def games():
    """/*----------------------
     | games
     | Description: The story stems that can be assigned, Zork I excluded.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: ROOMS, ZORK1_STEM
     | Params: N/A
     | Returns: a sorted list of stems
     ----------------------*/"""
    return sorted(p.stem for p in ROOMS.glob("*.json") if p.stem != ZORK1_STEM)


def rooms(stem):
    """/*----------------------
     | rooms
     | Description: One game's rooms, as {obj, title, description}. Sorted by
     |     object number so the review app's queue order is stable across runs
     |     -- a queue that reshuffles between sessions makes "where was I"
     |     unanswerable.
     | Author: suinevere
     | Dependencies: json
     | Globals: ROOMS
     | Params: stem -- the story stem
     | Returns: a list of room dicts
     ----------------------*/"""
    data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
    out = [{"obj": r["obj"], "title": r.get("title", ""),
            "description": r.get("description", "")} for r in data["rooms"]]
    out.sort(key=lambda r: r["obj"])
    return out


def scenes(stem):
    """/*----------------------
     | scenes
     | Description: One game's room -> scene tags, the surviving output of the
     |     retired classification pipeline. Kept because it is 1,021 rooms of
     |     hand and rule work and it is what a suggestion is derived from --
     |     but it no longer picks anything at runtime.
     | Author: suinevere
     | Dependencies: json
     | Globals: SCENES
     | Params: stem -- the story stem
     | Returns: obj-string -> scene name, empty when the game was never tagged
     ----------------------*/"""
    p = SCENES / f"{stem}.json"
    if not p.is_file():
        return {}
    return json.loads(p.read_text(encoding="utf-8"))


def path(stem):
    """/*----------------------
     | path
     | Description: Where one game's assignments live.
     | Author: suinevere
     | Dependencies: pathlib
     | Globals: STORE
     | Params: stem -- the story stem
     | Returns: a pathlib.Path
     ----------------------*/"""
    return STORE / f"{stem}.json"


def load(stem):
    """/*----------------------
     | load
     | Description: One game's assignments, or an empty record when it has none
     |     yet. The undo stack lives in the same file as the assignments so a
     |     copied or restored file carries its own history; splitting them would
     |     let the two drift apart and make an undo replay a value that was
     |     never there.
     | Author: suinevere
     | Dependencies: json
     | Globals: N/A
     | Params: stem -- the story stem
     | Returns: {"rooms": {obj: {image, track}}, "undo": [...]}
     ----------------------*/"""
    p = path(stem)
    if not p.is_file():
        return {"rooms": {}, "undo": []}
    d = json.loads(p.read_text(encoding="utf-8"))
    d.setdefault("rooms", {})
    d.setdefault("undo", [])
    return d


def save(stem, data):
    """/*----------------------
     | save
     | Description: Writes one game's assignments. Writes to a sibling .tmp and
     |     replaces, so an interrupted write cannot leave a truncated JSON file
     |     that the next load refuses -- which would lose every verdict in the
     |     game, not just the one in flight.
     | Author: suinevere
     | Dependencies: json, pathlib
     | Globals: STORE
     | Params: stem -- the story stem; data -- the record to write
     | Returns: N/A
     ----------------------*/"""
    STORE.mkdir(parents=True, exist_ok=True)
    p = path(stem)
    tmp = p.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(data, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    tmp.replace(p)


def assign(stem, obj, image, track):
    """/*----------------------
     | assign
     | Description: Records one room's picture and track, pushing whatever was
     |     there before onto the undo stack. image 0 means "no picture", which
     |     the runtime reads as hold-what-is-showing rather than show-nothing,
     |     and track 0 means silence -- both are real choices, so neither is
     |     treated as an absent verdict.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: stem -- the story stem; obj -- object number; image -- 0..74;
     |     track -- CD-DA track, 0 for silence
     | Returns: the updated record
     ----------------------*/"""
    d = load(stem)
    key = str(obj)
    d["undo"].append({"obj": key, "prev": d["rooms"].get(key)})
    d["rooms"][key] = {"image": int(image), "track": int(track)}
    save(stem, d)
    return d


def undo(stem):
    """/*----------------------
     | undo
     | Description: Reverses the last assignment, restoring the value that was
     |     there before it -- including restoring "no verdict at all", which is
     |     why the stack stores prev rather than just the object number.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: stem -- the story stem
     | Returns: the object number reversed, or None when the stack is empty
     ----------------------*/"""
    d = load(stem)
    if not d["undo"]:
        return None
    step = d["undo"].pop()
    key = step["obj"]
    if step["prev"] is None:
        d["rooms"].pop(key, None)
    else:
        d["rooms"][key] = step["prev"]
    save(stem, d)
    return key


def scene_of(obj, title, tags):
    """/*----------------------
     | scene_of
     | Description: A room's scene tag: the one a human or a rule already gave
     |     it, else whatever the title rules can read off its name now.
     |
     |     The fallback matters more than it looks. 906 of the 1,857 rooms
     |     awaiting a verdict were never tagged -- the retired pipeline only
     |     ever ran over the games someone sat down with -- and without it every
     |     one of them would open on "no suggestion" and be hand-picked from 74
     |     pictures. The rules are the same ordered first-match-wins patterns
     |     that produced the stored tags, so a fallback tag and a stored tag mean
     |     the same thing; the caller is told which it got so it can say so.
     | Author: suinevere
     | Dependencies: scene_vocab
     | Globals: N/A
     | Params: obj -- object number; title -- the room's short name; tags --
     |     the game's stored obj-string -> scene map
     | Returns: (scene or None, "stored" | "title" | "none")
     ----------------------*/"""
    stored = tags.get(str(obj))
    if stored:
        return stored, "stored"
    guess = vocab.scene_for_title(title or "")
    if guess:
        return guess, "title"
    return None, "none"


def suggest(scene, defaults, origin="stored"):
    """/*----------------------
     | suggest
     | Description: What to offer for a room, from its scene tag alone.
     |     Returns the confidence alongside the values, because the two are not
     |     separable in practice: FOREST is four rooms that all took the same
     |     picture and CAVE is thirteen rooms that took ten, and an app that
     |     showed both as "the suggestion" would be lying about one of them.
     |
     |     A tag read off the title now is never reported better than "weak",
     |     however well the underlying scene is supported: two independent
     |     inferences are stacked at that point -- that the title names the
     |     scene, and that the scene implies the picture -- and only the second
     |     one has evidence behind it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: scene -- the room's scene tag, or None; defaults -- the
     |     catalogue's scene_defaults; origin -- "stored" or "title", from
     |     scene_of
     | Returns: {image, track, scene, confidence, why} -- confidence is one of
     |     "strong", "weak", "analogue", "none"
     ----------------------*/"""
    if not scene or scene not in defaults:
        return {"image": 0, "track": 0, "scene": None, "confidence": "none",
                "why": "no scene tag and no title rule matched -- nothing to "
                       "derive a suggestion from"}
    d = defaults[scene]
    from_title = " (scene read off the title, not stored)" if origin == "title" else ""
    if d["source"] == "analogue":
        return {"image": d["image"], "track": d["track"], "scene": scene,
                "confidence": "analogue",
                "why": f"{scene} never appears in Zork I; this is a hand-picked "
                       f"visual stand-in, not evidence{from_title}"}
    n, sup = d["n"], d["image_support"]
    pct = (100 * sup) // n if n else 0
    conf = "strong" if pct >= 60 and origin == "stored" else "weak"
    trk = d["track"] if d["track"] in NEUTRAL_POOL else 0
    return {"image": d["image"], "track": trk, "scene": scene,
            "confidence": conf,
            "why": f"{sup} of {n} Zork I {scene} rooms took this picture ({pct}%); "
                   f"{d['track_support']} of {n} took track {d['track']}{from_title}"}


def accepted(rec):
    """/*----------------------
     | accepted
     | Description: Whether a record is a finished verdict -- a picture and a
     |     track both named. A record holding only one of the two is a room
     |     half-decided, not a decided one, and the review app shows it as such
     |     rather than counting it done.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: rec -- a stored {image, track} record, or None
     | Returns: True when both are named
     ----------------------*/"""
    return bool(rec) and bool(rec.get("image")) and bool(rec.get("track"))


def basis(rec, sug):
    """/*----------------------
     | basis
     | Description: How well founded a room's current pairing is -- the strength
     |     of the association behind it, which does not stop mattering once the
     |     pairing is stored. A record that matches the suggestion keeps the
     |     suggestion's confidence; one that does not was a human overruling the
     |     evidence and is reported as "chosen", which is the strongest basis
     |     there is and the only one this app cannot derive.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: rec -- the stored record or None; sug -- suggest()'s result
     | Returns: "chosen", "strong", "weak", "analogue" or "none"
     ----------------------*/"""
    if rec and (rec.get("image") != sug["image"] or rec.get("track") != sug["track"]):
        return "chosen"
    return sug["confidence"]


def bless(stem, suggest_for=None):
    """/*----------------------
     | bless
     | Description: Writes the standing suggestion into every room of one game
     |     that has no record at all, so the table starts populated and a human
     |     revises rather than originates. Rooms that already have a record are
     |     left exactly as they are, including half-decided ones: a stored
     |     verdict is a human's and this must never overwrite one.
     |
     |     A room whose suggestion names no picture is still skipped rather
     |     than written as blank. With room_guess supplying the suggestions
     |     that no longer happens for any room on the disc, but the rule stays:
     |     a blank record hides that a room needs a human, and nothing should
     |     be able to write one by accident.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: stem -- the story stem; suggest_for -- object -> a suggestion,
     |     or None to use this module's own scene-tag suggestion
     | Returns: how many rooms were written
     ----------------------*/"""
    p = pool()
    tags = scenes(stem)
    saved = load(stem)["rooms"]
    n = 0
    for r in rooms(stem):
        if str(r["obj"]) in saved:
            continue
        if suggest_for is not None:
            s = suggest_for(r["obj"])
        else:
            scene, origin = scene_of(r["obj"], r["title"], tags)
            s = suggest(scene, p["scene_defaults"], origin)
        if not s or not s["image"]:
            continue
        assign(stem, r["obj"], s["image"], s["track"])
        n += 1
    return n


def set_rooms_track(stem, objs, track, image_for=None):
    """/*----------------------
     | set_rooms_track
     | Description: Gives one set of rooms the same track, keeping each room's
     |     picture. This is what an area's track menu writes: the store has no
     |     record of an area and never will -- areas are derived and can split
     |     when a room is re-tagged -- so setting one writes through to the
     |     rooms it currently holds, which is a verdict that survives the area
     |     it was made in.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: stem -- the story stem; objs -- the object numbers; track -- the
     |     CD-DA track, 0 for silence
     | Returns: how many rooms changed
     ----------------------*/"""
    track = int(track)
    p = pool()
    tags = scenes(stem)
    titles = {r["obj"]: r["title"] for r in rooms(stem)}
    saved = load(stem)["rooms"]
    n = 0
    for obj in objs:
        rec = saved.get(str(obj))
        if rec and rec.get("track") == track:
            continue
        if rec:
            image = rec.get("image", 0)
        elif image_for is not None:
            image = image_for(obj)
        else:
            scene, origin = scene_of(obj, titles.get(obj, ""), tags)
            image = suggest(scene, p["scene_defaults"], origin)["image"]
        if not image and not track:
            continue
        assign(stem, obj, image, track)
        n += 1
    return n


def set_all_tracks(stem, track, image_for=None):
    """/*----------------------
     | set_all_tracks
     | Description: Gives every room of one game the same track, keeping each
     |     room's picture. A room with no record keeps the picture the app was
     |     showing it -- its suggestion -- rather than losing it to a blank,
     |     because the sweep is about the track and silently dropping a picture
     |     the reviewer could see is not what was asked for.
     |
     |     One undo entry per room changed, like any other write. A sweep is not
     |     a single decision that can be taken back in one step, and pretending
     |     otherwise would mean a second, differently-shaped kind of entry on a
     |     stack whose whole value is that every entry means the same thing.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: stem -- the story stem; track -- the CD-DA track, 0 for silence
     | Returns: how many rooms changed
     ----------------------*/"""
    track = int(track)
    p = pool()
    tags = scenes(stem)
    saved = load(stem)["rooms"]
    n = 0
    for r in rooms(stem):
        rec = saved.get(str(r["obj"]))
        if rec and rec.get("track") == track:
            continue
        if rec:
            image = rec.get("image", 0)
        elif image_for is not None:
            image = image_for(r["obj"])
        else:
            scene, origin = scene_of(r["obj"], r["title"], tags)
            image = suggest(scene, p["scene_defaults"], origin)["image"]
        if not image and not track:
            continue
        assign(stem, r["obj"], image, track)
        n += 1
    return n
