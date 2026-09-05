/* The song bank, against the MUSIC.PAT the generator actually wrote.

   This is the part of loading a tune off the disc that goes wrong quietly. A
   directory offset read one byte out gives a record that parses, plays, and is
   noise; a header from a build with a different pattern shape gives cells cast
   over the wrong stride. So the bank takes its reader as a callback and this
   test hands it the file from disk -- no SRL, no drive, and every refusal path
   reachable by handing it a header that is wrong in exactly one way.

   The file is read from cd/data/BG, which is where the disc build stages it.
   Skipped rather than failed when it is absent, the way the budget tests are:
   a checkout that has not run the generator has nothing to test.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_bank \
         saturn/tests/test_song_bank.c saturn/src/sound/song_bank.c \
         saturn/src/sound/music_synth_data.c && /tmp/t_bank
     ...and again with -DNETBIN, where every tune is linked and the bank is a
     pass-through.
*/
#include "../src/sound/song_bank.h"
#include "../src/sound/music_synth_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static unsigned char *g_file;
static long           g_file_len;
static unsigned char  g_header[MUSIC_PAT_HEADER_BYTES];
static unsigned char  g_slot[MUSIC_PAT_SLOT_BYTES];
static int            g_reads;

/*  The reader the bank would get from the CD layer, backed by the file in
    memory. Returns bytes read, and 0 for a read that runs off the end -- which
    is what a truncated file on a scratched disc looks like from up here.  */
static int file_read(int sector, int bytes, void *dest) {
    long at = (long) sector * MUSIC_PAT_SECTOR;
    g_reads++;
    if (at < 0 || at + bytes > g_file_len) return 0;
    memcpy(dest, g_file + at, (size_t) bytes);
    return bytes;
}

