/*----------------------
 | music_cdda.cxx
 | Description: The Saturn CD-DA backend behind the platform-independent music
 |   engine: play/stop/volume against SRL's Cdda block, plus a hand-decoded CD
 |   table of contents used to enumerate the disc's audio tracks and measure each
 |   track's length. It reads the raw BIOS TOC directly because
 |   SRL::Cd::TableOfContents is unusable here (see the TOC section box).
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda, CDC_* BIOS TOC/status calls), music.h (the
 |   backend/query signatures the engine calls through)
 ----------------------*/
#include <srl.hpp>
extern "C" {
#include "music.h"
}

/*----------------------
 | g_level / g_track / g_loop / g_vol_stopped
 | Description: Backend state. g_level is the CD-DA output level (0..7, SRL's
 |   SetVolume max; 0 = silence). g_track is the currently requested track (0 =
 |   none) and g_loop whether it repeats forever, kept so a mute->unmute restart
 |   can re-issue the same request.
 |
 |   g_vol_stopped says the drive is stopped because the level reached 0, as opposed
 |   to stopped because nothing was asked for -- only the first may be restarted by
 |   raising the volume, and g_level cannot tell them apart, being the level the
 |   player asked for rather than whether the head is running. Reading the unmute
 |   edge off g_level instead was the bug where dropping Music to 0 silenced the
 |   game for the rest of the session: music_set_volume moved g_level off 0 as the
 |   slider came back up, so the music_set_level on Ok found no edge left to act on
 |   and never reissued the track.
 | Author: suinevere
 ----------------------*/
static int g_level = 7;
static int g_track = 0;
static int g_loop  = 1;
static int g_vol_stopped = 0;

/*----------------------
 | CDDA_DUCK_STEPS / g_ducked / out_level
 | Description: Whether a duck is in force, and the level every volume write has to
 |   go through because of it. The duck is not a one-off write: the engine keeps
 |   cycling tracks underneath an open menu, and each new track is issued by
 |   music_cdda_play_mode, which sets the volume as it plays. Writing g_level there
 |   would hand the next track full volume and quietly cancel the duck -- which is
 |   exactly what happened. Every SetVolume outside music_cdda_unduck goes through
 |   out_level; tests/test_music_unmute.py holds that line.
 |
 |   Steps off the level rather than taking a fraction of it, because 0..7 is an
 |   attenuation scale -- a fixed number of steps is a fixed drop however loud the
 |   player runs the music, where a fraction would flatten the whole middle of the
 |   range onto 1. Floored at 1 so a low setting ducks to audible rather than to
 |   nothing. Nudge CDDA_DUCK_STEPS to taste; 0 disables ducking.
 | Author: suinevere
 ----------------------*/
#define CDDA_DUCK_STEPS 2

static int g_ducked = 0;

static int out_level(void) {
    if (!g_ducked) return g_level;
    int q = g_level - CDDA_DUCK_STEPS;
    return (q < 1) ? 1 : q;
}

#define MUSIC_SHORT_SECONDS 15

/*----------------------
 | raw CD table of contents (g_toc / TOC_* / the toc_* helpers)
 | Description: SRL::Cd::TableOfContents cannot read the TOC: TrackLocation derives
 |   from ITrack, so Control sits in its own 4-byte base subobject and
 |   sizeof(TrackLocation) is 8 not 4 -- the struct measures 812 bytes while
 |   CDC_TgetToc only writes the BIOS's 408-byte (102-longword) TOC. So toc.Tracks[t]
 |   reads longword 2t (the wrong track) and entries past t=50 are uninitialized
 |   stack; that produced a ~40-entry selector on a 32-track disc with most entries
 |   silent. This code reads the 102-longword TOC itself. Layout: [0..98] one entry
 |   per CD track 1..99 as (ctrladr<<24)|fad (absent tracks read 0xFFFFFFFF); [99]
 |   first-track and [100] last-track info as (ctrladr<<24)|(track<<16)|...; [101]
 |   lead-out as (ctrladr<<24)|fad. ctrladr's high nibble is the control field:
 |   bit 2 set = data track, clear = audio; 0x0f marks the entry absent. FAD is
 |   1/75s frames. toc_raw lazily fetches and caches; toc_ctrl/toc_fad/toc_is_audio
 |   decode one longword; toc_track_no reads a first/last record's track number (0
 |   when the TOC reads bogus -- no disc, or a read before the drive was ready).
 | Author: suinevere
 ----------------------*/
