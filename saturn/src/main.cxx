/*----------------------
 | main.cxx
 | Description: The Saturn client entry point and boot orchestrator. Initializes
 |   the console and peripherals, arms the soft-reset return-to-title target,
 |   front-loads what CD reads it can into the title screen's silent window (game
 |   catalogue and the title picture), starts the menu
 |   music, runs the top-level mode menu (Play Local / Play Online / Load Save
 |   Game / Options / Credits), loads the chosen story, wires the music engine to the game,
 |   and hands control to the Z-Machine. Every subsystem lives in its own module;
 |   this file only sequences them. The interpreter hooks it depends on
 |   (saturn_readline etc.) live in saturn_glue.cxx; the soft reset lives in
 |   soft_reset.cxx.
 | Author: suinevere
 | Dependencies: app_state.h, console.h, console_view.h, display.h, options.h,
 |   menu.h, menu_pages.h, save_ui.h, title.h, game_catalog.h, online.h,
 |   soft_reset.h, saturn_glue.h, saturn_backup.h, sound.h, music.h, input.h,
 |   SRL/GFS/SGL.
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"
#include <setjmp.h>

extern "C" {
#include "console.h"
#include "display.h"
#include "saturn_backup.h"
#include "saturn_glue.h"
#include "sound.h"
#include "music.h"
}
#include "app_state.h"
#include "input.h"
#include "console_view.h"
#include "options.h"
#include "soft_reset.h"
#include "menu.h"
#include "menu_pages.h"
#include "save_ui.h"
#include "title.h"
#include "splash.h"
#include "game_catalog.h"
#include "loading_screen.h"
#include "online.h"

using namespace SRL::Types;

/*----------------------
 | TITLE_FADE_FRAMES
 | Description: Title-screen fade length, ~1.5s at 60fps, matching the boot
 |   splash's SPLASH_FADE_FRAMES.
 | Author: suinevere
 ----------------------*/
#define TITLE_FADE_FRAMES 90

/*----------------------
 | MUSIC_FADE_FRAMES
 | Description: Half the length of a room-mood transition, in frames: the
 |   wallpaper and the music ramp down over this many, swap at the bottom, and
 |   ramp back up over as many again. 20 is a third of a second each way, which
 |   lands a mood change a little over two seconds after the player stops moving
 |   once the engine's 90-frame settle is counted. This is the number to cut if
 |   that reads sluggish -- not the settle, which is what stops fast movement
 |   through a corridor from thrashing the music.
 | Author: suinevere
 ----------------------*/
#define MUSIC_FADE_FRAMES 20

/*----------------------
 | QUICK_FADE_FRAMES
 | Description: Mode-select to Options transition length, ~0.25s, matching
 |   menu_pages.cxx's fade of the same name.
 | Author: suinevere
 ----------------------*/
#define QUICK_FADE_FRAMES 15

/*----------------------
 | STORY_READ_CHUNK
 | Description: How much of the story file is pulled per Cd::File::Read while the
 |   loading screen is held up. Eight 2048-byte sectors: enough that the per-call
 |   overhead stays irrelevant next to the transfer, small enough that the gap
 |   between two reads is around a tenth of a second at single speed, which is the
 |   resolution the screen's audio cue needs to be serviced at. Purely a pacing
 |   figure -- the bytes read, their order and the total are identical to the one
 |   whole-file Read this replaced.
 | Author: suinevere
 ----------------------*/
#define STORY_READ_CHUNK (2048 * 8)

