/*----------------------
 | save_ui.cxx
 | Description: Save/restore slot-picker UI. Builds per-game backup filenames,
 |   chooses the backup device, and runs the combined slot picker + in-place name
 |   editor over the SGL backup library. Called by main.cxx's save/restore flow.
 | Author: suinevere
 | Dependencies: save_ui.h, menu.h (menu_select/menu_frame/menu_clear/MenuBacking),
 |   menu_layout.h (menu_box_fit/menu_visible_digit/MENU_DIGIT_COLS), keyboard.h
 |   (on-screen keyboard state/layout), saturn_backup.h (SATURN_BUP_* ids and bup
 |   queries), saturn_keyboard.h (key events), input.h (g_pad), console_view.h
 |   (note_input_device/hint/g_kbd_visible), app_state.h (g_story_filename),
 |   saturn_glue.h (SAVE_BLOB_MAX), map_model.h (MAP_BLOB_MAX), game_catalog.h
 |   (scan_z3_folder), SRL -- whose
 |   srl.hpp is also what gives SYS_Exit its C linkage, by including sega_sys.h
 |   inside an extern "C" block of its own.
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"

#include "save_ui.h"
#include "menu.h"
extern "C" {
#include "menu_layout.h"
}
#include "keyboard.h"
#include "saturn_backup.h"
#include "saturn_keyboard.h"
#include "input.h"
#include "console_view.h"
#include "app_state.h"
#include "saturn_glue.h"
#include "map_model.h"
#include "game_catalog.h"

/*----------------------
 | snprintf (extern)
 | Description: Forward declaration for snprintf, which links from newlib but is
 |   omitted by the SRL dummy <stdio.h>.
 | Author: suinevere
 ----------------------*/
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

/*----------------------
 | slot_name_for
 | Description: Copies a story filename's base (up to 9 chars, stopping at the
 |   extension dot, forcing lowercase to uppercase) into `out`, then appends the
 |   slot digit and a NUL. The per-game prefix is what keeps each game's slots
 |   separate on the same backup device. Takes the story rather than reading
 |   g_story_filename, because the boot-time check asks after games the player has
 |   not chosen -- and, at that point, could not have chosen.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: story -- the story's CD filename; out -- destination buffer (>= 11
 |   bytes); slot -- appended as one digit
 | Returns: N/A
 ----------------------*/
static void slot_name_for(const char *story, char *out, int slot) {
    int i = 0;
    for (const char *p = story; *p && *p != '.' && i < 9; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[i++] = c;
    }
    out[i++] = (char) ('0' + slot);
    out[i] = 0;
}

/*----------------------
 | make_slot_name
 | Description: slot_name_for against the story the player is playing.
 | Author: suinevere
 | Dependencies: app_state.h (g_story_filename)
 | Globals: g_story_filename
 | Params: out -- destination buffer (>= 11 bytes); slot -- appended as one digit
 | Returns: N/A
 ----------------------*/
void make_slot_name(char *out, int slot) {
    slot_name_for(g_story_filename, out, slot);
}

/*----------------------
 | choose_device
 | Description: Assembles the device list (console always; cartridge only when
 |   saturn_bup_present reports one), runs it through menu_select, and maps the
 |   chosen row back to its SATURN_BUP_* id.
 | Author: suinevere
 | Dependencies: menu.h (menu_select), saturn_backup.h (saturn_bup_present /
 |   SATURN_BUP_CONSOLE / SATURN_BUP_CARTRIDGE)
 | Globals: N/A
 | Params: title -- menu title
 | Returns: chosen SATURN_BUP_* id, or -1 if cancelled
 ----------------------*/
