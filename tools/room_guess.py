#!/usr/bin/env python3
"""/*----------------------
 | room_guess.py
 | Description: A picture for every room and a track for every area, including
 |     the ones nothing can be derived for.
 |
 |     951 of the 1,857 rooms carry no scene tag and match no title rule, and
 |     under the old pipeline every one of them sat blank -- which reads as
 |     "not done yet" when what it actually means is "nothing here will ever
 |     get better on its own". A room holding a deliberate wrong picture can be
 |     seen and fixed. A room holding nothing cannot be seen at all.
 |
 |     So four layers run in falling order of how well each one holds up, and
 |     each says which one answered. The order is measured, not assumed: the
 |     two guessing layers were checked by hiding the scene tags of the 951
 |     rooms that have one and asking each layer to recover them.
 |
 |       description  83% (418 rooms) -- the room's own prose, counted across
 |                    every rule in the vocabulary rather than first-match,
 |                    because prose mentions a forest through a window and a
 |                    title does not
 |       neighbours   52% (806 rooms) -- the majority scene of the rooms it
 |                    opens onto, spread outward until it stops moving
 |       game         the game's own commonest scene, for a room connected to
 |                    nothing that was reached
 |       genre        game_genre.fallback, for a game with nothing anywhere
 |
 |     Everything these layers produce is marked "guess" and the app shows it
 |     as such. That is the whole point: the gap is closed with something
 |     visible and wrong rather than left open and invisible.
 | Author: suinevere
 | Dependencies: collections, game_genre, image_looks, pres_store, re,
 |     room_groups, scene_vocab, zexits
 | Globals: MARGIN, _WORD_RE
 ----------------------*/"""
import collections
import zlib
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import game_genre as genre_vocab
import image_looks
import pres_store as store
import room_groups
import scene_vocab as vocab
import zexits

MARGIN = 1
"""MARGIN

Description: How far ahead the winning scene must be before a description is
    allowed to name one. A tie between CAVE and RIVER in one paragraph is the
    prose describing an underground stream, and picking either is worse than
    handing the room to the next layer.
Author: suinevere
"""


_WORD_RE = {}


def _word_re(pat):
    """/*----------------------
     | _word_re
     | Description: One vocabulary pattern as a whole-word match, compiled
     |     once. Substring matching is what scene_for_title does and is
     |     survivable on a three-word title; over a paragraph it reads "pit"
     |     out of "pitch black" and "in" out of half the English language.
     | Author: suinevere
     | Dependencies: re
     | Globals: _WORD_RE
     | Params: pat -- a scene_vocab rule pattern
     | Returns: a compiled regular expression
     ----------------------*/"""
    r = _WORD_RE.get(pat)
    if r is None:
        r = _WORD_RE[pat] = re.compile(r"\b" + re.escape(pat) + r"\b")
    return r


def text_scene(text):
    """/*----------------------
     | text_scene
     | Description: The scene a room's own description argues for, by counting
     |     every vocabulary rule that matches anywhere in it and taking the
     |     winner. Counted rather than first-match, which is what
     |     scene_vocab.scene_for_title does and is right for a title: a title
     |     is three words and the first match is the only match, while a
     |     paragraph mentions the forest it faces, the path it leaves by and
     |     the house behind it, and only the majority means anything.
     |
     |     Recovers 79% of the stored tags of the 386 tagged rooms it can
     |     answer for at all, tags held out.
     | Author: suinevere
     | Dependencies: collections, re, scene_vocab
     | Globals: MARGIN, _WORD_RE
     | Params: text -- the room description, possibly None
     | Returns: a scene name, or None when nothing wins clearly
     ----------------------*/"""
    if not text:
        return None
    low = text.lower()
    hits = collections.Counter()
    for pat, scene in vocab.RULES:
        if scene is None:
            continue
        n = len(_word_re(pat).findall(low))
        if n:
            hits[scene] += n
    if not hits:
        return None
    top = hits.most_common(2)
    if len(top) > 1 and top[0][1] - top[1][1] < MARGIN:
        return None
    return top[0][0]


def _spread(scene, adj, rooms):
    """/*----------------------
     | _spread
     | Description: Hands every still-unscened room the majority scene of the
     |     rooms it opens onto, over and over until nothing moves. One pass
     |     would leave a corridor two rooms deep unreached; running it to a
     |     fixed point walks the scene out along the map the way a person
     |     reading it would.
     |
     |     A round is computed against the previous round's scenes and applied
     |     at the end of it, so the answer does not depend on which room is
     |     visited first.
     | Author: suinevere
     | Dependencies: collections
     | Globals: N/A
     | Params: scene -- room -> scene or None, mutated; adj -- room ->
     |     neighbours; rooms -- every room of the game
     | Returns: the set of rooms this filled in
     ----------------------*/"""
    filled = set()
    while True:
        round_ = {}
        for o in sorted(rooms):
            if scene.get(o):
                continue
            nb = collections.Counter(scene[k] for k in adj.get(o, ())
                                     if scene.get(k))
            if nb:
                round_[o] = nb.most_common(1)[0][0]
        if not round_:
            return filled
        scene.update(round_)
        filled |= set(round_)


