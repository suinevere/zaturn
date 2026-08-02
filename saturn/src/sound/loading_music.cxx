/*----------------------
 | loading_music.cxx
 | Description: Loads and plays the post-selection loading screen's PCM
 |   cue. See loading_music.h. Mirrors boot_music.cxx's load/fade/play/stop
 |   shape exactly -- same 8-bit signed mono raw format, same master-volume
 |   fade-out mechanism -- because that module already worked out the
 |   hardware traps involved. tools/assets/pvms.bat converts the source
 |   .ogg to this format with sox and writes it to cd/data/MSC/LOADCD.PCM.
 | Author: suinevere
 | Dependencies: loading_music.h, title.h (cd_enter_root), msc_dir.h
 |   (cd_enter_msc), SRL (Cd::File, Sound::Pcm, Memory::LowWorkRam)
 ----------------------*/
#include "loading_music.h"
#include "title.h"
#include "msc_dir.h"
#include <srl.hpp>

/*----------------------
 | LOADING_MUSIC_FILE / LOADING_MUSIC_RATE
 | Description: The loading cue's filename in /MSC and its 8-bit mono sample rate.
 | Author: suinevere
 ----------------------*/
#define LOADING_MUSIC_FILE "LOADCD.PCM"
#define LOADING_MUSIC_RATE 22050

/*----------------------
 | LOADING_MUSIC_MAX_SECONDS / LOADING_MUSIC_MAX_BYTES
 | Description: How much of LOADCD.PCM is ever read into Low Work RAM. The
 |   shipped file is the whole source .ogg, ~29 seconds (627.5 KB at this rate
 |   and depth), but the loading screen only runs about six or seven seconds --
 |   a fade in, one typed block, a fade out -- so the tail is never heard. It
 |   also cannot be afforded: by the time this module allocates, the typeahead
 |   trie is resident and title.cxx's art cache has taken however many slots it
 |   has needed so far (see the TGA_CACHE_SLOTS box there, which reserves
 |   TGA_CACHE_FLOOR of the zone for exactly this and the save scratch). A
 |   627.5 KB Malloc there fails, and every other loading_music_* call
 |   then no-ops on the null buffer -- the cue would go silently missing on a
 |   stock disc. Eight seconds is the ceiling: comfortably longer than the
 |   screen, 172.3 KB, and a proportionally shorter CD read before the fade-in
 |   starts. A shorter file than the cap is used whole.
 | Author: suinevere
 ----------------------*/
#define LOADING_MUSIC_MAX_SECONDS 8u
#define LOADING_MUSIC_MAX_BYTES   (LOADING_MUSIC_MAX_SECONDS * (uint32_t) LOADING_MUSIC_RATE)

/*----------------------
 | why the loop seam is not smoothed
 | Description: LOADING_MUSIC_MAX_BYTES cuts a 29-second track at eight seconds,
 |   so the loop returns from a mid-phrase waveform to the sample's opening
 |   level -- a discontinuity, and audible as a click. A short ramp baked into
 |   the tail was tried and taken back out: multiplying 8-bit samples by a small
 |   gain quantises them to a couple of LSBs, which is a hiss, and trading a
 |   click once every seven seconds for a fifth of a second of hiss every seven
 |   seconds is not a trade worth making. Any future attempt at this must not
 |   scale the samples -- trimming the loop's start and end to the nearest zero
 |   crossings costs nothing and adds no noise, and is the thing to try.
 | Author: suinevere
 ----------------------*/

/*----------------------
 | LoadingMusicPcm
 | Description: A pre-loaded 8-bit mono PCM sample handed to SRL's PCM
 |   machinery. IPcmFile's fields are protected, so this subclass fills
 |   them via set(); the sample buffer itself is owned by
 |   g_loading_music_buf, not by this object (mirrors boot_music.cxx's
 |   BootMusicPcm).
 | Author: suinevere
 ----------------------*/
class LoadingMusicPcm : public SRL::Sound::Pcm::IPcmFile {
public:
    void set(int8_t* d, uint32_t n, uint16_t r) {
        data = d; dataSize = n; mode = _Mono; depth = _PCM8Bit; sampleRate = r;
    }
};

