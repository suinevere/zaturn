"""Derive photographable place-nouns for one game's scene, in that game's genre.

Description: A scene names a place; a genre names what that place looks like in
    a particular story. CORRIDOR in Planetfall is a spaceship corridor and in
    Deadline a wood-panelled manor hallway, and a stock-photo search that asks
    for neither gets a hotel. The base vocabulary still comes from
    scene_vocab.FETCH_NOUNS -- deriving rather than duplicating is why a scene's
    fetch words can never drift from its tagging words -- and GENRE_NOUNS
    layers one genre's rewording on top of it, scene by scene.

    Overrides are per (genre, scene) phrases rather than an adjective glued to
    the front of a base noun. Gluing produces "spaceship rocky shore": the base
    phrases already carry their own adjectives, so a second one reads as noise
    to a search engine and returns nothing. Writing the whole phrase is more
    typing and no surprises.

    A genre overrides only the scenes where the picture genuinely differs. An
    outdoor scene usually does not -- a river is a river in every century -- so
    those fall through to the neutral base, and MODERN overrides nothing at all
    because the base vocabulary is already contemporary.
Author: suinevere
Dependencies: scene_vocab, art_terms
Globals: GENRES, DEFAULT_GENRE, GAME_GENRE, GENRE_NOUNS
"""
import art_terms
import scene_vocab as vocab

GENRES = ("FANTASY", "SCIFI", "DETECTIVE", "HORROR", "NAUTICAL", "ANCIENT",
          "MODERN")

DEFAULT_GENRE = "MODERN"
"""DEFAULT_GENRE

Description: What an unlisted game searches as. MODERN, because it overrides
    nothing -- an unknown story gets the neutral base vocabulary rather than
    someone else's century.
Author: suinevere
"""

GAME_GENRE = {
    "ADVENT": "FANTASY",
    "BALLYHOO": "MODERN",
    "CUTHROAT": "MODERN",
    "DEADLINE": "DETECTIVE",
    "ENCHANTR": "FANTASY",
    "HITCHHKR": "SCIFI",
    "HOLYWOOD": "MODERN",
    "HYPOCOND": "MODERN",
    "INFIDEL": "ANCIENT",
    "INFOSAM5": "FANTASY",
    "INFOSAM7": "FANTASY",
    "LEATHERG": "SCIFI",
    "LURKING": "HORROR",
    "MOONMIST": "HORROR",
    "MZORKI": "FANTASY",
    "MZORKI2": "FANTASY",
    "MZORKII": "FANTASY",
    "PLNDHRTS": "NAUTICAL",
    "PLNTFALL": "SCIFI",
    "SEASTLKR": "SCIFI",
    "SORCERER": "FANTASY",
    "SPLBRKR": "FANTASY",
    "STARCROS": "SCIFI",
    "STATFALL": "SCIFI",
    "SUSPECT": "DETECTIVE",
    "SUSPENDD": "SCIFI",
    "WISHBRNG": "FANTASY",
    "WITNESS": "DETECTIVE",
    "ZORK1": "FANTASY",
    "ZORK2": "FANTASY",
    "ZORK3": "FANTASY",
}

GENRE_NOUNS = {
    "FANTASY": {
        "CORRIDOR":  ("stone corridor", "castle passage", "vaulted passage"),
        "PARLOR":    ("great hall", "medieval hall", "timber framed room"),
        "KITCHEN":   ("rustic kitchen", "stone hearth", "cauldron fireplace"),
        "LIBRARY":   ("old library", "ancient books", "manuscript shelves"),
        "OFFICE":    ("wooden writing desk", "scriptorium", "quill and parchment"),
        "LAB":       ("alchemy table", "apothecary bottles", "potion shelves"),
        "VILLAGE":   ("medieval village", "thatched cottages", "old town lane"),
        "HOUSE_EXT": ("stone cottage", "thatched cottage", "timber farmhouse"),
        "CELL":      ("dungeon cell", "iron barred dungeon", "stone oubliette"),
        "STORAGE":   ("cellar barrels", "storeroom sacks", "rope and crates"),
        "TEMPLE":    ("stone temple", "ancient altar", "ruined shrine"),
        "SHIP_EXT":  ("wooden sailing ship", "old boat on water"),
        "SHIP_INT":  ("wooden ship hold", "ship cabin lantern"),
        "BEDROOM":   ("four poster bed", "rustic bedchamber"),
        "DARKROOM":  ("dark stone cellar", "unlit crawlspace"),
    },
    "SCIFI": {
        "CORRIDOR":  ("spaceship corridor", "space station corridor",
                      "metal walkway"),
        "LAB":       ("spacecraft control room", "science laboratory",
                      "instrument console"),
        "OFFICE":    ("computer terminal room", "operations console"),
        "SHIP_INT":  ("spaceship interior", "space capsule cockpit",
                      "airlock interior"),
        "SHIP_EXT":  ("spacecraft", "space station exterior", "rocket on pad"),
        "STORAGE":   ("cargo hold", "metal storage bay", "supply crates"),
        "CELL":      ("steel holding cell", "metal brig"),
        "BEDROOM":   ("sleeping pod", "bunk berth"),
        "KITCHEN":   ("galley kitchen", "steel galley"),
        "DARKROOM":  ("unlit machine room", "dark maintenance shaft"),
        "MAZE":      ("service tunnel", "ductwork maze"),
        "MINE":      ("mining machinery tunnel", "drilling rig underground"),
        "HOUSE_EXT": ("geodesic dome", "prefab habitat"),
        "VILLAGE":   ("futuristic city", "domed colony"),
        "FOREST":    ("alien jungle", "strange fungus forest"),
        "TEMPLE":    ("alien monolith", "monument chamber"),
        "PIT":       ("reactor shaft", "deep industrial shaft"),
        "CAVE":      ("ice cave", "alien cavern"),
        "PARLOR":    ("lounge module", "observation lounge"),
        "DOCK":      ("docking bay", "landing pad", "shuttle bay"),
        "LIBRARY":   ("data archive", "server room"),
        "GARDEN":    ("hydroponic garden", "greenhouse dome"),
    },
    "DETECTIVE": {
        "OFFICE":    ("1940s office", "vintage study", "desk lamp typewriter"),
        "PARLOR":    ("1930s drawing room", "country house sitting room",
                      "wood panelled room"),
        "LIBRARY":   ("gentlemans library", "leather armchair books"),
        "BEDROOM":   ("1930s bedroom", "vintage bedroom"),
        "KITCHEN":   ("1930s kitchen", "servants kitchen"),
        "CORRIDOR":  ("manor hallway", "wood panelled corridor"),
        "HOUSE_EXT": ("country manor", "brick mansion", "colonial house"),
        "GARDEN":    ("manor garden", "rose garden", "glasshouse"),
        "ROAD":      ("gravel driveway", "rainy street at night"),
        "DARKROOM":  ("cluttered attic", "dim cellar"),
        "STORAGE":   ("old storeroom", "dusty shed"),
        "THEATER":   ("old theatre stage", "empty auditorium"),
        "VILLAGE":   ("1930s town street", "rainy city street"),
        "CELL":      ("police holding cell", "interrogation room"),
        "BATHROOM":  ("1930s bathroom", "vintage tiled bathroom"),
    },
    "HORROR": {
        "CORRIDOR":  ("derelict corridor", "peeling hallway"),
        "CELL":      ("dungeon cell", "rusted iron door"),
        "CRYPT":     ("catacomb", "ossuary", "stone tomb"),
        "DARKROOM":  ("dark basement", "abandoned attic"),
        "LAB":       ("abandoned laboratory", "derelict machine room"),
        "HOUSE_EXT": ("derelict mansion", "gothic manor at night"),
        "CASTLE":    ("gothic castle", "ruined castle at dusk"),
        "LIBRARY":   ("dusty old library", "occult library"),
        "PARLOR":    ("abandoned drawing room", "decaying parlour"),
        "CAVE":      ("dripping cavern", "black cave"),
        "MINE":      ("abandoned mine tunnel",),
        "TEMPLE":    ("ruined chapel", "abandoned church interior"),
        "FOREST":    ("misty forest at night", "dead trees fog"),
        "STORAGE":   ("abandoned storeroom",),
        "KITCHEN":   ("derelict kitchen",),
        "MAZE":      ("dark stone labyrinth",),
        "PIT":       ("black pit", "deep dark shaft"),
        "BEDROOM":   ("abandoned bedroom", "iron bed peeling wall"),
        "GARDEN":    ("overgrown garden", "dead garden in fog"),
        "ROAD":      ("foggy road at night",),
        "BATHROOM":  ("derelict bathroom",),
    },
    "NAUTICAL": {
        "SHIP_EXT":  ("tall ship deck", "sailing ship rigging", "galleon"),
        "SHIP_INT":  ("wooden ship cabin", "ship hold barrels",
                      "captains cabin"),
        "DOCK":      ("old harbour quay", "wooden pier tall ships"),
        "PARLOR":    ("colonial sitting room", "18th century room"),
        "BEDROOM":   ("18th century bedroom",),
        "KITCHEN":   ("ship galley", "hearth kitchen"),
        "STORAGE":   ("ship hold", "barrels and rope"),
        "CELL":      ("ship brig",),
        "VILLAGE":   ("colonial port town",),
        "HOUSE_EXT": ("colonial house", "plantation house"),
        "CORRIDOR":  ("ship passageway",),
        "DARKROOM":  ("dark ship hold",),
        "LIBRARY":   ("chart room", "old maps and instruments"),
    },
    "ANCIENT": {
        "TEMPLE":    ("egyptian temple", "hieroglyph wall",
                      "stone sarcophagus chamber"),
        "CRYPT":     ("burial chamber", "sarcophagus", "tomb hieroglyphs"),
        "CORRIDOR":  ("stone tomb passage", "hieroglyph corridor"),
        "DESERT":    ("sahara dune", "desert expedition camp"),
        "HOUSE_EXT": ("desert tent camp",),
        "STORAGE":   ("excavation crates",),
        "MAZE":      ("stone tomb labyrinth",),
        "PIT":       ("excavation shaft",),
        "CAVE":      ("rock cut chamber",),
        "ROAD":      ("desert track",),
        "RIVER":     ("nile river", "river reeds"),
        "SHIP_EXT":  ("nile sailing boat",),
    },
    "MODERN": {},
}


