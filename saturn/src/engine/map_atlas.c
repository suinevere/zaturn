/*----------------------
 | map_atlas.c
 | Description: Implements map_atlas.h. The tables themselves are generated into
 |   map_atlas_data.inc by tools/gen_map_atlas.py, which reads Infocom's own
 |   InvisiClues maps and matches them against the story's exit graph; see that
 |   script for how a drawn box becomes an object number.
 | Author: suinevere
 ----------------------*/
#include "map_atlas.h"

/*----------------------
 | MapAtlasCell / MapAtlasStory
 | Description: One authored room and one authored story. Coordinates are signed
 |   chars because an authored map is tens of rooms across, not hundreds, and
 |   the whole point is that these are hand-checked rather than generated
 |   without bound. The room is one byte for the same reason it can be: a
 |   Z-machine v3 story numbers its objects 1 to 255, so a wider field would buy
 |   nothing and the floor rides in the space the padding was already taking --
 |   861 cells carry their page for no bytes at all. The serial is the six
 |   header bytes as text, not null-terminated in the story image, so it is
 |   compared by length.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char room, page;
    signed char   x, y;
} MapAtlasCell;

typedef struct {
    unsigned short      release;
    const char         *serial;
    const MapAtlasCell *cells;
    unsigned short      n;
    unsigned char       pages;
} MapAtlasStory;

#include "map_atlas_data.inc"

/*----------------------
 | HDR_RELEASE / HDR_SERIAL / HDR_MIN
 | Description: Where a Z-machine v3 header keeps the release word and the six
 |   serial bytes, and the shortest image that can hold both.
 | Author: suinevere
 ----------------------*/
#define HDR_RELEASE 0x02u
#define HDR_SERIAL  0x12u
#define HDR_MIN     0x18u

/*----------------------
 | g_cells / g_n / g_pages / g_box
 | Description: The bound table, or nothing. Null and zero is the ordinary
 |   state for a story nobody has drawn. g_box holds each floor's inclusive
 |   bounding box as x0, y0, x1, y1, measured once at bind: the caller asking
 |   for it is a renderer deciding which floor a cell falls on, and a scan of
 |   the whole table per cell is not free.
 | Author: suinevere
 ----------------------*/
static const MapAtlasCell *g_cells;
static int                 g_n;
static int                 g_pages;
static signed char         g_box[MAP_ATLAS_PAGE_MAX][4];

/*----------------------
 | serial_is
 | Description: Whether six unterminated header bytes spell a given serial.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: raw -- the six header bytes; want -- the serial to compare
 | Returns: 1 when equal, 0 otherwise
 ----------------------*/
static int serial_is(const unsigned char *raw, const char *want) {
    int i;
    for (i = 0; i < 6; i++)
        if (raw[i] != (unsigned char) want[i]) return 0;
    return want[6] == '\0';
}

/*----------------------
 | measure_pages
 | Description: Fills g_box from the bound table. A floor with no rooms -- which
 |   the generator does not emit, since it renumbers densely -- is left as an
 |   empty box and reported so by map_atlas_page_box.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n, g_pages, g_box
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void measure_pages(void) {
    int seen[MAP_ATLAS_PAGE_MAX];
    int p, i;

    for (p = 0; p < MAP_ATLAS_PAGE_MAX; p++) {
        seen[p] = 0;
        g_box[p][0] = g_box[p][1] = 0;
        g_box[p][2] = g_box[p][3] = -1;
    }
    for (i = 0; i < g_n; i++) {
        p = g_cells[i].page;
        if (p >= MAP_ATLAS_PAGE_MAX) p = MAP_ATLAS_PAGE_MAX - 1;
        if (!seen[p]) {
            g_box[p][0] = g_box[p][2] = g_cells[i].x;
            g_box[p][1] = g_box[p][3] = g_cells[i].y;
            seen[p] = 1;
            continue;
        }
        if (g_cells[i].x < g_box[p][0]) g_box[p][0] = g_cells[i].x;
        if (g_cells[i].y < g_box[p][1]) g_box[p][1] = g_cells[i].y;
        if (g_cells[i].x > g_box[p][2]) g_box[p][2] = g_cells[i].x;
        if (g_cells[i].y > g_box[p][3]) g_box[p][3] = g_cells[i].y;
    }
}

/*----------------------
 | map_atlas_bind
 | Description: See map_atlas.h.
 | Author: suinevere
 | Dependencies: serial_is
 | Globals: g_cells, g_n
 | Params: story -- the story image, may be null; len -- its length
 | Returns: the number of authored rooms bound, 0 when none matched
 ----------------------*/