int choose_device(const char *title) {
    const char *dev_items[2];
    int dev_ids[2];
    int ndev = 0;
    dev_items[ndev] = "Console (internal)"; dev_ids[ndev] = SATURN_BUP_CONSOLE; ndev++;
    if (saturn_bup_present(SATURN_BUP_CARTRIDGE)) {
        dev_items[ndev] = "Cartridge"; dev_ids[ndev] = SATURN_BUP_CARTRIDGE; ndev++;
    }
    g_menu_intro_fade = g_menu_page_fade;   // fades in from black in the title-menu Load flow; no-op in-game
    static int dev_sel = 0;   // held across visits, like every other list
    int d = menu_select_at(title, dev_items, ndev, &dev_sel);
    return (d < 0) ? -1 : dev_ids[d];
}

/*----------------------
 | pick_slot_and_name
 | Description: Two-state modal over one box. In the PICK state the player
 |   navigates the slot list (D-pad / number keys; the list never scrolls, so a
 |   digit maps directly to a slot) and picks one; in the EDIT state that slot's
 |   name is edited in place on its own line, the box growing to hold the
 |   on-screen keyboard. Picking pre-fills the editor with the slot's current
 |   name and drops the keyboard cursor on that name's last character.
 |   Ctrl+C (SATURN_KEY_CLEAR) blanks the field, Backspace/B leaves EDIT back to
 |   PICK, and A/Enter/Start confirms.
 |
 |   The box is sized every frame rather than once, because its shape changes
 |   when `editing` flips. A slot row budgets the reserved "N) " digit columns
 |   (reserved whether or not drawn) and the widest label actually present, with
 |   a floor of 10 chars (saturn_bup_info caps a comment at 10; "(empty)" is 7);
 |   the edit row budgets maxchars plus the caret. It budgets no cursor mark,
 |   because there is none: the selected row is the one drawn at full
 |   brightness, the rest in the dim ink. In EDIT
 |   the width must also cover the keyboard (KB_COLS*2). Neither state carries a
 |   controls hint -- only the confirm box still spells the buttons out -- which
 |   is also why the box no longer has to budget the longer of a pad/keyboard
 |   hint pair against a mid-menu device switch. That same row width is what
 |   every row pads to, so the centred list keeps one left edge.
 | Author: suinevere
 | Dependencies: menu.h (MenuBacking/menu_clear/menu_frame), menu_layout.h
 |   (menu_box_fit/menu_visible_digit/MENU_DIGIT_COLS), keyboard.h (KeyboardState
 |   and helpers/KB_LAYOUT/KB_ROWS/KB_COLS), saturn_backup.h (SAVE_SLOTS/
 |   saturn_bup_info), saturn_keyboard.h (saturn_keyboard_poll/SATURN_KEY_*),
 |   input.h (g_pad/Button), console_view.h (note_input_device/g_kbd_visible)
 | Globals: g_pad, g_kbd_visible
 | Params: device -- SATURN_BUP_* target; out_slot -- receives chosen slot;
 |   out_name -- receives edited name (empty if blank); maxchars -- name cap
 | Returns: 1 with *out_slot / out_name set, or 0 if cancelled
 ----------------------*/
