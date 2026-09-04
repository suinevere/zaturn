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
 |   menu.h, menu_layout.h, menu_pages.h, save_ui.h, title.h, game_catalog.h,
 |   online.h,
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
#include "menu_layout.h"
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
#include "video/item_art.h"
#include "map_view.h"
#include "splash.h"
#include "game_catalog.h"
#include "online.h"
using namespace SRL::Types;

/*----------------------
 | TITLE_FADE_FRAMES
 | Description: Title-screen fade length, ~1.5s at 60fps, matching the boot
 |   splash's SPLASH_FADE_FRAMES on both sides of it.
 |
 |   Unconditional, including on a boot whose logo the player skipped. That case
 |   briefly ran on QUICK_FADE_FRAMES, on the reasoning that a press is a request
 |   to be at the title already -- but a quarter-second is the length of a cut,
 |   not of a title screen arriving, and it read as one against the ramp that had
 |   just taken the logo off. What the skip saves is the logo's own hold; the
 |   screen it hands to is worth the same second and a half either way.
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
 | Description: The two ramps the game-start path spends, with the loading screen
 |   and the whole of what the player waits through between them.
 |
 |   LOAD_FADE_FRAMES (90 = 1.5s) is the loading screen going down, and it is the
 |   last thing the path does before the story's own screen is composed: the read
 |   is finished, the drive is idle, and nothing inside the ramp can block. It ran
 |   under the read once, paced off a field clock so the two could share the time
 |   -- but a read blocks the main line for whole frames, so the level only
 |   reached the hardware between two of them and the screen came down in jumps at
 |   whatever rate the drive returned at. The time is no longer shared; the
 |   waiting has a screen of its own to be spent on instead.
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
 |   loading screen is up. Eight 2048-byte sectors: enough that the per-call
 |   overhead stays irrelevant next to the transfer, small enough that the gap
 |   between two reads is around a tenth of a second at single speed, which is the
 |   resolution the screen's audio cue needs to be serviced at. Purely a pacing
 |   figure -- the bytes read, their order and the total are identical to the one
 |   whole-file Read this replaced.
 | Author: suinevere
 ----------------------*/
#define STORY_READ_CHUNK (2048 * 8)
/*----------------------
 | g_art_room
 | Description: The room whose picture the next transition owes. Written when the
 |   engine reports the room change, spent when the ramp reaches the bottom -- and
 |   overwritten in between by any further room the player walks into during the
 |   settle, which is the point: what should come up is the room they stopped in,
 |   not the first one they passed through.
 | Author: suinevere
 ----------------------*/
static unsigned int g_art_room = 0;

/*----------------------
 | on_text_room
 | Description: The authored art's half of a room change, for stories that carry a
 |   per-room presentation. Fires on every room, because every room has its own
 |   picture -- but it does not put that picture up. It resolves whether the
 |   picture would move and hands the answer to the engine, which arms its ramp on
 |   it and calls on_art_commit at the bottom.
 |
 |   Showing it here is what the transition used to look wrong for: the new picture
 |   arrived at full brightness the instant the turn was parsed, and the ramp that
 |   followed then darkened it and lit the same picture back up. Deferring also
 |   moves an area change's disc read -- up to 408.5 KB -- under the black, where a
 |   read nobody can see is a read nobody minds.
 |
 |   Answering 0 outside Dynamic is not just a skip: no picture is showing under
 |   the other palettes, so there is nothing for a ramp to move, and an arm left
 |   standing from an earlier room would spend one on a screen that cannot change.
 |
 |   The room is noted on every turn, not only on the turns that draw one: the
 |   picture goes up only under the Dynamic palette, and room_art_reshow needs a
 |   room to redraw for the player who selects Dynamic standing still.
 | Author: suinevere
 | Dependencies: room_art.h, options.h, music.h
 | Globals: g_art_room, g_display
 | Params: obj -- the room's object number
 | Returns: N/A
 ----------------------*/
static void on_text_room(unsigned int obj) {
    room_art_note_room(obj);
    g_art_room = obj;
    if (g_display.palette != DISP_PAL_DYNAMIC) { music_art_change(0); return; }
    music_art_change(room_art_changes_for(obj));
}

