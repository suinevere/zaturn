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
 |   peaks around 486 KB. That has to fit beside the boot jingle as well as
 |   beside a game's typeahead trie, because the title screen shows one of these
 |   frames too -- saturn/tests/test_lwram_budget.py is where the arithmetic is
 |   held.
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
 |   Skips the decode and the NBG0 upload when the resolved picture is the one
 |   already showing (several maze rooms share a frame), verified against what
 |   is actually recorded on NBG0 rather than only this module's own memory of
 |   what it last drew, so a picture taken over by another caller since is
 |   never mistaken for still being resident.
 |
 |   Restores the CD to the story directory before returning whenever it stepped
 |   out of it, which is the obligation every post-selection detour owes.
 | Author: suinevere
 | Dependencies: SRL, cgl.h, scene/presentation.h, title.h
 | Globals: g_have_game, g_release, g_serial, g_area, g_archive, g_archive_len,
 |   g_pixels, g_clut, g_cur_image
 | Params: obj -- the room's object number
 | Returns: 1 when the room's picture is on screen, whether freshly shown or
 |   already there; 0 on failure, which holds whatever was showing before
 ----------------------*/
int room_art_show(unsigned int obj);

/*----------------------
 | room_art_frame_count / room_art_show_frame
 | Description: The picture route with the room taken out of it, for the title
 |   screen: frame_count is how many frames the disc's archives hold between
 |   them, and show_frame puts one of them up by index.
 |
 |   Deliberately not gated on room_art_set_game. The title screen has no story
 |   selected -- that is the point of it -- but the frames themselves belong to
 |   the disc rather than to any one game, so a picture can be shown there
 |   without pretending a game is running. Every other refusal (an index out of
 |   range, an archive that will not open, a stream that will not decode) is the
 |   room route's, unchanged, and means the same thing: hold what is showing.
 |
 |   The archive one of these leaves resident is the caller's to drop --
 |   room_art_release() -- once the picture has faded out. Left alone it holds
 |   up to 408.5 KB of Low Work RAM for the whole menu phase.
 | Author: suinevere
 | Dependencies: scene/presentation.h
 | Globals: g_area, g_archive, g_archive_len, g_pixels, g_clut, g_cur_image
 | Params: image -- 1-based index, 1..room_art_frame_count()
 | Returns: frame_count the count; show_frame 1 when that picture is on screen,
 |   0 on any refusal
 ----------------------*/
int room_art_frame_count(void);
int room_art_show_frame(int image);

/*----------------------
 | room_art_note_room
 | Description: Records which room the player is in without drawing it, for the
 |   turns where the Palette is not Dynamic and nothing may be put on NBG0.
 |   Without it the module would learn a room only on the turns it was allowed
 |   to draw one, and room_art_reshow would have nothing to redraw for the
 |   player who selects Dynamic mid-game.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_room, g_have_room
 | Params: obj -- the room's object number
 | Returns: N/A
 ----------------------*/
void room_art_note_room(unsigned int obj);

/*----------------------
 | room_art_reshow
 | Description: Puts the last noted room's picture back on NBG0, for the moment
 |   the Palette becomes Dynamic on a game whose art is authored per room. The
 |   room subscriber fires only on a room CHANGE, so without this a player who
 |   selects Dynamic standing still would see nothing until they walked
 |   somewhere. Cheap when the picture is already up: it goes through
 |   room_art_show, which short-circuits to a bare ScrollEnable in that case.
 | Author: suinevere
 | Dependencies: room_art_show
 | Globals: g_room, g_have_room
 | Params: N/A
 | Returns: 1 when a picture is on screen, 0 when no room has been noted yet or
 |   the show failed
 ----------------------*/
int room_art_reshow(void);

/*----------------------
 | room_art_release
 | Description: Frees the resident archive and the decode target and forgets the
 |   game, for leaving back to the menus -- and for the title screen, once its
 |   own randomly picked frame has faded out and the megabyte is wanted for the
 |   boot jingle and the next game's typeahead trie instead.
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
