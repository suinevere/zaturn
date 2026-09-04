#!/usr/bin/env python3
"""/*----------------------
 | gen_room_prompts.py
 | Description: GENERATES a prompt for every room of every game that is not
 |     Zork I -- one picture per room, no two rooms sharing one -- except the
 |     rooms of the four Zork I derivatives that ARE Zork I rooms, which take
 |     the disc's own measured picture instead. See zork1_reuse.
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
 |     pres_store, room_art_style, room_groups, zork1_reuse
 | Globals: ROOT, ROOMS, OUT, OVERRIDES, REFS, MAX_PROSE, PALETTE,
 |     FIRST_WEIGHT, STYLE,
 |     TAIL,
 |     SCENE_WORDS,
 |     GENRE_SENSE, GENRE_SENSE_ANY, CONFINED, POV, POV_CONFINED,
 |     CONFINED_REFUSE
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
import zork1_reuse

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROOMS = ROOT / "tools" / "assets" / "rooms"
OUT = ROOT / "tools" / "assets" / "art" / "room_prompts.json"
OVERRIDES = ROOT / "tools" / "assets" / "art" / "room_prompt_overrides.json"
REFS = ROOT / "tools" / "assets" / "art" / "refs"
MAX_PROSE = 240
PALETTE = 4
FIRST_WEIGHT = 1.4
STYLE = "painted illustration"
TAIL = f"empty, {STYLE}, deep shadow"
"""ROOT / ROOMS / OUT / OVERRIDES / MAX_PROSE / STYLE / TAIL

Description: Where the room text comes from, where the sheet goes, how much of
    a room's prose survives into its prompt, and what is appended to every one
    of them. What a plate is aimed at is not here: room_art_style owns
    TARGET_MEAN, because a copy of it in a sheet is a copy of an answer, and
    the copies went stale the moment the target moved.

    STYLE is the one place the whole disc's look is decided, and it is a
    painting rather than a photograph. The disc's own frames ARE photographs --
    BHUS_00 is a photograph of a clapboard house -- but they are posterised to
    a duotone at 320x240, which is most of the way to a painting already, and
    a photograph is the format that fails worst when the model gets a detail
    wrong: a competently photographed beige 1990s desktop in a Miniaturization
    Booth reads as a mistake in a way a painted console does not. Change this
    string and every prompt changes, which is the point of it being a string --
    and tools/probe_prompts.py --style will try one without changing it.

    It is two words, and the whole tail is five, because the measurement that
    settled it was 24 words of boilerplate against 6 words of room in the
    median prompt -- and 190 prompts ran past CLIP's 77-token window, so their
    ends were not being read at all. At CFG 2.5 none of that mattered because
    nothing was being read; at CFG 7 it mattered enormously, because the model
    faithfully drew the style essay and glanced at the room. Every word here is
    a word the room does not get. Two is what a look costs.

    OVERRIDES is the escape hatch for the rooms no rule will ever get right. A
    title that is a proper noun has nothing in it to reason from -- Seastalker's
    SCIMITAR is a submarine, and to a model it is a sword -- and writing a rule
    for one room is how a rule collection stops being readable. An override
    replaces the room's own words and nothing else: the scene, the era, the
    genre and the tail are still appended, so an override cannot quietly opt a
    room out of the things every plate is held to.
