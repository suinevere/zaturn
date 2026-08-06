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
#include <string.h>

/*----------------------
 | KW
 | Description: Room keywords mapped to mood categories (best-effort, lowercase),
 |   each tagged with a KT_* tier -- Structure, Biome, or Feature -- for how
 |   reliably the word names the room itself. text_classify_room compares hits
 |   per category tier-first, count-second, to guess a room's mood.
 | Author: suinevere
 ----------------------*/
static const TextKeyword KW[] = {
    {"forest",TC_WILDERNESS,KT_BIOME,GN_ANY},{"tree",TC_WILDERNESS,KT_FEATURE,GN_ANY},
    {"woods",TC_WILDERNESS,KT_BIOME,GN_ANY},
    {"grove",TC_WILDERNESS,KT_BIOME,GN_ANY},{"meadow",TC_WILDERNESS,KT_BIOME,GN_ANY},
    {"field",TC_WILDERNESS,KT_BIOME,GN_ANY},{"clearing",TC_WILDERNESS,KT_BIOME,GN_ANY},
    {"path",TC_WILDERNESS,KT_FEATURE,GN_ANY},{"pathway",TC_WILDERNESS,KT_FEATURE,GN_ANY},
    {"hill",TC_WILDERNESS,KT_BIOME,GN_ANY},
    {"mountain",TC_WILDERNESS,KT_BIOME,GN_ANY},{"garden",TC_WILDERNESS,KT_BIOME,GN_ANY},

    {"cave",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},{"cavern",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"tunnel",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},{"underground",TC_UNDERGROUND,KT_BIOME,GN_ANY},
    {"cellar",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},{"mine",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"passage",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"passageway",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"grotto",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},
    {"crawlway",TC_UNDERGROUND,KT_STRUCTURE,GN_ANY},

    {"river",TC_WATER,KT_BIOME,GN_ANY},{"stream",TC_WATER,KT_BIOME,GN_ANY},
    {"lake",TC_WATER,KT_BIOME,GN_ANY},{"pool",TC_WATER,KT_FEATURE,GN_ANY},
    {"water",TC_WATER,KT_FEATURE,GN_ANY},{"waterfall",TC_WATER,KT_FEATURE,GN_ANY},
    {"shore",TC_WATER,KT_BIOME,GN_ANY},{"bank",TC_WATER,KT_FEATURE,GN_ANY},
    {"underwater",TC_WATER,KT_BIOME,GN_ANY},{"flooded",TC_WATER,KT_FEATURE,GN_ANY},

    /* The words that mean two different things. Each reading is its own row, so
       the table stays declarative and the classifier needs no special cases.
       A ship in Starcross is a spacecraft; the same word in Zork is a hull on
       water, and no single-answer row could say both. */
    {"ship",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"ship",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"deck",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"deck",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"cabin",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"cabin",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"hull",TC_NAUTICAL,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"hull",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"boat",TC_NAUTICAL,KT_STRUCTURE,GN_ANY},{"sea",TC_NAUTICAL,KT_BIOME,GN_ANY},
    {"ocean",TC_NAUTICAL,KT_BIOME,GN_ANY},{"dock",TC_NAUTICAL,KT_STRUCTURE,GN_ANY},
    {"harbor",TC_NAUTICAL,KT_BIOME,GN_ANY},{"sail",TC_NAUTICAL,KT_FEATURE,GN_ANY},
    {"mast",TC_NAUTICAL,KT_FEATURE,GN_ANY},{"submarine",TC_NAUTICAL,KT_STRUCTURE,GN_ANY},

    /* A house is not a town. These used to share TC_TOWN, and the art is where
       that showed: TC_HOUSE's pool is single houses (a boarded-up exterior, an
       overgrown one, a dark one) while TC_TOWN's is streets and village lanes.
       Zork I opens on a lone white house in a field, which the street art reads
       completely wrong for. */
    {"house",TC_HOUSE,KT_STRUCTURE,GN_ANY},{"kitchen",TC_HOUSE,KT_STRUCTURE,GN_ANY},
    {"parlor",TC_HOUSE,KT_STRUCTURE,GN_ANY},{"bedroom",TC_HOUSE,KT_STRUCTURE,GN_ANY},
    {"attic",TC_HOUSE,KT_STRUCTURE,GN_ANY},{"cottage",TC_HOUSE,KT_STRUCTURE,GN_ANY},
    {"farmhouse",TC_HOUSE,KT_STRUCTURE,GN_ANY},{"porch",TC_HOUSE,KT_STRUCTURE,GN_ANY},
    /* Not an inflection of "hall": that votes TC_TOWN for public buildings and
       settlements, while every hallway in the library is a domestic interior --
       Cutthroats' inn landing, Deadline's and Moonmist's mansions, Infidel's
       pyramid. "way" is not a general suffix precisely so this can differ. */
    {"hallway",TC_HOUSE,KT_STRUCTURE,GN_ANY},

    /* Left in TC_TOWN: words that are as much a public building or a settlement as
       a home. "town" and "village" are new -- with the domestic words moved out,
       nothing named the thing the art actually shows. */
    {"town",TC_TOWN,KT_BIOME,GN_ANY},{"village",TC_TOWN,KT_BIOME,GN_ANY},
    {"street",TC_TOWN,KT_BIOME,GN_ANY},{"building",TC_TOWN,KT_STRUCTURE,GN_ANY},
    {"hall",TC_TOWN,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"hall",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"office",TC_TOWN,KT_STRUCTURE,GN_ANY},
    {"stairs",TC_TOWN,KT_FEATURE,GN_ANY},{"square",TC_TOWN,KT_BIOME,GN_ANY},
    {"market",TC_TOWN,KT_BIOME,GN_ANY},{"shop",TC_TOWN,KT_STRUCTURE,GN_ANY},
    {"inn",TC_TOWN,KT_STRUCTURE,GN_ANY},{"tavern",TC_TOWN,KT_STRUCTURE,GN_ANY},

    {"temple",TC_DUNGEON,KT_STRUCTURE,GN_ANY},{"tomb",TC_DUNGEON,KT_STRUCTURE,GN_ANY},
    {"crypt",TC_DUNGEON,KT_STRUCTURE,GN_ANY},{"ruin",TC_DUNGEON,KT_STRUCTURE,GN_ANY},
    {"altar",TC_DUNGEON,KT_FEATURE,GN_ANY},{"ancient",TC_DUNGEON,KT_FEATURE,GN_ANY},
    /* A chamber is a tomb in a pyramid and a compartment on a spacecraft, and
       both readings are common enough to need their own row. GN_MODERN sits with
       fantasy rather than sci-fi: Infidel's burial chambers are modern-day
       archaeology, and dropping them from DUNGEON left six pyramid rooms with
       nothing to vote for at all. */
    {"chamber",TC_DUNGEON,KT_STRUCTURE,GN_FANTASY|GN_MODERN},
    {"chamber",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"dungeon",TC_DUNGEON,KT_STRUCTURE,GN_ANY},
    {"catacomb",TC_DUNGEON,KT_STRUCTURE,GN_ANY},{"vault",TC_DUNGEON,KT_STRUCTURE,GN_ANY},

    {"desert",TC_DESERT,KT_BIOME,GN_ANY},{"sand",TC_DESERT,KT_FEATURE,GN_ANY},
    {"dune",TC_DESERT,KT_BIOME,GN_ANY},{"oasis",TC_DESERT,KT_BIOME,GN_ANY},
    {"wasteland",TC_DESERT,KT_BIOME,GN_ANY},

    {"spell",TC_MAGIC,KT_FEATURE,GN_ANY},{"magic",TC_MAGIC,KT_FEATURE,GN_ANY},
    {"enchant",TC_MAGIC,KT_FEATURE,GN_ANY},{"wizard",TC_MAGIC,KT_FEATURE,GN_ANY},
    {"scroll",TC_MAGIC,KT_FEATURE,GN_ANY},{"rune",TC_MAGIC,KT_FEATURE,GN_ANY},
    {"mystic",TC_MAGIC,KT_FEATURE,GN_ANY},{"sorcerer",TC_MAGIC,KT_FEATURE,GN_ANY},

    {"console",TC_SCIFI,KT_FEATURE,GN_ANY},{"computer",TC_SCIFI,KT_FEATURE,GN_ANY},
    {"airlock",TC_SCIFI,KT_STRUCTURE,GN_ANY},
    {"panel",TC_SCIFI,KT_FEATURE,GN_SCIFI|GN_MODERN},
    {"robot",TC_SCIFI,KT_FEATURE,GN_ANY},{"laboratory",TC_SCIFI,KT_STRUCTURE,GN_ANY},
    {"reactor",TC_SCIFI,KT_STRUCTURE,GN_ANY},{"corridor",TC_SCIFI,KT_STRUCTURE,GN_ANY},
    {"module",TC_SCIFI,KT_STRUCTURE,GN_SCIFI},
    {"cockpit",TC_SCIFI,KT_STRUCTURE,GN_ANY},

    {"corpse",TC_HORROR,KT_FEATURE,GN_ANY},{"rotting",TC_HORROR,KT_FEATURE,GN_ANY},
    {"stench",TC_HORROR,KT_FEATURE,GN_ANY},{"shadow",TC_HORROR,KT_FEATURE,GN_ANY},
    {"eerie",TC_HORROR,KT_FEATURE,GN_ANY},{"decay",TC_HORROR,KT_FEATURE,GN_ANY},
    {"skeleton",TC_HORROR,KT_FEATURE,GN_ANY},

    {"body",TC_MYSTERY,KT_FEATURE,GN_ANY},{"clue",TC_MYSTERY,KT_FEATURE,GN_ANY},
    {"murder",TC_MYSTERY,KT_FEATURE,GN_ANY},{"evidence",TC_MYSTERY,KT_FEATURE,GN_ANY},
    {"study",TC_MYSTERY,KT_STRUCTURE,GN_MODERN},
    {"study",TC_HOUSE,KT_STRUCTURE,GN_FANTASY},
    {"library",TC_MYSTERY,KT_STRUCTURE,GN_ANY},
    {"detective",TC_MYSTERY,KT_FEATURE,GN_ANY},{"locked",TC_MYSTERY,KT_FEATURE,GN_ANY},
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
    {"monster",TC_DANGER,KT_FEATURE,GN_ANY},{"troll",TC_DANGER,KT_FEATURE,GN_ANY},
    {"grue",TC_DANGER,KT_FEATURE,GN_ANY},{"attack",TC_DANGER,KT_FEATURE,GN_ANY},
    {"fight",TC_DANGER,KT_FEATURE,GN_ANY},{"flames",TC_DANGER,KT_FEATURE,GN_ANY},
    {"fire",TC_DANGER,KT_FEATURE,GN_ANY},{"burning",TC_DANGER,KT_FEATURE,GN_ANY},
    {"scream",TC_DANGER,KT_FEATURE,GN_ANY},{"danger",TC_DANGER,KT_FEATURE,GN_ANY},
    {"treasure",TC_TRIUMPH,KT_FEATURE,GN_ANY},{"gold",TC_TRIUMPH,KT_FEATURE,GN_ANY},
    {"jewel",TC_TRIUMPH,KT_FEATURE,GN_ANY},{"chest",TC_TRIUMPH,KT_FEATURE,GN_ANY},
    {"reward",TC_TRIUMPH,KT_FEATURE,GN_ANY},{"gleaming",TC_TRIUMPH,KT_FEATURE,GN_ANY},
    {"victory",TC_TRIUMPH,KT_FEATURE,GN_ANY},
};

