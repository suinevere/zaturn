#!/usr/bin/env python3
"""Genre vocabulary and the genre each story on the disc belongs to.

Description: One word per game saying what its world sounds like, so a whole
    genre can be handed a track at once. Most of these thirty-one stories have
    no room-level evidence to draw on -- 905 of their 1,857 rooms carry no
    scene tag and match no title rule -- and for those the honest unit of
    decision is not the room but the game, and often not even the game: six
    mysteries all want the same close, watchful theme, and setting that one
    room at a time is work that does not get finished.

    This is NOT the GAME_CAT_* enum in saturn/src/menu/game_titles.h. That one
    groups the picker's shelves by publisher series -- The Zork Universe, The
    Enchanter Series -- and it puts Starcross next to The Lurking Horror under
    "Sci-Fi & Horror", which is a sensible shelf and a terrible playlist. This
    one asks only what a room in the game should sound like.

    A genre is a setting, never a tone. Hitchhiker's and Leather Goddesses are
    comedies, but the disc holds Zork I's fourteen room themes and nothing
    comic among them, so a COMEDY genre could only ever be a label with no
    music behind it; both are filed by where they happen, which is space.

    MEDIEVAL is offered and unused. Nothing on this disc is castles and
    knights -- Moonmist's castle is a house party in Cornwall in the 1980s --
    but the word is what a reviewer reaches for, and a vocabulary that refuses
    the obvious word teaches them to distrust it.
Author: suinevere
Dependencies: N/A
Globals: GENRES, GAME_GENRE, GENRE_TRACKS, GENRE_IMAGES, GENRE_FALLBACK,
    MAP_FILES, GENRE_MAP
"""

GENRES = (
    ("FANTASY",    "magic, treasure and the Great Underground Empire"),
    ("SCIFI",      "spaceships, stations and other planets"),
    ("MYSTERY",    "a crime, a house full of suspects, and questions to ask"),
    ("HORROR",     "something in the building that should not be"),
    ("UNDERWATER", "below the surface, under pressure"),
    ("PIRATE",     "sail, cutlass and the open Caribbean"),
    ("ANCIENT",    "tombs, ruins and the desert that buried them"),
    ("MODERN",     "the present day, at ordinary human scale"),
    ("MEDIEVAL",   "castles and knights -- offered, but nothing here is one"),
    ("SAMPLER",    "an anthology, several worlds in one story file"),
)
"""GENRES

Description: The vocabulary, as (name, what it means). Ordered for display, not
    indexed by anything -- nothing generates C from this, so the order is free
    to change and a rename only orphans the entries in GAME_GENRE below.
Author: suinevere
"""

GAME_GENRE = {
    "ADVENT":   "FANTASY",     # Colossal Cave: dwarves, magic words, a treasure vault
    "BALLYHOO": "MYSTERY",     # a kidnapping to solve, under a circus tent
    "CUTHROAT": "UNDERWATER",  # Cutthroats: a wreck dive off Hardscrabble Island
    "DEADLINE": "MYSTERY",     # the locked study, twelve hours to charge someone
    "ENCHANTR": "FANTASY",     # spells against Krill
    "HITCHHKR": "SCIFI",       # a Vogon ship, then the Heart of Gold
    "HOLYWOOD": "MYSTERY",     # a dead uncle's mansion and a hidden fortune
    "HYPOCOND": "MODERN",      # a house, a garden, and an imagined illness
    "INFIDEL":  "ANCIENT",     # the buried pyramid and the sand over it
    "INFOSAM5": "SAMPLER",     # Infocom Sampler: Infidel's desert and others
    "INFOSAM7": "SAMPLER",     # Infocom Sampler: Zork I and Trinity's gardens
    "LEATHERG": "SCIFI",       # Phobos, whatever else it is
    "LURKING":  "HORROR",      # the thing under the G.U.E. Tech campus
    "MOONMIST": "MYSTERY",     # a Cornish castle, a ghost, and four solutions
    "MZORKI":   "FANTASY",     # Mini-Zork I
    "MZORKI2":  "FANTASY",     # Mini-Zork I, the later release
    "MZORKII":  "FANTASY",     # Mini-Zork II: the Wizard's demesne
    "PLNDHRTS": "PIRATE",      # Plundered Hearts: the Lafond Deux and the Caribbean
    "PLNTFALL": "SCIFI",       # a derelict planet-sized station
    "SEASTLKR": "UNDERWATER",  # the Scimitar, and the Aquadome under threat
    "SORCERER": "FANTASY",     # the Circle of Enchanters, and Belboz missing
    "SPLBRKR":  "FANTASY",     # magic failing, and the cubes
    "STARCROS": "SCIFI",      # the alien ship at the mass detector
    "STATFALL": "SCIFI",       # the station Planetfall left behind
    "SUSPECT":  "MYSTERY",     # a costume ball and a murder pinned on you
    "SUSPENDD": "SCIFI",       # a cryogenic complex run through six robots
    "WISHBRNG": "FANTASY",     # Festeron turned Witchville, and the stone
    "WITNESS":  "MYSTERY",     # 1938 Los Angeles, and a man shot in front of you
    "ZORK1":    "FANTASY",     # the Great Underground Empire itself
    "ZORK2":    "FANTASY",     # the Wizard of Frobozz
    "ZORK3":    "FANTASY",     # the Dungeon Master
}
"""GAME_GENRE

Description: Every story stem on the disc and the one genre it belongs to,
    including ZORK1 -- which cannot be assigned a track here, since its rooms
    are measured off the original disc, but which is still a fantasy and would
    look wrong left out of a table that claims to cover the disc.
Author: suinevere
"""


