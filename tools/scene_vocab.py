#!/usr/bin/env python3
"""Scene vocabulary and title rules.

Description: The 32 scenes a room can be tagged with, and the ordered title
    rules that decide the obvious ones.

    Nothing generates C from this any more and nothing fetches anything with
    it. It survives as an INFERENCE INPUT for the presentation review app:
    tools/pres_store.py reads a room's stored scene tag, falls back to
    scene_for_title when the room was never tagged, and uses the result only to
    suggest which Zork I picture and track a human might want. The scene never
    reaches the Saturn.

    SCENES was a table index in three generated C tables, all of which are gone
    with the category art system, so reordering is no longer dangerous -- but
    tools/assets/scenes/*.json stores scenes by NAME, so a rename still orphans
    every room tagged with the old one.

    RULES is ordered and first-match-wins, so priority is expressed by position
    rather than by weights. "Shore Road" is a road, so `road` precedes `shore`.
    A rule whose scene is None is an explicit refusal: a title naming a shape
    ("Dead End", "Cube") cannot be resolved from the title, and refusing is
    better than guessing.

    FETCH_NOUNS -- the stock-photo query words each scene was searched with --
    went with the fetcher it fed.
Author: suinevere
Dependencies: N/A
Globals: SCENES, SCENE_INDEX, RULES
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
    ("forecastle", "SHIP_EXT"),
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
    ("deck", "SHIP_EXT"),
    ("stateroom", "SHIP_INT"), ("cabin", "SHIP_INT"), ("berth", "SHIP_INT"),
    ("engine room", "SHIP_INT"), ("boiler", "SHIP_INT"), ("reactor", "SHIP_INT"),
    ("bridge", None),
    ("airlock", "SPACE"), ("orbit", "SPACE"), ("space", "SPACE"),
)


def scene_for_title(title):
    """The scene a room title names, or None when no rule matches.

    Description: First match in RULES wins, so ordering is the priority
        mechanism. A rule whose scene is None is an explicit refusal --
        "Dead End" names a shape, and matching it early stops "end" like
        patterns claiming it later.
    Author: suinevere
    Dependencies: N/A
    Globals: RULES
    Params: title -- a room's short name
    Returns: a scene name, or None
    """
    t = title.lower()
    for pattern, scene in RULES:
        if pattern in t:
            return scene
    return None
