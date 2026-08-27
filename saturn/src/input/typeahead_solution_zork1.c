/*----------------------
 | typeahead_solution.c
 | Description: GENERATED FILE -- do not edit by hand; produced by
 |   tools/typeahead/gen_solution.py. Per-game "solution" overlay: base-weight and
 |   transition boosts derived from a winning walkthrough, applied on top of the
 |   runtime grammar layer. Keyed by the story's release number + serial, so it
 |   only touches the game it was built for.
 | Author: suinevere
 | Dependencies: typeahead_solution.h, string.h
 ----------------------*/

#include "typeahead_solution.h"
#include <string.h>

/*----------------------
 | SolWord / SolLink / Solution
 | Description: One boosted word (text + base weight), one boosted transition
 |   (word a -> word b + weight), and one game's overlay (release + serial keying
 |   its word and link arrays).
 | Author: suinevere
 ----------------------*/
typedef struct { const char* w; short wt; } SolWord;
typedef struct { const char* a; const char* b; short wt; } SolLink;
typedef struct {
    unsigned short release; const char* serial;
    const SolWord* words; int nwords;
    const SolLink* links; int nlinks;
} Solution;

/*----------------------
 | gN_words / gN_links (generated per game)
 | Description: The generated data: for each game index N, its boosted-word and
 |   boosted-link arrays, referenced by the SOLUTIONS table below.
 | Author: suinevere
 ----------------------*/
static const SolWord g0_words[] = { {"all",43}, {"bag",41}, {"bar",42}, {"basket",44}, {"bauble",42}, {"bell",42}, {"boat",42}, {"bolt",41}, {"book",43}, {"bracelet",41}, {"buoy",43}, {"button",41}, {"canary",43}, {"candles",43}, {"case",52}, {"chalice",42}, {"chimney",41}, {"climb",43}, {"close",41}, {"coal",43}, {"coffin",45}, {"cross",41}, {"diamond",42}, {"dig",44}, {"door",43}, {"down",68}, {"drop",54}, {"east",79}, {"echo",41}, {"egg",44}, {"emerald",41}, {"enter",45}, {"from",41}, {"garlic",44}, {"give",41}, {"go",100}, {"gold",42}, {"grate",42}, {"house",45}, {"in",55}, {"inflat",41}, {"inside",42}, {"invent",41}, {"jade",41}, {"kill",48}, {"knife",45}, {"lamp",48}, {"launch",41}, {"leaflet",42}, {"leave",41}, {"lid",43}, {"light",42}, {"machine",41}, {"mailbox",41}, {"match",42}, {"matche",41}, {"mirror",41}, {"move",41}, {"north",75}, {"northeast",45}, {"northwest",44}, {"off",42}, {"on",44}, {"open",52}, {"painting",42}, {"plastic",41}, {"pray",41}, {"pump",42}, {"push",41}, {"put",56}, {"railing",41}, {"rainbow",41}, {"read",42}, {"ring",41}, {"rope",42}, {"rub",41}, {"rug",41}, {"sand",44}, {"scarab",41}, {"sceptre",43}, {"screwdriver",46}, {"shovel",43}, {"skull",43}, {"south",73}, {"southeast",43}, {"southwest",47}, {"stiletto",41}, {"switch",41}, {"sword",47}, {"take",80}, {"thief",44}, {"tie",41}, {"to",42}, {"torch",43}, {"trap",43}, {"tree",42}, {"troll",45}, {"turn",48}, {"ulysse",41}, {"unlock",41}, {"up",63}, {"wait",43}, {"wave",41}, {"west",59}, {"wind",41}, {"window",41}, {"with",52}, {"wrench",43}, {"yellow",41} };
static const SolLink g0_links[] = { {"bar","in",3605}, {"bauble","in",3637}, {"bolt","with",3881}, {"bracelet","in",3622}, {"canary","from",3631}, {"canary","in",3628}, {"candles","with",3855}, {"chalice","in",3634}, {"climb","down",3697}, {"climb","tree",3699}, {"close","lid",3787}, {"coal","in",3789}, {"coffin","in",3911}, {"cross","rainbow",3713}, {"diamond","in",3775}, {"dig","sand",3725}, {"drop","all",3798}, {"drop","book",3852}, {"drop","buoy",3728}, {"drop","coffin",3927}, {"drop","garlic",3729}, {"drop","knife",3970}, {"drop","leaflet",3998}, {"drop","pump",3736}, {"drop","screwdriver",3792}, {"drop","shovel",3720}, {"drop","stiletto",3660}, {"drop","sword",3949}, {"drop","wrench",3879}, {"egg","in",3625}, {"egg","to",3679}, {"enter","house",3994}, {"from","egg",3630}, {"give","egg",3680}, {"go","down",3986}, {"go","east",3996}, {"go","inside",3735}, {"go","north",3982}, {"go","northeast",3895}, {"go","northwest",3918}, {"go","south",3997}, {"go","southeast",3946}, {"go","southwest",3921}, {"go","up",3981}, {"go","west",3993}, {"gold","in",3908}, {"in","basket",3835}, {"in","case",3910}, {"in","machine",3788}, {"inflat","plastic",3739}, {"inside","boat",3734}, {"inside","case",3971}, {"kill","thief",3670}, {"kill","troll",3964}, {"leave","boat",3731}, {"light","candles",3856}, {"light","match",3857}, {"move","rug",3991}, {"off","lamp",3934}, {"on","lamp",3987}, {"open","bag",3915}, {"open","buoy",3718}, {"open","case",3974}, {"open","coffin",3926}, {"open","grate",3650}, {"open","lid",3791}, {"open","mailbox",4000}, {"open","trap",3990}, {"open","window",3995}, {"painting","inside",3972}, {"plastic","with",3738}, {"push","yellow",3886}, {"put","bar",3606}, {"put","bauble",3638}, {"put","bracelet",3623}, {"put","canary",3629}, {"put","chalice",3635}, {"put","coal",3790}, {"put","coffin",3912}, {"put","diamond",3776}, {"put","egg",3626}, {"put","gold",3909}, {"put","painting",3973}, {"put","sceptre",3906}, {"put","screwdriver",3834}, {"put","skull",3620}, {"put","torch",3837}, {"read","book",3853}, {"read","leaflet",3999}, {"ring","bell",3859}, {"rope","to",3943}, {"rub","mirror",3846}, {"sceptre","in",3905}, {"screwdriver","in",3833}, {"skull","in",3619}, {"switch","with",3785}, {"take","all",3876}, {"take","bar",3612}, {"take","bauble",3643}, {"take","bell",3865}, {"take","book",3862}, {"take","buoy",3733}, {"take","canary",3632}, {"take","candles",3863}, {"take","chalice",3659}, {"take","coal",3808}, {"take","coffin",3938}, {"take","diamond",3781}, {"take","egg",3698}, {"take","emerald",3717}, {"take","garlic",3914}, {"take","gold",3923}, {"take","jade",3756}, {"take","knife",3978}, {"take","lamp",3992}, {"take","matche",3890}, {"take","painting",3983}, {"take","rope",3977}, {"take","scarab",3721}, {"take","sceptre",3925}, {"take","screwdriver",3887}, {"take","shovel",3730}, {"take","skull",3850}, {"take","sword",3969}, {"take","torch",3794}, {"take","wrench",3888}, {"thief","with",3669}, {"tie","rope",3944}, {"to","railing",3942}, {"to","thief",3678}, {"torch","in",3836}, {"trap","door",3989}, {"troll","with",3963}, {"turn","bolt",3882}, {"turn","off",3935}, {"turn","on",3988}, {"turn","switch",3786}, {"unlock","grate",3651}, {"up","canary",3645}, {"up","chimney",3980}, {"wave","sceptre",3924}, {"wind","up",3646}, {"with","knife",3668}, {"with","match",3854}, {"with","pump",3737}, {"with","screwdriver",3784}, {"with","sword",3962}, {"with","wrench",3880}, {"yellow","button",3885} };

