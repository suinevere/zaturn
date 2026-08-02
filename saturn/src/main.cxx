/*----------------------
 | main.cxx
 | Description: The Saturn client entry point and boot orchestrator. Initializes
 |   the console and peripherals, arms the soft-reset return-to-title target,
 |   front-loads what CD reads it can into the title screen's silent window (game
 |   catalogue, the online Zork I vocabulary, and the title picture), starts the menu
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
        for (int r = 0; r < console_height(); r++) SRL::Debug::PrintClearLine(r);
    }
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    int slot = display_dynamic_slot();
    if (slot == DISP_IMAGE_NONE) return;
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
 | Description: The title screen's version: the CD-DA track rises on the picture's
 |   own ramp, so the image and the music reach full together. No wallpaper call --
 |   the picture here is being lit by title_bg_fade_in_ex itself, on colour offset
 |   channel A, rather than by the in-game channel-B dimmer.
 |
 |   Level is coarse on purpose and cannot be otherwise: CD-DA volume is 0..7, so a
 |   ninety-frame ramp crosses seven steps, about one every thirteen frames. That is
 |   audibly a rise rather than a smooth one, and it is the whole range the hardware
 |   offers.
 | Author: suinevere
 | Dependencies: music.h, app_state.h
 | Globals: g_music_level
 | Params: level -- 0 (black/quiet) to 255 (normal)
 | Returns: N/A
 ----------------------*/
static void on_title_fade(int level) { music_fade_volume(level); }

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
 |   precedes any GFS_SetDir; display_scan_images precedes options_load so saved
 |   image indices validate against the real list. setjmp arms g_title_jmp so the
 |   soft reset (chord or typed reboot/quit) longjmps back here; because that jump
 |   skips destructors, the re-entry path hand-clears g_menu_backing_depth (else
 |   NBG3 stays opaque and hides the title image) and disables the NBG0 image
 |   window, and does NOT re-scan /TGA -- the first-boot scan's list and g_tga_tbl
 |   are plain static RAM that survives the longjmp, and a destructive post-reset
 |   re-scan once wiped the list and made every options background vanish. The
 |   story image is owned by the Z-machine (initStory frees the prior one), so it
 |   is never freed here. music_reset before the menu track clears stale engine
 |   state so a menu-frame music_tick cannot leak a game track. The menu track is
 |   started through the engine (music_start_menu) rather than handed straight to
 |   the CD-DA backend, so it obeys the same play-count-and-cycle rule the in-game
 |   music does instead of repeating one track for as long as the menu is open;
 |   that is also why the backend callbacks are installed here rather than at game
 |   start. Every CD read
 |   finishes before CD-DA starts, because the single drive head cannot play
 |   CD-DA while reading data. The screen is held black from Core::Initialize
 |   onward and is only ever lifted by an explicit fade-in, so every CD read on
 |   the way to the first picture -- the image scan here, the splash's own reads,
 |   HOUSE1.TGA -- happens behind black rather than over a bare console. On first
 |   cold boot, splash_show_once() covers
 |   the online-vocabulary read with a fading logo instead
 |   of a silent title picture; the game-catalogue scan (preload_game_catalog)
 |   runs afterward, its own silent beat once HOUSE1.TGA is already showing but
 |   still before the track starts and before title_and_seed()'s "Press any
 |   button" prompt appears. After this the menu never touches the CD, so the
 |   track plays uninterrupted (and on soft-reset re-entry splash_show_once()
 |   takes its no-splash branch and preload_game_catalog() is a cached no-op,
 |   so nothing new needs to run before the music starts). The Z3 load retries
 |   the flaky
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
    title_bg_fade_arm();
    saturn_bup_init();
    cd_capture_root();
    display_scan_images();
    display_defaults(&g_display);
    options_load();

    static MultiPad pads;
    g_pad = &pads;

    int cd_reentry = setjmp(g_title_jmp);
    (void) cd_reentry;
    g_title_jmp_armed = true;
    GFS_Reset();
    cd_capture_root();
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    g_in_game = false;
    slScrWindowModeNbg0(0);
    title_bg_fade_arm();
    console_init();

    music_reset();

    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);

    display_shuffle_category(TC_HOUSE, boot_entropy());
    display_set_dynamic_category(TC_HOUSE);
    if (g_display.palette == DISP_PAL_DYNAMIC) {
        int slot = display_dynamic_slot();
        if (slot != DISP_IMAGE_NONE) g_display.image = slot;
    }

    splash_show_once();

    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));
    title_bg_fade_arm();

    title_bg_show(display_category_image(TC_HOUSE));

    music_set_backend(music_cdda_play_mode);
    music_set_isplaying(music_cdda_is_playing);
    music_set_isshort(music_cdda_is_short);
    music_set_pausefns(music_cdda_pause, music_cdda_resume);

    music_set_level(g_music_level);
    music_set_mix(g_mix_mode, g_sel_track);
    if (g_music_level > 0) music_set_volume(1);

    title_bg_fade_in_ex(TITLE_FADE_FRAMES, on_title_fade);

    int seed = title_and_seed();
    title_bg_fade_out(TITLE_FADE_FRAMES);
    display_apply();
    music_set_category_fn(on_text_category);
    music_set_rotate_fn(on_text_rotate);
    music_set_fade_fn(on_music_fade);
    music_set_fade_frames(MUSIC_FADE_FRAMES);
    menu_intro_fade_arm();
    g_menu_intro_fade = TITLE_FADE_FRAMES;

    static const char *modes[] = { "Single Player", "Online (Netlink)",
                                   "Load Save Game", "Options", "Credits" };
    const char* game_file = nullptr;

    g_menu_page_fade = QUICK_FADE_FRAMES;

    for (;;) {
        int mode = menu_select("Z-ATURN", modes, 5);
        if (mode < 0) continue;
        if (mode == 3) {
            menu_fade_out(QUICK_FADE_FRAMES);
            music_pause();
            options_menu();
            music_resume();
            g_menu_intro_fade = QUICK_FADE_FRAMES;
            continue;
        }
        if (mode == 4) {
            menu_fade_out(QUICK_FADE_FRAMES);
            credits_page();
            g_menu_intro_fade = QUICK_FADE_FRAMES;
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
    g_menu_page_fade = 0;

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
                int32_t got = 0;
                while (got < bytes) {
                    int32_t want = bytes - got;
                    if (want > STORY_READ_CHUNK) want = STORY_READ_CHUNK;
                    int32_t n = f.Read(want, buf + got);
                    if (n <= 0) break;
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
        loading_screen_end();
        menu_fade_clear();
        saturn_die("Could not load %s from CD", game_file);
    }

    loading_screen_tick();
    mojo_boot(story, len, seed);
    g_in_game = true;

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        loading_screen_tick();
        sound_init(blb);
        sound_set_level(g_pcm_level);
        loading_screen_tick();
    }

    loading_screen_end();

    {
        music_set_level(g_music_level);
        music_set_game((unsigned int)((story[2] << 8) | story[3]), (const char*) (story + 0x12));
        music_seed((unsigned int) seed);
        music_reset();
        music_set_mix(g_mix_mode, g_sel_track);
        music_start();
    }

    menu_clear();
    menu_fade_clear();

    g_output_start = console_total_lines();
    mojo_run();

    render_console();
    SRL::Debug::Print(1, 27, "(press any key/button for the title screen)");
    menu_wait();
    soft_reset_to_title();
    return 0;
}