/*----------------------
 | on_text_category
 | Description: The background art's half of a text-category change. Moves the
 |   Dynamic palette's picture to the new mood's art and repaints. A category
 |   with no art of its own -- TC_DANGER, TC_TRIUMPH -- leaves
 |   display_set_dynamic_category's stored slot alone, so this re-requests the
 |   picture already showing and title_bg_show short-circuits: the wallpaper holds
 |   without a special case here.
 |
 |   Goes through display_apply rather than title_bg_show directly, so a picture
 |   that fails to load takes the same colour-preset fallback every other display
 |   change does, instead of this path inventing its own.
 |
 |   Wipes the console rows in-game because this runs at the BOTTOM of the fade,
 |   with the screen black: without it the ramp back up would reveal the new
 |   picture underneath the PREVIOUS turn's text, which is still on the text layer
 |   because run_room_transition deliberately does not render during the fade. The
 |   wipe costs nothing, since render_console repaints from the scrollback the
 |   moment the transition ends. Menus are excluded -- the console is not the
 |   visible view there, and clearing would take the menu's own rows with it.
 |
 |   May read the disc. Only a handful of the thirty-seven pictures are held in Low
 |   Work RAM at once (TGA_CACHE_SLOTS in title.cxx), so a mood the player has not
 |   been in lately costs one read, and a read stops CD-DA. That is survivable only
 |   because of WHERE this is called from: the engine fires it at the bottom of the
 |   transition fade, with the screen black and the outgoing track about to be
 |   replaced by play_dyn anyway (see commit_pending in sound/music.c). Calling it
 |   from anywhere else -- mid-turn, or on a frame where the picture is visible --
 |   would put an audible gap in the middle of whatever is playing.
 | Author: suinevere
 | Dependencies: display.h, options.h
 | Globals: g_display
 | Params: cat -- the TC_* category now sounding
 | Returns: N/A
 ----------------------*/
static void on_text_category(int cat) {
    display_set_dynamic_category(cat);
    if (g_in_game) {
        for (int r = 0; r < console_height(); r++) text_clear_line(r);
    }
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    int slot = display_dynamic_slot();
    if (slot == DISP_IMAGE_NONE) return;      // disc carries no art
    g_display.image = slot;
    display_apply();
}

/*----------------------
 | on_text_rotate
 | Description: The art's half of a same-category rotation. The mood has not
 |   changed -- the player has simply walked MUSIC_ROTATE_ROOMS rooms of it -- so
 |   this moves to a different picture within that category rather than resolving
 |   the category afresh, which would hand back the one already showing.
 |
 |   A category with fewer than two pictures holds what it has, so the track
 |   rotates underneath an unchanged wallpaper. That is the intended degradation,
 |   not a gap: with one picture there is nothing truthful to change to.
 | Author: suinevere
 | Dependencies: display.h, options.h
 | Globals: g_display
 | Params: cat -- the TC_* category being rotated within
 | Returns: N/A
 ----------------------*/
static void on_text_rotate(int cat) {
    display_rotate_dynamic_category(cat);
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    int slot = display_dynamic_slot();
    if (slot == DISP_IMAGE_NONE || slot == g_display.image) return;
    g_display.image = slot;
    display_apply();
}

/*----------------------
 | on_music_fade
 | Description: Drives one step of a transition ramp: the wallpaper's brightness
 |   and the CD-DA volume together, off the engine's single counter, so the
 |   picture and the track cannot drift apart.
 |
 |   The volume floor is 1, never 0, and that is load-bearing rather than
 |   cosmetic: music_set_volume(0) calls StopPause(), which halts the drive
 |   outright, and unlike music_set_level it has no resurrect path -- the ramp
 |   back up would raise a volume on a stopped disc and the music would simply be
 |   gone. Level 1 is quiet enough, and the swap at the bottom re-issues the track
 |   anyway.
 |
 |   A player who has set Music to 0 keeps silence: a fade never raises what they
 |   turned off.
 | Author: suinevere
 | Dependencies: title.h, music.h, app_state.h
 | Globals: g_music_level
 | Params: level -- 0 (black/quiet) to 255 (normal)
 | Returns: N/A
 ----------------------*/