Author: suinevere
"""

SECOND_PERSON = re.compile(
    r"\b(you are|you're|you have|you can see|you see|you|your|yourself)\b",
    re.I)
QUOTED = re.compile(r"[\"“”]([^\"“”]{2,}?)[\"“”]"
                    r"|(?<!\w)['‘]([^'‘’]{2,}?)['’](?!\w)")
EXITS = re.compile(
    r"\b(leads?|exits?|passages? (?:leads?|runs?)|"
    r"way (?:out|in|back)|you can go|to the (?:left|right))\b", re.I)
DIRECTION = re.compile(
    r"\b(north|south|east|west|northeast|northwest|southeast|southwest)"
    r"(?:ern|erly|ward|wards)?\b", re.I)
INCIDENTAL_EXIT = re.compile(
    r"\b(?:near|by|at|beside|through|with|past)\s+(?:the|an|a)\s+exits?\b",
    re.I)
LETTERING = re.compile(
    r"\b(messages?|signs?|writing|written|inscription|inscribed|scrawled|"
    r"reads?|lettering|letters|words?|label(?:led)?|engraved|graffiti|"
    r"notices?|plaques?|posters?|books?|newspapers?|scroll|scribbled)\b", re.I)
PEOPLE = re.compile(
    r"\b(people|persons?|man|men|woman|women|child|children|boy|girl|"
    r"human|humans|figures?|silhouettes?|crowd|guard|soldier|sailor|"
    r"she|he|her|hers|his|him|they|them|their|theirs|"
    r"clown|cousins?|aunts?|uncles?|mothers?|fathers?|brothers?|sisters?|"
    r"wife|husband|nephews?|nieces?|kings?|queens?|princes?|princess|"
    r"priests?|monks?|nurses?|doctors?|captains?|pirates?|wizards?|witch|"
    r"witches|thie(?:f|ves)|gnomes?|robots?|androids?|aliens?|vogons?|"
    r"passengers?|customers?|workers?|tourists?|dancers?|singers?|actors?|"
    r"actress|waiters?|clerks?|drivers?|pilots?|acrobats?|jugglers?|"
    r"magicians?|barkers?|performers?|audience|butlers?|maids?|servants?|"
    r"attendants?|comrades?|mister|mrs|madam|professors?|sergeants?|"
    r"inspectors?|detectives?|"
    r"statues?|effigy|effigies|bust|mannequin|dummy|"
    r"sculptures?|figurines?|idols?|totems?|gargoyles?|portraits?|"
    r"corpse|body|bodies|skeletons?|remains|"
    r"bones?|skulls?|teeth|tooth|hairs?|tongues?|eyes?|hands?|fingers?|"
    r"shoulders?|beards?|flesh)\b", re.I)
"""SECOND_PERSON / QUOTED / EXITS / DIRECTION / LETTERING / PEOPLE

Description: What is taken out of a room's prose before it becomes a prompt.

    EXITS drops the sentence; DIRECTION only takes the word out of it. They
    were one pattern and it cost 1,050 sentences: "To the west stands a droopy
    tent" is a tent, "a gently sloping north/south passage lined with oddly
    shaped limestone formations" is limestone, and "Far below you is an active
    volcano" is a volcano, and all three were thrown away for naming a compass
    point. Only a sentence whose VERB is an exit -- leads, runs, you can go --
    is about the way out rather than about the room, and there are 390 of
    those. Splitting them gave 507 rooms their scenery back.

    Two of the rest are not obvious. LETTERING: a sign whose words have been
    stripped is still a sign, and a model asked for a sign draws letters on it.

    QUOTED will not accept an apostrophe that follows a letter as an opening
    quote, because a possessive is not a quotation. It used to, and it paired
    possessives with each other and deleted everything in between: "Mariners'
    Trust, the Island's bank" became "Marinerss bank" and "You're standing in
    Aunt Hildegarde's much envied garden" became "Yous much envied garden".
    41 of the 1,235 descriptions were being fed to the model with a hole in
    them and an invented word across the seam.
    PEOPLE: a room background is a PLACE, and the moment a person is in it the
    picture is a scene instead -- and it is a scene that will be showing for
    every turn the player spends in that room, including the ones after they
    have dealt with whoever it was. The prose names one in twenty-three of the
    rooms, because a Z-machine description happily says there is a man here.

    The pronouns and the roles were added after a clown and a girl were drawn
    from prose the first version passed: it named neither a man nor a woman, it
    named "a tall and lanky clown" and "she would spend hours tending to her
    flowers". Dropping a whole sentence on a pronoun costs 19 of the 876 rooms
    with prose their entire description, and every one of those still has its
    title, which is what a room with no prose has always been drawn from.
