/*----------------------
 | main.cxx
 | Description: A disc that answers one question and nothing else: with the SGL
 |   sound driver running, which of the SCSP's thirty-two slots can this program
 |   key without the driver taking them back?
 |
 |   The question matters twice over. Lurking Horror's sound effects are 8 to 60
 |   KB each and there is no main RAM left to hold one, so they have to be played
 |   out of sound RAM by slots of our own -- and the synth already claims 28-31
 |   on the strength of a comment, which nothing has ever checked. The driver's
 |   own allocation lives in SDDRVS.TSK and BOOTSND.MAP, and that file's format
 |   is not decodable from the repository: two candidate readings of it both
 |   produce overlapping regions, so both are wrong.
 |
 |   So: measure. A one-second square wave is written straight into sound RAM,
 |   one slot at a time is keyed with it, and the listener answers for that slot
 |   before the next one is keyed. A slot that sounds is ours to use. A slot that
 |   does not, or that disturbs the driver, is the driver's.
 |
 |   It waits for an answer rather than advancing on a timer, and keeps every
 |   answer on screen, because the timed version could not be read. It asked
 |   someone to watch a number changing every two seconds and carry thirty-two
 |   verdicts in their head, and what came back from the first run was "I hear a
 |   tone" -- true, and not an answer to the question asked. Here the finished
 |   screen IS the answer, and one photograph of it carries the whole sweep.
 |
 |   That run also found that SRL's Debug::Print cannot do "%2d". Its snprintfEx
 |   switches on the character straight after the '%', so a width digit matches
 |   no case and the whole conversion is dropped: the screen read "slot d of d"
 |   and named no slot at all. Only %d, %02d, %c, %s, %x, %u and %f exist.
 | Author: suinevere
 | Dependencies: srl.hpp
 ----------------------*/
#include <srl.hpp>
using namespace SRL::Types;

/*----------------------
 | SCSP_REGS / TONE_RAM / TONE_SA
 | Description: The SCSP register file, where the test tone is written, and that
 |   same address as the chip sees it -- sound RAM is 0x25A00000 to the SH-2 and
 |   0 to the chip, so the SA field carries the offset alone.
 |
 |   0x60000 is 64 KB below the synth's own waveform area at 0x70000 and far
 |   above the highest address BOOTSND.MAP names under any reading of it
 |   (~0x48600), which is the same reasoning the synth's address rests on. If
 |   this probe is silent on EVERY slot, that reasoning is what to doubt first,
 |   not the slots.
 | Author: suinevere
 ----------------------*/
#define SCSP_REGS ((volatile unsigned short*) 0x25B00000)
#define TONE_RAM  ((volatile signed char*)    0x25A60000)
#define TONE_SA   0x60000UL

/*----------------------
 | DRIVER_ON
 | Description: Whether this build has the SGL sound driver. shared.mk defines
 |   SRL_USE_SGL_SOUND_DRIVER only when it is 1, so an undefined symbol is the
 |   driverless build rather than a mistake.
 | Author: suinevere
 ----------------------*/
#if defined(SRL_USE_SGL_SOUND_DRIVER) && SRL_USE_SGL_SOUND_DRIVER == 1
#define DRIVER_ON 1
#else
#define DRIVER_ON 0
#endif

/*----------------------
 | TONE_LEN / TONE_PERIOD
 | Description: The tone: a square wave of TONE_PERIOD samples, TONE_LEN long.
 |   Square rather than sine because what is being listened for is presence, not
 |   quality, and a square is audible through a small speaker at a level a sine
 |   is not. 128 samples at the SCSP's 44.1 kHz is 345 Hz.
 | Author: suinevere
 ----------------------*/
#define TONE_LEN    8192
#define TONE_PERIOD 128

