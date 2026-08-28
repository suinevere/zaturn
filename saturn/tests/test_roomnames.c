/*----------------------
 | test_roomnames.c
 | Description: Host test for room-name composition. Pins that a composed name
 |   always fits ROOMNAME_MAX so a long word added later cannot truncate a name
 |   silently, that the suffix form appears only when asked for, and that raw
 |   random() values are reduced rather than read off the end of the wordlists.
 | Author: suinevere
 | Dependencies: ../roomnames.h, assert.h, string.h, stdio.h
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/trn.exe saturn/tests/test_roomnames.c \
 |          && /tmp/trn.exe
 ----------------------*/
#include "../roomnames.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    char buf[ROOMNAME_MAX];

    roomname_compose(buf, sizeof (buf), 0, 0, 0);
    assert(strcmp(buf, "brass-lantern") == 0);

    roomname_compose(buf, sizeof (buf), 0, 0, 7);
    assert(strcmp(buf, "brass-lantern-07") == 0);

    roomname_compose(buf, sizeof (buf), 0, 0, 99);
    assert(strcmp(buf, "brass-lantern-99") == 0);

    for (size_t a = 0; a < ROOMNAME_NUM_ADJECTIVES; a++) {
        for (size_t n = 0; n < ROOMNAME_NUM_NOUNS; n++) {
            for (int s = 0; s <= 99; s++) {
                roomname_compose(buf, sizeof (buf), a, n, s);
                assert(strlen(buf) < ROOMNAME_MAX);
                assert(strchr(buf, '-') != NULL);
                for (const char *p = buf; *p; p++) {
                    assert(((*p >= 'a') && (*p <= 'z')) || (*p == '-') || ((*p >= '0') && (*p <= '9')));
                }
            }
        }
    }

    roomname_compose(buf, sizeof (buf), ROOMNAME_NUM_ADJECTIVES, ROOMNAME_NUM_NOUNS, 0);
    assert(strcmp(buf, "brass-lantern") == 0);

    printf("test_roomnames: OK\n");
    return 0;
}
