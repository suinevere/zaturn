/* Host tests for the engine's text-category announcement, the 1.5s settle, and
   the fade phases that bracket a Dynamic commit.

   The category is what selects both the CD-DA track and the background picture,
   so these tests are really about one question: does the engine tell its
   subscriber the right thing at the right moment, and exactly once?

   gcc -O2 -I saturn/src -o /tmp/mct test/music_category_test.c \
       saturn/src/sound/music.c saturn/src/sound/music_data.c && /tmp/mct */
#include <stdio.h>
#include "sound/music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

static int g_cats[32], g_ncat;
static void rec_cat(int c) { if (g_ncat < 32) g_cats[g_ncat++] = c; }

static int g_lv[512], g_nlv;
static void rec_lv(int l) { if (g_nlv < 512) g_lv[g_nlv++] = l; }
/* -1 when nothing has been emitted, so a failing assertion above does not turn
   the next one into an out-of-bounds read. */
static int last_lv(void) { return g_nlv ? g_lv[g_nlv - 1] : -1; }

static int g_rots[32], g_nrot;
static void rec_rot(int c) { if (g_nrot < 32) g_rots[g_nrot++] = c; }

static int g_last_track;
static void play(int t, int loop) { (void) loop; if (t) g_last_track = t; }
static int  isplaying(void) { return 1; }

