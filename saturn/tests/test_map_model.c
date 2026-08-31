/* Build:
     gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
         saturn/src/engine/map_model.c && /tmp/tmm
   map_model.c is deliberately free of SRL includes so this links on the host. */
#include "../src/engine/map_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The stand-in for room_model.c's decoder, which map_model_rebind_exits reads
   the story's static exits back through. Defined here so this test keeps
   linking map_model.c on its own: 12 leads south to 9 and north to 7, 9 leads
   east to 1, and 15 leads down to 16. */
typedef struct { unsigned short room; int dir; unsigned short dest; } FakeExit;
static const FakeExit FAKE_EXITS[] = {
    { 12, RM_S, 9 }, { 12, RM_N, 7 }, { 9, RM_E, 1 }, { 15, RM_DOWN, 16 }
};
static RoomModel g_fake;

void room_model_refresh_room(unsigned short room) {
    unsigned int i;
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.room = room;
    for (i = 0; i < sizeof FAKE_EXITS / sizeof FAKE_EXITS[0]; i++)
        if (FAKE_EXITS[i].room == room) {
            g_fake.exits[FAKE_EXITS[i].dir] = RM_EXIT_OPEN;
            g_fake.dest[FAKE_EXITS[i].dir]  = FAKE_EXITS[i].dest;
        }
}

const RoomModel *room_model_get(void) { return &g_fake; }

/* A snapshot with one exit wired to a destination, which is all the model
   reads: the room it is in, and where each direction would lead. */
static RoomModel mk(unsigned short room) {
    RoomModel m;
    memset(&m, 0, sizeof m);
    m.room = room;
    return m;
}

static void link1(RoomModel *m, int dir, unsigned short dest) {
    m->exits[dir] = RM_EXIT_OPEN;
    m->dest[dir]  = dest;
}

