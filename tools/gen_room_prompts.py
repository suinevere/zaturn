#!/usr/bin/env python3
"""/*----------------------
 | gen_room_prompts.py
 | Description: GENERATES a prompt for every room of every game that is not
 |     Zork I -- one picture per room, no two rooms sharing one.
 |
 |     What makes a per-room picture worth having is that it is drawn from the
 |     room's OWN words rather than from its scene tag: "SW End of Repository",
 |     with its pit of snakes and its wicker cages and its vast mirror, is not
 |     a generic cave and should not get a generic cave. 59% of the rooms carry
 |     prose and all of them carry a title, so a room with no description still
 |     gets its own name and its scene rather than the scene alone.
 |
 |     Three things are stripped out of that prose before it becomes a prompt.
 |     Quoted text, because a sign that reads TREASURE VAULT asks the model for
 |     letters and letters are the one thing a room background must not have --
 |     the game draws its own text over it. Second-person framing, because "you
 |     are standing" asks for a person and a room background is a place, not a
 |     scene. And everything past the first couple of sentences, because a Z
 |     machine description ends in exits and takeable objects, which are the
 |     parts a picture cannot honour and the model will try to.
 |
 |     The reference is picked per AREA rather than per room, deliberately.
 |     Every room now has its own picture, so the thing that keeps a place
 |     feeling like one place is no longer a shared picture but a shared
 |     palette: the rooms of one area are graded against one frame and come
 |     back in one colour, while the next area of the same kind is graded
 |     against another.
 | Author: suinevere
 | Dependencies: json, pathlib, re, sys, zlib, game_genre, image_looks,
 |     pres_store, room_art_style, room_groups
 | Globals: ROOT, ROOMS, OUT, MAX_PROSE, TAIL
 ----------------------*/"""
import json
import pathlib
import re
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import game_genre as genre_vocab
import image_looks
import pres_store as store
import room_art_style
import room_groups

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROOMS = ROOT / "tools" / "assets" / "rooms"
OUT = ROOT / "tools" / "assets" / "art" / "room_prompts.json"
MAX_PROSE = 240
TAIL = "empty, abandoned, uninhabited, photographic, deep shadow"
"""ROOT / ROOMS / OUT / MAX_PROSE / TAIL

Description: Where the room text comes from, where the sheet goes, how much of
    a room's prose survives into its prompt, and what is appended to every one
    of them. What a plate is aimed at is not here: room_art_style owns
    TARGET_MEAN, because a copy of it in a sheet is a copy of an answer, and
    the copies went stale the moment the target moved.
Author: suinevere
"""

SECOND_PERSON = re.compile(
    r"\b(you are|you're|you have|you can see|you see|you|your|yourself)\b",
    re.I)
QUOTED = re.compile(r"[\"“”'‘’]([^\"“”'‘’]{2,})"
                    r"[\"“”'‘’]")
EXITS = re.compile(
    r"\b(north|south|east|west|northeast|northwest|southeast|southwest|"
    r"up|down|upward|downward|leads?|exits?|passages? (?:leads?|runs?)|"
    r"you can go|to the (?:left|right))\b", re.I)
LETTERING = re.compile(
    r"\b(messages?|signs?|writing|written|inscription|inscribed|scrawled|"
    r"reads?|lettering|letters|words?|label(?:led)?|engraved|graffiti|"
    r"notices?|plaques?|posters?|books?|newspapers?|scroll|scribbled)\b", re.I)
PEOPLE = re.compile(
    r"\b(people|persons?|man|men|woman|women|child|children|boy|girl|"
    r"human|humans|figures?|silhouettes?|crowd|guard|soldier|sailor|"
    r"statues?|effigy|effigies|bust|mannequin|dummy|"
    r"corpse|body|bodies|skeletons?|remains|"
    r"bones?|skulls?|teeth|tooth|hairs?|tongues?|eyes?|hands?|fingers?|"
    r"shoulders?|beards?|flesh)\b", re.I)