/*----------------------
 | CHK_N / CHK_WORD_RAM / CHK_BYTE_RAM / CHK_BYTE_RAM16
 | Description: A read-back check of sound RAM, and the two ways of writing it
 |   laid side by side: CHK_WORD_RAM is filled with 16-bit stores and
 |   CHK_BYTE_RAM with 8-bit ones, then both are read back as words and compared
 |   against what was meant to be there.
 |
 |   This exists because three sessions of this fault were spent reasoning about
 |   code that an emulator ran correctly every time. The SCSP is behind a
 |   sixteen-bit bus, so a byte store into sound RAM is an access that bus cannot
 |   express -- but "cannot express" is a claim about hardware, and the only
 |   place to settle it is on hardware. The counts say whether a write lands at
 |   all; the four bytes printed under them say whether it lands where it was
 |   aimed, which is what would show a byte order that does not match the SH-2's.
 |
 |   Both regions sit above the highest address BOOTSND.MAP names under any
 |   reading (~0x48600) and below the synth's own area at 0x70000, so neither
 |   walks on the driver or on the tone.
 | Author: suinevere
 ----------------------*/
#define CHK_N          256
#define CHK_WORD_RAM   ((volatile unsigned short*) 0x25A62000)
#define CHK_BYTE_RAM   ((volatile signed char*)    0x25A64000)
#define CHK_BYTE_RAM16 ((volatile unsigned short*) 0x25A64000)

/*----------------------
 | CHK_OFF_RAM / CHK_ON_RAM / CHK_CYCLE_RAM
 | Description: One region per sound-block state, so no state can be read a
 |   leftover of another's.
 |
 |   Measured on hardware, from a netbin, with the block off: 1 of 256 bytes
 |   survived -- and 1 is exactly the score a region of zeroes gets, because
 |   position 219 is the only offset whose wanted byte is itself zero. Both write
 |   widths scored it. So the chip was not returning what was written, it was
 |   returning nothing, and the width was never the question.
 |
 |   What is left is whether the SCSP answers at all while SMPC SNDOFF holds the
 |   sound block in reset, which is the state the netbin puts it in before it
 |   writes a single waveform. Under Mednafen the chip answers regardless, which
 |   is why every build has looked correct for as long as this has existed.
 | Author: suinevere
 ----------------------*/
#define CHK_OFF_RAM    ((volatile unsigned short*) 0x25A62000)
#define CHK_ON_RAM     ((volatile unsigned short*) 0x25A66000)
#define CHK_CYCLE_RAM  ((volatile unsigned short*) 0x25A68000)

/*----------------------
 | NSLOTS / MARK_UNKNOWN / MARK_HEARD / MARK_SILENT
 | Description: The slots to walk, and the three states an answer can be in. All
 |   thirty-two are walked, not just the high ones: the answer for 0-27 is as
 |   interesting as the answer for 28-31, because a driver that turns out to use
 |   only a few would leave room for both the music and the effects.
 | Author: suinevere
 ----------------------*/
#define NSLOTS        32
#define MARK_UNKNOWN  '?'
#define MARK_HEARD    '+'
#define MARK_SILENT   '.'

/*----------------------
 | slot_regs
 | Description: One slot's sixteen registers. Each slot is 0x20 bytes apart in
 |   the register window.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- 0..31
 | Returns: a pointer to its first register
 ----------------------*/
static volatile unsigned short *slot_regs(int slot) {
    return SCSP_REGS + (slot * (0x20 / 2));
}

/*----------------------
 | write_tone
 | Description: Fills the sound-RAM tone buffer with a square wave. Written by
 |   the CPU rather than by DMA: sound RAM is directly addressable and this runs
 |   once, so there is nothing to gain and a DMA restriction to get wrong.
 |
 |   Written a word at a time, because sound RAM is behind the SCSP on the
 |   sixteen-bit B-bus and a byte write there is an access the bus cannot
 |   express. This was a byte loop, which is fine under an emulator and is what
 |   made the same code produce bleeps on one run of real hardware and silence on
 |   the next -- a probe that writes its tone the way the engine used to would
 |   inherit the fault it is supposed to be measuring around.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: how many of CHK_N bytes read back as written
 ----------------------*/