GENRE_TRACKS = {
    "FANTASY":    (2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 18, 20, 31),
    "SCIFI":      (3, 4, 5, 8, 9, 18, 20, 31),
    "MYSTERY":    (2, 3, 7, 8, 9, 10, 20),
    "HORROR":     (3, 7, 8, 18, 20, 31),
    "UNDERWATER": (3, 6, 8, 12, 18, 20),
    "PIRATE":     (2, 6, 8, 11, 12),
    "ANCIENT":    (2, 3, 6, 8, 18, 20),
    "MODERN":     (5, 9, 10, 11),
    "MEDIEVAL":   (2, 8, 10, 11, 20),
    "SAMPLER":    (2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 18, 20, 31),
}
"""GENRE_TRACKS

Description: Which of the fourteen offerable tracks suit each genre, from two
    measurements rather than from adjectives. The first is what the track
    accompanied on the original disc, in analysis/zork_bg/cd_tracks.csv: 10 is
    the house, 11 the forest, 12 the reservoir and stream, 6 the canyon and the
    beaches, 18 the coal mine, 8 the dam and the Loud Room, 20 the maze, 2 the
    temple, 3 the cold mirror passages, 4 the cellar. The second is
    tools/assets/track_mood.json, measured off the audio itself -- and the two
    agree, which is the reason to trust either: 6 is the brightest and airiest
    thing on the disc and it played over the open canyon, 12 is by a distance
    the most event-dense and it played over running water, 18 has the least
    low end of anything and it played in a hollow mine, 20 is the darkest and
    it played in the maze.

    So: HORROR gets the dark, heavy and oppressive end (7, 8, 18, 20) and not
    the outdoor brightness of 6 or 11. UNDERWATER gets the two water themes 12
    and 6 plus the pressure of 8. MODERN gets 10, the domestic theme, and 11,
    the one outdoor theme that is a garden rather than a wilderness. ANCIENT
    gets 2, which played in the Egypt Room and over the Altar. SCIFI gets the
    machinery and the cold stone -- 18, 8, 3, 4 -- and none of the outdoors.

    Track 7 appears only where it earns it. Nothing on the original disc ever
    selected it, so it carries no room association at all, and it measures dark
    and heavy: it is the one piece of this score with no history to contradict,
    which makes it the obvious first choice for the two genres Zork I has no
    music for at all.

    FANTASY and SAMPLER get everything. This is Zork's own score, and an
    anthology is several worlds at once.
Author: suinevere
"""