Author: suinevere
"""


TITLE_SENSE = [
    (re.compile(r"^dead[- ]end\b", re.I), "narrow passage ending at a blank wall"),
    (re.compile(r"^cul[- ]de[- ]sac\b", re.I),
     "narrow passage ending at a blank wall"),
    (re.compile(r"^forks?\b", re.I), "place where a passage divides in two"),
    (re.compile(r"^arcades?\b", re.I),
     "carnival midway of booths and sideshow stalls, strings of bulbs"),
    (re.compile(r"\bcrossover\b", re.I), "place where two passages cross"),
    (re.compile(r"^limbo\b", re.I), "featureless dark void"),
    (re.compile(r"^in pipe\b", re.I), "inside a large metal pipe"),
    (re.compile(r"^underwater\b", re.I), "underwater view"),
    (re.compile(r"^wading\b", re.I), "shallow water"),
    (re.compile(r"^underground\b", re.I), "underground space"),
    (re.compile(r"^crawl ?space\b", re.I),
     "underside of floorboards and joists filling the top of the frame, "
     "ducts and pipes and hanging wires, dust and cobwebs, bare dirt below"),
]
"""TITLE_SENSE

Description: Map jargon that names a place but reads as something else, and
    what to say instead. A third of the rooms carry no prose at all, so for
    them the title IS the prompt and there is nothing to correct a title the
    model misreads: "Dead End" is a road sign to a diffusion model and it drew
    one, letters and all, in sixteen rooms. Anchored at the front of the title
    because "Fork, Of Sorts" is a junction while a fork in a drawer is a fork.
Author: suinevere
"""

SCENE_WORDS = {
    "HOUSE_EXT": "outside a house",
    "SHIP_EXT": "outside a ship",
    "SHIP_INT": "inside a ship",
    "DARKROOM": "unlit room",
}
"""SCENE_WORDS

Description: The scene tags that cannot be said to a model as they are spelled.
    Three are abbreviations -- "ship ext" is not English and the model reads the
    half it recognises. The fourth is worse than that: DARKROOM means a room
    with no light in it, and a darkroom is a place where film is developed under
    a red safelight, which is what 29 rooms were being asked for. Every other
    tag is one ordinary word and is used as it stands.
Author: suinevere
"""

GENRE_SENSE = {
    "SCIFI": [
        (re.compile(r"\bships?\b", re.I), "spacecraft hull"),
        (re.compile(r"\bhold\b", re.I), "cargo bay"),
        (re.compile(r"\bbridge\b", re.I), "command bridge"),
        (re.compile(r"\bdecks?\b", re.I), "spacecraft deck"),
    ],
}
"""GENRE_SENSE

Description: Words whose meaning is decided by the kind of game they are in,
    and what they mean in that one. Starcross is set on an alien vessel and its
    "Outside Ship" came back as a beached ocean liner; the same word in
    Cutthroats is a wreck on the sea floor and in Plundered Hearts is a
    brigantine, and both of those are right. Only SCIFI has an entry, because
    only SCIFI disagrees with the ordinary reading -- FANTASY's bridges are
    bridges over water and must not become command bridges.

    Applied to the title and to the scene phrase and never to prose, because in
    prose these are not all nouns: two Planetfall rooms say that planters hold
    dead plants and that a room cannot hold a few days' worth, and a cargo bay
    of dry dead plants is a worse sentence than the one it replaced.

    Every replacement names a PLACE and not an object, which is the rule the
    first version of this broke. "Outside Ship" became "outside spacecraft" and
    came back as a giant satellite hanging in the frame, which is the cupcake
    and the fork again wearing different clothes: name a thing and the model
    draws a picture OF the thing, however the camera was described. A hull is
    somewhere you can stand next to; a spacecraft is something you photograph.
