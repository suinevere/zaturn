/*----------------------
 | display.c
 | Description: The display-appearance model: the background/text color tables, the
 |   microcomputer palette presets, the per-scene image slot arithmetic, and the
 |   cycling/selection logic behind Display Options, plus the save-blob encode/
 |   decode for persisting a chosen appearance. Pure model and data -- title.cxx
 |   loads the actual images and applies colors to VDP2.
 | Author: suinevere
 | Dependencies: display.h (DISP_* constants, DisplayState, DISP_RGB555),
 ----------------------*/
#include "display.h"
#include <string.h>        /* strcmp: parsing "GAMEDIR/NN.TGA" back to a slot */

/*----------------------
 | BG_RGB / BG_NAME
 | Description: Background colors and their display names, in selector order. RGB
 |   values approximate the design spec's ANSI codes; ANSI collisions collapse to
 |   one entry (the three blues to \033[44m, the two whites to \033[47m).
 |
 |   Light Gray is the one deliberate departure. ANSI 47 is 0xAA, which on this
 |   screen sits close enough to Bright White that the two stops read as one --
 |   and the marble the menus are cut from follows the ground's brightness
 |   (dash_view.cxx's DASH_LEVEL_*), so at 0xAA it came out as pale as the white
 |   stop's. 0x77 is a grey the row can tell apart, and still light enough for
 |   the two presets that put dark text on it (VIC-20, ZX Spectrum).
 | Author: suinevere
 ----------------------*/
static const unsigned short BG_RGB[DISP_BG_COLOR_N] = {
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 40  */
    DISP_RGB555(0xFF, 0xB0, 0x00),   /* Glowing Amber  ANSI 43  */
    DISP_RGB555(0x00, 0x00, 0xAA),   /* Blue           ANSI 44  */
    DISP_RGB555(0x77, 0x77, 0x77),   /* Light Gray     ANSI 47, darkened */
    DISP_RGB555(0x55, 0xFF, 0xFF),   /* Bright Cyan    ANSI 106 */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 42  */
    DISP_RGB555(0xFF, 0xFF, 0xFF)    /* Bright White   ANSI 107 */
};

/*----------------------
 | BG_NAME
 | Description: Player-facing names for the background colours, in selector order.
 | Author: suinevere
 ----------------------*/
static const char *const BG_NAME[DISP_BG_COLOR_N] = {
    "Black", "Glowing Amber", "Blue", "Light Gray",
    "Bright Cyan", "Green", "Bright White"
};

/*----------------------
 | TEXT_RGB / TEXT_NAME
 | Description: Text colors and their names, in selector order. ANSI 37 is a light
 |   gray, named "Gray" here -- it makes the BBC Micro and MSX presets look right
 |   but is not what a player asking for white expects, so true white (ANSI 97) is
 |   its own entry appended last, keeping the indices already stored in save blobs.
 | Author: suinevere
 ----------------------*/
static const unsigned short TEXT_RGB[DISP_TEXT_N] = {
    DISP_RGB555(0xFF, 0xAF, 0x00),   /* Bright Amber   ANSI 38;5;214 */
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 30  */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 32  */
    DISP_RGB555(0x55, 0x55, 0xFF),   /* Light Blue     ANSI 94  */
    DISP_RGB555(0x00, 0xAA, 0xAA),   /* Cyan           ANSI 36  */
    DISP_RGB555(0xFF, 0xFF, 0x55),   /* Bright Yellow  ANSI 93  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* Gray           ANSI 37  */
    DISP_RGB555(0x55, 0xFF, 0x55),   /* Bright Green   ANSI 92  */
    DISP_RGB555(0xFF, 0xFF, 0xFF)    /* White          ANSI 97  */
};

/*----------------------
 | TEXT_NAME
 | Description: Player-facing names for the text colours, in selector order.
 | Author: suinevere
 ----------------------*/
static const char *const TEXT_NAME[DISP_TEXT_N] = {
    "Bright Amber", "Black", "Green", "Light Blue",
    "Cyan", "Bright Yellow", "Gray", "Bright Green", "White"
};

