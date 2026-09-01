/*----------------------
 | title.h
 | Description: Title screen, the NBG0 picture layer and its fades, the boot
 |   logo's TGA load, CD directory juggling, and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, menu.h, bg_dim.h, SRL
 ----------------------*/
#ifndef TITLE_H
#define TITLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*----------------------
 | title_draw_art
 | Description: Draws the title screen text art (Z-ATURN and copyright).
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_draw_art(void);

/*----------------------
 | title_bg_show_oneoff
 | Description: Reads a TGA off the disc into a throwaway High Work RAM buffer,
 |   uploads it to VDP2 NBG0 and frees the buffer again. The TGA route: the boot
 |   splash logo and the title screen's own background take it, and every room
 |   background is a CGL frame that reaches NBG0 through title_bg_show_raw
 |   instead. Reads the disc, so it stops CD audio; both callers are on the way to
 |   the title screen, before any track is playing.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- bare /TGA filename of the image to load
 | Returns: true if the image was decoded and uploaded; false otherwise
 ----------------------*/
bool title_bg_show_oneoff(const char *file);

/*----------------------
 | title_bg_show_raw
 | Description: Shows an already-decoded 8bpp picture on VDP2 NBG0, for callers
 |   that produced their pixels themselves rather than reading a TGA off the
 |   disc. Every background takes this route: room_art.cxx decompresses one CGL
 |   frame and hands it straight here. Touches no CD, so it is safe to call with
 |   music playing -- which is the whole point, since the archive is already
 |   resident and a per-room change must not interrupt a track.
 |
 |   tag is recorded as the loaded-file name so title_bg_loaded_file and
 |   room_art's short-circuit keep working. It is a label, not a path, and
 |   nothing ever reopens it.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: pixels -- w*h 8bpp bytes; clut -- 256 Saturn RGB555 words; w, h --
 |   the picture's size; tag -- a name to record, truncated to the loaded-name
 |   field
 | Returns: true if the picture was applied, false if an argument was bad or the
 |   palette could not be made
 ----------------------*/
bool title_bg_show_raw(const unsigned char *pixels, const unsigned short *clut,
                       int w, int h, const char *tag);

/*----------------------
 | title_bg_hide
 | Description: Hides the title background image by disabling scroll on VDP2 NBG0.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_hide(void);

/*----------------------
 | title_bg_set_shift
 | Description: Scrolls NBG0 down over the wallpaper by `y` pixels, which moves
 |   the picture that many pixels UP the screen. For the gamepad input strip,
 |   which covers the bottom of the picture: see console_strip_shift for the
 |   size and the reasoning. Writes the register only when the value actually changes,
 |   so a caller may hand it the same answer every frame.
 |
 |   The picture's plane is 512x256 with a 320x240 image in it, so a positive
 |   offset wraps the plane's blank tail, and then the picture's own top, into
 |   the bottom of the screen. That is only ever safe because image_window_box
 |   suppresses NBG0 across the whole strip -- the wrap lands entirely inside
 |   the suppressed rows.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_bg_shift
 | Params: y -- pixels to raise the picture by, 0 for none
 | Returns: N/A
 ----------------------*/
void title_bg_set_shift(int y);

/*----------------------
 | title_bg_loaded_file
 | Description: The name of the picture currently uploaded to NBG0, or "" if
 |   nothing has been. For a CGL frame that is its area's archive stem, which is
 |   what room_art.cxx tests against to skip a decode and an upload it does not
 |   need; for the boot logo it is the filename. Hiding the wallpaper does not
 |   clear it: the picture stays in VRAM and stays free to re-show.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_nbg0_loaded
 | Params: N/A
 | Returns: the filename, or "" -- never NULL
 ----------------------*/
const char *title_bg_loaded_file(void);

/*----------------------
 | title_bg_fade_arm / title_bg_fade_in / title_bg_fade_out /
 | title_bg_fade_reset
 | Description: The title screen's fade. NBG3 (the Z-ATURN text art, and the
 |   console text under a menu) rides VDP2 colour offset channel A; NBG0 (the
 |   picture) is driven in step on channel B, the channel that composes the
 |   player's held wallpaper dim. Two channels rather than one because a scroll
 |   sits on only one of them, and taking the picture onto A for the length of a
 |   fade is what used to drop the dim -- see title_bg_fade_engage.
 |     title_bg_fade_arm  -- snap to full black BEFORE the picture and
 |       title_draw_art, so the title is composed unseen instead of flashing
 |       at full brightness for the frames it takes to build.
 |     title_bg_fade_in   -- ramp black -> normal over `frames` fields, then
 |       release (calls title_bg_fade_reset). Pair with an arm.
 |     title_bg_fade_out  -- ramp normal -> black over `frames` fields,
 |       leaving the screen held at black with the channel still engaged.
 |       Whatever runs next must reveal the screen again (title_bg_fade_reset
 |       for an instant cut) or it stays dark -- the offset has no automatic
 |       decay.
 |     title_bg_fade_reset -- instantly restore full brightness (which means the
 |       held dim, not "no offset"), release NBG3, and hand the picture back to
 |       title_bg_dyn_fade. Also the recovery path if a soft reset fires mid-ramp.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields (fade_in/fade_out only)
 | Returns: N/A
 ----------------------*/
void title_bg_fade_arm(void);
void title_bg_fade_in(int frames);
void title_bg_fade_out(int frames);
void title_bg_fade_reset(void);