GENRE_IMAGES = {
    "SCIFI":      (70, 51, 49, 30, 33, 39, 31, 69, 18, 12, 42, 58, 43, 72, 27),
    "HORROR":     (12, 18, 9, 17, 34, 35, 41, 66, 69, 20, 30, 31, 38, 13, 29),
    "MYSTERY":    (11, 14, 10, 9, 31, 49, 2, 8, 4, 38, 12, 15, 56),
    "MODERN":     (11, 14, 10, 9, 2, 8, 4, 5, 55, 31, 49, 1),
    "UNDERWATER": (26, 27, 25, 24, 59, 60, 52, 61, 63, 64, 70, 51, 35, 36),
    "PIRATE":     (60, 59, 63, 61, 64, 54, 53, 28, 56, 50, 2, 8, 52, 73),
    "ANCIENT":    (45, 44, 62, 48, 47, 46, 29, 23, 41, 6, 22, 37, 16, 40),
}
"""GENRE_IMAGES

Description: The pictures that suit each genre, for correcting a GUESS -- never
    for overruling evidence. A room the vocabulary actually tagged keeps the
    picture its scene argues for, because what a room is beats what kind of
    game it is in: Hitchhiker's has a country lane and a pub in it and neither
    should be handed an instrument panel.

    What this is for is the other half. Nine of Hitchhiker's thirty-two rooms
    carry a tag and three of them are Arthur's kitchen, dining room and living
    room, so "the rest of this game is mostly this kind of place" concluded
    Victorian domestic and put a dim room with a table into the Bridge of the
    Heart of Gold, the Vogon hold and the inside of a sperm whale. The genre is
    a statement about the whole game; the majority of a biased sample is not.

    A genre with no entry constrains nothing. FANTASY, SAMPLER and MEDIEVAL are
    absent on purpose -- this is Zork's own art and all 74 pictures were drawn
    for a fantasy.
Author: suinevere
"""


def images_for(stem):
    """/*----------------------
     | images_for
     | Description: The pictures that suit one game's genre, or an empty tuple
     |     when its genre constrains nothing.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRE_IMAGES, GAME_GENRE
     | Params: stem -- the story stem
     | Returns: a tuple of picture indices, possibly empty
     ----------------------*/"""
    return GENRE_IMAGES.get(GAME_GENRE.get(stem), ())


GENRE_SETTING = {
    "FANTASY":    "pre-industrial, torchlit",
    "SCIFI":      "1970s sci-fi",
    "MYSTERY":    "1930s period",
    "HORROR":     "damp, decaying",
    "UNDERWATER": "cold blue-green light",
    "PIRATE":     "18th century",
    "ANCIENT":    "ancient stone, torchlit",
    "MODERN":     "present day",
    "MEDIEVAL":   "medieval",
}
"""GENRE_SETTING

Description: The century each game is lit in, said in the prompt in place of
    the bare genre word. A genre word on its own does nothing to a picture:
    "scifi" was already the last token of every Planetfall prompt while the
    model put a beige 1990s desktop in a Miniaturization Booth and a modern
    office with double-glazing in the main Computer Room, and "fantasy" was in
    Sorcerer's while the model filled its fairground arcade with lit video
    game cabinets. The model was never told what century it was in.

    These say an ERA and a LIGHT and nothing else, deliberately. The obvious
    version says materials -- worn painted metal for SCIFI, stone and timber
    for FANTASY -- and materials are a claim about what kind of place a room
    is, which is the room's own business: Hitchhiker's is SCIFI and has a pub
    and a country lane in it, both untagged, and neither wants painted metal.
    An era composes with any room. A 1970s country lane is still a country
    lane.

    SAMPLER is absent on purpose, as it is from GENRE_IMAGES. An anthology is
    several worlds in one story file and there is no one century to put it in.

    Two or three words each. They were twice that and the whole run of
    boilerplate came to 24 words against a median of 6 words of room, which at
    CFG 7 is a prompt about lighting with a room mentioned in it.

    UNDERWATER says a colour of light and not that a room is submerged. It said
    "dim green underwater light" for one round and flooded the Scimitar's dry
    cockpit, and most of both underwater games is above the waterline anyway --
    Cutthroats is mainly an island and Seastalker mainly a dome interior.
Author: suinevere
"""


GENRE_LOOK = {
    "FANTASY":    {"style": "painted fantasy illustration"},
    "SAMPLER":    {"style": "painted fantasy illustration"},
    "ANCIENT":    {"style": "painted fantasy illustration"},
    "PIRATE":     {"style": "painted fantasy illustration"},
    "MEDIEVAL":   {"style": "painted fantasy illustration"},
    "SCIFI":      {"style": "painted sci-fi cover art"},
    "UNDERWATER": {"style": "painted sci-fi cover art"},
    "MYSTERY":    {"style": "painted noir illustration"},
    "HORROR":     {"style": "painted noir illustration"},
    "MODERN":     {"style": "painted noir illustration"},
}
"""GENRE_LOOK

Description: How each genre is drawn: the style words, and optionally the
    checkpoint to draw it with. Three looks rather than nine, because the games
    fall into three clusters and the tail genres are one game each -- 824 rooms
    of adventure, 557 of science fiction, 378 of period crime. A look per genre
    with one game behind it is a look nobody can judge.

    "checkpoint" is absent from every entry on purpose. A checkpoint is the
    strongest thing in this whole pipeline -- stronger than any wording, which
    a session of arguing with a photorealism model established at length -- so
    a second one is worth having, but only measured. Add
    "checkpoint": "some_model" to a cluster once it is downloaded and
    tools/probe_prompts.py --checkpoint has shown it is better; every plate of
    that cluster then draws with it and the rest are untouched.
Author: suinevere
"""