/*----------------------
 | DIM_STOPS / DIM_NAMES
 | Description: The wallpaper offsets the Display row steps through, darkest
 |   first, and their labels. Darkest first so that left steps darker and right
 |   steps brighter. Steps of 32 rather than a continuous slider: the picture is
 |   8bpp, so a large offset clips distinct palette entries onto one value and
 |   posterises, and a stop the player can name is easier to return to than a
 |   position on a bar.
 |
 |   The labels are the offset expressed in steps of 32 -- offset = 32 * label --
 |   so "0" is the hardware's zero, the picture as authored, and is both the
 |   middle of the row and the shipped default. Keep the two arrays in step with
 |   that arithmetic; test_display.c checks it.
 | Author: suinevere
 ----------------------*/
static const short DIM_STOPS[DISP_DIM_N] = { -64, -32, 0, 32, 64 };
static const char *const DIM_NAMES[DISP_DIM_N] = {
    "-2", "-1", "0", "+1", "+2"
};

/*----------------------
 | display_dim_offset
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: DIM_STOPS
 | Params: index -- 0..DISP_DIM_N-1
 | Returns: the signed offset, or 0 if index is out of range
 ----------------------*/
int display_dim_offset(int index) {
    if (index < 0 || index >= DISP_DIM_N) return 0;
    return (int) DIM_STOPS[index];
}

/*----------------------
 | display_dim_name
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: DIM_NAMES
 | Params: index -- 0..DISP_DIM_N-1
 | Returns: the label, or Normal's label if index is out of range
 ----------------------*/
const char *display_dim_name(int index) {
    if (index < 0 || index >= DISP_DIM_N) return DIM_NAMES[DISP_DIM_DEFAULT];
    return DIM_NAMES[index];
}

/*----------------------
 | dim_index_for_offset
 | Description: The current dim-row index whose stop is `off`, clamped into the
 |   row when `off` lies outside it. Every dim row this format has carried
 |   stepped by 32, so an offset identifies a stop exactly and a saved darkness
 |   survives a renumbering unchanged wherever the current row still reaches
 |   that far. This is how the old rows are read back rather than by index --
 |   see dim_index_v6 and dim_index_v8.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: off -- a signed VDP2 colour offset, a multiple of 32
 | Returns: 0..DISP_DIM_N-1
 ----------------------*/
static int dim_index_for_offset(int off) {
    int i = off / 32 + DISP_DIM_NORMAL;
    if (i < 0) return 0;
    if (i > DISP_DIM_N - 1) return DISP_DIM_N - 1;
    return i;
}

/*----------------------
 | dim_index_v6 / dim_index_v8
 | Description: The current dim-row index for a dim byte stored by a sentinel-6
 |   or sentinel-8 blob. Sentinel 6 indexed a brightest-first row running +64
 |   down to -128; sentinel 8 a darkest-first one running -160 up to +32. Both
 |   are translated by offset value, so a chosen darkness survives as far as the
 |   current -64..+64 row reaches and anything past either end clamps to it --
 |   sentinel 6's stops below -64 and sentinel 8's whole dark half land on the
 |   darkest stop the row still offers.
 | Author: suinevere
 | Dependencies: dim_index_for_offset
 | Globals: N/A
 | Params: v -- the stored byte, 0..6
 | Returns: 0..DISP_DIM_N-1, or DISP_DIM_DEFAULT if v is out of that range
 ----------------------*/
static int dim_index_v6(int v6) {
    if (v6 < 0 || v6 > 6) return DISP_DIM_DEFAULT;
    return dim_index_for_offset(64 - 32 * v6);
}

static int dim_index_v8(int v8) {
    if (v8 < 0 || v8 > 6) return DISP_DIM_DEFAULT;
    return dim_index_for_offset(-160 + 32 * v8);
}

/*----------------------
 | DisplayPreset / PRESETS
 | Description: The microcomputer presets (name + background + text index). Names
 |   are shortened where the full hardware name would overflow the selector field.
 |   Two deliberate duplicate combos (C64 == Atari 800, IBM PC MDA == Commodore
 |   PET) stay separate entries because the stored palette index -- not the color
 |   pair -- identifies the machine.
 | Author: suinevere
 ----------------------*/