def picture(scene, defaults, rank, allowed=()):
    """/*----------------------
     | picture
     | Description: Which picture a room of one scene gets, given how many
     |     areas of that scene came before it: one of the pictures drawn for
     |     that scene, spread one per area, so an area stays visually of a piece
     |     while the next area of the same kind looks different.
     |
     |     Nothing is pinned any more. A scene Zork I agreed strongly about used
     |     to keep its measured picture for every room of every game, on the
     |     reasoning that four of four FOREST rooms taking picture 3 is not a
     |     thing to improve on -- which is true of Zork I, and Zork I's table is
     |     measured and never comes through here. Applied to the other thirty it
     |     meant seventy-one maze rooms across many games showing one picture
     |     because one game's maze was coherent.
     |
     |     Per area rather than per room on purpose. Rotating room by room
     |     would make a corridor change its walls at every step, which is worse
     |     than the repetition it was fixing.
     |
     |     `allowed` narrows the list to the pictures that also suit the game's
     |     genre, and only when something survives the narrowing -- a forest in
     |     a science fiction story is still a forest, and a genre that agrees
     |     with none of a scene's pictures has nothing to say about that scene.
     |     The genre lists name Zork I's pictures, so for a scene that has art
     |     of its own the narrowing now finds no overlap and stands aside, which
     |     is that same rule reached from the other direction.
     | Author: suinevere
     | Dependencies: image_looks
     | Globals: N/A
     | Params: scene -- the scene name; defaults -- the catalogue's
     |     scene_defaults; rank -- how many areas of this scene came first;
     |     allowed -- the genre's pictures, narrowing the choice when the two
     |     lists meet and ignored when they do not
     | Returns: a picture index, or 0 when the scene has none
     ----------------------*/"""
    d = defaults.get(scene) or {}
    have = image_looks.room_images(scene)
    if not have:
        return d.get("image", 0)
    if allowed:
        narrowed = tuple(i for i in have if i in allowed)
        if narrowed:
            have = narrowed
    return have[rank % len(have)]


def suggestions(stem, pool=None, areas=None):
    """/*----------------------
     | suggestions
     | Description: Every room of one game and what to show in it, with the
     |     basis for it. Rooms with a scene tag or a title rule keep exactly
     |     the suggestion pres_store.suggest already made for them, evidence
     |     and confidence unchanged; the rest are filled by the guessing
     |     layers and marked "guess", which is a fifth confidence the app
     |     shows as "best guess".
     | Author: suinevere
     | Dependencies: collections, game_genre, pres_store, zexits
     | Globals: N/A
     | Params: stem -- the story stem; pool -- an already-read catalogue
     | Returns: {object number: a suggestion dict}
     ----------------------*/"""
    p = pool or store.pool()
    defaults = p["scene_defaults"]
    tags = store.scenes(stem)
    rows = {r["obj"]: r for r in store.rooms(stem)}
    raw = zexits.story(stem)
    adj = zexits.neighbours(zexits.graph(raw)) if raw else {}
    areas = areas if areas is not None else room_groups.groups(stem, p)
    where = {o: n for n, a in enumerate(areas) for o in a["rooms"]}

    out = {}
    scene = {}
    origin = {}
    for o, r in rows.items():
        s, how = store.scene_of(o, r["title"] or "", tags)
        if s:
            scene[o] = s
            origin[o] = how

    known = set(scene)
    for o, r in rows.items():
        if o in known:
            continue
        s = text_scene(r["description"])
        if s:
            scene[o] = s
            origin[o] = "text"

    for o in _spread(scene, adj, set(rows)):
        origin[o] = "neighbour"

    common = collections.Counter(scene[o] for o in known)
    game_scene = common.most_common(1)[0][0] if common else None
    fb_scene, _fb_track = genre_vocab.fallback(stem)
    for o in rows:
        if o in scene:
            continue
        scene[o] = game_scene or fb_scene
        origin[o] = "game" if game_scene else "genre"

    why = {
        "text": "guessed from the room's own description, which names this "
                "kind of place more often than any other",
        "neighbour": "guessed from the rooms it opens onto, which are this "
                     "kind of place",
        "game": "guessed from the rest of this game, which is mostly this "
                "kind of place",
        "genre": "nothing in this game names a place at all -- this is the "
                 "genre's fallback",
    }
    genre_imgs = set(genre_vocab.images_for(stem))
    rank = {}
    seen = collections.Counter()
    offset = zlib.crc32(stem.encode("utf-8"))
    for a in sorted(areas, key=lambda a: a["id"]):
        names = collections.Counter(scene[o] for o in a["rooms"] if scene.get(o))
        s = names.most_common(1)[0][0] if names else None
        rank[a["id"]] = seen[s] + offset
        seen[s] += 1

    by_id = {n: a["id"] for n, a in enumerate(areas)}
    for o in rows:
        s = scene[o]
        how = origin[o]
        evidence = how in ("stored", "title")
        sug = store.suggest(s, defaults, how if evidence else "stored")
        sug["image"] = picture(s, defaults, rank.get(by_id.get(where.get(o, 0), 0), 0),
                               () if evidence else genre_imgs)
        if not evidence:
            sug["confidence"] = "guess"
            sug["why"] = f"{why[how]} ({s}, narrowed to the genre)" if genre_imgs                 else f"{why[how]} ({s})"
        out[o] = sug
    return out


