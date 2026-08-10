/* Build:
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/trg.exe \
         saturn/tests/test_room_genre.c saturn/src/classify/room_class.c \
         saturn/src/classify/room_class_data.c && /tmp/trg.exe
   The -I saturn/src/sound is needed because room_class.h includes "music.h"
   unqualified. */
#include "../src/classify/room_class.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    room_class_reset();
    room_class_set_game(88, "840726");        /* Zork I, authored GN_FANTASY */
    assert(room_class_genre() == GN_FANTASY);
    assert(room_class_genre_locked() == 1);

    room_class_reset();
    room_class_set_game(16, "850603");        /* Seastalker, authored GN_MODERN */
    assert(room_class_genre() == GN_MODERN);

    room_class_reset();
    room_class_set_game(1, "000000");         /* unlisted: unresolved */
    assert(room_class_genre() == 0);
    assert(room_class_genre_locked() == 0);

    printf("test_room_genre ok\n");
    return 0;
}