typedef struct { const char *name; int bg; int text; } DisplayPreset;

static const DisplayPreset PRESETS[DISP_PRESET_N] = {
    { "IBM PC (MDA)",    DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Apple II Plus",   DISP_BG_BLACK,        DISP_TEXT_GREEN         },
    { "Toshiba T3100",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_AMBER  },
    { "BBC Micro",       DISP_BG_BLACK,        DISP_TEXT_GRAY          },
    { "Commodore PET",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "AmigaOS",         DISP_BG_BLUE,         DISP_TEXT_WHITE         },
    { "Commodore 64",    DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "Amstrad CPC 464", DISP_BG_BLUE,         DISP_TEXT_BRIGHT_YELLOW },
    { "MSX Standard",    DISP_BG_BLUE,         DISP_TEXT_GRAY          },
    { "Atari 800",       DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "VIC-20",          DISP_BG_LIGHT_GRAY,   DISP_TEXT_CYAN          },
    { "ZX Spectrum",     DISP_BG_LIGHT_GRAY,   DISP_TEXT_BLACK         },
    { "TRS-80 CoCo",     DISP_BG_GREEN,        DISP_TEXT_BLACK         },
    { "TI-99/4A",        DISP_BG_BRIGHT_CYAN,  DISP_TEXT_BLACK         },
    { "Mac Classic",     DISP_BG_BRIGHT_WHITE, DISP_TEXT_BLACK         },
    { "Monochrome P3",   DISP_BG_AMBER,        DISP_TEXT_BLACK         }
};

/*----------------------
 | g_custom_label
 | Description: The name shown when the current colors/image match no preset.
 | Author: suinevere
 ----------------------*/
static const char *const g_custom_label = "Custom";

/*----------------------
 | display_bg_rgb / display_text_rgb
 | Description: The RGB555 value for a background or text color index, falling back
 |   to a safe default on an out-of-range index.
 | Author: suinevere
 ----------------------*/
unsigned short display_bg_rgb(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) index = DISP_BG_BLACK;
    return BG_RGB[index];
}

unsigned short display_text_rgb(int index) {
    if (index < 0 || index >= DISP_TEXT_N) index = DISP_TEXT_BRIGHT_GREEN;
    return TEXT_RGB[index];
}

/*----------------------
 | display_bg_color_name / display_text_name
 | Description: The display name for a background or text color index ("?" if out
 |   of range).
 | Author: suinevere
 ----------------------*/
const char *display_bg_color_name(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) return "?";
    return BG_NAME[index];
}

const char *display_text_name(int index) {
    if (index < 0 || index >= DISP_TEXT_N) return "?";
    return TEXT_NAME[index];
}

/*----------------------
 | g_authored_art
 | Description: Whether the running game carries authored per-room art. Held as
 |   a plain flag rather than derived, because the table that answers it lives
 |   behind scene/presentation.h while this file is in the netbin build, which
 |   has no disc and no room art to describe.
 | Author: suinevere
 ----------------------*/
static int g_authored_art = 0;
/*----------------------
 | display_set_authored
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_authored_art
 | Params: has_authored -- non-zero when the game has authored per-room art
 | Returns: N/A
 ----------------------*/
void display_set_authored(int has_authored) {
    g_authored_art = has_authored ? 1 : 0;
}

/*----------------------
 | display_has_art
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_authored_art
 | Params: N/A
 | Returns: non-zero when the running game can show a picture by either route
 ----------------------*/
int display_has_art(void) {
    return g_authored_art;
}
/*----------------------
 | display_dynamic_slot
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via display_has_art)
 | Params: N/A
 | Returns: DISP_IMAGE_ROOM when this game has art, DISP_IMAGE_NONE otherwise
 ----------------------*/
int display_dynamic_slot(void) {
    return display_has_art() ? DISP_IMAGE_ROOM : DISP_IMAGE_NONE;
}

/*----------------------
 | display_slot_valid
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- an image value
 | Returns: 1 when slot names a picture this disc can show
 ----------------------*/
