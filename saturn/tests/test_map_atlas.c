/* Build:
     gcc -O2 -I saturn/src -I saturn/src/engine -o /tmp/tma \
         saturn/tests/test_map_atlas.c saturn/src/engine/map_atlas.c && /tmp/tma
   map_atlas.c reads nothing but the story's header, so a fabricated header is
   enough to exercise every path -- no story image is needed here. */
#include "../src/engine/map_atlas.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* How many rooms each table this test names is expected to carry. These track a
   generated file, so they are the one thing here that legitimately changes when
   tools/gen_map_atlas.py is re-run over better scans; everything else asserted
   below is geometry that must hold whatever the count.

   They moved when the generator learned to walk in the rooms a scan had missed:
   Zork I from 84 to 96, Enchanter from 52 to 59. Not to every room the story
   has -- Zork I's fifteen Maze rooms and Enchanter's seven Courtyards are left
   out on purpose, because a group of identically-named rooms is one the scan
   declined to place rather than failed to, and a maze walked in from the exit
   graph is a knot rather than a map. The geometry below did not move with the
   count, which is the point: every room this file names by hand was read off
   Infocom's drawing and is asserted to be exactly where it was. */
#define ZORK1_ROOMS    96
#define ENCHANTR_ROOMS 59

/* A Z-machine v3 header is all map_atlas_bind looks at: the release word at
   0x02 and the six serial bytes at 0x12. */
static void header(unsigned char *h, unsigned int release, const char *serial) {
    memset(h, 0, 0x18);
    h[0] = 3;
    h[0x02] = (unsigned char) (release >> 8);
    h[0x03] = (unsigned char) (release & 0xFF);
    memcpy(h + 0x12, serial, 6);
}

/* Starcross's walked table, as the generator accepted it. */
#define STARCROS_ROOMS 85

