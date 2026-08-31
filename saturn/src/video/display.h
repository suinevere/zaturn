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
 | DISP_BG_COLOR_N / DISP_TEXT_N / DISP_PRESET_N
 | Description: Counts: selectable background colors, text colors, and
 |   microcomputer presets.
 | Author: suinevere
 ----------------------*/
#define DISP_BG_COLOR_N 7
#define DISP_TEXT_N     9
#define DISP_PRESET_N   16

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
 | DISP_IMAGE_ROOM
 | Description: The only picture value left. It does not name a picture -- it
 |   says "whatever room_art.cxx has put on NBG0 for the room the player is
 |   standing in". The field used to hold a slot number selecting one of a
 |   game's downloaded scene pictures; there is one art route now, so the field
 |   is really a two-state flag and this is its set state. Kept as an int rather
 |   than collapsed into a bool because the save blob stores it and the decoder
 |   still has to tell a refused old slot from a present picture.
 | Author: suinevere
 ----------------------*/
#define DISP_IMAGE_ROOM 0

/*----------------------
 | DISP_PAL_DYNAMIC / DISP_PAL_PRESET0
 | Description: The palette row's layout. Index 0 is Dynamic -- "show the room's
 |   own picture" -- followed by the DISP_PRESET_N colour presets, and that is the
 |   whole row. Dynamic occupies index 0 on every disc and is reachable on every
 |   one: it is the shipped default (display_defaults) and nothing steps over it.
 |   On a game with no authored art it is simply black with white text and no
 |   picture, which is a legitimate appearance rather than a broken entry.
 |
 |   The row used to carry one entry per picture as well. That worked at eight and
 |   does not at thirty-seven: it made a fifty-four-entry cycler, and only a
 |   handful of the pictures were ever RAM-resident at once, so most steps along
 |   it would read the disc and stop the CD-DA track. Pictures are reached
 |   through the room the player is standing in now, which is what Dynamic is.
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
    int dim;       /* 0..DISP_DIM_N-1; DISP_DIM_NORMAL is unmodified */
} DisplayState;

/*----------------------
 | DISP_DIM_N / DISP_DIM_NORMAL / DISP_DIM_DEFAULT
 | Description: The wallpaper-dim row's length, the stop that leaves the picture
 |   unmodified, and the stop a fresh install starts on. Discrete steps rather
 |   than a continuous slider: the picture is 8bpp, so a large offset clips
 |   distinct palette entries onto one value and posterises, and a stop the
 |   player can name is easier to return to than a position on a bar.
 |
 |   The row runs darkest first, so pressing left steps darker and right steps
 |   brighter -- the direction a brightness control is expected to move -- and it
 |   stops at both ends rather than wrapping, since a control that jumps from
 |   darkest to brightest on one press too many is not a slider.
 |
 |   Its labels are relative to the default, not to the hardware: the default
 |   reads "0" and the stops either side run -3..+3. Unmodified is "+2", two
 |   stops up, because the default IS a dim -- the shipped wallpaper competes
 |   with the text over it at full brightness, so "no adjustment" is the setting
 |   a player should have to ask for rather than the one they start on.
 |   DISP_DIM_NORMAL therefore sits near the top of the row, not at its end;
 |   "+3" is the one stop that lightens past the picture's own palette.
 | Author: suinevere
 ----------------------*/
#define DISP_DIM_N       7
#define DISP_DIM_NORMAL  5
#define DISP_DIM_DEFAULT 3

/*----------------------
 | display_dim_offset / display_dim_name / display_cycle_dim
 | Description: dim_offset is the signed VDP2 colour offset a dim-row index
 |   holds (0 outside 0..DISP_DIM_N-1); dim_name is that index's label (the
 |   default stop's label for an out-of-range index, matching what
 |   display_decode defaults a bad byte to); cycle_dim steps d->dim one stop in
 |   `dir`, clamping at both ends rather than wrapping the way the colour rows do.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: index -- 0..DISP_DIM_N-1; d -- the state to update; dir -- -1 or +1
 | Returns: dim_offset the offset; dim_name the label; cycle_dim N/A
 ----------------------*/
int display_dim_offset(int index);
const char *display_dim_name(int index);
void display_cycle_dim(DisplayState *d, int dir);

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
 | display_dynamic_slot / display_slot_valid / display_preset_image
 | Description: The image value the Dynamic palette entry carries, whether a
 |   value names a present picture, and the value a palette index implies.
 |   All three collapsed to near-nothing when scene art was removed -- they used
 |   to index per-game picture pools -- but they are kept as calls because every
 |   caller reads better asking them than testing DISP_IMAGE_ROOM by hand, and
 |   because the save decoder needs the "is this still a real picture" question
 |   to have somewhere to live.
 | Author: suinevere
 ----------------------*/
/*----------------------
 | display_palette_count
 | Description: The palette row's length: Dynamic, then the microcomputer
 |   presets. Fixed -- it does not grow or shrink with what art the disc
 |   carries, because Dynamic holds index 0 on every disc and is merely stepped
 |   over when there is nothing to show.
 | Author: suinevere
 ----------------------*/
int display_palette_count(void);

