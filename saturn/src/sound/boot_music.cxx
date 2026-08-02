/*----------------------
 | boot_music.cxx
 | Description: Loads and plays the boot splash's background jingle. See
 |   boot_music.h. The sample is 8-bit signed mono raw PCM at BOOT_MUSIC_RATE
 |   Hz (no WAV/RIFF header) -- tools/assets/music.bat converts the source
 |   .ogg to this format with sox and writes it to cd/data/MSC/SPLASH.PCM.
 | Author: suinevere
 | Dependencies: boot_music.h, title.h (cd_enter_root), SRL (Cd::File,
 |   Sound::Pcm, Memory::LowWorkRam)
 ----------------------*/
#include "boot_music.h"
#include "title.h"
#include "msc_dir.h"
#include <srl.hpp>

/*----------------------
 | BOOT_MUSIC_FILE / BOOT_MUSIC_RATE
 | Description: The sample's filename inside /MSC and its sample rate. Must
 |   match the sox conversion in tools/assets/music.bat exactly, since a raw
 |   PCM file carries no header to read either back from.
 | Author: suinevere
 ----------------------*/
#define BOOT_MUSIC_FILE "SPLASH.PCM"
#define BOOT_MUSIC_RATE 22050

/*----------------------
 | BOOT_MUSIC_MAX_SECONDS / BOOT_MUSIC_MAX_BYTES
 | Description: How much of SPLASH.PCM is ever read into Low Work RAM, and the
 |   reason there is a cap at all.
 |
 |   The cap was originally sized against the background-art cache. The two used
 |   to be resident together -- the jingle was loaded first, deliberately, and
 |   freed last, with display_preload_images() running in between -- and
 |   uncapped, at 453 KB, the pair ran ~10 KB over the megabyte. tga_decode's
 |   free-space check then failed on the last picture in scan order, the preload
 |   ignored the miss the way it was written to, and that one picture read the
 |   disc every time it was selected. The Saturn plays CD-DA off the head it
 |   reads data with, so the read stopped the menu track, and music_tick read
 |   the stopped drive as loop-end and advanced -- a picture that changed the
 |   music, which is what sent anyone looking here.
 |
 |   That pairing is gone: nothing decodes art during the splash any more, and
 |   boot_music_stop frees this buffer before splash_show_once returns, so the
 |   first cache slot is taken after the jingle is already out of the zone. What
 |   the cap still guards is the overlap with the typeahead trie, which
 |   ensure_online_typeahead builds while the jingle is playing and which can
 |   want ~318 KB. Nineteen seconds is ~419 KB, so the pair sits comfortably
 |   under the megabyte with the art no longer in the argument.
 |
 |   It costs nothing audible either way: the splash is ten seconds of hold plus
 |   its two 90-frame ramps, and boot_music_stop scrubs the sample to silence
 |   and stops the channel at the end of it, so the tail past that was never
 |   reaching a speaker. A shorter file than the cap is used whole.
 | Author: suinevere
 ----------------------*/
#define BOOT_MUSIC_MAX_SECONDS 19u
#define BOOT_MUSIC_MAX_BYTES   (BOOT_MUSIC_MAX_SECONDS * (uint32_t) BOOT_MUSIC_RATE)

/*----------------------
 | BootMusicPcm
 | Description: A pre-loaded 8-bit mono PCM sample handed to SRL's PCM
 |   machinery. IPcmFile's fields are protected, so this subclass fills them
 |   via set(); the sample buffer itself is owned by g_boot_music_buf, not by
 |   this object (mirrors sound.cxx's SlicePcm).
 | Author: suinevere
 ----------------------*/
class BootMusicPcm : public SRL::Sound::Pcm::IPcmFile {
public:
    void set(int8_t* d, uint32_t n, uint16_t r) {
        data = d; dataSize = n; mode = _Mono; depth = _PCM8Bit; sampleRate = r;
    }
};

/*----------------------
 | g_boot_music_buf / g_boot_music_size / g_boot_music_pcm / g_boot_music_channel
 | Description: The Low Work RAM sample buffer (nullptr if not loaded) and how
 |   many samples it holds, the PCM handle wrapping it, and the channel it is
 |   currently playing on (-1 if none). The sample is 8-bit mono, so one sample
 |   is one byte and the size is both at once.
 | Author: suinevere
 ----------------------*/
static int8_t*     g_boot_music_buf     = nullptr;
static uint32_t    g_boot_music_size    = 0;
static BootMusicPcm g_boot_music_pcm;
static int          g_boot_music_channel = -1;

