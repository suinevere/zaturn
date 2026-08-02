/*----------------------
 | display.c
 | Description: The display-appearance model: the background/text color tables, the
 |   microcomputer palette presets, the registered background-image list, and the
 |   cycling/selection logic behind Display Options, plus the save-blob encode/
 |   decode for persisting a chosen appearance. Pure model and data -- title.cxx
 |   loads the actual images and applies colors to VDP2.
 | Author: suinevere
 | Dependencies: display.h (DISP_* constants, DisplayState, DISP_RGB555)
 ----------------------*/
#include "display.h"
#include "sound/music.h"   /* TC_* / TEXT_NUM_CATEGORIES: the categories the art follows */

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
 | g_image_names / g_image_count
 | Description: The registered background-image list (borrowed from title.cxx) and
 |   its length; the palette row appends one entry per image.
 | Author: suinevere
 ----------------------*/
static const char *const *g_image_names = 0;
static int g_image_count = 0;

static int image_slot_of(const char *name);   /* defined with the save-blob helpers */

/*----------------------
 | g_dyn_slot
 | Description: The image slot the Dynamic palette is currently showing. Seeded by
 |   display_set_images from TC_HOUSE's pool -- which is also where the title
 |   screen's picture comes from -- so Dynamic has something to show before any
 |   room has been classified, and shows the same thing the title just did.
 | Author: suinevere
 ----------------------*/
static int g_dyn_slot = DISP_IMAGE_NONE;

/*----------------------
 | display_set_images
 | Description: Registers the background-image name list (or clears it), capped at
 |   DISP_IMAGE_MAX, and re-seeds the Dynamic palette's slot -- that slot indexes
 |   the very list being replaced, so it cannot survive a rescan unresolved.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_image_names, g_image_count, g_dyn_slot
 | Params: names -- the image name array; count -- its length
 | Returns: N/A
 ----------------------*/
void display_set_images(const char *const *names, int count) {
    if (!names || count <= 0) {
        g_image_names = 0; g_image_count = 0; g_dyn_slot = DISP_IMAGE_NONE;
        return;
    }
    if (count > DISP_IMAGE_MAX) count = DISP_IMAGE_MAX;
    g_image_names = names;
    g_image_count = count;
    /* Seed from the house pool, falling back to whatever is first. TC_HOUSE and
       not TC_NEUTRAL: neutral means "hold what is showing", and at boot there is
       nothing yet to hold. The house pool is also where main.cxx takes the title
       picture from, so Dynamic opens on what the title screen was already showing
       rather than cutting to something else on the first frame of play.
       The fallback is what makes display_dynamic_slot's promise true -- it returns
       DISP_IMAGE_NONE only on a disc with NO art. Without it, a disc carrying
       art but not that pool's first picture would make Dynamic the
       default and then show
       nothing under it, with display_is_image false and the loading screen's
       wallpaper hide/restore quietly skipped. */
    g_dyn_slot = image_slot_of(display_category_image(TC_HOUSE));
    if (g_dyn_slot < 0) g_dyn_slot = 0;
}

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
 | Globals: g_dyn_slot, g_image_count
 | Params: N/A
 | Returns: the Dynamic palette's image slot, or DISP_IMAGE_NONE
 ----------------------*/
int display_dynamic_slot(void) {
    if (g_dyn_slot >= 0 && g_dyn_slot < g_image_count) return g_dyn_slot;
    return DISP_IMAGE_NONE;
}

/*----------------------
 | display_image_count
 | Description: The number of registered background images.
 | Author: suinevere
 ----------------------*/
int display_image_count(void) { return g_image_count; }

/*----------------------
 | display_bg_count
 | Description: The number of selectable background colors. The Background row
 |   offers colors only; pictures are chosen from the System Palette row, each
 |   paired with black background + white text so menu text stays legible.
 | Author: suinevere
 ----------------------*/
int display_bg_count(void)    { return DISP_BG_COLOR_N; }

/*----------------------
 | display_image_file
 | Description: The disc filename for an image slot, or "" if out of range.
 | Author: suinevere
 ----------------------*/
const char *display_image_file(int slot) {
    if (!g_image_names || slot < 0 || slot >= g_image_count) return "";
    return g_image_names[slot] ? g_image_names[slot] : "";
}

/*----------------------
 | display_image_label
 | Description: A display label for an image slot: the filename without extension,
 |   first letter upper, rest lower. Uses two rotating buffers so a single screen
 |   draw can hold two labels at once (the Palette row prints one while
 |   display_palette_name resolves another).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via display_image_file)
 | Params: slot -- the image slot
 | Returns: the formatted label (in a rotating static buffer)
 ----------------------*/
