/* Build:
     gcc -O2 -I saturn/src -o /tmp/tbd saturn/tests/test_bg_dim.c \
         saturn/src/video/bg_dim.c && /tmp/tbd
   bg_dim.c is deliberately free of SRL includes so this links on the host. */
#include "../src/video/bg_dim.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* No hold: byte-identical to the behaviour before this change. */
    assert(bg_dim_compose(255, 0) == 0);
    assert(bg_dim_compose(0, 0)   == -255);
    assert(bg_dim_compose(128, 0) == -127);

    /* A darkening hold rests dark and still dips to black. */
    assert(bg_dim_compose(255, -96) == -96);
    assert(bg_dim_compose(0, -96)   == -255);

    /* A lightening hold rests light and still dips. */
    assert(bg_dim_compose(255, 64) == 64);
    assert(bg_dim_compose(0, 64)   == -191);

    /* The clamp holds at both rails. */
    assert(bg_dim_compose(255, 400)  == 255);
    assert(bg_dim_compose(0, -400)   == -255);

    /* Only a resting, unheld wallpaper releases the channel. */
    assert(bg_dim_compose(255, 0) == 0);
    assert(bg_dim_compose(255, -32) != 0);

    /* The held value round-trips and clamps on the way in. */
    bg_dim_set(-96);
    assert(bg_dim_get() == -96);
    assert(bg_dim_effective(255) == -96);
    assert(bg_dim_effective(0)   == -255);

    bg_dim_set(9999);
    assert(bg_dim_get() == 255);
    bg_dim_set(-9999);
    assert(bg_dim_get() == -255);

    bg_dim_set(0);
    assert(bg_dim_get() == 0);
    assert(bg_dim_effective(255) == 0);

    /* Fresh state records the resting level, 255, before anything ramps. */
    assert(bg_dim_last_level() == 255);

    /* A ramp step is recorded byte-for-byte, not the composed offset. */
    bg_dim_note_level(128);
    assert(bg_dim_last_level() == 128);
    bg_dim_note_level(0);
    assert(bg_dim_last_level() == 0);

    /* This is what title_bg_dim_set relies on: mid-transition (level 0, the
       screen held black), a changed hold must not read as a jump back to full
       brightness -- replaying at the recorded level keeps the composed value
       from swinging toward neutral the way a hardcoded 255 would. */
    bg_dim_note_level(0);
    bg_dim_set(0);
    assert(bg_dim_effective(bg_dim_last_level()) == -255);   /* still black */
    bg_dim_set(-96);
    assert(bg_dim_effective(bg_dim_last_level()) == -255);   /* still black */

    /* At rest (255, the Options page's level), a changed hold takes effect
       immediately when replayed at the recorded level -- live preview. */
    bg_dim_note_level(255);
    bg_dim_set(64);
    assert(bg_dim_effective(bg_dim_last_level()) == 64);
    bg_dim_set(-32);
    assert(bg_dim_effective(bg_dim_last_level()) == -32);

    printf("test_bg_dim: ok\n");
    return 0;
}