/*----------------------
 | g_loading_music_buf / g_loading_music_size / g_loading_music_pcm
 | Description: The Low Work RAM sample buffer, its padded play length, and the
 |   IPcmFile object that points at them.
 | Author: suinevere
 ----------------------*/
static int8_t*         g_loading_music_buf     = nullptr;
static uint32_t        g_loading_music_size    = 0;
static LoadingMusicPcm  g_loading_music_pcm;

/*----------------------
 | g_loading_music_channel
 | Description: The playing PCM channel, or -1; volatile because it is the
 |   arm/disarm flag the V-blank handler reads and both sides write.
 | Author: suinevere
 ----------------------*/
static volatile int     g_loading_music_channel = -1;

/*----------------------
 | g_loading_music_head
 | Description: How many bytes at the front of the buffer loading_music_fade_in
 |   scaled down, and therefore where a repeat has to start from. The fade is
 |   baked into the sample rather than applied live (see loading_music.h), so it
 |   is still there on the second pass -- looping from byte 0 would duck the cue
 |   every time round and read as a pumping tremolo rather than a loop. Zero
 |   until a fade is baked, so an unfaded sample simply repeats whole.
 | Author: suinevere
 ----------------------*/
static uint32_t         g_loading_music_head    = 0;

/*----------------------
 | LOADING_MUSIC_FPS / g_loading_music_frames / g_loading_music_span_frames
 | Description: How far into the current pass we are and how long that pass
 |   lasts, both counted in video fields, and together the loop's trigger.
 |
 |   Fields rather than a caller-driven count of "ticks", and rather than the
 |   wall clock, because the counting happens in the V-blank interrupt (see
 |   loading_music_vblank). There it is exact: the handler runs once per field
 |   by definition, so a field count is a frame count with nothing to drift
 |   against. The wall clock was the right answer only while the counting was
 |   done by callers, where calls and frames are unrelated; reading SRL::Timer
 |   from inside an interrupt would additionally race the FRT overflow handler
 |   that maintains its high word.
 |
 |   Asking the driver whether the channel is still busy was tried before either
 |   and does not work here: the build that shipped it played the cue once and
 |   never came back round, which is what a channel that never reports itself
 |   busy looks like from outside. sound.cxx does not depend on that reading
 |   either -- its looping effects run off a computed duration and only consult
 |   the channel as a fallback -- so a duration is what this uses too.
 | Author: suinevere
 ----------------------*/
#define LOADING_MUSIC_FPS 60

static volatile int32_t g_loading_music_frames      = 0;
static volatile int32_t g_loading_music_span_frames = 0;

/*----------------------
 | loading_music_span_frames
 | Description: How many video fields `bytes` of 8-bit mono sample lasts.
 |   bytes is capped at LOADING_MUSIC_MAX_BYTES (176400), so the multiply stays
 |   well inside 32 bits.
 | Author: suinevere
 ----------------------*/
static int32_t loading_music_span_frames(uint32_t bytes) {
    return (int32_t) ((bytes * (uint32_t) LOADING_MUSIC_FPS) / (uint32_t) LOADING_MUSIC_RATE);
}

/*----------------------
 | g_dbg_loops / loading_music_debug
 | Description: How many times the cue has come round, for the DEBUG-only
 |   readout on the loading screen. One int and no branches, so it stays
 |   compiled in either way; only the printing is conditional.
 |
 |   It exists because this module cannot be observed any other way. It runs on
 |   hardware, its output is a sound, and "it plays once and stops" is equally
 |   consistent with the deadline never coming due, the repeat being issued and
 |   rejected, and the repeat sounding on a channel nothing can hear. Those need
 |   telling apart by looking, not by reasoning about which is likelier -- and
 |   it was this readout, sitting frozen at the same numbers for the several
 |   seconds it should have been counting through, that showed the loop was not
 |   being reached at all rather than being reached and refusing.
 | Author: suinevere
 | Params: loops/frames/span -- out, any may be NULL
 ----------------------*/
static volatile int g_dbg_loops = 0;

