/*----------------------
 | map_view.h
 | Description: The in-game map's screen: the ground, the room marks and links
 |   on the tile layer, the labels on the text layer, and the figure fixed at
 |   the centre. Owns every hardware write the map makes; map_model.c owns the
 |   geometry. Implemented in map_view.cxx.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef MAP_VIEW_H
#define MAP_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | map_view_show
 | Description: Draws the map and holds it until the player backs out, then
 |   returns with the screen cleared and the tile palette put back the way it
 |   was found. A/B/C/START or any key backs out; the D-pad scrolls the view a
 |   room at a time, clamped to the rooms actually placed so it cannot be walked
 |   off into empty ground.
 |
 |   How much of the map there is comes from g_difficulty. Easy places the whole
 |   authored table before the first draw, so the screen shows the map as drawn
 |   rather than as explored, and falls back to the explored map on a story with
 |   no table. Medium draws only what the player has walked into, and takes back
 |   any reveal a previous Easy open left behind. Hard has no map at all and
 |   options_menu does not offer the row that reaches this.
 |
 |   The scroll always starts with the player centred. It is a local of this
 |   call rather than anything kept between calls, so reopening the map after
 |   scrolling it to the far corner shows the player again -- there is no
 |   position to restore because none is stored.
 |
 |   Repaints only on the frames the scroll actually moved. dash_map's NBG2
 |   layer expires a frame after the last renderer stops claiming it, so the
 |   hold loop re-claims it with dash_map_hold on every other frame rather than
 |   repainting. Advances the frame through menu_sync so sound and music do not
 |   stall for as long as the map is up.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, dash_view.h, room_model.h, text_map.h,
 |   menu.h, title.h, input.h, saturn_keyboard.h, soft_reset.h, console_view.h,
 |   app_state.h
 | Globals: g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_view_show(void);

/*----------------------
 | map_view_preload
 | Description: Reads the map's parchment while the game is still loading, so
 |   opening the map later costs no disc access at all. Call once per game, under
 |   the loading ramp and before the music starts: this is the only thing on the
 |   map's path that touches the drive, and a seek taken with CD-DA playing does
 |   not merely pause it -- an unheld track reads to the music engine as one that
 |   ended and is restarted from the top.
 |
 |   Silently declines when the heap cannot spare the picture beside the story
 |   image, which is the case for the largest stories on the disc. That is the
 |   whole decision, made once here rather than retried on every open: the map
 |   draws on its tan back colour instead, and never reaches for the drive.
 |   No-op in the netbin, which has neither a drive nor room art.
 | Author: suinevere
 | Dependencies: title.h (title_bg_hold)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_view_preload(void);

#ifdef __cplusplus
}
#endif
#endif /* MAP_VIEW_H */