def look_for(stem):
    """/*----------------------
     | look_for
     | Description: The style words and the checkpoint for one game's genre.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRE_LOOK, GAME_GENRE
     | Params: stem -- the story stem
     | Returns: {"style": ..., "checkpoint": ...}, either possibly absent
     ----------------------*/"""
    return GENRE_LOOK.get(GAME_GENRE.get(stem), {})


GAME_SETTING = {
    "BALLYHOO": "circus, 1930s",
    "MOONMIST": "cornish castle, 1980s",
    "LURKING":  "1980s university, damp",
    "INFIDEL":  "egyptian desert dig",
    "STATFALL": "aboard a space station, 1970s sci-fi",
    "PLNTFALL": "aboard a space station, 1970s sci-fi",
    "SUSPENDD": "underground complex, 1970s sci-fi",
    "STARCROS": "aboard a spacecraft, 1970s sci-fi",
    "SEASTLKR": "undersea dome, cold blue-green light",
}
"""GAME_SETTING

Description: The games whose genre does not say where they happen, and where
    they happen. A genre is the coarsest thing true of a story and for most of
    them it is enough, but Ballyhoo is a MYSTERY the way a circus is a mystery:
    every room of it is under canvas or on the lot, and "1930s period" got a
    suburban back yard for a room whose description mentions a tent -- the tent
    having been dropped with the sentence that named it, because that sentence
    also gave the exits.

    Moonmist's is a correction rather than an addition: it is filed MYSTERY and
    would be lit as the 1930s, and it is a house party in Cornwall in the
    1980s.

    The four science fiction entries say where and not only when, which the
    genre phrase is forbidden to do. Stationfall's station is built like a
    small town and its rooms say so -- a Rec Shop, a Theatre, a Mayor's Office
    with Main Street below it -- and against that much civic vocabulary two
    words of "1970s sci-fi" bought a street of houses and a giant stone head.
    Saying the game happens aboard a station is not a claim about any one room
    and so is safe here, where it would not be in GENRE_SETTING: Hitchhiker's
    is the same genre and has a pub and a country lane in it.

    Two or three words, like the genre ones, and for the same reason. This
    replaces the genre phrase rather than joining it.
Author: suinevere
"""


def setting_for(stem):
    """/*----------------------
     | setting_for
     | Description: The era-and-place phrase for one game: its own if it has
     |     one, otherwise its genre's, otherwise "".
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GAME_SETTING, GENRE_SETTING, GAME_GENRE
     | Params: stem -- the story stem
     | Returns: a prompt fragment, possibly empty
     ----------------------*/"""
    if stem in GAME_SETTING:
        return GAME_SETTING[stem]
    return GENRE_SETTING.get(GAME_GENRE.get(stem), "")


MAP_FILES = ("MAP.TGA", "MAP2.TGA", "MAP3.TGA", "MAP4.TGA")
"""MAP_FILES

Description: The sheets the map is drawn on, in /TGA on the disc. Index 0 is
    the default and the only one that existed before: torn parchment, and the
    only one of the four that uses palette entry 0, so it alone shows the map
    page's ground colour through where the paper is not.
Author: suinevere
"""

GENRE_MAP = {
    "FANTASY":    0,   # parchment
    "PIRATE":     0,
    "ANCIENT":    0,
    "SAMPLER":    0,
    "MEDIEVAL":   0,
    "UNDERWATER": 1,   # ruled ledger paper
    "MYSTERY":    1,
    "HORROR":     2,   # lined notepaper
    "MODERN":     2,
    "SCIFI":      3,   # a dark screen
}
"""GENRE_MAP

Description: Which sheet each genre's map is drawn on. A game filed under no
    genre at all falls to 0, which is the sheet every game used before this and
    so cannot be a regression for one.
Author: suinevere
"""


def map_bg(stem):
    """/*----------------------
     | map_bg
     | Description: The index into MAP_FILES for one game's map page.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRE_MAP, GAME_GENRE
     | Params: stem -- the story stem
     | Returns: 0..len(MAP_FILES)-1
     ----------------------*/"""
    return GENRE_MAP.get(GAME_GENRE.get(stem), 0)