#define TOC_WORDS       102
#define TOC_FIRST_WORD  99
#define TOC_LAST_WORD   100
#define TOC_LEADOUT     101

static uint32_t g_toc[TOC_WORDS];
static int      g_toc_ready = 0;

static const uint32_t* toc_raw(void) {
    if (!g_toc_ready) { CDC_TgetToc(g_toc); g_toc_ready = 1; }
    return g_toc;
}
static int      toc_ctrl(uint32_t w)  { return (int)((w >> 28) & 0xfu); }
static uint32_t toc_fad(uint32_t w)   { return w & 0x00ffffffu; }
static int      toc_is_audio(uint32_t w) {
    int c = toc_ctrl(w);
    return (c != 0xf) && ((c & 0x4) == 0);
}
static int toc_track_no(int word) {
    int n = (int)((toc_raw()[word] >> 16) & 0xffu);
    return (n >= 1 && n <= 99) ? n : 0;
}

/*----------------------
 | music_cdda_play_mode
 | Description: The engine's backend play callback. loop=1 uses the CD block's
 |   native repeat (seamless, endless); loop=0 plays the track once so music_tick
 |   can detect completion via music_cdda_is_playing and decide what comes next. A
 |   track of 0 or a muted level stops output instead. Note that "play it N times"
 |   is deliberately NOT expressed here: the CD block's own repeat-count field was
 |   tried for that and did not produce N passes on hardware, so the pass counting
 |   lives in music.c where it can be driven by the one signal this backend is known
 |   to report accurately -- a one-shot track ending.
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda)
 | Globals: g_track, g_loop, g_level
 | Params: track -- CD track number (<=0 stops); loop -- nonzero to repeat forever
 | Returns: N/A
 ----------------------*/
extern "C" void music_cdda_play_mode(int track, int loop) {
    g_track = track; g_loop = loop;
    if (track <= 0 || g_level == 0) {
        SRL::Sound::Cdda::StopPause();
        // A track asked for while muted is owed a start once the level comes back;
        // a track of 0 is not owed anything.
        g_vol_stopped = (track > 0);
        return;
    }
    SRL::Sound::Cdda::SetVolume((uint8_t) out_level());
    SRL::Sound::Cdda::PlaySingle((uint16_t) track, loop != 0);
    g_vol_stopped = 0;
}

/*----------------------
 | music_set_volume
 | Description: Sets the CD-DA level (clamped 0..7). Level 0 stops output outright;
 |   coming back off 0 reissues the last requested track, because the drive was
 |   stopped rather than merely quiet and volume alone will not restart it. The
 |   restart lives here rather than in music_set_level so that every caller gets it
 |   -- the Sound page drives the slider through this one live.
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda)
 | Globals: g_level, g_track, g_loop, g_vol_stopped
 | Params: level -- requested level, clamped to 0..7
 | Returns: N/A
 ----------------------*/
extern "C" void music_set_volume(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    if (level == 0) {
        SRL::Sound::Cdda::StopPause();
        g_vol_stopped = 1;
        return;
    }
    SRL::Sound::Cdda::SetVolume((uint8_t) out_level());
    if (g_vol_stopped && g_track > 0) {
        SRL::Sound::Cdda::PlaySingle((uint16_t) g_track, g_loop != 0);
        g_vol_stopped = 0;
    }
}

/*----------------------
 | music_set_level
 | Description: The committing form of music_set_volume, and the one place the
 |   engine is told whether music is on at all.
 |
 |   The two are separate calls because they take different kinds of level:
 |   music_set_volume also carries the fade ramp, which walks to 1 and back many
 |   times a session without the player having turned anything off, so hooking
 |   audibility there would switch the engine off on every transition. This name
 |   is only ever called with the player's own setting -- at boot, at game start,
 |   and on either way out of the Options sound page -- which is exactly the
 |   granularity music_set_audible wants.
 | Author: suinevere
 | Dependencies: SRL (via music_set_volume), music.h (music_set_audible)
 | Globals: g_level, g_track, g_loop, g_vol_stopped
 | Params: level -- requested level
 | Returns: N/A
 ----------------------*/
extern "C" void music_set_level(int level) {
    music_set_volume(level);
    music_set_audible(level > 0);
}

/*----------------------
 | music_cdda_is_playing
 | Description: The engine's is_playing callback, read from the CD block status
 |   register. A repeating track stays in CDC_ST_PLAY forever; a one-shot track
 |   leaves CDC_ST_PLAY when it ends -- the loop-end signal music_tick relies on,
 |   and the only drive-state edge here that has proved trustworthy.
 | Author: suinevere
 | Dependencies: SRL (CDC status)
 | Globals: N/A
 | Params: N/A
 | Returns: 1 while a CD-DA track is playing, else 0
 ----------------------*/
