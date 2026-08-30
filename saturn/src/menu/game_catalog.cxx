/*----------------------
 | game_catalog.cxx
 | Description: Finds the story files on the disc, caches what is known about
 |   each one, and presents them as a category-then-title menu. All of the drive
 |   traffic happens in preload_game_catalog so that the picker itself is pure
 |   RAM work -- the Saturn has one drive head, and any read here would silence
 |   the CD-DA menu track.
 |
 |   What each story is called comes from /Z3's GAME.INF, a manifest the injection
 |   step writes alongside the games, in one read. Only a file that manifest does
 |   not describe is opened for its header, which is what the whole catalogue used
 |   to cost: one seek per game, thirty of them, in the middle of the intro.
 | Author: suinevere
 | Dependencies: game_catalog.h, menu.h (menu_select/menu_message/menu_wait/
 |   MenuBacking), menu_layout.h (MENU_ROW_TEXT_MAX), game_titles.h (game_title/
 |   game_category), title.h (cd_enter_root), SRL/GFS
 ----------------------*/

#include <srl.hpp>

#include "game_catalog.h"
#include "menu.h"
#include "title.h"
extern "C" {
#include "menu_layout.h"
#include "game_titles.h"
}

/*----------------------
 | g_z3_dirnames / g_z3_tbl / g_z3_dir_valid
 | Description: The directory record for the disc's "Z3" folder, captured by
 |   scan_z3_folder. Deliberately not static: title.cxx re-applies this table
 |   after a bitmap load moves the current CD directory elsewhere.
 | Author: suinevere
 ----------------------*/
GfsDirName g_z3_dirnames[SRL_MAX_CD_FILES];
GfsDirTbl  g_z3_tbl;
bool       g_z3_dir_valid = false;

/*----------------------
 | has_z3_ext
 | Description: Checks the last three characters for '.', 'z' or 'Z', and '3'.
 |   Hand-rolled rather than strcmp'd so this file needs no <string.h>.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- NUL-terminated filename
 | Returns: nonzero if `s` ends in ".z3" or ".Z3"
 ----------------------*/
static int has_z3_ext(const char *s) {
    int len = 0; while (s[len]) len++;
    if (len < 3) return 0;
    const char *e = s + len - 3;
    return e[0] == '.' && (e[1] == 'z' || e[1] == 'Z') && e[2] == '3';
}

/*----------------------
 | scan_z3_folder
 | Description: Returns to the root directory first, because GFS_SetDir below is
 |   persistent: after a soft reset we re-enter here still inside Z3, where "Z3"
 |   would resolve relative to Z3 and fail. Then loads the Z3 directory record,
 |   makes it current so later SRL::Cd::File() opens resolve there, and filters
 |   its entries down to the .Z3 names. Saturn/ISO9660 names are uppercase 8.3
 |   (GFS_FNAME_LEN = 12) and may carry a ";1" version suffix, which is stripped;
 |   the "." and ".." records fall out of the extension filter on their own.
 | Author: suinevere
 | Dependencies: title.h (cd_enter_root), SRL/GFS
 | Globals: g_z3_dirnames, g_z3_tbl, g_z3_dir_valid
 | Params: out -- receives up to `max` NUL-terminated names of at most 15 chars;
 |   max -- capacity of `out`
 | Returns: the number of names written, or -1 if there is no Z3 folder
 ----------------------*/
int scan_z3_folder(char out[][16], int max) {
    cd_enter_root();

    int32_t fid = GFS_NameToId((int8_t *) "Z3");
    if (fid < 0) return -1;

    GFS_DIRTBL_TYPE(&g_z3_tbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&g_z3_tbl) = g_z3_dirnames;
    GFS_DIRTBL_NDIR(&g_z3_tbl)    = SRL_MAX_CD_FILES;

    int32_t count = GFS_LoadDir(fid, &g_z3_tbl);
    if (count < 0) return -1;
    GFS_SetDir(&g_z3_tbl);
    g_z3_dir_valid = true;

    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        char nm[16];
        int j = 0;
        for (; j < GFS_FNAME_LEN && j < 15; j++) {
            char c = (char) g_z3_dirnames[i].fname[j];
            if (c == '\0' || c == ';') break;
            nm[j] = c;
        }
        nm[j] = '\0';
        if (has_z3_ext(nm)) {
            int k = 0;
            for (; nm[k] && k < 15; k++) out[n][k] = nm[k];
            out[n][k] = '\0';
            n++;
        }
    }
    return n;
}