extern "C" void loading_music_debug(int *loops, int *frames, int *span) {
    if (loops)  *loops  = g_dbg_loops;
    if (frames) *frames = (int) g_loading_music_frames;
    if (span)   *span   = (int) g_loading_music_span_frames;
}

/*----------------------
 | loading_music_hook
 | Description: Forward declaration; defined next to the V-blank handler it
 |   subscribes, but called from loading_music_load above.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void loading_music_hook(void);

/*----------------------
 | loading_music_cd_restore
 | Description: Puts the CD directory back where the caller needs it, which is
 |   /Z3 and not the root. This function is the whole reason the loading screen
 |   used to hang the game: it runs immediately after game_select() and
 |   immediately before main()'s story-file open, and that open is a bare
 |   SRL::Cd::File("XXX.Z3") that resolves against whatever directory is
 |   current. Leaving the CD at the root -- which is what the /MSC lookup above
 |   needs and what boot_music.cxx, the file this module was modelled on,
 |   correctly restores to -- meant the story was never found, all 300 retries
 |   missed, and the player watched a black screen for forty seconds before
 |   saturn_die finally said so. boot_music.cxx gets away with the root restore
 |   because it runs during the splash, before /Z3 has been captured and with
 |   the catalogue scan still to come to set it; nothing re-establishes it here.
 |   The sibling .BLB that sound_init opens next would have missed the same way.
 | Author: suinevere
 | Dependencies: title.h (cd_restore_z3)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void loading_music_cd_restore(void) {
    cd_restore_z3();
}

/*----------------------
 | loading_music_load
 | Description: See loading_music.h.
 | Author: suinevere
 | Dependencies: title.h, msc_dir.h, SRL (Cd::File, Memory::LowWorkRam)
 | Globals: g_loading_music_buf, g_loading_music_size, g_loading_music_head,
 |   g_loading_music_pcm
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void loading_music_load(void) {
    if (g_loading_music_buf) return;

    cd_enter_root();
    if (!cd_enter_msc()) { loading_music_cd_restore(); return; }

    SRL::Cd::File file(LOADING_MUSIC_FILE);
    if (!file.Exists()) { loading_music_cd_restore(); return; }

    uint32_t size = (uint32_t) file.Size.Bytes;
    if (size > LOADING_MUSIC_MAX_BYTES) size = LOADING_MUSIC_MAX_BYTES;
    uint32_t play = size < 0x900 ? 0x900 : size;   // slPCMOn's minimum
    int8_t* buf = (int8_t *) SRL::Memory::LowWorkRam::Malloc(play);
    if (!buf) { loading_music_cd_restore(); return; }

    int32_t got = file.LoadBytes(0, (int32_t) size, (uint8_t *) buf);
    loading_music_cd_restore();
    if (got != (int32_t) size) {
        SRL::Memory::Free(buf);
        return;
    }
    for (uint32_t i = size; i < play; i++) buf[i] = 0;

    g_loading_music_buf  = buf;
    g_loading_music_size = play;
    g_loading_music_head = 0;
    g_loading_music_pcm.set(buf, play, LOADING_MUSIC_RATE);
    loading_music_hook();   // inert until loading_music_play arms it
}

/*----------------------
 | LOADING_FADE_STEPS
 | Description: How many equal gain steps the fade-in is cut into.
 | Author: suinevere
 ----------------------*/
#define LOADING_FADE_STEPS 256

/*----------------------
 | loading_music_fade_in
 | Description: See loading_music.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_loading_music_buf, g_loading_music_size, g_loading_music_head
 | Params: frames -- ramp length in vblank fields
 | Returns: N/A
 ----------------------*/
extern "C" void loading_music_fade_in(int frames) {
    if (!g_loading_music_buf || frames <= 0) return;

    uint32_t n = (uint32_t) frames * (LOADING_MUSIC_RATE / 60);
    if (n > g_loading_music_size) n = g_loading_music_size;

    uint32_t seg = n / LOADING_FADE_STEPS;
    if (seg == 0) return;   // ramp too short to segment; leave it at full

    for (uint32_t s = 0; s < LOADING_FADE_STEPS; s++) {
        int32_t  gain = (int32_t) s;
        uint32_t end  = (s + 1) * seg;
        for (uint32_t i = s * seg; i < end; i++)
            g_loading_music_buf[i] = (int8_t) (((int32_t) g_loading_music_buf[i] * gain) >> 8);
    }

    g_loading_music_head = LOADING_FADE_STEPS * seg;   // where a repeat starts
}

/*----------------------
 | loading_music_play
 | Description: See loading_music.h.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm)
 | Globals: g_loading_music_buf, g_loading_music_pcm, g_loading_music_channel
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void loading_music_play(void) {
    if (!g_loading_music_buf || g_loading_music_channel >= 0) return;
    int8_t ch = g_loading_music_pcm.Play(LOADING_MUSIC_LEVEL_MAX);
    if (ch < 0) return;

    // Counters before the channel, and the channel last: the channel number is
    // what arms the V-blank handler, so everything it reads has to be true
    // before it can run. The reverse order leaves a field in which the handler
    // sees a live channel against a stale span.
    g_loading_music_frames      = 0;
    g_loading_music_span_frames = loading_music_span_frames(g_loading_music_size);
    g_loading_music_channel     = ch;
}

/*----------------------
 | loading_music_vblank
 | Description: The loop, run from the V-blank interrupt. The SCSP has no repeat
 |   flag reachable through SRL's PCM path -- slPCMOn is fire-and-forget and the
 |   channel simply goes idle at the end of the buffer -- so a repeat has to be
 |   issued by somebody, and this is where. The repeat skips the faded-in head
 |   (see g_loading_music_head) and is re-set on the handle each time, because
 |   that changes the buffer the handle points at.
 |
 |   From the interrupt and not from a caller, which is the whole point and was
 |   the bug in the two builds before this one. Servicing the cue from call sites
 |   means the cue is only serviced where somebody remembered to put a call, and
 |   the loading screen is made almost entirely of blocking work with no calls
 |   inside it: the story read, mojo_boot, sound_init's walk through the Blorb
 |   index. Every one of those is seconds long, and a pass that ran out inside
 |   one stayed silent until whatever came after it. That is what "it plays once
 |   and stops" was -- not a loop that would not fire, a loop nothing asked to
 |   fire. The interrupt runs whatever the main line is blocked on, so there is
 |   no such gap to leave.
 |
 |   Safe to do the PCM calls here: SGL services its own PCM streaming from this
 |   same interrupt, so a restart issued here cannot land in the middle of one,
 |   which is more than can be said for issuing it from the main line. Everything
 |   this touches is either volatile or written by the main line only while the
 |   handler is disarmed (g_loading_music_channel < 0), and the SH-2 can only
 |   interrupt between instructions, so arming last and disarming first is enough
 |   -- no masking needed.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm)
 | Globals: g_loading_music_buf, g_loading_music_size, g_loading_music_head,
 |   g_loading_music_pcm, g_loading_music_channel, g_loading_music_frames
 ----------------------*/
static void loading_music_vblank(void) {
    if (!g_loading_music_buf || g_loading_music_channel < 0) return;
    if (g_loading_music_span_frames <= 0) return;

    int32_t f = g_loading_music_frames + 1;
    g_loading_music_frames = f;
    if (f < g_loading_music_span_frames) return;

    // Stop on the field the pass expires and start on the next one, rather than
    // both in one go. Play() takes the first channel slPCMStat calls free, and
    // slPCMOff is a command to the sound driver rather than something that has
    // taken effect by the time it returns -- so a same-field restart can find
    // its own channel still busy, land on a different one, and walk through all
    // four. One field of silence at the seam is the price, and against a loop
    // seam that is already a waveform discontinuity it is not the audible part.
    if (f == g_loading_music_span_frames) {
        SRL::Sound::Pcm::StopSound((uint8_t) g_loading_music_channel);
        return;
    }

    uint32_t from = g_loading_music_head;
    if (from >= g_loading_music_size) from = 0;          // nothing left past the fade
    uint32_t len  = g_loading_music_size - from;
    if (len < 0x900) { from = 0; len = g_loading_music_size; }   // slPCMOn's minimum

    g_loading_music_pcm.set(g_loading_music_buf + from, len, LOADING_MUSIC_RATE);

    // Only adopt a channel we actually got, but restart the count either way: a
    // failed repeat that left the deadline behind would retry on every field
    // from then on, hammering the driver for the rest of the load.
    int8_t ch = g_loading_music_pcm.Play(LOADING_MUSIC_LEVEL_MAX);
    if (ch >= 0) g_loading_music_channel = ch;
    g_loading_music_frames      = 0;
    g_loading_music_span_frames = loading_music_span_frames(len);
    g_dbg_loops = g_dbg_loops + 1;   // not ++: deprecated on a volatile in C++20
}