/*----------------------
 | boot_music_load
 | Description: See boot_music.h.
 | Author: suinevere
 | Dependencies: title.h (cd_enter_root), SRL (Cd::File, Memory::LowWorkRam)
 | Globals: g_boot_music_buf, g_boot_music_pcm
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_music_load(void) {
    if (g_boot_music_buf) return;   // already loaded (soft-reset re-entry)

    cd_enter_root();
    if (!cd_enter_msc()) { cd_enter_root(); return; }

    SRL::Cd::File file(BOOT_MUSIC_FILE);
    if (!file.Exists()) { cd_enter_root(); return; }

    uint32_t size = (uint32_t) file.Size.Bytes;
    if (size > BOOT_MUSIC_MAX_BYTES) size = BOOT_MUSIC_MAX_BYTES;
    uint32_t play = size < 0x900 ? 0x900 : size;   // slPCMOn's minimum
    int8_t* buf = (int8_t *) SRL::Memory::LowWorkRam::Malloc(play);
    if (!buf) { cd_enter_root(); return; }

    int32_t got = file.LoadBytes(0, (int32_t) size, (uint8_t *) buf);
    cd_enter_root();
    if (got != (int32_t) size) {
        SRL::Memory::Free(buf);
        return;
    }
    for (uint32_t i = size; i < play; i++) buf[i] = 0;

    g_boot_music_buf  = buf;
    g_boot_music_size = play;
    g_boot_music_pcm.set(buf, play, BOOT_MUSIC_RATE);
}

/*----------------------
 | boot_music_fade_in
 | Description: See boot_music.h. Scales the head of the sample in place with a
 |   rising ramp, in BOOT_FADE_STEPS constant-gain segments rather than a gain
 |   per sample -- a divide per sample over ~33k samples is real time on an SH-2,
 |   and 256 steps across a 1.5s ramp changes gain about every 6ms, far below
 |   anything a listener can pick out as stepping.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_boot_music_buf, g_boot_music_size
 | Params: frames -- ramp length in 60Hz frames
 | Returns: N/A
 ----------------------*/
#define BOOT_FADE_STEPS 256

extern "C" void boot_music_fade_in(int frames) {
    if (!g_boot_music_buf || frames <= 0) return;

    uint32_t n = (uint32_t) frames * (BOOT_MUSIC_RATE / 60);
    if (n > g_boot_music_size) n = g_boot_music_size;

    uint32_t seg = n / BOOT_FADE_STEPS;
    if (seg == 0) return;   // ramp too short to segment; leave it at full

    for (uint32_t s = 0; s < BOOT_FADE_STEPS; s++) {
        int32_t  gain = (int32_t) s;                  // s/256 of full
        uint32_t end  = (s + 1) * seg;
        for (uint32_t i = s * seg; i < end; i++)
            g_boot_music_buf[i] = (int8_t) (((int32_t) g_boot_music_buf[i] * gain) >> 8);
    }
}

/*----------------------
 | boot_music_play
 | Description: See boot_music.h. Opens the channel at full level, which is the
 |   one PCM call this codebase has ever heard come out of a speaker. Anything
 |   quieter here is a bet on the level reaching the SCSP a second time, and that
 |   bet has already cost two silent boots.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm)
 | Globals: g_boot_music_buf, g_boot_music_pcm, g_boot_music_channel
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_music_play(void) {
    if (!g_boot_music_buf || g_boot_music_channel >= 0) return;
    g_boot_music_channel = g_boot_music_pcm.Play(BOOT_MUSIC_LEVEL_MAX);
}

/*----------------------
 | BOOT_MASTER_MAX / BOOT_MASTER_NUDGE / boot_master_restore
 | Description: The sound driver's total-volume scale and how to put it back. Both
 |   numbers here are settled by listening, not by reading: sega_snd.h gives SndTlVl
 |   no range constant, the tree carries no SGL documentation, and nothing else in
 |   SaturnRingLib calls SND_SetTlVl. Three builds pinned it down. Starting the ramp
 |   at 15 gave a fade that sounded correct; starting it at 7 was already inaudible,
 |   which is what makes 15 the full value and the scale steeply logarithmic rather
 |   than linear -- 7 is not "about half", it is most of the way to nothing. Between
 |   those two builds the only other change was the restore, and the one that ended
 |   in a single write left the whole machine silent afterwards while the one that
 |   wrote twice did not. So the restore keeps its redundant write, exactly as it
 |   stands: this driver takes commands on its own schedule, a restore issued one
 |   frame after the ramp's last step is evidently droppable, and a second write
 |   costs nothing. Do not tidy the pair into one call -- that is the shape that was
 |   observed to work, and the failure it guards against is silence everywhere, not
 |   just here.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl)
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define BOOT_MASTER_MAX   15
#define BOOT_MASTER_NUDGE 7

static void boot_master_restore(void) {
    SND_SetTlVl((SndTlVl) BOOT_MASTER_NUDGE);
    SND_SetTlVl((SndTlVl) BOOT_MASTER_MAX);
}

/*----------------------
 | boot_music_set_level
 | Description: See boot_music.h. Goes through the sound driver's master volume
 |   rather than the PCM channel's own level, because touching the channel level
 |   after slPCMOn kills the stream outright (see the fade box in boot_music.h).
 |   The caller's 0..BOOT_MUSIC_LEVEL_MAX ramp is mapped onto the driver's much
 |   coarser scale, so a long ramp lands on only BOOT_MASTER_MAX+1 distinct volumes
 |   -- that is the hardware's granularity, not a rounding choice. The scale is also
 |   steeply logarithmic, so the drop from one step to the next is far from even:
 |   the top of the ramp barely moves and the bottom few steps do most of the
 |   audible work. Never the last word on the master volume: boot_music_stop puts it
 |   back.
 | Author: suinevere
 | Dependencies: SGL (SND_SetTlVl, via srl.hpp)
 | Globals: N/A
 | Params: level -- 0..BOOT_MUSIC_LEVEL_MAX, clamped
 | Returns: N/A
 ----------------------*/