static int load_file(void) {
    FILE *f = fopen("saturn/cd/data/BG/" MUSIC_PAT_FILE, "rb");
    if (!f) f = fopen("cd/data/BG/" MUSIC_PAT_FILE, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    g_file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_file = (unsigned char *) malloc((size_t) g_file_len);
    if (!g_file || fread(g_file, 1, (size_t) g_file_len, f) != (size_t) g_file_len) {
        fclose(f); return 0;
    }
    fclose(f);
    memcpy(g_header, g_file, sizeof g_header);
    return 1;
}

static int bind_fresh(void) {
    song_bank_reset();
    return song_bank_bind(g_header, sizeof g_header, g_slot, sizeof g_slot, file_read);
}

static void test_unbound_is_the_linked_catalogue(void) {
    /* What the netbin always is, and what the CD build is before the disc has
       been read or on a disc that never carried the file. */
    song_bank_reset();
    assert(song_bank_count() == music_synth_song_count());
    assert(song_bank_at(0) == music_synth_song_at(0));
    assert(song_bank_resident() == -1);
    assert(strcmp(song_bank_id(0), music_synth_song_id(0)) == 0);
    assert(song_bank_for_track(MUSIC_SYNTH_TRACK_MIN)
           == music_synth_song_for_track(MUSIC_SYNTH_TRACK_MIN));
}

static void test_binding_the_real_file_is_accepted(void) {
    assert(bind_fresh() == 1);
    assert(song_bank_count() >= music_synth_song_count());
}

static void test_every_tune_loads_and_is_playable(void) {
    assert(bind_fresh() == 1);
    for (int i = 0; i < song_bank_count(); i++) {
        const TrackerSong *s = song_bank_at(i);
        assert(s != 0);
        assert(s->cells != 0 && s->order != 0);
        assert(s->rows == MUSIC_SYNTH_ROWS);
        assert(s->channels == MUSIC_SYNTH_CHANNELS);
        assert(s->order_len > 0);
        assert(s->speed > 0);
        assert(s->loop_to < s->order_len);
        int highest = 0;
        for (int k = 0; k < s->order_len; k++)
            if (s->order[k] > highest) highest = s->order[k];
        /* The order indexes patterns inside this record; the record's own
           pattern count is the first word of the slot for a loaded tune. */
        if (i >= MUSIC_SYNTH_SONGS) {
            int patterns = (g_slot[0] << 8) | g_slot[1];
            assert(highest < patterns);
        }
    }
}

static void test_every_tune_has_an_id(void) {
    assert(bind_fresh() == 1);
    for (int i = 0; i < song_bank_count(); i++) {
        const char *id = song_bank_id(i);
        assert(id != 0 && id[0] != '\0');
        assert(strlen(id) <= MUSIC_SYNTH_ID_MAX);
    }
}

static void test_a_linked_tune_costs_no_read(void) {
    /* The whole reason the linked prefix is kept: the boot menus play before
       anything has touched the drive, and re-asking for the default on every
       room of one mood must not seek. */
    assert(bind_fresh() == 1);
    g_reads = 0;
    for (int i = 0; i < MUSIC_SYNTH_SONGS; i++) song_bank_at(i);
    assert(g_reads == 0);
}

static void test_re_asking_for_the_resident_tune_costs_no_read(void) {
    assert(bind_fresh() == 1);
    if (song_bank_count() <= MUSIC_SYNTH_SONGS) return;   /* nothing on the disc */
    song_bank_at(MUSIC_SYNTH_SONGS);
    assert(song_bank_resident() == MUSIC_SYNTH_SONGS);
    g_reads = 0;
    song_bank_at(MUSIC_SYNTH_SONGS);
    assert(g_reads == 0);
}

static void test_every_track_names_a_tune_the_bank_has(void) {
    assert(bind_fresh() == 1);
    for (int t = MUSIC_SYNTH_TRACK_MIN; t <= MUSIC_SYNTH_TRACK_MAX; t++) {
        int s = song_bank_for_track(t);
        assert(s >= 0 && s < song_bank_count());
    }
}

static void test_a_header_that_is_not_ours_is_refused(void) {
    unsigned char bad[MUSIC_PAT_HEADER_BYTES];

    memcpy(bad, g_header, sizeof bad);
    bad[0] = 'X';
    song_bank_reset();
    assert(song_bank_bind(bad, sizeof bad, g_slot, sizeof g_slot, file_read) == 0);

    memcpy(bad, g_header, sizeof bad);
    bad[5] = 99;                                  /* version */
    song_bank_reset();
    assert(song_bank_bind(bad, sizeof bad, g_slot, sizeof g_slot, file_read) == 0);

    memcpy(bad, g_header, sizeof bad);
    bad[9] = MUSIC_SYNTH_ROWS + 1;                /* pattern shape */
    song_bank_reset();
    assert(song_bank_bind(bad, sizeof bad, g_slot, sizeof g_slot, file_read) == 0);
}

static void test_a_slot_too_small_is_refused(void) {
    /* Not clamped and not truncated. The slot size is compiled in from the
       generator, so a file wanting more is a file from another build, and
       reading it into this one's buffer would write past it. */
    assert(bind_fresh() == 1);
    song_bank_reset();
    assert(song_bank_bind(g_header, sizeof g_header, g_slot, 16, file_read) == 0);
}

static void test_a_refusal_leaves_the_linked_catalogue_working(void) {
    unsigned char bad[MUSIC_PAT_HEADER_BYTES];
    memcpy(bad, g_header, sizeof bad);
    bad[0] = 'X';
    song_bank_reset();
    assert(song_bank_bind(bad, sizeof bad, g_slot, sizeof g_slot, file_read) == 0);
    assert(song_bank_count() == music_synth_song_count());
    assert(song_bank_at(0) == music_synth_song_at(0));
}

static void test_a_read_that_fails_falls_back_to_the_default(void) {
    /* A scratched disc must sound like the wrong tune, not like silence. */
    assert(bind_fresh() == 1);
    if (song_bank_count() <= MUSIC_SYNTH_SONGS) return;
    long was = g_file_len;
    g_file_len = MUSIC_PAT_SECTOR;                /* every record now off the end */
    const TrackerSong *s = song_bank_at(song_bank_count() - 1);
    g_file_len = was;
    assert(s == music_synth_song_at(MUSIC_SYNTH_DEFAULT));
    assert(song_bank_resident() == -1);
}

int main(void) {
    test_unbound_is_the_linked_catalogue();
    if (!load_file()) {
        printf("test_song_bank: skipped -- no cd/data/BG/%s\n", MUSIC_PAT_FILE);
        return 0;
    }
    test_binding_the_real_file_is_accepted();
    test_every_tune_loads_and_is_playable();
    test_every_tune_has_an_id();
    test_a_linked_tune_costs_no_read();
    test_re_asking_for_the_resident_tune_costs_no_read();
    test_every_track_names_a_tune_the_bank_has();
    test_a_header_that_is_not_ours_is_refused();
    test_a_slot_too_small_is_refused();
    test_a_refusal_leaves_the_linked_catalogue_working();
    test_a_read_that_fails_falls_back_to_the_default();
    printf("test_song_bank: all passed\n");
    return 0;
}