/*----------------------
 | music_fade_volume
 | Description: Maps a 0..255 fade level onto the CD-DA volume range, flooring at
 |   1 because music_set_volume(0) stops the drive with no way back.
 | Author: suinevere
 | Dependencies: music.h, app_state.h
 | Globals: g_music_level
 | Params: level -- 0 (quiet) to 255 (normal)
 | Returns: N/A
 ----------------------*/
static void music_fade_volume(int level) {
    if (g_music_level <= 0) return;
    int v = 1 + ((g_music_level - 1) * level) / 255;
    if (v < 1)             v = 1;
    if (v > g_music_level) v = g_music_level;
    music_set_volume(v);
}

static void on_music_fade(int level) {
    title_bg_dyn_fade(level);
    music_fade_volume(level);
}

/*----------------------
 | on_title_fade
 | Description: The title screen's version: the CD-DA volume register rises on the
 |   picture's own ramp. Nothing is playing on it here any more -- the splash jingle
 |   carries the title screen -- so what this actually does is walk the register off
 |   the floor of 1 and back to the player's level, in time for music_start_menu at
 |   the end of title_and_seed to issue its first track at full. No wallpaper call --
 |   the picture here is being lit by title_bg_fade_in_ex itself, on colour offset
 |   channel A, rather than by the in-game channel-B dimmer.
 | Author: suinevere
 | Dependencies: music.h, app_state.h
 | Globals: g_music_level
 | Params: level -- 0 (black/quiet) to 255 (normal)
 | Returns: N/A
 ----------------------*/
static void on_title_fade(int level) { music_fade_volume(level); }

/*----------------------
 | game_intro_reveal
 | Description: Brings the game's opening frame up out of the black the loading
 |   screen left behind -- picture, text and CD-DA together over TITLE_FADE_FRAMES.
 |   Handed to the prompt through g_intro_reveal rather than run here, because here
 |   is too early: mojo_run has not printed a word yet, so a reveal at this point
 |   shows the title screen's wallpaper over an empty console until the opening
 |   room replaces it. The prompt calls it at the one moment the room is composed
 |   and not yet on screen.
 |
 |   music_fade_volume alone as the step, not on_music_fade: NBG0 is already being
 |   lit by this ramp on colour offset A, and on_music_fade would dim it a second
 |   time through the in-game channel-B dimmer.
 | Author: suinevere
 | Dependencies: menu.h, music.h, app_state.h
 | Globals: g_music_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void game_intro_reveal(void) { menu_fade_in_ex(TITLE_FADE_FRAMES, music_fade_volume); }

/*----------------------
 | boot_entropy
 | Description: A number that differs from one run to the next, read off the
 |   Saturn's real-time clock. Used to vary the title screen's house picture.
 |
 |   The RTC rather than the frame counter, because at this point in the boot there
 |   is no frame counter worth reading -- title_and_seed's is generated by the
 |   player's own reaction time and does not exist until AFTER the title has been
 |   composed, which is exactly what needs the number. Deliberately does not issue
 |   its own slGetStatus: SRL::Core::Initialize and the per-frame input path already
 |   refresh Smpc_Status, and extra SMPC traffic is how the peripheral table gets
 |   disturbed.
 |
 |   Seconds-of-the-day, so it changes every second and wraps daily. A soft reset
 |   back to the title re-reads it and gets a different answer, which is the point.
 | Author: suinevere
 | Dependencies: SRL (DateTime)
 | Globals: N/A
 | Params: N/A
 | Returns: an arbitrary run-to-run-varying value
 ----------------------*/
static unsigned int boot_entropy(void) {
    SRL::Types::DateTime now = SRL::Types::DateTime::Now();
    return (unsigned int) now.Second()
         + 60u  * (unsigned int) now.Minute()
         + 3600u * (unsigned int) now.Hour();
}