int display_slot_valid(int slot) {
    return slot == DISP_IMAGE_ROOM && display_has_art();
}

/*----------------------
 | display_preset_image
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- a palette index
 | Returns: the image value that palette entry implies
 ----------------------*/
int display_preset_image(int index) {
    return index == DISP_PAL_DYNAMIC ? display_dynamic_slot() : DISP_IMAGE_NONE;
}

/*----------------------
 | image_slot_of
 | Description: Resolves a picture name saved in an options blob back to an
 |   image value. Always refuses now, and deliberately still exists.
 |
 |   Only the retired scene-art pipeline ever wrote a name here -- paths like
 |   "UNDRGRND/99.TGA" naming one of a game's downloaded pictures. Those
 |   pictures are gone, so no saved name can be honoured. Refusing is not a
 |   failure mode the decoder has to learn: it already treats an unresolvable
 |   name as "not verbatim" and falls back to Dynamic or to the preset's own
 |   colours, which is exactly the right outcome for a save asking for a
 |   picture the disc no longer carries. Deleting this instead would have meant
 |   rewriting three sentinel branches of display_decode and re-proving the
 |   save-format compatibility they encode.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: name -- the NUL-terminated name from the blob
 | Returns: DISP_IMAGE_NONE, always
 ----------------------*/
static int image_slot_of(const char *name) {
    (void) name;
    return DISP_IMAGE_NONE;
}


/*----------------------
 | display_bg_count
 | Description: The number of selectable background colors. The Background row
 |   offers colors only; pictures are chosen from the System Palette row, each
 |   paired with black background + white text so menu text stays legible.
 | Author: suinevere
 ----------------------*/
int display_bg_count(void)    { return DISP_BG_COLOR_N; }

/*----------------------
 | display_palette_count
 | Description: The palette row length: Dynamic, then the microcomputer presets.
 |   Fixed -- it does not grow with the art on the disc, because the row no longer
 |   lists pictures individually (see the DISP_PAL_* box in display.h).
 | Author: suinevere
 ----------------------*/
int display_palette_count(void) { return 1 + DISP_PRESET_N; }
/*----------------------
 | display_preset_name
 | Description: The name for a palette index -- Dynamic, or a machine's name ("?"
 |   if out of range).
 | Author: suinevere
 ----------------------*/
const char *display_preset_name(int index) {
    if (index < 0 || index >= display_palette_count()) return "?";
    if (index == DISP_PAL_DYNAMIC) return "Dynamic";
    return PRESETS[index - DISP_PAL_PRESET0].name;
}

/*----------------------
 | display_preset_bg / display_preset_text
 | Description: The background and text color for a palette index. Dynamic puts its
 |   picture over black with white text -- black so the frames and letterboxing read
 |   as deliberate, white so menu text stays legible over the art.
 | Author: suinevere
 ----------------------*/
int display_preset_bg(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_BG_BLACK;
    if (index == DISP_PAL_DYNAMIC) return DISP_BG_BLACK;
    return PRESETS[index - DISP_PAL_PRESET0].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_TEXT_BRIGHT_GREEN;
    if (index == DISP_PAL_DYNAMIC) return DISP_TEXT_WHITE;
    return PRESETS[index - DISP_PAL_PRESET0].text;
}

/*----------------------
 | display_palette_name
 | Description: The current palette's name if the state still matches that preset's
 |   bg/text/image exactly, otherwise "Custom".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the display state
 | Returns: the preset name or the custom label
 ----------------------*/
const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < display_palette_count()
        && d->bg    == display_preset_bg(d->palette)
        && d->text  == display_preset_text(d->palette)
        && d->image == display_preset_image(d->palette)) {
        return display_preset_name(d->palette);
    }
    return g_custom_label;
}