/*----------------------
 | on_art_commit
 | Description: Puts the room's background up, called by the music engine at the
 |   bottom of a transition's ramp. Everything about this is the timing: the screen
 |   is black here, so an area archive read costs nothing anyone can see, and the
 |   picture is already in place when the ramp lights it.
 |
 |   The palette is re-tested rather than trusted from the arm, because a
 |   transition is a second and a half long and Options is reachable inside it --
 |   a player who left Dynamic during the ramp would otherwise get the room picture
 |   they had just turned off.
 | Author: suinevere
 | Dependencies: room_art.h, options.h
 | Globals: g_art_room, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void on_art_commit(void) {
    if (g_display.palette != DISP_PAL_DYNAMIC) return;
    room_art_show(g_art_room);
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
 |   one counter. The picture rides colour offset channel B (title_bg_dyn_fade)
 |   and the PCM effects their own per-channel scale, so a room change takes the
 |   images and the effects down together and brings them back on the far side of
 |   the swap.
 |
 |   The CD-DA volume only joins them when the engine says the track is being
 |   re-issued under this ramp. That is what the dip is for -- covering a fresh
 |   play -- and Zork I changes picture at 74 room boundaries against 13 track
 |   changes, so riding the volume on all of them would leave the score pulsing
 |   for the sixty-one where nothing about it changed.
 |
 |   The picture and the effects, in turn, only join when the engine says a
 |   picture is being put up. A track change on its own moves nothing on screen,
 |   so there is nothing to hide and no reason to dim the room the player is
 |   reading -- and, since run_room_transition waits only on the picture half, no
 |   reason to hold the prompt for it either.
 |
 |   The text deliberately does not go with any of them. It is on channel A with
 |   the marble chrome, and the turn's description is being read while this runs --
 |   a transition that blinked the words out mid-sentence is the thing
 |   title_bg_dyn_fade's channel split exists to prevent.
 | Author: suinevere
 | Dependencies: title.h, music.h, sound.h
 | Globals: N/A
 | Params: level -- 0 (black/silent) to 255 (normal); audio -- nonzero when the
 |   track is being re-issued and its volume must ride the ramp too; art --
 |   nonzero when a picture is being put up and the screen must go dark for it
 | Returns: N/A
 ----------------------*/