/*----------------------
 | g_loading_music_hooked / loading_music_hook
 | Description: Subscribes loading_music_vblank to SRL's V-blank event, once per
 |   session. Never unsubscribed: the handler's own first line stands down when
 |   there is nothing playing, which costs a null check per field, and removing a
 |   callback means mutating the std::vector the interrupt is iterating. A guard
 |   flag rather than an unconditional +=, because a second game (a soft reset,
 |   or switching stories) calls loading_music_load again and two copies in the
 |   list would count every field twice and halve the loop period.
 | Author: suinevere
 | Dependencies: SRL (Core::OnVblank)
 ----------------------*/
static bool g_loading_music_hooked = false;

static void loading_music_hook(void) {
    if (g_loading_music_hooked) return;
    SRL::Core::OnVblank += loading_music_vblank;
    g_loading_music_hooked = true;
}

/*----------------------
 | LOADING_MASTER_MAX / LOADING_MASTER_NUDGE / loading_master_restore
 | Description: The sound driver's total-volume scale and how to put it back,
 |   carried over from boot_music.cxx unchanged -- read the BOOT_MASTER_MAX /
 |   BOOT_MASTER_NUDGE box there, and the fade box in boot_music.h, for why
 |   these two numbers are what they are.
 |
 |   The two consecutive SND_SetTlVl writes are deliberate and are the whole
 |   point of this function. The driver takes commands on its own schedule and
 |   a restore issued right after a volume ramp's last step is evidently
 |   droppable: the build that ended in a single write left the entire machine
 |   silent afterwards, the one that wrote twice did not. Do not tidy the pair
 |   into one call. It looks redundant and is not, and what it guards against
 |   is not a quiet loading cue but no sound anywhere for the rest of the
 |   session -- including the menu's CD-DA track and the game's own audio.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define LOADING_MASTER_MAX   15
#define LOADING_MASTER_NUDGE 7

static void loading_master_restore(void) {
    SND_SetTlVl((SndTlVl) LOADING_MASTER_NUDGE);
    SND_SetTlVl((SndTlVl) LOADING_MASTER_MAX);
}

/*----------------------
 | loading_music_set_level
 | Description: See loading_music.h.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl)
 | Globals: N/A
 | Params: level -- 0..LOADING_MUSIC_LEVEL_MAX, clamped
 | Returns: N/A
 ----------------------*/
extern "C" void loading_music_set_level(int level) {
    if (level < 0) level = 0;
    if (level > LOADING_MUSIC_LEVEL_MAX) level = LOADING_MUSIC_LEVEL_MAX;
    SND_SetTlVl((SndTlVl) ((LOADING_MASTER_MAX * level) / LOADING_MUSIC_LEVEL_MAX));
}

/*----------------------
 | loading_music_stop
 | Description: See loading_music.h.
 | Author: suinevere
 | Dependencies: SGL, SRL (Memory::LowWorkRam)
 | Globals: g_loading_music_channel, g_loading_music_buf
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void loading_music_stop(void) {
    loading_master_restore();

    // Disarm before touching anything else, in the opposite order to
    // loading_music_play: clearing the channel is what stands the V-blank
    // handler down, and it has to be standing down before the buffer it plays
    // from is freed underneath it.
    int ch = g_loading_music_channel;
    g_loading_music_channel     = -1;
    g_loading_music_span_frames = 0;

    if (ch >= 0) SRL::Sound::Pcm::StopSound((uint8_t) ch);
    if (g_loading_music_buf) {
        SRL::Memory::Free(g_loading_music_buf);
        g_loading_music_buf = nullptr;
    }
}
