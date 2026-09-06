/*----------------------
 | map_view.cxx
 | Description: Implements map_view.h.
 | Author: suinevere
 | Dependencies: map_model.h, map_atlas.h, map_layout.h, map_edges.h, dash_map.h,
 |   room_model.h, text_map.h, dash_view.h, title.h, room_art.h, display.h,
 |   app_state.h, input.h, saturn_keyboard.h, soft_reset.h, console_view.h,
 |   party.h, menu.h, menu/options.h, scene/presentation.h
 ----------------------*/
#include <srl.hpp>
#include "map_model.h"
#include "map_atlas.h"
#include "dash_map.h"
#include "room_model.h"
#include "text_map.h"
#include "dash_view.h"
#include "title.h"
#include "room_art.h"
#include "display.h"
#include "app_state.h"
#include "input.h"
#include "saturn_keyboard.h"
#include "soft_reset.h"
#include "console_view.h"
#include "menu.h"
#include "controller.h"
#include "../menu/options.h"
#include "party.h"
#include "map_layout.h"
#include "map_edges.h"
#include "../scene/presentation.h"
#include "map_view.h"

/*----------------------
 | MAP_VIS_MAX
 | Description: The most rooms the viewport can show at once -- one per grid
 |   cell -- and so the size of the hoisted walk's arrays.
 | Author: suinevere
 ----------------------*/
#define MAP_VIS_MAX  (MAP_ROOMS_W * MAP_ROOMS_H)

/*----------------------
 | MAP_GROUND_555
 | Description: The tan the ground is tinted to, as a VDP2 BGR555 word. The
 |   tiles are 4bpp indices into palette 1, so the whole ground is these
 |   sixteen CRAM entries and nothing in VRAM moves -- the same arithmetic the
 |   marble chrome already uses to sit on a coloured background.
 | Author: suinevere
 ----------------------*/
#define MAP_GROUND_555 0x2B5Eu

/*----------------------
 | MAP_BG_FILE / MAP_BG_TAG
 | Description: The sheet the map is drawn on when the story does not name one,
 |   and the name NBG0 records it under. A bare /TGA filename, because that is
 |   the directory title.cxx steps into; the tag is what title_bg_loaded_file
 |   answers while the map is up, and is deliberately not a CGL area stem so
 |   room_art's nbg0_shows_area can never mistake the sheet for a room picture
 |   it left there.
 |     The tag stays "MAP" for all four sheets. It says which LAYER holds what,
 |   not which file: only one sheet is ever held at a time, dropped on the way
 |   back to the title before another game can ask for a different one.
 | Author: suinevere
 ----------------------*/
#define MAP_BG_FILE "MAP.TGA"
#define MAP_BG_TAG  "MAP"

/*----------------------
 | MAP_BACK_555
 | Description: MAP_GROUND_555 as a colour rather than as a tint target, which
 |   is a different thing by one bit. dash_tint takes the tint apart into three
 |   channels and puts the opaque bit back itself when it writes CRAM
 |   (dash_view.cxx's write_palette ORs 0x8000), so the constant it is given does
 |   not carry one. A HighColor does: every colour SRL defines has bit 15 set --
 |   HighColor::Colors::Black is 0x8000, not 0 -- and a back-screen colour handed
 |   over without it comes out black, which is exactly what the first build of
 |   this did.
 | Author: suinevere
 ----------------------*/
#define MAP_BACK_555 ((unsigned short) (MAP_GROUND_555 | 0x8000u))

/*----------------------
 | MAP_INK_BLACK / MAP_INK_WHITE / MAP_INK_BROWN / MAP_INK_GRAY /
 | MAP_INK_RED / MAP_INK_GREEN / MAP_INK_BLUE
 | Description: The colours the map draws itself in. Black and white are the two
 |   answers to "what can be seen on this paper"; brown and grey are the two
 |   pencils the passages are drawn with, one for the warm sheets and one for
 |   the white; and the last three are the seat colours, saturated because they
 |   have to be told apart at one cell across on a television.
 | Author: suinevere
 ----------------------*/
#define MAP_INK_BLACK DISP_RGB555(0x00, 0x00, 0x00)
#define MAP_INK_WHITE DISP_RGB555(0xF0, 0xF0, 0xF0)
#define MAP_INK_BROWN DISP_RGB555(0x4A, 0x2E, 0x18)
#define MAP_INK_GRAY  DISP_RGB555(0x78, 0x78, 0x80)
#define MAP_INK_RED   DISP_RGB555(0xE0, 0x10, 0x20)
#define MAP_INK_GREEN DISP_RGB555(0x10, 0xB0, 0x30)
#define MAP_INK_BLUE  DISP_RGB555(0x20, 0x50, 0xE0)

/*----------------------
 | g_sheet
 | Description: Which of the four map sheets this game's page is drawn on, as
 |   pres_map_bg_index answers it. Recorded at preload because that is where the
 |   story's identity is in hand -- map_view_show takes no arguments and is
 |   reached from a menu that knows nothing about the story -- and read again on
 |   every open to choose the ink. Zero is the default parchment, which is what
 |   a build with no presentation table (the netbin) draws on.
 | Author: suinevere
 ----------------------*/
static int g_sheet = 0;

/*----------------------
 | MapInk
 | Description: The five colours one sheet gives the map: its labels, the ink
 |   its passages and glyphs are drawn in, the fill in the middle of a location
 |   mark, the local player's own colour for their figure, their roster line and
 |   their quadrant of a shared room's shield, and the colour the first other
 |   seat falls back to when the
 |   player already holds its red. The crosshair is not among them -- it keeps
 |   the accent and is red on all four sheets.
 |
 |   `clash` is in here rather than being a constant because it is the same
 |   question the other four ask: what can be seen on this paper. Black is right
 |   on three sheets and invisible on the fourth.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short text;
    unsigned short line;
    unsigned short fill;
    unsigned short party;
    unsigned short clash;
} MapInk;

/*----------------------
 | map_ink
 | Description: What the sheet this game was given asks for. The four sheets are
 |   paper of four different colours -- MAP.TGA is tan parchment, MAP2 cream
 |   ledger, MAP3 white, MAP4 solid black -- and that, not the genre printed on
 |   them, is the whole of what decides these: a mark has to be found on the
 |   paper it is drawn on, and two genres sharing a sheet share that problem.
 |
 |   The two warm papers take a drawing in dark brown with black-filled
 |   locations and black labels, which is what an Infocom map on parchment is.
 |   The white sheet keeps the black fill but takes grey passages, so the
 |   drawing does not read as heavier than it does on tan, and red for the
 |   labels and the player. The black sheet has no ink of its own to borrow, so
 |   it takes the player's own two terminal colours -- their letters for the
 |   passages and labels, their background for the fill -- and reads as their
 |   console rather than as paper.
 |
 |   The player's figure follows the labels on all four, which on the black
 |   sheet means their font colour. It was briefly black there, along with the
 |   other three sheets' figures, and black on black is nothing at all.
 | Author: suinevere
 | Dependencies: scene/presentation.h (PRES_MAP_BG order), display.h
 | Globals: g_display, g_sheet
 | Params: ink -- receives the five colours
 | Returns: N/A
 ----------------------*/
static void map_ink(MapInk *ink)
{
    ink->text  = MAP_INK_BLACK;
    ink->line  = MAP_INK_BROWN;
    ink->fill  = MAP_INK_BLACK;
    ink->clash = MAP_INK_BLACK;
    if (g_sheet == 2) {
        ink->text  = MAP_INK_RED;
        ink->line  = MAP_INK_GRAY;
    } else if (g_sheet == 3) {
        ink->text  = (unsigned short) display_text_rgb(g_display.text);
        ink->line  = ink->text;
        ink->fill  = (unsigned short) display_bg_rgb(g_display.bg);
        ink->clash = MAP_INK_WHITE;
    }
    ink->party = ink->text;
}

/*----------------------
 | map_ink_is_red
 | Description: Whether the local player's colour is close enough to the first
 |   other seat's red that the two could not be told apart. The seat colour
 |   gives way rather than the player's: the player's is chosen by the sheet and
 |   has to be readable on it, and a seat has no such constraint. Asked of the
 |   colour rather than of the sheet number, because the fourth sheet takes a
 |   font colour the player picked and that can be red as easily as anything --
 |   which is also why what it gives way TO comes off the sheet (MapInk::clash)
 |   rather than being black everywhere: the sheet a player is most likely to
 |   have chosen red letters on is the black one.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- a packed RGB555 colour
 | Returns: 1 when the colour reads as red, 0 otherwise
 ----------------------*/