int display_dynamic_slot(void);
int display_slot_valid(int slot);
int display_preset_image(int index);
int display_bg_count(void);
/*----------------------
 | display_set_authored
 | Description: Tells the display layer whether the running game carries
 |   authored per-room art -- the pictures room_art.cxx decompresses from the
 |   original disc's own archives and puts on NBG0 itself. This is now the only
 |   art there is, so it decides whether the room-art path (which only runs under
 |   Dynamic) ever draws.
 |
 |   It no longer decides whether Dynamic can be SELECTED. It used to, and since
 |   the flag is 0 everywhere outside a running game, that hid the row's first
 |   entry from the Options menu at the title screen and defaulted a cold boot to
 |   a colour preset instead.
 |
 |   Nothing clears this implicitly any more. It was cleared by
 |   display_set_game, which existed to seat per-game picture state and
 |   went with it, so every caller that returns to the title or selects a game
 |   without authored art must now call this with 0 itself.
 | Author: suinevere
 ----------------------*/
void display_set_authored(int has_authored);

/*----------------------
 | display_has_art
 | Description: Whether the running game can show pictures at all. Equivalent to
 |   the authored-art flag, since authored per-room art is the only route left,
 |   but kept as its own call because the gates read better asking this question
 |   than that one.
 |
 |   Asked only about DRAWING now, never about what the player may select: the
 |   answer is 0 at the title and in the menus, where no game is running, and
 |   gating the Palette row on it made Dynamic unreachable from exactly there.
 | Author: suinevere
 ----------------------*/
int display_has_art(void);

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
 | Description: The image-name field width in a save blob, plus its NUL. Frozen
 |   at the old flat ISO9660 8.3 width (12 usable characters) for save-format
 |   compatibility -- display_encode no longer writes anything into the field
 |   (see its comment), and a synthesised "MOOD/NN.TGA" path is wider than it
 |   fits regardless, so this stays exactly as sized rather than growing to
 |   accommodate a writer that no longer exists.
 | Author: suinevere
 ----------------------*/
#define DISP_IMAGE_NAME_MAX 13

/*----------------------
 | DISP_BLOB_BYTES
 | Description: The save-block size and layout: [sentinel=8][palette][bg][text]
 |   [dim][image name, NUL-padded]. bg is always a color, stored independently of
 |   any image because it is the ground the menus sit on -- and, through
 |   dash_tint, the hue their marble carries -- and survives
 |   switching the picture off. palette holds 0xFE for Dynamic, which stores no
 |   name at all because its picture is a consequence of where the player is
 |   standing rather than a setting. dim is the wallpaper-offset row index (see
 |   DISP_DIM_N); sentinels 1-4 predate it and always decode to DISP_DIM_DEFAULT.
 |
 |   The sentinel moved from 6 to 8 because the dim row was renumbered: 6 stored
 |   an index into a seven-stop, brightest-first row that had two lightening
 |   stops, and 8 stores one into the five-stop, darkest-first row that replaced
 |   it. The two blocks are the same length, so only the sentinel tells them
 |   apart -- a 6 read as an 8 would land two stops away from where the player
 |   left it, brightest reading as darkest. display_decode remaps a sentinel-6
 |   index by offset value instead (see there).
 |
 |   8 and not 7, the next free number, for the same reason it was never 5:
 |   options.cxx's MOJOOPTS reader (options_load) locates the gameplay block
 |   ahead of this one by testing that position's byte against 5 and 7, and both
 |   were picked there specifically because no display sentinel had ever used
 |   them -- see options.cxx's "gameplay-block sentinel" comment and
 |   test_display.c's test_five_is_not_a_display_sentinel. Taking either here
 |   would make that peek ambiguous for a saved blob that has this block but no
 |   gameplay block ahead of it. 8 carries no meaning anywhere else in the save
 |   format.
 |
 |   display_encode always writes the name field empty now (see its comment):
 |   no UI path can pin a picture to a colour preset any more, and a synthesised
 |   path is wider than the field regardless. The field stays in the layout
 |   rather than being reclaimed -- the block size is what older forms are told
 |   apart by, a shorter current block would make an old blob and a new one
 |   indistinguishable, and display_decode still reads a name out of it for
 |   blobs written before this change.
 |
 |   Five older forms are still read. Sentinel 6 is this exact layout with the
 |   pre-renumbering dim row (see above). Sentinel 4 is that layout minus the
 |   dim byte -- its block is DISP_BLOB_BYTES_V4 (17) bytes where a sentinel-6
 |   or -8 block is DISP_BLOB_BYTES (18), so length and sentinel agree -- and,
 |   along with sentinels 2 and 3, predates Dynamic taking palette index 0, so a
 |   colour-preset index in any of them is one lower than it should be now and is
 |   shifted up on read. Sentinel 3 is the same layout as 4 without
 |   the Dynamic marker; sentinel 2 packed the image into bg (decoding to that image
 |   over black, losing the color beneath); sentinel 1 is the original four-byte,
 |   no-name form (colors honored, any image reference refused). Nothing writes
 |   0xFF (the old "this named picture" marker) any more either; a blob that
 |   still carries it lands on Dynamic instead, the nearest thing the row still
 |   offers to "I wanted a picture here."
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_BYTES (5 + DISP_IMAGE_NAME_MAX)

/*----------------------
 | display_encode / display_decode
 | Description: encode writes the display block (out must have DISP_BLOB_BYTES
 |   room), returning the bytes written. decode reads it, defaulting any field that
 |   is absent, truncated, mis-sentinelled, out of range, or names an image this
 |   disc lacks; returns 1 if fully accepted, 0 if anything was defaulted.
 | Author: suinevere
 ----------------------*/
int display_encode(const DisplayState *d, unsigned char *out);
int display_decode(const unsigned char *buf, int len, DisplayState *d);

#ifdef __cplusplus
}
#endif

#endif
