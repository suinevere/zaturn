/*----------------------
 | rose_draw.cxx
 | Description: The rose-row draw call described in rose_draw.h, moved here from
 |   command_view.cxx so console_view can draw a rose without pulling the command
 |   panel's 10 KB of image and 15 KB of .bss into a build that never shows it.
 |   The chord key lives here too, and not in either interface, because both of
 |   them reach the travel module through this one call: the swap has to happen
 |   below the fork or the panel and the in-game keyboard would drift apart.
 | Author: suinevere
 | Dependencies: rose_draw.h, command_rose.h, command_view.h, text_map.h, input.h
 ----------------------*/
#include "rose_draw.h"
#include "command_rose.h"
#include "command_view.h"
#include "text_map.h"
#include "input.h"

/*----------------------
 | cv_dir_label
 | Description: What one direction of a chord action does, in the four columns
 |   the key's compass gives a label. Line and Page share "Up"/"Down" on purpose:
 |   there is no room to tell them apart and no need to, since a player holding
 |   the modifier is asking which way the D-pad goes rather than which of the two
 |   bindings answers. Recall and Autocomplete both step a list, so both read
 |   Prev/Next.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: N/A
 | Params: act -- one of the CA_* constants; dir -- -1 or +1
 | Returns: the label, or null for an action with no label
 ----------------------*/
static const char *cv_dir_label(int act, int dir) {
    if (act == CA_LINE || act == CA_PAGE)     return dir < 0 ? "Up" : "Down";
    if (act == CA_HOMEEND)                    return dir < 0 ? "Home" : "End";
    if (act == CA_RECALL || act == CA_AUTO)   return dir < 0 ? "Prev" : "Next";
    return 0;
}

/*----------------------
 | cv_slot_owner
 | Description: Which chord action holds a slot, or -1 when none does. There is
 |   at most one: chord_assign swaps rather than shares, and every chord is in the
 |   one group, so a slot cannot end up with two owners to choose between.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: g_chord_slot
 | Params: slot -- one of the SL_* constants
 | Returns: a CA_* action, or -1
 ----------------------*/
static int cv_slot_owner(int slot) {
    for (int a = 0; a < CA_N; a++) if (g_chord_slot[a] == slot) return a;
    return -1;
}

/*----------------------
 | cv_draw_key_row
 | Description: Draws one row of the chord key in the travel module's place,
 |   composed from the live mapping. Dim throughout: nothing on it is selected,
 |   and an unfocused module is what the rest of the strip looks like.
 | Author: suinevere
 | Dependencies: command_rose.h, input.h, text_map.h, command_view.h
 | Globals: g_chord_slot
 | Params: row -- 0..CR_ROWS-1; y -- text-map cell row
 | Returns: N/A
 ----------------------*/
static void cv_draw_key_row(int row, int y) {
    CrKey k;
    char buf[CR_COLS + 1];
    int v = cv_slot_owner(SL_CUD), h = cv_slot_owner(SL_CLR), t = cv_slot_owner(SL_CT);
    Button c = chord_btn_button();
    k.up    = (v >= 0) ? cv_dir_label(v, -1) : 0;
    k.down  = (v >= 0) ? cv_dir_label(v, +1) : 0;
    k.left  = (h >= 0) ? cv_dir_label(h, -1) : 0;
    k.right = (h >= 0) ? cv_dir_label(h, +1) : 0;
    /* Only the trigger the modifier has left free: it cannot be its own
       direction, so under the default R the key names L and nothing else --
       naming both would name a gesture the pad cannot make. */
    k.ltrig = (t >= 0 && c != Button::L) ? cv_dir_label(t, -1) : 0;
    k.rtrig = (t >= 0 && c != Button::R) ? cv_dir_label(t, +1) : 0;
    cr_key_row(&k, row, buf);
    text_print_dim(CV_TRAVEL_X, y, buf);
}

void cv_draw_rose_row(int row, const unsigned char *exits, int y, int sel) {
    /* Holding the modifier turns the whole module into the key for it: the rose
       is a list of directions the D-pad is not addressing while the chord has
       it, so leaving it up would be showing the player the one thing their pad
       cannot do just then. */
    if (chord_mod_held()) { cv_draw_key_row(row, y); return; }
    char buf[CR_COLS + 1];
    int srow, scol, slen;
    cr_row(exits, row, buf);
    text_print_dim(CV_TRAVEL_X, y, buf);
    if (sel < 0 || !cr_dir_cell(sel, &srow, &scol, &slen) || srow != row) return;
    if (buf[scol] == ' ') return;
    {
        char label[5];
        int i;
        for (i = 0; i < slen && i < (int) sizeof label - 1; i++) label[i] = buf[scol + i];
        label[i] = '\0';
        text_print(CV_TRAVEL_X + scol, y, label);
    }
}
