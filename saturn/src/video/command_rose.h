/*----------------------
 | command_rose.h
 | Description: Composition of the travel module's compass rose from decoded
 |   exit states, the chord key that stands in for it while the modifier is held,
 |   and the grid the cursor walks over the rose. Seven rows of thirteen
 |   columns, drawn so that an unavailable direction erases its own spoke as well
 |   as its label -- the rose is a map of the room rather than a menu of twelve
 |   buttons. Pure string building and index arithmetic; the view prints what
 |   this returns dim and overprints the selected label at full brightness.
 |   Implemented in command_rose.c.
 |
 |   The shape:
 |
 |       UP         IN
 |         NW N NE
 |           \|/
 |         W -+- E
 |           /|\
 |         SW S SE
 |       DOWN      OUT
 |
 |   Up, down, in and out sit in the corners as words rather than as markers on
 |   the vertical spokes, which is what lets all twelve directions be cursor
 |   targets in one grid instead of four of them being special cases.
 | Author: suinevere
 | Dependencies: room_model.h (the RM_* direction indices and exit states)
 ----------------------*/
#ifndef COMMAND_ROSE_H
#define COMMAND_ROSE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CR_ROWS / CR_COLS
 | Description: The rose's drawn shape, in text cells.
 | Author: suinevere
 ----------------------*/
#define CR_ROWS  7
#define CR_COLS 13

/*----------------------
 | CR_GRID_ROWS / CR_GRID_COLS
 | Description: The logical grid the cursor moves over -- five rows of three,
 |   with the centre column empty on the corner rows and holding the '+' on the
 |   middle one. Deliberately not the drawn geometry: the drawn rose has blank
 |   spoke rows between its label rows, and a cursor that had to step through
 |   them would take two presses to move between neighbours.
 | Author: suinevere
 ----------------------*/
#define CR_GRID_ROWS 5
#define CR_GRID_COLS 3

/*----------------------
 | cr_row
 | Description: Composes one rose row into `out` as exactly CR_COLS characters
 |   plus a NUL. An open direction prints uppercase, a conditional or
 |   undecodable one lowercase, and an absent or message-only one prints spaces
 |   along with its spoke. The centre '+' is always drawn.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: exits -- RM_DIR_N exit states; row -- 0..CR_ROWS-1; out -- receives
 |   CR_COLS + 1 bytes
 | Returns: N/A
 ----------------------*/
void cr_row(const unsigned char *exits, int row, char *out);

/*----------------------
 | CR_KEY_MID / CR_KEY_LABEL_W
 | Description: The chord key's centre column -- the rose's own, so the two
 |   blocks share an axis -- and the width one label may take. Four columns is
 |   what the row through the centre affords on each side of the marker, and it is
 |   why a chord's two directions are named rather than described.
 | Author: suinevere
 ----------------------*/
#define CR_KEY_MID      6
#define CR_KEY_LABEL_W  4

/*----------------------
 | CrKey
 | Description: What the chord does, by direction: the D-pad's vertical and
 |   horizontal pairs and the trigger pair, each label null where that gesture is
 |   unbound or unreachable. A trigger is unreachable when the modifier is that
 |   trigger -- the modifier cannot be its own direction.
 |     Labels rather than action names: the block is a compass, and a compass says
 |   which way a press goes, not what the thing it moves is called. Line and Page
 |   are deliberately both "Up"/"Down" because four columns cannot tell them
 |   apart, and a player holding the modifier is asking which way, not which
 |   binding.
 | Author: suinevere
 ----------------------*/
typedef struct {
    const char *up, *down;      /* the modifier + D-pad Up/Down slot */
    const char *left, *right;   /* the modifier + D-pad Left/Right slot */
    const char *ltrig, *rtrig;  /* the modifier + L/R trigger slot */
} CrKey;

/*----------------------
 | cr_key_row
 | Description: Composes one row of the chord key into `out` as exactly CR_COLS
 |   characters plus a NUL -- the block the travel module shows in the rose's
 |   place while the modifier is held. It is the rose's own shape with the chord's
 |   labels in it: the trigger pair on the corner row, the vertical pair above and
 |   below the centre with arrows for spokes, and the horizontal pair through the
 |   centre marker, which is always drawn exactly as the rose's is. An unbound
 |   pair takes its arrows with it, the way an absent exit takes its spoke.
 |   Nothing else is written here -- the bare tap is a real gesture but not a
 |   direction, and a line of prose under a compass reads as a caption on it.
 |     Takes composed labels rather than the mapping itself so this stays a
 |   layout function in C: the slot and action constants live in input.h, which is
 |   C++, and the caller is already the side that knows which button the modifier
 |   is on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: k -- the labels; row -- 0..CR_ROWS-1; out -- receives CR_COLS + 1 bytes
 | Returns: N/A
 ----------------------*/
