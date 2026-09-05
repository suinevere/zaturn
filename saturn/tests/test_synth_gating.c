/* Which build plays the synth, and when.

   The rule is one line -- the synth is the fallback, so it plays wherever the
   disc did not bring music -- but it has to hold for both builds without a
   compile-time switch, because a rule that reads differently in the two builds
   is a rule that will drift apart. The netbin passes 0 because it has no disc
   at all.

   Build (from the repo root):
     gcc -O2 -I saturn/src -I saturn/src/sound -o /tmp/t_gate \
         saturn/tests/test_synth_gating.c saturn/src/sound/synth.c \
         saturn/src/sound/scsp.c saturn/src/sound/tracker.c \
         saturn/src/sound/synth_waves.c \
         saturn/src/sound/music_synth_data.c && /tmp/t_gate
*/
#include "../src/sound/synth.h"
#include <stdio.h>
#include <assert.h>

static void test_disc_with_cd_audio_keeps_the_synth_quiet(void) {
    assert(synth_should_play(1) == 0);
}

static void test_disc_without_cd_audio_hands_over_to_the_synth(void) {
    assert(synth_should_play(0) != 0);
}

static void test_the_netbin_case_is_the_same_rule(void) {
    /* The netbin has no disc, so it asks the same question with 0 and gets
       the same answer -- no #ifdef in the decision. */
    int netbin_has_cd_audio = 0;
    assert(synth_should_play(netbin_has_cd_audio) != 0);
}

static void test_any_nonzero_track_count_counts_as_cd_audio(void) {
    /* music_cdda_has_audio() returns a count-ish int, not a strict 0/1, so the
       rule must treat every nonzero value as "the disc brought music". */
    for (int n = 1; n < 32; n++) assert(synth_should_play(n) == 0);
}

int main(void) {
    test_disc_with_cd_audio_keeps_the_synth_quiet();
    test_disc_without_cd_audio_hands_over_to_the_synth();
    test_the_netbin_case_is_the_same_rule();
    test_any_nonzero_track_count_counts_as_cd_audio();
    printf("test_synth_gating: all passed\n");
    return 0;
}