static void on_music_fade(int level, int audio, int art) {
    // The screen and the sound effects follow the picture; the CD-DA volume
    // follows the track. A transition that only changes the track has nothing on
    // screen to hide, and dipping the room the player is reading for it was a
    // second and a half of dimming for a change they could only hear.
    if (art) { title_bg_dyn_fade(level); sound_fade_level(level); }
    if (audio) music_fade_volume(level);
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
 | LOADING_REVEAL_FRAMES
 | Description: How long the loading screen takes to come up out of the black the
 |   picker left. Deliberately shorter than the ramp that takes it back down: this
 |   end is a word arriving in front of a wait nobody asked for, and the other end
 |   is the hand-off to the game.
 | Author: suinevere
 ----------------------*/
#define LOADING_REVEAL_FRAMES 20

/*----------------------
 | loading_screen_show
 | Description: Puts LOADING centred on the menu's own wallpaper and fades it up
 |   out of the black every picker leaves behind, so the story read, the sound
 |   blorb, the typeahead build and the map's parchment all happen under a screen
 |   that says what the machine is doing.
 |
 |   Leaves the screen lit and the offset channels released, which is what the
 |   single ramp at the far end of the load re-engages. Nothing here touches the
 |   drive: the fade is the last free moment before the read starts.
 | Author: suinevere
 | Dependencies: menu.h, text_map.h, console_view.h, menu_layout.h,
 |   dash_map.h (dash_clear)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void loading_screen_show(void) {
    static const char WORD[] = "LOADING...";
    // The picker's box is still on the black the ramp left behind. ~MenuBacking
    // owes its window and its marble to the next frame that CHANGES THE TEXT, and
    // a fade changes none -- every frame of one holds what is painted rather than
    // drawing, which is how the box outlived the ramp that was meant to be its
    // exit. Left alone, the ramp below simply lit it again with the word inside
    // it, having never been seen to go.
    //
    // menu_clear is what ends it, in two halves that both land on that ramp's
    // first frame, at level 0, where nothing is visible: the cleared rows are
    // what makes the owed window-off fire, and dash_clear drops the latch that
    // stops the marble expiring and blanks the layer outright. Deliberately no
    // text_flush between them and the ramp -- flushing here would empty the dirty
    // span the window-off is waiting on, and leave the box's rectangle punched
    // out of the wallpaper for the rest of the load.
    menu_clear();
    dash_clear();
    text_print((MENU_SCREEN_COLS - (int) (sizeof(WORD) - 1)) / 2,
               console_screen_rows() / 2, WORD);
    menu_fade_in(LOADING_REVEAL_FRAMES);
}

/*----------------------
 | title_show_wallpaper
 | Description: Puts TITLE.TGA behind the title screen.
 |
 |   One authored picture, committed to the disc, shown every boot. It used to be
 |   one of Zork I's own room frames picked off the real-time clock, on the
 |   reasoning that the disc carried no title art and any picture would do; there
 |   is title art now, so none of that reasoning survives -- not the clock read,
 |   not the retry walk over the frame table, and not the room-art frame API the
 |   walk was the only caller of.
 |
 |   A refusal leaves no wallpaper rather than whatever a previous game left on
 |   NBG0, which is the one thing the old pick and this share.
 | Author: suinevere
 | Dependencies: title.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void title_show_wallpaper(void) {
    if (!title_bg_show_oneoff("TITLE.TGA")) title_bg_hide();
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
 |   the CD-DA backend, so it obeys the engine's own loop-end rules; that is also
 |   why the backend callbacks are installed here rather than at game start. On a
 |   cold boot it is MUSIC_OPENING_TRACK and stays there for as long as the menu
 |   is open -- the machine introduces itself the same way every session -- and
 |   only a Return to Title draws one from the pool and lets it cycle. Every CD read
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
 |   A soft-reset re-entry runs exactly the same sequence, deliberately: the only
 |   thing below that reads which one it is is the menu track, through
 |   g_returned_to_title. The catalogue is a cached static the
 |   longjmp did not touch, so a return pays for the logo's six seconds and one
 |   re-read of TITLE.TGA and nothing else. The Z3 load retries the flaky
 |   first-access GFS size stat before allocating and reading. Both it and the
 |   sound-blorb read after it run underneath the loading screen, which is
 |   raised before them and not taken down until the game is built. Anything on
 |   this path that steps out of /Z3 has to step back
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
 | Globals: g_display, g_pad, g_title_jmp, g_title_jmp_armed,
 |   g_returned_to_title, g_z3_dir_valid,
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

    // Before the splash, and before the setjmp below, so a soft reset does not
    // re-ask: the answer cannot have changed, and one of the two answers leaves
    // for the BIOS anyway. Needs the pad, hence its place under it.
    save_space_warn();

    // setjmp answers 0 on the way in and 1 on a longjmp back, and that answer is
    // the only thing distinguishing a cold boot from a Return to Title on this
    // path -- everything below it runs identically, deliberately. Kept because
    // the menu track is the one place the difference is wanted; see
    // g_returned_to_title.
    g_returned_to_title = (setjmp(g_title_jmp) != 0);
    g_title_jmp_armed = true;
    GFS_Reset();
    cd_capture_root();
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    // The map sets this while it is showing its own ground, and clears it on the
    // way out -- a way out the reset chord skips, since the map polls for it.
    // Left set, every fade from here on would drive the map's tan.
    menu_back_override(0);
    // And the menu chrome the same longjmp left painted. Coming out of Return to
    // Title there are two guards whose destructors never ran -- the confirm box's
    // and the Options page's underneath it -- so NBG2 still holds the box, and a
    // latch owed by an earlier one holds it there: dash_frame_end will not expire
    // a latched layer, and nothing on the title screen ever claims NBG2 to paint
    // over it. The box sat on the logo and the menu for the rest of the session.
    dash_clear();
    // And the two debts a save or restore leaves for the prompt on the far side
    // of the interpreter's turn. A reset chord taken inside a picker jumps out
    // between the two, and a set g_menu_reopen would open the pause menu over the
    // next game's first prompt.
    g_menu_reopen = 0;
    g_screen_owed = 0;
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
    item_art_close();
    // And the plate, if the jump came from a title screen that still had one up:
    // the splash below draws on NBG0 and would wear the ZATURN over the SUINEVERE.
    title_logo_hide();
    // And the map's parchment, which is the same claim against the OTHER zone:
    // 78 KB of a 194 KB C heap, held from the first time the player opened the
    // map and kept by a plain static across the longjmp. Left resident it is
    // taken out of the next story's allocation, and nine of the thirty-one
    // stories on the disc are larger than what would be left -- which is the
    // load that silently retries for forty seconds and then says it could not
    // read the disc.
    title_bg_drop_held();

    for (int r = 0; r < console_screen_rows(); r++) text_clear_line(r);

    // No game is selected at the title, so the Dynamic palette the client
    // defaults to correctly draws no room picture behind the menus. The title
    // screen's own wallpaper does not come from there -- it is TITLE.TGA, put up
    // below.
    splash_show();                // the logo, six seconds, loading nothing

    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF), DISP_RGB555(0, 0, 0));
    title_bg_fade_arm();          // black out first, so the title is composed unseen

    title_show_wallpaper();
    // The plate and the credit under it go up here rather than in
    // title_and_seed, which runs after the ramp below: composed before the fade
    // means the whole title screen arrives on it, instead of the wallpaper
    // arriving and the rest snapping on at the end of it. The "Press any button"
    // line is deliberately NOT here -- it is a promise that pressing does
    // something, and it waits for the catalogue scan title_and_seed runs behind
    // this screen.
    title_logo_show();
    title_draw_art();

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
    // The title's own picture frees itself -- title_bg_show_oneoff drops its
    // buffer the moment the upload lands -- so what this still answers for is a
    // previous game's area archive, and forgetting which image NBG0 holds. Safe
    // here and not a frame earlier: the screen is black, so nothing is on NBG0
    // that anyone can see go.
    room_art_release();
    item_art_close();
    title_bg_drop_held();
    // NBG1 is the title's alone until the first inventory opens, and nothing
    // between here and there draws to it -- so the plate would sit over the mode
    // menu, the game list and every page under them until an item picture
    // happened to overwrite it.
    title_logo_hide();
    display_apply();              // set the menu's background image/colour + text
    // Subscribed here rather than beside the other music callbacks above, and
    // deliberately after this display_apply: the title screen picks and shows its
    // own house, and music_start_menu() announces the neutral category, and
    // neither is meant to repaint the title out from under itself.
    music_set_room_fn(on_text_room);
    music_set_art_fn(on_art_commit);
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
        int mode = menu_select_final("Z-ATURN", modes, 5, &mode_sel);
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

    // The pickers leave the screen black-held, so the wait gets a screen of its
    // own: LOADING up out of that black, everything slow underneath it, and one
    // ramp back down to black at the end with nothing blocking inside it. The
    // ramp used to run UNDER the read, paced off the field clock -- correct in
    // level and wrong in motion, because the level only reached the hardware
    // between two blocking reads and the screen therefore stepped down in whole
    // jumps at whatever rate the drive happened to return at.
    loading_screen_show();

    uint8_t *story = nullptr;
    uint32_t len = 0;
    // Set when a read completed but what came back was not a story, so the
    // failure notice below can say which of the two it was. A short read and a
    // full read of the wrong bytes need different fixes and look identical from
    // the outside.
    bool bad_header = false;
    // Set when the heap could not hold the image, with what was asked for and
    // what was free. Not a retryable condition and not a CD one: every attempt
    // after the first asks the same allocator for the same bytes and is told the
    // same thing, so the loop is left at once rather than spending three hundred
    // attempts at eight fields each -- forty seconds of LOADING standing still,
    // which is indistinguishable from a hang and was reported as one.
    uint32_t oom_want = 0, oom_free = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf == nullptr) {
                oom_want = (uint32_t) bytes;
                oom_free = (uint32_t) SRL::Memory::HighWorkRam::GetFreeSpace();
                break;
            }
            if (f.Open()) {
                // A chunk at a time rather than one Read of the whole story:
                // the same sectors in the same order at no cost, but with a
                // Synchronize between each pair, so the loading screen keeps
                // being pushed and the audio cue keeps being serviced instead of
                // the machine going away for the length of one whole-file Read.
                int32_t got = 0;
                while (got < bytes) {
                    int32_t want = bytes - got;
                    if (want > STORY_READ_CHUNK) want = STORY_READ_CHUNK;
                    int32_t n = f.Read(want, buf + got);
                    if (n <= 0) break;      // short read: fall through to the retry
                    got += n;
                    SRL::Core::Synchronize();
                }
                f.Close();
                // The header, not merely the byte count. A story that reached
                // the interpreter and was refused there halted the machine on
                // "only version 3 is supported, this is 32" -- 32 being a space,
                // so what had been read was text rather than a story -- and that
                // is a message about the wrong thing, printed from a place with
                // no way back. The size guard above cannot catch it: the note in
                // saturn_read_story_prefix that the GFS size read can come back
                // garbage on a first access is exactly why this loop retries,
                // and a garbage size inside the plausible range buys a complete,
                // successful read of something that is not the file asked for.
                //
                // Two fields, because one is not enough to tell a story from a
                // coincidence: the version byte, and the length word at 0x1A,
                // which is in words for a v3 image and so covers no more than
                // the file itself. Every story on the disc satisfies both; the
                // test that says so is saturn/tests/test_story_header.py.
                if (got == bytes) {
                    int32_t hdr_len = (int32_t) (((buf[0x1a] << 8) | buf[0x1b]) * 2);
                    if (buf[0] == 3 && hdr_len > 0 && hdr_len <= bytes) {
                        story = buf; len = (uint32_t) bytes; break;
                    }
                    bad_header = true;
                }
            }
            SRL::Memory::HighWorkRam::Free(buf);
        }
        for (int i = 0; i < 8; i++) { SRL::Core::Synchronize(); }
    }
    if (story == nullptr) {
        // The loading screen is lit, not held, so there is no hold to release --
        // only its own word to take off before the halt notice is written where
        // it stood.
        menu_clear();
        // The heap is its own answer and needs its own sentence. It is not a CD
        // fault at all, and the two numbers are the whole diagnosis: what the
        // image wanted against what was free when it asked. __heap_start moves
        // with the program image, so the second number falls every time the
        // build grows -- which is how the largest story on the disc came to stop
        // loading without one line of its own path changing.
        if (oom_want != 0) {
            saturn_die("Out of memory loading %s: wants %u bytes, %u free",
                       game_file, (unsigned int) oom_want, (unsigned int) oom_free);
        }
        // Which of the two it was, because they need different fixes and the
        // screen is the only place this can be read: "dir LOST" means the /Z3
        // record was never captured, so the open resolved against whatever GFS
        // was pointing at; "dir ok" means the record was there and the read
        // itself failed, which is a disc or drive problem instead.
        saturn_die("Could not load %s from CD (Z3 dir %s, %s)",
                   game_file, g_z3_dir_valid ? "ok" : "LOST",
                   bad_header ? "read the wrong file" : "read failed");
    }

    mojo_boot(story, len, seed);

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        // A second CD read of its own, and it belongs under the loading screen
        // like the first.
        sound_init(blb);
        sound_set_level(g_pcm_level);
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

    // No art warm. A picture the cache does not hold is read at the bottom of the
    // mood fade, and music.c's commit_pending notifies the picture BEFORE it starts
    // the new track -- so the read lands where the volume is already at zero and the
    // screen has gone with it, costing a few frames of a dip nobody can see or hear.
    // Warming spent 158 fields of every single load pre-empting misses that are free
    // where they fall, and guessed at which scenes the player would reach. The cache
    // still fills, on demand, from rooms actually visited: better targeting than the
    // guess, at no cost to the load at all.

    {
        music_set_level(g_music_level);
        music_set_game(game_release, game_serial);
        room_art_set_game(game_release, game_serial);
        item_art_set_game(game_release, game_serial);
        // The map's parchment, read here for the same reason item_art_set_game
        // reads OITEM.CZ here: it is the last moment the drive is free. Every
        // later opening of the map would take this seek with CD-DA playing, and
        // a seek does not merely interrupt a track -- one that was never held
        // reads to the music engine as ended and restarts from the top, which is
        // heard as the music cutting out and coming back. Declines by itself
        // when the story is too large to hold the picture beside it.
        map_view_preload(game_release, game_serial);
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
    }

    // Everything that moves the drive head is done, so the ramp below has nothing
    // blocking inside it: LOADING goes down over LOAD_FADE_FRAMES uninterrupted,
    // which is the whole reason it is here and not spread through the read. The
    // music engine starts after it and not before -- music_start puts the CD-DA
    // head to work, and it would be starting under a screen that is still lit.
    menu_fade_out(LOAD_FADE_FRAMES);
    // After the ramp, not back at mojo_boot where it reads more naturally: a fade
    // holds the screen by claiming NBG2 every frame, and dash_hold paints the
    // gameplay strip the moment this flag says there is a game -- so setting it
    // any earlier drew the input strip onto the loading screen and took it down
    // again over the ramp.
    g_in_game = true;
    g_cmd_mode = g_cmd_iface;
    music_start();

    // The ramp above left the screen held black, and it stays that way through
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