int main(void) {
    int fails = 0;
    int i;

    music_set_backend(play);
    music_set_isplaying(isplaying);
    music_set_category_fn(rec_cat);
    music_set_game(0, "000000");

    /* These pools must be non-empty, or the engine takes the "nothing playing
       yet" branch every turn and no debounce is ever armed. */
    CHECK(music_category_track(TC_UNDERGROUND) != 0);
    CHECK(music_category_track(TC_WILDERNESS)  != 0);

    /* ---- the default settle is 90 frames (1.5s @ 60Hz) ----
       Runs FIRST, before any music_set_debounce_frames call, because that
       override is sticky across music_reset(). */
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(1);                       /* NEUTRAL, plays at once */
    g_ncat = 0;
    music_note_output("A damp cave with a tunnel.", 26);
    music_on_turn(2);                       /* UNDERGROUND, armed */
    for (i = 0; i < 89; i++) music_tick();
    CHECK(g_ncat == 0);                     /* not yet */
    music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);

    music_set_debounce_frames(3);           /* short settle for the rest */

    /* ---- reset announces the neutral default ---- */
    g_ncat = 0;
    music_reset();
    CHECK(g_ncat == 1 && g_cats[0] == TC_NEUTRAL);

    /* ---- the first room of a session commits immediately ---- */
    g_ncat = 0;
    music_note_output("You are in a dark cave with a tunnel.", 37);
    music_on_turn(10);
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);

    /* ---- a later room is debounced ---- */
    g_ncat = 0;
    music_note_output("A forest clearing among tall trees.", 35);
    music_on_turn(11);
    CHECK(g_ncat == 0);
    music_tick(); music_tick();
    CHECK(g_ncat == 0);
    music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_WILDERNESS);

    /* ---- standing still announces nothing more ---- */
    g_ncat = 0;
    music_note_output("The forest is quiet.", 20);
    music_on_turn(11);
    music_tick(); music_tick(); music_tick(); music_tick();
    CHECK(g_ncat == 0);

    /* ---- same category, NEW room: the settle restarts ----
       The rule is "stopped in one room for 1.5s", not "1.5s since the mood first
       changed" -- walking a corridor of same-mood rooms must not commit shortly
       after arriving in one of them. */
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(20);                      /* NEUTRAL now sounding */
    g_ncat = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(21);                      /* UNDERGROUND armed, 3 frames */
    music_tick(); music_tick();             /* 2 of 3 elapsed */
    music_note_output("A dark tunnel.", 14);
    music_on_turn(22);                      /* same category, new room */
    music_tick();
    CHECK(g_ncat == 0);                     /* would have fired without the restart */
    music_tick(); music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);

    /* ---- fade off (the default) is the old instant commit ---- */
    music_set_fade_fn(rec_lv);
    music_set_fade_frames(0);
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(30);
    g_ncat = 0; g_nlv = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(31);
    music_tick(); music_tick(); music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);
    CHECK(g_nlv == 0);                          /* no fade callback at all */

    /* ---- with a fade, the commit lands at the bottom, exactly once ---- */
    music_set_fade_frames(4);
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(40);
    g_ncat = 0; g_nlv = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(41);
    music_tick(); music_tick(); music_tick();   /* settle elapses, fade begins */
    CHECK(g_ncat == 0);                         /* not committed yet: fading out */
    CHECK(g_nlv > 0);                           /* ramp has started */
    CHECK(last_lv() < 255);               /* and it is heading down */
    for (i = 0; i < 4 && g_ncat == 0; i++) music_tick();
    CHECK(g_ncat == 1 && g_cats[0] == TC_UNDERGROUND);
    CHECK(last_lv() == 0);                /* committed at the bottom */
    for (i = 0; i < 8; i++) music_tick();
    CHECK(last_lv() == 255);              /* and came back up */
    CHECK(g_ncat == 1);                         /* still exactly one commit */

    /* ---- a reset mid-fade restores full brightness ----
       Without this the screen would be left dim with nothing due to raise it. */
    music_reset();
    music_note_output("An empty stone landing.", 23);
    music_on_turn(50);
    music_note_output("A damp cave.", 12);
    music_on_turn(51);
    music_tick(); music_tick(); music_tick(); music_tick();
    g_nlv = 0;
    music_reset();
    CHECK(last_lv() == 255);

    music_set_fade_frames(0);   /* leave the engine as we found it */

    /* ---- three rooms of one mood rotate within the category ---- */
    music_set_rotate_fn(rec_rot);
    music_reset();
    g_nrot = 0; g_ncat = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(60);                      /* UNDERGROUND, sounds at once */
    CHECK(g_ncat == 1);
    {
        int first_track = g_last_track;
        CHECK(first_track != 0);

        /* Two more underground rooms: still the same mood, nothing yet. */
        g_ncat = 0; g_nrot = 0;
        music_note_output("A dark tunnel.", 14);
        music_on_turn(61);
        music_tick(); music_tick(); music_tick(); music_tick();
        CHECK(g_nrot == 0 && g_ncat == 0);

        music_note_output("A damp cavern.", 14);
        music_on_turn(62);
        music_tick(); music_tick(); music_tick(); music_tick();
        CHECK(g_nrot == 0 && g_ncat == 0);

        /* The third arms the rotation; it commits after the same settle. */
        music_note_output("An underground passage.", 23);
        music_on_turn(63);
        CHECK(g_nrot == 0);
        music_tick(); music_tick(); music_tick();
        CHECK(g_nrot == 1 && g_rots[0] == TC_UNDERGROUND);
        CHECK(g_ncat == 0);                 /* a rotation is NOT a category change */
        CHECK(g_last_track != first_track); /* and it does not re-pick the same one */
    }

    /* ---- the counter restarts after a rotation ---- */
    g_nrot = 0;
    music_note_output("A dark tunnel.", 14);
    music_on_turn(64);
    music_tick(); music_tick(); music_tick(); music_tick();
    CHECK(g_nrot == 0);                     /* one room in, not three */

    /* ---- a real mood change supersedes a pending rotation ---- */
    music_reset();
    g_nrot = 0; g_ncat = 0;
    music_note_output("A damp cave.", 12);
    music_on_turn(70);
    g_ncat = 0;
    music_note_output("A dark tunnel.", 14);   music_on_turn(71);
    music_note_output("A damp cavern.", 14);   music_on_turn(72);
    music_note_output("An underground passage.", 23);
    music_on_turn(73);                      /* rotation armed, settling */
    music_note_output("A forest clearing among tall trees.", 35);
    music_on_turn(74);                      /* mood actually changes */
    music_tick(); music_tick(); music_tick();
    CHECK(g_nrot == 0);                     /* the rotation never fired... */
    CHECK(g_ncat == 1 && g_cats[0] == TC_WILDERNESS);   /* ...the change did */

    /* ---- the room title outweighs the scenery around it ----
       Zork I's house rooms are the case this exists for. Their descriptions talk
       about a field, a path, trees and a forest, so a flat keyword count put three
       of these four in the woods and broke the fourth ("West of House", 1-1) on
       enum order. The title is what the room IS, so it is worth more: see the
       TEXT_TITLE_WEIGHT box in music.c.

       These are the real texts from the game, not paraphrases -- the whole point
       is the ratio of scenery words to title words that Infocom actually wrote. */
    CHECK(text_classify_room(
        "West of House\n"
        "You are standing in an open field west of a white house, with a boarded\n"
        "front door.\nThere is a small mailbox here.") == TC_HOUSE);
    CHECK(text_classify_room(
        "North of House\n"
        "You are facing the north side of a white house. There is no door here,\n"
        "and all the windows are boarded up. To the north a narrow path winds\n"
        "through the trees.") == TC_HOUSE);
    CHECK(text_classify_room(
        "Behind House\n"
        "You are behind the white house. A path leads into the forest to the east.\n"
        "In one corner of the house there is a small window which is slightly ajar.")
        == TC_HOUSE);
    CHECK(text_classify_room(
        "Kitchen\n"
        "You are in the kitchen of the white house. A table seems to have been used\n"
        "recently for the preparation of food. A passage leads to the west and a dark\n"
        "staircase can be seen leading upward.") == TC_HOUSE);

    /* A room that never mentions anything keeps the default, which is the same
       category -- so an unclassified Zork interior and a named one agree rather
       than flicking the wallpaper between two moods on the way through a door. */
    CHECK(text_classify_room(
        "Living Room\n"
        "You are in the living room. There is a doorway to the east, a wooden door\n"
        "with strange gothic lettering to the west, and a large oriental rug.")
        == TC_NEUTRAL);

    /* The weight is 2, not "the title decides": two agreeing description words
       still beat a title that only names a direction. */
    CHECK(text_classify_room(
        "Forest\nThis is a forest, with trees in all directions.") == TC_WILDERNESS);
    CHECK(text_classify_room(
        "Cellar\nYou are in a dark and damp cellar with a narrow passageway leading\n"
        "north, and a crawlway to the south.") == TC_UNDERGROUND);

    /* A title with no keyword in it at all falls back to the description alone. */
    CHECK(text_classify_room(
        "In the Clearing\nYou are in a clearing, with a forest surrounding you on\n"
        "all sides. A path leads south.") == TC_WILDERNESS);

    /* Degenerate inputs must not walk off the front of the buffer looking for a
       title. */
    CHECK(text_classify_room("") == TC_NEUTRAL);
    CHECK(text_classify_room("\n\n\n") == TC_NEUTRAL);

    /* A title longer than TEXT_TITLE_MAX is cut, and a keyword past the cut gets
       no title bonus -- it still counts, but only as ordinary description text.
       Here "Forest" sits past character 64 and so merely ties with "cave" rather
       than winning outright, and the tie falls to whichever category is earlier in
       the enum. Pinned because it is the visible edge of the truncation: no room
       title in these games is anywhere near this long, and if one ever is, this is
       what it will do rather than crash or read past the buffer. */
    CHECK(text_classify_room(
        "A Room With A Very Long Name Indeed That Runs Well Past Any Sensible "
        "Title Buffer And Then Says Forest\nA cave.") == TC_WILDERNESS);
    /* The same keyword inside the cut does win, which is what makes the case
       above about the truncation rather than about the keyword. */
    CHECK(text_classify_room("Forest Path\nA cave.") == TC_WILDERNESS);

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
