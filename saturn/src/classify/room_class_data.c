/*----------------------
 | room_class_data.c
 | Description: The tunable classification tables: the room keyword -> category
 |   table and the event keyword -> category table, plus the two accessors the
 |   classifier calls to reach them. All of it is data meant to be edited freely;
 |   the logic lives in room_class.c.
 | Author: suinevere
 | Dependencies: room_class.h (TextKeyword, TC_*)
 ----------------------*/
#include "room_class.h"

/*----------------------
 | KW
 | Description: Room keywords mapped to mood categories (best-effort, lowercase),
 |   each tagged with a KT_* tier -- Structure, Biome, or Feature -- for how
 |   reliably the word names the room itself. text_classify_room compares hits
 |   per category tier-first, count-second, to guess a room's mood.
 | Author: suinevere
 ----------------------*/
static const TextKeyword KW[] = {
    {"forest",TC_WILDERNESS,KT_BIOME},{"tree",TC_WILDERNESS,KT_FEATURE},
    {"trees",TC_WILDERNESS,KT_FEATURE},{"woods",TC_WILDERNESS,KT_BIOME},
    {"grove",TC_WILDERNESS,KT_BIOME},{"meadow",TC_WILDERNESS,KT_BIOME},
    {"field",TC_WILDERNESS,KT_BIOME},{"clearing",TC_WILDERNESS,KT_BIOME},
    {"path",TC_WILDERNESS,KT_FEATURE},{"hill",TC_WILDERNESS,KT_BIOME},
    {"mountain",TC_WILDERNESS,KT_BIOME},{"garden",TC_WILDERNESS,KT_BIOME},

    {"cave",TC_UNDERGROUND,KT_STRUCTURE},{"cavern",TC_UNDERGROUND,KT_STRUCTURE},
    {"tunnel",TC_UNDERGROUND,KT_STRUCTURE},{"underground",TC_UNDERGROUND,KT_BIOME},
    {"cellar",TC_UNDERGROUND,KT_STRUCTURE},{"mine",TC_UNDERGROUND,KT_STRUCTURE},
    {"passage",TC_UNDERGROUND,KT_STRUCTURE},{"grotto",TC_UNDERGROUND,KT_STRUCTURE},
    {"crawlway",TC_UNDERGROUND,KT_STRUCTURE},

    {"river",TC_WATER,KT_BIOME},{"stream",TC_WATER,KT_BIOME},
    {"lake",TC_WATER,KT_BIOME},{"pool",TC_WATER,KT_FEATURE},
    {"water",TC_WATER,KT_FEATURE},{"waterfall",TC_WATER,KT_FEATURE},
    {"shore",TC_WATER,KT_BIOME},{"bank",TC_WATER,KT_FEATURE},
    {"underwater",TC_WATER,KT_BIOME},{"flooded",TC_WATER,KT_FEATURE},

    {"ship",TC_NAUTICAL,KT_STRUCTURE},{"boat",TC_NAUTICAL,KT_STRUCTURE},
    {"deck",TC_NAUTICAL,KT_STRUCTURE},{"cabin",TC_NAUTICAL,KT_STRUCTURE},
    {"hull",TC_NAUTICAL,KT_STRUCTURE},{"sea",TC_NAUTICAL,KT_BIOME},
    {"ocean",TC_NAUTICAL,KT_BIOME},{"dock",TC_NAUTICAL,KT_STRUCTURE},
    {"harbor",TC_NAUTICAL,KT_BIOME},{"sail",TC_NAUTICAL,KT_FEATURE},
    {"mast",TC_NAUTICAL,KT_FEATURE},{"submarine",TC_NAUTICAL,KT_STRUCTURE},

    /* A house is not a town. These used to share TC_TOWN, and the art is where
       that showed: TC_HOUSE's pool is single houses (a boarded-up exterior, an
       overgrown one, a dark one) while TC_TOWN's is streets and village lanes.
       Zork I opens on a lone white house in a field, which the street art reads
       completely wrong for. */
    {"house",TC_HOUSE,KT_STRUCTURE},{"kitchen",TC_HOUSE,KT_STRUCTURE},
    {"parlor",TC_HOUSE,KT_STRUCTURE},{"bedroom",TC_HOUSE,KT_STRUCTURE},
    {"attic",TC_HOUSE,KT_STRUCTURE},{"cottage",TC_HOUSE,KT_STRUCTURE},
    {"farmhouse",TC_HOUSE,KT_STRUCTURE},{"porch",TC_HOUSE,KT_STRUCTURE},

    /* Left in TC_TOWN: words that are as much a public building or a settlement as
       a home. "town" and "village" are new -- with the domestic words moved out,
       nothing named the thing the art actually shows. */
    {"town",TC_TOWN,KT_BIOME},{"village",TC_TOWN,KT_BIOME},
    {"street",TC_TOWN,KT_BIOME},{"building",TC_TOWN,KT_STRUCTURE},
    {"hall",TC_TOWN,KT_STRUCTURE},{"office",TC_TOWN,KT_STRUCTURE},
    {"stairs",TC_TOWN,KT_FEATURE},{"square",TC_TOWN,KT_BIOME},
    {"market",TC_TOWN,KT_BIOME},{"shop",TC_TOWN,KT_STRUCTURE},
    {"inn",TC_TOWN,KT_STRUCTURE},{"tavern",TC_TOWN,KT_STRUCTURE},

    {"temple",TC_DUNGEON,KT_STRUCTURE},{"tomb",TC_DUNGEON,KT_STRUCTURE},
    {"crypt",TC_DUNGEON,KT_STRUCTURE},{"ruin",TC_DUNGEON,KT_STRUCTURE},
    {"altar",TC_DUNGEON,KT_FEATURE},{"ancient",TC_DUNGEON,KT_FEATURE},
    {"chamber",TC_DUNGEON,KT_STRUCTURE},{"dungeon",TC_DUNGEON,KT_STRUCTURE},
    {"catacomb",TC_DUNGEON,KT_STRUCTURE},{"vault",TC_DUNGEON,KT_STRUCTURE},

    {"desert",TC_DESERT,KT_BIOME},{"sand",TC_DESERT,KT_FEATURE},
    {"dune",TC_DESERT,KT_BIOME},{"oasis",TC_DESERT,KT_BIOME},
    {"wasteland",TC_DESERT,KT_BIOME},

    {"spell",TC_MAGIC,KT_FEATURE},{"magic",TC_MAGIC,KT_FEATURE},
    {"enchant",TC_MAGIC,KT_FEATURE},{"wizard",TC_MAGIC,KT_FEATURE},
    {"scroll",TC_MAGIC,KT_FEATURE},{"rune",TC_MAGIC,KT_FEATURE},
    {"mystic",TC_MAGIC,KT_FEATURE},{"sorcerer",TC_MAGIC,KT_FEATURE},

    {"console",TC_SCIFI,KT_FEATURE},{"computer",TC_SCIFI,KT_FEATURE},
    {"airlock",TC_SCIFI,KT_STRUCTURE},{"panel",TC_SCIFI,KT_FEATURE},
    {"robot",TC_SCIFI,KT_FEATURE},{"laboratory",TC_SCIFI,KT_STRUCTURE},
    {"reactor",TC_SCIFI,KT_STRUCTURE},{"corridor",TC_SCIFI,KT_STRUCTURE},
    {"module",TC_SCIFI,KT_STRUCTURE},{"cockpit",TC_SCIFI,KT_STRUCTURE},

    {"corpse",TC_HORROR,KT_FEATURE},{"rotting",TC_HORROR,KT_FEATURE},
    {"stench",TC_HORROR,KT_FEATURE},{"shadow",TC_HORROR,KT_FEATURE},
    {"eerie",TC_HORROR,KT_FEATURE},{"decay",TC_HORROR,KT_FEATURE},
    {"skeleton",TC_HORROR,KT_FEATURE},

    {"body",TC_MYSTERY,KT_FEATURE},{"clue",TC_MYSTERY,KT_FEATURE},
    {"murder",TC_MYSTERY,KT_FEATURE},{"evidence",TC_MYSTERY,KT_FEATURE},
    {"study",TC_MYSTERY,KT_STRUCTURE},{"library",TC_MYSTERY,KT_STRUCTURE},
    {"detective",TC_MYSTERY,KT_FEATURE},{"locked",TC_MYSTERY,KT_FEATURE},
};

