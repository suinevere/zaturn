/*----------------------
 | synth_target.cxx
 | Description: Binds the synth to real hardware and drives it from V-blank.
 |   Two addresses matter: the SCSP register window at 0x25B00000 and the
 |   waveform area at 0x25A70000, high in sound RAM and clear of the region the
 |   SGL sound driver allocates from the bottom. SCSP_WAVE_BYTES of the 64 KB
 |   above that address are used -- 5 KB once the percussion waveform is counted
 |   -- out of the 512 KB the chip has.
 | Author: suinevere
 | Dependencies: srl.hpp, synth.h, synth_target.h
 ----------------------*/
#include <srl.hpp>

extern "C" {
#include "synth.h"
#include "scsp.h"
#include "synth_target.h"
}

/*----------------------
 | SYNTH_SCSP_REGS / SYNTH_WAVE_RAM / SYNTH_WAVE_SA
 | Description: The SCSP register file, the sound-RAM address the waveforms are
 |   copied to, and that same address as the SCSP sees it -- sound RAM is
 |   0x25A00000 to the SH-2 and 0 to the chip, so the SA field carries the
 |   offset alone.
 | Author: suinevere
 ----------------------*/
#define SYNTH_SCSP_REGS ((volatile unsigned short*) 0x25B00000)
#define SYNTH_WAVE_RAM  ((volatile signed char*)   0x25A70000)
#define SYNTH_WAVE_SA   0x70000UL

/*----------------------
 | g_synth_subscribed
 | Description: Whether the V-blank handler is already registered, so a second
 |   synth_target_init does not stack a second tick per frame and double the
 |   tempo.
 | Author: suinevere
 ----------------------*/
static bool g_synth_subscribed = false;

/*----------------------
 | synth_target_vblank
 | Description: One tick per frame. Runs in interrupt context, which is the
 |   point: the netbin blocks on modem reads and the CD build blocks on disc
 |   reads, and a tick driven from either main loop would lose tempo every time
 |   one of those happened.
 | Author: suinevere
 | Dependencies: synth.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void synth_target_vblank(void) {
    synth_tick();
}

void synth_target_init(void) {
    /* The netbin never turns the sound block on -- SRL only does that when the
       SGL driver is enabled, and this build disables it (srl_core.hpp:107).
       PlanetWeb leaves the block in whatever state its own audio finished in,
       so put it into a known one rather than inheriting it. In the CD build the
       driver is already up and this is left alone. */
    /* The sound block is turned ON, with the 68K parked, and that is not a
       detail. This used to leave it OFF -- SNDOFF, then set the master volume,
       then drive the slots from the SH-2 -- and measured on hardware from a
       netbin that state writes and reads sound RAM perfectly, 256 of 256 with
       the right byte order, and produces no audio on any of the thirty-two
       slots. The registers take the writes and the chip does not sound. Every
       emulator run agreed with the old version, because Mednafen generates
       audio whatever the block state is.

       SNDON needs the 68K to have somewhere harmless to be: with no driver
       loaded it would otherwise run whatever PlanetWeb left in sound RAM. Its
       reset vectors are the first eight bytes of that RAM -- stack pointer,
       then entry address -- and at the entry a single instruction, 0x60FE,
       `bra.s` to itself: two bytes that spin forever and touch nothing. */
#ifdef NETBIN
    {
        volatile unsigned short *sram = (volatile unsigned short *) 0x25A00000;
        slSoundOffWait();
        sram[0x000 / 2] = 0x0000;
        sram[0x002 / 2] = 0x0FFC;
        sram[0x004 / 2] = 0x0000;
        sram[0x006 / 2] = 0x0100;
        sram[0x100 / 2] = 0x60FE;
        slSoundOnWait();
    }
#endif

    synth_bind(SYNTH_SCSP_REGS, SYNTH_WAVE_RAM, SYNTH_WAVE_SA);

    /* Only where no driver runs. A program taking the chip over should not
       inherit its state on trust -- though measured over NetLink, PlanetWeb
       hands it over with no slot keyed, so this has never had anything to
       clear. The CD build must not do it: the driver owns the other slots
       there, and clearing them would stop the CD-DA and the effects. */
#ifdef NETBIN
    scsp_silence_all();
#endif

    synth_init();

    /* Only where no sound driver runs. The master volume is the machine's, not
       the music's, and the SGL driver sets it during SND_Init -- which is why
       the CD build's splash jingle is audible without anyone here touching it.
       The netbin has no driver, so nothing sets it at all, and every slot plays
       into a muted output: correct registers, total silence. Measured, not
       guessed -- a recording of this build was flat until this call existed. */
#ifdef NETBIN
    scsp_enable_output();
#endif

    if (!g_synth_subscribed) {
        SRL::Core::OnVblank += synth_target_vblank;
        g_synth_subscribed = true;
    }
}