/*----------------------
 | read_game_info
 | Description: Pulls the Z-machine header (version, release at 0x02, serial at
 |   0x12) out of sector 0 and looks the pair up in the title table. One
 |   sector-addressed LoadBytes rather than a Size stat plus Open/Read/Close:
 |   fewer GFS calls per file across ~25 games, and it sidesteps the flaky
 |   first-access GFS_GetFileSize that the old size check had to retry around.
 |   The retry loop remains for the same flakiness on the read itself.
 | Author: suinevere
 | Dependencies: game_titles.h, SRL
 | Globals: N/A
 | Params: filename -- story file to read; cat -- receives the game's category,
 |   GAME_CAT_OTHER when unknown
 | Returns: the game's display title, or NULL when unknown, unreadable, or not a
 |   v3 story
 ----------------------*/
static const char* read_game_info(const char* filename, int* cat) {
    static uint8_t hdr[64];
    *cat = GAME_CAT_OTHER;
    for (int attempt = 0; attempt < 8; attempt++) {
        SRL::Cd::File f(filename);
        int32_t got = f.LoadBytes(0, (int32_t) sizeof(hdr), hdr);
        if (got >= 0x1a) {
            if (hdr[0] != 3) return nullptr;
            unsigned short rel = (unsigned short)((hdr[2] << 8) | hdr[3]);
            const char* serial = (const char*)(hdr + 0x12);
            *cat = game_category(rel, serial);
            return game_title(rel, serial);
        }
        for (int i = 0; i < 4; i++) SRL::Core::Synchronize();
    }
    return nullptr;
}

/*----------------------
 | CAT_NAMES
 | Description: Menu titles for the GAME_CAT_* ids, in id order.
 | Author: suinevere
 ----------------------*/
static const char *const CAT_NAMES[GAME_CAT_COUNT] = {
    "The Zork Universe", "The Enchanter Series", "The Planetfall Series",
    "The Mystery Series", "Tales of Adventure & Fantasy", "Sci-Fi & Horror",
    "Comedy", "Other",
};

/*----------------------
 | label_cmp
 | Description: Walks both strings to the first difference and subtracts the
 |   bytes there, giving strcmp's sign convention without a <string.h>
 |   dependency.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b -- NUL-terminated labels to compare
 | Returns: <0, 0 or >0 as `a` sorts before, with, or after `b`
 ----------------------*/
static int label_cmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/*----------------------
 | label_year
 | Description: Reads the four digits of a trailing "(YYYY)" (e.g. "Zork I
 |   (1980)"). Undated labels answer 9999 so the menu's sort places them after
 |   every dated game rather than at the front.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the display label to inspect
 | Returns: the release year, or 9999 when the label carries none
 ----------------------*/
static int label_year(const char* s) {
    int n = 0; while (s[n]) n++;
    if (n >= 6 && s[n-6] == '(' && s[n-1] == ')') {
        int y = 0;
        for (int k = n-5; k <= n-2; k++) {
            if (s[k] < '0' || s[k] > '9') return 9999;
            y = y * 10 + (s[k] - '0');
        }
        return y;
    }
    return 9999;
}

/*----------------------
 | MAX_GAMES / names / labels / cats / g_catalog_count / g_catalog_ready
 | Description: The catalogue cache, filled once by preload_game_catalog and read
 |   directly by game_select so the picker does no CD I/O. `names` is static
 |   storage because game_select hands one of its rows back to the caller.
 |   MAX_GAMES is headroom for the full Infocom Z3 catalogue.
 | Author: suinevere
 ----------------------*/
static const int MAX_GAMES = 32;
static char names[MAX_GAMES][16];
static char labels[MAX_GAMES][40];
static int  cats[MAX_GAMES];
static int  g_catalog_count = 0;
static bool g_catalog_ready = false;

