/*----------------------
 | bg_dim.h
 | Description: The wallpaper dim's arithmetic and held value: how a player's
 |   chosen offset composes with a transition ramp, and where that choice lives
 |   between rooms.
 |
 |   Split out of title.cxx because it is arithmetic, not hardware. title.cxx
 |   includes SRL and cannot build on the host, and this composition is the
 |   subtlest logic in the feature -- keeping it here is what lets
 |   saturn/tests/test_bg_dim.c pin it. Nothing in this file may include an SRL
 |   header.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef BG_DIM_H
#define BG_DIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | bg_dim_compose
 | Description: The signed VDP2 colour offset for a ramp level and a held offset.
 |   Additive rather than multiplicative: a held lighten still dips toward black
 |   during a transition instead of scaling around its own resting point, which is
 |   what makes a room change read the same at every dim setting.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: level -- 0 (black) to 255 (unmodified); hold -- -255..+255
 | Returns: the per-channel offset, -255..+255
 ----------------------*/
int bg_dim_compose(int level, int hold);

/*----------------------
 | bg_dim_set / bg_dim_get / bg_dim_effective
 | Description: set clamps and holds the player's chosen offset; get reads it;
 |   effective composes it with a ramp level. Zero is "no dim", and is the value
 |   at which title.cxx releases the colour-offset channel entirely.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_bg_hold (in bg_dim.c)
 | Params: offset -- -255..+255, clamped; level -- 0..255
 | Returns: get and effective return the offset; set returns N/A
 ----------------------*/
void bg_dim_set(int offset);
int  bg_dim_get(void);
int  bg_dim_effective(int level);

#ifdef __cplusplus
}
#endif
#endif /* BG_DIM_H */
