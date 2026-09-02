/*----------------------
 | dump_exits.c
 | Description: Host tool that binds a story image through room_model.c's real
 |   public API and prints every non-RM_EXIT_NONE direction for object numbers
 |   1..255: room, direction index, destination, exit state -- one line per
 |   direction, space-separated integers. Exists so test_exit_dests.py can
 |   cross-check the shipped C decoder's actual output against an independent
 |   Python decode of the same story bytes, rather than trusting that the two
 |   agree.
 | Author: suinevere
 | Build: gcc -O2 -Wall -Wextra -I saturn/src/engine -o dump_exits \
 |          saturn/tests/dump_exits.c saturn/src/engine/room_model.c \
 |          && ./dump_exits saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "room_model.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *f;
    long n;
    unsigned char *story;
    int room, dir;

    if (argc != 2) { fprintf(stderr, "usage: dump_exits <story.z3>\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (f == NULL) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) n);
    if (story == NULL || fread(story, 1, (size_t) n, f) != (size_t) n) return 2;
    fclose(f);

    if (!room_model_bind(story, (unsigned int) n)) {
        fprintf(stderr, "bind failed\n");
        return 3;
    }
    room_model_set_exits_only(1);

    for (room = 1; room <= 255; room++) {
        room_model_refresh_room((unsigned short) room);
        {
            const RoomModel *m = room_model_get();
            for (dir = 0; dir < RM_DIR_N; dir++) {
                if (m->exits[dir] == RM_EXIT_NONE) continue;
                printf("%d %d %d %d\n", room, dir, (int) m->dest[dir], (int) m->exits[dir]);
            }
        }
    }
    return 0;
}
