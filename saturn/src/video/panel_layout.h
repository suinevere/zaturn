/*----------------------
 | panel_layout.h
 | Description: The command panel's cell arithmetic, with no includes and no
 |   declarations, so a plain-C host test can take it without pulling in
 |   command_view.h's C++ references and its per-subdirectory includes.
 |   command_view.h includes this in place of the constants it used to define
 |   itself, so console_view.cxx's existing use of CV_STRIP_ROWS keeps working
 |   untouched.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef PANEL_LAYOUT_H
#define PANEL_LAYOUT_H

/*----------------------
 | CV_TRAVEL_X / CV_WORD_X / CV_CMD_X / CV_STRIP_ROWS
 | Description: The inner starting column of each module and the strip's content
 |   height. The strip is 1 + 13 + 1 + 15 + 1 + 8 + 1 = 40 columns and seven
 |   rows, all seven of them content: the compass rose is that tall, and the word
 |   and command lists are five rows sitting one row in from either end of it.
 |   The two blank rows that used to pad a five-row rose out to the strip's
 |   height are gone, so the panel's overall height is unchanged.
 | Author: suinevere
 ----------------------*/
#define CV_TRAVEL_X    1
#define CV_WORD_X     15
#define CV_CMD_X      31
#define CV_STRIP_ROWS  7

/*----------------------
 | CV_OVERLAY_X / CV_OVERLAY_W
 | Description: The inventory overlay's box. It is the strip itself -- all forty
 |   columns, framed by the strip's own frame -- rather than a second box drawn
 |   inside it. The overlay used to print a 34-column ASCII box at column 2, so
 |   over the tile dashboard the player saw two frames nested one inside the
 |   other, an outer one of marble and an inner one of pipes and plus signs.
 |   Now the marble frame is the box's frame, and the split inside it is a real
 |   tiled divider (see CV_OVERLAY_DIV_X); the ASCII characters are drawn only
 |   on the fallback path where the tile layer never came up.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_X 0
#define CV_OVERLAY_W 40

/*----------------------
 | CV_OVERLAY_TALL_ROWS / CV_OVERLAY_ROWS / CV_OVERLAY_SHORT_LIST
 | Description: The overlay's content height -- the rows between the strip's two
 |   frame rows -- in each of its two shapes, and the item count each lists.
 |   Both numbers are the height, because with the box's frame being the strip's
 |   frame every content row is a list row.
 |     The strip keeps its seven content rows for a story with no item art. With
 |   item art it grows to twelve, which is the picture's ten rows plus the one
 |   row above and below that its own frame takes (see CV_OVERLAY_PIC_X).
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_TALL_ROWS  12
#define CV_OVERLAY_ROWS       CV_OVERLAY_TALL_ROWS
#define CV_OVERLAY_SHORT_LIST CV_STRIP_ROWS

/*----------------------
 | CV_OVERLAY_DIV_X / CV_OVERLAY_LIST_X / CV_OVERLAY_LIST_W
 | CV_OVERLAY_PANE_X / CV_OVERLAY_PANE_W
 | Description: The tall overlay's split. Column 27 is the divider: the tile
 |   dashboard closes the item list's module there and opens the picture's in
 |   column 28, exactly as the three-module strip's own dividers work, so the
 |   split is a bevelled seam in the stone rather than a printed bar. That leaves
 |   the list columns 1..26 and the picture module the ten columns 29..38. The
 |   plain overlay has no divider and no picture module; its list runs the whole
 |   interior.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_DIV_X   27
#define CV_OVERLAY_LIST_X   1
#define CV_OVERLAY_LIST_W  (CV_OVERLAY_DIV_X - CV_OVERLAY_LIST_X)
#define CV_OVERLAY_PANE_X  (CV_OVERLAY_DIV_X + 2)
#define CV_OVERLAY_PANE_W  (CV_OVERLAY_W - 1 - CV_OVERLAY_PANE_X)

/*----------------------
 | CV_OVERLAY_NUM_COLS
 | Description: The columns each item row spends on its position in the
 |   inventory -- two digits, a bracket and a space, so "16) " and " 1) " put
 |   their names in the same column and the list reads as a column of names
 |   rather than a ragged one. Two digits is enough for good: RM_CARRIED_MAX
 |   bounds the list at sixteen.
 |     The menus reserve MENU_DIGIT_COLS for the same job, but three columns and
 |   only up to nine, because there the digit is a key the player presses and
 |   the keyboard has no way to send a tenth. Nothing is pressed here -- the
 |   pad walks the list -- so the number is free to say where in the inventory
 |   the row actually is, including on the second page.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_NUM_COLS 4

/*----------------------
 | CV_OVERLAY_PIC_X / CV_OVERLAY_PIC_W / CV_OVERLAY_PIC_ROWS
 | Description: The item picture itself, one cell in from the picture module on
 |   every side. That one-cell border is a second frame -- the same bead as the
 |   module's own, facing inward, laid by dash_map from the DT_PIC_* tiles --
 |   so the picture sits in a frame of its own rather than floating on the
 |   module's stone. Eight columns and ten rows is exactly the 64x80 picture,
 |   which is why the module is ten by twelve: a frame all the way round it and
 |   not a pixel of slack, so the black an item with no picture gets fills the
 |   frame exactly as a picture would.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_PIC_X    (CV_OVERLAY_PANE_X + 1)
#define CV_OVERLAY_PIC_W    (CV_OVERLAY_PANE_W - 2)
#define CV_OVERLAY_PIC_ROWS (CV_OVERLAY_TALL_ROWS - 2)

/*----------------------
 | CV_OVERLAY_RISE
 | Description: How many rows the input line and the strip's own top border
 |   climb when the overlay takes its tall shape, so the strip's bottom border
 |   stays on the same row in both cases -- the transcript above the panel
 |   loses these rows instead of the input line being covered.
 | Author: suinevere
 ----------------------*/
#define CV_OVERLAY_RISE (CV_OVERLAY_TALL_ROWS - CV_STRIP_ROWS)

#endif /* PANEL_LAYOUT_H */
