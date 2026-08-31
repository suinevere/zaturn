/*----------------------
 | display.c
 | Description: The display-appearance model: the background/text color tables, the
 |   microcomputer palette presets, the per-scene image slot arithmetic, and the
 |   cycling/selection logic behind Display Options, plus the save-blob encode/
 |   decode for persisting a chosen appearance. Pure model and data -- title.cxx
 |   loads the actual images and applies colors to VDP2.
 | Author: suinevere
 | Dependencies: display.h (DISP_* constants, DisplayState, DISP_RGB555),
 |   scene/scene_map.h (SC_*, SCENE_N, GAME_N)
 ----------------------*/
#include "display.h"
#include "scene/scene_map.h"   /* SC_* / SCENE_N / GAME_N: the scenes the art follows */
#include <string.h>        /* strcmp: parsing "GAMEDIR/NN.TGA" back to a slot */

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
 | Description: The wallpaper offsets the Display row steps through, darkest
 |   first, and their labels. Darkest first so that left steps darker and right
 |   steps brighter. Steps of 32 rather than a continuous slider: the picture is
 |   8bpp, so a large offset clips distinct palette entries onto one value and
 |   posterises, and a stop the player can name is easier to return to than a
 |   position on a bar.
 |
 |   The labels are the offset expressed in steps away from the DEFAULT stop, so
 |   the row a player sees is centred on where they started rather than on the
 |   hardware's zero -- offset = 32 * (label - 2), which puts unmodified at "+2"
 |   (DISP_DIM_NORMAL) and the shipped default at "0". Keep the two arrays in
 |   step with that arithmetic; test_display.c checks it.
 | Author: suinevere
 ----------------------*/
