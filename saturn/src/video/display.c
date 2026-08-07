/*----------------------
 | display.c
 | Description: The display-appearance model: the background/text color tables, the
 |   microcomputer palette presets, the per-category image slot arithmetic, and the
 |   cycling/selection logic behind Display Options, plus the save-blob encode/
 |   decode for persisting a chosen appearance. Pure model and data -- title.cxx
 |   loads the actual images and applies colors to VDP2.
 | Author: suinevere
 | Dependencies: display.h (DISP_* constants, DisplayState, DISP_RGB555)
 ----------------------*/
#include "display.h"
#include "sound/music.h"   /* TC_* / TEXT_NUM_CATEGORIES: the categories the art follows */
#include <string.h>        /* strcmp: parsing "MOOD/NN.TGA" back to a slot */

/*----------------------
 | BG_RGB / BG_NAME
 | Description: Background colors and their display names, in selector order. RGB
 |   values approximate the design spec's ANSI codes; ANSI collisions collapse to
 |   one entry (the three blues to \033[44m, the two whites to \033[47m).
 | Author: suinevere
 ----------------------*/
static const unsigned short BG_RGB[DISP_BG_COLOR_N] = {
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 40  */
    DISP_RGB555(0xFF, 0xB0, 0x00),   /* Glowing Amber  ANSI 43  */
    DISP_RGB555(0x00, 0x00, 0xAA),   /* Blue           ANSI 44  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* Light Gray     ANSI 47  */
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
 | Description: The wallpaper offsets the Display row steps through, brightest
 |   first, and their labels. Steps of 32 rather than a continuous slider: the
 |   picture is 8bpp, so a large offset clips distinct palette entries onto one
 |   value and posterises, and a stop the player can name is easier to return to
 |   than a position on a bar.
 | Author: suinevere
 ----------------------*/
static const short DIM_STOPS[DISP_DIM_N] = { 64, 32, 0, -32, -64, -96, -128 };
static const char *const DIM_NAMES[DISP_DIM_N] = {
    "Lighter +2", "Lighter +1", "Normal", "Darker -1",
    "Darker -2", "Darker -3", "Darker -4"
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
    if (index < 0 || index >= DISP_DIM_N) return DIM_NAMES[DISP_DIM_NORMAL];
    return DIM_NAMES[index];
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
 | CATEGORY_DIR / CATEGORY_ART_N
 | Description: The disc folder each text category's pictures live in, and how
 |   many it carries. The folder name is the only name a picture has now -- files
 |   inside it are a bare two-digit index -- so a slot is (category, index) rather
 |   than a position in a scanned list.
 |
 |   Row order is the TC_* enum order in music.h and has to stay that way. The
 |   three empty rows are TC_NEUTRAL, TC_DANGER and TC_TRIUMPH: a room that named
 |   nothing and a moment that is not a place both hold whatever is showing.
 |
 |   CATEGORY_ART_N is generated by tools/make_tga.py from the files that actually
 |   converted, so a count can never name a picture the disc lacks.
 | Author: suinevere
 ----------------------*/
static const char *const CATEGORY_DIR[TEXT_NUM_CATEGORIES] = {
    0, "WILDER", "UNDRGRND", "WATER", "NAUTICAL", "TOWN", "DUNGN",
    "DESERT", "MAGIC", "SCIFI", "HORROR", "MYSTERY", "HOUSE", 0, 0
};

#include "category_art.inc"

#define SLOT_STRIDE 100

/*----------------------
 | display_slot_make
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: cat -- the TC_* category; index -- 1-based, 1..the category's count
 | Returns: the slot, or DISP_IMAGE_NONE when the category or index is out of range
 ----------------------*/
int display_slot_make(int cat, int index) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return DISP_IMAGE_NONE;
    if (index < 1 || index > (int) CATEGORY_ART_N[cat]) return DISP_IMAGE_NONE;
    return cat * SLOT_STRIDE + index;
}

/*----------------------
 | display_slot_valid
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: 1 when the slot names a picture this disc carries, else 0
 ----------------------*/
int display_slot_valid(int slot) {
    if (slot < 0) return 0;
    return display_slot_make(slot / SLOT_STRIDE, slot % SLOT_STRIDE) == slot;
}

static int image_slot_of(const char *name);   /* defined with the save-blob helpers */

/*----------------------
 | g_dyn_slot
 | Description: The image slot the Dynamic palette is currently showing.
 | Author: suinevere
 ----------------------*/
static int g_dyn_slot = DISP_IMAGE_NONE;

/*----------------------
 | g_dyn_pin
 | Description: While set, the slot display_dynamic_slot hands back instead of
 |   g_dyn_slot. See display_pin_dynamic_slot.
 | Author: suinevere
 ----------------------*/
static int g_dyn_pin = DISP_IMAGE_NONE;

/*----------------------
 | display_set_dynamic_category
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dyn_slot
 | Params: cat -- the TC_* category to resolve
 | Returns: N/A
 ----------------------*/
void display_set_dynamic_category(int cat) {
    const char *file = display_category_image(cat);
    int slot;
    if (!file) return;                 /* no art for this mood: hold what is showing */
    slot = image_slot_of(file);
    if (slot >= 0) g_dyn_slot = slot;  /* absent from this disc: likewise hold */
}

/*----------------------
 | display_dynamic_slot
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dyn_slot, g_dyn_pin
 | Params: N/A
 | Returns: the Dynamic palette's image slot, or DISP_IMAGE_NONE
 ----------------------*/
int display_dynamic_slot(void) {
    if (display_slot_valid(g_dyn_pin)) return g_dyn_pin;
    if (display_slot_valid(g_dyn_slot)) return g_dyn_slot;
    return DISP_IMAGE_NONE;
}

/*----------------------
 | g_dyn_pin / display_pin_dynamic_slot
 | Description: An override that makes Dynamic resolve to one fixed slot while it
 |   is set, without disturbing g_dyn_slot -- so the mood the engine is tracking
 |   is still there, unchanged, when the pin comes off.
 |
 |   For screens that must not move the CD head. Resolving Dynamic normally can
 |   name a picture that is neither uploaded nor cached, and loading it reads the
 |   disc, which stops CD-DA mid-track. Pinning to the picture already on NBG0
 |   makes every display_apply() on that screen a short-circuit in title_bg_show.
 |   The Display options page is the caller: it is the one page under Options that
 |   applies a wallpaper, and this is what lets the menu track keep playing
 |   through it.
 |
 |   DISP_IMAGE_NONE, or any slot this disc does not carry, clears the pin.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_dyn_pin
 | Params: slot -- the slot to hold Dynamic at, or DISP_IMAGE_NONE to release
 | Returns: N/A
 ----------------------*/
void display_pin_dynamic_slot(int slot) {
    g_dyn_pin = display_slot_valid(slot) ? slot : DISP_IMAGE_NONE;
}

/*----------------------
 | display_image_slot
 | Description: The slot holding `name`, or DISP_IMAGE_NONE if this disc does not
 |   carry it. The public face of image_slot_of, for callers that hold a filename
 |   and need the slot the DisplayState stores.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: name -- the image filename; "" and NULL both miss
 | Returns: the slot index, or DISP_IMAGE_NONE
 ----------------------*/
int display_image_slot(const char *name) {
    int slot;
    if (!name) return DISP_IMAGE_NONE;
    slot = image_slot_of(name);
    return (slot >= 0) ? slot : DISP_IMAGE_NONE;
}

/*----------------------
 | display_image_count
 | Description: See display.h. Only ever compared against zero now -- the slot
 |   space is sparse, so this is not an upper bound and display_slot_valid is what
 |   answers "is this slot real".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: N/A
 | Returns: how many pictures the disc carries in total
 ----------------------*/
int display_image_count(void) {
    int cat, n = 0;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) n += (int) CATEGORY_ART_N[cat];
    return n;
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
 | g_file_buf
 | Description: Two rotating buffers for display_image_file, so one screen draw
 |   can hold two filenames at once -- the Palette row prints one while resolving
 |   another. "UNDRGRND/99.TGA" is 15 characters plus a terminator.
 | Author: suinevere
 ----------------------*/
static char g_file_buf[2][16];
static int  g_file_turn = 0;

/*----------------------
 | display_image_file
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_DIR, g_file_buf, g_file_turn
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: the disc path, or "" when the slot names no picture
 ----------------------*/
const char *display_image_file(int slot) {
    char *out;
    const char *dir;
    int index, k = 0;

    if (!display_slot_valid(slot)) return "";
    dir   = CATEGORY_DIR[slot / SLOT_STRIDE];
    index = slot % SLOT_STRIDE;

    out = g_file_buf[g_file_turn];
    g_file_turn ^= 1;

    while (*dir) out[k++] = *dir++;
    out[k++] = '/';
    out[k++] = (char) ('0' + index / 10);
    out[k++] = (char) ('0' + index % 10);
    out[k++] = '.'; out[k++] = 'T'; out[k++] = 'G'; out[k++] = 'A';
    out[k]   = '\0';
    return out;
}

/*----------------------
 | display_palette_count
 | Description: The palette row length: Dynamic, then the microcomputer presets.
 |   Fixed -- it does not grow with the art on the disc, because the row no longer
 |   lists pictures individually (see the DISP_PAL_* box in display.h).
 | Author: suinevere
 ----------------------*/
int display_palette_count(void) { return 1 + DISP_PRESET_N; }

/*----------------------
 | g_cat_rot
 | Description: Where each category's pool is currently pointing. Rotation is
 |   round-robin rather than random so a pool of two cannot hand back what is
 |   already showing, which is the whole ask; it also means leaving a mood and
 |   returning to it later resumes at a different picture instead of always
 |   opening on the same one.
 | Author: suinevere
 ----------------------*/
static unsigned char g_cat_rot[TEXT_NUM_CATEGORIES];

/*----------------------
 | display_category_image
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cat_rot, CATEGORY_ART_N
 | Params: cat -- the TC_* category
 | Returns: the pool's current filename, or NULL to hold what is showing
 ----------------------*/
const char *display_category_image(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    if (CATEGORY_ART_N[cat] == 0) return 0;
    return display_image_file(display_slot_make(
        cat, (int) (g_cat_rot[cat] % CATEGORY_ART_N[cat]) + 1));
}

/*----------------------
 | display_category_image_count
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_ART_N
 | Params: cat -- the TC_* category
 | Returns: how many pictures the category can draw on, 0 if none
 ----------------------*/
int display_category_image_count(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    return (int) CATEGORY_ART_N[cat];
}

/*----------------------
 | display_shuffle_category
 | Description: Points a category's pool at an arbitrary one of its pictures,
 |   chosen by `r`. Unlike display_rotate_dynamic_category this does NOT touch the
 |   showing slot -- it only moves where the pool is pointing, so the caller
 |   decides whether and when that becomes visible.
 |
 |   Exists for the title screen, which wants a different house each time it is
 |   reached rather than the same one every boot. Kept here rather than done by the
 |   caller because g_cat_rot is what display_category_image reads, and a caller
 |   picking its own filename would leave the pool pointing somewhere else -- the
 |   menu behind the title would then resolve TC_HOUSE to a different picture and
 |   jump the moment the title faded out.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cat_rot
 | Params: cat -- the TC_* category; r -- any value; reduced modulo the pool size
 | Returns: N/A
 ----------------------*/
void display_shuffle_category(int cat, unsigned int r) {
    int n;
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return;
    n = (int) CATEGORY_ART_N[cat];
    if (n <= 0) return;                       /* no pool: nothing to point at */
    g_cat_rot[cat] = (unsigned char)(r % (unsigned int) n);
}

/*----------------------
 | display_rotate_dynamic_category
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cat_rot, g_dyn_slot, CATEGORY_ART_N
 | Params: cat -- the TC_* category to advance
 | Returns: N/A
 ----------------------*/
void display_rotate_dynamic_category(int cat) {
    int n, i;
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return;
    n = (int) CATEGORY_ART_N[cat];
    if (n <= 1) return;          /* one picture (or none): nothing to rotate to */

    /* Walk the pool for the next entry that is not what is already showing. Every
       index 1..n is a real slot by construction now -- CATEGORY_ART_N is generated
       from the files that actually converted -- so a single step always lands on
       a valid picture; walking still guards against landing back on g_dyn_slot. */
    for (i = 0; i < n; i++) {
        int slot;
        g_cat_rot[cat] = (unsigned char)((g_cat_rot[cat] + 1) % n);
        slot = display_slot_make(cat, (int) g_cat_rot[cat] + 1);
        if (slot >= 0 && slot != g_dyn_slot) { g_dyn_slot = slot; return; }
    }
}

/*----------------------
 | display_preset_image
 | Description: The image slot a palette index maps to, or DISP_IMAGE_NONE for a
 |   color preset.
 | Author: suinevere
 ----------------------*/
int display_preset_image(int index) {
    if (index == DISP_PAL_DYNAMIC) return display_dynamic_slot();
    return DISP_IMAGE_NONE;
}

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
 | Description: Resets a DisplayState to the default appearance (IBM PC MDA, the
 |   closest match to the previous hardcoded look, with no image).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: PRESETS
 | Params: d -- the state to reset
 | Returns: N/A
 ----------------------*/
void display_defaults(DisplayState *d) {
    if (display_image_count() > 0) {
        /* Dynamic: the picture follows the room's mood. The shipped default. */
        d->palette = DISP_PAL_DYNAMIC;
        d->bg      = DISP_BG_BLACK;
        d->text    = DISP_TEXT_WHITE;
        d->image   = display_dynamic_slot();
        d->dim     = DISP_DIM_NORMAL;
    } else {
        /* No art on this disc, so Dynamic has nothing to show. */
        d->palette = DISP_PAL_PRESET0;         /* IBM PC (MDA): closest to the */
        d->bg      = PRESETS[0].bg;            /* previous hardcoded appearance */
        d->text    = PRESETS[0].text;
        d->image   = DISP_IMAGE_NONE;
        d->dim     = DISP_DIM_NORMAL;
    }
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
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
void display_cycle_dim(DisplayState *d, int dir) {
    d->dim = step(d->dim, dir, DISP_DIM_N);
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
 |   bg/text/image. Dynamic is stepped over on a disc with no art, since it would
 |   land the player on an entry that shows nothing.
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
    /* Dynamic holds index 0 on every disc, art or not, so the display_preset_*
       accessors above can stay single unconditional expressions. The cost is one
       unreachable entry to step past here, which is cheaper than the row changing
       shape -- that would put the arithmetic behind display_image_count() everywhere. */
    if (next == DISP_PAL_DYNAMIC && display_image_count() == 0)
        next = step(next, dir, count);
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
    d->image   = display_preset_image(next);
}

/*----------------------
 | image_slot_of
 | Description: The slot a disc path names, or -1. Parses the form
 |   display_image_file writes and nothing else, so a flat name from an older save
 |   blob misses -- which is the wanted answer, since the Palette row stopped
 |   honouring pinned pictures.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_DIR
 | Params: name -- a disc path; NULL and "" both miss
 | Returns: the slot, or -1
 ----------------------*/
static int image_slot_of(const char *name) {
    int cat, i, index;
    const char *dir, *p;

    if (!name || !name[0]) return -1;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        dir = CATEGORY_DIR[cat];
        if (!dir) continue;
        for (i = 0; dir[i] && name[i] == dir[i]; i++) { }
        if (dir[i] || name[i] != '/') continue;
        p = name + i + 1;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
        index = (p[0] - '0') * 10 + (p[1] - '0');
        if (strcmp(p + 2, ".TGA") != 0) return -1;
        return display_slot_make(cat, index);
    }
    return -1;
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
 | Description: Serializes a DisplayState into a save blob (sentinel 6 -- not 5,
 |   see DISP_BLOB_BYTES in display.h): palette, background, text, and dim bytes
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

    out[0] = 6;                                /* block sentinel: + dim */
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
 |   (sentinels 2/3/4/6): resolves the image by name, refusing one this disc lacks,
 |   and validates each field independently. Only sentinel 6 carries a dim byte;
 |   the older forms leave d->dim at the DISP_DIM_NORMAL display_defaults set.
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
    } else if (buf[0] == 6) {
        /* Current form: sentinel 2/3/4's layout plus a dim byte ahead of the
           name, and its own length -- DISP_BLOB_BYTES, not DISP_BLOB_BYTES_V4.
           Post-Dynamic numbering throughout, like sentinel 4, so the palette
           byte needs no shift. Sentinel is 6, not 5 -- see DISP_BLOB_BYTES in
           display.h for why 5 is reserved for options.cxx's gameplay block. */
        const char *name = (const char *) (buf + 5);
        int slot, n = 0;

        if (len < DISP_BLOB_BYTES) return 0;
        while (n < DISP_IMAGE_NAME_MAX && name[n]) n++;
        if (n >= DISP_IMAGE_NAME_MAX) return 0;   /* name never terminates */
        slot = image_slot_of(name);

        if (buf[1] == DISP_BLOB_DYNAMIC) {
            if (display_image_count() > 0) d->palette = DISP_PAL_DYNAMIC;  else ok = 0;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            if (display_image_count() > 0) d->palette = DISP_PAL_DYNAMIC;
            ok = 0;
        } else if (buf[1] < DISP_PRESET_N) {
            d->palette = (int) buf[1];
        } else ok = 0;

        if (buf[2] == DISP_BLOB_IMAGE) {
            d->bg = DISP_BG_BLACK;
            if (slot < 0) ok = 0;
        } else if (buf[2] < DISP_BG_COLOR_N) {
            d->bg = (int) buf[2];
        } else ok = 0;

        if (buf[3] < DISP_TEXT_N) d->text = (int) buf[3];  else ok = 0;
        if (buf[4] < DISP_DIM_N)  d->dim  = (int) buf[4];  else ok = 0;

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
            /* Only sentinel 4 writes this, and only from a disc that had art.
               Reloaded onto one that has none, Dynamic has nothing to show. */
            if (display_image_count() > 0) d->palette = DISP_PAL_DYNAMIC;  else ok = 0;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            /* A blob from when the row let one picture be pinned. It cannot be
               honoured -- there is no palette index for a single picture any more
               -- so it lands on Dynamic, the one entry that still shows art at
               all. Reported as not-verbatim either way: the player is getting
               something other than what they saved. */
            if (display_image_count() > 0) d->palette = DISP_PAL_DYNAMIC;
            ok = 0;
        } else if (buf[1] < DISP_PRESET_N) {
            /* Sentinels 2 and 3 predate Dynamic, so their colour-preset indices
               are in the old space, where 0 was the first preset rather than
               Dynamic -- they shift up one or every saved appearance silently
               moves one entry along the row. Sentinel 4 already stores the new
               index. Image presets are resolved by name and need no shift. */
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
