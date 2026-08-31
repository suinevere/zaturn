/*----------------------
 | room_art.h
 | Description: The room backgrounds' hardware half: which area archive is
 |   resident, reading the next one off the disc, decompressing one frame and
 |   putting it on NBG0. The decoding is in cgl.c and the data in
 |   scene/presentation.h; this is only the policy and the SRL calls.
 |
 |   One archive is resident at a time. All eleven are 2.0 MB together and Low
 |   Work RAM is 1 MB, so holding more is not on the table; the largest single
 |   archive is 408.5 KB, which with the 76.8 KB decode target and the palette
 |   peaks around 486 KB. That only fits because title_bg_cache_release() drops
 |   the nine TGA cache slots when a game with authored art starts -- the two
 |   art paths never hold memory at the same time.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef ROOM_ART_H
#define ROOM_ART_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | room_art_set_game
 | Description: Tells the loader which story is running, once, when it is
 |   selected. Held rather than passed per call because the room subscriber runs
 |   where the story identity is not in scope. Passing a story with no authored
 |   art is how the loader is turned off again.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_release, g_serial, g_have_game
 | Params: release -- Z-machine release; serial -- 6-char serial
 | Returns: N/A
 ----------------------*/
void room_art_set_game(unsigned int release, const char *serial);

/*----------------------
 | room_art_available
 | Description: Whether the story set by room_art_set_game has authored room
 |   art. The one call that decides whether a game takes this path or the scene
 |   path.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_have_game
 | Params: N/A
 | Returns: 1 when the story has a presentation table, 0 otherwise
 ----------------------*/
int room_art_available(void);

/*----------------------
 | room_art_show
 | Description: Puts one room's original background on NBG0, reading its area
 |   archive first if a different one is resident. Every failure -- no game set,
 |   an unauthored room, an archive that will not open, a read that comes up
 |   short, a stream that will not decode -- holds the picture already showing
 |   and says nothing on screen. Art is decoration; a failed load must never be
 |   able to blank the screen or stop the game.
 |
 |   Restores the CD to the story directory before returning whenever it stepped
 |   out of it, which is the obligation every post-selection detour owes.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, scene/presentation.h, title.h
 | Globals: g_have_game, g_release, g_serial, g_area, g_archive, g_archive_len,
 |   g_pixels, g_clut
 | Params: obj -- the room's object number
 | Returns: 1 when a new picture was applied, 0 when nothing changed
 ----------------------*/
int room_art_show(unsigned int obj);

/*----------------------
 | room_art_release
 | Description: Frees the resident archive and the decode target and forgets the
 |   game, for leaving back to the menus where the TGA cache wants the memory
 |   again.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_have_game
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void room_art_release(void);

#ifdef __cplusplus
}
#endif
#endif /* ROOM_ART_H */