static const short DIM_STOPS[DISP_DIM_N] = { -160, -128, -96, -64, -32, 0, 32 };
static const char *const DIM_NAMES[DISP_DIM_N] = {
    "-3", "-2", "-1", "0", "+1", "+2", "+3"
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
 | dim_index_v6
 | Description: The current dim-row index for a dim byte stored by a sentinel-6
 |   blob. That form indexed a brightest-first row running +64 down to -128; the
 |   row now runs darkest-first from -160 up to +32. Both step by 32, so
 |   reversing the index is an exact match by offset value and a chosen darkness
 |   survives the renumbering. Only the old +64 stop has no equivalent left, and
 |   it lands on +32, the brightest the row still offers.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: v6 -- the stored byte, 0..6
 | Returns: 0..DISP_DIM_N-1, or DISP_DIM_DEFAULT if v6 is out of that range
 ----------------------*/
static int dim_index_v6(int v6) {
    int i;
    if (v6 < 0 || v6 > 6) return DISP_DIM_DEFAULT;
    i = 7 - v6;                                 /* both rows step by 32 */
    return (i > DISP_DIM_N - 1) ? DISP_DIM_N - 1 : i;
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
 | GameScene / GAME_DIR / GAME_SCENE / g_game
 | Description: Every game gets its own flat disc folder now, rather than
 |   games sharing mood folders -- GAME_DIR is that folder name (the story
 |   stem, already 8.3-safe), and GAME_SCENE[game][scene] is where a scene's
 |   pictures sit inside that game's own 1..99 index range: base is 0-based,
 |   so the nth picture of a scene is index base + n + 1. g_game is the
 |   running game's row into both tables, or negative when no game is
 |   selected -- see display_set_game.
 |
 |   A folder per scene was not possible: g_file_buf is exactly
 |   "UNDRGRND/99.TGA" and the ISO 8.3 rule caps a stem at eight characters,
 |   so a suffixed name overflows both that buffer and the save blob's frozen
 |   name field. "STARCROS/99.TGA" is the same 15 characters, so this stays a
 |   rename rather than a restructuring.
 | Author: suinevere
 ----------------------*/
typedef struct { unsigned char base, count; } GameScene;
static int g_game = -1;

#include "scene/game_scenes.inc"

#define SLOT_STRIDE 100

/*----------------------
 | g_scene_rot
 | Description: Where each scene's pool is currently pointing, as a 0-based
 |   offset within that scene's own range (0..count-1), not an absolute
 |   index into the game's folder -- display_image_file adds the scene's
 |   base separately. Rotation is round-robin rather than random so a pool of
 |   two cannot hand back what is already showing, which is the whole ask;
 |   it also means leaving a scene and returning to it later resumes at a
 |   different picture instead of always opening on the same one. Re-seated
 |   whenever display_set_game changes the running game, since a rotor left
 |   over from a different game's scene ranges means nothing here.
 | Author: suinevere
 ----------------------*/
static unsigned char g_scene_rot[SCENE_N];

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
 | display_set_game
 | Description: See display.h. Re-seats every scene's rotor to its range's
 |   start on an actual change, because a rotor left over from a different
 |   game validates against that game's own GAME_SCENE row at best by
 |   coincidence and would otherwise surface the wrong game's picture,
 |   silently. A no-op when game_index already names the current game, so a
 |   caller that calls this ahead of every room change does not reset an
 |   in-progress rotation. Any index outside 0..GAME_N-1, including every negative one,
 |   normalizes to "no game selected" -- the one answer every later scene
 |   accessor treats as "nothing."
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, g_scene_rot
 | Params: game_index -- a row from scene_game_index, or any value for "none"
 | Returns: N/A
 ----------------------*/
void display_set_game(int game_index) {
    int scene;
    if (game_index < 0 || game_index >= GAME_N) game_index = -1;
    g_authored_art = 0;
    if (game_index == g_game) return;
    g_game = game_index;
    for (scene = 0; scene < SCENE_N; scene++) g_scene_rot[scene] = 0;
}

/*----------------------
 | display_set_authored
 | Description: See display.h. Set after display_set_game, which clears it.
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
    return g_authored_art || display_image_count() > 0;
}

/*----------------------
 | display_next_in_band
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: cur -- the current absolute 0-based index; base -- the band's base;
 |   count -- the band's width
 | Returns: the next absolute index inside the band, wrapping; cur unchanged
 |   if count <= 0; snapped to base if cur falls outside the band
 ----------------------*/
int display_next_in_band(int cur, int base, int count) {
    int off;
    if (count <= 0) return cur;
    off = cur - base;
    if (off < 0 || off >= count) return base;
    return base + ((off + 1) % count);
}

/*----------------------
 | display_slot_make
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, GAME_SCENE
 | Params: scene -- an SC_* scene; index -- 1-based, 1..the scene's count in
 |   the running game
 | Returns: the slot, or DISP_IMAGE_NONE when no game is selected, or the
 |   scene or index is out of range
 ----------------------*/
int display_slot_make(int scene, int index) {
    if (g_game < 0) return DISP_IMAGE_NONE;
    if (scene < 0 || scene >= SCENE_N) return DISP_IMAGE_NONE;
    if (index < 1 || index > (int) GAME_SCENE[g_game][scene].count) return DISP_IMAGE_NONE;
    return scene * SLOT_STRIDE + index;
}

/*----------------------
 | display_slot_valid
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game (via display_slot_make)
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: 1 when the slot names a picture the running game carries, else 0
 ----------------------*/
int display_slot_valid(int slot) {
    if (slot < 0 || g_game < 0) return 0;
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
 | Params: cat -- the SC_* scene to resolve
 | Returns: N/A
 ----------------------*/
void display_set_dynamic_category(int cat) {
    const char *file = display_scene_image(cat);
    int slot;
    if (!file) return;                 /* no art for this scene: hold what is showing */
    slot = image_slot_of(file);
    if (slot >= 0) g_dyn_slot = slot;  /* absent from this game's folder: likewise hold */
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
 |   answers "is this slot real". 0 when no game is selected.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, GAME_SCENE
 | Params: N/A
 | Returns: how many pictures the running game carries in total
 ----------------------*/
int display_image_count(void) {
    int scene, n = 0;
    if (g_game < 0) return 0;
    for (scene = 0; scene < SCENE_N; scene++) n += (int) GAME_SCENE[g_game][scene].count;
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
 | Description: See display.h. The folder is the running game's; the two digits
 |   are the picture's absolute index inside that folder, which is the scene's
 |   base plus the slot's 1-based local index.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: GAME_DIR, GAME_SCENE, g_game, g_file_buf, g_file_turn
 | Params: slot -- a slot, or DISP_IMAGE_NONE
 | Returns: the disc path, or "" when the slot names no picture
 ----------------------*/
const char *display_image_file(int slot) {
    char *out;
    const char *dir;
    int scene, local, index, k = 0;

    if (g_game < 0 || !display_slot_valid(slot)) return "";
    scene = slot / SLOT_STRIDE;
    local = slot % SLOT_STRIDE;
    index = GAME_SCENE[g_game][scene].base + local;
    dir   = GAME_DIR[g_game];

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
 | display_scene_image
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, g_scene_rot, GAME_SCENE
 | Params: scene -- an SC_* scene
 | Returns: the pool's current filename, or NULL to hold what is showing
 ----------------------*/
const char *display_scene_image(int scene) {
    if (g_game < 0) return 0;
    if (scene < 0 || scene >= SCENE_N) return 0;
    if (GAME_SCENE[g_game][scene].count == 0) return 0;
    return display_image_file(display_slot_make(scene, (int) g_scene_rot[scene] + 1));
}

/*----------------------
 | display_scene_image_count
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, GAME_SCENE
 | Params: scene -- an SC_* scene
 | Returns: how many pictures the scene can draw on in the running game, 0 if none
 ----------------------*/
int display_scene_image_count(int scene) {
    if (g_game < 0) return 0;
    if (scene < 0 || scene >= SCENE_N) return 0;
    return (int) GAME_SCENE[g_game][scene].count;
}

/*----------------------
 | display_shuffle_scene
 | Description: Points a scene's pool at an arbitrary one of its pictures,
 |   chosen by `r`. Unlike display_rotate_scene this does NOT touch the
 |   showing slot -- it only moves where the pool is pointing, so the caller
 |   decides whether and when that becomes visible.
 |
 |   Written for the art warm that used to run at game start, which wanted a random
 |   pick from each scene rather than the scene's own current rotor position. That
 |   warm is gone and this currently has no caller. Kept here rather than done by
 |   the caller
 |   because g_scene_rot is what display_scene_image reads, and a caller picking
 |   its own filename would leave the pool pointing somewhere else -- a later
 |   read of the same scene would then resolve to a different picture than the
 |   one just cached.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, g_scene_rot, GAME_SCENE
 | Params: scene -- an SC_* scene; r -- any value, reduced modulo the scene's
 |   count in the running game
 | Returns: N/A
 ----------------------*/
void display_shuffle_scene(int scene, unsigned int r) {
    unsigned char n;
    if (g_game < 0) return;
    if (scene < 0 || scene >= SCENE_N) return;
    n = GAME_SCENE[g_game][scene].count;
    if (!n) return;
    g_scene_rot[scene] = (unsigned char)(r % (unsigned int) n);
}

/*----------------------
 | display_rotate_scene
 | Description: See display.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, g_scene_rot, g_dyn_slot, GAME_SCENE
 | Params: scene -- the SC_* scene to advance
 | Returns: N/A
 ----------------------*/
void display_rotate_scene(int scene) {
    int n, i;
    if (g_game < 0) return;
    if (scene < 0 || scene >= SCENE_N) return;
    n = (int) GAME_SCENE[g_game][scene].count;
    if (n <= 1) return;

    for (i = 0; i < n; i++) {
        int slot;
        g_scene_rot[scene] = (unsigned char) display_next_in_band(
            (int) g_scene_rot[scene], 0, n);
        slot = display_slot_make(scene, (int) g_scene_rot[scene] + 1);
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
    if (display_has_art()) {
        /* Dynamic: the picture follows the room's mood. The shipped default. */
        d->palette = DISP_PAL_DYNAMIC;
        d->bg      = DISP_BG_BLACK;
        d->text    = DISP_TEXT_WHITE;
        d->image   = display_dynamic_slot();
        d->dim     = DISP_DIM_DEFAULT;
    } else {
        /* No art on this disc, so Dynamic has nothing to show. */
        d->palette = DISP_PAL_PRESET0;         /* IBM PC (MDA): closest to the */
        d->bg      = PRESETS[0].bg;            /* previous hardcoded appearance */
        d->text    = PRESETS[0].text;
        d->image   = DISP_IMAGE_NONE;
        d->dim     = DISP_DIM_DEFAULT;
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
    if (next == DISP_PAL_DYNAMIC && !display_has_art())
        next = step(next, dir, count);
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
    d->image   = display_preset_image(next);
}

/*----------------------
 | image_slot_of
 | Description: The slot a disc path names, or -1. Parses the form
 |   display_image_file writes and nothing else, so a flat name from an older
 |   save blob misses -- which is the wanted answer, since the Palette row
 |   stopped honouring pinned pictures. The path names the running game's
 |   folder and an absolute index inside it; that index is matched against
 |   each scene's [base+1, base+count] range in turn to recover the scene and
 |   the slot's 1-based local index.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_game, GAME_DIR, GAME_SCENE
 | Params: name -- a disc path; NULL and "" both miss
 | Returns: the slot, or -1
 ----------------------*/
static int image_slot_of(const char *name) {
    int scene, i, index;
    const char *dir, *p;

    if (!name || !name[0] || g_game < 0) return -1;
    dir = GAME_DIR[g_game];
    for (i = 0; dir[i] && name[i] == dir[i]; i++) { }
    if (dir[i] || name[i] != '/') return -1;
    p = name + i + 1;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    index = (p[0] - '0') * 10 + (p[1] - '0');
    if (strcmp(p + 2, ".TGA") != 0) return -1;

    for (scene = 0; scene < SCENE_N; scene++) {
        int base  = (int) GAME_SCENE[g_game][scene].base;
        int count = (int) GAME_SCENE[g_game][scene].count;
        if (count && index > base && index <= base + count) {
            return display_slot_make(scene, index - base);
        }
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
 | Description: Serializes a DisplayState into a save blob (sentinel 8 -- not 5
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

    out[0] = 8;                                /* block sentinel: renumbered dim row */
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
 |   (sentinels 2/3/4/6/7): resolves the image by name, refusing one this disc lacks,
 |   and validates each field independently. Only sentinels 6 and 7 carry a dim
 |   byte -- 6's indexes the pre-renumbering row and is translated by
 |   dim_index_v6 -- and the older forms leave d->dim at the DISP_DIM_DEFAULT
 |   display_defaults set.
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
    } else if (buf[0] == 6 || buf[0] == 8) {
        /* Current form: sentinel 2/3/4's layout plus a dim byte ahead of the
           name, and its own length -- DISP_BLOB_BYTES, not DISP_BLOB_BYTES_V4.
           Post-Dynamic numbering throughout, like sentinel 4, so the palette
           byte needs no shift. Sentinel is 8, not 5 or 7 -- see DISP_BLOB_BYTES
           in display.h for why both are reserved for options.cxx's gameplay
           block. 6 is the same block with the pre-renumbering dim row and
           differs only in that byte, which dim_index_v6 translates below. */
        const char *name = (const char *) (buf + 5);
        int slot, n = 0;

        if (len < DISP_BLOB_BYTES) return 0;
        while (n < DISP_IMAGE_NAME_MAX && name[n]) n++;
        if (n >= DISP_IMAGE_NAME_MAX) return 0;   /* name never terminates */
        slot = image_slot_of(name);

        if (buf[1] == DISP_BLOB_DYNAMIC) {
            if (display_has_art()) d->palette = DISP_PAL_DYNAMIC;  else ok = 0;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            if (display_has_art()) d->palette = DISP_PAL_DYNAMIC;
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
            /* Only sentinel 4 writes this, and only from a disc that had art.
               Reloaded onto one that has none, Dynamic has nothing to show. */
            if (display_has_art()) d->palette = DISP_PAL_DYNAMIC;  else ok = 0;
        } else if (buf[1] == DISP_BLOB_IMAGE) {
            /* A blob from when the row let one picture be pinned. It cannot be
               honoured -- there is no palette index for a single picture any more
               -- so it lands on Dynamic, the one entry that still shows art at
               all. Reported as not-verbatim either way: the player is getting
               something other than what they saved. */
            if (display_has_art()) d->palette = DISP_PAL_DYNAMIC;
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