/*----------------------
 | NEG / POS
 | Description: Spatial modifiers, lowercase. A sentence containing a NEG phrase
 |   describes something the player can see but is not in, so its keyword hits
 |   are discarded outright. A sentence containing a POS phrase names the room
 |   itself, so its hits count double.
 |
 |   Both lists are deliberately short. "you can see" is NOT here despite being a
 |   distance idiom: it introduces genuinely present objects far more often than
 |   distant ones, and discarding those sentences would cost more than the
 |   occasional distant landmark it catches.
 | Author: suinevere
 ----------------------*/
static const char* const NEG[] = {
    "in the distance", "far off", "through the window", "painted on",
    "on the horizon", "beyond the", "you can hear",
};
static const char* const POS[] = {
    "you are in", "you are standing", "you stand", "this is a",
    "you find yourself", "you are on",
};

/*----------------------
 | text_keywords / text_events
 | Description: Hand back the room-keyword / event-keyword tables and their
 |   lengths.
 | Author: suinevere
 ----------------------*/
const TextKeyword* text_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const TextKeyword* text_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }

/*----------------------
 | text_neg_phrases / text_pos_phrases
 | Description: Hand back the negative / positive spatial-modifier tables and
 |   their lengths.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: NEG, POS
 | Params: n -- receives the table length
 | Returns: the table
 ----------------------*/