Author: suinevere
"""

GENRE_SENSE_ANY = {
    "SCIFI": [
        (re.compile(r"\bkeyboards?\b", re.I), "panel of numbered buttons"),
        (re.compile(r"\bcomputers?\b", re.I), "mainframe cabinet"),
        (re.compile(r"\b(?:screens?|monitors?)\b", re.I), "small round crt screen"),
    ],
}
"""GENRE_SENSE_ANY

Description: The same idea as GENRE_SENSE for words that are safe to rewrite in
    prose as well, because they are never verbs. Planetfall's Miniaturization
    Booth says a keyboard with numeric keys is next to the slot and got a beige
    1990s desktop for it twice; its main Computer Room got a modern office with
    double glazing. The era phrase alone did not beat a noun that names a
    specific object, so the noun has to name a different one.
Author: suinevere
"""

CONFINED = re.compile(
    r"\b(crawl ?space|closet|cupboard|wardrobe|niche|alcove|nook|cubby|"
    r"hatch|duct|vent|chimney|flue|shaft|cell|booth|cage|coffin|burrow|"
    r"tunnel|crevice|chute|pipe)\b", re.I)
POV = "first person view"
POV_CONFINED = "first person view, floor level, ceiling fills upper half"
CONFINED_REFUSE = ("tall room, high ceiling, spacious, headroom, standing, "
                   "door, doorway, pillar, column, post, arch, hall, "
                   "corridor, room interior, architecture")
"""CONFINED / POV / POV_CONFINED

Description: Where the camera is, which is the first thing every prompt says.

    A room background in a parser game is what the player is looking at while
    they read "You are standing in an open field". It is not a picture OF the
    room, taken from outside it or above it, and the disc's own frames are not
    either: BHUS_00 is the white house seen from where the player stands in
    front of it. Every prompt leads with this, prose or no prose, override or
    not, because a shot from anywhere else is a picture of somewhere the player
    is not.

    It replaced a wide establishing shot, which was doing a second job as well
    -- a bare noun phrase with no framing in front of it is read as the SUBJECT
    of a picture rather than as a place, which is how "fork, of sorts" became a
    studio-lit fork on a road. A point of view does that job better, since
    there is no way to be standing inside a fork.

    Two of them because one was a contradiction. You cannot stand up in a crawl
    space, a cell, a wet tunnel or the inside of a cage, and 53 rooms name one
    of those, so the camera goes to the floor and the ceiling comes down to it.

    The confined one names where the ceiling sits in the FRAME and not just how
    low it is, which is three words longer than the other and earns them. Said
    only as a low ceiling, the model drew a tall room seen from the floor: a
    correct description of a wrong picture. What a crawl space looks like is
    boards across the top half of the page and a hand's width of dirt under
    them, and that is a fact about the composition, not about the room.

    Both are as short as they can be said, twice cut. The first ran eighteen
    words at the front of all 1,759 prompts; a room whose whole description is
    six words cannot outvote that at any guidance scale. Boilerplate every
    prompt shares distinguishes no plate from any other, and at CFG 7 it does
    worse than nothing: it is drawn faithfully, in place of the room.
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


def clean_parts(prose, genre=""):
    """/*----------------------
     | clean_parts
     | Description: The same as clean, as a list of sentences rather than one
     |     string, so the caller can tell the first one from the rest.
     |
     |     Which matters because the first sentence of a Z-machine description
     |     is what the room IS and everything after it is detail. Stationfall's
     |     Main Street opens "This large spacetube is the main thoroughfare of
     |     a space village" and the picture came back a street of brick shops,
     |     because the title said Main Street and the title led the prompt.
     | Author: suinevere
     | Dependencies: re
     | Globals: MAX_PROSE
     | Params: prose -- the room description; genre -- the game's genre
     | Returns: a list of sentence fragments, possibly empty
     ----------------------*/"""
    joined = _clean_sentences(prose, genre)
    out, room = [], MAX_PROSE
    for part in joined:
        if len(part) > room:
            # On a word, not mid-syllable: the old cut left "the narro".
            cut = part[:room].rsplit(" ", 1)[0].strip(" .,;")
            if len(cut) > 12:
                out.append(cut)
            break
        out.append(part)
        room -= len(part) + 2
    return out