int pick_slot_and_name(int device, int *out_slot, char *out_name, int maxchars) {
    MenuBacking backing;

    char slotname[SAVE_SLOTS][12];
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char fn[12];
        make_slot_name(fn, i);
        if (!saturn_bup_info(device, fn, slotname[i])) slotname[i][0] = '\0';
    }

    static int last_sel = 0;   // held across visits; SAVE_SLOTS never changes
    int sel = last_sel;
    int editing = 0;
    KeyboardState k;
    keyboard_reset(&k);
    SRL::Core::Synchronize();

    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);

        if (!editing) {
            bool pick = false, cancel = false;
            if (ke.kind == SATURN_KEY_ENTER) pick = true;
            else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
            else if (ke.kind == SATURN_KEY_CHAR) {
                int idx = menu_visible_digit(ke.ch, 0, SAVE_SLOTS, SAVE_SLOTS);
                if (idx >= 0) { sel = idx; pick = true; }
            } else {
                if (g_pad->WasPressed(Button::Up))   sel = (sel - 1 + SAVE_SLOTS) % SAVE_SLOTS;
                if (g_pad->WasPressed(Button::Down)) sel = (sel + 1) % SAVE_SLOTS;
                if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                    || g_pad->WasPressed(Button::START)) pick = true;
                if (g_pad->WasPressed(Button::B)) cancel = true;
            }
            last_sel = sel;   // after every move, so no exit path has to remember to
            if (cancel) return 0;
            if (pick) {
                keyboard_reset(&k);
                for (int i = 0; slotname[sel][i] && k.input_len < maxchars; i++)
                    keyboard_type_char(&k, slotname[sel][i]);
                if (k.input_len > 0) {
                    char last = k.input[k.input_len - 1];
                    for (int r = 0; r < KB_ROWS; r++)
                        for (int c = 0; c < KB_COLS; c++)
                            if (KB_LAYOUT[r][c] == last) { k.cursor_row = r; k.cursor_col = c; }
                }
                editing = 1;
                SRL::Core::Synchronize();
                continue;
            }
        } else {
            bool submit = false;
            if (ke.kind == SATURN_KEY_ENTER) submit = true;
            else if (ke.kind == SATURN_KEY_ESCAPE) { editing = 0; SRL::Core::Synchronize(); continue; }
            else if (ke.kind == SATURN_KEY_CLEAR) { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
            else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
            else if (ke.kind == SATURN_KEY_CHAR) { if (k.input_len < maxchars) keyboard_type_char(&k, ke.ch); }
            else {
                if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
                if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
                if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
                if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
                if (g_pad->WasPressed(Button::C))     { if (k.input_len < maxchars) keyboard_type(&k); }
                if (g_pad->WasPressed(face_button(FA_SPACE)))
                                                     { if (k.input_len < maxchars) keyboard_type_char(&k, ' '); }
                if (g_pad->WasPressed(Button::B))     { editing = 0; SRL::Core::Synchronize(); continue; }
                if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)) submit = true;
            }
            if (submit) {
                int n = k.input_len;
                if (n > maxchars) n = maxchars;
                for (int i = 0; i < n; i++) out_name[i] = k.input[i];
                out_name[n] = '\0';
                *out_slot = sel;
                return 1;
            }
        }

        const char *btitle = editing ? "NAME THIS SAVE" : "SAVE - PICK A SLOT";

        // No cursor column any more -- the selected row is drawn in reverse
        // video -- so a row is the digit prefix plus its label, and the widest
        // slot name gets a say too, since a centred row that overran the box
        // would be clipped rather than merely ragged.
        int row_w = MENU_DIGIT_COLS + 10;
        int edit_w = MENU_DIGIT_COLS + maxchars + 1;
        if (edit_w > row_w) row_w = edit_w;
        for (int i = 0; i < SAVE_SLOTS; i++) {
            int n = 0;
            while (slotname[i][n]) n++;
            if (MENU_DIGIT_COLS + n > row_w) row_w = MENU_DIGIT_COLS + n;
        }
        int content_w;
        int rows;
        if (editing) {
            int kb_w = KB_COLS * 2;
            content_w = row_w;
            if (kb_w > content_w) content_w = kb_w;
            rows = SAVE_SLOTS + 1 + KB_ROWS;
        } else {
            content_w = row_w;
            rows = SAVE_SLOTS;
        }
        int x0, y0, w, h;
        menu_box_fit(btitle, content_w, rows, &x0, &y0, &w, &h);

        bool nums = !g_kbd_visible && !editing;

        menu_clear();
        menu_frame(x0, y0, w, h, btitle);
        int cy = y0 + 3;
        for (int i = 0; i < SAVE_SLOTS; i++) {
            if (editing && i == sel) {
                menu_rowf(x0, w, cy + i, 1, row_w, "%s%s_",
                          menu_num(0, i), k.input);
            } else {
                const char *label = slotname[i][0] ? slotname[i] : "(empty)";
                menu_rowf(x0, w, cy + i, i == sel, row_w, "%s%s",
                          menu_num(nums, i), label);
            }
        }
        if (editing) {
            // Centred as one block, not row by row, so the cursor highlight can
            // still be addressed by column.
            int kbx = x0 + 2 + ((w - 4) - KB_COLS * 2) / 2;
            for (int r = 0; r < KB_ROWS; r++) {
                char rowbuf[KB_COLS * 2 + 1];
                int p = 0;
                for (int c = 0; c < KB_COLS; c++) {
                    rowbuf[p++] = ' ';
                    rowbuf[p++] = KB_LAYOUT[r][c];
                }
                rowbuf[p] = '\0';
                text_print_dim(kbx, cy + SAVE_SLOTS + 1 + r, rowbuf);
                if (r == k.cursor_row) {
                    char sel[2] = { KB_LAYOUT[r][k.cursor_col], '\0' };
                    text_print(kbx + k.cursor_col * 2 + 1, cy + SAVE_SLOTS + 1 + r, sel);
                }
            }
        }
        menu_sync();
    }
}