static int map_ink_is_red(unsigned short c)
{
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return r >= 16 && r > g + 8 && r > b + 8;
}


/*----------------------
 | MAP_ROOM_DROP / MAP_ROW_ROSTER / MAP_ROW_TOP / MAP_TEXT_LEFT /
 | MAP_TEXT_COLS / MAP_PAGE_COLS
 | Description: The text rows written over the map and the band they may run
 |   in. All of them are derived from the grid rather than written down, because
 |   the point of them is to stay on the paper the grid was inset to reach.
 |
 |   The picked room's name has no row of its own. It is centred under the
 |   crosshair and moves with it -- MAP_ROOM_DROP cells below the mark, clear of
 |   the reticle's own bottom brackets -- because it names the thing the cursor
 |   is on and a caption belongs against what it captions. Reading it anywhere
 |   else means looking away from the cursor and back.
 |
 |   It is drawn over the map rather than beside it, which is what a caption
 |   does. The text layer is above the tile layer, so the label is never
 |   corrupted by what it lands on; but a passage running south out of the room
 |   the cursor is on runs straight down through its own name, and letters
 |   crossing a groove read as neither. The cells the label covers are therefore
 |   cleared to bare paper -- see MAP_CAP_ON -- and the whole plate steps aside
 |   for a third of the time so the drawing it is standing on can be read. The label is laid again from scratch
 |   every time the cursor moves, since draw_once opens with menu_clear.
 |
 |   The roster and the floor number share the row directly below the grid --
 |   row twenty-four, the last of MAP.TGA's solid band; row twenty-six, which is
 |   where the sheet is torn, is what the floor number used to be printed on.
 |   The local player is on that row and the others climb above it, so the name
 |   that is always there is always in the same place and a growing roster grows
 |   away from the corner rather than pushing the player's own line up the
 |   sheet.
 |
 |   There is no help row. It carried the pad legend, which said the same three
 |   things on every screen the map has ever been opened from.
 |
 |   MAP_TEXT_LEFT/COLS are the drawing box's own columns, gutter included,
 |   which is narrower than the forty a 320-pixel screen shows and much
 |   narrower than text_map's 64-cell pitch -- printing outside it writes cells
 |   that are either off the paper or off the screen. MAP_PAGE_COLS is what the
 |   floor number reserves at the right-hand end of the roster's own row.
 | Author: suinevere
 ----------------------*/
/* MAP_ROOM_DROP lives in map_layout.h: map_layout_updown has to keep the D
   off the caption's row, and that is worth a host test. */
#define MAP_ROW_ROSTER  (MAP_CELL_H)
#define MAP_ROW_TOP     (MAP_ROW_ROSTER - (PARTY_SEATS - 1))
#define MAP_TEXT_LEFT   MAP_CLIP_X0
#define MAP_TEXT_COLS   (MAP_CELL_W - MAP_CLIP_X0)
#define MAP_PAGE_COLS   10

/*----------------------
 | MAP_FLASH_SHIFT
 | Description: How long each half of a player mark's pulse lasts, as a power of
 |   two frames -- sixteen, so a little over a quarter second each way at 60Hz.
 |   The pulse is why this screen repaints anything per frame at all: everything
 |   else it draws is settled by draw_once and held by dash_map_hold.
 | Author: suinevere
 ----------------------*/
#define MAP_FLASH_SHIFT 4

/*----------------------
 | MAP_FLASH_MAX
 | Description: The most marks that can be pulsing at once -- one per seat, and
 |   one more for the local player, who has a mark whether or not the server has
 |   given them a seat.
 | Author: suinevere
 ----------------------*/
#define MAP_FLASH_MAX (PARTY_SEATS + 1)

/*----------------------
 | g_ids / g_dxs / g_dys
 | Description: The rooms inside the viewport and their offsets from the
 |   player, gathered once so the pairwise link walk that follows scans
 |   nothing. At file scope rather than on the stack because seventy entries of
 |   three arrays is more than a menu-depth stack wants to carry.
 | Author: suinevere
 ----------------------*/
static unsigned short g_ids[MAP_VIS_MAX];
static short          g_dxs[MAP_VIS_MAX];
static short          g_dys[MAP_VIS_MAX];

/*----------------------
 | g_slot
 | Description: Which gathered entry each object number landed in, or -1. The
 |   exit walk asks this once per exit; a scan of g_ids instead would put an
 |   O(n) search inside a loop that already runs n times, which is the shape
 |   that cost this screen a dozen frames a redraw once before.
 | Author: suinevere
 ----------------------*/
static short g_slot[MAP_ROOM_MAX];

/*----------------------
 | g_flash_x / g_flash_y / g_flash_tile / g_flash_n
 | Description: The cells holding a player's mark, gathered by draw_once so the
 |   frame loop can pulse them without repeating the room and link walk. Each
 |   alternates between its own tile and DT_ROOM, so a pulsing mark reads as a
 |   room that is being pointed at rather than as one blinking out of existence.
 | Author: suinevere
 ----------------------*/
static short         g_flash_x[MAP_FLASH_MAX];
static short         g_flash_y[MAP_FLASH_MAX];
static unsigned char g_flash_tile[MAP_FLASH_MAX];
static int           g_flash_n;

/*----------------------
 | MAP_CAP_ON / MAP_CAP_OFF / MAP_CAP_MAX
 | Description: How long the room caption stands and how long it stands down,
 |   in frames -- one second and a half at 60Hz -- and the most cells its plate
 |   can cover, which is the drawing's whole width.
 |
 |   The label is centred on the room it names, so it lands on top of whatever
 |   the map drew there; a room with a passage leaving south has that passage
 |   running down through its own name. Moving the label off the room it names
 |   is worse than the overlap, so instead the cells it covers are cleared to
 |   bare paper, and the whole thing steps aside for a third of the time and
 |   puts the drawing back. Neither the name nor what it
 |   covers is ever gone for long enough to be waited for.
 | Author: suinevere
 ----------------------*/
#define MAP_CAP_ON  60
#define MAP_CAP_OFF 30
#define MAP_CAP_MAX MAP_TEXT_COLS

/*----------------------
 | g_cap_x / g_cap_y / g_cap_n / g_cap_text / g_cap_under / g_cap_frame /
 | g_cap_on
 | Description: The caption as laid: the cell its left bracket went in, its row,
 |   how many cells the plate covers, the name itself, and the tiles that were
 |   under it, so the blink can put the drawing back and take it away again
 |   without repeating the room and link walk. g_cap_n is zero when there is no
 |   caption on screen, which is what makes the blink inert on a cursor sitting
 |   over empty ground.
 | Author: suinevere
 ----------------------*/
static short         g_cap_x, g_cap_y, g_cap_n;
static char          g_cap_text[MAP_CAP_MAX];
static unsigned char g_cap_under[MAP_CAP_MAX];
static int           g_cap_frame;
static int           g_cap_on;

/*----------------------
 | cap_show
 | Description: Puts the caption up or takes it down. Up is the plate -- the
 |   drawing cleared to bare paper -- and the name over it; down is the tiles that
 |   were there before and spaces where the letters were. Both write the same
 |   cells, so the two are exact inverses of each other and a blink cannot leak.
 | Author: suinevere
 | Dependencies: dash_map.h, text_map.h
 | Globals: g_cap_x, g_cap_y, g_cap_n, g_cap_text, g_cap_under
 | Params: on -- non-zero to show the caption, zero to hide it
 | Returns: N/A
 ----------------------*/
static void cap_show(int on)
{
    int i;
    if (g_cap_n <= 0) return;
    if (on) {
        for (i = 0; i < g_cap_n; i++) dash_map_paint(g_cap_x + i, g_cap_y, DT_BLANK);
        text_print_str(g_cap_x, g_cap_y, g_cap_text);
    } else {
        char blank[MAP_CAP_MAX + 1];
        for (i = 0; i < g_cap_n; i++) {
            dash_map_paint(g_cap_x + i, g_cap_y, g_cap_under[i]);
            blank[i] = ' ';
        }
        blank[i] = '\0';
        text_print_str(g_cap_x, g_cap_y, blank);
    }
}

