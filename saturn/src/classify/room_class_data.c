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
 | Description: Room keywords mapped to mood categories (best-effort, lowercase).
 |   text_classify_room counts hits per category to guess a room's mood.
 | Author: suinevere
 ----------------------*/
static const TextKeyword KW[] = {
    {"forest",TC_WILDERNESS},{"tree",TC_WILDERNESS},{"trees",TC_WILDERNESS},
    {"woods",TC_WILDERNESS},{"grove",TC_WILDERNESS},{"meadow",TC_WILDERNESS},
    {"field",TC_WILDERNESS},{"clearing",TC_WILDERNESS},{"path",TC_WILDERNESS},
    {"hill",TC_WILDERNESS},{"mountain",TC_WILDERNESS},{"garden",TC_WILDERNESS},

    {"cave",TC_UNDERGROUND},{"cavern",TC_UNDERGROUND},{"tunnel",TC_UNDERGROUND},
    {"underground",TC_UNDERGROUND},{"cellar",TC_UNDERGROUND},{"mine",TC_UNDERGROUND},
    {"passage",TC_UNDERGROUND},{"grotto",TC_UNDERGROUND},{"crawlway",TC_UNDERGROUND},

    {"river",TC_WATER},{"stream",TC_WATER},{"lake",TC_WATER},{"pool",TC_WATER},
    {"water",TC_WATER},{"waterfall",TC_WATER},{"shore",TC_WATER},{"bank",TC_WATER},
    {"underwater",TC_WATER},{"flooded",TC_WATER},

    {"ship",TC_NAUTICAL},{"boat",TC_NAUTICAL},{"deck",TC_NAUTICAL},{"cabin",TC_NAUTICAL},
    {"hull",TC_NAUTICAL},{"sea",TC_NAUTICAL},{"ocean",TC_NAUTICAL},{"dock",TC_NAUTICAL},
    {"harbor",TC_NAUTICAL},{"sail",TC_NAUTICAL},{"mast",TC_NAUTICAL},{"submarine",TC_NAUTICAL},

    /* A house is not a town. These used to share TC_TOWN, and the art is where
       that showed: TC_HOUSE's pool is single houses (a boarded-up exterior, an
       overgrown one, a dark one) while TC_TOWN's is streets and village lanes.
       Zork I opens on a lone white house in a field, which the street art reads
       completely wrong for. */
    {"house",TC_HOUSE},{"kitchen",TC_HOUSE},{"parlor",TC_HOUSE},
    {"bedroom",TC_HOUSE},{"attic",TC_HOUSE},{"cottage",TC_HOUSE},
    {"farmhouse",TC_HOUSE},{"porch",TC_HOUSE},

    /* Left in TC_TOWN: words that are as much a public building or a settlement as
       a home. "town" and "village" are new -- with the domestic words moved out,
       nothing named the thing the art actually shows. */
    {"town",TC_TOWN},{"village",TC_TOWN},{"street",TC_TOWN},{"building",TC_TOWN},
    {"hall",TC_TOWN},{"office",TC_TOWN},{"stairs",TC_TOWN},{"square",TC_TOWN},
    {"market",TC_TOWN},{"shop",TC_TOWN},{"inn",TC_TOWN},{"tavern",TC_TOWN},

    {"temple",TC_DUNGEON},{"tomb",TC_DUNGEON},{"crypt",TC_DUNGEON},{"ruin",TC_DUNGEON},
    {"altar",TC_DUNGEON},{"ancient",TC_DUNGEON},{"chamber",TC_DUNGEON},
    {"dungeon",TC_DUNGEON},{"catacomb",TC_DUNGEON},{"vault",TC_DUNGEON},

    {"desert",TC_DESERT},{"sand",TC_DESERT},{"dune",TC_DESERT},{"oasis",TC_DESERT},
    {"wasteland",TC_DESERT},

    {"spell",TC_MAGIC},{"magic",TC_MAGIC},{"enchant",TC_MAGIC},{"wizard",TC_MAGIC},
    {"scroll",TC_MAGIC},{"rune",TC_MAGIC},{"mystic",TC_MAGIC},{"sorcerer",TC_MAGIC},

    {"console",TC_SCIFI},{"computer",TC_SCIFI},{"airlock",TC_SCIFI},{"panel",TC_SCIFI},
    {"robot",TC_SCIFI},{"laboratory",TC_SCIFI},{"reactor",TC_SCIFI},{"corridor",TC_SCIFI},
    {"module",TC_SCIFI},{"cockpit",TC_SCIFI},

    {"corpse",TC_HORROR},{"rotting",TC_HORROR},{"stench",TC_HORROR},{"shadow",TC_HORROR},
    {"eerie",TC_HORROR},{"decay",TC_HORROR},{"skeleton",TC_HORROR},

    {"body",TC_MYSTERY},{"clue",TC_MYSTERY},{"murder",TC_MYSTERY},{"evidence",TC_MYSTERY},
    {"study",TC_MYSTERY},{"library",TC_MYSTERY},{"detective",TC_MYSTERY},{"locked",TC_MYSTERY},
};

/*----------------------
 | EV
 | Description: Event keywords (danger / triumph) mapped to categories, lowercase.
 |   text_scan_event fires on any turn's text to override the room's base mood.
 | Author: suinevere
 ----------------------*/
static const TextKeyword EV[] = {
    {"monster",TC_DANGER},{"troll",TC_DANGER},{"grue",TC_DANGER},{"attack",TC_DANGER},
    {"fight",TC_DANGER},{"flames",TC_DANGER},{"fire",TC_DANGER},{"burning",TC_DANGER},
    {"scream",TC_DANGER},{"danger",TC_DANGER},
    {"treasure",TC_TRIUMPH},{"gold",TC_TRIUMPH},{"jewel",TC_TRIUMPH},{"chest",TC_TRIUMPH},
    {"reward",TC_TRIUMPH},{"gleaming",TC_TRIUMPH},{"victory",TC_TRIUMPH},
};

/*----------------------
 | text_keywords / text_events
 | Description: Hand back the room-keyword / event-keyword tables and their
 |   lengths.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const TextKeyword* text_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }
