/*----------------------
 | test_room_model_static.c
 | Description: Host test that room_model decodes real Zork I geography from the
 |   *trimmed* story the netbin embeds, given only a room object id, and that in
 |   exits-only mode it reports nothing else.
 |
 |   Three things are pinned here and they fail for different reasons. The exit
 |   tables catch a trim that cut the object or property tables, or a change to
 |   the direction-property decode. The destinations catch a decode that produces
 |   plausible-looking nonsense: 180's north must be 81 and 81's west must be
 |   180, so the map has to actually join up. And exits-only mode must leave
 |   nhere and ncarried at zero, because with it off refresh_room goes on to
 |   collect the room's shipped contents and to *guess* at a player object to
 |   build an inventory from -- both fine against a live story, both a lie
 |   against a static one.
 |
 |   The three ids are not invented. They were captured from a live multizorkd
 |   telling a client where it was as it walked north, north, south from the
 |   start: 180 is West of House, 81 North of House, 75 the Forest Path.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -o /tmp/trms.exe saturn/tests/test_room_model_static.c \
 |          saturn/src/engine/room_model.c -I saturn/src/engine \
 |          && /tmp/trms.exe saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "room_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEST_OF_HOUSE   180
#define NORTH_OF_HOUSE   81
#define FOREST_PATH      75

static int failures = 0;

static void check(const char *name, int ok, const char *detail) {
    if (ok) printf("ok   %s\n", name);
    else  { printf("FAIL %s\n  %s\n", name, detail); failures++; }
}

static void check_exit(const char *room, int dir, const char *dirname,
                       int want_state, int want_dest) {
    const RoomModel *m = room_model_get();
    char detail[128];
    int ok = (m->exits[dir] == want_state)
          && (want_dest < 0 || m->dest[dir] == (unsigned short) want_dest);
    snprintf(detail, sizeof detail, "%s %s: state %d dest %u, wanted state %d dest %d",
             room, dirname, (int) m->exits[dir], (unsigned int) m->dest[dir],
             want_state, want_dest);
    {
        char name[128];
        snprintf(name, sizeof name, "%s %s", room, dirname);
        check(name, ok, detail);
    }
}

int main(int argc, char **argv) {
    FILE *f;
    long n;
    unsigned char *story;
    unsigned int himem;

    if (argc != 2) { fprintf(stderr, "usage: test_room_model_static <story>\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (f == NULL) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) n);
    if (story == NULL || fread(story, 1, (size_t) n, f) != (size_t) n) return 2;
    fclose(f);

    /* Bind only what the netbin embeds: everything below the high-memory base. */
    himem = ((unsigned int) story[0x04] << 8) | story[0x05];
    check("trimmed story binds", room_model_bind(story, himem) == 1,
          "bind refused the trimmed image -- did the trim cut the object table?");
    check("model reports available", room_model_available() == 1, "");

    room_model_set_exits_only(1);

    check("no room known before the first refresh", !room_model_has_room(),
          "has_room was true before any id arrived");

    room_model_refresh_room(WEST_OF_HOUSE);
    check("a room is known after a refresh", room_model_has_room(), "");
    check_exit("West of House", RM_N,  "north", RM_EXIT_OPEN,    NORTH_OF_HOUSE);
    check_exit("West of House", RM_E,  "east",  RM_EXIT_BLOCKED, -1);
    check_exit("West of House", RM_W,  "west",  RM_EXIT_OPEN,    78);
    check_exit("West of House", RM_S,  "south", RM_EXIT_OPEN,    80);

    room_model_refresh_room(NORTH_OF_HOUSE);
    check_exit("North of House", RM_N, "north", RM_EXIT_OPEN,    FOREST_PATH);
    check_exit("North of House", RM_W, "west",  RM_EXIT_OPEN,    WEST_OF_HOUSE);
    check_exit("North of House", RM_S, "south", RM_EXIT_BLOCKED, -1);

    room_model_refresh_room(FOREST_PATH);
    check_exit("Forest Path", RM_S,    "south", RM_EXIT_OPEN,    NORTH_OF_HOUSE);
    check_exit("Forest Path", RM_UP,   "up",    RM_EXIT_OPEN,    88);

    /* The whole point of the mode. */
    {
        const RoomModel *m = room_model_get();
        char detail[96];
        snprintf(detail, sizeof detail, "nhere=%d ncarried=%d", m->nhere, m->ncarried);
        check("exits-only reports no contents and no inventory",
              m->nhere == 0 && m->ncarried == 0, detail);
    }

    /* With the mode off the same call fills contents -- the behaviour the CD
       build wants and the netbin must not have. Asserting it here means nobody
       can quietly make exits-only a no-op. */
    room_model_set_exits_only(0);
    room_model_refresh_room(WEST_OF_HOUSE);
    {
        const RoomModel *m = room_model_get();
        char detail[96];
        snprintf(detail, sizeof detail, "nhere=%d with exits-only off", m->nhere);
        check("with the mode off, contents do come back", m->nhere > 0, detail);
    }

    printf("\n");
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("test_room_model_static: OK\n");
    return 0;
}