/*----------------------
 | cap_covers
 | Description: Whether a cell is under the caption's plate right now. The
 |   pulse asks before repainting a player's mark: a mark two rows below the
 |   room the cursor is on falls inside the label, and one that kept pulsing
 |   there would punch a hole through it every half second.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_cap_on, g_cap_x, g_cap_y, g_cap_n
 | Params: x, y -- a cell
 | Returns: 1 when the caption is up and covering it, 0 otherwise
 ----------------------*/
static int cap_covers(int x, int y)
{
    return g_cap_on && g_cap_n > 0 && y == g_cap_y &&
           x >= g_cap_x && x < g_cap_x + g_cap_n;
}

/*----------------------
 | cap_lay
 | Description: Records where the caption is going and what it is covering, then
 |   shows it. Reading the cells back out of the shadow rather than recomputing
 |   them is what lets the blink restore a groove, a mark, a glyph or bare paper
 |   without knowing which of those it is putting back.
 | Author: suinevere
 | Dependencies: dash_map.h (dash_cell)
 | Globals: g_cap_*
 | Params: col, row -- where the name's first letter goes; nm, nc -- the name
 |   and its length
 | Returns: N/A
 ----------------------*/
static void cap_lay(int col, int row, const char *nm, int nc)
{
    int i;
    if (nc > MAP_CAP_MAX - 3) nc = MAP_CAP_MAX - 3;
    for (i = 0; i < nc; i++) g_cap_text[i] = nm[i];
    g_cap_text[nc] = '\0';
    g_cap_x = (short) (col - 1);
    g_cap_y = (short) row;
    g_cap_n = (short) (nc + 2);
    for (i = 0; i < g_cap_n; i++)
        g_cap_under[i] = dash_cell(g_cap_x + i, g_cap_y);
    g_cap_frame = 0;
    g_cap_on = 1;
    cap_show(1);
}

/*----------------------
 | gather
 | Description: Fills g_ids/g_dxs/g_dys with every placed room inside the
 |   viewport, in one pass over the object-number space. map_model_offset is
 |   constant time, where map_model_room_at costs an O(MAP_ROOM_MAX) scan per
 |   call -- which mattered, because draw_once used to nest that scan inside a
 |   pairwise loop and so spent about a dozen frames between one menu_sync and
 |   the next, long enough to starve the looping PCM hand-off.
 |
 |   Rooms on another floor are skipped here rather than at paint time, which is
 |   what keeps the links honest: a link is only drawn between two gathered
 |   rooms, so a staircase leaving this floor simply has no far end to draw to
 |   and no line is invented for it.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: g_ids, g_dxs, g_dys, g_slot
 | Params: sx, sy -- the scroll offset in rooms; page -- the floor being shown
 | Returns: how many rooms were gathered
 ----------------------*/
static int gather(int sx, int sy, int page)
{
    int r, n = 0;
    for (r = 0; r < MAP_ROOM_MAX; r++) g_slot[r] = -1;
    for (r = 1; r < MAP_ROOM_MAX && n < MAP_VIS_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (map_model_page((unsigned short) r) != page) continue;
        dx -= sx;
        dy -= sy;
        if (!map_layout_visible(dx, dy)) continue;
        g_ids[n] = (unsigned short) r;
        g_dxs[n] = (short) dx;
        g_dys[n] = (short) dy;
        g_slot[r] = (short) n;
        n++;
    }
    return n;
}

/*----------------------
 | extent
 | Description: The bounding box of every placed room on one floor, as offsets
 |   from the player. It is what the crosshair is clamped to, so the cursor
 |   cannot be walked off into empty ground and lost -- at either limit it sits
 |   on the outermost room of that floor rather than past it.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: N/A
 | Params: page -- the floor to measure; x0, x1, y0, y1 -- receive the box; all
 |   zero when the floor holds nothing placed
 | Returns: N/A
 ----------------------*/
static void extent(int page, int *x0, int *x1, int *y0, int *y1)
{
    int r, first = 1;
    *x0 = *x1 = *y0 = *y1 = 0;
    for (r = 1; r < MAP_ROOM_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (map_model_page((unsigned short) r) != page) continue;
        if (first) { *x0 = *x1 = dx; *y0 = *y1 = dy; first = 0; continue; }
        if (dx < *x0) *x0 = dx;
        if (dx > *x1) *x1 = dx;
        if (dy < *y0) *y0 = dy;
        if (dy > *y1) *y1 = dy;
    }
}

/*----------------------
 | put_uint
 | Description: One small unsigned decimal into a buffer, answering where it
 |   stopped. This screen shows two numbers -- a floor and a floor count -- and
 |   nothing else in it formats anything, so a digit loop is the whole
 |   requirement and pulling in a printf for it would not be.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- the buffer; at -- where to start; v -- the value
 | Returns: the index one past the last digit written
 ----------------------*/
static int put_uint(char *out, int at, unsigned int v)
{
    char tmp[6];
    int n = 0;
    do { tmp[n++] = (char) ('0' + (v % 10u)); v /= 10u; }
    while (v != 0u && n < (int) sizeof tmp);
    while (n > 0) out[at++] = tmp[--n];
    return at;
}

/*----------------------
 | page_field
 | Description: Where the floor number and its two paging arrows sit on the
 |   roster's row, and how many columns the number itself takes. The draw and the
 |   pointer hit test both call this, so a click cannot land beside the arrow it
 |   looks like it is on. The field ends three columns short of the drawing's
 |   right edge, which is where the sheet starts to tear.
 | Author: suinevere
 | Dependencies: put_uint
 | Globals: N/A
 | Params: page/pages -- the current floor and the count; x -- receives the
 |   field's first column; k -- receives the number's own width
 | Returns: N/A
 ----------------------*/
static void page_field(int page, int pages, int *x, int *k)
{
    char pg[MAP_PAGE_COLS];
    int n = put_uint(pg, 0, (unsigned int) (page + 1));
    pg[n++] = '/';
    n = put_uint(pg, n, (unsigned int) pages);
    *k = n;
    *x = MAP_TEXT_LEFT + MAP_TEXT_COLS - 3 - (n + 4);
}

/*----------------------
 | map_cell_offset
 | Description: The room offset a text cell falls on -- map_layout_cell run
 |   backwards, rounded to the nearest room -- which is how a click on the paper
 |   becomes a place to put the crosshair.
 | Author: suinevere
 | Dependencies: map_layout.h
 | Globals: N/A
 | Params: cell -- the column or row clicked; s -- the view's offset; centre --
 |   MAP_CX or MAP_CY; base -- MAP_LEFT or MAP_TOP
 | Returns: the room offset
 ----------------------*/
static int map_cell_offset(int cell, int s, int centre, int base)
{
    int rel = cell - base + MAP_CELLS / 2;
    int q   = rel >= 0 ? rel / MAP_CELLS : -(((-rel) + MAP_CELLS - 1) / MAP_CELLS);
    return s + q - centre;
}

/*----------------------
 | peer_slot
 | Description: Which of the three other-seat colours a seat is drawn in. The
 |   seats are numbered by the server and one of them is ours, so the colour is
 |   the seat's number with our own taken out of the count -- red, green, blue
 |   in seat order, and the same colour for the same person however many people
 |   join or leave, which a running index over the occupied seats would not
 |   give.
 |
 |   Before the server has said which seat is ours there is no seat to take out,
 |   so the fourth is the one with no colour. That is the honest answer for a
 |   state in which we do not know which of the four is the player holding the
 |   pad; it lasts until the first roster frame.
 | Author: suinevere
 | Dependencies: party.h
 | Globals: N/A
 | Params: seat -- 0 to PARTY_SEATS-1
 | Returns: 0, 1 or 2, or -1 for our own seat and for a seat past the colours
 ----------------------*/
static int peer_slot(int seat)
{
    int self = party_self();
    int slot;
    if (seat < 0 || seat >= PARTY_SEATS) return -1;
    if (seat == self) return -1;
    slot = (self >= 0 && seat > self) ? seat - 1 : seat;
    return (slot < DT_PARTY_INKS - 1) ? slot : -1;
}

/*----------------------
 | room_party
 | Description: Who is standing in a room, as the bit mask DT_SHIELD0 is
 |   indexed by: bit 0 the local player, bits 1..3 the other seats in
 |   peer_slot's order.
 |
 |   The local player's bit is taken from map_model_current rather than from
 |   their seat, because that answers offline and before the server has said
 |   which seat is ours -- which is every disc game and the first seconds of
 |   every netbin one.
 | Author: suinevere
 | Dependencies: party.h, map_model.h, peer_slot
 | Globals: N/A
 | Params: room -- object number
 | Returns: the mask, 0 when nobody is there
 ----------------------*/