def area_tracks(stem, pool=None, areas=None, sug=None):
    """/*----------------------
     | area_tracks
     | Description: A track for every area, including the ones no evidence
     |     reaches. An area whose scene has a measured track keeps it; the rest
     |     take, in order, the majority track of the areas they open onto, the
     |     game's own commonest, and the genre's.
     |
     |     Per area rather than per room because an area is what a track
     |     belongs to -- see room_groups -- and because a guess spread evenly
     |     over a whole area at least sounds deliberate, while a guess made
     |     room by room would have the music change as the player walks through
     |     one place.
     | Author: suinevere
     | Dependencies: collections, game_genre, pres_store, room_groups, zexits
     | Globals: N/A
     | Params: stem -- the story stem; pool -- an already-read catalogue;
     |     areas -- room_groups.groups' result; sug -- suggestions' result
     | Returns: {area id: (track, "measured" | "neighbour" | "game" | "genre")}
     ----------------------*/"""
    p = pool or store.pool()
    areas = areas if areas is not None else room_groups.groups(stem, p)
    sug = sug if sug is not None else suggestions(stem, p, areas)
    raw = zexits.story(stem)
    adj = zexits.neighbours(zexits.graph(raw)) if raw else {}

    owner = {o: a["id"] for a in areas for o in a["rooms"]}
    track = {a["id"]: a["track"] for a in areas}
    how = {i: ("measured" if t else None) for i, t in track.items()}

    touching = collections.defaultdict(collections.Counter)
    for a in areas:
        for o in a["rooms"]:
            for k in adj.get(o, ()):
                j = owner.get(k)
                if j is not None and j != a["id"] and track.get(j):
                    touching[a["id"]][track[j]] += 1

    while True:
        moved = False
        for i, t in list(track.items()):
            if t or not touching[i]:
                continue
            track[i] = touching[i].most_common(1)[0][0]
            how[i] = "neighbour"
            moved = True
        if not moved:
            break
        touching = collections.defaultdict(collections.Counter)
        for a in areas:
            for o in a["rooms"]:
                for k in adj.get(o, ()):
                    j = owner.get(k)
                    if j is not None and j != a["id"] and track.get(j):
                        touching[a["id"]][track[j]] += 1

    seen = collections.Counter(t for i, t in track.items() if t and how[i] == "measured")
    game_track = seen.most_common(1)[0][0] if seen else 0
    _fb_scene, fb_track = genre_vocab.fallback(stem)
    for i, t in track.items():
        if t:
            continue
        track[i] = game_track or fb_track
        how[i] = "game" if game_track else "genre"
    return {i: (track[i], how[i]) for i in track}


def blessing(stem, pool=None):
    """/*----------------------
     | blessing
     | Description: What to write into every room of one game that has no
     |     record: the picture its own layers argued for, and the track its
     |     area was given. Picture per room and track per area, because that is
     |     what each is actually a property of.
     | Author: suinevere
     | Dependencies: pres_store, room_groups
     | Globals: N/A
     | Params: stem -- the story stem; pool -- an already-read catalogue
     | Returns: {object number: {image, track, confidence, why, scene}}
     ----------------------*/"""
    p = pool or store.pool()
    areas = room_groups.groups(stem, p)
    sug = suggestions(stem, p, areas)
    tracks = area_tracks(stem, p, areas, sug)
    out = {}
    for a in areas:
        track = tracks[a["id"]][0]
        for o in a["rooms"]:
            d = dict(sug.get(o, {"image": 0, "confidence": "none", "why": "", "scene": None}))
            d["track"] = track
            out[o] = d
    return out