extern "C" void boot_music_set_level(int level) {
    if (level < 0) level = 0;
    if (level > BOOT_MUSIC_LEVEL_MAX) level = BOOT_MUSIC_LEVEL_MAX;
    SND_SetTlVl((SndTlVl) ((BOOT_MASTER_MAX * level) / BOOT_MUSIC_LEVEL_MAX));
}

/*----------------------
 | BOOT_SCRUB_FRAMES / the silence scrub, and why stopping is not enough
 | Description: slPCMOff silences the channel; it does not empty what the sound
 |   driver has already staged. slPCMOn is SND_StartPcm underneath, and that call
 |   takes a streaming buffer in sound RAM -- SND_PRM_SADR/SND_PRM_SIZE with an
 |   SND_PRM_OFSET playback start (sega_snd.h), which SGL refills from work RAM as
 |   the SCSP drains it. Nothing in that API zeroes the buffer, so a stop leaves it
 |   holding whatever was last staged, and the next slPCMOn on the same stream
 |   number keys the SCSP back onto that region before the refill has caught up.
 |   The listener hears the tail of the previous sample under the start of the new
 |   one.
 |
 |   It reaches all the way to the loading screen because nothing in between clears
 |   it. The splash runs five seconds against a 16.6-second sample (two if the
 |   player skips), so this cut lands with some eleven seconds still staged, and the
 |   only thing that plays between here and LOADCD.PCM is CD-DA -- separate
 |   hardware, which never touches a PCM stream. The stale buffer survives the whole
 |   title menu intact.
 |
 |   So the buffer is emptied the only way this codebase can reach it: by zeroing
 |   the sample the driver is already streaming from and giving it frames to carry
 |   those zeros through. No new allocation, and no second slPCMOn to race the
 |   still-pending slPCMOff (see the same-field note in loading_music.cxx). The
 |   master volume stays where the fade-out left it throughout, and is restored
 |   afterwards, so the scrub itself is inaudible -- the reverse of the old order,
 |   which restored full volume while the stream was still live and the pending
 |   slPCMOff had not yet landed.
 |
 |   BOOT_SCRUB_FRAMES is one second, ~22 KB at this rate. SGL's own PCM buffer
 |   constants stop at PCM_SIZE_8K, so that clears any plausible stream buffer
 |   several times over; it is deliberately generous because a scrub that is too
 |   short does not fail loudly, it fails intermittently. It costs nothing visible:
 |   every caller reaches here with the screen already black.
 | Author: suinevere
 ----------------------*/
#define BOOT_SCRUB_FRAMES 60

/*----------------------
 | boot_music_stop
 | Description: See boot_music.h. Scrubs the driver's staged samples to silence
 |   (see the box above), stops the channel, then restores the driver's master
 |   volume -- unconditionally, whether or not anything was playing:
 |   boot_music_set_level turns down a global, not something scoped to this sample,
 |   and every exit from the splash comes through here, so leaving it down would
 |   take the menu's CD-DA track with it. The buffer is freed last, after the scrub
 |   has given the pending slPCMOff a full second to land; the driver was still
 |   streaming out of it when this function was entered.
 | Author: suinevere
 | Dependencies: SRL (Sound::Pcm, Core::Synchronize, Memory), SGL (SND_SetTlVl)
 | Globals: g_boot_music_buf, g_boot_music_size, g_boot_music_channel
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_music_stop(void) {
    if (g_boot_music_channel >= 0 && g_boot_music_buf) {
        for (uint32_t i = 0; i < g_boot_music_size; i++) g_boot_music_buf[i] = 0;
        for (int i = 0; i < BOOT_SCRUB_FRAMES; i++) SRL::Core::Synchronize();
    }

    if (g_boot_music_channel >= 0) {
        SRL::Sound::Pcm::StopSound((uint8_t) g_boot_music_channel);
        g_boot_music_channel = -1;
    }
    boot_master_restore();
    if (g_boot_music_buf) {
        SRL::Memory::Free(g_boot_music_buf);
        g_boot_music_buf = nullptr;
    }
}