/*----------------------
 | main
 | Description: Boots the client and never returns to its caller (it ends by
 |   soft-resetting to the title). Order matters at several points: cd_capture_root
 |   precedes any GFS_SetDir. setjmp arms g_title_jmp so the soft reset (chord or
 |   typed reboot/quit) longjmps back here; because that jump skips destructors,
 |   the re-entry path hand-clears g_menu_backing_depth (else NBG3 stays opaque and
 |   hides the title image) and disables the NBG0 image window. The story image is
 |   owned by the Z-machine (initStory frees the prior one), so it is never freed
 |   here. music_reset before the menu track clears stale engine
 |   state so a menu-frame music_tick cannot leak a game track. The menu track is
 |   started through the engine (music_start_menu) rather than handed straight to
 |   the CD-DA backend, so it obeys the same play-count-and-cycle rule the in-game
 |   music does instead of repeating one track for as long as the menu is open;
 |   that is also why the backend callbacks are installed here rather than at game
 |   start. Every CD read
 |   finishes before CD-DA starts, because the single drive head cannot play
 |   CD-DA while reading data. The screen is held black from Core::Initialize
 |   onward and is only ever lifted by an explicit fade-in, so every CD read on
 |   the way to the first picture -- the splash's own reads, HOUSE1.TGA -- happens
 |   behind black rather than over a bare console. On first
 |   cold boot, splash_show() covers the game-catalogue scan and the first few
 |   background pictures with a fading logo instead of a silent title picture, and
 |   title_and_seed() finishes whatever is left with its prompt already on screen.
 |   After this the menu never touches the CD, so the track plays uninterrupted.
 |
 |   A soft-reset re-entry runs exactly the same sequence, deliberately: there is no
 |   cold-boot/return branch anywhere below. The catalogue is a cached static the
 |   longjmp did not touch, so a return pays for the logo's fades and refilling the
 |   emptied art cache and nothing else. The Z3 load retries the flaky
 |   first-access GFS size stat before allocating and reading. Both it and the
 |   sound-blorb read after it run underneath the loading screen, which is
 |   raised before them and not taken down until the game is built -- the
 |   screen's cue is ticked through the retry loop so it lasts as long as the
 |   read does. Anything on this path that steps out of /Z3 has to step back
 |   before it returns: both opens are by bare filename and resolve against
 |   whatever directory is current (cd_restore_z3). Enabling sound
 |   keys off a sibling <base>.BLB, and the music engine is wired to the CD-DA
 |   backend and seeded from the story's release/serial; it is started after the
 |   loading screen has gone, so the CD-DA head and the loading cue never overlap. When mojo_run returns
 |   (only a death/victory-screen QUIT reaches here, since the prompt intercepts
 |   typed quit), the final screen is held until acknowledged, then it soft-resets
 |   to the title -- the same place every other exit lands.
 | Author: suinevere
 | Dependencies: title.h, splash.h, game_catalog.h, online.h, options.h, menu.h,
 |   menu_pages.h, save_ui.h, soft_reset.h, saturn_glue.h, saturn_backup.h,
 |   display.h, console.h, console_view.h, sound.h, music.h, input.h, SRL/GFS/SGL
 | Globals: g_display, g_pad, g_title_jmp, g_title_jmp_armed, g_z3_dir_valid,
 |   g_menu_backing_depth, g_music_level, g_pcm_level, g_mix_mode, g_sel_track,
 |   g_story_filename, g_restore_device, g_restore_slot, g_autocmd,
 |   g_output_start, g_in_game
 | Params: N/A
 | Returns: 0 nominally, but it never actually returns
 ----------------------*/