const char *display_image_label(int slot) {
    static char ring[2][DISP_IMAGE_NAME_MAX];
    static int turn = 0;
    const char *src = display_image_file(slot);
    char *out = ring[turn];
    int i = 0;

    turn = (turn + 1) & 1;
    for (; src[i] && src[i] != '.' && i < DISP_IMAGE_NAME_MAX - 1; i++) {
        char c = src[i];
        if (i == 0) { if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A'); }
        else        { if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a'); }
        out[i] = c;
    }
    out[i] = '\0';
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
 | CATEGORY_IMAGE
 | Description: Which pictures each text category can show. One pool per place
 |   category, three or four deep, and no picture appears in two pools -- a mood
 |   rotating onto art the player has just been looking at somewhere else reads as
 |   the rotation being broken, so the pools are disjoint by construction rather
 |   than by care.
 |
 |   The names are not free-form. Each picture is filed under tools/assets/png/<DIR>/
 |   and built to <DIR truncated to 7 chars><1..N>.TGA, so the pool a file belongs
 |   to is legible from its name alone on the disc, where the folders are gone (the
 |   ISO is flat). NAUTICAL and UNDRGRND are already eight characters, which is the
 |   whole ISO9660 8.3 stem with no room left for the index, hence the seven-letter
 |   NAUTICA and UNDRGRN.
 |
 |   The two event categories are NULL on purpose; see display_category_image.
 |   Indexed by TC_*, so the row order is the enum order in music.h and has to stay
 |   that way. saturn/tests/test_category_art.py checks the names are actually on
 |   the disc and that the pools stay disjoint, because a name with no file behind
 |   it shows nothing at runtime: the wallpaper just holds, and that mood silently
 |   never gets its art.
 | Author: suinevere
 ----------------------*/
static const char *const IMG_HOUSE[]       = { "HOUSE1.TGA", "HOUSE2.TGA", "HOUSE3.TGA" };
static const char *const IMG_WILDERNESS[]  = { "WILDER1.TGA", "WILDER2.TGA", "WILDER3.TGA" };
static const char *const IMG_UNDERGROUND[] = { "UNDRGRN1.TGA", "UNDRGRN2.TGA", "UNDRGRN3.TGA" };
static const char *const IMG_WATER[]       = { "WATER1.TGA", "WATER2.TGA", "WATER3.TGA" };
static const char *const IMG_NAUTICAL[]    = { "NAUTICA1.TGA", "NAUTICA2.TGA", "NAUTICA3.TGA" };
static const char *const IMG_TOWN[]        = { "TOWN1.TGA", "TOWN2.TGA", "TOWN3.TGA" };
static const char *const IMG_DUNGEON[]     = { "DUNGN1.TGA", "DUNGN2.TGA", "DUNGN3.TGA" };
static const char *const IMG_DESERT[]      = { "DESERT1.TGA", "DESERT2.TGA", "DESERT3.TGA" };
static const char *const IMG_MAGIC[]       = { "MAGIC1.TGA", "MAGIC2.TGA", "MAGIC3.TGA" };
static const char *const IMG_SCIFI[]       = { "SCIFI1.TGA", "SCIFI2.TGA", "SCIFI3.TGA" };
static const char *const IMG_HORROR[]      = { "HORROR1.TGA", "HORROR2.TGA", "HORROR3.TGA",
                                               "HORROR4.TGA" };
static const char *const IMG_MYSTERY[]     = { "MYSTERY1.TGA", "MYSTERY2.TGA", "MYSTERY3.TGA" };

/* Row order is the TC_* enum order and has to stay that way -- the category id is
   the index. Three rows carry no pool, and each is a deliberate "hold whatever is
   showing" rather than an omission:
     TC_NEUTRAL           the nothing-matched answer. A room that named nothing has
                          said nothing about how it looks, so the honest thing is to
                          keep the picture the player already has rather than cut to
                          an arbitrary one. It has no folder of its own; if you want
                          it to, add tools/assets/png/NEUTRAL/ and a pool here.
     TC_DANGER/TC_TRIUMPH moments rather than places, so the music shifts for them
                          while the wallpaper stays on the room. */
/*----------------------
 | IMGS / CATEGORY_IMAGE
 | Description: Pairs each pool array with its length into the per-category table,
 |   indexed by TC_* id.
 | Author: suinevere
 ----------------------*/
#define IMGS(a) { a, (unsigned char)(sizeof(a) / sizeof((a)[0])) }
static const struct { const char *const *p; unsigned char n; }
CATEGORY_IMAGE[TEXT_NUM_CATEGORIES] = {
    /* TC_NEUTRAL */ { 0, 0 },
    IMGS(IMG_WILDERNESS), IMGS(IMG_UNDERGROUND),
    IMGS(IMG_WATER), IMGS(IMG_NAUTICAL), IMGS(IMG_TOWN), IMGS(IMG_DUNGEON),
    IMGS(IMG_DESERT), IMGS(IMG_MAGIC), IMGS(IMG_SCIFI), IMGS(IMG_HORROR),
    IMGS(IMG_MYSTERY), IMGS(IMG_HOUSE),
    /* TC_DANGER  */ { 0, 0 },
    /* TC_TRIUMPH */ { 0, 0 }
};

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
 | Globals: g_cat_rot, CATEGORY_IMAGE
 | Params: cat -- the TC_* category
 | Returns: the pool's current filename, or NULL to hold what is showing
 ----------------------*/
const char *display_category_image(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    if (CATEGORY_IMAGE[cat].n == 0) return 0;
    return CATEGORY_IMAGE[cat].p[g_cat_rot[cat] % CATEGORY_IMAGE[cat].n];
}

/*----------------------
 | display_category_image_count
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_IMAGE
 | Params: cat -- the TC_* category
 | Returns: how many pictures the category can draw on, 0 if none
 ----------------------*/
int display_category_image_count(int cat) {
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return 0;
    return CATEGORY_IMAGE[cat].n;
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
    n = CATEGORY_IMAGE[cat].n;
    if (n <= 0) return;                       /* no pool: nothing to point at */
    g_cat_rot[cat] = (unsigned char)(r % (unsigned int) n);
}

/*----------------------
 | display_rotate_dynamic_category
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cat_rot, g_dyn_slot
 | Params: cat -- the TC_* category to advance
 | Returns: N/A
 ----------------------*/
void display_rotate_dynamic_category(int cat) {
    int n, i;
    if (cat < 0 || cat >= TEXT_NUM_CATEGORIES) return;
    n = CATEGORY_IMAGE[cat].n;
    if (n <= 1) return;          /* one picture (or none): nothing to rotate to */

    /* Walk the pool for the next entry that is BOTH on this disc and not what is
       already showing. Walking rather than stepping once matters on a disc
       missing some of the art: a single step could land on a name that resolves
       to nothing and leave the picture unchanged, which reads as the rotation
       silently not working. */
    for (i = 0; i < n; i++) {
        int slot;
        g_cat_rot[cat] = (unsigned char)((g_cat_rot[cat] + 1) % n);
        slot = image_slot_of(CATEGORY_IMAGE[cat].p[g_cat_rot[cat]]);
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
    if (g_image_count > 0) {
        /* Dynamic: the picture follows the room's mood. The shipped default. */
        d->palette = DISP_PAL_DYNAMIC;
        d->bg      = DISP_BG_BLACK;
        d->text    = DISP_TEXT_WHITE;
        d->image   = display_dynamic_slot();
    } else {
        /* No art on this disc, so Dynamic has nothing to show. */
        d->palette = DISP_PAL_PRESET0;         /* IBM PC (MDA): closest to the */
        d->bg      = PRESETS[0].bg;            /* previous hardcoded appearance */
        d->text    = PRESETS[0].text;
        d->image   = DISP_IMAGE_NONE;
    }
}

/*----------------------
 | display_is_image
 | Description: True when the state's image index refers to a present image slot.
 | Author: suinevere
 ----------------------*/
int display_is_image(const DisplayState *d) {
    return d->image >= 0 && d->image < g_image_count;
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
 |   bg/text/image. From the Custom state it enters the list at whichever end it is
 |   heading toward. Dynamic is stepped over on a disc with no art, since it would
 |   land the player on an entry that shows nothing.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_image_count
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
void display_cycle_palette(DisplayState *d, int dir) {
    int count = display_palette_count();
    int next;
    if (display_palette_name(d) == g_custom_label) {
        next = (dir > 0) ? 0 : count - 1;
    } else {
        next = step(d->palette, dir, count);
    }
    /* Dynamic holds index 0 on every disc, art or not, so the display_preset_*
       accessors above can stay single unconditional expressions. The cost is one
       unreachable entry to step past here, which is cheaper than the row changing
       shape -- that would put the arithmetic behind g_image_count everywhere. */
    if (next == DISP_PAL_DYNAMIC && g_image_count == 0)
        next = step(next, dir, count);
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
    d->image   = display_preset_image(next);
}

/*----------------------
 | image_slot_name
 | Description: The registered name for an image slot, or "" when the slot is not
 |   present.
 | Author: suinevere
 ----------------------*/
static const char *image_slot_name(int slot) {
    if (!g_image_names || slot < 0 || slot >= g_image_count) return "";
    return g_image_names[slot] ? g_image_names[slot] : "";
}

/*----------------------
 | image_slot_of
 | Description: The slot currently holding `name`, or -1 if this disc does not
 |   carry it -- so a saved appearance can be matched back to a slot by name.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_image_names, g_image_count
 | Params: name -- the image filename
 | Returns: the slot index, or -1
 ----------------------*/
static int image_slot_of(const char *name) {
    int i;
    if (!g_image_names || !name[0]) return -1;
    for (i = 0; i < g_image_count; i++) {
        const char *have = g_image_names[i];
        int j = 0;
        if (!have) continue;
        while (have[j] && have[j] == name[j]) j++;
        if (have[j] == '\0' && name[j] == '\0') return i;
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
 | display_encode
 | Description: Serializes a DisplayState into a save blob (sentinel 4): palette,
 |   background, and text bytes (Dynamic is marked DISP_BLOB_DYNAMIC and an image
 |   palette DISP_BLOB_IMAGE), plus the on-screen picture's name so it can be
 |   matched back by name on a different disc. If image and palette disagree, what
 |   is displayed wins.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via image_slot_name)
 | Params: d -- the state; out -- the blob buffer
 | Returns: DISP_BLOB_BYTES (the bytes written)
 ----------------------*/
int display_encode(const DisplayState *d, unsigned char *out) {
    const char *name = "";
    int i;

    out[0] = 4;                                /* block sentinel: + the Dynamic palette */
    out[1] = (d->palette == DISP_PAL_DYNAMIC) ? DISP_BLOB_DYNAMIC
           : (unsigned char) d->palette;
    out[2] = (unsigned char) d->bg;            /* always a color now */
    out[3] = (unsigned char) d->text;

    /* Dynamic stores no name: the picture it is showing is a consequence of where
       the player happens to be standing, not a setting worth restoring. Nothing
       else can be carrying a picture now that the row lists none, so in practice
       this writes an empty name every time -- the field is kept, and kept
       populated from d->image, so a state built by hand (a test, a future row that
       pins a picture again) still round-trips instead of silently losing it. */
    if (d->palette == DISP_PAL_DYNAMIC) name = "";
    else if (d->image >= 0)             name = image_slot_name(d->image);

    for (i = 0; i < DISP_IMAGE_NAME_MAX - 1 && name[i]; i++)
        out[4 + i] = (unsigned char) name[i];
    for (; i < DISP_IMAGE_NAME_MAX; i++)
        out[4 + i] = 0;
    return DISP_BLOB_BYTES;
}

/*----------------------
 | display_decode
 | Description: Restores a DisplayState from a save blob, defaulting first so a bad
 |   blob leaves a sane state. Handles the original slot-only form (sentinel 1,
 |   image slots refused since the picture cannot be trusted), and the named forms
 |   (sentinels 2/3/4): resolves the image by name, refusing one this disc lacks,
 |   and validates each field independently.
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
    } else if (buf[0] == 2 || buf[0] == 3 || buf[0] == 4) {
        const char *name = (const char *) (buf + 4);
        int slot, n = 0;

        if (len < DISP_BLOB_BYTES) return 0;
        while (n < DISP_IMAGE_NAME_MAX && name[n]) n++;
        if (n >= DISP_IMAGE_NAME_MAX) return 0;   /* name never terminates */
        slot = image_slot_of(name);

        if (buf[1] == DISP_BLOB_DYNAMIC) {
            /* Only sentinel 4 writes this, and only from a disc that had art.
               Reloaded onto one that has none, Dynamic has nothing to show. */
            if (g_image_count > 0) d->palette = DISP_PAL_DYNAMIC;  else ok = 0;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            /* A blob from when the row let one picture be pinned. It cannot be
               honoured -- there is no palette index for a single picture any more
               -- so it lands on Dynamic, the one entry that still shows art at
               all. Reported as not-verbatim either way: the player is getting
               something other than what they saved. */
            if (g_image_count > 0) d->palette = DISP_PAL_DYNAMIC;
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