def clean(prose, genre=""):
    """/*----------------------
     | clean
     | Description: clean_parts as one string, which is what a prompt wants
     |     when nothing needs to tell the first sentence from the rest.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: N/A
     | Params: prose -- the room description; genre -- the game's genre
     | Returns: a prompt fragment, possibly empty
     ----------------------*/"""
    return ", ".join(clean_parts(prose, genre))


def _clean_sentences(prose, genre=""):
    """/*----------------------
     | _clean_sentences
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
     | Params: prose -- the room description, possibly empty;
     |     genre -- the game's genre, for the words it decides the sense of
     | Returns: the surviving sentences, in order, possibly none
     ----------------------*/"""
    if not prose:
        return ""
    text = QUOTED.sub("", prose.replace("\n", " "))
    keep = []
    for sentence in re.split(r"(?<=[.!?])\s+", text):
        s = sentence.strip()
        # "Near the exit is a game booth lined with prizes" is a game booth.
        # The word only means the way out when the sentence is about it.
        s = INCIDENTAL_EXIT.sub("", s)
        if not s or EXITS.search(s) or LETTERING.search(s) or PEOPLE.search(s):
            continue
        s = SECOND_PERSON.sub("", s)
        # The direction is taken out of the sentence rather than the sentence
        # out of the room. "To the west stands a droopy tent" is a tent.
        s = DIRECTION.sub("", s)
        # Tidied off the hole it leaves, which is findable while the double
        # space is still there: "To the  stands" and "continues  and".
        s = re.sub(r"\b(?:at|to|from|on|in)\s+the\s{2,}", "", s, flags=re.I)
        s = re.sub(r"\s{2,}(?:and|or)\b", "", s, flags=re.I)
        s = re.sub(r"\s*/\s*", " ", s)
        s = re.sub(r"\s+(?:and|or)\s*(?=[,.;]|$)", "", s, flags=re.I)
        s = re.sub(r"\b(?:to|at|from|on|in)\s+(?=is\b|are\b)", "", s, flags=re.I)
        s = re.sub(r",?\s*\bwhich (?:is|are)\s*(?=[,.;]|$)", "", s, flags=re.I)
        # "There are doorways to the north" leaves "doorways to the", and a
        # clause that is only a preposition is debris rather than scenery --
        # at the end of the sentence or between two commas in the middle of it.
        # The full stop is still attached here -- it is stripped further down --
        # so the lookahead has to allow it, or "is to the ." never matches and
        # "The way out is to the east" keeps a dangling "to the".
        s = re.sub(r"[,;]?\s*\b(?:to|at|from|on|in|of|toward|towards)\s+the\s*"
                   r"(?=[,.;]|$)", "", s, flags=re.I)
        # "a north-south corridor" leaves the hyphen behind on its own.
        s = re.sub(r"\s+-\s+", " ", s)
        s = re.sub(r"\b(is|are|stands?|lies?|sits?)\s+here\b", "", s, flags=re.I)
        s = re.sub(r"\s{2,}", " ", s).strip(" .,;")
        # A sentence that opened "You have reached..." is a bare verb once the
        # second person is gone, and a bare verb is an instruction, not a place.
        # The word boundary here was a literal backspace until 2026-09-03, put
        # there by a heredoc that ate the backslash, so this had never once
        # fired and 47 rooms were drawn from "reached a dead end".
        s = re.sub(r"^(?:have\s+|had\s+|can\s+|are\s+|is\s+)?"
                   r"(?:reached|entered|arrived|come|found|noticed|seen)\b\s*",
                   "", s, flags=re.I).strip(" .,;")
        for pat, word in ANATOMY:
            s = pat.sub(word, s)
        if len(s) > 3:
            # Per sentence rather than over the joined string, which is where
            # this used to happen: splitting clean() in two moved the join out
            # from under it and the keyboards went back to being keyboards.
            keep.append(in_sense_any(s, genre))
        if sum(len(k) for k in keep) >= MAX_PROSE:
            break
    return keep


