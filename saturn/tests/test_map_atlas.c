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
   below is geometry that must hold whatever the count. */
#define ZORK1_ROOMS    84
#define ENCHANTR_ROOMS 52

/* A Z-machine v3 header is all map_atlas_bind looks at: the release word at
   0x02 and the six serial bytes at 0x12. */
static void header(unsigned char *h, unsigned int release, const char *serial) {
    memset(h, 0, 0x18);
    h[0] = 3;
    h[0x02] = (unsigned char) (release >> 8);
    h[0x03] = (unsigned char) (release & 0xFF);
    memcpy(h + 0x12, serial, 6);
}

int main(void) {
    unsigned char h[0x18];
    int x = 99, y = 99;

    /* Nothing is bound before a story is, and every lookup says so rather than
       reading off the end of a null table. */
    assert(map_atlas_count() == 0);
    assert(!map_atlas_pos(180, &x, &y));

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
            {  97, "851218" }, {  29, "860820" }, {  37, "861215" },
            {  22, "830916" }, {  59, "860730" }, {  26, "870730" },
            {  15, "851108" }, {  87, "860904" }, { 107, "870430" },
            {  69, "850920" }, {  88, "840726" }, {  48, "840904" },
            {  17, "840727" }
        };
        unsigned int b;
        for (b = 0; b < sizeof BUILDS / sizeof BUILDS[0]; b++) {
            int i, n, prev = -1, seen = 0;
            unsigned short room = 0;
            header(h, BUILDS[b].rel, BUILDS[b].ser);
            n = map_atlas_bind(h, sizeof h);
            assert(n > 0);
            for (i = 0; i < n; i++) {
                assert(map_atlas_room_at(i, &room));
                assert((int) room > prev);
                prev = (int) room;
                assert(map_atlas_pos(room, &x, &y));
                seen++;
            }
            assert(seen == n);
            assert(!map_atlas_room_at(n, &room));
        }
    }

    /* No table is bound for a story nobody drew, and the previously bound one is
       forgotten rather than left to answer for it. */
    header(h, 88, "999999");
    assert(map_atlas_bind(h, sizeof h) == 0);
    assert(map_atlas_count() == 0);
    assert(!map_atlas_pos(180, &x, &y));

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