static int room_party(unsigned short room)
{
    int i, mask = 0;
    if (room == 0) return 0;
    if (room == map_model_current()) mask |= DT_SHIELD_SELF;
    for (i = 0; i < PARTY_SEATS; i++) {
        unsigned short rm = 0;
        int slot = peer_slot(i);
        if (slot < 0) continue;
        if (!party_seat(i, &rm, 0)) continue;
        if (rm == room) mask |= 1 << (slot + 1);
    }
    return mask;
}

/*----------------------
 | party_one
 | Description: The single bit set in a mask, or -1 when it holds none or more
 |   than one. Which is the question the mark asks: one occupant takes the mark
 |   they already had and a figure in their own colour beside it, and two or
 |   more take the quartered shield instead, because there is no room for two
 |   figures beside one cell.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: mask -- a room_party mask
 | Returns: 0..3, or -1
 ----------------------*/
static int party_one(int mask)
{
    int i, found = -1;
    for (i = 0; i < DT_PARTY_INKS; i++) {
        if (!(mask & (1 << i))) continue;
        if (found >= 0) return -1;
        found = i;
    }
    return found;
}

/*----------------------
 | paint_knight
 | Description: Stands a figure beside a mark, two cells wide by three tall
 |   with one cell of clearance so a link leaving west still shows where it
 |   goes. It goes to the right of the mark instead when the left would run off
 |   the viewport: dash_map_paint drops cells it cannot place, so the
 |   alternative is not a knight that hangs over the edge but half a knight,
 |   which reads as a drawing fault rather than as a figure.
 |
 |   One figure per person on the map, each in its own colour, so the roster
 |   below and the drawing above name the same people. Which colour is which
 |   ink is the tile set's business, not this function's: the caller hands over
 |   the first tile of one of the five drawings.
 |
 |   A room two or more people share gets one figure between them -- there is no
 |   space beside a cell for a second -- drawn in the neutral ink, with the four
 |   quadrants of the shield on its arm filled in the colours of whoever is
 |   standing there. That is `mask`.
 |
 |   It is painted after the links and before the crosshair, so it covers a
 |   groove running under it and the cursor covers it. A figure that a link was
 |   drawn through would look like part of the map.
 | Author: suinevere
 | Dependencies: dash_map.h, map_layout.h
 | Globals: N/A
 | Params: mx, my -- the cell holding the mark; base -- the first of the six
 |   tiles of one figure; mask -- the occupancy to quarter its shield by, or 0
 |   to leave the shield blank
 | Returns: N/A
 ----------------------*/
static void paint_knight(int mx, int my, int base, int mask)
{
    int kx, ky, tx, ty;
    map_layout_knight(mx, my, DT_KNIGHT_W, &kx, &ky);
    for (ty = 0; ty < DT_KNIGHT_H; ty++)
        for (tx = 0; tx < DT_KNIGHT_W; tx++) {
            int cell = ty * DT_KNIGHT_W + tx;
            int t = base + cell;
            /* The two cells the shield falls across come from the mask sets
               instead, which carry the same figure with its quadrants filled. */
            if (mask != 0) {
                if      (cell == DT_SHIELD_HI_CELL) t = DT_SHIELD_HI0 + mask;
                else if (cell == DT_SHIELD_LO_CELL) t = DT_SHIELD_LO0 + mask;
            }
            dash_map_paint(kx + tx, ky + ty, (unsigned char) t);
        }
}

/*----------------------
 | knight_tiles
 | Description: The first tile of the figure drawn in one party colour: the
 |   local player's is the set the accent colours, and the other three follow it
 |   in seat order.
 | Author: suinevere
 | Dependencies: dash_map.h
 | Globals: N/A
 | Params: ink -- 0 for the local player, 1..3 for the other seats
 | Returns: a DT_* tile index
 ----------------------*/
static int knight_tiles(int ink)
{
    if (ink <= 0) return DT_KNIGHT0;
    return DT_KNIGHT_PEER0 + (ink - 1) * DT_KNIGHT_CELLS;
}

/*----------------------
 | seat_line
 | Description: One roster line -- a name, a colon and the room that seat is
 |   standing in -- into a caller's buffer.
 | Author: suinevere
 | Dependencies: party.h, room_model.h
 | Globals: N/A
 | Params: seat -- the seat to name; line, cap -- the buffer and its size
 | Returns: 1 when the seat was occupied and a line was written, 0 otherwise
 ----------------------*/
static int seat_line(int seat, char *line, int cap)
{
    unsigned short rm = 0;
    const char *nm = 0;
    int k = 0;
    if (!party_seat(seat, &rm, &nm)) return 0;
    while (nm[k] != '\0' && k < PARTY_NAME_MAX - 1 && k < cap - 3) {
        line[k] = nm[k];
        k++;
    }
    line[k++] = ':';
    line[k++] = ' ';
    room_model_object_name(rm, line + k, cap - k);
    return 1;
}

/*----------------------
 | draw_players
 | Description: Writes the roster into the bottom-left corner: one row per seat
 |   the server has told us about, naming who is in the game and the room they
 |   are standing in. The local player is on the lowest row, beside the floor
 |   number, and the others climb above them in seat order -- so the one line
 |   that is always there is always in the same place, and a roster that grows
 |   grows away from the corner rather than pushing the player's own line up the
 |   sheet.
 |
 |   An empty roster is not an error and is the ordinary state on a disc, which
 |   has no server to hear from. It falls back to the one line the local player
 |   deserves either way, labelled rather than named because offline there is
 |   nobody to have a name. The same fallback covers a netbin that has heard a
 |   roster but not yet been told which seat is ours.
 |
 |   Every room name comes from the story image the client already holds, so
 |   naming where somebody else is standing costs no traffic and works for a
 |   room this map has never drawn -- which on a difficulty that shows only what
 |   has been walked into is most of them.
 |
 |   Each line is written in that seat's own colour, which is the same colour
 |   their figure is drawn in and the same quadrant colour their shield takes in
 |   a shared room. A roster that named people the drawing above could not be
 |   matched to was a legend with nothing to look up.
 | Author: suinevere
 | Dependencies: party.h, room_model.h, text_map.h, map_model.h, seat_line
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void draw_players(void)
{
    char line[MAP_TEXT_COLS - MAP_PAGE_COLS];
    int i, self = party_self(), row = MAP_ROW_ROSTER;

    if (self < 0 || !seat_line(self, line, (int) sizeof line)) {
        static const char lbl[] = "Player: ";
        int k = 0;
        while (lbl[k] != '\0') { line[k] = lbl[k]; k++; }
        room_model_object_name(map_model_current(), line + k,
                               (int) sizeof line - k);
    }
    text_print_ink(MAP_TEXT_LEFT, row--, line, 0);

    for (i = PARTY_SEATS - 1; i >= 0 && row >= MAP_ROW_TOP; i--) {
        int slot = peer_slot(i);
        if (i == self) continue;
        if (!seat_line(i, line, (int) sizeof line)) continue;
        /* A seat past the three colours the map tells apart has none, and
           text_print_ink reads that -1 as the plain ink. */
        text_print_ink(MAP_TEXT_LEFT, row--, line, slot < 0 ? -1 : slot + 1);
    }
}

/*----------------------
 | edge_stub
 | Description: Draws the short run that says a passage leaves this room toward
 |   one the viewport does not reach. gather() only collects rooms inside the
 |   viewport, and the link pass can only join two gathered rooms, so an exit
 |   whose far end has scrolled off drew nothing at all: step the crosshair one
 |   room and every passage back the way you came vanished rather than running
 |   to the edge. This lays MAP_GUTTER cells of the same dashed-or-solid run a
 |   link is made of, into the margin the grid is inset by.
 |
 |   Only for a room on the floor being shown. An exit to another floor is not
 |   a passage running off the edge of this one, and the U/D pass below already
 |   gives it a letter and a stub of its own; drawing this as well would put two
 |   marks on one exit.
 |
 |   The run carries the exit's own decoration, not the forced dash a stub to
 |   another floor takes: this far end is a room on this floor that has merely
 |   scrolled off, and the passage to it is as solid or as conditional as one
 |   drawn end to end.
 |
 |   The direction is the axis the far room actually left the viewport by, not
 |   the larger of its two offsets: a room one step north-east that is off the
 |   top but not off the right side is reached northward, and a stub pointing
 |   east at a column still on screen would name a passage that is not there.
 |   Off a corner, both are true and the longer leg wins.
 | Author: suinevere
 | Dependencies: map_model.h, map_edges.h, map_layout.h
 | Globals: g_dxs, g_dys
 | Params: cx, cy -- the room's mark cell; i -- its slot, for its own offset;
 |   ex -- the exit; sx, sy -- the view's offset in rooms; page -- the floor
 | Returns: N/A
 ----------------------*/