/*----------------------
 | SOLUTIONS
 | Description: The per-game overlay table, one row per known (release,
 |   serial), pairing each game with its gN_words/gN_links arrays.
 | Author: suinevere
 ----------------------*/
static const Solution SOLUTIONS[] = {
    { 88, "840726", g0_words, 109, g0_links, 149 },
};

/*----------------------
 | sol_word_is_alpha
 | Description: True when a token is purely a-z, so it can be inserted into the
 |   alphabetic trie. Tokens with a digit or punctuation cannot (insert_trie skips
 |   non-letters, which would corrupt a shorter word's node) and are left out.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the token
 | Returns: 1 if all-lowercase-letters and non-empty, 0 otherwise
 ----------------------*/
static int sol_word_is_alpha(const char* s) {
    if (!s || !*s) return 0;
    for (const char* p = s; *p; p++)
        if (*p < 'a' || *p > 'z') return 0;
    return 1;
}

/*----------------------
 | apply_solution_overlay
 | Description: Applies the matching game's overlay onto the trie: boosts each
 |   listed word's base weight (or inserts a-z-only walkthrough vocabulary the
 |   parser dictionary lacks, e.g. the Lurking Horror password), then adds the
 |   boosted transition links between words that exist. Matches by release +
 |   6-byte serial, so it only touches the game it was built for.
 | Author: suinevere
 | Dependencies: typeahead.h (find_exact_word/insert_trie/create_word/
 |   add_solution_link), string.h
 | Globals: SOLUTIONS
 | Params: root -- the trie to boost; story -- the loaded story bytes; len -- its
 |   length
 | Returns: 1 if a matching overlay was applied, 0 otherwise
 ----------------------*/
int apply_solution_overlay(TrieNode* root, const unsigned char* story, unsigned int len) {
    if (len < 0x1a) return 0;
    unsigned short release = (unsigned short)((story[2] << 8) | story[3]);
    const char* serial = (const char*) (story + 0x12);
    for (int i = 0; i < (int)(sizeof(SOLUTIONS) / sizeof(SOLUTIONS[0])); i++) {
        if (SOLUTIONS[i].release != release) continue;
        if (memcmp(SOLUTIONS[i].serial, serial, 6) != 0) continue;
        for (int j = 0; j < SOLUTIONS[i].nwords; j++) {
            const char* sw = SOLUTIONS[i].words[j].w;
            DictionaryWord* w = find_exact_word(root, sw);
            if (w) {
                if (SOLUTIONS[i].words[j].wt > w->base_weight)
                    w->base_weight = SOLUTIONS[i].words[j].wt;
            } else if (sol_word_is_alpha(sw)) {
                // Walkthrough vocabulary the parser dictionary lacks (e.g. the
                // Lurking Horror password): insert it so typeahead can suggest it.
                insert_trie(root, create_word(sw, TYPE_UNKNOWN, SOLUTIONS[i].words[j].wt));
            }
        }
        for (int j = 0; j < SOLUTIONS[i].nlinks; j++) {
            DictionaryWord* a = find_exact_word(root, SOLUTIONS[i].links[j].a);
            DictionaryWord* b = find_exact_word(root, SOLUTIONS[i].links[j].b);
            if (a && b) add_solution_link(a, b, SOLUTIONS[i].links[j].wt);
        }
        return 1;
    }
    return 0;
}