/*----------------------
 | GAME_INFO_HDR / GAME_INFO_REC / GAME_INFO_NAME / GAME_INFO_LBL / g_info_buf /
 | g_info_count
 | Description: The disc's story manifest, written into /Z3 by
 |   tools/gametitles/gen_game_info.py at the moment the games are injected, and
 |   read here in one go. A 16-byte header -- magic "ZGI1", record count, record
 |   size, reserved -- then one record per story: 16 bytes of filename, 32 of
 |   display label, the GAME_CAT_* id, and three reserved. At MAX_GAMES the whole
 |   file is 1680 bytes, so reading it is a single sector.
 |
 |   g_info_count is 0 whenever there is no usable manifest, which is the signal
 |   every lookup falls back on.
 | Author: suinevere
 ----------------------*/
static const int GAME_INFO_HDR  = 16;
static const int GAME_INFO_REC  = 52;
static const int GAME_INFO_NAME = 16;
static const int GAME_INFO_LBL  = 32;
static uint8_t   g_info_buf[GAME_INFO_HDR + MAX_GAMES * GAME_INFO_REC];
static int       g_info_count = 0;

/*----------------------
 | load_game_info
 | Description: Reads /Z3's GAME.INF into g_info_buf and validates its header,
 |   leaving g_info_count at the number of records that can be trusted.
 |
 |   A disc without the file costs nothing: SRL::Cd::File resolves the name
 |   against the directory table scan_z3_folder has already put in RAM, so the
 |   miss never reaches the drive. A short read is clamped to the records that
 |   did arrive rather than rejected outright, so a truncated manifest still
 |   spares the games it does describe. The retry loop is read_game_info's, for
 |   the same flaky first access.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_info_buf, g_info_count
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void load_game_info(void) {
    g_info_count = 0;

    SRL::Cd::File f("GAME.INF");
    if (!f.Exists()) return;

    int32_t want = f.Size.Bytes;
    if (want <= 0 || want > (int32_t) sizeof(g_info_buf)) want = (int32_t) sizeof(g_info_buf);

    int32_t got = -1;
    for (int attempt = 0; attempt < 8; attempt++) {
        got = f.LoadBytes(0, want, g_info_buf);
        if (got >= GAME_INFO_HDR) break;
        for (int i = 0; i < 4; i++) SRL::Core::Synchronize();
    }
    if (got < GAME_INFO_HDR) return;

    if (g_info_buf[0] != 'Z' || g_info_buf[1] != 'G' ||
        g_info_buf[2] != 'I' || g_info_buf[3] != '1') return;
    if ((((int) g_info_buf[6] << 8) | (int) g_info_buf[7]) != GAME_INFO_REC) return;

    int count = (((int) g_info_buf[4] << 8) | (int) g_info_buf[5]);
    int avail = (int) (got - GAME_INFO_HDR) / GAME_INFO_REC;
    if (count > avail)     count = avail;
    if (count > MAX_GAMES) count = MAX_GAMES;
    if (count < 0)         count = 0;
    g_info_count = count;
}

/*----------------------
 | info_lookup
 | Description: Finds `filename` among the loaded records and hands back the label
 |   and category the picker would otherwise have opened the story to learn. A
 |   record carrying an empty label counts as a miss, so a malformed row falls
 |   back to the header read instead of putting a blank row on the menu.
 | Author: suinevere
 | Dependencies: game_titles.h (GAME_CAT_*), menu_layout.h (MENU_ROW_TEXT_MAX)
 | Globals: g_info_buf, g_info_count
 | Params: filename -- a name from scan_z3_folder; label -- receives up to
 |   MENU_ROW_TEXT_MAX characters and a NUL; cat -- receives the GAME_CAT_* id
 | Returns: true when the manifest described this file
 ----------------------*/
