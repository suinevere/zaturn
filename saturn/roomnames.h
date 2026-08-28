/*----------------------
 | roomnames.h
 | Description: Wordlists and the composer that names a multizork room. Names are
 |   an adjective-noun pair so a player can read one aloud to a friend, with an
 |   optional two-digit suffix the generator falls back on once plain pairs start
 |   colliding.
 | Author: suinevere
 | Dependencies: stdio.h, stddef.h
 | Globals: roomname_adjectives, roomname_nouns
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#ifndef ROOMNAMES_H
#define ROOMNAMES_H

#include <stdio.h>
#include <stddef.h>

/*----------------------
 | ROOMNAME_MAX
 | Description: Bytes a composed room name needs including its terminator: two
 |   nine-character words, a joining hyphen, a suffix hyphen and two digits.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_MAX 24

/*----------------------
 | ROOMNAME_WORD_MAX
 | Description: Bytes per wordlist entry. Nine characters and a terminator; a
 |   longer word fails to compile rather than truncating a name at runtime.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_WORD_MAX 10

/*----------------------
 | roomname_adjectives
 | Description: The first half of a room name.
 | Author: suinevere
 ----------------------*/
static const char roomname_adjectives[][ROOMNAME_WORD_MAX] = {
    "brass", "rusty", "dented", "hollow", "ancient", "crooked",
    "silver", "wooden", "iron", "gilded", "mossy", "frozen",
    "hidden", "narrow", "quiet", "sunken", "dusty", "bitter",
    "coiled", "drifting", "echoing", "faded", "gloomy", "granite",
    "humming", "jagged", "lonely", "molten", "murky", "oaken",
    "pale", "quartz", "ragged", "restless", "salted", "scarlet",
    "shallow", "slanted", "sodden", "tangled", "velvet", "windswept",
    "amber", "cracked", "hallowed", "leaden", "painted", "twisted"
};

/*----------------------
 | roomname_nouns
 | Description: The second half of a room name.
 | Author: suinevere
 ----------------------*/
static const char roomname_nouns[][ROOMNAME_WORD_MAX] = {
    "lantern", "mailbox", "coffin", "sword", "grating", "troll",
    "cellar", "attic", "chimney", "canyon", "river", "chasm",
    "lamp", "rope", "skeleton", "kitchen", "gallery", "painting",
    "basket", "boat", "rug", "window", "forest", "clearing",
    "maze", "vault", "dam", "reservoir", "temple", "altar",
    "bell", "candle", "book", "mirror", "tunnel", "bridge",
    "volcano", "balloon", "crystal", "sphere", "trident", "scarab",
    "jewel", "chalice", "bracelet", "pot", "timber", "barrow"
};

/*----------------------
 | ROOMNAME_NUM_ADJECTIVES
 | Description: Entries in roomname_adjectives.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_NUM_ADJECTIVES (sizeof (roomname_adjectives) / ROOMNAME_WORD_MAX)

/*----------------------
 | ROOMNAME_NUM_NOUNS
 | Description: Entries in roomname_nouns.
 | Author: suinevere
 ----------------------*/
#define ROOMNAME_NUM_NOUNS (sizeof (roomname_nouns) / ROOMNAME_WORD_MAX)

/*----------------------
 | roomname_compose
 | Description: Writes one room name, reducing the word indices so a caller can
 |   hand over raw random() output.
 | Author: suinevere
 | Dependencies: stdio.h
 | Globals: roomname_adjectives, roomname_nouns
 | Params: out -- destination, at least ROOMNAME_MAX bytes
 |   outlen -- bytes available at out
 |   adj -- adjective index, reduced modulo the list length
 |   noun -- noun index, reduced modulo the list length
 |   suffix -- 0 for a plain pair, 1 to 99 for a numbered one
 | Returns: N/A
 ----------------------*/
static void roomname_compose(char *out, const size_t outlen, const size_t adj, const size_t noun, const int suffix)
{
    const char *a = roomname_adjectives[adj % ROOMNAME_NUM_ADJECTIVES];
    const char *n = roomname_nouns[noun % ROOMNAME_NUM_NOUNS];
    if ((suffix > 0) && (suffix < 100)) {
        snprintf(out, outlen, "%s-%s-%02d", a, n, suffix);
    } else {
        snprintf(out, outlen, "%s-%s", a, n);
    }
}

#endif