/*----------------------
 | display_defaults
 | Description: Resets a DisplayState to the default appearance: Dynamic, the
 |   picture over black with white text.
 |
 |   Unconditionally Dynamic, where this used to fall back to IBM PC (MDA)
 |   whenever display_has_art() read 0. That condition was answered at the wrong
 |   time far more often than at the right one: no game is selected at the title
 |   or in the menus, so the flag is 0 there and a cold boot defaulted to MDA and
 |   only offered Dynamic once a game with art was already running. Dynamic on a
 |   game with no art is not a broken state -- it is black with white text and no
 |   picture, which is a legitimate appearance and the one this port looks like
 |   with the wallpaper off.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to reset
 | Returns: N/A
 ----------------------*/
void display_defaults(DisplayState *d) {
    d->palette = DISP_PAL_DYNAMIC;
    d->bg      = DISP_BG_BLACK;   /* black so frames and letterboxing read as */
    d->text    = DISP_TEXT_WHITE; /* deliberate; white to stay legible on art */
    d->image   = display_dynamic_slot();
    d->dim     = DISP_DIM_DEFAULT;
}

/*----------------------
 | display_is_image
 | Description: True when the state's image index refers to a present image slot.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the display state
 | Returns: 1 when d->image names a picture this disc carries, else 0
 ----------------------*/
int display_is_image(const DisplayState *d) {
    return display_slot_valid(d->image);
}

/*----------------------
 | display_bg_name
 | Description: The current background color's display name.
 | Author: suinevere
 ----------------------*/
const char *display_bg_name(const DisplayState *d) {
    return display_bg_color_name(d->bg);
}

/*----------------------
 | clashes
 | Description: True when a background/text pairing would render text invisible
 |   (identical colors).
 | Author: suinevere
 ----------------------*/
static int clashes(int bg, int text) {
    return display_bg_rgb(bg) == display_text_rgb(text);
}

/*----------------------
 | step
 | Description: Advances `value` by `dir` with wraparound over [0, count).
 | Author: suinevere
 ----------------------*/
static int step(int value, int dir, int count) {
    value += dir;
    if (value >= count) value = 0;
    if (value < 0)      value = count - 1;
    return value;
}

/*----------------------
 | display_cycle_dim
 | Description: See display.h. Clamps where every other row wraps: this one is a
 |   brightness slider with a dark end and a bright end, and a press past either
 |   that jumped to the far side would be read as the control malfunctioning.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
void display_cycle_dim(DisplayState *d, int dir) {
    int v = d->dim + dir;
    if (v < 0)             v = 0;
    if (v > DISP_DIM_N - 1) v = DISP_DIM_N - 1;
    d->dim = v;
}

/*----------------------
 | display_cycle_bg / display_cycle_text
 | Description: Cycle the background or text color one step in `dir`, skipping past
 |   any choice that would clash (invisible text) against the other, bounded so it
 |   cannot loop forever.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
void display_cycle_bg(DisplayState *d, int dir) {
    int count = display_bg_count();
    int next  = step(d->bg, dir, count);
    int tries = count;
    while (clashes(next, d->text) && tries-- > 0) next = step(next, dir, count);
    d->bg = next;
}

void display_cycle_text(DisplayState *d, int dir) {
    int next  = step(d->text, dir, DISP_TEXT_N);
    int tries = DISP_TEXT_N;
    while (clashes(d->bg, next) && tries-- > 0) next = step(next, dir, DISP_TEXT_N);
    d->text = next;
}

/*----------------------
 | display_cycle_palette
 | Description: Cycle the palette one step in `dir`, applying the new preset's
 |   bg/text/image. Every entry is reachable, Dynamic included.
 |
 |   Dynamic used to be stepped over whenever display_has_art() read 0, on the
 |   grounds that it would land the player on an entry showing nothing. The flag
 |   is 0 for every game without authored art AND everywhere outside a running
 |   game, so what that actually did was remove the row's first entry from the
 |   Options menu at the title screen -- the one place a player is most likely to
 |   go looking for it. Landing on it there is correct: it is the appearance the
 |   port defaults to, and a game with art will honour it the moment one starts.
 |
 |   Custom is not a position on the row and never was -- it is what
 |   display_palette_name reports when the colours have been edited away from the
 |   palette they came from. d->palette still holds that base palette throughout,
 |   so cycling out of Custom is the same plain step as any other, taken from the
 |   base. It used to re-enter at an end of the row instead, which meant a custom
 |   built on Dynamic (index 0) answered a forward press by selecting Dynamic
 |   again: the player's colours were wiped back to black and white and the row had
 |   not moved. Stepping from the base leaves Custom behind on the first press in
 |   either direction, which is the whole of what it should do.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
void display_cycle_palette(DisplayState *d, int dir) {
    int count = display_palette_count();
    int next  = step(d->palette, dir, count);
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
    d->image   = display_preset_image(next);
}
/*----------------------
 | DISP_BLOB_IMAGE
 | Description: The palette/bg marker byte in a save blob meaning "the image named
 |   below", distinguishing an image from a plain color index.
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_IMAGE 0xFF

/*----------------------
 | DISP_BLOB_DYNAMIC
 | Description: The palette byte's marker for the Dynamic palette, which carries no
 |   picture name because the picture is chosen at runtime -- storing the one that
 |   happened to be showing would only invite a later load to treat it as a
 |   requirement. Distinct from DISP_BLOB_IMAGE (0xFF), which does carry one.
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_DYNAMIC 0xFE

/*----------------------
 | DISP_BLOB_BYTES_V4
 | Description: The block size of save forms 2, 3 and 4 -- four header bytes plus
 |   the name. Frozen, and named rather than spelled 17, because DISP_BLOB_BYTES
 |   now describes form 6 and the two must not be confused: measuring an old blob
 |   against the new size rejects every save file already on a card.
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_BYTES_V4 (4 + DISP_IMAGE_NAME_MAX)

/*----------------------
 | display_encode
 | Description: Serializes a DisplayState into a save blob (sentinel 9 -- not 5
 |   or 7, see DISP_BLOB_BYTES in display.h): palette, background, text and dim bytes
 |   (Dynamic is marked DISP_BLOB_DYNAMIC). The name field is always written
 |   empty now -- see the comment below.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state; out -- the blob buffer
 | Returns: DISP_BLOB_BYTES (the bytes written)
 ----------------------*/