/*----------------------
 | EV
 | Description: Event keywords (danger / triumph) mapped to categories, lowercase.
 |   text_scan_event fires on any turn's text to override the room's base mood.
 |   Tier is unused here -- events are never tiered -- so every row carries
 |   KT_FEATURE as a harmless placeholder.
 | Author: suinevere
 ----------------------*/
static const TextKeyword EV[] = {
    {"monster",TC_DANGER,KT_FEATURE},{"troll",TC_DANGER,KT_FEATURE},
    {"grue",TC_DANGER,KT_FEATURE},{"attack",TC_DANGER,KT_FEATURE},
    {"fight",TC_DANGER,KT_FEATURE},{"flames",TC_DANGER,KT_FEATURE},
    {"fire",TC_DANGER,KT_FEATURE},{"burning",TC_DANGER,KT_FEATURE},
    {"scream",TC_DANGER,KT_FEATURE},{"danger",TC_DANGER,KT_FEATURE},
    {"treasure",TC_TRIUMPH,KT_FEATURE},{"gold",TC_TRIUMPH,KT_FEATURE},
    {"jewel",TC_TRIUMPH,KT_FEATURE},{"chest",TC_TRIUMPH,KT_FEATURE},
    {"reward",TC_TRIUMPH,KT_FEATURE},{"gleaming",TC_TRIUMPH,KT_FEATURE},
    {"victory",TC_TRIUMPH,KT_FEATURE},
};

/*----------------------
 | text_keywords / text_events
 | Description: Hand back the room-keyword / event-keyword tables and their
 |   lengths.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const TextKeyword* text_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }
