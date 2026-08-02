/*----------------------
 | display.h
 | Description: The display-appearance model's interface: the color/preset
 |   constants, the DisplayState, the cycling/selection API behind Display Options,
 |   and the save-blob encode/decode. Implemented in display.c; images are loaded
 |   and colors applied to VDP2 by title.cxx.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef DISPLAY_H
#define DISPLAY_H

/*----------------------
 | DISP_RGB555
 | Description: Packs 8-bit RGB into a Saturn RGB555 word (blue high, red low,
 |   opaque bit set), matching SRL::Types::HighColor(r, g, b).
 | Author: suinevere
 ----------------------*/
#define DISP_RGB555(r, g, b) \
    ((unsigned short)(0x8000 | (((b) >> 3) << 10) | (((g) >> 3) << 5) | ((r) >> 3)))

/*----------------------
 | DISP_BG_COLOR_N / DISP_TEXT_N / DISP_PRESET_N / DISP_IMAGE_MAX
 | Description: Counts: selectable background colors, text colors, microcomputer
 |   presets, and the cap on bitmaps read from the disc's TGA folder.
 |
 |   DISP_IMAGE_MAX is a registration cap, not a menu length -- the Palette row
 |   offers Dynamic plus the colour presets and nothing else, so the pictures are
 |   reached through the text categories rather than picked one by one. It only has
 |   to cover every file in /TGA (37 backgrounds at the time of writing), and the
 |   cost of raising it is one name buffer and one pointer per slot in title.cxx,
 |   not RAM for the art: how many pictures are actually held in memory at once is
 |   TGA_CACHE_SLOTS over there, and that is a much smaller number.
 | Author: suinevere
 ----------------------*/
#define DISP_BG_COLOR_N 7
#define DISP_TEXT_N     9
#define DISP_PRESET_N   16
#define DISP_IMAGE_MAX  40

/*----------------------
 | DISP_BG_* / DISP_TEXT_* (color indices)
 | Description: Named indices into the background and text color tables, in
 |   selector order.
 | Author: suinevere
 ----------------------*/
#define DISP_BG_BLACK        0
#define DISP_BG_AMBER        1
#define DISP_BG_BLUE         2
#define DISP_BG_LIGHT_GRAY   3
#define DISP_BG_BRIGHT_CYAN  4
#define DISP_BG_GREEN        5
#define DISP_BG_BRIGHT_WHITE 6

#define DISP_TEXT_BRIGHT_AMBER  0
#define DISP_TEXT_BLACK         1
#define DISP_TEXT_GREEN         2
#define DISP_TEXT_LIGHT_BLUE    3
#define DISP_TEXT_CYAN          4
#define DISP_TEXT_BRIGHT_YELLOW 5
#define DISP_TEXT_GRAY          6
#define DISP_TEXT_BRIGHT_GREEN  7
#define DISP_TEXT_WHITE         8

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DISP_IMAGE_NONE
 | Description: The image-slot sentinel meaning "no picture".
 | Author: suinevere
 ----------------------*/
#define DISP_IMAGE_NONE (-1)

/*----------------------
 | DISP_PAL_DYNAMIC / DISP_PAL_PRESET0
 | Description: The palette row's layout. Index 0 is Dynamic -- "let the text
 |   category choose the picture" -- followed by the DISP_PRESET_N colour presets,
 |   and that is the whole row. Dynamic always occupies index 0 even on a disc with
 |   no art, so the display_preset_* accessors stay unconditional;
 |   display_cycle_palette steps over it in that case instead.
 |
 |   The row used to carry one entry per picture as well. That worked at eight and
 |   does not at thirty-seven: it made a fifty-four-entry cycler, and since only a
 |   handful of pictures are RAM-resident at a time (see TGA_CACHE_SLOTS in
 |   title.cxx) most steps along it would read the disc, which stops the CD-DA
 |   track. Every picture is still reachable -- through the mood it belongs to,
 |   which is what the pools in display.c are for.
 |
 |   Use these names rather than the arithmetic: the row has been renumbered once
 |   already (Dynamic took index 0 from the first colour preset) and anything
 |   spelling the offsets out by hand had to be found and shifted.
 | Author: suinevere
 ----------------------*/
#define DISP_PAL_DYNAMIC 0
#define DISP_PAL_PRESET0 1

/*----------------------
 | DisplayState
 | Description: A chosen appearance: the preset index ("Custom" is derived, never
 |   stored), the background color, the text color, and the image slot (or
 |   DISP_IMAGE_NONE).
 | Author: suinevere
 ----------------------*/
typedef struct {
    int palette;   /* preset index 0..display_palette_count()-1; "Custom" is derived, never stored */
    int bg;        /* 0..DISP_BG_COLOR_N-1: always a color */
    int text;      /* 0..DISP_TEXT_N-1 */
    int image;     /* image slot, or DISP_IMAGE_NONE */
} DisplayState;

/*----------------------
 | color / preset lookups (display_bg_rgb .. display_preset_text)
 | Description: RGB and display-name lookups by color index, and a preset's name,
 |   background, and text color by palette index.
 | Author: suinevere
 ----------------------*/
unsigned short display_bg_rgb(int index);
unsigned short display_text_rgb(int index);
const char *display_bg_color_name(int index);
const char *display_text_name(int index);
const char *display_preset_name(int index);
int display_preset_bg(int index);
int display_preset_text(int index);

/*----------------------
 | display_palette_name / display_defaults
 | Description: palette_name returns the preset name (machine or image) when the
 |   state still matches that preset's bg/text/image, else "Custom"; defaults resets
 |   a state to the default appearance.
 | Author: suinevere
 ----------------------*/
const char *display_palette_name(const DisplayState *d);
void display_defaults(DisplayState *d);

/*----------------------
 | display_set_images / display_image_count / display_bg_count
 | Description: set_images registers the disc's TGA bitmaps (names borrowed, not
 |   copied -- keep the array alive for the program's life; count clamped to
 |   DISP_IMAGE_MAX); image_count is how many were registered; bg_count is
 |   DISP_BG_COLOR_N (the Background row is colors only).
 | Author: suinevere
 ----------------------*/
void display_set_images(const char *const *names, int count);
int display_image_count(void);
int display_bg_count(void);

/*----------------------
 | display_image_file / display_image_label
 | Description: file is the on-disc name to open ("HOUSE1.TGA"), or "" for an
 |   unregistered slot; label is the player-facing form (extension dropped,
 |   capitalized -> "House"), held in a small rotating buffer -- copy it if you need
 |   to keep it past a couple of uses.
 | Author: suinevere
 ----------------------*/
const char *display_image_file(int slot);
const char *display_image_label(int slot);

/*----------------------
 | display_category_image
 | Description: The picture a text category shows, as an on-disc filename, or NULL
 |   for "keep whatever is showing". Keyed by name rather than slot because slots
 |   index the disc's TGA scan order, so adding, removing or reordering art would
 |   repoint a slot-keyed table with nothing to show for it. NULL comes back for
 |   the two turn-text event categories (TC_DANGER, TC_TRIUMPH): those are
 |   moments, not places, so the music shifts for them while the wallpaper holds
 |   on the room's own picture instead of flicking away and back.
 | Author: suinevere
 ----------------------*/
const char *display_category_image(int cat);

/*----------------------
 | display_category_image_count / display_rotate_dynamic_category
 | Description: image_count is how many pictures a category can draw on (0 for the
 |   two event categories). rotate moves that category to a different one of them
 |   and makes it current -- what the engine asks for after MUSIC_ROTATE_ROOMS
 |   rooms of one unbroken mood, so a long stretch in one category does not sit on
 |   one picture. A category with fewer than two pictures holds what it has, which
 |   is also how a pool of one behaves: the rotation becomes a no-op rather than a
 |   flicker back to the same image.
 | Author: suinevere
 ----------------------*/
int  display_category_image_count(int cat);
void display_rotate_dynamic_category(int cat);

/*----------------------
 | display_shuffle_category
 | Description: Points a category's pool at an arbitrary one of its pictures,
 |   chosen by `r` (reduced modulo the pool size; any value is legal, and a
 |   category with no pool is a no-op). Unlike display_rotate_dynamic_category it
 |   does not change what is currently on screen -- follow it with
 |   display_set_dynamic_category if the new pick should become the showing slot.
 |   The title screen uses it so the house behind Z-ATURN differs from boot to boot.
 | Author: suinevere
 ----------------------*/
void display_shuffle_category(int cat, unsigned int r);

/*----------------------
 | display_set_dynamic_category / display_dynamic_slot
 | Description: set_dynamic_category resolves a text category to an image slot and
 |   remembers it, ignoring any category with no art so the wallpaper holds;
 |   dynamic_slot returns that slot. It stores the resolved SLOT rather than the
 |   raw category so "keep current" is never a transient answer: cycling onto
 |   Dynamic during a TC_DANGER moment would otherwise have no current picture to
 |   keep and would land on no wallpaper at all. DISP_IMAGE_NONE comes back only
 |   on a disc with no art.
 | Author: suinevere
 ----------------------*/
void display_set_dynamic_category(int cat);
int  display_dynamic_slot(void);

/*----------------------
 | display_palette_count / display_preset_image
 | Description: palette_count is Dynamic plus the DISP_PRESET_N colour presets --
 |   a fixed length that does not depend on what art the disc carries; preset_image
 |   is the image slot a palette index selects, which is the Dynamic palette's
 |   current picture for index 0 and DISP_IMAGE_NONE for every colour preset.
 | Author: suinevere
 ----------------------*/
int display_palette_count(void);
int display_preset_image(int index);

/*----------------------
 | display_is_image / display_bg_name
 | Description: is_image is true when the state shows a present image; bg_name is
 |   the current background color's display name.
 | Author: suinevere
 ----------------------*/
int display_is_image(const DisplayState *d);
const char *display_bg_name(const DisplayState *d);

/*----------------------
 | display_cycle_bg / display_cycle_text / display_cycle_palette
 | Description: Cycle the background, text, or palette one step (dir +1/-1). bg and
 |   text skip any candidate that would make text the same color as the background.
 | Author: suinevere
 ----------------------*/
void display_cycle_bg(DisplayState *d, int dir);
void display_cycle_text(DisplayState *d, int dir);
void display_cycle_palette(DisplayState *d, int dir);

/*----------------------
 | DISP_IMAGE_NAME_MAX
 | Description: The longest image filename stored, plus its NUL. Disc names are
 |   ISO9660 8.3, which GFS_FNAME_LEN caps at 12.
 | Author: suinevere
 ----------------------*/
#define DISP_IMAGE_NAME_MAX 13

/*----------------------
 | DISP_BLOB_BYTES
 | Description: The save-block size and layout: [sentinel=4][palette][bg][text]
 |   [image name, NUL-padded]. bg is always a color and the name says which picture
 |   sits over it (empty for none) -- both stored because they are independent (an
 |   image hides its color, but the color still shows through the menu frames and
 |   survives switching the picture off). palette holds 0xFE for Dynamic, which
 |   stores no name at all because its picture is a consequence of where the player
 |   is standing rather than a setting.
 |
 |   Nothing writes 0xFF (the "this named picture" marker) any more, because the
 |   Palette row no longer lets a single picture be chosen. Blobs that already carry
 |   it are still read and land on Dynamic, which is the nearest thing the row still
 |   offers to "I wanted a picture here"; the name they stored is not honoured,
 |   since the mood decides. The name field itself stays in the layout rather than
 |   being reclaimed -- the block size is what older forms are told apart by, and a
 |   shorter sentinel-4 would make an old blob and a new one indistinguishable.
 |
 |   Three older forms are still read, and all three predate Dynamic taking palette
 |   index 0 -- so a colour-preset index in any of them is one lower than it should
 |   be now and is shifted up on read. Sentinel 3 is the same layout as 4 without
 |   the Dynamic marker; sentinel 2 packed the image into bg (decoding to that image
 |   over black, losing the color beneath); sentinel 1 is the original four-byte,
 |   no-name form (colors honored, any image reference refused).
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_BYTES (4 + DISP_IMAGE_NAME_MAX)

/*----------------------
 | display_encode / display_decode
 | Description: encode writes the display block (out must have DISP_BLOB_BYTES
 |   room), returning the bytes written. decode reads it, defaulting any field that
 |   is absent, truncated, mis-sentinelled, out of range, or names an image this
 |   disc lacks; returns 1 if fully accepted, 0 if anything was defaulted. Call
 |   display_set_images first so image references resolve against present names.
 | Author: suinevere
 ----------------------*/
int display_encode(const DisplayState *d, unsigned char *out);
int display_decode(const unsigned char *buf, int len, DisplayState *d);

#ifdef __cplusplus
}
#endif

#endif
