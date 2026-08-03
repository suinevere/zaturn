/*----------------------
 | title.h
 | Description: Title screen, background art, TGA loading, CD directory juggling,
 |   and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, menu.h, SRL
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
 | title_bg_show
 | Description: Shows a TGA image on VDP2 NBG0 behind the title text (menus and
 |   gameplay stay on solid black). Serves it from the Low Work RAM cache on a hit,
 |   which keeps the CD idle and the music playing; a miss reads the disc, which
 |   stops CD-DA for the length of the read, and takes a cache slot so the next
 |   request for the same picture is free. Accepts only uncompressed 8bpp
 |   colour-mapped TGA. Returns false if the load fails or the format is
 |   unsupported, so the caller can fall back to a colour background.
 |
 |   Callers reachable with music playing have to cover the miss -- by picking the
 |   moment (the engine only changes the wallpaper at the bottom of a fade, where
 |   the track is being swapped anyway) or by pausing around it (Options does).
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename of the TGA image to load
 | Returns: true if the requested display was applied; false if it fell back
 ----------------------*/
bool title_bg_show(const char *file);

/*----------------------
 | title_bg_show_oneoff
 | Description: Shows a TGA image on VDP2 NBG0 exactly like title_bg_show, but
 |   always decodes into a throwaway High Work RAM buffer and frees it right
 |   after the VRAM upload -- it never touches the Low Work RAM background
 |   cache (g_cache) or its TGA_CACHE_SLOTS slots. Use this for a TGA that is not
 |   one of the registered room backgrounds, so it can neither take a slot nor
 |   evict a picture out of one: the boot splash logo is the case this exists for.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename of the TGA image to load
 | Returns: true if the image was decoded and uploaded; false otherwise
 ----------------------*/
bool title_bg_show_oneoff(const char *file);

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
 | title_bg_fade_arm / title_bg_fade_in / title_bg_fade_out /
 | title_bg_fade_reset
 | Description: The title screen's fade, and the only fade in the client
 |   outside the boot splash -- menus, Display Options cycling, and every
 |   other screen change cut instantly by design. All four drive VDP2 color
 |   offset channel A, which both NBG0 (the title picture) and NBG3 (the
 |   Z-ATURN text art over it) are pointed at, so the title screen dims and
 |   brightens as one picture rather than the text popping in ahead of the
 |   image.
 |     title_bg_fade_arm  -- snap to full black BEFORE title_bg_show and
 |       title_draw_art, so the title is composed unseen instead of flashing
 |       at full brightness for the frames it takes to build.
 |     title_bg_fade_in   -- ramp black -> normal over `frames` fields, then
 |       release both layers (calls title_bg_fade_reset). Pair with an arm.
 |     title_bg_fade_out  -- ramp normal -> black over `frames` fields,
 |       leaving the screen held at black with the channel still engaged.
 |       Whatever runs next must reveal the screen again (title_bg_fade_reset
 |       for an instant cut) or it stays dark -- the offset has no automatic
 |       decay.
 |     title_bg_fade_reset -- instantly restore full brightness and release
 |       both layers. Also the recovery path if a soft reset fires mid-ramp.
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
 |   (black) to 255 (unmodified); 255 fully disengages, leaving no residue for
 |   the title and page fades that share the hardware.
 |     Unlike the four fades above it deliberately does NOT touch NBG3: in game
 |   that layer carries the player's text, and dimming it would blink a sentence
 |   out mid-read. It therefore runs on color offset channel B rather than A --
 |   see title.cxx for why the channel split has to be done this way, and for the
 |   NBG3 clearing SRL's shared offset bitfields make necessary.
 |     One step per call, never a blocking ramp: the caller is the music engine's
 |   per-frame tick, and stalling the interpreter for a whole fade every time a
 |   room's mood changed is exactly what this avoids.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: level -- 0 (black) to 255 (unmodified)
 | Returns: N/A
 ----------------------*/
void title_bg_dyn_fade(int level);

/*----------------------
 | title_and_seed
 | Description: Displays the title screen with a "Press any button" prompt and waits
 |   for user input. Returns a random seed based on the number of elapsed frames.
 |   Also handles soft reset chords while waiting on this screen. Finishes whatever
 |   loading the splash left -- everything it calls is idempotent, so on an unskipped
 |   splash that is nothing -- and fades the splash jingle out on the press.
 | Author: suinevere
 | Dependencies: console_view.h, input.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: a random seed integer
 ----------------------*/
int title_and_seed(void);

/*----------------------
 | display_scan_images
 | Description: Scans the disc's TGA folder once at boot and registers any usable
 |   .TGA files found with the display system so the background selector can cycle
 |   into them.
 | Author: suinevere
 | Dependencies: display.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_scan_images(void);

/*----------------------
 | display_preload_categories
 | Description: Warms the background-art cache with one picture per text category,
 |   in an order that puts the title screen's own picture first, and stops as soon
 |   as Low Work RAM will not spare another slot.
 |
 |   Reads the disc, so it must be called somewhere a stalled drive does not
 |   matter. The splash is that place: its jingle plays from RAM rather than CD-DA,
 |   so reads underneath it are inaudible, and covering a load is what the splash
 |   is for. Safe to call more than once -- pictures already held are skipped.
 | Author: suinevere
 ----------------------*/
void display_preload_categories(int max_slots);

/*----------------------
 | display_warm_cache_random
 | Description: Fills the background-art cache to its budget with one randomly
 |   chosen picture per category, categories visited in a random order, stopping
 |   when Low Work RAM will not spare another slot.
 |
 |   Call at game start, with the loading screen still up and before music_start:
 |   it reads the disc up to eight times, and that is the last window where reading
 |   is inaudible. This is the fill that matters -- the splash's is a token one taken
 |   around the still-resident jingle. Shuffles each category as it goes, so the
 |   picture cached is the one that mood will actually show.
 | Author: suinevere
 ----------------------*/
void display_warm_cache_random(unsigned int seed);

/*----------------------
 | title_bg_cache_release
 | Description: Frees every background-art cache slot and empties the cache.
 |
 |   For the soft-reset return and nothing else. A session ends with ~580 KB of
 |   cached art held, which leaves no room for the boot jingle to be reloaded -- so
 |   without this the title screen comes back silent. What is on NBG0 stays on NBG0;
 |   only the RAM copies go.
 | Author: suinevere
 ----------------------*/
void title_bg_cache_release(void);

/* display_preload_images() is gone. It decoded every registered background into
   Low Work RAM during the boot splash, so that cycling pictures in the Options
   menu never read the disc. That was affordable at eight pictures and is not at
   thirty-seven -- the art alone would want ~2.6 MB of a 1 MB zone. title.cxx now
   keeps TGA_CACHE_SLOTS pictures, filled on demand and evicted least-recently-
   used; see the box there for where the reads it no longer avoids end up. */

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