int display_encode(const DisplayState *d, unsigned char *out) {
    int i;

    out[0] = 9;                                /* block sentinel: renumbered dim row */
    out[1] = (d->palette == DISP_PAL_DYNAMIC) ? DISP_BLOB_DYNAMIC
           : (unsigned char) d->palette;
    out[2] = (unsigned char) d->bg;            /* always a color now */
    out[3] = (unsigned char) d->text;
    out[4] = (unsigned char) d->dim;

    /* The name field is frozen at its size for save-format compatibility and is
       no longer populated: no UI path can produce palette != Dynamic with
       image >= 0 (the Palette row offers Dynamic plus colour presets and
       nothing else, and a colour preset carries DISP_IMAGE_NONE), and even a
       hand-built state's synthesised "MOOD/NN.TGA" path does not fit the field
       -- it would truncate to something image_slot_of then rejects on the way
       back in, which corrupts silently rather than failing clean. If
       per-picture pinning ever returns, it needs a wider field and a new
       sentinel regardless, not this one repurposed. */
    for (i = 0; i < DISP_IMAGE_NAME_MAX; i++) out[5 + i] = 0;
    return DISP_BLOB_BYTES;
}

/*----------------------
 | display_decode
 | Description: Restores a DisplayState from a save blob, defaulting first so a bad
 |   blob leaves a sane state. Handles the original slot-only form (sentinel 1,
 |   image slots refused since the picture cannot be trusted), and the named forms
 |   (sentinels 2/3/4/6/8/9): resolves the image by name, refusing one this disc lacks,
 |   and validates each field independently. Only sentinels 6, 8 and 9 carry a dim
 |   byte -- 6's and 8's index the two earlier rows and are translated by
 |   dim_index_v6/dim_index_v8 -- and the older forms leave d->dim at the
 |   DISP_DIM_DEFAULT display_defaults set.
 |
 |   Sentinels 1, 2 and 3 were written before Dynamic took palette index 0, so a
 |   colour-preset index stored in them is one lower than it should be now and is
 |   shifted on read. Getting that wrong is invisible until a player reboots and
 |   finds their appearance moved one entry along the row, which is why
 |   test_display.c pins both directions.
 |
 |   A picture that had been hiding a
 |   background/text clash can be gone from this disc, so a final clash check
 |   restores both colors from the (legible) preset. Returns whether everything was
 |   accepted verbatim.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via image_slot_of / display_preset_*)
 | Params: buf -- the blob; len -- its length; d -- the state to fill
 | Returns: 1 if fully accepted, 0 if anything was defaulted/refused
 ----------------------*/
