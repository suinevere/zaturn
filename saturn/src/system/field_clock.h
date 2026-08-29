/*----------------------
 | field_clock.h
 | Description: A monotonic count of video fields since the clock was started,
 |   advanced from the V-blank interrupt and therefore still advancing while the
 |   main line is blocked inside a CD read.
 |
 |   That property is the whole point. Anything paced by counting its own
 |   Core::Synchronize calls measures frames it was present for, not time that
 |   passed: slSynch waits for the frame boundary, so a read that blocks for a
 |   fifth of a second simply makes that one frame a fifth of a second long, and
 |   a ramp paced that way adds its full length to the load instead of covering
 |   any of it. Pacing against this clock is what lets the menu's fade-out and the
 |   story read happen over the same seconds rather than one after the other.
 | Author: suinevere
 | Dependencies: SRL (Core::OnVblank)
 ----------------------*/
#ifndef FIELD_CLOCK_H
#define FIELD_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | field_clock_start
 | Description: Subscribes the counter to V-blank. Idempotent, and never
 |   unsubscribed: one increment per field is not worth the teardown, and a
 |   caller that stopped the clock would break every other caller reading it.
 | Author: suinevere
 | Dependencies: SRL (Core::OnVblank)
 | Globals: g_fields, g_hooked
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void field_clock_start(void);

/*----------------------
 | field_clock_now
 | Description: The field count. Wraps after about two years of continuous
 |   running; compare two readings as a signed difference rather than with < or >
 |   and the wrap costs nothing.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_fields
 | Params: N/A
 | Returns: fields elapsed since field_clock_start
 ----------------------*/
unsigned int field_clock_now(void);

#ifdef __cplusplus
}
#endif
#endif /* FIELD_CLOCK_H */
