/*----------------------
 | party.c
 | Description: Implements party.h.
 | Author: suinevere
 ----------------------*/
#include "party.h"

/*----------------------
 | g_room / g_name / g_self
 | Description: The roster. A seat is occupied exactly when its name is not
 |   empty, so the room may be zero for somebody the server has named but not
 |   yet placed -- which happens for a seat claimed between one turn and the
 |   next -- without that seat vanishing from the list.
 | Author: suinevere
 ----------------------*/
static unsigned short g_room[PARTY_SEATS];
static char           g_name[PARTY_SEATS][PARTY_NAME_MAX];
static int            g_self = -1;

/*----------------------
 | party_reset
 | Description: See party.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_name, g_self
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void party_reset(void) {
    int i;
    for (i = 0; i < PARTY_SEATS; i++) {
        g_room[i] = 0;
        g_name[i][0] = '\0';
    }
    g_self = -1;
}

/*----------------------
 | party_set
 | Description: See party.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_name
 | Params: seat -- 0 to PARTY_SEATS-1; room -- object number; name -- username
 | Returns: N/A
 ----------------------*/
void party_set(int seat, unsigned short room, const char *name) {
    int i;
    if (seat < 0 || seat >= PARTY_SEATS) return;
    g_room[seat] = room;
    for (i = 0; i < PARTY_NAME_MAX - 1 && name != 0 && name[i] != '\0'; i++)
        g_name[seat][i] = name[i];
    g_name[seat][i] = '\0';
}

/*----------------------
 | party_set_self
 | Description: See party.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_self
 | Params: seat -- 0 to PARTY_SEATS-1, or -1
 | Returns: N/A
 ----------------------*/
void party_set_self(int seat) {
    g_self = (seat >= 0 && seat < PARTY_SEATS) ? seat : -1;
}

/*----------------------
 | party_self
 | Description: See party.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_self
 | Params: N/A
 | Returns: the local player's seat, or -1
 ----------------------*/
int party_self(void) {
    return g_self;
}

/*----------------------
 | party_seat
 | Description: See party.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_name
 | Params: seat -- the seat; room, name -- receive the occupant, either may be
 |   null
 | Returns: 1 when occupied, 0 otherwise
 ----------------------*/
int party_seat(int seat, unsigned short *room, const char **name) {
    if (seat < 0 || seat >= PARTY_SEATS) return 0;
    if (g_name[seat][0] == '\0') return 0;
    if (room != 0) *room = g_room[seat];
    if (name != 0) *name = g_name[seat];
    return 1;
}

/*----------------------
 | party_count
 | Description: See party.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_name
 | Params: N/A
 | Returns: how many seats are occupied
 ----------------------*/
int party_count(void) {
    int i, n = 0;
    for (i = 0; i < PARTY_SEATS; i++)
        if (g_name[i][0] != '\0') n++;
    return n;
}