static void edge_stub(int cx, int cy, int i, const MapExit *ex,
                      int sx, int sy, int page) {
    int dx = 0, dy = 0, ox, oy;

    if (map_model_page(ex->dest) != page) return;
    if (!map_model_offset(ex->dest, &dx, &dy)) return;
    dx -= sx;
    dy -= sy;

    ox = (dx < MAP_DX_MIN) ? -1 : (dx > MAP_DX_MAX) ? 1 : 0;
    oy = (dy < MAP_DY_MIN) ? -1 : (dy > MAP_DY_MAX) ? 1 : 0;
    if (ox != 0 && oy != 0) {
        int ax = dx - g_dxs[i], ay = dy - g_dys[i];
        if (ax < 0) ax = -ax;
        if (ay < 0) ay = -ay;
        if (ay > ax) ox = 0; else oy = 0;
    }
    if (ox == 0 && oy == 0) return;

    map_edges_offview(cx, cy, ox, oy, ex->flags);
}

/*----------------------
 | pick
 | Description: The room drawn at one cell of one floor, or zero when none is.
 |   draw_once works this out as it paints, for the caption; the paging rule
 |   needs it before anything is painted, so it is asked here.
 | Author: suinevere
 | Dependencies: map_model.h
 | Globals: N/A
 | Params: page -- the floor; hx, hy -- the cell, as offsets from the player
 | Returns: the object number, or 0
 ----------------------*/
static unsigned short pick(int page, int hx, int hy)
{
    int r;
    for (r = 1; r < MAP_ROOM_MAX; r++) {
        int dx = 0, dy = 0;
        if (!map_model_offset((unsigned short) r, &dx, &dy)) continue;
        if (map_model_page((unsigned short) r) != page) continue;
        if (dx == hx && dy == hy) return (unsigned short) r;
    }
    return 0;
}

/*----------------------
 | draw_once
 | Description: Paints one whole frame of the map: every room of one floor the
 |   viewport reaches, the links between them, a figure beside every player
 |   standing on it, the crosshair, the picked room's name captioned under it,
 |   and the roster and the floor number on one row in the bottom-left corner.
 |   The
 |   ground is not among them: it is the parchment behind this layer, and every
 |   cell nothing is painted into shows it through. Clears the text layer
 |   first,
 |   because the caller is the Options menu, which redraws its title and every
 |   row each frame and would otherwise leave them lit over a map whose box
 |   border dash_map_begin has just blanked.
 |
 |   Every pair of gathered rooms the story links is joined, at whatever
 |   distance -- map_edges_link's header (map_edges.h) has why that is not the
 |   rule it used to be. Links are routed into map_edges first and the whole
 |   layer swept from it afterwards, so a cell two links cross knows about both
 |   and draws as a crossing; painting each link as it was routed would have
 |   left whichever came last. Marks go down after that again, so a mark always
 |   wins its own cell.
 |
 |   The walk is gather() and then a per-room enumeration of what it returned,
 |   both bounded by the viewport rather than by the placed set, which is what
 |   keeps this inside one frame; it used to nest a pairwise scan inside a
 |   pairwise loop and spent about a dozen frames between one menu_sync and the
 |   next, long enough to starve the looping PCM hand-off.
 |
 |   Only one floor is drawn. The floors of an authored table are separate
 |   drawings the publisher split because the geography did, and the table
 |   stacks them into one coordinate space only because it has nowhere else to
 |   put them; showing them all at once shows a tall strip with empty ground
 |   between the parts. A story with no authored table has exactly one floor,
 |   so nothing is hidden from one.
 |
 |   A second walk runs after the links are in, placing the self-loop, U/D and
 |   arrowhead glyphs map_layout_glyph finds room for. A vertical exit whose
 |   destination is on this floor now also labels its own mark from this same
 |   walk, preferring the step toward that destination; one leaving the floor
 |   lays a dashed stub first, since its own run is not already drawn. It has
 |   to come after: the glyph pass reads map_edges_layer to see which cells the
 |   lines already claimed, and asks first before the stub is laid so laying it
 |   cannot push its own letter onto a diagonal by marking its own two cells
 |   occupied. Where the search finds nothing free, this draws neither the stub
 |   nor the letter -- the same declining-is-honest rule the rest of the
 |   placement follows.
 |
 |   Paint order is links (including the arrows and dashes map_edges_tile now
 |   folds in), then marks, then the figure, then the crosshair, and each step
 |   is allowed to cover the one before it. That ordering is the whole priority
 |   rule: a mark wins its own cell over a groove crossing it, the figure wins
 |   over a groove running under it -- a link drawn through it would read as
 |   part of the map -- and the cursor wins over everything, because a cursor
 |   showing the map through itself is harder to find than the room it is
 |   pointing at.
 |
 |   Called on open, on each cursor step that moves the view, and on each floor
 |   change; not per frame. map_view_show holds the NBG2 claim between those with
 |   dash_map_hold and repaints only the pulsing marks, whose cells this leaves
 |   in g_flash_*; the text this writes needs no such upkeep, since text_map has
 |   no per-frame expiry.
 | Author: suinevere
 | Dependencies: map_model.h, dash_map.h, text_map.h, room_model.h, party.h,
 |   menu.h, map_edges.h, map_layout.h, gather, paint_knight, knight_tiles,
 |   draw_players, room_party, party_one, put_uint
 | Globals: g_ids, g_dxs, g_dys, g_flash_x, g_flash_y, g_flash_tile, g_flash_n
 | Params: sx, sy -- the scroll offset in rooms, zero with the player centred;
 |   page -- the floor to draw; hx, hy -- the crosshair, in the same offsets
 |   from the player that map_model_offset answers in
 | Returns: N/A
 ----------------------*/