"""SECOND_PERSON / QUOTED / EXITS / LETTERING / PEOPLE

Description: What is taken out of a room's prose before it becomes a prompt.

    Two of these are not obvious. LETTERING: a sign whose words have been
    stripped is still a sign, and a model asked for a sign draws letters on it.
    PEOPLE: a room background is a PLACE, and the moment a person is in it the
    picture is a scene instead -- and it is a scene that will be showing for
    every turn the player spends in that room, including the ones after they
    have dealt with whoever it was. The prose names one in twenty-three of the
    rooms, because a Z-machine description happily says there is a man here.
Author: suinevere
"""


ANATOMY = [
    (re.compile(r"\bmouths\b", re.I), "openings"),
    (re.compile(r"\bmouth\b", re.I), "opening"),
    (re.compile(r"\bheads?\s+of\b", re.I), "top of"),
    (re.compile(r"\bfaces?\b", re.I), "wall"),
    (re.compile(r"\bribs?\b", re.I), "frames"),
    (re.compile(r"\bbacks?\s+of\b", re.I), "rear of"),
    (re.compile(r"\bat\s+(?:the\s+)?feet\b", re.I), "on the floor"),
    (re.compile(r"\b(?:the\s+)?(?:foot|feet)\s+of\b", re.I), "the base of"),
    (re.compile(r"\bnecks?\b", re.I), "narrows"),
    (re.compile(r"\bthroats?\b", re.I), "shaft"),
    (re.compile(r"\bspines?\b", re.I), "ridge"),
    (re.compile(r"\barms?\s+of\b", re.I), "branch of"),
]
"""ANATOMY

Description: Body words that interactive fiction uses for architecture, and
    what to say instead. A cave has a mouth, a stair has a head, a hill has a
    foot and a hull has ribs -- and a diffusion model asked for three tunnel
    MOUTHS in a rock wall draws three grinning faces, which is exactly what it
    did. Substituted rather than dropped: "rock face" is a real feature of a
    real room and the sentence describing it is worth keeping, it just has to
    be called a wall.
Author: suinevere
"""


def clean(prose):
    """/*----------------------
     | clean
     | Description: One room's description reduced to the part a picture can
     |     honour: no quoted signage, nothing that mentions writing at all, no
     |     second person, no exit directions, and at most a couple of sentences.
     |
     |     Lettering is dropped rather than merely un-quoted. A sign whose words
     |     have been stripped is still "a sign", and a model asked for a sign
     |     draws letters on it -- which is the one thing a room background must
     |     not have, because the game draws its own text over the picture.
     | Author: suinevere
     | Dependencies: re
     | Globals: QUOTED, SECOND_PERSON, EXITS, LETTERING, MAX_PROSE
     | Params: prose -- the room description, possibly empty
     | Returns: a prompt fragment, possibly empty
     ----------------------*/"""
    if not prose:
        return ""
    text = QUOTED.sub("", prose.replace("\n", " "))
    keep = []
    for sentence in re.split(r"(?<=[.!?])\s+", text):
        s = sentence.strip()
        if not s or EXITS.search(s) or LETTERING.search(s) or PEOPLE.search(s):
            continue
        s = SECOND_PERSON.sub("", s)
        s = re.sub(r"\b(is|are|stands?|lies?|sits?)\s+here\b", "", s, flags=re.I)
        s = re.sub(r"\s{2,}", " ", s).strip(" .,;")
        # A sentence that opened "You have reached..." is a bare verb once the
        # second person is gone, and a bare verb is an instruction, not a place.
        s = re.sub(r"^(?:have\s+|had\s+|can\s+|are\s+|is\s+)?"
                   r"(?:reached|entered|arrived|come|found|noticed|seen)\s*",
                   "", s, flags=re.I).strip(" .,;")
        for pat, word in ANATOMY:
            s = pat.sub(word, s)
        if len(s) > 3:
            keep.append(s)
        if sum(len(k) for k in keep) >= MAX_PROSE:
            break
    out = ", ".join(keep)[:MAX_PROSE]
    return out.strip(" .,;")