int main(void) {
    int x = 99, y = 99;

    map_model_reset();

    /* Nothing is known before the first prompt. */
    assert(!map_model_visited(12));
    assert(!map_model_pos(12, &x, &y));

    /* The first room seen is the origin. */
    RoomModel a = mk(12);
    link1(&a, RM_N, 7);
    map_model_enter(&a);
    assert(map_model_visited(12));
    assert(map_model_pos(12, &x, &y) && x == 0 && y == 0);
    assert(map_model_current() == 12);

    /* North of it lands one cell up. */
    RoomModel b = mk(7);
    link1(&b, RM_S, 12);
    map_model_enter(&b);
    assert(map_model_pos(7, &x, &y) && x == 0 && y == -1);
    assert(map_model_current() == 7);

    /* Going back does not move the room that was already placed. */
    map_model_enter(&a);
    assert(map_model_pos(12, &x, &y) && x == 0 && y == 0);
    assert(map_model_current() == 12);

    /* A room reached with no matching dest[] still gets placed, adjacent to
       where the player came from rather than dropped. */
    RoomModel c = mk(40);
    map_model_enter(&c);
    assert(map_model_visited(40));
    assert(map_model_pos(40, &x, &y));

    /* Two rooms wanting one cell: the first keeps it, the second takes the
       first free cell in the fixed order -- north of the target. */
    map_model_reset();

    RoomModel h = mk(1);
    link1(&h, RM_N, 2);
    link1(&h, RM_E, 3);
    map_model_enter(&h);

    RoomModel n1 = mk(2);
    link1(&n1, RM_S, 1);
    map_model_enter(&n1);
    assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);

    map_model_enter(&h);

    /* Room 3 is east of room 1, so it wants (1,0) -- free, no contest. */
    RoomModel e1 = mk(3);
    link1(&e1, RM_W, 1);
    map_model_enter(&e1);
    assert(map_model_pos(3, &x, &y) && x == 1 && y == 0);

    /* Now a genuine contest. Room 5 is UP from room 1, and UP steps the same
       way north does, so it wants (0,-1) -- which room 2 already holds. The
       spiral's first probe is north, so room 5 must land at (0,-2) and room 2
       must not have moved. */
    map_model_reset();
    RoomModel c1 = mk(1);
    link1(&c1, RM_N, 2);
    link1(&c1, RM_UP, 5);
    map_model_enter(&c1);
    assert(map_model_pos(1, &x, &y) && x == 0 && y == 0);

    RoomModel c2 = mk(2);
    link1(&c2, RM_S, 1);
    map_model_enter(&c2);
    assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);

    map_model_enter(&c1);
    RoomModel c5 = mk(5);
    link1(&c5, RM_DOWN, 1);
    map_model_enter(&c5);
    assert(map_model_pos(5, &x, &y) && x == 0 && y == -2);
    assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);

    /* Whatever the arrangement, no two placed rooms may share a cell. */
    {
        int i, j, xi, yi, xj, yj;
        for (i = 0; i < MAP_ROOM_MAX; i++) {
            if (!map_model_visited((unsigned short) i)) continue;
            for (j = i + 1; j < MAP_ROOM_MAX; j++) {
                if (!map_model_visited((unsigned short) j)) continue;
                assert(map_model_pos((unsigned short) i, &xi, &yi));
                assert(map_model_pos((unsigned short) j, &xj, &yj));
                assert(!(xi == xj && yi == yj));
            }
        }
    }

    /* Offsets are measured from the player, which is what keeps the figure
       fixed while the map moves under it. */
    map_model_reset();
    RoomModel o1 = mk(12);
    link1(&o1, RM_N, 7);
    map_model_enter(&o1);
    RoomModel o2 = mk(7);
    link1(&o2, RM_N, 8);
    link1(&o2, RM_S, 12);
    map_model_enter(&o2);
    RoomModel o3 = mk(8);
    link1(&o3, RM_S, 7);
    map_model_enter(&o3);

    /* Standing in 8: itself is the origin, 7 is one south, 12 is two south. */
    assert(map_model_current() == 8);
    assert(map_model_offset(8,  &x, &y) && x == 0 && y == 0);
    assert(map_model_offset(7,  &x, &y) && x == 0 && y == 1);
    assert(map_model_offset(12, &x, &y) && x == 0 && y == 2);

    /* Walking back re-centres on the new room without moving anything. */
    map_model_enter(&o2);
    assert(map_model_offset(7,  &x, &y) && x == 0 && y == 0);
    assert(map_model_offset(8,  &x, &y) && x == 0 && y == -1);

    /* An unplaced room has no offset. */
    assert(!map_model_offset(99, &x, &y));

    /* The placed set is walkable, ascending, and holds exactly the three. */
    assert(map_model_count() == 3);
    {
        unsigned short r = 0;
        assert(map_model_room_at(0, &r) && r == 7);
        assert(map_model_room_at(1, &r) && r == 8);
        assert(map_model_room_at(2, &r) && r == 12);
        assert(!map_model_room_at(3, &r));
    }

    /* A staircase is a link, but not the same kind of link as a road. */
    map_model_reset();
    RoomModel v1 = mk(15);
    link1(&v1, RM_DOWN, 16);
    link1(&v1, RM_N, 14);
    map_model_enter(&v1);
    RoomModel v2 = mk(16);
    link1(&v2, RM_UP, 15);
    map_model_enter(&v2);
    RoomModel v3 = mk(14);
    link1(&v3, RM_S, 15);
    map_model_enter(&v3);

    assert(map_model_link(15, 16) == MAP_LINK_VERT);
    assert(map_model_link(16, 15) == MAP_LINK_VERT);
    assert(map_model_link(15, 14) == MAP_LINK_FLAT);
    assert(map_model_link(14, 16) == MAP_LINK_NONE);
    assert(map_model_link(15, 99) == MAP_LINK_NONE);

    /* Room ids from the two captured walks in docs/ZORK1_MAP_RECON.md: 12 West
       of House, 9 South of House, 1 Behind House, 7 North of House, 8 Forest
       Path, 15 Living Room, 16 Cellar, 17 East of Chasm, 18 Gallery,
       19 Studio. These are the Japanese release's own 0-109 room indices, NOT
       Z-machine object numbers -- West of House is object 180. The model keys
       on object numbers in the game, but the shape of the walk is what this
       fixture is for, and these are the ids the recon actually recorded. */
    {
        int ax, ay, bx, by;

        map_model_reset();
        RoomModel woh = mk(12); link1(&woh, RM_S, 9); link1(&woh, RM_N, 7);
        RoomModel soh = mk(9);  link1(&soh, RM_E, 1); link1(&soh, RM_W, 12);
        RoomModel bh  = mk(1);  link1(&bh,  RM_N, 7); link1(&bh, RM_S, 9);
        RoomModel noh = mk(7);  link1(&noh, RM_E, 1); link1(&noh, RM_S, 12);

        /* Reach Behind House the first way: south from the front of the house,
           then east. */
        map_model_enter(&woh);
        map_model_enter(&soh);
        map_model_enter(&bh);
        assert(map_model_pos(1, &ax, &ay));

        /* Now walk out north and come back east into the same room. The
           original recomputed its layout on every open and moved Behind House
           when the route changed; this must not. */
        map_model_enter(&noh);
        map_model_enter(&bh);
        assert(map_model_pos(1, &bx, &by));
        assert(ax == bx && ay == by);

        /* And the room it was first placed against has not moved either. */
        assert(map_model_pos(9, &x, &y) && x == 0 && y == 1);
    }

    /* The underground walk lays out without any room landing on another:
       Living Room -> down -> Cellar -> south -> East of Chasm -> east ->
       Gallery -> north -> Studio. */
    {
        int i, j, xi, yi, xj, yj;
        map_model_reset();
        RoomModel lr = mk(15); link1(&lr, RM_DOWN, 16);
        RoomModel ce = mk(16); link1(&ce, RM_UP, 15); link1(&ce, RM_S, 17);
        RoomModel ec = mk(17); link1(&ec, RM_N, 16);  link1(&ec, RM_E, 18);
        RoomModel ga = mk(18); link1(&ga, RM_W, 17);  link1(&ga, RM_N, 19);
        RoomModel st = mk(19); link1(&st, RM_S, 18);
        map_model_enter(&lr);
        map_model_enter(&ce);
        map_model_enter(&ec);
        map_model_enter(&ga);
        map_model_enter(&st);

        assert(map_model_count() == 5);
        assert(map_model_link(15, 16) == MAP_LINK_VERT);
        assert(map_model_link(17, 18) == MAP_LINK_FLAT);

        for (i = 0; i < MAP_ROOM_MAX; i++) {
            if (!map_model_visited((unsigned short) i)) continue;
            for (j = i + 1; j < MAP_ROOM_MAX; j++) {
                if (!map_model_visited((unsigned short) j)) continue;
                assert(map_model_pos((unsigned short) i, &xi, &yi));
                assert(map_model_pos((unsigned short) j, &xj, &yj));
                assert(!(xi == xj && yi == yj));
            }
        }
    }

    /* A saved map restores to the same picture, and a truncated or foreign
       blob is refused rather than half-loaded. */
    {
        unsigned char blob[2048];
        unsigned int len;
        int px, py;

        map_model_reset();
        RoomModel s1 = mk(12); link1(&s1, RM_N, 7);
        RoomModel s2 = mk(7);  link1(&s2, RM_S, 12);
        map_model_enter(&s1);
        map_model_enter(&s2);
        assert(map_model_pos(7, &px, &py) && px == 0 && py == -1);

        len = map_model_serialize(blob, sizeof blob);
        assert(len > 0);

        map_model_reset();
        assert(!map_model_visited(7));
        assert(map_model_deserialize(blob, len));
        assert(map_model_visited(7) && map_model_visited(12));
        assert(map_model_pos(7, &px, &py) && px == 0 && py == -1);
        assert(map_model_current() == 7);

        /* Refused, and the model left empty rather than half-filled. */
        map_model_reset();
        assert(!map_model_deserialize(blob, len - 1));
        assert(map_model_count() == 0);
        blob[0] = 0xEE;
        assert(!map_model_deserialize(blob, len));
        assert(map_model_count() == 0);

        /* Too small a buffer is refused rather than overrun. */
        assert(map_model_serialize(blob, 3) == 0);

        /* A blob whose current room is not in its own placed set would leave
           map_model_offset answering 0 for everything and the view painting
           bare ground, so it is refused too. */
        blob[0] = (unsigned char) MAP_BLOB_MAGIC;
        blob[2] = 0; blob[3] = 99;
        assert(!map_model_deserialize(blob, len));
        assert(map_model_count() == 0);
    }

    /* The blob carries positions and nothing else, so a restored map has no
       links at all until the exits are re-derived from the story -- and the
       links ARE the trail. */
    {
        unsigned char blob[MAP_BLOB_MAX];
        unsigned int len;

        map_model_reset();
        RoomModel r12 = mk(12); link1(&r12, RM_S, 9);
        RoomModel r9  = mk(9);  link1(&r9,  RM_E, 1);
        map_model_enter(&r12);
        map_model_enter(&r9);
        assert(map_model_link(12, 9) == MAP_LINK_FLAT);

        len = map_model_serialize(blob, sizeof blob);
        assert(len > 0);
        assert(map_model_deserialize(blob, len));
        assert(map_model_visited(12) && map_model_visited(9));
        assert(map_model_link(12, 9) == MAP_LINK_NONE);

        map_model_rebind_exits();
        assert(map_model_link(12, 9) == MAP_LINK_FLAT);
        assert(map_model_link(9, 12) == MAP_LINK_FLAT);

        /* Only placed rooms link: 7 and 1 are in the story exits the stub
           reports but were never in the blob. */
        assert(map_model_link(12, 7) == MAP_LINK_NONE);
        assert(map_model_link(9, 1) == MAP_LINK_NONE);
    }

    printf("test_map_model: ok\n");
    return 0;
}
