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
 |   soft_reset.h, saturn_glue.h, saturn_backup.h, sound.h, music.h,
 |   input.h, SRL/GFS/SGL.
 ----------------------*/

#include <srl.hpp>
#include "text_map.h"
#include "dash_view.h"
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
#include "room_art.h"
#include "splash.h"
#include "game_catalog.h"
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
 |   picture, the CD-DA track and the PCM effects ramp down over this many
 |   together, swap at the bottom, and ramp back up over as many again. 45 is
 |   three quarters of a second each way, matching the splash jingle's
 |   TITLE_JINGLE_FADE_FRAMES. This is the number to cut if a mood change reads
 |   sluggish -- not the engine's 90-frame settle, which is what stops fast
 |   movement through a corridor from thrashing the music.
 | Author: suinevere
 ----------------------*/
#define MUSIC_FADE_FRAMES 45

/*----------------------
 | QUICK_FADE_FRAMES
 | Description: Mode-select to Options transition length, ~0.25s, matching
 |   menu_pages.cxx's fade of the same name.
 | Author: suinevere
 ----------------------*/
#define QUICK_FADE_FRAMES 15

/*----------------------
 | LOAD_FADE_FRAMES / GAME_REVEAL_FRAMES
 | Description: The two ramps the game-start path spends, and between them the
 |   whole of what the player waits through.
 |
 |   LOAD_FADE_FRAMES (90 = 1.5s) is the menu going down, and the story is read
 |   underneath it rather than after it -- menu_fade_out_begin paces the ramp off
 |   the field clock precisely so the two can share the time. It is a floor, not a
 |   budget: menu_fade_out_hold spends whatever is left if the read finished
 |   early, and a read that outlasts the ramp simply holds on black, which is what
 |   a slower drive than the one this was measured on will do.
 |
 |   GAME_REVEAL_FRAMES (30 = 0.5s) is the game coming up, and is deliberately not
 |   TITLE_FADE_FRAMES: the title's 90-field ramp is a title screen presenting
 |   itself, where this is a room the player is waiting to type into.
 | Author: suinevere
 ----------------------*/
#define LOAD_FADE_FRAMES   90
#define GAME_REVEAL_FRAMES 30

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
 | on_text_room
 | Description: The authored art's half of a room change, for stories that carry
 |   a per-room presentation. Unlike on_text_category this fires on every room,
 |   because every room has its own picture, and it takes that picture
 |   immediately rather than at the bottom of a fade: within an area the
 |   archive is already resident, so the change costs a decompress and touches
 |   no CD.
 |
 |   An area change does read the disc, and not under a fade: music_on_turn
 |   (music.c) calls g_room_fn -- this function -- inside its room_changed
 |   block, before it arms the debounced pending switch that later drives the
 |   fade, so room_art_show's read (up to 408.5 KB) happens immediately at full
 |   volume and the fade only starts once music_tick counts that switch down.
 |   Whether the read is audible against the CD-DA has not been measured. It is
 |   not a first-room cost either -- every area change that happens with music
 |   already playing is also a track change, since only track 0 spans more than
 |   one area.
 |
 |   The room is noted on every turn, not only on the turns that draw one: the
 |   picture goes up only under the Dynamic palette, and room_art_reshow needs a
 |   room to redraw for the player who selects Dynamic standing still.
 | Author: suinevere
 | Dependencies: room_art.h, options.h
 | Globals: g_display
 | Params: obj -- the room's object number
 | Returns: N/A
 ----------------------*/
static void on_text_room(unsigned int obj) {
    room_art_note_room(obj);
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    room_art_show(obj);
}

