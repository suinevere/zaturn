/*----------------------
 | music_source.cxx
 | Description: Implementation of the source switch. A .cxx and not a .c only so
 |   the CD build's `find src/ -name '*.cxx'` picks it up and the netbin's
 |   explicit source list does not -- the netbin has no music.c to bind to.
 | Author: suinevere
 | Dependencies: music_source.h, music.h, synth.h, app_state.h
 ----------------------*/
#include "music_source.h"

extern "C" {
#include "music.h"
#include "synth.h"
}
#include "app_state.h"

/*----------------------
 | g_active
 | Description: The source currently bound, held rather than recomputed because
 |   music_cdda_has_audio caches its own answer and because everything that asks
 |   -- the fade ramp, the Sound page's row list -- asks every frame.
 | Author: suinevere
 ----------------------*/
static int g_active = MUSIC_SOURCE_CD;

/*----------------------
 | SYNTH_DUCK_STEPS
 | Description: How far the synth drops under an open menu, in its own 0..7
 |   level steps. Two, the same as music_cdda.cxx's CDDA_DUCK_STEPS, so the two
 |   sources duck by the same audible amount and a player cannot tell which one
 |   is playing from how far it gets out of the way.
 | Author: suinevere
 ----------------------*/
#define SYNTH_DUCK_STEPS 2

/*----------------------
 | synth_duck_down / synth_duck_up
 | Description: The engine's duck, for the synth. Shaped for music_set_duckfns,
 |   which takes no arguments -- the level to return to is the player's, which is
 |   where it always comes from.
 | Author: suinevere
 | Dependencies: synth.h, app_state.h
 | Globals: g_synth_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void synth_duck_down(void) {
    if (g_synth_level <= 0) return;
    int v = g_synth_level - SYNTH_DUCK_STEPS;
    synth_set_level(v < 1 ? 1 : v);
}
static void synth_duck_up(void) { synth_set_level(g_synth_level); }

void music_source_bind(void) {
    /* The preference only decides anything where there is something to decide.
       synth_should_play is asked rather than has_audio negated, so the netbin
       and this build answer the forced case through the same function. */
    int forced_synth = synth_should_play(music_cdda_has_audio());
    g_active = (forced_synth || g_music_source == MUSIC_SOURCE_SYNTH)
             ? MUSIC_SOURCE_SYNTH : MUSIC_SOURCE_CD;

    if (g_active == MUSIC_SOURCE_SYNTH) {
        /* All five together. The one that is easy to forget is is_playing: the
           engine reads not-playing as loop-end and, bound to CD-DA on a disc
           with none, sits in its await-play branch forever -- which is what the
           music did on such a disc before there was a source to switch. */
        music_set_backend(synth_play_track);
        music_set_isplaying(synth_playing);
        music_set_isshort(synth_track_is_short);
        music_set_pausefns(synth_pause, synth_resume);
        music_set_duckfns(synth_duck_down, synth_duck_up);
        synth_set_level(g_synth_level);
    } else {
        music_set_backend(music_cdda_play_mode);
        music_set_isplaying(music_cdda_is_playing);
        music_set_isshort(music_cdda_is_short);
        music_set_pausefns(music_cdda_pause, music_cdda_resume);
        music_set_duckfns(music_cdda_duck, music_cdda_unduck);
        music_set_level(g_music_level);
    }
}

int music_source_active(void) {
    return g_active;
}

int music_source_can_choose(void) {
    return music_cdda_has_audio() ? 1 : 0;
}

void music_source_select(int source) {
    if (source != MUSIC_SOURCE_CD && source != MUSIC_SOURCE_SYNTH) return;
    if (source == MUSIC_SOURCE_SYNTH && !music_cdda_has_audio()) {
        /* Already the only source; nothing to switch and nothing to restart. */
        g_music_source = MUSIC_SOURCE_SYNTH;
        return;
    }
    if (source == MUSIC_SOURCE_CD && !music_cdda_has_audio()) return;
    if (source == g_active) { g_music_source = source; return; }

    /* Silence what is being left before binding the new one. The engine's stop
       goes through the backend, and after the rebind that is the other source's
       -- so this has to happen first or the source being switched away from
       keeps sounding under the one being switched to. */
    if (g_active == MUSIC_SOURCE_SYNTH) synth_stop();
    else                                music_cdda_play_mode(0, 0);

    g_music_source = source;
    music_source_bind();

    /* Put the music back where the engine already thinks it is, through the
       backend it now has. music_refresh re-issues the active track without
       disturbing the engine's idea of the mood, which is what a source change
       should cost -- the room has not changed. It does nothing when nothing has
       played yet, so a source switched at the title draws instead. Both paths
       are source-agnostic: the engine names a CD-DA track either way, and the
       synth's backend turns that into the tune measured nearest to it. */
    if (music_active_track() > 0) music_refresh();
    else                          music_start_menu(0);
}