/*----------------------
 | title_bg_fade_engage / title_bg_fade_level
 | Description: The screen-wide fade's two halves, for the fades that live in
 |   other files and run their own ramps -- the boot splash (splash.cxx), the
 |   menu ramps (menu.cxx), including the one the story is read under. engage
 |   claims NBG3 on channel A and declares that a screen-wide fade owns the
 |   picture's brightness; level writes one step of that fade to both layers,
 |   composing the held wallpaper dim into the picture's half. Every engage must
 |   reach a title_bg_fade_reset, which is what hands the picture back to the
 |   room transitions -- until it does, title_bg_dyn_fade records levels but
 |   writes nothing, so a music callback or a room change cannot lift a blackout
 |   the fade put there.
 |
 |   Driving a screen fade with a bare SetColorOffsetA instead leaves the picture
 |   wherever the last ramp left it, which is how the wallpaper dim came to be
 |   silently dropped by every menu fade.
 | Author: suinevere
 | Dependencies: SRL, bg_dim.h
 | Globals: N/A
 | Params: v -- -255 (black) to 0 (normal), clamped
 | Returns: N/A
 ----------------------*/
void title_bg_fade_engage(void);
void title_bg_fade_level(int v);

/*----------------------
 | TitleFadeStep / title_bg_fade_in_ex
 | Description: title_bg_fade_in with a per-frame callback, handed the same 0..255
 |   the picture is being lit to. It exists so the CD-DA track can come up on the
 |   picture's ramp rather than on its own: the title's music starts at level 1
 |   under a black screen and reaches full at the same moment the image does.
 |
 |   Driven off the ONE counter rather than two loops for the reason the in-game
 |   transition works the same way -- two ramps of nominally equal length still
 |   drift, and a picture that finishes lighting a beat before or after the sound
 |   arrives reads as a glitch rather than as a fade.
 |
 |   The callback is invoked before each frame's Synchronize, so whatever it sets
 |   is in effect for the frame that brightness belongs to. title_bg_fade_in is
 |   this with no callback.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: frames -- ramp length in vblank fields (clamped to >= 1); step -- the
 |   callback, or nullptr
 | Returns: N/A
 ----------------------*/
typedef void (*TitleFadeStep)(int level);
void title_bg_fade_in_ex(int frames, TitleFadeStep step);

/*----------------------
 | title_bg_dyn_fade
 | Description: Dims the background wallpaper (NBG0) alone, for the in-game
 |   transition between one room mood's picture and the next. `level` runs 0
 |   (black) to 255 (unmodified); at 255 the resting brightness is whatever
 |   offset title_bg_dim_set last held (see bg_dim.h), and the colour-offset
 |   channel is released only when that composed value is neutral -- with no
 |   dim held, behaviour is unchanged from before the dim existed.
 |     Unlike the fades above it deliberately does NOT touch NBG3: in game that
 |   layer carries the player's text, and dimming it would blink a sentence out
 |   mid-read. It therefore runs on colour offset channel B rather than A.
 |     Inert while a screen-wide fade is up (between title_bg_fade_engage and
 |   title_bg_fade_reset): the level is recorded but nothing is written, so a
 |   room change cannot re-light a screen something else is holding black.
 |     One step per call, never a blocking ramp: the caller is the music engine's
 |   per-frame tick, and stalling the interpreter for a whole fade every time a
 |   room's mood changed is exactly what this avoids.
 | Author: suinevere
 | Dependencies: SRL, bg_dim.h
 | Globals: N/A
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: N/A
 ----------------------*/
void title_bg_dyn_fade(int level);

/*----------------------
 | title_bg_dim_set / title_bg_dim_get
 | Description: Sets or reads the player's chosen wallpaper offset (see
 |   bg_dim.h), held across rooms and composed into every title_bg_dyn_fade
 |   ramp -- including the one a screen-wide title fade's disengage re-applies,
 |   so the dim survives a trip through the Options menu. set re-applies the
 |   new offset to VDP2 immediately, at whatever ramp level is currently
 |   showing (see bg_dim_last_level) rather than forcing full brightness -- at
 |   rest that level is 255, so a menu row still previews it live.
 | Author: suinevere
 | Dependencies: bg_dim.h
 | Globals: N/A
 | Params: offset -- -255 (darken) to +255 (lighten), clamped
 | Returns: get returns the held offset; set returns N/A
 ----------------------*/
void title_bg_dim_set(int offset);
int  title_bg_dim_get(void);

/*----------------------
 | title_and_seed
 | Description: Displays the title screen with a "Press any button" prompt and waits
 |   for user input. Returns a random seed based on the number of elapsed frames.
 |   Also handles soft reset chords while waiting on this screen. Runs the game
 |   catalogue scan behind the art, and fades the splash jingle out on the press.
 | Author: suinevere
 | Dependencies: console_view.h, input.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: a random seed integer
 ----------------------*/
int title_and_seed(void);

/*----------------------
 | cd_capture_root
 | Description: Snapshots the root directory record straight after GFS_Reset()
 |   so that cd_enter_root() can return to it later.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dirnames, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_capture_root(void);

/*----------------------
 | cd_enter_root
 | Description: Re-points the CD to the root directory captured by cd_capture_root.
 |   Used to make directory changes idempotent.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_enter_root(void);

/*----------------------
 | cd_restore_z3
 | Description: Re-points the CD at the /Z3 directory captured by the catalogue
 |   scan, so a bare SRL::Cd::File("XXX.Z3") or "XXX.BLB" resolves. This is the
 |   restore any CD detour taken after game_select() owes -- the story load and
 |   the sound-blorb load that follow it both open by bare name, and neither
 |   re-establishes the directory itself. A no-op until the scan has run, so it
 |   is safe to call on the boot path too.
 | Author: suinevere
 | Dependencies: SRL, game_catalog.h (which captures the record)
 | Globals: g_z3_tbl, g_z3_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_restore_z3(void);

#ifdef __cplusplus
}
#endif

#endif
