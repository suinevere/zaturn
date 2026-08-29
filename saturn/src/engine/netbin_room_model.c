/*----------------------
 | netbin_room_model.c
 | Description: The netbin's room model, which is the honest admission that it
 |   has none. room_model.c decodes the room from a live story image; this build
 |   has no interpreter, the game runs on the multizork server and arrives as
 |   telnet text, so there is no object tree to read and nothing to refresh from.
 |   Every query here reports "unavailable", and the one thing that can be
 |   answered without a running game -- the twelve direction words -- is answered
 |   from a table.
 |
 |   Reporting unavailable is not a degradation, it is what makes the panel
 |   correct here. command_view.cxx branches on room_model_available(): with no
 |   model it sources candidate words from the typeahead trie instead of the
 |   story dictionary, validates picks against the trie, and -- the important one
 |   -- submits "inventory" as a real command instead of opening a local
 |   inventory browser. The server answers all three better than a stale local
 |   snapshot would.
 |
 |   The exits are all twelve open for the reason
 |   docs/superpowers/specs/2026-08-25-netbin-direction-rose-design.md gives for
 |   the keyboard's rose: Infocom games reuse room names with different exits, so
 |   any attempt to recover the room from the server's printed text would draw a
 |   confidently wrong rose. Offering all twelve makes no claim it cannot keep --
 |   the player picks one and the server says "You can't go that way" exactly as
 |   it would for a typed command.
 |
 |   Netbin-only, and it must stay out of the CD build, where it would collide
 |   with room_model.c; makefile:NETBIN_ONLY_SOURCES filters it out of the find
 |   glob and tests/test_netbin_sources.py gates that.
 | Author: suinevere
 | Dependencies: room_model.h (the contract it implements)
 ----------------------*/
#include "room_model.h"

/*----------------------
 | NETBIN_ROOM
 | Description: The snapshot every caller gets: all twelve directions open, no
 |   objects here, nothing carried. `room` is 0, which is no object -- callers
 |   that care gate on room_model_available() first.
 | Author: suinevere
 ----------------------*/
static const RoomModel NETBIN_ROOM = {
    0,
    { RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN,
      RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN,
      RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN, RM_EXIT_OPEN },
    { 0 }, { 0 }, 0, { 0 }, 0
};

/*----------------------
 | NETBIN_DIR_WORD
 | Description: The twelve direction words, in RM_N..RM_OUT order. Hardcoded
 |   rather than recovered from a dictionary because the words the panel submits
 |   go to the server, not to a local parser, and every v3 Infocom game accepts
 |   these spellings.
 | Author: suinevere
 ----------------------*/
static const char *const NETBIN_DIR_WORD[RM_DIR_N] = {
    "north", "east", "west", "south",
    "northeast", "northwest", "southeast", "southwest",
    "up", "down", "in", "out"
};

/*----------------------
 | room_model_get / room_model_available / room_model_dir_word
 | Description: The three queries that mean something without a live story: the
 |   static snapshot, the flat "no model here", and the direction words.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: NETBIN_ROOM, NETBIN_DIR_WORD
 | Params: dir -- RM_N..RM_OUT
 | Returns: the snapshot / 0 / the word, or NULL for a direction out of range
 ----------------------*/
const RoomModel *room_model_get(void) { return &NETBIN_ROOM; }

int room_model_available(void) { return 0; }

const char *room_model_dir_word(int dir) {
    if (dir < 0 || dir >= RM_DIR_N) return 0;
    return NETBIN_DIR_WORD[dir];
}

/*----------------------
 | room_model_bind / room_model_refresh / room_model_refresh_room /
 | room_model_player / room_model_dir_prop / room_model_has_word /
 | room_model_object_word / room_model_full_word / room_model_dict_count /
 | room_model_dict_word
 | Description: The rest of the contract, all answering "nothing here". Bind
 |   fails, so no caller believes a story was loaded; the refreshes do nothing,
 |   there being no interpreter to read; and every lookup misses, which routes
 |   command_view.cxx to the typeahead trie. Present so command_view.cxx links
 |   unchanged rather than growing a second set of NETBIN guards.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: as room_model.h documents; all ignored
 | Returns: 0 / N/A
 ----------------------*/
int room_model_bind(const unsigned char *story, unsigned int len) {
    (void) story; (void) len;
    return 0;
}

void room_model_refresh(void) { }

void room_model_refresh_room(unsigned short room) { (void) room; }

unsigned short room_model_player(void) { return 0; }

int room_model_dir_prop(int dir) { (void) dir; return 0; }

int room_model_has_word(const char *text) { (void) text; return 0; }

int room_model_object_word(unsigned short obj, char *out, int max) {
    (void) obj; (void) out; (void) max;
    return 0;
}

int room_model_full_word(unsigned short obj, const char *word, char *out, int max) {
    (void) obj; (void) word; (void) out; (void) max;
    return 0;
}

int room_model_dict_count(void) { return 0; }

int room_model_dict_word(int index, char *out, int max, unsigned char *flags_out) {
    (void) index; (void) out; (void) max; (void) flags_out;
    return 0;
}