extern "C" int music_cdda_is_playing(void) {
    CdcStat stat;
    CDC_GetCurStat(&stat);
    return (CDC_GET_STC(&stat) == CDC_ST_PLAY) ? 1 : 0;
}

/*----------------------
 | toc_track_end
 | Description: The first frame past the end of a track -- the next track's start,
 |   or the lead-out for the last one. Read from the hand-decoded TOC above rather
 |   than SRL::Cd::TableOfContents, which cannot be trusted for this (see the TOC
 |   section box); that is also why pause/resume below is written here instead of
 |   using SRL::Sound::Cdda::Resume, which derives its end address from that same
 |   broken table.
 | Author: suinevere
 | Dependencies: SRL (via the TOC helpers)
 | Globals: g_toc (via toc_raw)
 | Params: track -- CD track number (1..99)
 | Returns: the end frame address, or 0 when the TOC cannot answer
 ----------------------*/
static uint32_t toc_track_start(int track) {
    if (track < 1 || track > 99) return 0;
    return toc_fad(toc_raw()[track - 1]);
}

static uint32_t toc_track_end(int track) {
    if (track < 1 || track > 99) return 0;
    const uint32_t* toc = toc_raw();
    int last = toc_track_no(TOC_LAST_WORD);
    if (last == 0) return 0;
    return (track >= last) ? toc_fad(toc[TOC_LEADOUT]) : toc_fad(toc[track]);
}

/*----------------------
 | g_pause_fad / music_cdda_pause / music_cdda_resume
 | Description: Stop the drive and later pick the same track up where it stopped.
 |   Pause reads the head's current frame out of the CD block status and then seeks,
 |   which is what actually silences the output. Resume plays from that frame to the
 |   end of the track, one-shot -- the remainder of the interrupted pass, not a
 |   fresh one -- so what the listener hears is the track continuing rather than
 |   restarting. When that remainder ends, it reads to the engine as an ordinary
 |   loop-end and the pass counting carries on from there, which is why music.c
 |   needs an Override branch in music_tick: Override otherwise never sees one.
 |   Falls back to replaying the whole track whenever the saved frame or the TOC
 |   cannot be trusted, since restarting is a far better failure than silence.
 | Author: suinevere
 | Dependencies: SRL (CDC status and play calls)
 | Globals: g_pause_fad, g_track, g_loop, g_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static uint32_t g_pause_fad = 0;

/*----------------------
 | music_cdda_duck / music_cdda_unduck
 | Description: The quiet-hold pair. Where pause seeks the head away and silences
 |   the output, these only attenuate it, to out_level (see the state box above).
 |   Duck sets the flag before writing, so every later track the engine issues
 |   under the hold comes up quiet too; unduck clears it first, for the same
 |   reason in reverse.
 |
 |   Neither touches g_pause_fad, so a duck cannot land the drive somewhere else
 |   on the disc the way a mis-saved frame can, and there is no seek to restart
 |   from. A player who has muted the music (g_level 0) stays muted -- but unduck
 |   still clears the flag on that path, so a later unmute is not left ducked.
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda)
 | Globals: g_level, g_track, g_ducked
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void music_cdda_duck(void) {
    if (g_track <= 0 || g_level == 0) return;
    g_ducked = 1;
    SRL::Sound::Cdda::SetVolume((uint8_t) out_level());
}

extern "C" void music_cdda_unduck(void) {
    g_ducked = 0;
    if (g_track <= 0 || g_level == 0) return;
    SRL::Sound::Cdda::SetVolume((uint8_t) g_level);
}

extern "C" void music_cdda_pause(void) {
    // A stop supersedes a duck -- the engine upgrades one into the other, and the
    // lift that follows is music_cdda_resume, not unduck. Clearing here is what
    // stops the drive coming back quiet with nothing left to raise it.
    g_ducked = 0;
    CdcStat stat;
    CDC_GetCurStat(&stat);
    g_pause_fad = (uint32_t) CDC_STAT_FAD(&stat);

    CdcPos pos;
    CDC_POS_PTYPE(&pos) = CDC_PTYPE_DFL;
    CDC_CdSeek(&pos);
}

extern "C" void music_cdda_resume(void) {
    if (g_track <= 0 || g_level == 0) { SRL::Sound::Cdda::StopPause(); return; }

    // The saved frame has to land inside the track it was saved from. It will not
    // if the status register was read between tracks, or was stale from an earlier
    // one -- in which case resuming there would drop the listener somewhere else on
    // the disc entirely.
    uint32_t start = toc_track_start(g_track);
    uint32_t end   = toc_track_end(g_track);
    if (end == 0 || g_pause_fad < start || g_pause_fad >= end) {
        music_cdda_play_mode(g_track, g_loop);
        return;
    }

    SRL::Sound::Cdda::SetVolume((uint8_t) out_level());
    CdcPly ply;
    CDC_PLY_STYPE(&ply) = CDC_PTYPE_FAD;
    CDC_PLY_SFAD(&ply)  = g_pause_fad;
    CDC_PLY_ETYPE(&ply) = CDC_PTYPE_FAD;
    CDC_PLY_EFAS(&ply)  = end - g_pause_fad;
    CDC_PLY_PMODE(&ply) = CDC_PM_DFL;
    CDC_CdPlay(&ply);
}

/*----------------------
 | music_cdda_is_short
 | Description: The engine's is_short callback: true when the track runs under
 |   MUSIC_SHORT_SECONDS, measured from the TOC frame delta (this track's start to
 |   the next track's start; the last track measures against the lead-out). A
 |   non-positive delta is treated as long, and an unreadable TOC treats every
 |   track as long. Cached per CD track, since the TOC is static for the disc.
 | Author: suinevere
 | Dependencies: SRL (via the TOC helpers)
 | Globals: g_toc (via toc_raw)
 | Params: track -- CD track number (1..99)
 | Returns: 1 if short, 0 if long or out of range
 ----------------------*/