/*----------------------
 | music_fade_volume
 | Description: Maps a 0..255 fade level onto the CD-DA volume range, flooring at
 |   1 because music_set_volume(0) stops the drive with no way back -- that call
 |   is StopPause, not attenuation, and unlike music_set_level it has no
 |   resurrect path, so a ramp that reached 0 would raise a volume on a stopped
 |   disc and the music would simply be gone. Level 1 is quiet enough, and the
 |   swap at the bottom of a transition re-issues the track anyway.
 |
 |   A player who has set Music to 0 keeps silence: a fade never raises what they
 |   turned off.
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

/*----------------------
 | on_music_fade
 | Description: The room-transition step: everything that is not text moves on
 |   one counter. The picture rides colour offset channel B (title_bg_dyn_fade),
 |   the CD-DA track its volume register, and the PCM effects their own
 |   per-channel scale, so a room change takes the images and the sound down
 |   together and brings them back together on the far side of the swap.
 |
 |   The text deliberately does not go with them. It is on channel A with the
 |   marble chrome, and the turn's description is being read while this runs --
 |   a transition that blinked the words out mid-sentence is the thing
 |   title_bg_dyn_fade's channel split exists to prevent.
 | Author: suinevere
 | Dependencies: title.h, music.h, sound.h
 | Globals: N/A
 | Params: level -- 0 (black/silent) to 255 (normal)
 | Returns: N/A
 ----------------------*/
