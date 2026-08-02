/*----------------------
 | music_data.c
 | Description: The tunable data the music engine reads: room keyword -> category
 |   table, event keyword -> category table, per-category CD-DA track pools, and
 |   the (currently empty) per-game room->category override maps, plus the three
 |   accessors the engine calls to reach them. All of it is data meant to be
 |   edited freely; the engine logic lives in music.c.
 | Author: suinevere
 | Dependencies: music.h (TextKeyword, MC_*, TEXT_NUM_CATEGORIES), string.h
 ----------------------*/
#include "music.h"
#include <string.h>

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
 | P_* pools and CATEGORY_POOL
 | Description: Per-category pools of CD-DA track numbers (2..32); Dynamic mode
 |   picks one at random on a category change. The neutral pool is folded into
 |   every category, so neutral ambience can surface anywhere. POOL() pairs each
 |   array with its length into the CATEGORY_POOL table, indexed by MC_* id.
 | Author: suinevere
 ----------------------*/
static const unsigned char P_NEUTRAL[]     = {4,5,6,10,11,12,16,22,24,28,30};
static const unsigned char P_WILDERNESS[]  = {4,5,6,9,10,11,12,16,17,22,24,28,30,31};
static const unsigned char P_UNDERGROUND[] = {2,3,4,5,6,7,10,11,12,16,18,19,20,22,23,24,28,29,30};
static const unsigned char P_WATER[]       = {2,4,5,6,7,8,10,11,12,16,20,21,22,24,26,28,30};
static const unsigned char P_NAUTICAL[]    = {2,3,4,5,6,7,10,11,12,16,19,20,21,22,24,26,28,30};
static const unsigned char P_TOWN[]        = {4,5,6,9,10,11,12,16,22,24,28,30};
static const unsigned char P_DUNGEON[]     = {4,5,6,9,10,11,12,16,17,18,19,20,22,23,24,28,29,30};
static const unsigned char P_DESERT[]      = {4,5,6,9,10,11,12,16,22,24,28,30};
static const unsigned char P_MAGIC[]       = {4,5,6,8,10,11,12,16,18,19,21,22,23,24,26,28,29,30};
static const unsigned char P_SCIFI[]       = {3,4,5,6,8,10,11,12,14,15,16,18,19,22,23,24,27,28,30};
static const unsigned char P_HORROR[]      = {2,4,5,6,7,8,10,11,12,13,14,15,16,19,22,24,27,28,30};
static const unsigned char P_MYSTERY[]     = {3,4,5,6,8,10,11,12,15,16,21,22,24,27,28,30};
/* Quieter and more domestic than P_TOWN, which it split off from: no 9 (the
   bustling one that suits a street and not a parlour), plus 21 and 26 for the
   still, indoor end of the disc. Overlaps P_TOWN heavily on purpose -- a house
   and the village around it should not sound like different games. */
static const unsigned char P_HOUSE[]       = {4,5,6,10,11,12,16,21,22,24,26,28,30};
static const unsigned char P_DANGER[]      = {4,5,6,10,11,12,13,14,15,16,17,22,24,27,28,30};
static const unsigned char P_TRIUMPH[]     = {4,5,6,9,10,11,12,16,22,24,25,28,29,30};

/* Row order is the TC_* enum order and has to stay that way -- the category id is
   the index. TC_HOUSE sits after TC_MYSTERY rather than beside TC_TOWN for the
   reason in music.h's TC_PLACE_LAST box: appending shifted no existing row. */
#define POOL(a) { a, (unsigned char)(sizeof(a)/sizeof((a)[0])) }
static const struct { const unsigned char* p; unsigned char n; } CATEGORY_POOL[TEXT_NUM_CATEGORIES] = {
    POOL(P_NEUTRAL), POOL(P_WILDERNESS), POOL(P_UNDERGROUND), POOL(P_WATER),
    POOL(P_NAUTICAL), POOL(P_TOWN), POOL(P_DUNGEON), POOL(P_DESERT),
    POOL(P_MAGIC), POOL(P_SCIFI), POOL(P_HORROR), POOL(P_MYSTERY),
    POOL(P_HOUSE), POOL(P_DANGER), POOL(P_TRIUMPH),
};
#undef POOL

/*----------------------
 | MusicGameMap / GAME_MAPS
 | Description: Per-game room->category overrides keyed by release+serial
 |   (room_cat[room] = cat+1, 0 = none). Empty for v1 -- just a sentinel row;
 |   real maps are added later as data only.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short release; const char* serial;
    const unsigned char* room_cat;
    int nrooms;
} MusicGameMap;
static const MusicGameMap GAME_MAPS[] = { { 0, 0, 0, 0 } };

/*----------------------
 | text_keywords / text_events
 | Description: Hand back the room-keyword / event-keyword tables and their
 |   lengths.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const TextKeyword* text_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }

/*----------------------
 | music_category_pool
 | Description: Returns a category's track pool and its length, or an empty result
 |   for an out-of-range category.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_POOL
 | Params: category -- MC_* id; out -- receives the pool pointer (may be NULL)
 | Returns: the pool length (0 when out of range)
 ----------------------*/
int music_category_pool(int category, const unsigned char** out) {
    if (category < 0 || category >= TEXT_NUM_CATEGORIES) { if (out) *out = 0; return 0; }
    if (out) *out = CATEGORY_POOL[category].p;
    return CATEGORY_POOL[category].n;
}

/*----------------------
 | text_game_room_category
 | Description: Looks up a room's authored category for the loaded game, matching
 |   a GAME_MAPS row by release and 6-char serial. With no map (the v1 default) or
 |   a room past the map's end, returns -1 so the engine falls back to keyword
 |   classification.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_MAPS
 | Params: release -- Z-machine release; serial -- game serial; room -- room id
 | Returns: the MC_* category, or -1 when unmapped
 ----------------------*/
int text_game_room_category(unsigned int release, const char* serial, unsigned int room) {
    for (int i = 0; i < (int)(sizeof GAME_MAPS / sizeof GAME_MAPS[0]); i++) {
        const MusicGameMap* g = &GAME_MAPS[i];
        if (g->serial == 0) continue;
        if (g->release != release || memcmp(g->serial, serial, 6) != 0) continue;
        if ((int)room >= g->nrooms) return -1;
        int v = g->room_cat[room];
        return v ? v - 1 : -1;
    }
    return -1;
}
