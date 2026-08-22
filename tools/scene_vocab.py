#!/usr/bin/env python3
"""Scene vocabulary and title rules.

Description: The 32 scenes a room can be tagged with, the ordered title rules
    that decide the obvious ones, and the stock-photo nouns each scene is
    fetched with. Single source of truth: the C enum in
    saturn/src/scene/scene_map.h is generated against SCENES, and
    tools/art_nouns.py derives its query vocabulary from FETCH_NOUNS.

    SCENES order is a table index in three generated C tables. Appending is
    safe; reordering silently repoints every row.

    RULES is ordered and first-match-wins, so priority is expressed by position
    rather than by weights. "Shore Road" is a road, so `road` precedes `shore`.

    Every noun in FETCH_NOUNS is a word a photographer types. 589d6c5 measured
    that experience adjectives never return a keeper -- `torchlit` 0/14,
    `bustling` 0/12, against `misty` 85% -- and a guard test carries the dead
    list by name.
Author: suinevere
Dependencies: N/A
Globals: SCENES, SCENE_INDEX, RULES, FETCH_NOUNS
"""

SCENES = (
    "FOREST", "GARDEN", "DESERT", "ROCKY", "SHORE", "RIVER", "ROAD",
    "CAVE", "MAZE", "MINE", "PIT", "CRYPT",
    "HOUSE_EXT", "VILLAGE", "CASTLE", "DOCK",
    "PARLOR", "KITCHEN", "BEDROOM", "BATHROOM", "LIBRARY", "DARKROOM",
    "CORRIDOR", "OFFICE", "LAB", "STORAGE", "CELL", "THEATER",
    "TEMPLE",
    "SHIP_EXT", "SHIP_INT", "SPACE",
)

SCENE_INDEX = {name: i for i, name in enumerate(SCENES)}

RULES = (
    ("dead end", None),
    ("road", "ROAD"), ("driveway", "ROAD"), ("street", "ROAD"),
    ("trail", "ROAD"), ("path", "ROAD"),
    ("closet", "DARKROOM"), ("cupboard", "DARKROOM"), ("wardrobe", "DARKROOM"),
    ("attic", "DARKROOM"), ("garret", "DARKROOM"), ("cellar", "DARKROOM"),
    ("basement", "DARKROOM"),
    ("hallway", "CORRIDOR"), ("corridor", "CORRIDOR"), ("staircase", "CORRIDOR"),
    ("stairway", "CORRIDOR"), ("stairs", "CORRIDOR"), ("landing", "CORRIDOR"),
    ("hall", "CORRIDOR"),
    ("of house", "HOUSE_EXT"), ("outside house", "HOUSE_EXT"),
    ("behind house", "HOUSE_EXT"), ("porch", "HOUSE_EXT"), ("tent", "HOUSE_EXT"),
    ("forest", "FOREST"), ("woods", "FOREST"), ("grove", "FOREST"),
    ("thicket", "FOREST"),
    ("garden", "GARDEN"), ("orchard", "GARDEN"), ("courtyard", "GARDEN"),
    ("desert", "DESERT"), ("dune", "DESERT"), ("oasis", "DESERT"),
    ("canyon", "ROCKY"), ("gorge", "ROCKY"), ("ravine", "ROCKY"),
    ("cliff", "ROCKY"), ("ledge", "ROCKY"), ("precipice", "ROCKY"),
    ("bluff", "ROCKY"), ("mountain", "ROCKY"), ("volcano", "ROCKY"),
    ("summit", "ROCKY"), ("peak", "ROCKY"),
    ("beach", "SHORE"), ("shore", "SHORE"), ("ocean", "SHORE"),
    ("sea", "SHORE"), ("lake", "SHORE"), ("pond", "SHORE"),
    ("reservoir", "SHORE"),
    ("river", "RIVER"), ("stream", "RIVER"), ("brook", "RIVER"),
    ("creek", "RIVER"), ("falls", "RIVER"), ("rapids", "RIVER"),
    ("cavern", "CAVE"), ("cave", "CAVE"), ("grotto", "CAVE"),
    ("tunnel", "CAVE"), ("passage", "CAVE"), ("crawl", "CAVE"),
    ("labyrinth", "MAZE"), ("maze", "MAZE"),
    ("quarry", "MINE"), ("mine", "MINE"), ("shaft", "MINE"),
    ("chasm", "PIT"), ("abyss", "PIT"), ("crevice", "PIT"), ("pit", "PIT"),
    ("catacomb", "CRYPT"), ("mausoleum", "CRYPT"), ("crypt", "CRYPT"),
    ("tomb", "CRYPT"),
    ("village", "VILLAGE"), ("town", "VILLAGE"), ("plaza", "VILLAGE"),
    ("square", "VILLAGE"),
    ("castle", "CASTLE"), ("fortress", "CASTLE"), ("tower", "CASTLE"),
    ("ruin", "CASTLE"),
    ("wharf", "DOCK"), ("dock", "DOCK"), ("pier", "DOCK"),
    ("harbour", "DOCK"), ("harbor", "DOCK"), ("quay", "DOCK"),
    ("living room", "PARLOR"), ("sitting room", "PARLOR"), ("parlour", "PARLOR"),
    ("parlor", "PARLOR"), ("lounge", "PARLOR"), ("dining", "PARLOR"),
    ("foyer", "PARLOR"),
    ("kitchen", "KITCHEN"), ("pantry", "KITCHEN"), ("galley", "KITCHEN"),
    ("bedroom", "BEDROOM"), ("bunk", "BEDROOM"),
    ("bathroom", "BATHROOM"), ("washroom", "BATHROOM"),
    ("lavatory", "BATHROOM"), ("restroom", "BATHROOM"),
    ("library", "LIBRARY"), ("study", "LIBRARY"),
    ("laboratory", "LAB"), ("lab", "LAB"),
    ("office", "OFFICE"), ("cubicle", "OFFICE"),
    ("storeroom", "STORAGE"), ("storage", "STORAGE"),
    ("warehouse", "STORAGE"), ("supply", "STORAGE"),
    ("dungeon", "CELL"), ("prison", "CELL"), ("jail", "CELL"), ("cell", "CELL"),
    ("theatre", "THEATER"), ("theater", "THEATER"),
    ("auditorium", "THEATER"), ("stage", "THEATER"),
    ("cathedral", "TEMPLE"), ("chapel", "TEMPLE"), ("church", "TEMPLE"),
    ("temple", "TEMPLE"), ("shrine", "TEMPLE"), ("altar", "TEMPLE"),
    ("forecastle", "SHIP_EXT"), ("deck", "SHIP_EXT"),
    ("stateroom", "SHIP_INT"), ("cabin", "SHIP_INT"), ("berth", "SHIP_INT"),
    ("engine", "SHIP_INT"), ("boiler", "SHIP_INT"), ("reactor", "SHIP_INT"),
    ("bridge", "SHIP_INT"),
    ("airlock", "SPACE"), ("orbit", "SPACE"), ("space", "SPACE"),
)

FETCH_NOUNS = {
    "FOREST":    ("forest", "woodland", "pine grove", "birch woods"),
    "GARDEN":    ("garden", "orchard", "courtyard", "hedge"),
    "DESERT":    ("desert", "sand dune", "oasis", "arid plain"),
    "ROCKY":     ("canyon", "cliff", "rock ledge", "mountain"),
    "SHORE":     ("beach", "rocky shore", "ocean", "lake"),
    "RIVER":     ("river", "stream", "waterfall", "rapids"),
    "ROAD":      ("dirt road", "country lane", "cobbled street", "footpath"),
    "CAVE":      ("cave", "cavern", "rock tunnel", "grotto"),
    "MAZE":      ("stone maze", "hedge maze", "labyrinth"),
    "MINE":      ("mine tunnel", "mine shaft", "quarry"),
    "PIT":       ("chasm", "crevasse", "deep pit"),
    "CRYPT":     ("crypt", "catacomb", "stone tomb"),
    "HOUSE_EXT": ("cottage", "farmhouse", "clapboard house", "canvas tent"),
    "VILLAGE":   ("village street", "old town square", "market square"),
    "CASTLE":    ("castle", "stone tower", "ruined fortress"),
    "DOCK":      ("wharf", "wooden pier", "harbour", "quay"),
    "PARLOR":    ("parlour", "sitting room", "dining room", "entrance hall"),
    "KITCHEN":   ("kitchen", "pantry", "ship galley"),
    "BEDROOM":   ("bedroom", "bunk room"),
    "BATHROOM":  ("bathroom", "washroom"),
    "LIBRARY":   ("library", "bookshelves", "study desk"),
    "DARKROOM":  ("attic", "cellar", "cluttered closet"),
    "CORRIDOR":  ("stone corridor", "narrow hallway", "wooden staircase"),
    "OFFICE":    ("vintage office", "desk", "filing cabinet"),
    "LAB":       ("laboratory", "control room", "instrument panel"),
    "STORAGE":   ("storeroom", "warehouse", "crates", "shelving"),
    "CELL":      ("prison cell", "dungeon", "iron bars"),
    "THEATER":   ("theatre stage", "auditorium", "empty theatre"),
    "TEMPLE":    ("temple", "stone altar", "chapel", "shrine"),
    "SHIP_EXT":  ("ship deck", "sailing ship", "bow of ship"),
    "SHIP_INT":  ("ship cabin", "engine room", "ship bridge"),
    "SPACE":     ("airlock", "earth from orbit", "starfield"),
}


def scene_for_title(title):
    """/*----------------------
     | scene_for_title
     | Description: The scene a room title names, or None when no rule matches.
     |   First match in RULES wins, so ordering is the priority mechanism. A rule
     |   whose scene is None is an explicit refusal -- "Dead End" names a shape,
     |   and matching it early stops "end" like patterns claiming it later.
     | Author: suinevere
     | Dependencies: N/A
     | Globals: RULES
     | Params: title -- a room's short name
     | Returns: a scene name, or None
     ----------------------*/"""
    t = title.lower()
    for pattern, scene in RULES:
        if pattern in t:
            return scene
    return None