GENRE_FALLBACK = {
    "FANTASY":    ("CAVE", 4),        # the underground, and the theme its rooms took
    "SCIFI":      ("SHIP_INT", 18),   # inside the hull; 18 is the hollow one
    "MYSTERY":    ("PARLOR", 10),     # a house full of suspects, and the house theme
    "HORROR":     ("DARKROOM", 7),    # 7 is dark and heavy and nothing ever claimed it
    "UNDERWATER": ("SHIP_INT", 12),   # inside the hull, and the water theme
    "PIRATE":     ("SHIP_EXT", 6),    # on deck, and the open-air theme
    "ANCIENT":    ("TEMPLE", 2),      # 2 played in the Egypt Room and over the Altar
    "MODERN":     ("PARLOR", 10),     # the domestic theme, for domestic rooms
    "MEDIEVAL":   ("CASTLE", 2),
    "SAMPLER":    ("CAVE", 4),
}
"""GENRE_FALLBACK

Description: The scene and track to guess for a room the other layers cannot
    reach at all -- one whose game has no tagged room anywhere to argue from.
    It is the weakest thing this app will ever say and it is still worth
    saying: a room holding a deliberate wrong picture can be seen and fixed,
    and a room holding nothing cannot be seen at all.
Author: suinevere
"""


def fallback(stem):
    """/*----------------------
     | fallback
     | Description: The last-resort (scene, track) for one game.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRE_FALLBACK, GAME_GENRE
     | Params: stem -- the story stem
     | Returns: (scene name, track number)
     ----------------------*/"""
    return GENRE_FALLBACK.get(GAME_GENRE.get(stem), GENRE_FALLBACK["FANTASY"])


def tracks_for(stem, assigned=()):
    """/*----------------------
     | tracks_for
     | Description: The tracks one game may be given: silence, its genre's
     |     list, and anything already stored against it.
     |
     |     That last part is not politeness. A menu that cannot represent the
     |     value it is showing does not show it -- the browser falls back to
     |     the first option -- so a room set from a wider pool, or before its
     |     game was filed, would silently read as track 2 and be written back
     |     as track 2 by the next unrelated edit. A shortlist may narrow what
     |     can be chosen next; it may never rewrite what was chosen already.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRE_TRACKS, GAME_GENRE
     | Params: stem -- the story stem; assigned -- tracks already stored for it
     | Returns: a sorted tuple of track numbers, always including 0
     ----------------------*/"""
    g = GAME_GENRE.get(stem)
    if g is None:
        return tuple(sorted({0} | set(assigned) | set(GENRE_TRACKS["FANTASY"])))
    return tuple(sorted({0} | set(GENRE_TRACKS[g]) | set(assigned)))


def genre_of(stem):
    """/*----------------------
     | genre_of
     | Description: One game's genre, or None when it is not in the table.
     |     Answering None rather than guessing a default matters: a story
     |     nobody has filed is a story nobody has thought about, and quietly
     |     calling it FANTASY would hide that.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GAME_GENRE
     | Params: stem -- the story stem
     | Returns: the genre name, or None
     ----------------------*/"""
    return GAME_GENRE.get(stem)


def genre_note(genre):
    """/*----------------------
     | genre_note
     | Description: What one genre means, in a line.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRES
     | Params: genre -- the genre name
     | Returns: the note, or an empty string for an unknown genre
     ----------------------*/"""
    return dict(GENRES).get(genre, "")


def by_genre(stems):
    """/*----------------------
     | by_genre
     | Description: The given stems grouped under their genre, in GENRES order,
     |     with any stem this table has never heard of collected last under
     |     None rather than dropped. A game missing from the vocabulary must
     |     still appear in a list of every game, or the list stops being one.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: GENRES, GAME_GENRE, GENRE_TRACKS, GENRE_IMAGES, GENRE_FALLBACK,
    MAP_FILES, GENRE_MAP
     | Params: stems -- the story stems to group
     | Returns: a list of (genre or None, [stem, ...]), empty genres omitted
     ----------------------*/"""
    out = []
    for name, _note in GENRES:
        held = [s for s in stems if GAME_GENRE.get(s) == name]
        if held:
            out.append((name, held))
    loose = [s for s in stems if s not in GAME_GENRE]
    if loose:
        out.append((None, loose))
    return out
