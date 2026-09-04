/* Build:
     gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
         saturn/src/engine/map_model.c saturn/src/engine/map_atlas.c \
         saturn/src/engine/map_marks.c && /tmp/tmm
   map_model.c is deliberately free of SRL includes so this links on the host. */
#include "../src/engine/map_model.h"
#include "../src/engine/map_atlas.h"
#include "../src/engine/map_marks.h"
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

/* link1 always opens an exit. Three of the four exit states matter to the
   flags map_model_exits derives, so this is the general form. */
static void link_kind(RoomModel *m, int dir, unsigned short dest, int state) {
    m->exits[dir] = (unsigned char) state;
    m->dest[dir]  = dest;
}

/* Chebyshev distance, which is the metric the placement search works in: the
   ring at radius r is every cell whose distance is exactly r. */
static int chebyshev(int dx, int dy) {
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
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

    /* UP must not contest north. Room 5 is UP from room 1 and room 2 is north
       of it; UP steps two, so room 5 takes (0,-2) outright and nothing is
       displaced. This used to be the collision fixture, because UP stepped one
       cell and so wanted the very cell north had already taken. */
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

    /* DOWN, IN and OUT are on distinct axes from each other and from every
       compass direction, so a room with all four plus north and east places
       six neighbours without one contest. */
    {
        int dx, dy;
        map_model_reset();
        RoomModel hub = mk(1);
        link1(&hub, RM_N, 2); link1(&hub, RM_E, 3);
        link1(&hub, RM_UP, 4); link1(&hub, RM_DOWN, 5);
        link1(&hub, RM_IN, 6); link1(&hub, RM_OUT, 7);
        map_model_enter(&hub);
        {
            static const struct { unsigned short room; int x, y; } WANT[] = {
                { 2, 0,-1 }, { 3, 1, 0 }, { 4, 0,-2 },
                { 5, 0, 2 }, { 6, 2, 0 }, { 7,-2, 0 }
            };
            unsigned int k;
            for (k = 0; k < sizeof WANT / sizeof WANT[0]; k++) {
                RoomModel nb = mk(WANT[k].room);
                map_model_enter(&hub);
                map_model_enter(&nb);
                assert(map_model_pos(WANT[k].room, &dx, &dy));
                assert(dx == WANT[k].x && dy == WANT[k].y);
            }
        }
        assert(map_model_count() == 7);
    }

    /* A genuine contest settles on the nearest ring, not on a ray. Room 5
       arrives east out of room 4 and wants (0,-1), which room 2 holds; the
       ring at radius one is otherwise free, so it must land one step from its
       target -- and so still within the one grid step the view will draw a
       link across. A room flung further than that is drawn floating. */
    {
        int ax, ay;
        map_model_reset();
        RoomModel h1 = mk(1); link1(&h1, RM_N, 2); link1(&h1, RM_W, 3);
        RoomModel h2 = mk(2); link1(&h2, RM_S, 1);
        RoomModel h3 = mk(3); link1(&h3, RM_E, 1); link1(&h3, RM_N, 4);
        RoomModel h4 = mk(4); link1(&h4, RM_S, 3); link1(&h4, RM_E, 5);
        RoomModel h5 = mk(5); link1(&h5, RM_W, 4);
        map_model_enter(&h1);
        map_model_enter(&h2);
        map_model_enter(&h1);
        map_model_enter(&h3);
        map_model_enter(&h4);
        map_model_enter(&h5);

        assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);
        assert(map_model_pos(4, &x, &y) && x == -1 && y == -1);
        assert(map_model_pos(5, &ax, &ay));
        assert(!(ax == 0 && ay == -1));
        assert(chebyshev(ax - 0, ay - (-1)) == 1);
        assert(chebyshev(ax - (-1), ay - (-1)) <= 1);
    }

    /* With the target and its whole radius-one ring taken, the search steps out
       to radius two and stops there. The hub holds (0,0) and rooms 2-9 hold all
       eight cells around it; room 22 then walks south out of room 2 and so wants
       (0,0) itself. Room 10 sits UP from the hub at (0,-2), which takes the
       first cell of the radius-two ring as well, so the search must go on to
       the second -- (1,-2), one step clockwise.

       That cell is the whole point of the fix. A ray search probes only eight
       of the sixteen cells at radius two, and (1,-2) is not among them: with
       (0,-2) taken it would skip past to (2,0), twice as far from room 2 as it
       needed to go. The exact coordinate is asserted rather than the radius,
       because both answers sit at radius two and only the coordinate tells
       rings from rays. */
    {
        static const int DIR8[8] = { RM_N, RM_E, RM_W, RM_S,
                                     RM_NE, RM_NW, RM_SE, RM_SW };
        int ax, ay, k;
        map_model_reset();
        RoomModel hub = mk(1);
        for (k = 0; k < 8; k++) link1(&hub, DIR8[k], (unsigned short) (2 + k));
        link1(&hub, RM_UP, 10);
        map_model_enter(&hub);
        for (k = 0; k < 8; k++) {
            RoomModel nb = mk((unsigned short) (2 + k));
            map_model_enter(&hub);
            map_model_enter(&nb);
        }
        {
            RoomModel above = mk(10);
            map_model_enter(&hub);
            map_model_enter(&above);
        }
        assert(map_model_count() == 10);
        for (k = 0; k < 8; k++) {
            assert(map_model_pos((unsigned short) (2 + k), &x, &y));
            assert(chebyshev(x, y) == 1);
        }
        assert(map_model_pos(10, &x, &y) && x == 0 && y == -2);

        map_model_enter(&hub);
        {
            RoomModel n2 = mk(2);
            RoomModel deep = mk(22);
            link1(&n2, RM_S, 22);
            map_model_enter(&n2);
            map_model_enter(&deep);
            assert(map_model_pos(22, &ax, &ay));
            assert(chebyshev(ax, ay) == 2);
            assert(ax == 1 && ay == -2);
        }

        /* And nothing it displaced past moved to make room for it. */
        assert(map_model_pos(1, &x, &y) && x == 0 && y == 0);
        assert(map_model_pos(2, &x, &y) && x == 0 && y == -1);
    }

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

    /* The first move after a restore has no previous snapshot to read the
       direction forwards out of, so it is read backwards out of the room
       arrived in. Room 1 is entered from room 9 going east and carries a west
       exit back to it, so it must land east of room 9 -- not due south of it,
       which is where an uninferred move used to put everything. */
    {
        unsigned char blob[MAP_BLOB_MAX];
        unsigned int len;

        map_model_reset();
        RoomModel r12 = mk(12); link1(&r12, RM_S, 9);
        RoomModel r9  = mk(9);  link1(&r9,  RM_E, 1);
        map_model_enter(&r12);
        map_model_enter(&r9);
        assert(map_model_pos(9, &x, &y) && x == 0 && y == 1);

        len = map_model_serialize(blob, sizeof blob);
        assert(len > 0);
        map_model_reset();
        assert(map_model_deserialize(blob, len));
        map_model_rebind_exits();
        assert(map_model_current() == 9);

        {
            RoomModel r1 = mk(1);
            link1(&r1, RM_W, 9);
            map_model_enter(&r1);
            assert(map_model_pos(1, &x, &y));
            assert(x == 1 && y == 1);
        }
    }

    /* Reading backwards only works when the arrival has a way back. A room
       that does not is still placed due south, which is the one case the
       original fault survives in, and it is asserted so a future change to the
       fallback is a deliberate one. */
    {
        unsigned char blob[MAP_BLOB_MAX];
        unsigned int len;

        map_model_reset();
        RoomModel r12 = mk(12); link1(&r12, RM_S, 9);
        RoomModel r9  = mk(9);  link1(&r9,  RM_E, 1);
        map_model_enter(&r12);
        map_model_enter(&r9);
        len = map_model_serialize(blob, sizeof blob);
        map_model_reset();
        assert(map_model_deserialize(blob, len));
        map_model_rebind_exits();

        {
            RoomModel oneway = mk(30);
            map_model_enter(&oneway);
            assert(map_model_pos(30, &x, &y));
            assert(x == 0 && y == 2);
        }
    }

    /* With an atlas bound, an authored room goes where the drawing puts it and
       the walk is not consulted. West of House, North of House, South of House
       and Behind House must come out in the arrangement Infocom drew -- which is
       the whole point, since walking Zork's exits cannot produce it. */
    {
        unsigned char hdr[0x18];
        int wx, wy, nx, ny, sx, sy;
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        /* The count tracks a generated table and moves when the maps are
           re-read; what this test actually pins is the geometry below. */
        assert(map_atlas_bind(hdr, sizeof hdr) > 0);

        map_model_reset();
        {
            RoomModel woh = mk(180); link1(&woh, RM_N, 81); link1(&woh, RM_S, 80);
            RoomModel noh = mk(81);  link1(&noh, RM_S, 180); link1(&noh, RM_E, 79);
            RoomModel soh = mk(80);  link1(&soh, RM_N, 180);
            map_model_enter(&woh);
            map_model_enter(&noh);
            map_model_enter(&woh);
            map_model_enter(&soh);
        }
        assert(map_model_pos(180, &wx, &wy));
        assert(map_model_pos(81, &nx, &ny));
        assert(map_model_pos(80, &sx, &sy));

        /* The exact drawn offsets, not merely the right side of the house. The
           walk would put North of House one cell straight up; Infocom draws it
           two up and one across, and only the atlas produces that. A weaker
           assertion here passes under either rule and pins nothing. */
        assert(nx == wx + 1 && ny == wy - 2);
        assert(sx == wx + 1 && sy == wy + 2);
    }

    /* Walking off the edge of the drawn region falls back to the step rule, and
       the room lands one cell from where it was reached from -- close enough
       that the view still draws the link onward. Object 250 is not in the
       table. */
    {
        int ax, ay, bx, by;
        map_model_reset();
        {
            RoomModel woh = mk(180); link1(&woh, RM_N, 250);
            RoomModel off = mk(250); link1(&off, RM_S, 180);
            map_model_enter(&woh);
            map_model_enter(&off);
        }
        assert(map_model_pos(180, &ax, &ay));
        assert(map_model_pos(250, &bx, &by));
        assert(bx == ax && by == ay - 1);
        assert(map_model_link(180, 250) == MAP_LINK_FLAT);
    }

    /* Entering the drawn region from outside it reconciles the two coordinate
       systems: the first authored room lands exactly where the walk would have
       put it, and everything authored afterwards keeps its position relative to
       that one. Without the reconciliation the atlas's absolute cells would sit
       an arbitrary distance from the walked ones. */
    {
        int ox, oy, wx, wy, nx, ny;
        map_model_reset();
        {
            RoomModel off = mk(250); link1(&off, RM_S, 180);
            RoomModel woh = mk(180); link1(&woh, RM_N, 250); link1(&woh, RM_N, 81);
            RoomModel noh = mk(81);  link1(&noh, RM_S, 180);
            map_model_enter(&off);
            map_model_enter(&woh);
            map_model_enter(&noh);
        }
        assert(map_model_pos(250, &ox, &oy));
        assert(map_model_pos(180, &wx, &wy));
        assert(map_model_pos(81, &nx, &ny));
        assert(wx == ox && wy == oy + 1);
        assert(nx == wx + 1 && ny == wy - 2);
    }

    /* Unbinding restores the walk exactly, so a story nobody drew is unaffected
       by the existence of the table. */
    {
        int ax, ay;
        assert(map_atlas_bind(0, 0) == 0);
        map_model_reset();
        {
            RoomModel woh = mk(180); link1(&woh, RM_N, 81);
            RoomModel noh = mk(81);  link1(&noh, RM_S, 180);
            map_model_enter(&woh);
            map_model_enter(&noh);
        }
        assert(map_model_pos(180, &x, &y) && x == 0 && y == 0);
        assert(map_model_pos(81, &ax, &ay) && ax == 0 && ay == -1);
    }

    /* Easy's reveal is a loan, not a gift. Every room it places is taken back by
       clear_reveal, and only the walked rooms were ever in the save -- otherwise
       one open of the map on Easy would put the whole drawing on the Medium map
       for the rest of the session and into every save after it. A room walked
       into while revealed stops being a loan and keeps its cell. */
    {
        unsigned char hdr[0x18];
        unsigned char blob[MAP_BLOB_MAX];
        unsigned int len;
        int revealed, kx, ky;
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        assert(map_atlas_bind(hdr, sizeof hdr) > 0);

        map_model_reset();
        {
            RoomModel woh = mk(180); link1(&woh, RM_N, 81);
            map_model_enter(&woh);
        }
        assert(map_model_count() == 1);
        assert(!map_model_visited(81));

        revealed = map_model_reveal_atlas();
        assert(revealed > 0);
        assert(map_model_count() == revealed + 1);
        assert(map_model_visited(81));

        /* Four header bytes and six per room, for the one room walked into --
           not for the revealed ones sharing the map with it. */
        len = map_model_serialize(blob, sizeof blob);
        assert(len == 4u + 6u);

        {
            RoomModel noh = mk(81); link1(&noh, RM_S, 180);
            map_model_enter(&noh);
        }
        assert(map_model_pos(81, &kx, &ky));

        assert(map_model_clear_reveal() == revealed - 1);
        assert(map_model_count() == 2);
        assert(map_model_visited(180) && map_model_visited(81));
        assert(map_model_pos(81, &x, &y) && x == kx && y == ky);
        assert(map_model_clear_reveal() == 0);

        len = map_model_serialize(blob, sizeof blob);
        assert(len == 4u + 12u);
    }

    /* Floors. Zork I is drawn on three sheets and the map shows one at a time,
       so every room has to name one -- including the rooms the atlas does not
       cover, which are the fifteen Maze rooms and are reached only from the
       dungeon. A bare zero for those would strand them on the surface among the
       forests, so the model answers with the floor whose box they were placed
       nearest, which is the one the player walked in from.

       Object numbers here are Zork I release 88's own: 180 West of House and
       193 Living Room are drawn on the first sheet, 72 Cellar on the second,
       and 73 is not in the table at all -- it stands in for a Maze room. */
    {
        unsigned char hdr[0x18];
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        assert(map_atlas_bind(hdr, sizeof hdr) > 0);
        /* Nine, not the three sheets it is drawn on: a floor is one vertical
           step of the story's own routes inside one sheet, so the coal mine's
           levels and the temple above the dungeon are floors of their own. */
        assert(map_model_pages() == 9);

        map_model_reset();
        {
            RoomModel lr = mk(193); link1(&lr, RM_DOWN, 72);
            RoomModel ce = mk(72);  link1(&ce, RM_UP, 193); link1(&ce, RM_S, 73);
            RoomModel mz = mk(73);  link1(&mz, RM_N, 72);
            map_model_enter(&lr);
            map_model_enter(&ce);
            map_model_enter(&mz);
        }

        /* The two authored rooms take the floor the story's routes put them
           on, and they are not the same floor -- a stairway down is a floor
           change, which is the whole reason paging exists. The Living Room is
           above ground and the Cellar is in the dungeon, five vertical steps
           below it; both used to answer 0 and 1, which were the sheets they
           were drawn on. */
        assert(map_model_page(193) == 0);
        assert(map_model_page(72) == 5);
        assert(map_model_page(193) != map_model_page(72));

        /* And the unmapped room follows the room it can reach without changing
           floor rather than falling to zero. Object 73 stands in for a Maze
           room, whose only exit here is north into the Cellar, so
           page_via_routes walks that one level exit and takes the Cellar's
           floor -- which is the case the nearest-box rule could not answer once
           floors began to overlap. */
        assert(map_model_page(73) == 5);

        /* An object nobody placed and nobody drew has no floor to be on and
           answers with the first, which is what the caller filters by: it is
           never asked about a room it has not been given. */
        assert(map_model_page(250) == 0);
    }

    /* With no table bound there is exactly one floor and everything is on it,
       so a story nobody drew never offers a page the player cannot reach. */
    {
        assert(map_atlas_bind(0, 0) == 0);
        assert(map_model_pages() == 1);
        map_model_reset();
        {
            RoomModel only = mk(12);
            map_model_enter(&only);
        }
        assert(map_model_page(12) == 0);
    }

    /* One-way, conditional, self-loop and the reverse-blocked exception. */
    {
        MapExit ex[RM_DIR_N];
        int n, i, seen;

        map_model_reset();

        /* 20 leads east to 21 and 21 leads back west: two-way. */
        { RoomModel a = mk(20); link1(&a, RM_E, 21); map_model_enter(&a); }
        { RoomModel b = mk(21); link1(&b, RM_W, 20); map_model_enter(&b); }

        n = map_model_exits(20, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].dest == 21);
        assert(ex[0].dir == RM_E);
        assert(ex[0].kind == MAP_LINK_FLAT);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* 30 leads east to 31, and 31 leads nowhere: one-way. */
        map_model_reset();
        { RoomModel a = mk(30); link1(&a, RM_E, 31); map_model_enter(&a); }
        { RoomModel b = mk(31); map_model_enter(&b); }
        n = map_model_exits(30, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].flags & MAP_EXIT_ONEWAY);

        /* An unplaced room takes its floor from the rooms it can reach without
           changing floor, not from whichever floor box it happens to sit
           nearest. Floors overlap now -- a floor is one vertical step of the
           story's routes inside one drawn sheet, so the storeys of a building
           share the building's footprint -- and inside a shared footprint every
           candidate box is distance zero, which would have put every room the
           atlas does not place onto the lowest-numbered of them.

           No atlas is bound here, so map_atlas_page answers for nothing and the
           walk finds nothing either; what this pins is that the walk runs and
           declines rather than reading off the end of anything. The floor a
           real table produces is checked in test_map_atlas.c, where a table
           exists to produce one. */
        map_model_reset();
        { RoomModel a = mk(90); link1(&a, RM_E, 91); map_model_enter(&a); }
        { RoomModel b = mk(91); link1(&b, RM_W, 90); map_model_enter(&b); }
        assert(map_model_page(90) == 0);
        assert(map_model_page(91) == 0);

        /* Up and down are the only exits the walk refuses to cross. A room
           whose only way out is a staircase must not inherit the floor at the
           top of it, or a building's storeys would collapse back into one. */
        map_model_reset();
        { RoomModel a = mk(92); link1(&a, RM_UP, 93); map_model_enter(&a); }
        { RoomModel b = mk(93); link1(&b, RM_DOWN, 92); map_model_enter(&b); }
        assert(map_model_page(92) == 0);
        assert(map_model_page(93) == 0);

        /* A cycle of level exits must not hang the walk. */
        map_model_reset();
        { RoomModel a = mk(94); link1(&a, RM_E, 95); map_model_enter(&a); }
        { RoomModel b = mk(95); link1(&b, RM_E, 96); map_model_enter(&b); }
        { RoomModel c = mk(96); link1(&c, RM_W, 94); map_model_enter(&c); }
        assert(map_model_page(94) == 0);

        /* A room holding a passage whose far end the story never states cannot
           be said to have no way back, so no arrow. This is what a v3 direction
           property three bytes long is -- a routine deciding at run time, which
           carries no destination -- and it is every door the game opens with a
           verb: Zork I's trap door, its grating and its chimney, and The
           Lurking Horror's Terminal Room, whose only two exits are both
           routines and which was drawn with an arrowhead on the one passage the
           player walks both ways. */
        map_model_reset();
        { RoomModel a = mk(32); link1(&a, RM_E, 33); map_model_enter(&a); }
        { RoomModel b = mk(33);
          link_kind(&b, RM_S, 0, RM_EXIT_MAYBE); map_model_enter(&b); }
        n = map_model_exits(32, ex, RM_DIR_N);
        assert(n == 1);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* A refusal message is an assertion, not an absence: it says there is
           no passage that way. It must not veto the arrow, or a room that
           merely prints "the wall is solid" would suppress every arrowhead
           pointing at it. */
        map_model_reset();
        { RoomModel a = mk(34); link1(&a, RM_E, 35); map_model_enter(&a); }
        { RoomModel b = mk(35);
          link_kind(&b, RM_S, 0, RM_EXIT_BLOCKED); map_model_enter(&b); }
        n = map_model_exits(34, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].flags & MAP_EXIT_ONEWAY);

        /* An unstated destination anywhere in the room is enough. The reverse
           of a compass exit is not always its opposite -- a maze, an "out", a
           one-way loop -- so the veto cannot be narrowed to the facing
           direction without assuming a symmetry the stories do not have. */
        map_model_reset();
        { RoomModel a = mk(36); link1(&a, RM_E, 37); map_model_enter(&a); }
        { RoomModel b = mk(37);
          link1(&b, RM_E, 38);
          link_kind(&b, RM_UP, 0, RM_EXIT_MAYBE); map_model_enter(&b); }
        { RoomModel c = mk(38); link1(&c, RM_W, 37); map_model_enter(&c); }
        n = map_model_exits(36, ex, RM_DIR_N);
        assert(n == 1);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* A conditional exit that DOES name its destination -- a door or flag
           property, which carries one in byte 0 -- states where it goes, so it
           is no reason to withhold the arrow. */
        map_model_reset();
        { RoomModel a = mk(42); link1(&a, RM_E, 43); map_model_enter(&a); }
        { RoomModel b = mk(43);
          link_kind(&b, RM_S, 44, RM_EXIT_MAYBE); map_model_enter(&b); }
        { RoomModel c = mk(44); link1(&c, RM_N, 43); map_model_enter(&c); }
        n = map_model_exits(42, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].flags & MAP_EXIT_ONEWAY);

        /* A shut door back is still a way back, so this is not one-way. The
           arrow must not appear and vanish as the player opens things. */
        map_model_reset();
        { RoomModel a = mk(40); link1(&a, RM_E, 41); map_model_enter(&a); }
        { RoomModel b = mk(41);
          link_kind(&b, RM_W, 40, RM_EXIT_BLOCKED); map_model_enter(&b); }
        n = map_model_exits(40, ex, RM_DIR_N);
        assert(n == 1);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* RM_EXIT_MAYBE survives record_exits and shows as COND. */
        map_model_reset();
        { RoomModel a = mk(50);
          link_kind(&a, RM_E, 51, RM_EXIT_MAYBE); map_model_enter(&a); }
        { RoomModel b = mk(51); link1(&b, RM_W, 50); map_model_enter(&b); }
        n = map_model_exits(50, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].flags & MAP_EXIT_COND);

        /* An open exit is not conditional. */
        map_model_reset();
        { RoomModel a = mk(52); link1(&a, RM_E, 53); map_model_enter(&a); }
        { RoomModel b = mk(53); link1(&b, RM_W, 52); map_model_enter(&b); }
        n = map_model_exits(52, ex, RM_DIR_N);
        assert((ex[0].flags & MAP_EXIT_COND) == 0);

        /* An exit leading back to its own room is a self-loop, and is never
           also reported as one-way. */
        map_model_reset();
        { RoomModel a = mk(60); link1(&a, RM_N, 60); map_model_enter(&a); }
        n = map_model_exits(60, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].dest == 60);
        assert(ex[0].flags & MAP_EXIT_SELF);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* A destination nobody has placed has no exits on record, so it must
           not be read as having no way back. Guessing one-way from absent
           evidence would arrow half the map on the first move. */
        map_model_reset();
        { RoomModel a = mk(70); link1(&a, RM_E, 71); map_model_enter(&a); }
        n = map_model_exits(70, ex, RM_DIR_N);
        assert(n == 1);
        assert((ex[0].flags & MAP_EXIT_ONEWAY) == 0);

        /* A vertical exit keeps its kind and its direction, which is what
           lets a caller say U rather than D. */
        map_model_reset();
        { RoomModel a = mk(80); link1(&a, RM_DOWN, 81); map_model_enter(&a); }
        { RoomModel b = mk(81); link1(&b, RM_UP, 80); map_model_enter(&b); }
        n = map_model_exits(80, ex, RM_DIR_N);
        assert(n == 1);
        assert(ex[0].kind == MAP_LINK_VERT);
        assert(ex[0].dir == RM_DOWN);

        /* Every exit is reported, and max is honoured. */
        map_model_reset();
        { RoomModel a = mk(90);
          link1(&a, RM_N, 91); link1(&a, RM_E, 92); link1(&a, RM_S, 93);
          map_model_enter(&a); }
        assert(map_model_exits(90, ex, RM_DIR_N) == 3);
        assert(map_model_exits(90, ex, 2) == 2);

        /* An unvisited room reports nothing rather than reading a stale row. */
        assert(map_model_exits(200, ex, RM_DIR_N) == 0);

        seen = 0;
        n = map_model_exits(90, ex, RM_DIR_N);
        for (i = 0; i < n; i++) if (ex[i].dest == 92) seen = 1;
        assert(seen);

        /* map_model_link is unchanged by any of this. It answers NONE for an
           endpoint nobody has entered, so 91 has to be placed before the pair
           is a pair at all. */
        { RoomModel b = mk(91); link1(&b, RM_S, 90); map_model_enter(&b); }
        assert(map_model_link(90, 91) == MAP_LINK_FLAT);
        assert(map_model_link(90, 200) == MAP_LINK_NONE);
    }

    /* Unit steps, and the four with no direction on a flat drawing take north
       so a glyph annotating one of them lands somewhere. */
    {
        int dx, dy;
        map_model_step(RM_E, &dx, &dy);    assert(dx == 1 && dy == 0);
        map_model_step(RM_NE, &dx, &dy);   assert(dx == 1 && dy == -1);
        map_model_step(RM_UP, &dx, &dy);   assert(dx == 0 && dy == -1);
        map_model_step(RM_IN, &dx, &dy);   assert(dx == 0 && dy == -1);
        map_model_step(99, &dx, &dy);      assert(dx == 0 && dy == -1);
    }

    /* The chimney. The story decodes Studio's up exit with no destination and
       the Kitchen's descent as conditional on a flag that is never set, so the
       map drew a one-way arrow at the single direction the game refuses. The
       scanned marks supply the one and retract the other, and the two edits are
       one edit: supplying the destination alone would make has_reverse succeed
       and delete the arrow entirely. */
    {
        unsigned char hdr[0x18];
        MapExit ex[RM_DIR_N];
        int n, k, saw_up = 0;

        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        assert(map_marks_bind(hdr, sizeof hdr) > 0);

        map_model_reset();
        {
            RoomModel studio = mk(94);
            link_kind(&studio, RM_UP, 0, RM_EXIT_MAYBE);
            map_model_enter(&studio);
        }
        {
            RoomModel kitchen = mk(203);
            link_kind(&kitchen, RM_DOWN, 94, RM_EXIT_MAYBE);
            map_model_enter(&kitchen);
        }

        n = map_model_exits(94, ex, RM_DIR_N);
        for (k = 0; k < n; k++) {
            if (ex[k].dir != RM_UP) continue;
            saw_up = 1;
            assert(ex[k].dest == 203);
            assert(ex[k].flags & MAP_EXIT_BAGGAGE);
            assert(ex[k].flags & MAP_EXIT_ONEWAY);
        }
        assert(saw_up);

        n = map_model_exits(203, ex, RM_DIR_N);
        for (k = 0; k < n; k++)
            assert(ex[k].dir != RM_DOWN);
    }

    /* A baggage-only mark: Timber Room's west exit to the Drafty Room is a
       plain two-way passage the story already resolves on its own, marked
       narrow only for the weight it can carry through. Unlike the chimney,
       this row never overrides dest -- mdest stays 0 -- so it is the only
       case that exercises record_exits leaving the story's own destination
       and kind alone while still setting MAP_EXIT_BAGGAGE. Drafty holds the
       reverse exit, so this also pins that a mark does not manufacture a
       one-way arrow, and that it leaves the conditional bit the four earlier
       legend symbols are drawn from untouched. */
    {
        unsigned char hdr[0x18];
        MapExit ex[RM_DIR_N];
        int n, k, saw_w = 0;

        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        assert(map_marks_bind(hdr, sizeof hdr) > 0);

        map_model_reset();
        {
            RoomModel timber = mk(206);
            link_kind(&timber, RM_W, 228, RM_EXIT_MAYBE);
            map_model_enter(&timber);
        }
        {
            RoomModel drafty = mk(228);
            link1(&drafty, RM_E, 206);
            map_model_enter(&drafty);
        }

        n = map_model_exits(206, ex, RM_DIR_N);
        for (k = 0; k < n; k++) {
            if (ex[k].dir != RM_W) continue;
            saw_w = 1;
            assert(ex[k].dest == 228);
            assert(ex[k].flags & MAP_EXIT_BAGGAGE);
            assert((ex[k].flags & MAP_EXIT_ONEWAY) == 0);
            assert(ex[k].flags & MAP_EXIT_COND);
        }
        assert(saw_w);
    }

    /* The rule the generator enforces, enforced again here. A table row is a
       reading of a drawing and the story's own exit graph outranks it, so a
       mark lands only on an exit the story left conditional. Timber Room's
       west exit carries a shipped baggage row; stated OPEN rather than MAYBE,
       the row must be ignored outright -- no baggage bit, and the story's own
       destination and kind left as the snapshot gave them. Retraction is the
       sharper half of the same rule, since it would delete the exit, but the
       two share one guard and the baggage row is the one the shipped table
       lets this test reach. */
    {
        unsigned char hdr[0x18];
        MapExit ex[RM_DIR_N];
        int n, k, saw_w = 0;

        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        assert(map_marks_bind(hdr, sizeof hdr) > 0);

        map_model_reset();
        {
            RoomModel timber = mk(206);
            link_kind(&timber, RM_W, 228, RM_EXIT_OPEN);
            map_model_enter(&timber);
        }
        {
            RoomModel drafty = mk(228);
            link1(&drafty, RM_E, 206);
            map_model_enter(&drafty);
        }

        n = map_model_exits(206, ex, RM_DIR_N);
        for (k = 0; k < n; k++) {
            if (ex[k].dir != RM_W) continue;
            saw_w = 1;
            assert((ex[k].flags & MAP_EXIT_BAGGAGE) == 0);
            assert((ex[k].flags & MAP_EXIT_COND) == 0);
            assert(ex[k].dest == 228);
        }
        assert(saw_w);
    }

    /* A staircase the story decides by running code still says there is a way
       up. A three-byte direction property is a routine, so it names no
       destination at all -- room_model records it as RM_EXIT_MAYBE with a
       destination of zero -- and every such exit used to be dropped here, on
       the reasonable-looking rule that an exit with no far end has nothing to
       draw. It has: the map's U and D glyphs annotate the mark and claim
       nothing about where the stair comes out, which is the whole reason the
       glyph pass can already draw one for a staircase whose far end is off the
       viewport. The Lurking Horror's Concrete Box goes up to the Basement
       through one of these and showed no U at all.

       Only the vertical ones. A flat exit with no destination has a direction
       already, no far end for a run to reach, and no glyph of its own; letting
       it through would only give the link pass a stub to draw toward a room it
       cannot find. */
    {
        MapExit ex[RM_DIR_N];
        int n, k, saw_up = 0, saw_west = 0;

        assert(map_marks_bind(0, 0) == 0);
        map_model_reset();
        {
            RoomModel box = mk(37);
            link_kind(&box, RM_UP, 0, RM_EXIT_MAYBE);
            link_kind(&box, RM_W, 0, RM_EXIT_MAYBE);
            link_kind(&box, RM_N, 138, RM_EXIT_MAYBE);
            map_model_enter(&box);
        }

        n = map_model_exits(37, ex, RM_DIR_N);
        for (k = 0; k < n; k++) {
            if (ex[k].dir == RM_UP) {
                saw_up = 1;
                assert(ex[k].kind == MAP_LINK_VERT);
                assert(ex[k].dest == 0);
                assert(ex[k].flags & MAP_EXIT_COND);
                /* Not a self-loop: dest 0 is no destination, not this room. */
                assert((ex[k].flags & MAP_EXIT_SELF) == 0);
                assert((ex[k].flags & MAP_EXIT_ONEWAY) == 0);
            }
            if (ex[k].dir == RM_W) saw_west = 1;
        }
        assert(saw_up);
        assert(!saw_west);
    }

    /* IN and OUT are not staircases. They sit past RM_UP in the direction
       enum for no reason but the order the compass rose reads, and the kind
       was taken from `d >= RM_UP`, so every one of them drew as a level
       change: stair bars along the route and a letter beside the mark, with
       the glyph pass reading that letter off the parity of the index and so
       calling RM_OUT a D and RM_IN a U. Walking into a building puts you on
       its ground floor, which is what gen_map_atlas.py's LEVEL_DIRS has said
       since the floor pass was written.

       Two halves, and this asserts both. A destination-less OUT is now flat,
       so map_model_exits drops it and nothing is drawn -- The Lurking Horror's
       Terminal Room, whose only two exits are both routines, was showing a
       bare D. An OUT that names a room is flat too, so it draws as an ordinary
       passage and the glyph pass declines it. UP and DOWN are untouched. */
    {
        MapExit ex[RM_DIR_N];
        int n, k, saw_out = 0, saw_in = 0, saw_up = 0;

        assert(map_marks_bind(0, 0) == 0);
        map_model_reset();
        {
            RoomModel term = mk(176);
            link_kind(&term, RM_OUT, 0, RM_EXIT_MAYBE);
            link_kind(&term, RM_S, 0, RM_EXIT_MAYBE);
            map_model_enter(&term);
        }
        n = map_model_exits(176, ex, RM_DIR_N);
        for (k = 0; k < n; k++)
            assert(ex[k].dir != RM_OUT && ex[k].dir != RM_S);

        map_model_reset();
        {
            RoomModel hall = mk(60);
            link1(&hall, RM_OUT, 61);
            link1(&hall, RM_IN, 62);
            link1(&hall, RM_UP, 63);
            map_model_enter(&hall);
        }
        {
            RoomModel yard = mk(61);
            link1(&yard, RM_IN, 60);
            map_model_enter(&yard);
        }
        {
            RoomModel loft = mk(63);
            link1(&loft, RM_DOWN, 60);
            map_model_enter(&loft);
        }

        n = map_model_exits(60, ex, RM_DIR_N);
        for (k = 0; k < n; k++) {
            if (ex[k].dir == RM_OUT) {
                saw_out = 1;
                assert(ex[k].kind == MAP_LINK_FLAT);
            }
            if (ex[k].dir == RM_IN) {
                saw_in = 1;
                assert(ex[k].kind == MAP_LINK_FLAT);
            }
            if (ex[k].dir == RM_UP) {
                saw_up = 1;
                assert(ex[k].kind == MAP_LINK_VERT);
            }
        }
        assert(saw_out && saw_in && saw_up);

        /* map_model_link answers from the same rows, so it must agree: a way
           out of a building is a road between two rooms, not a stair. */
        assert(map_model_link(60, 61) == MAP_LINK_FLAT);
        assert(map_model_link(60, 63) == MAP_LINK_VERT);
    }

    /* Two floors may stand on the same cell, and the crosshair uses that.

       Only one floor is drawn at a time -- gather() and extent() both filter on
       the page -- so a cell is owed to be unique within a floor and not across
       the whole table, and the atlas slides the floors over each other on
       purpose so that a staircase comes out at the coordinate it went in at.
       The contest search enforced it globally, so every one of those coincident
       cells was a contest and the room entered second was flung to a ring cell:
       that is what put The Lurking Horror's Second Floor east of the Terminal
       Room instead of south of it. It was already wrong before the floors were
       aligned -- 135 rooms across the disc shared a cell with a room on another
       floor and were being displaced for it.

       The pair is looked up in the bound table rather than named, because which
       rooms coincide is a property of a generated file and would rot. */
    {
        unsigned char hdr[0x18];
        int r, q, lo = 0, hi = 0, lop = 0, hip = 0;

        memset(hdr, 0, sizeof hdr);
        hdr[0] = 3; hdr[2] = 0; hdr[3] = 88;
        memcpy(hdr + 0x12, "840726", 6);
        assert(map_atlas_bind(hdr, sizeof hdr) > 0);

        for (r = 1; r < MAP_ROOM_MAX && !lo; r++) {
            int ax, ay, ap;
            if (!map_atlas_pos((unsigned short) r, &ax, &ay)) continue;
            if (!map_atlas_page((unsigned short) r, &ap)) continue;
            for (q = r + 1; q < MAP_ROOM_MAX; q++) {
                int bx, by, bp;
                if (!map_atlas_pos((unsigned short) q, &bx, &by)) continue;
                if (!map_atlas_page((unsigned short) q, &bp)) continue;
                if (ax != bx || ay != by || ap == bp) continue;
                lo = r; hi = q; lop = ap; hip = bp;
                break;
            }
        }
        assert(lo && hi);

        map_model_reset();
        {
            RoomModel a = mk((unsigned short) lo);
            RoomModel b = mk((unsigned short) hi);
            map_model_enter(&a);
            map_model_enter(&b);
        }
        {
            int ax, ay, bx, by, nx = 99, ny = 99;
            assert(map_model_pos((unsigned short) lo, &ax, &ay));
            assert(map_model_pos((unsigned short) hi, &bx, &by));
            assert(map_model_page((unsigned short) lo) == lop);
            assert(map_model_page((unsigned short) hi) == hip);
            assert(ax == bx && ay == by);

            /* And that is what the crosshair is for. Standing on one of them,
               paging to the other's floor lands at distance zero: the room the
               staircase reaches, already under the cursor.

               A floor is routinely taller than the five rows the viewport has
               -- The Lurking Horror's first is eleven -- so clamping into the
               floor's bounding box can leave the crosshair on empty ground with
               every room off screen, which reads as a floor holding nothing.
               The nearest room is always something to look at. */
            assert(map_model_nearest(hip, 0, 0, &nx, &ny));
            assert(nx == 0 && ny == 0);
        }

        /* A floor with nothing placed on it answers no rather than zero, so the
           caller can fall back rather than draw a cursor over bare ground. */
        {
            int nx = 99, ny = 99, pg, n = map_model_pages(), empty = -1;
            for (pg = 0; pg < n; pg++) {
                int ax, ay;
                if (!map_model_nearest(pg, 0, 0, &ax, &ay)) { empty = pg; break; }
            }
            assert(empty >= 0);
            assert(!map_model_nearest(empty, 0, 0, &nx, &ny));
            assert(nx == 99 && ny == 99);
        }

        /* Within one floor a cell is still owed to be unique, which is the half
           that must not be lost: two rooms of one drawn floor in one cell draw
           one mark and lose the other. */
        {
            int wx, wy, nx, ny;
            map_model_reset();
            {
                RoomModel woh = mk(180);
                RoomModel noh = mk(81);
                link1(&woh, RM_N, 81);
                link1(&noh, RM_S, 180);
                map_model_enter(&woh);
                map_model_enter(&noh);
            }
            assert(map_model_pos(180, &wx, &wy));
            assert(map_model_pos(81, &nx, &ny));
            assert(map_model_page(180) == map_model_page(81));
            assert(wx != nx || wy != ny);
        }
    }

    /* What L and R follow. Page order cannot answer "which floor is above
       this one" and no numbering of the pages could: a page index is one line
       and a story's floors are a tree, so a level with three floors above it
       gets at most one of them as its neighbour. The room knows -- up is the
       floor its own staircase reaches. */
    {
        unsigned short up = 0, down = 0;

        assert(map_marks_bind(0, 0) == 0);
        assert(map_atlas_bind(0, 0) == 0);
        map_model_reset();
        {
            RoomModel mid = mk(20);
            RoomModel top = mk(30);
            RoomModel bot = mk(40);
            link1(&mid, RM_UP, 30);
            link1(&mid, RM_DOWN, 40);
            link1(&mid, RM_N, 50);
            link1(&top, RM_DOWN, 20);
            link1(&bot, RM_UP, 20);
            map_model_enter(&mid);
            map_model_enter(&top);
            map_model_enter(&mid);
            map_model_enter(&bot);
            map_model_enter(&mid);
        }
        assert(map_model_climb(20, 1, &up) && up == 30);
        assert(map_model_climb(20, 0, &down) && down == 40);
        assert(map_model_climb(30, 0, &down) && down == 20);

        /* No staircase that way is a real answer: the caller steps the page
           index instead, which is all it ever had. */
        assert(!map_model_climb(30, 1, &up));
        assert(!map_model_climb(40, 0, &down));

        /* A flat exit is not a staircase, however the rooms are drawn. */
        {
            MapExit ex[RM_DIR_N];
            int n = map_model_exits(20, ex, RM_DIR_N), k, saw = 0;
            for (k = 0; k < n; k++) if (ex[k].dir == RM_N) saw = 1;
            assert(saw);
        }
        assert(!map_model_climb(50, 1, &up));

        /* An unvisited far end is not somewhere to send the reader: the map
           would page to a floor with nothing drawn on it. */
        map_model_reset();
        {
            RoomModel lone = mk(20);
            link1(&lone, RM_UP, 30);
            map_model_enter(&lone);
        }
        assert(!map_model_visited(30));
        assert(!map_model_climb(20, 1, &up));

        /* And a staircase to itself goes nowhere. */
        map_model_reset();
        {
            RoomModel loop = mk(20);
            link1(&loop, RM_UP, 20);
            map_model_enter(&loop);
        }
        assert(!map_model_climb(20, 1, &up));
    }

    printf("test_map_model: ok\n");
    return 0;
}