int map_atlas_bind(const unsigned char *story, unsigned int len) {
    unsigned int release;
    int i;

    g_cells = 0;
    g_n = 0;
    g_pages = 0;
    if (story == 0 || len < HDR_MIN) return 0;

    release = ((unsigned int) story[HDR_RELEASE] << 8) | story[HDR_RELEASE + 1u];
    for (i = 0; i < MAP_ATLAS_STORY_N; i++) {
        if (MAP_ATLAS_STORIES[i].release != (unsigned short) release) continue;
        if (!serial_is(story + HDR_SERIAL, MAP_ATLAS_STORIES[i].serial)) continue;
        g_cells = MAP_ATLAS_STORIES[i].cells;
        g_n = (int) MAP_ATLAS_STORIES[i].n;
        g_pages = (int) MAP_ATLAS_STORIES[i].pages;
        if (g_pages > MAP_ATLAS_PAGE_MAX) g_pages = MAP_ATLAS_PAGE_MAX;
        if (g_pages < 1) g_pages = 1;
        measure_pages();
        return g_n;
    }
    return 0;
}

/*----------------------
 | map_atlas_pos
 | Description: See map_atlas.h. The table is generated in ascending room order,
 |   so this bisects rather than scanning: a room the player walks into costs a
 |   handful of comparisons instead of a pass over the whole map.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: room -- object number; x, y -- receive the cell
 | Returns: 1 when the room is in the bound table, 0 otherwise
 ----------------------*/
int map_atlas_pos(unsigned short room, int *x, int *y) {
    int lo = 0, hi = g_n - 1;
    if (g_cells == 0) return 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_cells[mid].room == room) {
            *x = g_cells[mid].x;
            *y = g_cells[mid].y;
            return 1;
        }
        if (g_cells[mid].room < room) lo = mid + 1;
        else                          hi = mid - 1;
    }
    return 0;
}

/*----------------------
 | map_atlas_count
 | Description: See map_atlas.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_n
 | Params: N/A
 | Returns: the authored room count, 0 when no table is bound
 ----------------------*/
int map_atlas_count(void) {
    return g_n;
}

/*----------------------
 | map_atlas_room_at
 | Description: See map_atlas.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: index -- position in the table; room -- receives the object number
 | Returns: 1 on success, 0 when index is out of range
 ----------------------*/
int map_atlas_room_at(int index, unsigned short *room) {
    if (g_cells == 0 || index < 0 || index >= g_n) return 0;
    *room = g_cells[index].room;
    return 1;
}

/*----------------------
 | map_atlas_pages
 | Description: See map_atlas.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pages
 | Params: N/A
 | Returns: the floor count, 0 when no table is bound
 ----------------------*/
int map_atlas_pages(void) {
    return g_pages;
}

/*----------------------
 | map_atlas_page
 | Description: See map_atlas.h. Bisects the same way map_atlas_pos does, and
 |   for the same reason: the table is in ascending room order and a renderer
 |   asks this once per drawn room.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cells, g_n
 | Params: room -- object number; page -- receives the floor
 | Returns: 1 when the room is in the bound table, 0 otherwise
 ----------------------*/
int map_atlas_page(unsigned short room, int *page) {
    int lo = 0, hi = g_n - 1;
    if (g_cells == 0) return 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_cells[mid].room == room) {
            int p = g_cells[mid].page;
            *page = (p >= MAP_ATLAS_PAGE_MAX) ? MAP_ATLAS_PAGE_MAX - 1 : p;
            return 1;
        }
        if (g_cells[mid].room < room) lo = mid + 1;
        else                          hi = mid - 1;
    }
    return 0;
}

/*----------------------
 | map_atlas_page_box
 | Description: See map_atlas.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_box, g_pages
 | Params: page -- the floor; x0, y0, x1, y1 -- receive the box
 | Returns: 1 when the floor exists and holds at least one room, 0 otherwise
 ----------------------*/
int map_atlas_page_box(int page, int *x0, int *y0, int *x1, int *y1) {
    if (page < 0 || page >= g_pages) return 0;
    if (g_box[page][2] < g_box[page][0]) return 0;
    *x0 = g_box[page][0];
    *y0 = g_box[page][1];
    *x1 = g_box[page][2];
    *y1 = g_box[page][3];
    return 1;
}

/*----------------------
 | map_atlas_pages_overlap
 | Description: See map_atlas.h.
 | Author: suinevere
 | Dependencies: map_atlas_page_box
 | Globals: g_pages
 | Params: N/A
 | Returns: 1 when two floors share ground, 0 otherwise
 ----------------------*/
int map_atlas_pages_overlap(void) {
    int a, b;
    for (a = 0; a < g_pages; a++) {
        int ax0, ay0, ax1, ay1;
        if (!map_atlas_page_box(a, &ax0, &ay0, &ax1, &ay1)) continue;
        for (b = a + 1; b < g_pages; b++) {
            int bx0, by0, bx1, by1;
            if (!map_atlas_page_box(b, &bx0, &by0, &bx1, &by1)) continue;
            if (ax0 <= bx1 && bx0 <= ax1 && ay0 <= by1 && by0 <= ay1) return 1;
        }
    }
    return 0;
}