static bool info_lookup(const char* filename, char* label, int* cat) {
    for (int i = 0; i < g_info_count; i++) {
        const uint8_t* rec = g_info_buf + GAME_INFO_HDR + i * GAME_INFO_REC;
        int j = 0;
        while (j < GAME_INFO_NAME && rec[j] && (char) rec[j] == filename[j]) j++;
        if (j >= GAME_INFO_NAME || rec[j] != '\0' || filename[j] != '\0') continue;

        const uint8_t* text = rec + GAME_INFO_NAME;
        if (text[0] == '\0') return false;
        int k = 0;
        for (; k < GAME_INFO_LBL && k < MENU_ROW_TEXT_MAX && text[k]; k++) label[k] = (char) text[k];
        label[k] = '\0';

        int c = (int) rec[GAME_INFO_NAME + GAME_INFO_LBL];
        *cat = (c < GAME_CAT_COUNT) ? c : GAME_CAT_OTHER;
        return true;
    }
    return false;
}

/*----------------------
 | preload_game_catalog
 | Description: Scans the Z3 folder and reads every game's header once, guarded
 |   by g_catalog_ready so repeat calls are free. Labels are capped at
 |   MENU_ROW_TEXT_MAX -- the width a menu row can actually draw, not the buffer
 |   size: a row is "> N) " (5 columns) plus the title, and a full-width box
 |   leaves 37 columns from the content origin to the border, so anything past 32
 |   characters would overwrite that border. Every real title fits; the cap only
 |   guards the filename fallback and any future long entry. Once the deferred
 |   marquee lands, long titles can scroll instead of being clipped.
 |
 |   Titles come from GAME.INF where it covers them and from the story's own header
 |   where it does not, so a disc built without the manifest -- or one a player has
 |   added a game to by hand -- reads exactly what it always did and nothing more.
 | Author: suinevere
 | Dependencies: game_titles.h, menu_layout.h, SRL
 | Globals: names, labels, cats, g_catalog_count, g_catalog_ready
 | Params: N/A
 | Returns: N/A
 ----------------------*/
/*----------------------
 | game_catalog_invalidate
 | Description: Drops the cached catalogue so the next preload_game_catalog scans
 |   the disc again. For the soft reset, and it is not a saving -- it is what makes
 |   a return to title identical to a cold boot.
 |
 |   The scan is the only thing on this path that issues GFS_LoadDir on /Z3 and
 |   makes it current. Skipping it left the session running on a directory record
 |   captured before the previous game, with nothing since having re-established it
 |   from a known-good root; the bare SRL::Cd::File("XXX.Z3") open that main() does
 |   next resolves against whatever GFS is pointing at, so a stale record is a load
 |   that misses all 300 retries and halts. Re-scanning costs one directory read per
 |   restart and restores the cold-boot ordering exactly, rather than trying to make
 |   a second ordering work -- the same call the online trie's release makes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_catalog_ready, g_catalog_count
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void game_catalog_invalidate(void) {
    g_catalog_ready = false;
    g_catalog_count = 0;
}

void preload_game_catalog(void) {
    if (g_catalog_ready) return;
    g_catalog_count = scan_z3_folder(names, MAX_GAMES);
    if (g_catalog_count > 0) {
        load_game_info();
        for (int i = 0; i < g_catalog_count; i++) {
            if (info_lookup(names[i], labels[i], &cats[i])) continue;
            const char* title = read_game_info(names[i], &cats[i]);
            int j = 0; const char* src = title ? title : names[i];
            for (; src[j] && j < MENU_ROW_TEXT_MAX; j++) labels[i][j] = src[j];
            labels[i][j] = '\0';
        }
    }
    g_catalog_ready = true;
}

/*----------------------
 | game_catalog_title_for
 | Description: See game_catalog.h. A manual character-by-character compare
 |   rather than strcmp, to avoid adding a <string.h> dependency to this
 |   file for one lookup.
 | Author: suinevere
 | Dependencies: none
 | Globals: names, labels, g_catalog_count
 ----------------------*/
const char* game_catalog_title_for(const char *filename) {
    for (int i = 0; i < g_catalog_count; i++) {
        int j = 0;
        while (filename[j] && names[i][j] == filename[j]) j++;
        if (filename[j] == '\0' && names[i][j] == '\0') return labels[i];
    }
    return filename;
}