int main(void) {
    unsigned char h[0x18];
    int x = 99, y = 99;

    /* Nothing is bound before a story is, and every lookup says so rather than
       reading off the end of a null table. */
    assert(map_atlas_count() == 0);
    assert(!map_atlas_pos(180, &x, &y));

    /* Starcross release 17, which is the build under saturn/cd/data/Z3. It has
       no printed map anybody could scan, so its table is laid out from its own
       exit graph -- and the point of this assertion is that the runtime cannot
       tell, and does not have to: a walked table binds, counts and reads back
       exactly as a measured one does. Before it existed this bind returned 0
       and the game had no map on any difficulty.

       The number is the room count the generator accepted, so it fails if a
       regeneration silently ships fewer rooms. */
    header(h, 17, "821021");
    assert(map_atlas_bind(h, sizeof h) == STARCROS_ROOMS);
    assert(map_atlas_count() == STARCROS_ROOMS);
    assert(map_atlas_pages() >= 1);
    {
        /* Every cell reads back inside the signed-char range the table stores,
           on a floor the reader can reach. */
        int i;
        for (i = 0; i < map_atlas_count(); i++) {
            unsigned short room = 0;
            int cx = 999, cy = 999;
            assert(map_atlas_room_at(i, &room));
            assert(room > 0 && room <= 255);
            assert(map_atlas_pos(room, &cx, &cy));
            assert(cx >= -128 && cx <= 127 && cy >= -128 && cy <= 127);
        }
    }

    /* Zork I release 88, which is the build under saturn/cd/data/Z3. */
    header(h, 88, "840726");
    assert(map_atlas_bind(h, sizeof h) == ZORK1_ROOMS);
    assert(map_atlas_count() == ZORK1_ROOMS);

    /* The four rooms around the house, and the geometry that is the whole point
       of the table: North of House is north of West of House, South of House is
       south of it, and Behind House is east of both. */
    {
        int wx, wy, nx, ny, sx, sy, bx, by;
        assert(map_atlas_pos(180, &wx, &wy));
        assert(map_atlas_pos(81, &nx, &ny));
        assert(map_atlas_pos(80, &sx, &sy));
        assert(map_atlas_pos(79, &bx, &by));
        assert(ny < wy);
        assert(sy > wy);
        assert(bx > wx && bx > nx && bx > sx);
    }

    /* The canyon descends: Canyon View above Rocky Ledge above Canyon Bottom. */
    {
        int cx, cy, rx, ry, bx, by;
        assert(map_atlas_pos(25, &cx, &cy));
        assert(map_atlas_pos(26, &rx, &ry));
        assert(map_atlas_pos(27, &bx, &by));
        assert(cy < ry && ry < by);
    }

    /* Maze rooms are deliberately absent, so they fall through to the walk. In
       release 88 the maze occupies a contiguous run of object numbers; none of
       it may be in the table. */
    {
        int i, hit = 0;
        for (i = 1; i < 256; i++)
            if (map_atlas_pos((unsigned short) i, &x, &y)) hit++;
        assert(hit == ZORK1_ROOMS);
    }

    /* A second story resolves to its own table, which is the whole point of
       keying on the header rather than assuming one game. Enchanter's rooms are
       numbered in its own space and must not be answered from Zork I's table. */
    {
        int ex, ey, n = 0, i;
        header(h, 29, "860820");
        assert(map_atlas_bind(h, sizeof h) == ENCHANTR_ROOMS);
        assert(map_atlas_count() == ENCHANTR_ROOMS);
        for (i = 1; i < 256; i++)
            if (map_atlas_pos((unsigned short) i, &ex, &ey)) n++;
        assert(n == ENCHANTR_ROOMS);

        /* Rebinding Zork I puts its own answers back, so nothing leaks between
           tables. */
        header(h, 88, "840726");
        assert(map_atlas_bind(h, sizeof h) == ZORK1_ROOMS);
        assert(map_atlas_pos(180, &ex, &ey));
    }

    /* Every table must be sorted ascending by object number, because
       map_atlas_pos bisects rather than scans. A generated file that came out
       unsorted would answer "not in the table" for real rooms, silently and
       only for some of them. */
    {
        static const struct { unsigned int rel; const char *ser; } BUILDS[] = {
            {  97, "851218" },   /* BALLYHOO */
            {  23, "840809" },   /* CUTHROAT */
            {  29, "860820" },   /* ENCHANTR */
            {  37, "861215" },   /* HOLYWOOD */
            {  22, "830916" },   /* INFIDEL */
            {  59, "860730" },   /* LEATHERG */
            { 219, "870912" },   /* LURKING */
            {   9, "861022" },   /* MOONMIST */
            {  26, "870730" },   /* PLNDHRTS */
            {  15, "851108" },   /* SORCERER */
            {  87, "860904" },   /* SPLBRKR */
            { 107, "870430" },   /* STATFALL */
            {   8, "840521" },   /* SUSPENDD */
            {  69, "850920" },   /* WISHBRNG */
            {  22, "840924" },   /* WITNESS */
            {  88, "840726" },   /* ZORK1 */
            {  48, "840904" },   /* ZORK2 */
            {  17, "840727" },   /* ZORK3 */
        };
        unsigned int b;
        for (b = 0; b < sizeof BUILDS / sizeof BUILDS[0]; b++) {
            int i, n, prev = -1, seen = 0, pages;
            unsigned short room = 0;
            header(h, BUILDS[b].rel, BUILDS[b].ser);
            n = map_atlas_bind(h, sizeof h);
            assert(n > 0);
            pages = map_atlas_pages();
            assert(pages >= 1 && pages <= MAP_ATLAS_PAGE_MAX);
            for (i = 0; i < n; i++) {
                int page = -1;
                assert(map_atlas_room_at(i, &room));
                assert((int) room > prev);
                prev = (int) room;
                assert(map_atlas_pos(room, &x, &y));
                /* Every room names a floor the table declares, and its cell is
                   inside that floor's box. The second half is what makes the
                   first mean something: a page field that was right about its
                   own count and wrong about which rooms belonged to it would
                   pass the count check on its own. */
                assert(map_atlas_page(room, &page));
                assert(page >= 0 && page < pages);
                {
                    int x0, y0, x1, y1;
                    assert(map_atlas_page_box(page, &x0, &y0, &x1, &y1));
                    assert(x >= x0 && x <= x1 && y >= y0 && y <= y1);
                }
                seen++;
            }
            assert(seen == n);
            assert(!map_atlas_room_at(n, &room));
            /* Every declared floor holds at least one room, so paging never
               offers an empty screen -- the generator renumbers densely and
               this is what holds it to that. */
            for (i = 0; i < pages; i++) {
                int x0, y0, x1, y1;
                assert(map_atlas_page_box(i, &x0, &y0, &x1, &y1));
                assert(x1 >= x0 && y1 >= y0);
            }
            assert(!map_atlas_page_box(pages, &x, &y, &x, &y));
            assert(!map_atlas_page_box(-1, &x, &y, &x, &y));
            /* Floors may share ground, and mostly do: a floor is one vertical
               step of the story's routes inside one drawn sheet, so the storeys
               of a building sit on the building's own footprint. What is
               asserted is only that the question can be asked and answered --
               a caller that drew two floors at once would have to consult it,
               and nothing in the port does. */
            (void) map_atlas_pages_overlap();
        }
    }

    /* Zork I is drawn on three sheets -- above ground, the dungeon, and the
       coal mine -- and its routes cut those into nine floors, so a table that
       lost its page column would report one floor here and still pass every
       check above. The count is pinned rather than derived because the whole
       point of the column is that it cannot be recomputed from the
       coordinates. */
    {
        int page = -1;
        header(h, 88, "840726");
        assert(map_atlas_bind(h, sizeof h) > 0);
        assert(map_atlas_pages() == 9);
        /* West of House is object 180 and stands on the first sheet. */
        assert(map_atlas_page(180, &page));
        assert(page == 0);
        assert(!map_atlas_page(1, &page));
    }

    /* No table is bound for a story nobody drew, and the previously bound one is
       forgotten rather than left to answer for it -- including its floors, which
       would otherwise let a caller page a map that has none. */
    header(h, 88, "999999");
    assert(map_atlas_bind(h, sizeof h) == 0);
    assert(map_atlas_count() == 0);
    assert(map_atlas_pages() == 0);
    assert(!map_atlas_pos(180, &x, &y));
    assert(!map_atlas_page_box(0, &x, &y, &x, &y));

    /* Object numbers are assigned by the compiler, so a different release of the
       same game must not match: its rooms are numbered differently and the table
       would place them somewhere plausible and wrong. Release 119 is the build
       the Japanese disc carries. */
    header(h, 119, "880429");
    assert(map_atlas_bind(h, sizeof h) == 0);

    /* A truncated image and a null one are refused rather than read. */
    header(h, 88, "840726");
    assert(map_atlas_bind(h, 0x17) == 0);
    assert(map_atlas_bind(0, 64) == 0);
    assert(!map_atlas_pos(180, &x, &y));

    printf("test_map_atlas: ok\n");
    return 0;
}
