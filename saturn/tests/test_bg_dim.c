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

    printf("test_bg_dim: ok\n");
    return 0;
}
