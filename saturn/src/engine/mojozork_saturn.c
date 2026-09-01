/*----------------------
 | mojozork_saturn.c
 | Description: The single translation unit that pulls in the mojozork Z-Machine
 |   core (via #include of ../../mojozork.c, mirroring mojozork-libretro.c) so this
 |   file can reach the core's file-static symbols. MOJOZORK_SATURN excludes the
 |   core's stdio main/die and enables the Saturn guards. Owns the interpreter
 |   state and the boot/run entry points main.cxx calls, plus the accessor that
 |   exposes the loaded story image to the typeahead.
 | Author: suinevere
 | Dependencies: saturn_compat.h, ../../mojozork.c (the Z-Machine core),
 |   saturn_glue.h (the hooks wired into ZMachineState)
 ----------------------*/
#define MOJOZORK_SATURN 1
#include "saturn_compat.h"
#include "../../mojozork.c"

#include "saturn_glue.h"

/*----------------------
 | g_zmachine
 | Description: The interpreter's state block, pointed to by the core's global
 |   GState for the life of the program.
 | Author: suinevere
 ----------------------*/
static ZMachineState g_zmachine;

/*----------------------
 | mojo_boot
 | Description: Wires the Saturn hooks into the Z-Machine state (die, writestr,
 |   readline, sound_effect), seeds the RNG, and loads the story image. The core
 |   takes ownership of the buffer -- initStory frees the previous story on the
 |   next boot -- so the caller must not free it.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (GState, initStory), saturn_glue.h (the hooks)
 | Globals: g_zmachine, GState, random_seed
 | Params: story -- the loaded story bytes; len -- their length; seed -- RNG seed
 | Returns: N/A
 ----------------------*/
void mojo_boot(uint8_t *story, uint32_t len, int seed) {
    GState = &g_zmachine;
    GState->startup_script = NULL;
    GState->die      = saturn_die;
    GState->writestr = saturn_writestr;
    GState->readline = saturn_readline;
    GState->sound_effect = saturn_sound_effect;
    random_seed = (sint32) seed;
    initStory("ZORK1.Z3", story, len);
}

/*----------------------
 | mojo_run
 | Description: Runs the interpreter until it sets quit, executing one instruction
 |   per iteration. Input/output happen inside the instructions via the wired
 |   hooks.
 |
 |   Reaching the end of that loop is the win signal. Only Z-code can set the
 |   quit flag here: a typed "quit" is intercepted by soft_reset before the
 |   interpreter sees it, so the story getting here means its own ending
 |   routine ran to completion.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (runInstruction, GState->quit),
 |   music_on_win (declared locally, the way mojozork.c declares its own
 |   Saturn hooks -- this unit is included into a C++ translation unit and a
 |   header's extern "C" would conflict with those declarations)
 | Globals: GState
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mojo_run(void) {
    extern void music_on_win(void);
    while (!GState->quit) {
        runInstruction();
    }
    music_on_win();
}

/*----------------------
 | mojo_release
 | Description: Frees the loaded story image and its filename copy and clears
 |   every pointer into them, so the C heap comes back to what a cold boot has.
 |   initStory frees the outgoing image too, but not until mojo_boot -- by which
 |   point main has already allocated the incoming one, and the heap is ~194 KB
 |   against stories up to 129 KB, so the two never fit. Between them sits the
 |   title screen, whose wallpaper decode refuses outright without 82 KB of the
 |   same heap free. So the release belongs on the way back to the title rather
 |   than at the next load, and soft_reset_to_title is where it is called from.
 |   A no-op before any story has been booted.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (GState), saturn_compat.h (free)
 | Globals: GState
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mojo_release(void) {
    if (GState == NULL) return;
    if (GState->story != NULL) { free(GState->story); GState->story = NULL; }
    if (GState->story_filename != NULL) {
        free(GState->story_filename);
        GState->story_filename = NULL;
    }
    GState->story_len = 0;
    GState->pc = NULL;
    GState->sp = NULL;
    GState->bp = 0;
    GState->quit = 1;
}

/*----------------------
 | saturn_story_data
 | Description: Exposes the loaded story image so the typeahead can decode the
 |   game's own dictionary/grammar at runtime.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (GState)
 | Globals: GState
 | Params: len_out -- receives the story length (may be NULL); set to 0 when none
 | Returns: the story bytes, or NULL before a story is loaded
 ----------------------*/
const uint8_t* saturn_story_data(uint32_t* len_out) {
    if (GState == NULL || GState->story == NULL) { if (len_out) *len_out = 0; return NULL; }
    if (len_out) *len_out = (uint32_t) GState->story_len;
    return GState->story;
}