int display_decode(const unsigned char *buf, int len, DisplayState *d) {
    int ok = 1;
    display_defaults(d);

    if (!buf || len < 4) return 0;

    if (buf[0] == 1) {
        /* Original form: slot numbers, no name. Colors still mean what they said,
           but an image slot cannot be trusted, so it is refused not guessed. The
           palette index predates Dynamic taking index 0, so it shifts up one --
           see the sentinel-4 note below. */
        if (buf[1] < DISP_PRESET_N) {
            d->palette = (int) buf[1] + DISP_PAL_PRESET0;
            /* The defaults this started from are Dynamic, which carries a
               picture. A colour preset does not, and this form cannot name one,
               so the inherited image has to go -- otherwise the state reads as
               Custom and shows a wallpaper the blob never asked for. Refused
               indices fall through and keep the defaults whole. */
            d->image = DISP_IMAGE_NONE;
        } else ok = 0;
        if (buf[2] < DISP_BG_COLOR_N) d->bg      = (int) buf[2];  else ok = 0;
        if (buf[3] < DISP_TEXT_N)     d->text    = (int) buf[3];  else ok = 0;
    } else if (buf[0] == 6 || buf[0] == 8 || buf[0] == 9) {
        /* Current form: sentinel 2/3/4's layout plus a dim byte ahead of the
           name, and its own length -- DISP_BLOB_BYTES, not DISP_BLOB_BYTES_V4.
           Post-Dynamic numbering throughout, like sentinel 4, so the palette
           byte needs no shift. Sentinel is 9, not 5 or 7 -- see DISP_BLOB_BYTES
           in display.h for why both are reserved for options.cxx's gameplay
           block. 6 and 8 are the same block with the two earlier dim rows and
           differ only in that byte, which dim_index_v6/v8 translate below. */
        const char *name = (const char *) (buf + 5);
        int slot, n = 0;

        if (len < DISP_BLOB_BYTES) return 0;
        while (n < DISP_IMAGE_NAME_MAX && name[n]) n++;
        if (n >= DISP_IMAGE_NAME_MAX) return 0;   /* name never terminates */
        slot = image_slot_of(name);

        if (buf[1] == DISP_BLOB_DYNAMIC) {
            /* Accepted whatever display_has_art() says. It used to be refused
               when that read 0, which is the answer at the title screen where
               options are loaded -- so a player who chose Dynamic found it
               silently reset to a colour preset on the next boot. */
            d->palette = DISP_PAL_DYNAMIC;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            d->palette = DISP_PAL_DYNAMIC;
            ok = 0;
        } else if (buf[1] <= DISP_PRESET_N) {
            /* Post-Dynamic indices run DISP_PAL_PRESET0 (1) through DISP_PRESET_N
               (16) inclusive -- <= here, not <, or the last preset (Monochrome
               P3) fails this check and silently decodes to Dynamic instead. */
            d->palette = (int) buf[1];
        } else ok = 0;

        if (buf[2] == DISP_BLOB_IMAGE) {
            d->bg = DISP_BG_BLACK;
            if (slot < 0) ok = 0;
        } else if (buf[2] < DISP_BG_COLOR_N) {
            d->bg = (int) buf[2];
        } else ok = 0;

        if (buf[3] < DISP_TEXT_N) d->text = (int) buf[3];  else ok = 0;
        if (buf[0] == 6) {
            if (buf[4] <= 6) d->dim = dim_index_v6((int) buf[4]);  else ok = 0;
        } else if (buf[0] == 8) {
            if (buf[4] <= 6) d->dim = dim_index_v8((int) buf[4]);  else ok = 0;
        } else if (buf[4] < DISP_DIM_N) {
            d->dim = (int) buf[4];
        } else ok = 0;

        if (name[0]) {
            if (slot >= 0) d->image = slot;  else ok = 0;
        }
        if (d->palette == DISP_PAL_DYNAMIC) d->image = display_dynamic_slot();
        else if (!name[0])                  d->image = DISP_IMAGE_NONE;
    } else if (buf[0] == 2 || buf[0] == 3 || buf[0] == 4) {
        const char *name = (const char *) (buf + 4);
        int slot, n = 0;

        if (len < DISP_BLOB_BYTES_V4) return 0;
        while (n < DISP_IMAGE_NAME_MAX && name[n]) n++;
        if (n >= DISP_IMAGE_NAME_MAX) return 0;   /* name never terminates */
        slot = image_slot_of(name);

        if (buf[1] == DISP_BLOB_DYNAMIC) {
            /* Only sentinel 4 writes this. Accepted unconditionally, same as the
               current form above: Dynamic is a valid appearance on a disc with no
               art -- black, white text, no picture -- not an unshowable one. */
            d->palette = DISP_PAL_DYNAMIC;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            /* A blob from when the row let one picture be pinned. It cannot be
               honoured -- there is no palette index for a single picture any more
               -- so it lands on Dynamic, the one entry that still shows art at
               all. Reported as not-verbatim either way: the player is getting
               something other than what they saved. */
            d->palette = DISP_PAL_DYNAMIC;
            ok = 0;
        } else if (buf[0] == 4
                       ? (buf[1] >= DISP_PAL_PRESET0 && buf[1] <= DISP_PRESET_N)
                       : (buf[1] < DISP_PRESET_N)) {
            /* Sentinels 2 and 3 predate Dynamic, so their colour-preset indices
               are in the OLD space, 0..DISP_PRESET_N-1, where 0 was the first
               preset rather than Dynamic -- they shift up one or every saved
               appearance silently moves one entry along the row. Sentinel 4
               already stores the NEW-space index, DISP_PAL_PRESET0..
               DISP_PRESET_N inclusive, same as sentinel 6 -- <= here, not <,
               or the last preset (Monochrome P3) fails this check and silently
               decodes to Dynamic instead. The two groups need different bounds,
               not one shared test. Image presets are resolved by name and need
               no shift. */
            d->palette = (int) buf[1] + ((buf[0] == 4) ? 0 : DISP_PAL_PRESET0);
        } else ok = 0;

        if (buf[2] == DISP_BLOB_IMAGE) {
            /* Sentinel-2 packed the image into bg, so the color beneath it was
               never stored. Black is what an image preset pairs with. */
            d->bg = DISP_BG_BLACK;
            if (slot < 0) ok = 0;
        } else if (buf[2] < DISP_BG_COLOR_N) {
            d->bg = (int) buf[2];
        } else ok = 0;

        if (buf[3] < DISP_TEXT_N) d->text = (int) buf[3];  else ok = 0;

        /* A name present means a picture was showing; drop it, reporting the loss,
           when this disc no longer carries it. */
        if (name[0]) {
            if (slot >= 0) d->image = slot;  else ok = 0;
        }
        /* Dynamic stored no name, so its picture comes from the engine's current
           category rather than the blob -- whatever room the player resumes in
           decides it, which is the whole point of the setting. Anything else
           that named no picture is not asking for one, and has to shed the image
           the defaults came with, or a plain colour preset would decode to that
           preset wearing a wallpaper and read as Custom. */
        if (d->palette == DISP_PAL_DYNAMIC) d->image = display_dynamic_slot();
        else if (!name[0])                  d->image = DISP_IMAGE_NONE;
    } else {
        return 0;
    }

    /* Fields were validated independently, so an accepted bg can still match an
       accepted text -- most easily when a picture that was hiding the pairing is
       gone. d->palette is a real preset index here and every preset pair is
       legible, so restore both from it. */
    if (clashes(d->bg, d->text)) {
        d->bg   = display_preset_bg(d->palette);
        d->text = display_preset_text(d->palette);
        ok = 0;
    }

    return ok;
}
