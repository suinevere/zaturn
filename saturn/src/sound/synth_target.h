/*----------------------
 | synth_target.h
 | Description: The Saturn-side half of the synth: the real SCSP addresses, the
 |   sound-block state the netbin has to establish for itself, and the V-blank
 |   subscription that drives the tick. Split from synth.c so that file stays
 |   plain C and keeps compiling on the host, where none of this exists.
 | Author: suinevere
 | Dependencies: srl.hpp
 ----------------------*/
#ifndef SYNTH_TARGET_H
#define SYNTH_TARGET_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | synth_target_init
 | Description: Binds the synth to the SCSP, uploads its waveforms and
 |   subscribes the V-blank tick. Call once at startup, before any synth_start.
 |   Idempotent: a second call re-binds and re-uploads but subscribes only once.
 | Author: suinevere
 | Dependencies: SRL (Core::OnVblank), SGL (slSoundOffWait), synth.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void synth_target_init(void);

#ifdef __cplusplus
}
#endif
#endif /* SYNTH_TARGET_H */