static void on_music_fade(int level) {
    title_bg_dyn_fade(level);
    music_fade_volume(level);
    sound_fade_level(level);
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
static void game_intro_reveal(void) { menu_fade_in_ex(GAME_REVEAL_FRAMES, music_fade_volume); }

/*----------------------
 | boot_entropy
 | Description: A number that differs from one run to the next, read off the
 |   Saturn's real-time clock. Used to pick the title screen's background from
 |   the disc's CGL frames.
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
 | title_pick_wallpaper
 | Description: Puts one of the disc's own room backgrounds behind the title
 |   screen, chosen fresh on every boot and every soft-reset return.
 |
 |   The frames are Zork I's CGL archives -- the same pictures the game shows in
 |   its rooms -- rather than a folder of title art, of which the disc carries
 |   none. Any of them will do: the title screen has no room, no scene and no
 |   game to suit, so the only requirement is that it is a picture and that it
 |   is not the same one every time.
 |
 |   Retried up to WALLPAPER_TRIES times because a pick can legitimately fail:
 |   the boot jingle is still resident here, and an area whose archive will not
 |   fit beside it is refused by room_art rather than forced. Stepping to the
 |   next frame lands in a different archive soon enough, and stepping by a
 |   number coprime with the frame count visits them in a different order for
 |   each seed instead of always walking the same run of neighbours. If every
 |   try is refused the title simply shows no wallpaper, which is what it did
 |   before it had one at all.
 | Author: suinevere
 | Dependencies: room_art.h, title.h, boot_entropy
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void title_pick_wallpaper(void) {
    const int WALLPAPER_TRIES = 6;
    const int n = room_art_frame_count();
    if (n <= 0) { title_bg_hide(); return; }

    unsigned int seed = boot_entropy();
    for (int try_i = 0; try_i < WALLPAPER_TRIES; try_i++) {
        if (room_art_show_frame((int) (seed % (unsigned int) n) + 1)) return;
        seed += 7u;
    }
    title_bg_hide();   // nothing would load: no stale picture left over from a game
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
 |   the way to the first picture -- the splash's jingle and logo, then the
 |   title's own CGL frame -- happens behind black rather than over a bare
 |   console. The game-catalogue scan runs behind the title art, in
 |   title_and_seed(), with the prompt already on screen; the splash itself
 |   covers nothing and is a fixed six seconds. After this the menu never
 |   touches the CD, so the track plays uninterrupted.
 |
 |   A soft-reset re-entry runs exactly the same sequence, deliberately: there is no
 |   cold-boot/return branch anywhere below. The catalogue is a cached static the
 |   longjmp did not touch, so a return pays for the logo's six seconds and one
 |   fresh wallpaper archive and nothing else. The Z3 load retries the flaky
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
 |   g_menu_backing_depth, g_music_level, g_pcm_level,
 |   g_story_filename, g_restore_device, g_restore_slot, g_autocmd,
 |   g_output_start, g_in_game, g_cmd_mode, g_cmd_iface
 | Params: N/A
 | Returns: 0 nominally, but it never actually returns
 ----------------------*/
int main(void) {
    // 320x240, SRL's own NTSC default. The client used to narrow this to 224
    // because every layer it painted was 224 lines tall and the surplus showed
    // the back-plane as a band under everything. Zork I's backgrounds are
    // 320x240 on the original disc, so the surplus now carries picture, and the
    // text grid grew to meet it rather than leaving a band.
    SRL::Core::Initialize(HighColor::Colors::Black, SRL::TV::Resolutions::Normal320x240);
    // Black in the raster around that 320x224, rather than the back-screen colour
    // VDP2 puts there by default -- which is the player's background colour, and
    // framed the picture with it.
    border_use_black();
    // The room backgrounds use all 256 CLUT entries, index 0 among them, and
    // VDP2 would otherwise punch that colour through to the back-plane. The
    // image window console_view aims at NBG0 is what still punches holes, and
    // it is unaffected by this.
    SRL::VDP2::NBG0::TransparentDisable();
    text_map_init();       // before anything prints: draws land in the shadow and
                           // reach VRAM on the vblank the next Synchronize waits for
    dash_init();        // after text_map_init: VDP2 and the font are up, and a
                        // failure here only means the printed borders stay
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
    display_set_authored(0); // no game is selected at the title/menu, so there is
                             // no room art; a longjmp back here does not otherwise
                             // clear the last game's flag
    slScrWindowModeNbg0(0);
    title_bg_fade_arm();     // a reset chord can fire mid-ramp, so overwrite any held
                             // offset -- with black, not with clear: nothing between
                             // here and the first fade-in below is meant to be seen
    // The boot screens carry no dim, and are the only wallpaper that does not.
    // The player's setting is a READING aid -- it exists because a room picture
    // at full brightness competes with the game text printed over it -- and the
    // logo and the title have three lines of text between them and are there to
    // be looked at. Applied here rather than before the loop so a soft-reset
    // return, which re-enters below the options load, gets it too; the player's
    // own value is put back by the display_apply() after the title fades out.
    title_bg_dim_set(0);
    console_init();

    music_reset();

    // A finished session leaves an area archive resident, up to 408.5 KB, and the
    // jingle splash_show is about to load wants ~453 KB of the same megabyte.
    // Unconditional: on a cold boot nothing is held and this costs nothing, and
    // the sequence below is meant not to know which boot it is.
    room_art_release();

    for (int r = 0; r < console_screen_rows(); r++) text_clear_line(r);

    // No game is selected at the title, so the Dynamic palette the client
    // defaults to correctly draws no room picture behind the menus. The title
    // screen's own wallpaper does not come from there -- it is picked below.
    splash_show();                // the logo, six seconds, loading nothing

    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF), DISP_RGB555(0, 0, 0));
    title_bg_fade_arm();          // black out first, so the title is composed unseen

    title_pick_wallpaper();

    // The backend goes in here rather than at game start, because the menu track
    // below is the engine's now too -- that is what makes it obey the cycle rule
    // instead of repeating one track forever.
    music_set_backend(music_cdda_play_mode);
    music_set_isplaying(music_cdda_is_playing);
    music_set_isshort(music_cdda_is_short);
    music_set_pausefns(music_cdda_pause, music_cdda_resume);
    music_set_duckfns(music_cdda_duck, music_cdda_unduck);

    music_set_level(g_music_level);
    // Down to the floor BEFORE the track is issued, or its first frames play at
    // whatever the player's saved level is and the ramp starts from full. Floor 1
    // and never 0: music_set_volume(0) calls StopPause() with no way back up (see
    // on_music_fade), so a fade that bottomed out at 0 would have nothing to raise.
    if (g_music_level > 0) music_set_volume(1);

    // No music_start_menu() here any more: the splash jingle carries the whole
    // title screen, so CD-DA is not started until title_and_seed has faded it out.
    title_bg_fade_in_ex(TITLE_FADE_FRAMES, on_title_fade);

    int seed = title_and_seed();
    title_bg_fade_out(QUICK_FADE_FRAMES);
    // The wallpaper's own archive, up to 408.5 KB, has done its job and the menu
    // phase has no picture at all -- so it goes back to the zone here rather than
    // sitting in it until a game is picked. Safe now and not a frame earlier: the
    // screen is black, so nothing is on NBG0 that anyone can see go.
    room_art_release();
    display_apply();              // set the menu's background image/colour + text
    // Subscribed here rather than beside the other music callbacks above, and
    // deliberately after this display_apply: the title screen picks and shows its
    // own house, and music_start_menu() announces the neutral category, and
    // neither is meant to repaint the title out from under itself.
    music_set_room_fn(on_text_room);
    music_set_fade_fn(on_music_fade);
    music_set_fade_frames(MUSIC_FADE_FRAMES);
    menu_intro_fade_arm();        // ...then hold it black across the swap so the
                                  // menu is composed unseen (display_apply lit the
                                  // backdrop; this re-darkens it with no frame shown)
    g_menu_intro_fade = TITLE_FADE_FRAMES;   // first menu_select fades the menu up

    static const char *modes[] = { "Single Player", "Online (Netlink)",
                                   "Load Save Game", "Options", "Credits" };
    const char* game_file = nullptr;

    // Navigation fades are on from here and stay on: the mode menu, Options and
    // its pages, the game/save pickers, and -- since it is the same
    // options_menu reached over live gameplay -- the in-game F10/F11/F12
    // openings too, which saturn_glue.cxx brackets with a matching ramp of the
    // gameplay screen so the same length reads as one transition either side.
    // game_select() and choose_dest() are entered at normal brightness and every
    // return leaves the screen faded to black -- so on a cancel the mode menu is
    // faded back in (g_menu_intro_fade), and on a pick the black simply carries
    // through the CD load into the instant reveal below.
    g_menu_page_fade = QUICK_FADE_FRAMES;

    // Held across the loop so every `continue` below -- Options, Credits,
    // Online, a cancelled game or save pick -- comes back to the row the player
    // left on rather than to Single Player.
    static int mode_sel = 0;

    for (;;) {
        int mode = menu_select_at("Z-ATURN", modes, 5, &mode_sel);
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
    // g_menu_page_fade is deliberately NOT cleared here. It used to be, so
    // that the in-game menus opened and closed instantly; what that actually
    // bought was a menu that popped on and, on the way out, left one frame
    // of its own text on a black rectangle before gameplay came back.

    // The menu goes down from here, and everything below happens underneath that
    // ramp rather than after it: game_select deliberately returns without fading,
    // so the first thing the read has over it is a screen still going dark. Every
    // menu_fade_out_tick below sets the level the clock says is due -- missing one
    // costs nothing but a coarser ramp, since the level is never derived from how
    // many times it was called.
    menu_fade_out_begin(LOAD_FADE_FRAMES);

    uint8_t *story = nullptr;
    uint32_t len = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                // A chunk at a time rather than one Read of the whole story:
                // the same sectors in the same order at no cost, but with a seam
                // between each pair for the fade to be stepped in. One whole-file
                // Read would black the screen in a single jump at the end of it.
                int32_t got = 0;
                while (got < bytes) {
                    int32_t want = bytes - got;
                    if (want > STORY_READ_CHUNK) want = STORY_READ_CHUNK;
                    int32_t n = f.Read(want, buf + got);
                    if (n <= 0) break;      // short read: fall through to the retry
                    got += n;
                    menu_fade_out_tick();
                    SRL::Core::Synchronize();
                }
                f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) { SRL::Memory::HighWorkRam::Free(buf); }
        }
        for (int i = 0; i < 8; i++) { menu_fade_out_tick(); SRL::Core::Synchronize(); }
    }
    if (story == nullptr) {
        // saturn_die's message has to land on an un-held screen, and the fade
        // above left it held black.
        menu_fade_clear();
        // Which of the two it was, because they need different fixes and the
        // screen is the only place this can be read: "dir LOST" means the /Z3
        // record was never captured, so the open resolved against whatever GFS
        // was pointing at; "dir ok" means the record was there and the read
        // itself failed, which is a disc or drive problem instead.
        saturn_die("Could not load %s from CD (Z3 dir %s)",
                   game_file, g_z3_dir_valid ? "ok" : "LOST");
    }

    menu_fade_out_tick();
    mojo_boot(story, len, seed);
    g_in_game = true;
    g_cmd_mode = g_cmd_iface;

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        // A second CD read of its own, and it belongs under the ramp like the
        // first. Stepped either side rather than through the middle -- it is a run
        // of short index reads rather than one long transfer, so there is no seam
        // inside it worth reaching into sound.cxx for.
        menu_fade_out_tick();
        sound_init(blb);
        sound_set_level(g_pcm_level);
        menu_fade_out_tick();
    }

    // Every later picture choice resolves against this, so it is read the moment
    // the story's header is readable.
    const unsigned int game_release = (unsigned int)((story[2] << 8) | story[3]);
    const char        *game_serial  = (const char*) (story + 0x12);

    // The one Low Work RAM claimant left on this path, now that the art is not
    // warmed here, so it gets the whole zone and an honest reading of it. Built here
    // rather than left to the prompt -- which would build it on its first turn
    // anyway -- only because the reading is honest here and the prompt's would not
    // be, with the cache having started to fill by then.
    saturn_typeahead_build();
    menu_fade_out_tick();

    // No art warm. A picture the cache does not hold is read at the bottom of the
    // mood fade, and music.c's commit_pending notifies the picture BEFORE it starts
    // the new track -- so the read lands where the volume is already at zero and the
    // screen has gone with it, costing a few frames of a dip nobody can see or hear.
    // Warming spent 158 fields of every single load pre-empting misses that are free
    // where they fall, and guessed at which scenes the player would reach. The cache
    // still fills, on demand, from rooms actually visited: better targeting than the
    // guess, at no cost to the load at all.

    // Whatever ramp the load did not use up. Nothing is charged here that the
    // reading already covered, and on a drive slow enough to outlast the ramp this
    // returns immediately. The music engine starts after it, not before:
    // music_start puts the CD-DA head to work.
    menu_fade_out_hold();

    {
        music_set_level(g_music_level);
        music_set_game(game_release, game_serial);
        room_art_set_game(game_release, game_serial);
        // Authored per-room art is the only art there is, so this flag is what
        // makes the Dynamic palette entry reachable at all: without it Dynamic is
        // skipped, and the room-art path, which only runs under Dynamic, never
        // draws. Cleared back to 0 on the way to the title screen above.
        display_set_authored(room_art_available());
        music_seed((unsigned int) seed);
        music_reset();
        // Floored before the track is issued, exactly as the title screen does it,
        // so the opening frames play at 1 and game_intro_reveal has somewhere to
        // ramp up from. Never 0: music_set_volume(0) stops the drive.
        if (g_music_level > 0) music_set_volume(1);
        music_start();
    }

    // The fade above left the screen held black, and it stays that way through
    // mojo_run's opening output. Revealing here would show the title's
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
    text_print(1, console_screen_rows() - 1, "(press any key/button for the title screen)");
    menu_wait();
    soft_reset_to_title();
    return 0;
}
