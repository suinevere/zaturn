/* Which rows the Sound page shows.

   The page has always built its row list from what is available rather than
   from a fixed layout, and remembers the selection as a row ID rather than an
   index, because the same index names a different row on a different disc.
   The synth adds one more row to that list under one rule: it is the fallback,
   so its slider appears exactly where the CD Music slider does not. The page
   must never offer two music sliders at once, which is the assertion below
   that is worth keeping if every other one is rewritten.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/menu -I saturn/src/video \
         -o /tmp/t_rows saturn/tests/test_sound_rows.c \
         saturn/src/menu/menu_layout.c && /tmp/t_rows
*/
#include "../src/menu/menu_layout.h"
#include <stdio.h>
#include <assert.h>

static int has_row(const int *rows, int n, int want) {
    for (int i = 0; i < n; i++) if (rows[i] == want) return 1;
    return 0;
}

static void test_cd_disc_shows_cd_and_hides_synth(void) {
    int rows[8];
    int n = sound_page_rows(1, 0, rows, 8);
    assert(has_row(rows, n, SND_ROW_CD));
    assert(!has_row(rows, n, SND_ROW_SYNTH));
}

static void test_silent_disc_shows_synth_and_hides_cd(void) {
    int rows[8];
    int n = sound_page_rows(0, 0, rows, 8);
    assert(has_row(rows, n, SND_ROW_SYNTH));
    assert(!has_row(rows, n, SND_ROW_CD));
}

static void test_never_two_music_sliders(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++) {
            int rows[8];
            int n = sound_page_rows(cd, blb, rows, 8);
            assert(!(has_row(rows, n, SND_ROW_CD) && has_row(rows, n, SND_ROW_SYNTH)));
        }
}

static void test_master_row_is_always_present_now(void) {
    /* It used to be hidden when neither CD-DA nor a Blorb was there, because
       it would have switched two levels nothing read. The synth is always a
       source, so there is always something for it to switch. */
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++) {
            int rows[8];
            int n = sound_page_rows(cd, blb, rows, 8);
            assert(has_row(rows, n, SND_ROW_MASTER));
        }
}

static void test_pcm_row_follows_the_blorb(void) {
    int rows[8];
    int n = sound_page_rows(0, 1, rows, 8);
    assert(has_row(rows, n, SND_ROW_PCM));
    n = sound_page_rows(0, 0, rows, 8);
    assert(!has_row(rows, n, SND_ROW_PCM));
}

static void test_ok_and_cancel_come_last_and_always(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++) {
            int rows[8];
            int n = sound_page_rows(cd, blb, rows, 8);
            assert(n >= 2);
            assert(rows[n - 2] == SND_ROW_OK);
            assert(rows[n - 1] == SND_ROW_CANCEL);
        }
}

static void test_the_netbin_page_is_master_synth_ok_cancel(void) {
    int rows[8];
    int n = sound_page_rows(0, 0, rows, 8);
    assert(n == 4);
    assert(rows[0] == SND_ROW_MASTER);
    assert(rows[1] == SND_ROW_SYNTH);
    assert(rows[2] == SND_ROW_OK);
    assert(rows[3] == SND_ROW_CANCEL);
}

static void test_a_short_buffer_is_not_overrun(void) {
    int rows[3];
    int n = sound_page_rows(1, 1, rows, 3);
    assert(n <= 3);
}

int main(void) {
    test_cd_disc_shows_cd_and_hides_synth();
    test_silent_disc_shows_synth_and_hides_cd();
    test_never_two_music_sliders();
    test_master_row_is_always_present_now();
    test_pcm_row_follows_the_blorb();
    test_ok_and_cancel_come_last_and_always();
    test_the_netbin_page_is_master_synth_ok_cancel();
    test_a_short_buffer_is_not_overrun();
    printf("test_sound_rows: all passed\n");
    return 0;
}
