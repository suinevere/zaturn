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
