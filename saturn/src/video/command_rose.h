/*----------------------
 | command_rose.h
 | Description: Composition of the travel module's compass rose from decoded
 |   exit states. Five rows of thirteen columns, drawn so that an unavailable
 |   direction erases its own spoke as well as its label -- the rose is a map of
 |   the room rather than a menu of twelve buttons. Pure string building; the
 |   view prints what this returns and overprints the up and down markers in
 |   reverse video. Implemented in command_rose.c.
 | Author: suinevere
 | Dependencies: room_model.h (the RM_* direction indices and exit states)
 ----------------------*/
#ifndef COMMAND_ROSE_H
#define COMMAND_ROSE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CR_ROWS / CR_COLS / CR_UP_L / CR_UP_R
 | Description: The rose's shape, and the two inner columns the up and down
 |   markers occupy -- the view needs them to overprint those cells highlighted.
 | Author: suinevere
 ----------------------*/
#define CR_ROWS  5
#define CR_COLS 13
#define CR_UP_L  3
#define CR_UP_R  9

/*----------------------
 | cr_row
 | Description: Composes one rose row into `out` as exactly CR_COLS characters
 |   plus a NUL. An open direction prints uppercase, a conditional or
 |   undecodable one lowercase, and an absent or message-only one prints spaces
 |   along with its spoke. Available in and out replace the vertical spokes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: exits -- RM_DIR_N exit states; row -- 0..CR_ROWS-1; out -- receives
 |   CR_COLS + 1 bytes
 | Returns: N/A
 ----------------------*/
void cr_row(const unsigned char *exits, int row, char *out);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_ROSE_H */