static void write_tone(void) {
    volatile unsigned short *ram = (volatile unsigned short *) TONE_RAM;
    for (int i = 0; i < TONE_LEN; i += 2) {
        unsigned int hi = ((i     % TONE_PERIOD) < (TONE_PERIOD / 2)) ? 100u : (unsigned int)(-100 & 0xFF);
        unsigned int lo = (((i + 1) % TONE_PERIOD) < (TONE_PERIOD / 2)) ? 100u : (unsigned int)(-100 & 0xFF);
        ram[i >> 1] = (unsigned short) ((hi << 8) | lo);
    }
}

/*----------------------
 | key_slot
 | Description: Keys one slot on the tone at its natural rate, sustaining, at
 |   full send level. The register layout is the SCSP User's Manual's and the
 |   same one saturn/src/sound/scsp.c writes: 0x00 carries the loop control, the
 |   key bits and SA's high nibble, 0x02 the rest of SA, 0x04/0x06 the loop
 |   start and end in samples, 0x08/0x0A the envelope, 0x10 the pitch and 0x16
 |   the send level.
 |
 |   Looping, so a slot that works is heard for as long as it is held rather
 |   than for the tone's own length -- a one-shot that ended early would be hard
 |   to tell from a slot that never started.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- 0..31
 | Returns: how many of CHK_N bytes read back as written
 ----------------------*/
static void key_slot(int slot) {
    volatile unsigned short *s = slot_regs(slot);
    s[0x00 / 2] = (unsigned short) ((1u << 5) | (1u << 4) | ((TONE_SA >> 16) & 0x0Fu));
    s[0x02 / 2] = (unsigned short) (TONE_SA & 0xFFFFu);
    s[0x04 / 2] = 0;
    s[0x06 / 2] = (unsigned short) (TONE_LEN - 1);
    s[0x08 / 2] = 0x001F;          /* attack immediate, no decay: sustains */
    s[0x0A / 2] = 0x001F;
    s[0x0C / 2] = 0x0000;
    s[0x0E / 2] = 0x0000;
    s[0x10 / 2] = 0x0000;          /* the waveform's own rate */
    s[0x12 / 2] = 0x0000;
    s[0x14 / 2] = 0x0000;
    s[0x16 / 2] = (unsigned short) (7u << 13);
    s[0x00 / 2] = (unsigned short) (s[0x00 / 2] | (1u << 11));
    s[0x00 / 2] = (unsigned short) (s[0x00 / 2] | (1u << 12));
}

/*----------------------
 | unkey_slot
 | Description: Releases a slot. Written back the way scsp.c does, so a slot
 |   left sounding cannot be mistaken for the next one working.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- 0..31
 | Returns: how many of CHK_N bytes read back as written
 ----------------------*/
static void unkey_slot(int slot) {
    volatile unsigned short *s = slot_regs(slot);
    s[0x00 / 2] = (unsigned short) (s[0x00 / 2] & ~(1u << 11));
    s[0x00 / 2] = (unsigned short) (s[0x00 / 2] | (1u << 12));
}

/*----------------------
 | count_keyed
 | Description: How many of the thirty-two slots have KYONB set -- how many are
 |   sounding, or would be the moment the chip scans them.
 |
 |   Read three times: on arrival, after SNDOFF, and after every slot has been
 |   cleared by hand. Those three numbers are the whole question when a program
 |   inherits the chip from another one. A browser that plays audio leaves slots
 |   keyed; SNDOFF halts the 68K that would have released them but does not touch
 |   the registers it left behind; and a keyed slot loops its waveform for as
 |   long as the machine is on. Nothing about that is visible from a CD boot,
 |   where the chip arrives quiet.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: 0..32
 ----------------------*/
static int count_keyed(void) {
    int n = 0;
    for (int i = 0; i < NSLOTS; i++)
        if ((slot_regs(i)[0x00 / 2] & (1u << 11)) != 0u) n++;
    return n;
}

/*----------------------
 | silence_all
 | Description: Zeroes every slot and pulses KYONEX once, the same thing
 |   scsp_silence_all does in the engine. KYONEX makes the chip scan every
 |   slot's KYONB at once, so one pulse at the end releases all of them.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: how many of CHK_N bytes read back as written
 ----------------------*/