/*----------------------
 | choose_dest
 | Description: The restore-only device+slot picker (title-menu Load and in-game
 |   Restore both come through here; saving uses the name editor instead). Runs
 |   choose_device, then a menu_select over the SAVE_SLOTS slots with each row
 |   labelled by saturn_bup_info's comment (or "(empty)"). The labels are held in
 |   a function-static buffer so the const char* array handed to menu_select stays
 |   valid for the length of the menu. A pick on an empty slot is refused with a
 |   message and the list re-opens, since restoring from one black-screens the
 |   interpreter. Cancelling either menu returns 0 without touching the out-params.
 | Author: suinevere
 | Dependencies: menu.h (menu_select), saturn_backup.h (SAVE_SLOTS/saturn_bup_info)
 | Globals: N/A
 | Params: title_dev -- device-menu title; title_slot -- slot-menu title;
 |   out_device -- receives the chosen device; out_slot -- receives the chosen slot
 | Returns: 1 with *out_device / *out_slot set, or 0 if cancelled
 ----------------------*/
int choose_dest(const char *title_dev, const char *title_slot,
                int *out_device, int *out_slot) {
    // Fade contract mirrors game_select: in the title-menu Load flow
    // (g_menu_page_fade > 0) this is entered black-held and every return leaves
    // the screen black for main() to reveal; the device -> slot step fades out
    // then in. In-game (Restore via a function key) the gate is 0 and all of
    // this is a no-op, so it opens instantly over gameplay as before.
    int device = choose_device(title_dev);   // fades in (title flow); shown at normal on return
    if (device < 0) { if (g_menu_page_fade) menu_fade_out(g_menu_page_fade); return 0; }
    if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);   // device list -> black before the slots

    static char labels[SAVE_SLOTS][40];
    const char *slot_items[SAVE_SLOTS];
    bool used[SAVE_SLOTS];
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char name[12];
        make_slot_name(name, i);
        char comment[12];
        used[i] = saturn_bup_info(device, name, comment);
        if (used[i]) snprintf(labels[i], sizeof(labels[i]), "%s", comment);
        else         snprintf(labels[i], sizeof(labels[i]), "(empty)");
        slot_items[i] = labels[i];
    }

    /* Restore-only picker: an empty slot has nothing to load and restoring from
       one drops the interpreter into a black screen, so a pick on one is refused
       and the list re-opens rather than returned. Only the first open fades in
       from black; the refusal re-opens at the brightness it left. */
    static int restore_sel = 0;   // held across visits; SAVE_SLOTS never changes
    g_menu_intro_fade = g_menu_page_fade;
    for (;;) {
        int slot = menu_select_at(title_slot, slot_items, SAVE_SLOTS, &restore_sel);
        if (slot < 0) { if (g_menu_page_fade) menu_fade_out(g_menu_page_fade); return 0; }
        if (used[slot]) {
            if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);   // slot list -> black
            *out_device = device;
            *out_slot = slot;
            return 1;
        }
        menu_message(title_slot, "That slot is empty.", hint("A/C = back", "Enter = back"));
        menu_wait();
    }
}

