/*----------------------
 | game_cues.inc
 | Description: GENERATED FILE -- do not edit by hand; produced by
 |   tools/gen_cues.py. The situational CD-DA cues the original
 |   Saturn release played over a room's own theme, resolved to the
 |   Z-machine object numbers this port actually walks. Rules are
 |   ordered and the first match wins. track 0 anywhere means the
 |   cue is not offered for that game.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short villain;
    unsigned short room;
    unsigned char  track;
    unsigned char  unseen;
} CueRule;
typedef struct {
    unsigned short release;
    const char *serial;
    const CueRule *rules;
    unsigned char  nrules;
    unsigned char  invisible;
    unsigned short sword;
    unsigned char  danger;
    unsigned char  take;
    unsigned char  death;
    unsigned char  win;
} GameCueMap;
#define CUE_GAME_N 1

static const CueRule ZORK1_CUE_RULES[4] = {
    { 217, 102, 14, 0 },   /* troll in The Troll Room */
    { 186, 185, 17, 0 },   /* cyclops in Cyclops Room */
    { 114, 190, 16, 1 },   /* thief in Treasure Room */
    { 114,   0, 15, 1 },   /* thief anywhere */
};

static const GameCueMap GAME_CUE_MAP[CUE_GAME_N] = {
    { 88, "840726", ZORK1_CUE_RULES, 4, 7, 110,
      13, 25, 19, 30 },
};