/*----------------------
 | game_select
 | Description: Calls preload_game_catalog first (idempotent, so on the normal
 |   boot path the reads have already happened at the title) and then loops
 |   between two menu_select pages. The category page lists only categories that
 |   actually have a game, and is skipped when there is exactly one -- in which
 |   case backing out of the game page cancels outright, since there is no
 |   category page to return to. The game page is insertion-sorted by release
 |   year then title, which also breaks ties among undated games.
 | Author: suinevere
 | Dependencies: menu.h, SRL
 | Globals: names, labels, cats, g_catalog_count
 | Params: N/A
 | Returns: the chosen story's filename, or NULL on cancel / empty catalogue
 ----------------------*/
const char* game_select(void) {
    const char *items[MAX_GAMES];

    preload_game_catalog();
    int count = g_catalog_count;

    // Fade contract (when g_menu_page_fade > 0, i.e. reached from the title
    // menu): entered at normal brightness with the mode-select menu showing.
    // A cancelled return leaves the screen faded to black for main() to fade the
    // mode-select back in; a return WITH a game leaves it lit, because main()
    // fades it out itself with the story read running under the ramp. Each menu_select fades
    // in from that black via the g_menu_intro_fade one-shot; the transitions
    // between the category and game lists fade out then in, one continuous
    // dark beat. In-game the gate is 0 and every fade below is a no-op, so this
    // reads exactly as it did before.
    if (count <= 0) {
        MenuBacking backing;
        menu_message("NO GAMES", (count < 0)
            ? "No Z3 folder found on the CD."
            : "No .Z3 games found in the Z3 folder.",
            "(press any key/button to go back)");
        menu_wait();
        if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);
        return nullptr;
    }

    if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);   // mode-select -> black

    // The category row and one game row PER category, so stepping back out of a
    // game list lands on the category it came from and re-entering that
    // category lands on the game -- one shared game row would hand the cursor
    // from one category's list to another's, where its index means nothing.
    static int cat_sel = 0;
    static int game_sel[GAME_CAT_COUNT] = { 0 };

    for (;;) {
        int catmap[GAME_CAT_COUNT], ncat = 0;
        for (int c = 0; c < GAME_CAT_COUNT; c++) {
            int any = 0;
            for (int i = 0; i < count && !any; i++) if (cats[i] == c) any = 1;
            if (any) { items[ncat] = CAT_NAMES[c]; catmap[ncat] = c; ncat++; }
        }
        int cs;
        if (ncat == 1) {
            cs = 0;   // only one category: skip the picker, screen stays black-held
        } else {
            g_menu_intro_fade = g_menu_page_fade;   // category list fades in from black
            cs = menu_select_at("Choose a category:", items, ncat, &cat_sel);
            if (cs < 0) { if (g_menu_page_fade) menu_fade_out(g_menu_page_fade); return nullptr; }
            if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);   // -> black before the game list
        }

        int gmap[MAX_GAMES], ng = 0;
        for (int i = 0; i < count; i++) if (cats[i] == catmap[cs]) gmap[ng++] = i;
        for (int a = 1; a < ng; a++) {
            int key = gmap[a], ya = label_year(labels[key]);
            int b = a - 1;
            while (b >= 0) {
                int yb = label_year(labels[gmap[b]]);
                if (yb < ya || (yb == ya && label_cmp(labels[gmap[b]], labels[key]) <= 0)) break;
                gmap[b+1] = gmap[b]; b--;
            }
            gmap[b+1] = key;
        }
        for (int i = 0; i < ng; i++) items[i] = labels[gmap[i]];
        g_menu_intro_fade = g_menu_page_fade;   // game list fades in from black
        int gs = menu_select_at(CAT_NAMES[catmap[cs]], items, ng, &game_sel[catmap[cs]]);
        if (gs < 0) {
            if (g_menu_page_fade) menu_fade_out(g_menu_page_fade);   // game list -> black
            if (ncat == 1) return nullptr;   // nothing above it: back to the mode menu
            else continue;                    // back up to the category list (fades in)
        }
        // Deliberately no fade on the way out with a game in hand: main() runs its
        // own, longer one and reads the story underneath it, which it cannot do if
        // the screen is already black before it is told what to load. Every other
        // exit from this function still fades, because nothing follows them.
        return names[gmap[gs]];
    }
}