/*----------------------
 | GAME_SCAN_MAX
 | Description: How many story filenames any_save_present will take off /Z3,
 |   matching game_catalog.cxx's own catalogue cap -- a scan that stopped short
 |   would only ever miss a save, and missing one shows a warning that was not
 |   needed.
 | Author: suinevere
 ----------------------*/
#define GAME_SCAN_MAX 32

/*----------------------
 | same_name
 | Description: Compares two NUL-terminated names. Character by character rather
 |   than through strcmp, as game_catalog.cxx does, to keep <string.h> out of this
 |   file for one comparison.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- the names to compare
 | Returns: 1 when they match, 0 otherwise
 ----------------------*/
static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (*a == *b);
}

/*----------------------
 | device_has_save
 | Description: True when one device holds a save for any of `games` stories.
 |
 |   Listed once and matched in memory where the driver will list: 160 name
 |   lookups against a full cartridge is seconds of boot, and the whole point of
 |   this check is to be spent before the player sees anything. The per-name probe
 |   is kept only as the fallback saturn_bup_list's contract asks for, and the case
 |   that falls back -- a directory the listing found nothing in -- is also the case
 |   where a probe has nothing to scan, so the slow path is only ever taken when it
 |   is not slow.
 |
 |   Either way the question is asked by name, never by shape: a name is ours
 |   because it is a story on this disc plus a slot digit, so another Saturn game's
 |   file -- which can perfectly well be eight uppercase characters ending in a
 |   digit -- is not mistaken for one. MOJOOPTS falls out on its own: it is
 |   settings, it is named for no story, and nothing here asks for it.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: device -- backup device id; games -- story filenames from /Z3; count --
 |   how many
 | Returns: 1 if any of those stories has a save on this device, 0 otherwise
 ----------------------*/
static int device_has_save(int device, const char games[][16], int count) {
    char files[SATURN_BUP_LIST_MAX][12];
    int  listed = saturn_bup_list(device, files, SATURN_BUP_LIST_MAX);
    char name[12];

    for (int g = 0; g < count; g++) {
        for (int slot = 0; slot < SAVE_SLOTS; slot++) {
            slot_name_for(games[g], name, slot);
            if (listed > 0) {
                for (int f = 0; f < listed; f++)
                    if (same_name(files[f], name)) return 1;
            } else {
                char comment[12];
                if (saturn_bup_info(device, name, comment)) return 1;
            }
        }
    }
    return 0;
}

/*----------------------
 | any_save_present
 | Description: True when either backup device already holds a save for any story
 |   on the disc.
 |
 |   The scan reads /Z3 and leaves it as the current CD directory; main follows
 |   this with GFS_Reset and cd_capture_root, which puts that back. scan_z3_folder
 |   caches nothing by design -- re-establishing that directory record is what the
 |   call is for -- so this is a real directory read, and it is why nothing calls
 |   it until the space check has already decided it is about to complain. A boot
 |   with room to spare never touches the drive here at all.
 | Author: suinevere
 | Dependencies: game_catalog.h (scan_z3_folder), saturn_backup.h
 | Globals: g_z3_dir_valid, and the CD's current directory (both reset by main)
 | Params: N/A
 | Returns: 1 if any story has a save on any device, 0 otherwise
 ----------------------*/
static int any_save_present(void) {
    char games[GAME_SCAN_MAX][16];
    int  count = scan_z3_folder(games, GAME_SCAN_MAX);
    if (count <= 0) return 0;

    for (int device = SATURN_BUP_CONSOLE; device <= SATURN_BUP_CARTRIDGE; device++) {
        if (!saturn_bup_present(device)) continue;
        if (device_has_save(device, games, count)) return 1;
    }
    return 0;
}