int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    text_map_init();       // before anything prints: draws land in the shadow and
                           // reach VRAM on the vblank the next Synchronize waits for
    title_bg_fade_arm();   // hold black over the pre-splash CD work below; the
                           // splash re-arms and owns the screen from there
    saturn_bup_init();
    cd_capture_root();
    display_defaults(&g_display);
    options_load();

    static MultiPad pads;
    g_pad = &pads;

    setjmp(g_title_jmp);
    g_title_jmp_armed = true;
    GFS_Reset();
    cd_capture_root();
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    g_in_game = false;
    slScrWindowModeNbg0(0);
    title_bg_fade_arm();     // a reset chord can fire mid-ramp, so overwrite any held
                             // offset -- with black, not with clear: nothing between
                             // here and the first fade-in below is meant to be seen
    console_init();

    music_reset();

    // A finished session leaves the art cache full, and the jingle splash_show is
    // about to load wants ~453 KB of the same zone. Unconditional: on a cold boot
    // the cache is empty and this costs nothing, and the sequence below is meant
    // not to know which boot it is.
    title_bg_cache_release();

    for (int r = 0; r <= 28; r++) text_clear_line(r);

    // A different house behind the title each time it is reached -- cold boot or
    // soft-reset return, since boot_entropy re-reads the clock either way. The
    // Dynamic slot is moved with it so the menu underneath resolves TC_HOUSE to the
    // same picture: without that, the wallpaper would jump to a different house the
    // moment the title faded out and display_apply ran.
    //
    // Ahead of the splash rather than after it, because the splash is what warms
    // the art cache (display_preload_categories) and TC_HOUSE is the first picture
    // it takes. Shuffling afterwards would preload one house and then show another.
    display_shuffle_category(TC_HOUSE, boot_entropy());
    display_set_dynamic_category(TC_HOUSE);
    if (g_display.palette == DISP_PAL_DYNAMIC) {
        int slot = display_dynamic_slot();
        if (slot != DISP_IMAGE_NONE) g_display.image = slot;
    }

    splash_show();                // does the boot's CD reads, under its own logo

    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));
    title_bg_fade_arm();          // black out first, so the title is composed unseen

    title_bg_show(display_category_image(TC_HOUSE));

    // The backend goes in here rather than at game start, because the menu track
    // below is the engine's now too -- that is what makes it obey the cycle rule
    // instead of repeating one track forever.
    music_set_backend(music_cdda_play_mode);
    music_set_isplaying(music_cdda_is_playing);
    music_set_isshort(music_cdda_is_short);
    music_set_pausefns(music_cdda_pause, music_cdda_resume);
    music_set_duckfns(music_cdda_duck, music_cdda_unduck);

    music_set_level(g_music_level);
    music_set_mix(g_mix_mode, g_sel_track);
    // Down to the floor BEFORE the track is issued, or its first frames play at
    // whatever the player's saved level is and the ramp starts from full. Floor 1
    // and never 0: music_set_volume(0) calls StopPause() with no way back up (see
    // on_music_fade), so a fade that bottomed out at 0 would have nothing to raise.
    if (g_music_level > 0) music_set_volume(1);

    // No music_start_menu() here any more: the splash jingle carries the whole
    // title screen, so CD-DA is not started until title_and_seed has faded it out.
    title_bg_fade_in_ex(TITLE_FADE_FRAMES, on_title_fade);

    int seed = title_and_seed();
    title_bg_fade_out(TITLE_FADE_FRAMES);
    display_apply();              // set the menu's background image/colour + text
    // Subscribed here rather than beside the other music callbacks above, and
    // deliberately after this display_apply: the title screen picks and shows its
    // own house, and music_start_menu() announces the neutral category, and
    // neither is meant to repaint the title out from under itself.
    music_set_category_fn(on_text_category);
    music_set_rotate_fn(on_text_rotate);
    music_set_fade_fn(on_music_fade);
    music_set_fade_frames(MUSIC_FADE_FRAMES);
    menu_intro_fade_arm();        // ...then hold it black across the swap so the
                                  // menu is composed unseen (display_apply lit the
                                  // backdrop; this re-darkens it with no frame shown)
    g_menu_intro_fade = TITLE_FADE_FRAMES;   // first menu_select fades the menu up

    static const char *modes[] = { "Single Player", "Online (Netlink)",
                                   "Load Save Game", "Options", "Credits" };
    const char* game_file = nullptr;

    // Menu-phase navigation fades are on for the whole loop: the mode menu,
    // Options and its pages, and the game/save pickers all fade between screens.
    // Cleared to 0 before gameplay so the in-game F10/F11/F12 menus stay instant.
    // game_select() and choose_dest() are entered at normal brightness and every
    // return leaves the screen faded to black -- so on a cancel the mode menu is
    // faded back in (g_menu_intro_fade), and on a pick the black simply carries
    // through the CD load into the instant reveal below.
    g_menu_page_fade = QUICK_FADE_FRAMES;

    for (;;) {
        int mode = menu_select("Z-ATURN", modes, 5);
        if (mode < 0) continue;
        if (mode == 3) {
            menu_fade_out(QUICK_FADE_FRAMES);      // mode-select dims to black
            // The menu track plays straight through Options here, unlike in-game
            // (saturn_glue.cxx), where the drive is held for as long as the menu is
            // up. Only one page under this could reach the disc -- Display, whose
            // Palette row lands on a picture that may not be resident -- and it
            // pins Dynamic to the wallpaper already uploaded for its own duration,
            // so nothing here moves the CD head at all.
            options_menu();                        // Options + sub-pages fade in/out
            g_menu_intro_fade = QUICK_FADE_FRAMES; // mode-select fades back in
            continue;
        }
        if (mode == 4) {
            menu_fade_out(QUICK_FADE_FRAMES);      // mode-select dims to black
            credits_page();                        // Credits fades in/out itself
            g_menu_intro_fade = QUICK_FADE_FRAMES; // mode-select fades back in
            continue;
        }
        if (mode == 1) { online_mode(); continue; }
        if (mode == 2) {
            game_file = game_select();
            if (game_file == nullptr) { g_menu_intro_fade = QUICK_FADE_FRAMES; continue; }
            g_story_filename = game_file;
            int device, slot;
            if (!choose_dest("LOAD - device?", "LOAD - slot?", &device, &slot)) {
                g_menu_intro_fade = QUICK_FADE_FRAMES; continue;
            }
            g_restore_device = device; g_restore_slot = slot;
            g_autocmd = "restore";
            break;
        }
        game_file = game_select();
        if (game_file == nullptr) { g_menu_intro_fade = QUICK_FADE_FRAMES; continue; }
        break;
    }
    g_story_filename = game_file;
    g_menu_page_fade = 0;   // leaving the menu phase; in-game menus stay instant

    // Up it goes, and it stays up: everything from here to loading_screen_end
    // happens with the boot block lit on screen and LOADCD.PCM running under it.
    // No progress text is printed into it -- the block is the progress report,
    // and a raw "loading ZORK1.Z3..." would break the fiction it is selling.
    // The disc's 8.3 name, not the catalogue's display title: the block this
    // types is a LOAD line, and a LOAD line names a file. "LOAD ZORK1.Z3,8,1"
    // is the thing being quoted; "LOAD ZORK I,8,1" is a title dressed up as one.
    loading_screen_begin(game_file);

    uint8_t *story = nullptr;
    uint32_t len = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                // A chunk at a time rather than one Read of the whole story.
                // The cue no longer depends on this -- it loops from the V-blank
                // interrupt and does not care whether the main line is blocked
                // (see loading_music.h) -- but reading in STORY_READ_CHUNK
                // pieces costs nothing, the same sectors in the same order, and
                // it keeps the screen's DEBUG readout advancing through what is
                // otherwise the longest blind stretch of the load.
                int32_t got = 0;
                while (got < bytes) {
                    int32_t want = bytes - got;
                    if (want > STORY_READ_CHUNK) want = STORY_READ_CHUNK;
                    int32_t n = f.Read(want, buf + got);
                    if (n <= 0) break;      // short read: fall through to the retry
                    got += n;
                    loading_screen_tick();
                    SRL::Core::Synchronize();
                }
                f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) { SRL::Memory::HighWorkRam::Free(buf); }
        }
        for (int i = 0; i < 8; i++) { loading_screen_tick(); SRL::Core::Synchronize(); }
    }
    if (story == nullptr) {
        // Take the screen down properly first: saturn_die's message has to land
        // on a cleared, un-held screen, and menu_fade_clear alone would only
        // release the hold and leave the boot block sitting under the error.
        loading_screen_end();
        menu_fade_clear();
        // Which of the two it was, because they need different fixes and the
        // screen is the only place this can be read: "dir LOST" means the /Z3
        // record was never captured, so the open resolved against whatever GFS
        // was pointing at; "dir ok" means the record was there and the read
        // itself failed, which is a disc or drive problem instead.
        saturn_die("Could not load %s from CD (Z3 dir %s)",
                   game_file, g_z3_dir_valid ? "ok" : "LOST");
    }

    loading_screen_tick();
    mojo_boot(story, len, seed);
    g_in_game = true;

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        // Still under the loading screen: this is a second CD read of its own and
        // belongs inside the cover, not after it. Ticked either side rather than
        // through the middle -- it is a run of short index reads rather than one
        // long transfer, so there is no seam inside it worth reaching into
        // sound.cxx for.
        loading_screen_tick();
        sound_init(blb);
        sound_set_level(g_pcm_level);
        loading_screen_tick();
    }

    // The game is built, so the screen and the cue bow out together. The music
    // engine starts after that rather than before: music_start puts the CD-DA head
    // to work, and doing it while the loading cue is still fading would have the
    // two overlap.
    loading_screen_end();

    // The trie first and the art second, in that order and not the other way. Both
    // want Low Work RAM and both are sized against whatever is free when they run,
    // so whichever goes first gets an honest reading and the other has to be
    // guessed at. The prompt would build this on its first turn anyway.
    saturn_typeahead_build();

    // Then the art, into what is genuinely left. The screen is still black, both
    // PCM cues have been freed and CD-DA has not started, so these are the last
    // reads on this path that cost nothing audible -- every one not taken here is
    // taken later at a mood change, over a playing track.
    display_warm_cache_random((unsigned int) seed);

    {
        music_set_level(g_music_level);
        music_set_game((unsigned int)((story[2] << 8) | story[3]), (const char*) (story + 0x12));
        music_seed((unsigned int) seed);
        music_reset();
        music_set_mix(g_mix_mode, g_sel_track);
        // Floored before the track is issued, exactly as the title screen does it,
        // so the opening frames play at 1 and game_intro_reveal has somewhere to
        // ramp up from. Never 0: music_set_volume(0) stops the drive.
        if (g_music_level > 0) music_set_volume(1);
        music_start();
    }

    // loading_screen_end left the screen faded to black, and it stays that way
    // through mojo_run's opening output. Revealing here would show the title's
    // wallpaper over an empty console, because the story has not run yet and the
    // picture on NBG0 is still the one the title screen put up -- the flash of the
    // old background before the room's own arrived. The first prompt reveals it
    // instead, by which point run_room_transition has settled the room's picture
    // and track and the opening text is composed.
    menu_clear();
    g_intro_reveal = game_intro_reveal;

    // The parser keeps the room-text mode in the story's own state, so the only
    // way to set it is to type it. Armed here rather than in the prompt so it
    // holds for a soft-reset return too, which re-runs this whole path.
    g_verb_pending = 1;

    g_output_start = console_total_lines();
    mojo_run();

    // A story that ended without ever reaching a prompt never spent the reveal, so
    // the screen is still held black and the final text below would be invisible.
    if (g_intro_reveal) { g_intro_reveal = 0; menu_fade_clear(); }

    render_console();
    text_print(1, 27, "(press any key/button for the title screen)");
    menu_wait();
    soft_reset_to_title();
    return 0;
}
