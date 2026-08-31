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
 | Description: One authored room and one authored story. Cells are signed
 |   chars because an authored map is tens of rooms across, not hundreds, and
 |   the whole point is that these are hand-checked rather than generated
 |   without bound. The serial is the six header bytes as text, not
 |   null-terminated in the story image, so it is compared by length.
 | Author: suinevere
 ----------------------*/
typedef struct { unsigned short room; signed char x, y; } MapAtlasCell;

typedef struct {
    unsigned short      release;
    const char         *serial;
    const MapAtlasCell *cells;
    unsigned short      n;
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
 | g_cells / g_n
 | Description: The bound table, or nothing. Null and zero is the ordinary
 |   state for a story nobody has drawn.
 | Author: suinevere
 ----------------------*/
static const MapAtlasCell *g_cells;
static int                 g_n;

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
    if (story == 0 || len < HDR_MIN) return 0;

    release = ((unsigned int) story[HDR_RELEASE] << 8) | story[HDR_RELEASE + 1u];
    for (i = 0; i < MAP_ATLAS_STORY_N; i++) {
        if (MAP_ATLAS_STORIES[i].release != (unsigned short) release) continue;
        if (!serial_is(story + HDR_SERIAL, MAP_ATLAS_STORIES[i].serial)) continue;
        g_cells = MAP_ATLAS_STORIES[i].cells;
        g_n = (int) MAP_ATLAS_STORIES[i].n;
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