static void draw_once(int sx, int sy, int page, int hx, int hy) {
    int n, i;
    int hvx = hx - sx, hvy = hy - sy;
    int hcx = map_layout_cell(hx, sx, MAP_CX, MAP_LEFT);
    int hcy = map_layout_cell(hy, sy, MAP_CY, MAP_TOP);
    unsigned short hover = 0;

    menu_clear();
    dash_map_begin();
    g_flash_n = 0;
    /* Dropped before anything is painted, so a cursor that has moved onto empty
       ground leaves the blink with nothing to restore rather than putting the
       last room's cells back over the new drawing. cap_lay sets it again. */
    g_cap_n = 0;

    // No ground is painted. The map's ground is the parchment on NBG0, or the
    // tan back colour where there is no parchment, and every cell this layer
    // does not claim is DT_BLANK already -- dash_map_begin clears it to that.
    // Paving the viewport with DT_GROUND, which is what this used to do, would
    // hide whichever of the two is behind it.
    n = gather(sx, sy, page);

    map_edges_reset();
    for (i = 0; i < n; i++)
        map_edges_mark(map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT),
                       map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP));

    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int k, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        for (k = 0; k < ne; k++) {
            int j, lo, hi, arrow;
            if (ex[k].flags & MAP_EXIT_SELF) continue;
            j = g_slot[ex[k].dest];
            if (j < 0) {
                // A staircase off the edge is the glyph pass's, not a run
                // toward wherever the room above happens to be drawn.
                if (map_layout_offview(ex[k].kind == MAP_LINK_VERT, 0) ==
                    MAP_OFFVIEW_RUN)
                    edge_stub(map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT),
                              map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP),
                              i, &ex[k], sx, sy, page);
                continue;
            }
            if (!(ex[k].flags & MAP_EXIT_ONEWAY) && ex[k].dest < g_ids[i])
                continue;
            if (g_ids[i] < ex[k].dest) { lo = i; hi = j; } else { lo = j; hi = i; }
            arrow = !(ex[k].flags & MAP_EXIT_ONEWAY) ? 0
                    : (ex[k].dest == g_ids[hi]) ? 1 : 2;
            map_edges_link(map_layout_cell(g_dxs[lo], 0, MAP_CX, MAP_LEFT),
                           map_layout_cell(g_dys[lo], 0, MAP_CY, MAP_TOP),
                           map_layout_cell(g_dxs[hi], 0, MAP_CX, MAP_LEFT),
                           map_layout_cell(g_dys[hi], 0, MAP_CY, MAP_TOP),
                           map_model_link(g_ids[i], ex[k].dest),
                           ex[k].flags, arrow);
        }
    }

    for (i = 0; i < n; i++) {
        MapExit ex[RM_DIR_N];
        int cx = map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT);
        int cy = map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP);
        int k, gx, gy, ne = map_model_exits(g_ids[i], ex, RM_DIR_N);
        const unsigned short (*layer)[MAP_CELL_W] =
            (const unsigned short (*)[MAP_CELL_W]) map_edges_layer();

        for (k = 0; k < ne; k++) {
            int up;
            if (ex[k].flags & MAP_EXIT_SELF) {
                int sdx, sdy;
                map_model_step(ex[k].dir, &sdx, &sdy);
                if (map_layout_glyph(cx, cy, sdx, sdy, layer, &gx, &gy))
                    map_edges_glyph(gx, gy, MAP_EDGE_LOOP);
                continue;
            }
            if (ex[k].kind != MAP_LINK_VERT) continue;
            // MAP_DIR_VERT admits RM_UP and RM_DOWN and nothing else, so this
            // is the whole of the question. It used to read the parity of the
            // direction index, which is true of that pair by coincidence and
            // was false the moment RM_IN and RM_OUT reached here: it made
            // every `out` a D and every `in` a U.
            up = (ex[k].dir == RM_UP);
            // Every staircase gets its letter in the same place, whether its
            // far end is gathered, off screen, on another floor, or -- for one
            // the story decides by running code -- not stated at all. It used
            // to aim the letter at the far room when that room was on this
            // floor, which between two rooms a cell apart put it nearer the far
            // one: Third Floor's U sat against the Roof and read as the Roof's.
            // A staircase has no direction on the page to point in, so the
            // letter says only that there is a way up, and it says it beside
            // the room it belongs to.
            if (map_layout_updown(cx, cy, up, layer, &gx, &gy))
                map_edges_glyph(gx, gy, up ? MAP_EDGE_UP : MAP_EDGE_DOWN);
        }
    }

    for (i = MAP_CLIP_Y0; i < MAP_CELL_H; i++) {
        int c;
        for (c = MAP_CLIP_X0; c < MAP_CELL_W; c++) {
            unsigned char t = map_edges_tile(c, i);
            if (t) dash_map_paint(c, i, t);
        }
    }

    for (i = 0; i < n; i++) {
        int cx = map_layout_cell(g_dxs[i], 0, MAP_CX, MAP_LEFT);
        int cy = map_layout_cell(g_dys[i], 0, MAP_CY, MAP_TOP);
        int picked = (g_dxs[i] == (short) hvx && g_dys[i] == (short) hvy);
        int party = room_party(g_ids[i]);
        int lone = party_one(party);
        unsigned char tile = DT_ROOM;
        int flash = 0;

        /* Every occupied room keeps the mark the map has always drawn -- one
           occupant or four, the cell says only "somebody is standing here", and
           it is the figure beside it that says who. The mark used to be
           quartered for a shared room, which put four colours in one cell that
           is eight pixels across and already carries a ring, a core, a pulse and
           whatever grooves run under it; the shield the figure is holding has
           room for the same four and nothing else to say. */
        if (party) {
            tile = (party & DT_SHIELD_SELF) ? DT_ROOM_HERE : DT_ROOM_PEER;
            flash = 1;
        }
        /* The pick recolours an ordinary room and leaves a player's mark alone.
           The crosshair opens sitting on the local player, so a pick that
           overrode every mark would hide the one thing the screen is for and
           stop its pulse before the player had touched anything -- the four
           brackets say what is picked in either case, and they are drawn last
           so they say it over whatever the mark turned out to be. */
        if (picked) {
            hover = g_ids[i];
            if (tile == DT_ROOM) tile = DT_ROOM_SEL;
        }

        dash_map_paint(cx, cy, tile);
        if (flash && g_flash_n < MAP_FLASH_MAX) {
            g_flash_x[g_flash_n] = (short) cx;
            g_flash_y[g_flash_n] = (short) cy;
            g_flash_tile[g_flash_n] = tile;
            g_flash_n++;
        }
        if (lone >= 0)  paint_knight(cx, cy, knight_tiles(lone), 0);
        else if (party) paint_knight(cx, cy, DT_KNIGHT_PARTY0, party);
    }

    dash_map_paint(hcx - 1, hcy - 1, DT_XHAIR_TL);
    dash_map_paint(hcx + 1, hcy - 1, DT_XHAIR_TR);
    dash_map_paint(hcx - 1, hcy + 1, DT_XHAIR_BL);
    dash_map_paint(hcx + 1, hcy + 1, DT_XHAIR_BR);

    {
        char nm[MAP_TEXT_COLS - 2];
        int nc, ncol;
        char pg[8];
        int pages = map_model_pages();
        int k;

        draw_players();

        /* Centred on the crosshair's own column and clamped into the drawing's
           columns, so a room picked at either edge of the viewport still reads
           left to right on paper rather than running off it. The clamp moves
           the label, not the cursor: a caption pinned under a cursor two cells
           from the edge would be half off the sheet. */
        if (hover != 0 && room_model_object_name(hover, nm, (int) sizeof nm)) {
            for (nc = 0; nm[nc] != '\0'; nc++) { }
            ncol = hcx - nc / 2;
            if (ncol + nc > MAP_TEXT_LEFT + MAP_TEXT_COLS)
                ncol = MAP_TEXT_LEFT + MAP_TEXT_COLS - nc;
            if (ncol < MAP_TEXT_LEFT) ncol = MAP_TEXT_LEFT;
            cap_lay(ncol, hcy + MAP_ROOM_DROP, nm, nc);
        }

        /* The floor number ends three columns short of the drawing's right
           edge and shares the roster's row: the corner itself is where the
           sheet starts to tear, and the two things that are on the screen
           whatever the map is showing belong on one line. */
        {
            int px;
            page_field(page, pages, &px, &k);
            k = put_uint(pg, 0, (unsigned int) (page + 1));
            pg[k++] = '/';
            k = put_uint(pg, k, (unsigned int) pages);
            pg[k] = '\0';
            text_print_str(px + 2, MAP_ROW_ROSTER, pg);
            /* The arrows are the only thing on this screen a mouse or a gun can
               turn a floor with: L and R are pad buttons and there is nothing
               else here to point at. Drawn only when there is somewhere to go. */
            if (pages > 1) {
                text_print_str(px, MAP_ROW_ROSTER, "<");
                text_print_str(px + k + 3, MAP_ROW_ROSTER, ">");
            }
        }

        text_flush();
    }
}