static void silence_all(void) {
    for (int i = 0; i < NSLOTS; i++) {
        volatile unsigned short *s = slot_regs(i);
        for (int j = 0; j < 0x20 / 2; j++) s[j] = 0;
    }
    slot_regs(0)[0x00 / 2] = (unsigned short) (1u << 12);
}

/*----------------------
 | chk_expect
 | Description: What byte belongs at offset i. A stride of seven so no two
 |   neighbours share a value: a swapped pair, a dropped store and a store that
 |   landed one byte over all read differently, which a constant or a simple ramp
 |   would not distinguish.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: i -- byte offset within the checked region
 | Returns: the byte that should be readable there
 ----------------------*/
static unsigned char chk_expect(int i) {
    return (unsigned char) (i * 7 + 3);
}

/*----------------------
 | chk_run
 | Description: Fills both check regions, reads them back as words, and counts
 |   how many bytes survived each way. Read back as words in both cases on
 |   purpose -- the read is then the same access for both, so any difference in
 |   the counts is a difference between the two writes and not between two ways
 |   of looking.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: ram -- the region to test; first -- receives the first four bytes read
 |   back, or 0 for none
 | Returns: how many of CHK_N bytes read back as written
 ----------------------*/
static int chk_run(volatile unsigned short *ram, unsigned char *first) {
    int i, ok = 0;
    for (i = 0; i < CHK_N; i += 2)
        ram[i >> 1] = (unsigned short)
            (((unsigned int) chk_expect(i) << 8) | chk_expect(i + 1));
    for (i = 0; i < CHK_N; i += 2) {
        unsigned short w = ram[i >> 1];
        if (((w >> 8) & 0xFFu) == chk_expect(i)) ok++;
        if ((w & 0xFFu) == chk_expect(i + 1))    ok++;
    }
    if (first != 0)
        for (i = 0; i < 4; i++) {
            unsigned short w = ram[i >> 1];
            first[i] = (unsigned char) ((i & 1) ? (w & 0xFFu) : ((w >> 8) & 0xFFu));
        }
    return ok;
}

/*----------------------
 | g_d0 / g_d1 / g_a0 / g_a1
 | Description: Both ports on both pad families, so a controller works whichever
 |   port it is in and whatever mode a 3D pad is switched to. Pointers rather
 |   than objects because SRL's peripheral classes want Core::Initialize to have
 |   run first, and main owns that ordering.
 | Author: suinevere
 ----------------------*/
static SRL::Input::Digital *g_d0, *g_d1;
static SRL::Input::Analog  *g_a0, *g_a1;

/*----------------------
 | pressed
 | Description: True on the frame `b` edges down on any pad in either port.
 |
 |   Edge rather than level, and that is not a preference. SRL reads pads
 |   active-low, so an Analog device opened on a port that actually holds a
 |   Digital pad reports every button held, forever, and IsConnected does not
 |   screen it out. A level test built on that is true on every frame of every
 |   boot; an edge test is immune because a constant never transitions. This is
 |   the same finding saturn/src/input/input.h carries at length, and it cost
 |   that module two attempts to learn.
 | Author: suinevere
 | Dependencies: SRL (Input)
 | Globals: g_d0, g_d1, g_a0, g_a1
 | Params: b -- the button to test
 | Returns: 1 on the frame it goes down
 ----------------------*/
static int pressed(SRL::Input::Digital::Button b) {
    return (g_d0->IsConnected() && g_d0->WasPressed(b))
        || (g_d1->IsConnected() && g_d1->WasPressed(b))
        || (g_a0->IsConnected() && g_a0->WasPressed(b))
        || (g_a1->IsConnected() && g_a1->WasPressed(b));
}