/*----------------------
 | save_space_warn
 | Description: Boot-time check that backup memory can take a first save at all,
 |   and the modal shown when it cannot.
 |
 |   The cost is two records, not one: the game blob at SAVE_BLOB_MAX and the map
 |   companion at MAP_BLOB_MAX are separate files, each rounded up to whole blocks
 |   and each charged its own header block, so their block costs add rather than
 |   their byte counts. Both devices are asked, because either one alone is enough
 |   to save on and the player is only stuck when neither will do.
 |
 |   A device that is present but unformatted ends the check without a word: it is
 |   empty, it gets formatted at the first write, and the only way to learn its
 |   geometry before then is to format it on a probe -- which nothing here does.
 |
 |   When it does warn, it reports the device that came closest rather than the
 |   first one asked, so the numbers on screen are the best case the player has.
 |   Internal memory and a cartridge count in blocks of different sizes, so the two
 |   figures shown are always from the same device. A machine with no backup device
 |   at all is left alone: there is nothing to free and nothing to advise.
 |
 |   So is a player who already has a save for one of the disc's games, however
 |   short the free space now is: they have saved before, the slot they used can be
 |   written over without asking the device for another block, and a warning about
 |   a first save is not about them. Settings do not count -- MOJOOPTS is not a
 |   save and any_save_present never looks at it.
 |
 |   menu_fade_clear before the box because boot holds the screen black for the
 |   splash to take over, and main re-arms that hold on the way past. The
 |   Synchronize before the loop eats the button edge that arrived with the call,
 |   as menu_confirm does, so an in-flight press cannot answer the question.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.h, saturn_backup.h, saturn_glue.h,
 |   map_model.h, input.h, console_view.h, saturn_keyboard.h, sega_sys.h
 | Globals: g_pad, g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void save_space_warn(void) {
    uint32_t need = 0, have = 0;
    int measured = 0;

    for (int device = SATURN_BUP_CONSOLE; device <= SATURN_BUP_CARTRIDGE; device++) {
        uint32_t blob_need, blob_free, map_need, map_free;
        int state = saturn_bup_space(device, SAVE_BLOB_MAX, &blob_need, &blob_free);
        if (state == SATURN_BUP_UNFORMATTED) return;
        if (state != SATURN_BUP_MEASURED) continue;
        if (saturn_bup_space(device, MAP_BLOB_MAX, &map_need, &map_free)
                != SATURN_BUP_MEASURED) continue;

        uint32_t want = blob_need + map_need;
        if (blob_free >= want) return;
        if (!measured || (want - blob_free) < (need - have)) {
            need = want; have = blob_free;
        }
        measured = 1;
    }
    if (!measured) return;
    if (any_save_present()) return;

    char line1[MENU_ROW_TEXT_MAX + 1];
    char line2[MENU_ROW_TEXT_MAX + 1];
    snprintf(line1, sizeof(line1), "You need %u blocks to save,", (unsigned int) need);
    snprintf(line2, sizeof(line2), "only %u available.", (unsigned int) have);

    menu_fade_clear();

    MenuBacking backing;
    int x0, y0, w, h;
    menu_box_fit("BACKUP MEMORY", MENU_ROW_TEXT_MAX, 5, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) SYS_Exit(0);
        if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) return;
        if (ke.kind != SATURN_KEY_CHAR) {
            if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)) SYS_Exit(0);
            if (g_pad->WasPressed(Button::B)) return;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "BACKUP MEMORY");
        int cy = y0 + 3;
        menu_text(x0, w, cy, 0, line1);
        menu_text(x0, w, cy + 1, 0, line2);
        menu_text(x0, w, cy + 3, 0,
            hint("A / C = Restart to BIOS", "Enter = Restart to BIOS"));
        menu_text(x0, w, cy + 4, 0,
            hint("B = Continue without Saving", "Esc = Continue w/o Saving"));
        menu_sync();
    }
}
