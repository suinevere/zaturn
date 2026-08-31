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
 |   was found. Draws once: dash_map's NBG2 layer expires a frame after the
 |   last renderer stops claiming it, so the hold loop re-claims it with
 |   dash_map_hold rather than repainting. Advances the frame through menu_sync
 |   so sound and music do not stall for as long as the map is up.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, dash_view.h, room_model.h, text_map.h,
 |   menu.h, title.h, input.h, saturn_keyboard.h, soft_reset.h, console_view.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void map_view_show(void);

#ifdef __cplusplus
}
#endif
#endif /* MAP_VIEW_H */