/*----------------------
 | map_view_show
 | Description: See map_view.h. Holds itself the way every full-screen page in
 |   menu_pages.cxx does (credits_page is the closest analog): a loop that polls
 |   input, checks for an exit, and otherwise advances the frame -- not a single
 |   draw followed by menu_wait's generic block, which does not re-touch
 |   dash_map's NBG2 claim and so would lose it a frame after draw_once painted
 |   it.
 |
 |   The D-pad moves a crosshair rather than the map. It is the same
 |   pad_repeat_update/pad_fired pair every other page uses for held movement, so
 |   the delay before it runs on matches the rest of the interface, and the room
 |   under it is named along the bottom. The view follows the cursor instead of
 |   being steered: it only moves when the cursor would otherwise leave the
 |   viewport, and then by exactly enough to keep it inside. That is what lets
 |   one control do both jobs -- a map that scrolled under a fixed cursor would
 |   need a second binding to reach a room the scroll clamp had stopped short of.
 |
 |   extent() clamps the cursor to the rooms placed on the floor being shown, so
 |   it cannot be walked off into empty ground and lost, and the view is clamped
 |   only by following it.
 |
 |   L and R change floor. They are free here -- A, B, C and Start are all back,
 |   and the D-pad is the cursor -- and they wrap, since a two-floor game would
 |   otherwise need the player to remember which way they had come. A floor
 |   change recentres both cursor and view on that floor's own extent, because
 |   the offsets that put the player in the middle of one floor point at nothing
 |   on another.
 |
 |   Neither the cursor nor the floor is carried across an open. The player is
 |   centred and the floor is theirs each time the screen appears, however far
 |   it was scrolled when it last closed -- there is no position to restore
 |   because none is kept.
 |
 |   draw_once runs on open and on each step that changes something, and not on
 |   frames where nothing moved. What does run every frame is the pulse: the
 |   marks draw_once left in g_flash_* alternate with DT_ROOM every sixteen
 |   frames, so a player's own room and everybody else's beat against a map that
 |   is otherwise still. It is done by repainting those few cells rather than by
 |   redrawing, because redrawing is the room and link walk and that is not free.
 |
 |   dash_tint rewrites the sixteen CRAM entries every NBG2 tile draws from, so
 |   the tan is captured on the way in and put back on the way out; without that
 |   the gamepad strip and every menu box wear it for the rest of the session.
 |
 |   Difficulty decides how much of the map there is. Easy reveals the whole
 |   authored table on open, which is the reveal the development switch used to
 |   force; Medium takes it back and draws only what the player has walked into,
 |   still placed where the atlas says. Both consult the same table, so Easy on a
 |   story nobody drew is Medium by falling through map_model_reveal_atlas rather
 |   than by testing for the table here. Hard never reaches this function at all
 |   -- options_menu drops the Map row -- and this deliberately does not check for
 |   it a second time: a page that silently closed itself would read as a broken
 |   menu rather than as a disabled feature.
 |
 |   The clear on Medium is not tidiness. A placed room never moves and the model
 |   has no other memory of how it came to be placed, so without it one open on
 |   Easy would leave the whole drawing on the map for the rest of the session,
 |   through every later difficulty change.
 |
 |   The wallpaper is replaced for the map's duration by the parchment and
 |   restored by asking room_art for the room again, which re-uploads because
 |   the parchment has taken the layer's recorded name with it. None of that is compiled into the netbin, which has neither room art
 |   nor a title background to put back -- the three symbols it would need
 |   (title_bg_hide, room_art_available, room_art_reshow) are the only ones in
 |   this file that build does not already link, which is why they are the only
 |   thing guarded rather than the file being split.
 |
 |   The wallpaper used to be restored by re-showing title_bg_loaded_file() by
 |   name, which was wrong twice over: for a CGL frame that name is an area stem
 |   and no file, so the picture never came back, and on a game with no art at
 |   all the name was still the boot logo's -- so closing the map put the
 |   SUINEVERE logo up behind the game and left it there.
 | Author: suinevere
 | Dependencies: draw_once, extent, map_model.h (map_model_reveal_atlas,
 |   map_model_clear_reveal, map_model_page, map_model_pages), dash_map.h,
 |   dash_view.h, menu.h, input.h, saturn_keyboard.h, soft_reset.h,
 |   console_view.h, title.h, room_art.h, display.h, app_state.h
 | Globals: g_difficulty, g_flash_x, g_flash_y, g_flash_tile, g_flash_n
 | Params: N/A
 | Returns: N/A
 ----------------------*/
