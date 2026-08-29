/*----------------------
 | field_clock.cxx
 | Description: See field_clock.h.
 | Author: suinevere
 | Dependencies: field_clock.h, SRL
 ----------------------*/
#include "field_clock.h"
#include <srl.hpp>

/*----------------------
 | g_fields / g_hooked
 | Description: The field count, volatile because an interrupt writes it while
 |   the main line reads it, and the one-time subscription guard -- the same
 |   idiom boot_music.cxx uses for its own V-blank handler.
 | Author: suinevere
 ----------------------*/
static volatile unsigned int g_fields = 0;
static bool g_hooked = false;

/*----------------------
 | field_clock_vblank
 | Description: One increment per field, and nothing else: this runs on every
 |   field of the session, so anything added here is charged to all of them.
 |   Written as a read and a store rather than ++, which C++20 deprecates on a
 |   volatile.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fields
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void field_clock_vblank(void) {
    g_fields = g_fields + 1;
}

/*----------------------
 | field_clock_start
 | Description: See field_clock.h.
 | Author: suinevere
 | Dependencies: SRL (Core::OnVblank)
 | Globals: g_hooked
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void field_clock_start(void) {
    if (g_hooked) return;
    SRL::Core::OnVblank += field_clock_vblank;
    g_hooked = true;
}

/*----------------------
 | field_clock_now
 | Description: See field_clock.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fields
 | Params: N/A
 | Returns: the field count
 ----------------------*/
extern "C" unsigned int field_clock_now(void) {
    return g_fields;
}