def overrides():
    """/*----------------------
     | overrides
     | Description: Hand-written replacements for the words a room is drawn
     |     from, by plate name. Absent file means none.
     | Author: suinevere
     | Dependencies: json
     | Globals: OVERRIDES
     | Params: N/A
     | Returns: {plate name: {"text": ..., "shows": ...}}
     ----------------------*/"""
    if not OVERRIDES.is_file():
        return {}
    data = json.loads(OVERRIDES.read_text(encoding="utf-8"))
    out = {k: v for k, v in data.get("rooms", {}).items()}
    for name, rec in out.items():
        if not rec.get("text"):
            raise SystemExit(f"gen_room_prompts: override {name} has no text")
        for phrase in ("no people", "without people", "no person"):
            if phrase in rec["text"].lower():
                raise SystemExit(
                    f"gen_room_prompts: override {name} says {phrase!r}. "
                    "Diffusion has no negation and that puts the word straight "
                    "into what is being sampled towards -- say what IS there.")
        ref = rec.get("compose_from")
        if ref and not (REFS / ref).is_file():
            raise SystemExit(
                f"gen_room_prompts: override {name} composes from {ref!r}, "
                f"which is not in {REFS.relative_to(ROOT)}. A missing "
                "reference would quietly fall back to drawing from the words "
                "alone, which is the thing it was written to stop doing.")
    return out


def in_sense(text, genre):
    """/*----------------------
     | in_sense
     | Description: One phrase read in the sense its genre gives it.
     | Author: suinevere
     | Dependencies: re
     | Globals: GENRE_SENSE
     | Params: text -- a title or scene phrase; genre -- the game's genre
     | Returns: the phrase, possibly rewritten
     ----------------------*/"""
    for pat, word in GENRE_SENSE.get(genre or "", ()):
        text = pat.sub(word, text)
    return in_sense_any(text, genre)


def in_sense_any(text, genre):
    """/*----------------------
     | in_sense_any
     | Description: The part of a genre's sense that is safe anywhere, prose
     |     included, because none of its words is ever a verb.
     | Author: suinevere
     | Dependencies: re
     | Globals: GENRE_SENSE_ANY
     | Params: text -- any prompt fragment; genre -- the game's genre
     | Returns: the fragment, possibly rewritten
     ----------------------*/"""
    for pat, word in GENRE_SENSE_ANY.get(genre or "", ()):
        text = pat.sub(word, text)
    return text


def scene_words(scene, genre=""):
    """/*----------------------
     | scene_words
     | Description: A scene tag as a phrase a model can read, in the sense its
     |     genre gives it.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: SCENE_WORDS
     | Params: scene -- the tag, possibly empty; genre -- the game's genre
     | Returns: the phrase, possibly empty
     ----------------------*/"""
    if not scene:
        return ""
    said = SCENE_WORDS.get(scene, scene.replace("_", " ").lower())
    return in_sense(said, genre)