/*----------------------
 | map_view_preload
 | Description: See map_view.h.
 | Author: suinevere
 | Dependencies: title.h (title_bg_hold)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void map_view_preload(unsigned int release, const char *serial) {
#ifndef NETBIN
    const char *sheet;
    g_sheet = pres_map_bg_index(release, serial);
    sheet = pres_map_bg(release, serial);
    title_bg_hold((sheet != nullptr) ? sheet : MAP_BG_FILE);
#else
    (void) release; (void) serial;
#endif
}

extern "C" void map_view_show(void) {
    MenuBacking backing;
    int sx = 0, sy = 0, hx = 0, hy = 0;
    // Where the reader last asked to be, which is not the same as where
    // the crosshair is: a floor holding one room drags the crosshair onto
    // it, and if that were remembered as the request then paging through
    // such a floor would erase the place the reader was keeping. The
    // D-pad sets this; a floor change only reads it.
    int wx = 0, wy = 0;
    int pages, page, frame = 0, phase = -1;
    unsigned short tint = dash_tint_current();
    bool parchment = false;
#ifndef NETBIN
    const bool had_art = (g_display.palette == DISP_PAL_DYNAMIC)
                         && room_art_available() != 0;

    // The parchment goes on NBG0 rather than into the tile layer because NBG2
    // has one plane and the marks have to sit over the paper, not carry a patch
    // of it each. NBG0 is already the layer below NBG2 -- priorities are 1 and 2
    // -- so it needs no reordering, and room_art puts the room back on it when
    // the map closes.
    //
    // Read once and held for the rest of the session. The map is opened and
    // closed repeatedly with a CD-DA track playing, and tga_decode is the one
    // thing here that touches the drive; a read per open would stop the music
    // every time, which is the reason the room pictures stopped being TGAs.
    // Asked, never read. The read is map_view_preload's, done under the loading
    // ramp before CD-DA has started, because a data seek silences the track
    // whatever else is going on -- and a track that was never held reads to the
    // engine as one that ended, so it restarts from the top rather than
    // resuming. That is the cut in and out this used to make on the first open.
    // Reading here was worse than once, too: tga_decode reads the header before
    // it checks the heap, so on a story too large to hold the picture the seek
    // happened on every open and still put no paper up.
    if (title_bg_held()) parchment = title_bg_show_held(MAP_BG_TAG);
    if (!parchment) title_bg_hide();
#endif
    // The menu that opened this armed the VDP2 window that suppresses NBG0
    // inside a box -- MenuBacking's constructor switches it on and every
    // menu_frame aims it at whatever box is being drawn -- and nothing re-aims
    // it for a full-screen page that draws no box. Left on, it cuts the last
    // menu's rectangle out of the parchment and the back colour shows through:
    // nineteen cells by fifteen of black in the middle of the sheet. It was
    // invisible for as long as the map paved itself with opaque ground, and
    // appeared the moment it stopped.
    image_window_off();

    // Where there is no parchment -- the netbin, which has no drive, and a disc
    // whose MAP.TGA would not read -- the back colour is the ground instead, so
    // the marks sit on flat tan rather than on black.
    //
    // Not touched when there is one. The sheet's torn edges are transparent by
    // design and are drawn to read against a dark ground; filling that with tan
    // would flatten the edge the picture is shaped around.
    // The override alongside the colour, or the fade below undoes this on its
    // first frame: every ramp recomputes the backdrop from the player's own
    // background setting, so the tan lasted until the screen started coming up
    // and the map arrived on their colour instead. Nothing saw it while a
    // parchment covered the ground; on a story too large to hold one it was the
    // entire screen.
    if (!parchment) {
        SRL::VDP2::SetBackColor(SRL::Types::HighColor(MAP_BACK_555));
        menu_back_override(MAP_BACK_555);
    }
    // The ink first, then the party colours over it. text_set_color carries the
    // dash_tint the map's ground needs -- the two have to be one call or the
    // labels and the marks would be lit by different grounds -- and dash_map_ink
    // has to follow it, because a tint rewrites all sixteen entries from the
    // ramp and would take the borrowed slots back.
    //
    // The crosshair is not among the colours set here. It is drawn in the accent
    // and the accent is red whatever the sheet, because the cursor is the one
    // mark on the screen a player looks for rather than reads: a reticle that
    // changed colour with the paper would have to be found before it could be
    // used.
    //
    // Two calls rather than one because they answer different questions. What
    // the drawing is made of is a property of the paper; what colour a seat is
    // is a property of the party, and the paper has no say in it beyond the one
    // clash rule below.
    //
    // The map's labels do not take the player's font colour. It is chosen to be
    // read on their own background and this screen is not on it: a bright green
    // that is right on black is barely there on tan paper. The one sheet where
    // it IS the ink is the one with nothing of its own to be read against.
    {
        MapInk ink;
        unsigned short seat0;
        map_ink(&ink);
        seat0 = map_ink_is_red(ink.party) ? ink.clash : MAP_INK_RED;
        text_set_color(ink.text, MAP_GROUND_555);
        dash_map_ink(ink.line, ink.fill);
        dash_map_party(ink.party, seat0, MAP_INK_GREEN, MAP_INK_BLUE);
        /* The same four colours again on the text layer, so the roster's names
           are written in the inks their figures and shields are drawn in. Four
           palettes rather than four fonts: text_print_ink moves a cell onto one
           with a bit-or and no VRAM traffic at all. */
        text_set_party_ink(0, ink.party);
        text_set_party_ink(1, seat0);
        text_set_party_ink(2, MAP_INK_GREEN);
        text_set_party_ink(3, MAP_INK_BLUE);
    }
    if (g_difficulty == DIFF_EASY) map_model_reveal_atlas();
    else                           map_model_clear_reveal();

    pages = map_model_pages();
    page = map_model_page(map_model_current());
    if (page >= pages) page = pages - 1;
    if (page < 0)      page = 0;

    draw_once(sx, sy, page, hx, hy);
    menu_sync();
    // The Options row that opens this fades to black first, the same as it does
    // for every other page it can reach, so the map has to bring the screen back
    // up itself or it is drawn where nobody can see it. Guarded on
    // g_menu_page_fade like menu_pages.cxx's page_fade_in/out, and after the
    // first menu_sync for the reason those are: menu_fade_in reveals a frame
    // that is already composed.
    if (g_menu_page_fade > 0) menu_fade_in(g_menu_page_fade);
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool back = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
                    g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
                    ke.kind != SATURN_KEY_NONE || menu_pointer_back();
        if (back) break;

        pad_repeat_update();
        {
            int nx = hx, ny = hy, np = page, x0, x1, y0, y1;
            /* A wheel steers the crosshair and paddles it up and down: the map
               is the one screen with no rows to step, and without this a player
               holding one can open it and not move on it. */
            if (pad_fired_raw(Button::Left)  || controller_nav_fired(NAV_LEFT))  nx--;
            if (pad_fired_raw(Button::Right) || controller_nav_fired(NAV_RIGHT)) nx++;
            if (pad_fired_raw(Button::Up)    || controller_nav_fired(NAV_UP))    ny--;
            if (pad_fired_raw(Button::Down)  || controller_nav_fired(NAV_DOWN))  ny++;
            if (pad_fired_raw(Button::L))     np--;
            if (pad_fired_raw(Button::R))     np++;
            /* A click on an arrow turns the floor; a click anywhere on the paper
               puts the crosshair on the room nearest it, which is the only way a
               pointing device has of picking a room -- it has no D-pad to walk
               one cell at a time with. The clamp below is what keeps a click off
               the edge of the drawing from asking for a room that is not there. */
            if (menu_pointer_act()) {
                const DevPointer *pt = controller_pointer();
                int px, pk;
                page_field(page, pages, &px, &pk);
                if (pages > 1 && pt->row == MAP_ROW_ROSTER &&
                    pt->col >= px && pt->col <= px + 1)                    np--;
                else if (pages > 1 && pt->row == MAP_ROW_ROSTER &&
                         pt->col >= px + pk + 2 && pt->col <= px + pk + 3) np++;
                else if (pt->row >= MAP_CLIP_Y0 && pt->row < MAP_ROW_ROSTER &&
                         pt->col >= MAP_CLIP_X0 && pt->col < MAP_CELL_W) {
                    nx = map_cell_offset(pt->col, sx, MAP_CX, MAP_LEFT);
                    ny = map_cell_offset(pt->row, sy, MAP_CY, MAP_TOP);
                }
            }
            if (np < 0)       np = pages - 1;
            if (np >= pages)  np = 0;
            // np is only ever a request for "the floor below" or "the floor
            // above" now; which floor that is comes from the staircase below.

            if (np != page) {
                unsigned short over = pick(page, hx, hy), climb = 0;
                // L and R follow the staircase out of the room under the
                // crosshair, and fall back on the page index only when that
                // room has none.
                //
                // Paging by index alone cannot be made to work and no numbering
                // of the pages would fix it: a page index is one line and a
                // story's floors are a tree. The Lurking Horror's ground level
                // has three floors above it and three below, so at most one of
                // each can ever be the page next door -- ordering the pages by
                // drawn sheet then height leaves three of its fifteen
                // staircases running BACKWARDS, and ordering by height first
                // trades those three for ten that skip. Both were built and
                // measured. The room knows the answer the atlas cannot: up is
                // the floor this room's own stair reaches.
                if (over != 0 && map_model_climb(over, np > page, &climb) &&
                    map_model_page(climb) != page) {
                    page = map_model_page(climb);
                    if (!map_model_offset(climb, &hx, &hy)) { hx = wx; hy = wy; }
                } else {
                    page = np;
                    hx = wx;
                    hy = wy;
                    if (!map_model_nearest(page, hx, hy, &hx, &hy)) {
                        extent(page, &x0, &x1, &y0, &y1);
                        map_layout_clamp(x0, x1, y0, y1, &hx, &hy);
                    }
                }
                // Centred on the landing, not merely brought into view.
                // map_layout_follow moves the view the least it can, which is
                // right for a cursor step -- the map should not lurch under a
                // D-pad press -- and wrong here: it leaves the landing on
                // whichever edge it entered by, with the rest of the floor
                // beyond it off screen. That is what hid Third Floor behind the
                // Roof two rows above the top of the viewport.
                sx = hx;
                sy = hy;
                draw_once(sx, sy, page, hx, hy);
                phase = -1;
            } else if (nx != hx || ny != hy) {
                extent(page, &x0, &x1, &y0, &y1);
                map_layout_clamp(x0, x1, y0, y1, &nx, &ny);
                if (nx != hx || ny != hy) {
                    hx = nx;
                    hy = ny;
                    wx = hx;
                    wy = hy;
                    map_layout_follow(hx, hy, &sx, &sy);
                    draw_once(sx, sy, page, hx, hy);
                    phase = -1;
                }
            }
        }

        dash_map_hold();
        {
            int ph = (frame >> MAP_FLASH_SHIFT) & 1, i;
            if (ph != phase) {
                phase = ph;
                for (i = 0; i < g_flash_n; i++) {
                    if (cap_covers(g_flash_x[i], g_flash_y[i])) continue;
                    dash_map_paint(g_flash_x[i], g_flash_y[i],
                                   ph ? g_flash_tile[i] : DT_ROOM);
                }
            }
        }
        /* Counted from the caption being laid rather than off `frame`, so a
           cursor moved during the off second gets its new label at once instead
           of arriving into the gap the old one had left. */
        {
            int on = (g_cap_frame % (MAP_CAP_ON + MAP_CAP_OFF)) < MAP_CAP_ON;
            if (on != g_cap_on) { g_cap_on = on; cap_show(on); }
            g_cap_frame++;
        }
        frame++;
        menu_sync();
    }

    if (g_menu_page_fade > 0) menu_fade_out(g_menu_page_fade);
    {
        /* The whole band the map may have written in. The room's caption goes
           wherever the cursor was, so there is no single row to name. */
        int r;
        for (r = MAP_CLIP_Y0; r <= MAP_ROW_ROSTER; r++) text_clear_line(r);
    }
    text_flush();
    // Both the map's ink and its ground handed back in one call, the same way
    // they were taken: this rewrites the whole ramp, which is what calls the
    // three borrowed party slots back in to the stone.
    text_set_color((unsigned short) display_text_rgb(g_display.text), tint);
    if (!parchment) {
        // After the fade-out above, so that ramp takes the tan down rather than
        // the player's colour, and before the page underneath fades itself back
        // in on its own.
        menu_back_override(0);
        // Back to what SRL::Core::Initialize set in both builds (main.cxx:361,
        // main_netbin.cxx:251), so this restores a known value rather than a
        // guess.
        SRL::VDP2::SetBackColor(SRL::Types::HighColor::Colors::Black);
    }
    // And the window back on for the page underneath, which is where it came
    // from: this screen is only ever reached from a menu, so a MenuBacking is
    // always up around it and wants its box suppressing the image again. Its
    // rectangle is still aimed where that page last put it, and the page aims
    // it again on its next menu_frame either way. If this were ever the
    // outermost backing instead, the destructor below re-defers the switch-off
    // and undoes this, which is also right.
    image_window_on();
#ifndef NETBIN
    // room_art_reshow re-uploads rather than trusting what is on the layer --
    // its own check is against the area NBG0 records, and the parchment
    // records MAP -- so the room comes back whether or not it was there before.
    // A game with no art has nothing to put back, so the parchment is taken
    // down instead of being left standing behind the console.
    if (had_art)         room_art_reshow();
    else if (parchment)  title_bg_hide();
#endif
}
