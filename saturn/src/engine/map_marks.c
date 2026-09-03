/*----------------------
 | map_marks.c
 | Description: Implements map_marks.h. The table itself is generated into
 |   map_marks_data.inc by tools/gen_map_marks.py, which reads Infocom's own
 |   InvisiClues maps and reconciles the marks it finds against the story's
 |   exit graph; see that script for how a drawn symbol becomes a row here.
 | Author: suinevere
 ----------------------*/
#include "map_marks.h"

/*----------------------
 | MapMark / MapMarkStory
 | Description: One scanned exit mark and one scanned story. room and dir key
 |   the exit; dest is the destination the drawing supplies where the story's
 |   own exit hid it behind a routine, 0 where the story already carries one;
 |   flags holds MARK_BAGGAGE and MARK_RETRACT. All four fields fit a byte --
 |   a Z-machine v3 story numbers its objects 1 to 255 and its compass has
 |   nowhere near 256 directions -- so the row costs four bytes and no more.
 |   The serial is the six header bytes as text, not null-terminated in the
 |   story image, so it is compared by length.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char room, dir, dest, flags;
} MapMark;

typedef struct {
    unsigned short  release;
    const char     *serial;
    const MapMark  *marks;
    unsigned short  n;
} MapMarkStory;

#include "map_marks_data.inc"

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
 | g_marks / g_n
 | Description: The bound table, or nothing. Null and zero is the ordinary
 |   state for a story nobody has drawn.
 | Author: suinevere
 ----------------------*/
static const MapMark *g_marks;
static int             g_n;

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
 | map_marks_bind
 | Description: See map_marks.h.
 | Author: suinevere
 | Dependencies: serial_is
 | Globals: g_marks, g_n
 | Params: story -- the story image, may be null; len -- its length
 | Returns: the number of marks bound, 0 when none matched
 ----------------------*/
int map_marks_bind(const unsigned char *story, unsigned int len) {
    unsigned int release;
    int i;

    g_marks = 0;
    g_n = 0;
    if (story == 0 || len < HDR_MIN) return 0;

    release = ((unsigned int) story[HDR_RELEASE] << 8) | story[HDR_RELEASE + 1u];
    for (i = 0; i < MAP_MARKS_STORY_N; i++) {
        if (MAP_MARKS_STORIES[i].release != (unsigned short) release) continue;
        if (!serial_is(story + HDR_SERIAL, MAP_MARKS_STORIES[i].serial)) continue;
        g_marks = MAP_MARKS_STORIES[i].marks;
        g_n = (int) MAP_MARKS_STORIES[i].n;
        return g_n;
    }
    return 0;
}

/*----------------------
 | map_marks_for
 | Description: See map_marks.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_marks, g_n
 | Params: room -- object number; dir -- the exit direction; dest -- receives
 |   the drawing-supplied destination; flags -- receives MARK_BAGGAGE and
 |   MARK_RETRACT
 | Returns: 1 when the exit carries a mark, 0 otherwise
 ----------------------*/
int map_marks_for(unsigned short room, int dir, unsigned char *dest,
                   unsigned char *flags) {
    int i;
    if (g_marks == 0) return 0;
    for (i = 0; i < g_n; i++) {
        if (g_marks[i].room != (unsigned char) room) continue;
        if (g_marks[i].dir != (unsigned char) dir) continue;
        *dest = g_marks[i].dest;
        *flags = g_marks[i].flags;
        return 1;
    }
    return 0;
}