def genre_for_game(game):
    """The genre a story searches in.

    Description: An unlisted stem gets DEFAULT_GENRE rather than a KeyError, so
        a story added to the corpus fetches neutral pictures on the day it
        arrives and gains a genre when someone gets round to it.
    Author: suinevere
    Dependencies: N/A
    Globals: GAME_GENRE, DEFAULT_GENRE
    Params: game -- a story stem, e.g. "ZORK1"
    Returns: a genre name from GENRES
    """
    return GAME_GENRE.get(game, DEFAULT_GENRE)


def nouns_for_scene(scene, genre=None, terms=None):
    """The stock-photo query phrases to fetch one scene in one genre.

    Description: A hand-edited override first, then the genre's own phrases
        for that scene, then scene_vocab.FETCH_NOUNS's neutral ones. The
        override comes first because the other two are shipped guesses and it
        is the correction someone made after seeing what they returned.

        An unknown scene name gets an empty tuple rather than a KeyError -- a
        caller iterating scene_vocab.SCENES never needs to guard the lookup,
        and a caller passing a typo sees "no nouns" instead of a crash. An
        unknown genre falls through for the same reason.
    Author: suinevere
    Dependencies: scene_vocab, art_terms
    Globals: GENRE_NOUNS
    Params: scene -- an SC_* scene name, e.g. "FOREST"; genre -- a name from
        GENRES, or None for the neutral base vocabulary; terms -- a loaded
        art_terms document, or None to consult no overrides
    Returns: a tuple of query phrases, possibly empty
    """
    if terms is not None:
        edited = art_terms.scene_override(terms, scene)
        if edited:
            return tuple(edited)
    override = GENRE_NOUNS.get(genre or "", {}).get(scene)
    if override:
        return tuple(override)
    return vocab.FETCH_NOUNS.get(scene, ())


def nouns_for_game(game, scenes, terms=None):
    """One game's whole shopping list: scene -> phrases, ready to search.

    Description: Resolves each scene's phrases, then appends the story's own
        filter terms to every one of them -- a period or a setting is not a
        place to photograph, so it can only ever narrow the places.

        Skips a scene with no phrases rather than emitting an empty list,
        because art_queries.validate refuses a scene it cannot search and a
        caller should not have to filter first.
    Author: suinevere
    Dependencies: art_terms
    Globals: N/A
    Params: game -- a story stem; scenes -- the scene names that game needs;
        terms -- a loaded art_terms document, or None
    Returns: dict mapping scene to a tuple of query phrases
    """
    genre = genre_for_game(game)
    filters = art_terms.game_terms(terms, game) if terms is not None else ()
    out = {}
    for scene in scenes:
        words = nouns_for_scene(scene, genre, terms)
        if words:
            out[scene] = art_terms.apply_terms(words, filters)
    return out
