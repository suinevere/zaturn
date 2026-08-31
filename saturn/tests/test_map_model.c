/* Build:
     gcc -O2 -I saturn/src -o /tmp/tmm saturn/tests/test_map_model.c \
         saturn/src/engine/map_model.c && /tmp/tmm
   map_model.c is deliberately free of SRL includes so this links on the host. */
#include "../src/engine/map_model.h"
#include "../src/engine/map_atlas.h"
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

    printf("test_map_model: ok\n");
    return 0;
}