const char* const* text_neg_phrases(int* n) { *n = (int)(sizeof NEG / sizeof NEG[0]); return NEG; }
const char* const* text_pos_phrases(int* n) { *n = (int)(sizeof POS / sizeof POS[0]); return POS; }

/*----------------------
 | GENRE_KW
 | Description: Marker words that identify a game's genre, for a story with no
 |   GAME_GENRE row. Deliberately narrow: a marker only earns its place if it is
 |   near-impossible in the other genres.
 | Author: suinevere
 ----------------------*/
static const GenreKeyword GENRE_KW[] = {
    {"airlock",GN_SCIFI},{"hyperspace",GN_SCIFI},{"android",GN_SCIFI},
    {"spacesuit",GN_SCIFI},{"reactor",GN_SCIFI},{"robot",GN_SCIFI},
    {"spell",GN_FANTASY},{"elf",GN_FANTASY},{"troll",GN_FANTASY},
    {"wizard",GN_FANTASY},{"sorcerer",GN_FANTASY},{"scroll",GN_FANTASY},
    {"telephone",GN_MODERN},{"elevator",GN_MODERN},{"automobile",GN_MODERN},
    {"cigarette",GN_MODERN},{"newspaper",GN_MODERN},
};

/*----------------------
 | GameGenre / GAME_GENRE
 | Description: The authored genre and default mood of each shipped story, keyed
 |   by Z-header release number and 6-char serial -- the same key SOLUTIONS[] in
 |   typeahead_solution.c and text_game_room_category use.
 |
 |   `fallback` is what the engine shows after a run of rooms whose text says
 |   nothing. TC_NEUTRAL means this game has no default and keeps the older
 |   behaviour of holding the previous picture. Hypochondriac carries TC_NEUTRAL
 |   deliberately: guessing at an obscure game's mood is worse than leaving it.
 |
 |   The two Infocom Samplers are deliberately absent. A sampler carries excerpts
 |   of several games and genuinely changes genre partway through, so inference
 |   describes it better than any single tag could.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short release;
    const char*    serial;
    unsigned char  genre;
    unsigned char  fallback;
} GameGenre;
static const GameGenre GAME_GENRE[] = {
    {   1, "151001", GN_FANTASY, TC_UNDERGROUND },  /* Adventure           */
    {  97, "851218", GN_MODERN , TC_MYSTERY     },  /* Ballyhoo            */
    {  23, "840809", GN_MODERN , TC_NAUTICAL    },  /* Cutthroats          */
    {  27, "831005", GN_MODERN , TC_MYSTERY     },  /* Deadline            */
    {  29, "860820", GN_FANTASY, TC_MAGIC       },  /* Enchanter           */
    {  59, "851108", GN_SCIFI  , TC_SCIFI       },  /* Hitchhiker's Guide  */
    {  37, "861215", GN_MODERN , TC_HOUSE       },  /* Hollywood Hijinx    */
    {  11, "870225", GN_MODERN , TC_NEUTRAL     },  /* Hypochondriac       */
    {  22, "830916", GN_MODERN , TC_DESERT      },  /* Infidel             */
    {  59, "860730", GN_SCIFI  , TC_SCIFI       },  /* Leather Goddesses   */
    { 219, "870912", GN_MODERN , TC_HORROR      },  /* The Lurking Horror  */
    {   9, "861022", GN_MODERN , TC_MYSTERY     },  /* Moonmist            */
    {   2, "840207", GN_FANTASY, TC_UNDERGROUND },  /* Mini-Zork I         */
    {  34, "871124", GN_FANTASY, TC_UNDERGROUND },  /* Mini-Zork I         */
    {   2, "871123", GN_FANTASY, TC_UNDERGROUND },  /* Mini-Zork II        */
    {  26, "870730", GN_MODERN , TC_NAUTICAL    },  /* Plundered Hearts    */
    {  37, "851003", GN_SCIFI  , TC_SCIFI       },  /* Planetfall          */
    {  16, "850603", GN_MODERN , TC_NAUTICAL    },  /* Seastalker          */
    {  15, "851108", GN_FANTASY, TC_MAGIC       },  /* Sorcerer            */
    {  87, "860904", GN_FANTASY, TC_MAGIC       },  /* Spellbreaker        */
    {  17, "821021", GN_SCIFI  , TC_SCIFI       },  /* Starcross           */
    { 107, "870430", GN_SCIFI  , TC_SCIFI       },  /* Stationfall         */
    {  14, "841005", GN_MODERN , TC_MYSTERY     },  /* Suspect             */
    {   8, "840521", GN_SCIFI  , TC_SCIFI       },  /* Suspended           */
    {  69, "850920", GN_FANTASY, TC_TOWN        },  /* Wishbringer         */
    {  22, "840924", GN_MODERN , TC_MYSTERY     },  /* The Witness         */
    {  88, "840726", GN_FANTASY, TC_UNDERGROUND },  /* Zork I              */
    {  48, "840904", GN_FANTASY, TC_UNDERGROUND },  /* Zork II             */
    {  17, "840727", GN_FANTASY, TC_UNDERGROUND },  /* Zork III            */
};