void cr_key_row(const CrKey *k, int row, char *out);

/*----------------------
 | cr_grid_dir
 | Description: The direction occupying one logical grid cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: grow -- 0..CR_GRID_ROWS-1; gcol -- 0..CR_GRID_COLS-1
 | Returns: an RM_* index, or -1 for a gap or an out-of-range cell
 ----------------------*/
int cr_grid_dir(int grow, int gcol);

/*----------------------
 | cr_dir_cell
 | Description: Where a direction's label is drawn, so the view can overprint it
 |   highlighted without re-deriving the layout.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dir -- an RM_* index; row, col, len -- (out) drawn row, first column
 |   and label width; any may be null
 | Returns: 1 when dir is a rose direction, 0 otherwise (outputs untouched)
 ----------------------*/
int cr_dir_cell(int dir, int *row, int *col, int *len);

/*----------------------
 | cr_dir_row
 | Description: The drawn row a direction's label sits on -- what the view maps
 |   through when focus crosses into or out of the module, so the cursor arrives
 |   beside where it left rather than at the top.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dir -- an RM_* index
 | Returns: 0..CR_ROWS-1, or -1 when dir is not a rose direction
 ----------------------*/
int cr_dir_row(int dir);

/*----------------------
 | cr_dir_word
 | Description: The canonical spelling of a direction as Infocom's parsers hold
 |   it, so a rose selection can be submitted as a typed command. Duplicates
 |   room_model.c's own table deliberately: this one carries no story dependency,
 |   so a build with no interpreter can still name a direction.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dir -- one of the RM_* direction indices
 | Returns: the word, or "" when dir is out of range
 ----------------------*/
const char *cr_dir_word(int dir);

/*----------------------
 | cr_enter
 | Description: The direction the cursor should land on when focus arrives in
 |   the module. Searches outward from `want_row` so the cursor keeps the row it
 |   crossed at when that row has an exit, and falls to the nearest row that
 |   does when it has not; within a row the column nearest the edge focus came
 |   through is preferred.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: exits -- RM_DIR_N exit states; want_row -- the drawn row to aim for,
 |   0..CR_ROWS-1; from_right -- 1 when focus arrived from the module to the
 |   right, 0 otherwise
 | Returns: an RM_* index, or -1 when the room offers no direction at all
 ----------------------*/
int cr_enter(const unsigned char *exits, int want_row, int from_right);

/*----------------------
 | cr_move
 | Description: Aims the press and takes whichever available direction it points
 |   at best -- nearest in bearing first, then nearest in distance. Geometric
 |   rather than a table of neighbours, because the rose is not a full grid: half
 |   its cells are missing in any given room, and a rule written in terms of the
 |   cells that would be there has to answer for every combination that is not.
 |   Aiming answers all of them the same way, so a press with north gone finds
 |   whatever is next along that bearing rather than a special case.
 |
 |   Diagonal presses are the same operation with a diagonal vector, which is
 |   what makes up-and-right from west find north -- squarely on the bearing --
 |   ahead of north-east, which is further off it.
 |
 |   A vertical press with nothing ahead of it wraps to the far side, so a short
 |   column can be ridden round rather than stopping at the pole. A press leaning
 |   right with nothing ahead of it leaves the module instead: travel is the
 |   leftmost of the three, so right is the only side with anywhere to go, and
 |   that is what guarantees the cursor can always get out.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: exits -- RM_DIR_N exit states; dir -- the RM_* index under the cursor;
 |   dx, dy -- each -1, 0 or +1, both set for a diagonal; out -- receives the new
 |   RM_* index, unchanged when the press does not move
 | Returns: 0 when the cursor stayed in the rose, +1 when the press carried focus
 |   off the right edge
 ----------------------*/
int cr_move(const unsigned char *exits, int dir, int dx, int dy, int *out);

#ifdef __cplusplus
}
#endif
#endif /* COMMAND_ROSE_H */