def lift_for(index):
    """Where the plate is aimed, which room_art_style owns: a stored copy
    of that answer is what left every already-drawn plate at the old
    brightness when the target moved."""
    return room_art_style.lift_for(index)


def area_reference(stem, pool):
    """/*----------------------
     | area_reference
     | Description: One reference frame per area, so the rooms of a place come
     |     back in one colour while every one of them is a different picture.
     |     The area's own scene names the candidates and its rank among that
     |     game's areas of the same scene picks between them, which is the same
     |     rotation the pictures themselves used to get.
     | Author: suinevere
     | Dependencies: image_looks, room_groups
     | Globals: N/A
     | Params: stem -- the story stem; pool -- the catalogue
     | Returns: {object number: reference index}
     ----------------------*/"""
    areas = room_groups.groups(stem, pool)
    tags = store.scenes(stem)
    titles = {r["obj"]: r["title"] for r in store.rooms(stem)}
    seen, out = {}, {}
    for a in sorted(areas, key=lambda a: a["id"]):
        scene = None
        for o in a["rooms"]:
            scene, _origin = store.scene_of(o, titles.get(o, ""), tags)
            if scene:
                break
        cands = [i for i in image_looks.images_for(scene or "") if i <= 74]
        if not cands:
            cands = [37]
        rank = seen.get(scene, 0)
        seen[scene] = rank + 1
        ref = cands[rank % len(cands)]
        for o in a["rooms"]:
            out[o] = ref
    return out


def main():
    """/*----------------------
     | main
     | Description: Writes one entry per room of every game but Zork I.
     | Author: suinevere
     | Dependencies: json, zlib, game_genre, pres_store
     | Globals: OUT, ROOT, ROOMS, TAIL
     | Params: N/A
     | Returns: 0
     ----------------------*/"""
    pool = store.pool()
    batch = []
    for stem in store.games():
        data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
        refs = area_reference(stem, pool)
        tags = store.scenes(stem)
        genre = genre_vocab.GAME_GENRE.get(stem, "")
        for r in data["rooms"]:
            obj = int(r["obj"])
            title = (r.get("title") or "").strip()
            scene, _origin = store.scene_of(obj, title, tags)
            ref = refs.get(obj, 37)
            body = clean(r.get("description"))
            words = [title.lower()] if title else []
            if body:
                words.append(body.lower())
            if scene:
                words.append(scene.replace("_", " ").lower())
            if genre:
                words.append(genre.lower())
            batch.append({
                "name": f"{stem.lower()}_{obj}",
                "game": stem,
                "obj": obj,
                "scenes": [scene] if scene else [],
                "reference": ref,
                "seed": zlib.crc32(f"{stem}:{obj}".encode("utf-8")) % 2**31,
                "lift": lift_for(ref),
                "shows": title or (scene or "a room"),
                "prompt": ", ".join(words) + ", " + TAIL,
            })
    OUT.write_text(json.dumps({
        "_comment": "GENERATED by tools/gen_room_prompts.py -- one picture per "
                    "room, drawn from that room's own title and prose rather "
                    "than from its scene tag, so no two rooms share a picture. "
                    "reference is per AREA, not per room: with every room "
                    "carrying its own picture the thing that makes a place feel "
                    "like one place is a shared palette, not a shared picture.",
        "batch": batch,
    }, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    withprose = sum(1 for e in batch if len(e["prompt"]) > 80)
    print(f"Wrote {OUT.relative_to(ROOT)}: {len(batch)} rooms across "
          f"{len(store.games())} games, {withprose} with prose of their own")
    return 0


if __name__ == "__main__":
    sys.exit(main())
