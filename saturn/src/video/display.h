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
 | display_slot_make / display_slot_valid
 | Description: A slot is a scene and a 1-based index encoded together
 |   (scene * 100 + index) rather than a position in a scanned list, so it names a
 |   picture without ever reading the disc. make builds one from the pair, refusing
 |   an index the scene does not carry in the running game; valid answers whether a
 |   slot (however it was built, including DISP_IMAGE_NONE) names a picture the
 |   running game actually has.
 | Author: suinevere
 ----------------------*/
int display_slot_make(int cat, int index);
int display_slot_valid(int slot);

/*----------------------
 | display_image_count / display_bg_count
 | Description: image_count is how many pictures the running game carries in
 |   total, summed from every scene's pool (0 when no game is selected); bg_count
 |   is DISP_BG_COLOR_N (the Background row is colors only).
 | Author: suinevere
 ----------------------*/
int display_image_count(void);
int display_bg_count(void);

/*----------------------
 | display_image_file
 | Description: Synthesises the on-disc path from a slot ("HORROR/07.TGA"), or ""
 |   for a slot this disc does not carry. Held in a small rotating buffer -- copy
 |   it if you need to keep it past a couple of uses.
 | Author: suinevere
 ----------------------*/
const char *display_image_file(int slot);

/*----------------------
 | display_scene_image
 | Description: The picture a scene shows in the running game, as an on-disc
 |   filename, or NULL for "keep whatever is showing". Returns a filename
 |   rather than a slot so display_set_dynamic_category can resolve it with
 |   display_image_slot the same way it would resolve any other disc path.
 |   NULL also comes back when no game is selected, or the scene carries no
 |   pictures in this game.
 | Author: suinevere
 ----------------------*/
const char *display_scene_image(int scene);

/*----------------------
 | display_scene_image_count / display_rotate_scene
 | Description: image_count is how many pictures a scene can draw on in the
 |   running game (0 when no game is selected, or the scene is unauthored for
 |   it). rotate moves that scene to a different one of them and makes it
 |   current -- what the engine asks for after MUSIC_ROTATE_ROOMS rooms of one
 |   unbroken mood, so a long stretch in one scene does not sit on one
 |   picture. A scene with fewer than two pictures holds what it has, which is
 |   also how a pool of one behaves: the rotation becomes a no-op rather than
 |   a flicker back to the same image.
 | Author: suinevere
 ----------------------*/
int  display_scene_image_count(int scene);
void display_rotate_scene(int scene);

/*----------------------
 | display_shuffle_scene
 | Description: Points a scene's pool at an arbitrary one of its pictures,
 |   chosen by `r` (reduced modulo the scene's count in the running game; any
 |   value is legal, and a scene with no pictures is a no-op). Unlike
 |   display_rotate_scene it does not change what is currently on screen --
 |   follow it with display_set_dynamic_category if the new pick should
 |   become the showing slot. Written for the game-start art warm, which has
 |   since been removed; nothing calls it today.
 | Author: suinevere
 ----------------------*/
void display_shuffle_scene(int scene, unsigned int r);

/*----------------------
 | display_set_game
 | Description: Selects the folder (GAME_DIR) and scene ranges (GAME_SCENE)
 |   every later resolve uses. Re-seats every scene's rotor to its new range's
 |   start whenever the running game actually changes, so a rotor left over
 |   from a different game cannot surface the wrong game's picture on a read
 |   path that does not rotate or shuffle first. A no-op when game_index
 |   already names the current game, so calling this ahead of every room
 |   change does not reset an in-progress rotation. Any value outside
 |   0..GAME_N-1 -- including every negative one -- means "no game selected",
 |   which every scene accessor treats as "nothing."
 | Author: suinevere
 ----------------------*/
void display_set_game(int game_index);

/*----------------------
 | display_next_in_band
 | Description: The next absolute 0-based index inside a range, wrapping --
 |   the arithmetic display_rotate_scene walks with. If cur falls outside
 |   [base, base+count), it snaps to base rather than wrapping from where it
 |   happens to sit; that path is load-bearing, not defensive padding -- it
 |   is what a rotor left over from a different game's range lands on: the
 |   old range's index, snapped into the new one.
 | Author: suinevere
 ----------------------*/
int display_next_in_band(int cur, int base, int count);

/*----------------------
 | display_set_dynamic_category / display_dynamic_slot
 | Description: set_dynamic_category resolves a scene to an image slot and
 |   remembers it, ignoring any scene with no art so the wallpaper holds;
 |   dynamic_slot returns that slot. It stores the resolved SLOT rather than the
 |   raw scene so "keep current" is never a transient answer: cycling onto
 |   Dynamic during a scene-less moment would otherwise have no current picture to
 |   keep and would land on no wallpaper at all. DISP_IMAGE_NONE comes back only
 |   when no game is selected, or the running game carries no art.
 | Author: suinevere
 ----------------------*/
void display_set_dynamic_category(int cat);
int  display_dynamic_slot(void);

/*----------------------
 | display_pin_dynamic_slot / display_image_slot
 | Description: pin_dynamic_slot holds Dynamic at one fixed slot until released
 |   with DISP_IMAGE_NONE, leaving the mood the engine is tracking untouched
 |   underneath. For a screen that must not move the CD head: resolving Dynamic
 |   normally can name a picture that is neither uploaded nor cached, and loading
 |   that reads the disc, which stops CD-DA mid-track. Pin to the picture already
 |   on NBG0 (title_bg_loaded_file) and every display_apply() on that screen
 |   short-circuits inside title_bg_show instead.
 |     image_slot returns the slot holding a filename, or DISP_IMAGE_NONE -- the
 |   lookup that turns that filename into something a DisplayState can hold.
 | Author: suinevere
 ----------------------*/
void display_pin_dynamic_slot(int slot);
int  display_image_slot(const char *name);

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
 |   any image because it is what shows through the menu frames and survives
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