extern "C" int music_cdda_is_short(int track) {
    static signed char cache[100];
    static int inited = 0;
    if (!inited) { for (int i = 0; i < 100; i++) cache[i] = 0; inited = 1; }
    if (track < 1 || track > 99) return 0;
    if (cache[track]) return cache[track] == 1;
    uint32_t end = toc_track_end(track);
    if (end == 0) return 0;
    uint32_t start = toc_fad(toc_raw()[track - 1]);
    int frames = (int)(end - start);
    int is_short = (frames > 0 && frames < MUSIC_SHORT_SECONDS * 75) ? 1 : 0;
    cache[track] = is_short ? 1 : 2;
    return is_short;
}

/*----------------------
 | music_cdda_audio_tracks
 | Description: Returns the disc's CD-DA track numbers in order -- real CD track
 |   numbers, the same ones PlaySingle and the .cue use. Walks only the tracks the
 |   TOC says exist (first-track record .. last-track record) so absent slots and
 |   the longwords past the end of the BIOS TOC are never consulted. On our discs
 |   track 1 is data and the music is 2..N, so this comes back as {2,3,...,N}.
 |   Cached; the TOC is static.
 | Author: suinevere
 | Dependencies: SRL (via the TOC helpers)
 | Globals: g_toc (via toc_raw)
 | Params: out -- receives a pointer to the internal list (or 0 when none)
 | Returns: the number of audio tracks (0 for a data-only or unreadable disc)
 ----------------------*/
extern "C" int music_cdda_audio_tracks(const unsigned char** out) {
    static unsigned char list[99];
    static int n = -1;
    if (n < 0) {
        const uint32_t* toc = toc_raw();
        int first = toc_track_no(TOC_FIRST_WORD), last = toc_track_no(TOC_LAST_WORD);
        n = 0;
        if (first != 0 && last != 0 && first <= last)
            for (int t = first; t <= last && n < 99; t++)
                if (toc_is_audio(toc[t - 1])) list[n++] = (unsigned char) t;
    }
    if (out) *out = (n > 0) ? list : 0;
    return n;
}

/*----------------------
 | music_cdda_has_audio
 | Description: True when the disc carries any CD-DA audio; gates the audio rows
 |   in Sound Options.
 | Author: suinevere
 ----------------------*/
extern "C" int music_cdda_has_audio(void) {
    return music_cdda_audio_tracks(0) > 0 ? 1 : 0;
}

/*----------------------
 | music_cdda_current_track
 | Description: The track the CD block was last asked to play (0 = none). Sound
 |   Options opens its Track row on this so it shows what is actually sounding
 |   rather than the saved selection.
 | Author: suinevere
 ----------------------*/
extern "C" int music_cdda_current_track(void) { return g_track; }