def retitled(title):
    """/*----------------------
     | retitled
     | Description: Whether TITLE_SENSE has an opinion about this title, which
     |     is also whether the title is better evidence than the room's scene
     |     tag. TITLE_SENSE only fires on titles whose meaning has been decided
     |     by hand; a tag is a classifier's guess. Five rooms have both, and in
     |     all five the tag is CAVE while the title says crawl space or
     |     underwater passage -- one of those is under a Hollywood mansion.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: TITLE_SENSE
     | Params: title -- the room title, possibly empty
     | Returns: True when a substitution applies
     ----------------------*/"""
    t = (title or "").strip().lower()
    return any(pat.search(t) for pat, _ in TITLE_SENSE)


def title_words(title, genre=""):
    """/*----------------------
     | title_words
     | Description: A room title as a picture can use it: map jargon replaced by
     |     what it means, then read in the sense its genre gives it, and nothing
     |     else touched.
     | Author: suinevere
     | Dependencies: re
     | Globals: TITLE_SENSE
     | Params: title -- the room title, possibly empty; genre -- the game's genre
     | Returns: the title, lowercased, possibly empty
     ----------------------*/"""
    t = (title or "").strip().lower()
    for pat, word in TITLE_SENSE:
        if pat.search(t):
            t = pat.sub(word, t, count=1)
            break
    return in_sense(t, genre)


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
     |
     |     A game gets PALETTE references and no more. It used to get one per
     |     area, which meant Hollywood Hijinx was graded against 23 different
     |     frames and the median game against 12 -- twelve colour casts inside
     |     one story, which is the opposite of a game looking like one place.
     |     The disc itself varies by area, so the answer is not one reference
     |     per game either: Zork I's own frames run eleven area prefixes, the
     |     house teal and the cellar red. Four is near that and far from
     |     twenty-three, and it goes to the four kinds of place the game is
     |     mostly made of, by room count.
     |
     |     The area's scene is deliberately NOT lent to the rooms of the area
     |     that have none of their own. It is the scene of the first tagged room
     |     in a connectivity group, which is a guess that called a Martian
     |     desert a castle, and lending it rewrote two hundred prompts that were
     |     working in order to improve a handful.
     | Author: suinevere
     | Dependencies: image_looks, room_groups
     | Globals: N/A
     | Params: stem -- the story stem; pool -- the catalogue
     | Returns: {object number: reference index}
     ----------------------*/"""
    areas = room_groups.groups(stem, pool)
    tags = store.scenes(stem)
    titles = {r["obj"]: r["title"] for r in store.rooms(stem)}

    # What each area is, and how much of the game is that.
    scene_of, weight = {}, {}
    for a in sorted(areas, key=lambda a: a["id"]):
        scene = None
        for o in a["rooms"]:
            scene, _origin = store.scene_of(o, titles.get(o, ""), tags)
            if scene:
                break
        scene_of[a["id"]] = scene
        weight[scene] = weight.get(scene, 0) + len(a["rooms"])

    # The game's palette: one reference for each of the few kinds of place it
    # is mostly made of, biggest first, and nothing beyond PALETTE.
    ranked = sorted(weight, key=lambda sc: (-weight[sc], str(sc)))
    palette, seen = {}, {}
    for scene in ranked[:PALETTE]:
        cands = [i for i in image_looks.images_for(scene or "") if i <= 74]
        if not cands:
            cands = [37]
        rank = seen.get(scene, 0)
        seen[scene] = rank + 1
        palette[scene] = cands[rank % len(cands)]
    fallback = palette[ranked[0]] if ranked else 37

    out = {}
    for a in sorted(areas, key=lambda a: a["id"]):
        ref = palette.get(scene_of[a["id"]], fallback)
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
    reuse = zork1_reuse.all_matches()
    hand = overrides()
    batch = []
    for stem in store.games():
        data = json.loads((ROOMS / f"{stem}.json").read_text(encoding="utf-8"))
        refs = area_reference(stem, pool)
        tags = store.scenes(stem)
        genre = genre_vocab.GAME_GENRE.get(stem, "")
        setting = genre_vocab.setting_for(stem)
        look = genre_vocab.look_for(stem)
        genre_style = look.get("style", STYLE)
        for r in data["rooms"]:
            obj = int(r["obj"])
            if (stem, obj) in reuse:
                continue
            title = (r.get("title") or "").strip()
            scene, _origin = store.scene_of(obj, title, tags)
            ref = refs.get(obj, 37)
            parts = clean_parts(r.get("description"), genre)
            said = title_words(title, genre)
            spoken = hand.get(f"{stem.lower()}_{obj}")
            # Matched on the title as written: the substitution may already
            # have replaced the very word that says the place is confined.
            # Where the camera is, then what kind of world it is standing in,
            # then the room. The era used to come last, which put it behind up
            # to 240 characters of prose: Stationfall's rooms are a Rec Shop, a
            # Theatre and a Mayor's Office with Main Street below, and against
            # that much civic vocabulary a trailing "1970s sci-fi" bought a
            # street of houses and a giant stone head. Context belongs in front
            # of the thing it is context for.
            era = setting or genre
            if spoken and "setting" in spoken:
                era = spoken["setting"]
            words = [POV_CONFINED if CONFINED.search(title) else POV]
            if era:
                words.append(era.lower())
            # With prose the description carries the place and the title only
            # names it; with none, the title is all there is.
            if spoken:
                words.append(spoken["text"].lower())
            elif parts:
                # The first sentence of a Z-machine description is what the
                # room IS and the rest is detail, so it is weighted up and it
                # goes in front of the title. Stationfall's Main Street opens
                # "This large spacetube is the main thoroughfare of a space
                # village" and came back a street of brick shops, because the
                # title said Main Street and the title led.
                body = ", ".join(parts).lower()
                words.append(f"({parts[0].lower().replace(':', ' ')}:"
                             f"{FIRST_WEIGHT})")
                words.extend(p.lower() for p in parts[1:])
                # A title the description already says is not said twice.
                if said and said not in body:
                    words.append(said)
            else:
                words.append(said or "an empty room")
            # And the scene only when nothing already said it: a room titled
            # Kitchen and tagged KITCHEN was asking for a kitchen twice.
            if scene and not retitled(title):
                phrase = scene_words(scene, genre)
                if phrase and phrase not in ", ".join(words):
                    words.append(phrase)
            # Saying the ceiling is low in the positive is not enough on its
            # own; the height has to be refused as well, for all 51 of them
            # and not only the two that were looked at.
            refuse = ", ".join(x for x in (
                CONFINED_REFUSE if CONFINED.search(title) else "",
                (spoken or {}).get("negative", "")) if x)
            batch.append({
                "name": f"{stem.lower()}_{obj}",
                "game": stem,
                "obj": obj,
                "scenes": [scene] if scene else [],
                "reference": ref,
                "seed": zlib.crc32(f"{stem}:{obj}".encode("utf-8")) % 2**31,
                "lift": lift_for(ref),
                "shows": (spoken.get("shows") if spoken else None)
                         or title or (scene or "a room"),
                "prompt": ", ".join(words) + ", empty, "
                          + ((spoken or {}).get("style") or genre_style)
                          + ", deep shadow",
                **({"checkpoint": look["checkpoint"]}
                   if look.get("checkpoint") else {}),
                **({"negative": refuse} if refuse else {}),
                **({"compose_from": (spoken or {})["compose_from"],
                    "denoise": float((spoken or {}).get("denoise", 0.6))}
                   if (spoken or {}).get("compose_from") else {}),
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
    withprose = sum(1 for e in batch if not e["prompt"].startswith("wide "))
    print(f"Wrote {OUT.relative_to(ROOT)}: {len(batch)} rooms across "
          f"{len(store.games())} games, {withprose} with prose of their own")
    print(f"{len(reuse)} rooms of the Zork I derivatives are left out -- they "
          "show the disc's own picture rather than one drawn for them")
    return 0


if __name__ == "__main__":
    sys.exit(main())
