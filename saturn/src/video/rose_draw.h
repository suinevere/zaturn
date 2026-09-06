/*----------------------
 | rose_draw.h
 | Description: The one call that paints a compass-rose row through the text map,
 |   split out of command_view so a build can draw a rose without linking the
 |   command panel. C++ linkage because the body calls text_map's text_print,
 |   which is a C++ inline outside that header's extern "C" block.
 | Author: suinevere
 | Dependencies: command_rose.h (the row composition it draws)
 ----------------------*/
#ifndef ROSE_DRAW_H
#define ROSE_DRAW_H

/*----------------------
 | cv_draw_rose_row
 | Description: Draws one composed rose row at a text-map cell row dim, then
 |   overprints the selected direction's label at full brightness. A row with no
 |   selection on it stays dim throughout, which is also what the whole rose
 |   looks like while the focus is somewhere else.
 |     While the chord modifier is held this draws the chord key instead -- the
 |   same seven rows in the same columns, and the same compass, with each cell
 |   naming what the D-pad does that way under the player's own bindings rather
 |   than which exit the room has. The swap is here rather than at the call sites
 |   because both interfaces draw their travel module through this one call, and
 |   `exits` and `sel` are simply unread on those frames.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h, command_view.h (CV_TRAVEL_X),
 |   input.h (the live mapping and whether the modifier is down)
 | Globals: N/A
 | Params: row -- rose row 0..CR_ROWS-1; exits -- RM_DIR_N exit states;
 |   y -- text-map cell row; sel -- selected RM_* direction, or negative for none
 | Returns: N/A
 ----------------------*/
void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel);

#endif /* ROSE_DRAW_H */
