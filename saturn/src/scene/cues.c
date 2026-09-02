/*----------------------
 | cues.c
 | Description: See cues.h.
 | Author: suinevere
 | Dependencies: string.h, game_cues.inc, cues.h, room_model.h
 | Globals: GAME_CUE_MAP
 ----------------------*/
#include <string.h>

// Ahead of cues.h: its typedef block is guarded on CUE_GAME_N so it steps
// aside once the .inc below has already defined the same names.
#include "game_cues.inc"
#include "cues.h"

/*----------------------
 | cue_game_index
 | Description: See cues.h.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_CUE_MAP
 | Params: release, serial -- the story identity
 | Returns: the row index, or -1
 ----------------------*/
int cue_game_index(unsigned int release, const char *serial) {
    int i;
    if (serial == 0) return -1;
    for (i = 0; i < CUE_GAME_N; i++) {
        const GameCueMap *g = &GAME_CUE_MAP[i];
        if (g->release == release && memcmp(g->serial, serial, 6) == 0) return i;
    }
    return -1;
}

/*----------------------
 | here_holds
 | Description: Whether an object is among the room's collected contents.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the snapshot; obj -- object number
 | Returns: 1 when present, 0 otherwise
 ----------------------*/
static int here_holds(const RoomModel *m, unsigned short obj) {
    int i;
    for (i = 0; i < m->nhere; i++) if (m->here[i] == obj) return 1;
    return 0;
}

/*----------------------
 | carrying
 | Description: Whether an object is in the player's collected inventory.
 |   Answers 0 while the player object is still unknown, which is the right
 |   answer for a cue: an inventory nobody has identified yet is not evidence
 |   the sword is in hand.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the snapshot; obj -- object number
 | Returns: 1 when carried, 0 otherwise
 ----------------------*/
static int carrying(const RoomModel *m, unsigned short obj) {
    int i;
    for (i = 0; i < m->ncarried; i++) if (m->carried[i] == obj) return 1;
    return 0;
}

/*----------------------
 | rule_villain_visible
 | Description: Whether a rule's villain counts as present for cue purposes --
 |   which for a rule marked `unseen` means the story's invisible attribute is
 |   clear. The thief is in a room for most of the game and in it visibly for
 |   very little of it; without this the cue would fire on his wanderings.
 | Author: suinevere
 | Dependencies: room_model.h (room_model_object_attr)
 | Globals: N/A
 | Params: g -- the game's row; r -- the rule
 | Returns: 1 when the villain may raise its cue, 0 otherwise
 ----------------------*/
static int rule_villain_visible(const GameCueMap *g, const CueRule *r) {
    if (!r->unseen) return 1;
    return !room_model_object_attr(r->villain, (int) g->invisible);
}

/*----------------------
 | cue_track
 | Description: See cues.h.
 | Author: suinevere
 | Dependencies: here_holds, carrying, rule_villain_visible,
 |   room_model.h (room_model_object_parent)
 | Globals: GAME_CUE_MAP
 | Params: release, serial, m -- see cues.h
 | Returns: a CD-DA track number, or 0
 ----------------------*/
int cue_track(unsigned int release, const char *serial, const RoomModel *m) {
    int gi = cue_game_index(release, serial);
    const GameCueMap *g;
    int i, d;
    if (gi < 0 || m == 0 || m->room == 0) return 0;
    g = &GAME_CUE_MAP[gi];

    for (i = 0; i < (int) g->nrules; i++) {
        const CueRule *r = &g->rules[i];
        if (r->room != 0 && r->room != m->room) continue;
        if (!here_holds(m, r->villain)) continue;
        if (!rule_villain_visible(g, r)) continue;
        return (int) r->track;
    }

    if (g->danger == 0 || g->sword == 0 || !carrying(m, g->sword)) return 0;
    for (d = 0; d < RM_DIR_N; d++) {
        if (m->exits[d] != RM_EXIT_OPEN || m->dest[d] == 0) continue;
        for (i = 0; i < (int) g->nrules; i++) {
            const CueRule *r = &g->rules[i];
            if (room_model_object_parent(r->villain) != m->dest[d]) continue;
            if (!rule_villain_visible(g, r)) continue;
            return (int) g->danger;
        }
    }
    return 0;
}

/*----------------------
 | cue_take_track / cue_death_track / cue_win_track
 | Description: See cues.h.
 | Author: suinevere
 | Dependencies: cue_game_index
 | Globals: GAME_CUE_MAP
 | Params: release, serial -- the story identity
 | Returns: a CD-DA track number, or 0
 ----------------------*/
int cue_take_track(unsigned int release, const char *serial) {
    int g = cue_game_index(release, serial);
    return g < 0 ? 0 : (int) GAME_CUE_MAP[g].take;
}

int cue_death_track(unsigned int release, const char *serial) {
    int g = cue_game_index(release, serial);
    return g < 0 ? 0 : (int) GAME_CUE_MAP[g].death;
}

int cue_win_track(unsigned int release, const char *serial) {
    int g = cue_game_index(release, serial);
    return g < 0 ? 0 : (int) GAME_CUE_MAP[g].win;
}