/*----------------------
 | text_genre_keywords
 | Description: Hands back the genre-marker table and its length.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GENRE_KW
 | Params: n -- receives the table length
 | Returns: the table
 ----------------------*/
const GenreKeyword* text_genre_keywords(int* n) {
    *n = (int)(sizeof GENRE_KW / sizeof GENRE_KW[0]);
    return GENRE_KW;
}

/*----------------------
 | text_game_genre
 | Description: Looks up a story's authored genre by release and 6-char serial.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_GENRE
 | Params: release -- Z-machine release number; serial -- the 6-char game serial
 | Returns: the GN_* mask, or 0 when the story is not listed
 ----------------------*/
unsigned char text_game_genre(unsigned int release, const char* serial) {
    int i;
    if (!serial) return 0;
    for (i = 0; i < (int)(sizeof GAME_GENRE / sizeof GAME_GENRE[0]); i++)
        if (GAME_GENRE[i].release == release &&
            memcmp(GAME_GENRE[i].serial, serial, 6) == 0)
            return GAME_GENRE[i].genre;
    return 0;
}

/*----------------------
 | text_game_fallback
 | Description: Looks up a story's default mood by release and 6-char serial.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_GENRE
 | Params: release -- Z-machine release number; serial -- the 6-char game serial
 | Returns: the TC_* default, or TC_NEUTRAL when the story is not listed
 ----------------------*/
unsigned char text_game_fallback(unsigned int release, const char* serial) {
    int i;
    if (!serial) return TC_NEUTRAL;
    for (i = 0; i < (int)(sizeof GAME_GENRE / sizeof GAME_GENRE[0]); i++)
        if (GAME_GENRE[i].release == release &&
            memcmp(GAME_GENRE[i].serial, serial, 6) == 0)
            return GAME_GENRE[i].fallback;
    return TC_NEUTRAL;
}
