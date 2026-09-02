#!/usr/bin/env python3
"""/*----------------------
 | room_groups.py
 | Description: Each game's rooms gathered into areas -- runs of adjacent rooms
 |     of the same kind, which is the unit a track actually belongs to. Zork I
 |     did not give a track to a room, it gave one to the forest, to the house,
 |     to the maze; the per-room table is how that was stored, not how it was
 |     decided.
 |
 |     An area is a connected component of the exit graph in which every edge
 |     joins two rooms carrying the same scene tag. Both halves are needed and
 |     neither is enough. Connectivity alone gives two components for all of
 |     Zork I, because the river reaches the canyon and the maze reaches the
 |     cellar; the scene tag alone gives fifteen groups at 59% track purity,
 |     because Zork I's four forests are not one place. Together they give 91%.
 |
 |     A component left holding one room is then absorbed into whichever
 |     neighbouring area it has the most edges to, smallest first, repeatedly.
 |     Without that step Zork I comes out as 75 areas, 61 of them a single room
 |     -- technically pure and useless to assign anything to.
 |
 |     Measured against Zork I, whose real areas are known from which picture
 |     archive each room was drawn from: 33 areas, 91% of rooms in an area
 |     whose majority archive is their own, 90% in one whose majority track is
 |     their own. That number is the honest claim for the other thirty games,
 |     where nothing can check it.
 |
 |     Scene tags come from pres_store.scene_of, so a room the retired
 |     classifier never reached still gets the tag its title implies, and rooms
 |     with no tag at all group with their untagged neighbours rather than
 |     each standing alone.
 | Author: suinevere
 | Dependencies: collections, pres_store, zexits
 | Globals: N/A
 ----------------------*/"""
import collections
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import pres_store as store
import zexits


def _components(rooms, adj, key):
    """/*----------------------
     | _components
     | Description: Connected components of the graph restricted to edges whose
     |     ends agree under key.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: rooms -- the room set; adj -- room -> neighbours; key -- room ->
     |     the value two rooms must share for their edge to count
     | Returns: a list of sorted room lists
     ----------------------*/"""
    seen, out = set(), []
    for n in sorted(rooms):
        if n in seen:
            continue
        stack, comp = [n], []
        seen.add(n)
        while stack:
            c = stack.pop()
            comp.append(c)
            for k in sorted(adj.get(c, ())):
                if k in seen or k not in rooms or key(k) != key(c):
                    continue
                seen.add(k)
                stack.append(k)
        out.append(sorted(comp))
    return out


def _absorb(comps, adj):
    """/*----------------------
     | _absorb
     | Description: Folds every one-room area into the neighbouring area it has
     |     the most edges to, smallest neighbour winning a tie so the fold does
     |     not all run into whichever area happens to be biggest. Repeated
     |     until no lone room has anywhere to go, because absorbing one can
     |     leave another with a neighbour it did not have before.
     | Author: suinevere
     | Dependencies: collections
     | Globals: N/A
     | Params: comps -- the components; adj -- room -> neighbours
     | Returns: a list of sorted room lists
     ----------------------*/"""
    out = {i: list(g) for i, g in enumerate(comps)}
    owner = {n: i for i, g in out.items() for n in g}
    while True:
        moved = False
        for i in [i for i, g in out.items() if len(g) == 1]:
            n = out[i][0]
            cand = collections.Counter(owner[k] for k in adj.get(n, ())
                                       if owner.get(k) is not None and owner[k] != i)
            if not cand:
                continue
            tgt = max(cand, key=lambda c: (cand[c], -len(out[c])))
            out[tgt].append(n)
            owner[n] = tgt
            del out[i]
            moved = True
            break
        if not moved:
            break
    return [sorted(g) for g in out.values()]


def _label(rooms, titles):
    """/*----------------------
     | _label
     | Description: What to call an area: the title most of its rooms share,
     |     else the title of its lowest-numbered room. Zork I's maze is fifteen
     |     rooms called Maze and the label writes itself; a corridor of five
     |     differently-named rooms is named for the first of them, which is at
     |     least a place the reviewer can find.
     | Author: suinevere
     | Dependencies: collections
     | Globals: N/A
     | Params: rooms -- the area's object numbers; titles -- object -> title
     | Returns: a display name
     ----------------------*/"""
    have = [titles.get(n, "") for n in rooms if titles.get(n)]
    if not have:
        return f"rooms {rooms[0]}-{rooms[-1]}"
    common = collections.Counter(have).most_common(1)[0]
    return common[0] if common[1] > 1 else titles.get(rooms[0], have[0])


def groups(stem, pool=None):
    """/*----------------------
     | groups
     | Description: One game's areas, largest first, each with the track its
     |     scene tag says it should have.
     |
     |     The id is the area's lowest object number. It is derived rather than
     |     stored because nothing may depend on it surviving: re-tag a room and
     |     areas legitimately merge or split, and an id that outlived that
     |     would name an area that no longer exists. What IS stored is the
     |     per-room verdict, which survives any regrouping -- so an area is a
     |     way of setting many rooms at once, never a thing with its own record.
     | Author: suinevere
     | Dependencies: collections, pres_store, zexits
     | Globals: N/A
     | Params: stem -- the story stem; pool -- an already-read catalogue
     | Returns: a list of dicts: id, label, scene, rooms, track
     ----------------------*/"""
    p = pool or store.pool()
    raw = zexits.story(stem)
    inv = store.rooms(stem)
    titles = {r["obj"]: r["title"] for r in inv}
    tags = store.scenes(stem)

    adj = zexits.neighbours(zexits.graph(raw)) if raw else {}
    rooms = {r["obj"] for r in inv}
    scene = {n: store.scene_of(n, titles.get(n, ""), tags)[0] for n in rooms}

    comps = _absorb(_components(rooms, adj, lambda n: scene.get(n)), adj)

    out = []
    for g in comps:
        names = collections.Counter(scene[n] for n in g if scene.get(n))
        s = names.most_common(1)[0][0] if names else None
        d = p["scene_defaults"].get(s, {}) if s else {}
        track = d.get("track", 0)
        out.append({
            "id": g[0],
            "label": _label(g, titles),
            "scene": s,
            "rooms": g,
            "track": track if track in store.NEUTRAL_POOL else 0,
        })
    out.sort(key=lambda a: (-len(a["rooms"]), a["id"]))
    return out