/*----------------------
 | verdict
 | Description: How one contiguous run of slots came out, in a word. Reported
 |   rather than left to be counted off the map by eye, because "are 24-27 all
 |   ours" is the question the effect engine waits on, and miscounting it is the
 |   entire cost of getting this wrong.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: mark -- the answer map; first/last -- the inclusive slot range
 | Returns: "all", "none", "some", or "?" while any of the run is unanswered
 ----------------------*/
static const char *verdict(const char *mark, int first, int last) {
    int heard = 0, silent = 0;
    for (int i = first; i <= last; i++) {
        if (mark[i] == MARK_UNKNOWN) return "?";
        if (mark[i] == MARK_HEARD) heard++; else silent++;
    }
    if (silent == 0) return "all";
    if (heard == 0)  return "none";
    return "some";
}

/*----------------------
 | sound_env_init
 | Description: Puts the chip into whichever of the two environments this build
 |   is asking about.
 |
 |   With the driver, nothing to do: SRL's Core::Initialize has already run
 |   SND_Init, and SND_Init is what sets the master volume -- which is why the CD
 |   build's splash jingle is audible without anybody asking for it.
 |
 |   Without the driver, the block is turned ON with the 68K parked, and that is
 |   the correction this whole probe was built to find. It used to leave the
 |   block OFF: SNDOFF, then set the master volume, then drive slots from the
 |   SH-2. Measured on hardware from a netbin, that state writes and reads sound
 |   RAM perfectly -- 256 of 256, right byte order -- and produces no audio at
 |   all, on any of the thirty-two slots. The registers take the writes and the
 |   chip does not sound. Mednafen generates audio whatever the block state is,
 |   which is why every build and every probe has looked correct since the day
 |   this was written.
 |
 |   So SNDON, which needs the 68K to have somewhere harmless to be: with no
 |   driver loaded it would otherwise run whatever the browser left in sound RAM.
 |   Its reset vectors are the first eight bytes of that RAM, so a stack pointer
 |   and an entry address go there, and at that address a single instruction --
 |   0x60FE, `bra.s` to itself -- which is a two-byte program that spins forever
 |   and touches nothing. Then the master volume, which nothing else sets when
 |   there is no driver to set it.
 | Author: suinevere
 | Dependencies: SGL (slSoundOffWait, slSoundOnWait) when driverless
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void sound_env_init(void) {
#if !DRIVER_ON
    volatile unsigned short *ram  = (volatile unsigned short *) 0x25A00000;
    volatile unsigned short *ctrl = SCSP_REGS + (0x400 / 2);
    slSoundOffWait();
    ram[0x000 / 2] = 0x0000;
    ram[0x002 / 2] = 0x0FFC;
    ram[0x004 / 2] = 0x0000;
    ram[0x006 / 2] = 0x0100;
    ram[0x100 / 2] = 0x60FE;
    slSoundOnWait();
    ctrl[0] = (unsigned short) ((ctrl[0] & 0xFFF0u) | 0x000Fu);
#endif
}

int main() {
    SRL::Core::Initialize(HighColor::Colors::Black);

    int keyed_arrival = count_keyed();

    /* Three states, one region each, because the question this build exists to
       answer is which of them lets the SH-2 reach the chip at all. On hardware
       from a netbin the block-off case scored 1 of 256, and 1 is what a region
       of zeroes scores. Whichever of these reads back 256 is the sequence the
       engine has to use before it uploads a single waveform. */
    slSoundOffWait();
    int ok_off = chk_run(CHK_OFF_RAM, 0);

    slSoundOnWait();
    int ok_on = chk_run(CHK_ON_RAM, 0);

    slSoundOffWait();
    slSoundOnWait();
    unsigned char cfirst[4];
    int ok_cycle = chk_run(CHK_CYCLE_RAM, cfirst);

    sound_env_init();
    int keyed_after_off = count_keyed();
    silence_all();
    int keyed_after_clear = count_keyed();

    write_tone();

    SRL::Input::Digital d0(0), d1(1);
    SRL::Input::Analog  a0(0), a1(1);
    g_d0 = &d0; g_d1 = &d1; g_a0 = &a0; g_a1 = &a1;

    char mark[NSLOTS + 1];
    char row0[17], row1[17];
    int i, slot = 0, done = 0, answered;




    for (i = 0; i < NSLOTS; i++) mark[i] = MARK_UNKNOWN;
    mark[NSLOTS] = 0;
    row0[16] = 0;
    row1[16] = 0;


    while (1) {
        SRL::Debug::Print(2, 2, "CAN THE SH-2 REACH THE CHIP?");
        SRL::Debug::Print(2, 4, "block OFF      %d of %d   ", ok_off, CHK_N);
        SRL::Debug::Print(2, 5, "block ON       %d of %d   ", ok_on, CHK_N);
        SRL::Debug::Print(2, 6, "OFF then ON    %d of %d   ", ok_cycle, CHK_N);
        SRL::Debug::Print(2, 8, "want       %02x %02x %02x %02x",
                          chk_expect(0), chk_expect(1), chk_expect(2), chk_expect(3));
        SRL::Debug::Print(2, 9, "OFF then ON %02x %02x %02x %02x",
                          cfirst[0], cfirst[1], cfirst[2], cfirst[3]);
        SRL::Debug::Print(2, 11, "slots keyed on arrival  %d   ", keyed_arrival);
        SRL::Debug::Print(2, 12, "        after SNDOFF    %d   ", keyed_after_off);
        SRL::Debug::Print(2, 13, "        after clearing  %d   ", keyed_after_clear);
        SRL::Debug::Print(2, 15, "A or RIGHT for the slot sweep");
        SRL::Core::Synchronize();
        if (pressed(SRL::Input::Digital::Button::A)
         || pressed(SRL::Input::Digital::Button::C)
         || pressed(SRL::Input::Digital::Button::Right)) break;
    }
    for (i = 2; i <= 15; i++)
        SRL::Debug::Print(2, (uint8_t) i, "                                    ");

    key_slot(slot);

    while (1) {
        for (i = 0; i < 16; i++) { row0[i] = mark[i]; row1[i] = mark[i + 16]; }

        SRL::Debug::Print(2, 2, "SCSP FREE-SLOT PROBE   driver: %s",
                          DRIVER_ON ? "ON " : "OFF");
        if (done) {
            SRL::Debug::Print(2, 4, "sweep done -- photograph this     ");
            SRL::Debug::Print(2, 5, "LEFT re-tests the last slot       ");
        } else {
            SRL::Debug::Print(2, 4, "slot %d keyed -- do you hear it?  ", slot);
            SRL::Debug::Print(2, 5, "RIGHT/A yes  DOWN/B no  LEFT back ");
        }
        SRL::Debug::Print(2, 7,  " 0-15  %s", row0);
        SRL::Debug::Print(2, 8,  "16-31  %s", row1);
        SRL::Debug::Print(2, 10, "+ ours    . driver's    ? unasked");
        SRL::Debug::Print(2, 12, "effects want 24-27: %s   ", verdict(mark, 24, 27));
        SRL::Debug::Print(2, 13, "synth claims 28-31: %s   ", verdict(mark, 28, 31));

        SRL::Core::Synchronize();

        answered = 0;
        if (!done && (pressed(SRL::Input::Digital::Button::Right)
                   || pressed(SRL::Input::Digital::Button::A)
                   || pressed(SRL::Input::Digital::Button::C))) {
            mark[slot] = MARK_HEARD;
            answered = 1;
        } else if (!done && (pressed(SRL::Input::Digital::Button::Down)
                          || pressed(SRL::Input::Digital::Button::B))) {
            mark[slot] = MARK_SILENT;
            answered = 1;
        }

        if (answered) {
            unkey_slot(slot);
            if (slot >= NSLOTS - 1) {
                done = 1;
            } else {
                slot++;
                key_slot(slot);
            }
        } else if (pressed(SRL::Input::Digital::Button::Left)) {
            unkey_slot(slot);
            if (done) done = 0;
            else if (slot > 0) slot--;
            mark[slot] = MARK_UNKNOWN;
            key_slot(slot);
        }
    }
    return 0;
}
