/* Which rows the Sound page shows.

   The page has always built its row list from what is available rather than
   from a fixed layout, and remembers the selection as a row ID rather than an
   index, because the same index names a different row on a different disc.

   That rule now has a second edge. It used to be the disc that decided which
   music slider appeared -- the synth was the fallback and showed up exactly
   where CD-DA was absent -- and it is now the SOURCE, which the player can
   change on a disc that has both. So the list changes under the cursor while
   the page is open, which nothing else on any Options page does, and the page
   rebuilds it every frame and clamps the selection. The assertion worth keeping
   if every other one is rewritten is still that there are never two music
   sliders at once.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/menu -I saturn/src/video \
         -o /tmp/t_rows saturn/tests/test_sound_rows.c \
         saturn/src/menu/menu_layout.c && /tmp/t_rows
*/
#include "../src/menu/menu_layout.h"
#include <stdio.h>
#include <assert.h>

#define MAXR SND_PAGE_ROW_MAX

static int has_row(const int *rows, int n, int want) {
    for (int i = 0; i < n; i++) if (rows[i] == want) return 1;
    return 0;
}

static int index_of(const int *rows, int n, int want) {
    for (int i = 0; i < n; i++) if (rows[i] == want) return i;
    return -1;
}

static void test_the_active_source_decides_which_slider_shows(void) {
    /* Not the disc. A disc with CD-DA played through the synth shows the synth's
       level, because the CD one would be a control over something silent. */
    int rows[MAXR];
    int n = sound_page_rows(1, 0, 0, 1, rows, MAXR);
    assert(has_row(rows, n, SND_ROW_CD));
    assert(!has_row(rows, n, SND_ROW_SYNTH));

    n = sound_page_rows(1, 0, 1, 1, rows, MAXR);
    assert(has_row(rows, n, SND_ROW_SYNTH));
    assert(!has_row(rows, n, SND_ROW_CD));
}

static void test_never_two_music_sliders(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++)
            for (int syn = 0; syn < 2; syn++)
                for (int test = 0; test < 2; test++) {
                    int rows[MAXR];
                    int n = sound_page_rows(cd, blb, syn, test, rows, MAXR);
                    assert(!(has_row(rows, n, SND_ROW_CD)
                             && has_row(rows, n, SND_ROW_SYNTH)));
                }
}

static void test_the_source_row_needs_two_sources(void) {
    /* Without CD-DA there is one source, and a row that cycles between one
       thing and itself is a control that lies about being a control. */
    int rows[MAXR];
    int n = sound_page_rows(0, 0, 1, 1, rows, MAXR);
    assert(!has_row(rows, n, SND_ROW_SOURCE));
    n = sound_page_rows(1, 0, 0, 1, rows, MAXR);
    assert(has_row(rows, n, SND_ROW_SOURCE));
    n = sound_page_rows(1, 0, 1, 1, rows, MAXR);
    assert(has_row(rows, n, SND_ROW_SOURCE));
}

static void test_test_track_sits_under_the_level_it_is_judged_by(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int syn = 0; syn < 2; syn++) {
            int rows[MAXR];
            int n = sound_page_rows(cd, 1, syn, 1, rows, MAXR);
            int lvl = index_of(rows, n, syn ? SND_ROW_SYNTH : SND_ROW_CD);
            int tst = index_of(rows, n, SND_ROW_TEST);
            assert(lvl >= 0 && tst >= 0);
            assert(tst == lvl + 1);
        }
}

static void test_test_track_is_the_callers_call(void) {
    int rows[MAXR];
    int n = sound_page_rows(1, 0, 0, 0, rows, MAXR);
    assert(!has_row(rows, n, SND_ROW_TEST));
}

static void test_master_row_is_always_present_now(void) {
    /* It used to be hidden when neither CD-DA nor a Blorb was there, because
       it would have switched two levels nothing read. The synth is always a
       source, so there is always something for it to switch. */
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++)
            for (int syn = 0; syn < 2; syn++) {
                int rows[MAXR];
                int n = sound_page_rows(cd, blb, syn, 1, rows, MAXR);
                assert(has_row(rows, n, SND_ROW_MASTER));
            }
}

static void test_pcm_row_follows_the_blorb(void) {
    int rows[MAXR];
    int n = sound_page_rows(0, 1, 1, 1, rows, MAXR);
    assert(has_row(rows, n, SND_ROW_PCM));
    n = sound_page_rows(0, 0, 1, 1, rows, MAXR);
    assert(!has_row(rows, n, SND_ROW_PCM));
}

static void test_ok_and_cancel_come_last_and_always(void) {
    for (int cd = 0; cd < 2; cd++)
        for (int blb = 0; blb < 2; blb++)
            for (int syn = 0; syn < 2; syn++)
                for (int test = 0; test < 2; test++) {
                    int rows[MAXR];
                    int n = sound_page_rows(cd, blb, syn, test, rows, MAXR);
                    assert(n >= 2);
                    assert(rows[n - 2] == SND_ROW_OK);
                    assert(rows[n - 1] == SND_ROW_CANCEL);
                }
}

static void test_the_netbin_page_is_master_synth_test_ok_cancel(void) {
    /* No disc, so no Source row and no CD level -- the shortest the page gets. */
    int rows[MAXR];
    int n = sound_page_rows(0, 0, 1, 1, rows, MAXR);
    assert(n == 5);
    assert(rows[0] == SND_ROW_MASTER);
    assert(rows[1] == SND_ROW_SYNTH);
    assert(rows[2] == SND_ROW_TEST);
    assert(rows[3] == SND_ROW_OK);
    assert(rows[4] == SND_ROW_CANCEL);
}

static void test_the_longest_list_fits_the_declared_maximum(void) {
    /* SND_PAGE_ROW_MAX is what the page sizes its array and its box from, so a
       row added without raising it would be silently dropped off the bottom. */
    int rows[MAXR];
    int n = sound_page_rows(1, 1, 1, 1, rows, MAXR);
    assert(n == SND_PAGE_ROW_MAX);
}

static void test_a_short_buffer_is_not_overrun(void) {
    int rows[3];
    int n = sound_page_rows(1, 1, 1, 1, rows, 3);
    assert(n <= 3);
}

int main(void) {
    test_the_active_source_decides_which_slider_shows();
    test_never_two_music_sliders();
    test_the_source_row_needs_two_sources();
    test_test_track_sits_under_the_level_it_is_judged_by();
    test_test_track_is_the_callers_call();
    test_master_row_is_always_present_now();
    test_pcm_row_follows_the_blorb();
    test_ok_and_cancel_come_last_and_always();
    test_the_netbin_page_is_master_synth_test_ok_cancel();
    test_the_longest_list_fits_the_declared_maximum();
    test_a_short_buffer_is_not_overrun();
    printf("test_sound_rows: all passed\n");
    return 0;
}
