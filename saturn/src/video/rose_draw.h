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
 | Description: Draws one composed rose row at a text-map cell row, overprinting
 |   the selected direction's label in reverse video.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h, command_view.h (CV_TRAVEL_X)
 | Globals: N/A
 | Params: row -- rose row 0..CR_ROWS-1; exits -- RM_DIR_N exit states;
 |   y -- text-map cell row; sel -- selected RM_* direction, or negative for none
 | Returns: N/A
 ----------------------*/
void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel);

#endif /* ROSE_DRAW_H */
