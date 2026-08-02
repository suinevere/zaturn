/* Build:
     gcc -O2 -I saturn/src -o /tmp/td saturn/tests/test_display.c \
         saturn/src/video/display.c && /tmp/td
   The -I is needed because display.c reaches for "sound/music.h" -- the display
   model now carries the category -> picture table, so it knows the TC_* names. */
#include "../src/video/display.h"
#include "../src/sound/music.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Recompute the RGB555 packing independently so a typo in the macro is caught. */
static unsigned short rgb(int r, int g, int b) {
    return (unsigned short)(0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}

static void test_tables_well_formed(void) {
    int i;
    /* Palette indices of the colour presets: 1..DISP_PRESET_N, since Dynamic
       holds index 0. */
    for (i = DISP_PAL_PRESET0; i <= DISP_PRESET_N; i++) {
        assert(display_preset_bg(i)   >= 0 && display_preset_bg(i)   < DISP_BG_COLOR_N);
        assert(display_preset_text(i) >= 0 && display_preset_text(i) < DISP_TEXT_N);
        assert(display_preset_name(i) != NULL);
        assert(display_preset_name(i)[0] != '\0');
        /* Must fit the selector field. The DISPLAY box is the full 40 columns
           (fx=0, fw=40), so its right border sits at column 39 and the last
           drawable column is 38. Content starts at x = 2 and the value prints
           at x + 17 = 19 as "< %s >", which is the name plus 4. That leaves
           38 - 19 + 1 - 4 = 16 columns for the name itself. The old bound of 22
           was never achievable; it is tightened here because Task 7's row
           numbers consume the label side's slack, so the value column can no
           longer be moved right to rescue a long name. */
        assert(strlen(display_preset_name(i)) <= 16);
    }
    for (i = 0; i < DISP_BG_COLOR_N; i++)  assert(display_bg_color_name(i) != NULL);
    for (i = 0; i < DISP_TEXT_N; i++)      assert(display_text_name(i)     != NULL);
}

static void test_known_colors(void) {
    assert(display_bg_rgb(DISP_BG_BLACK)        == rgb(0x00, 0x00, 0x00));
    assert(display_bg_rgb(DISP_BG_AMBER)        == rgb(0xFF, 0xB0, 0x00));
    assert(display_bg_rgb(DISP_BG_BLUE)         == rgb(0x00, 0x00, 0xAA));
    assert(display_bg_rgb(DISP_BG_BRIGHT_WHITE) == rgb(0xFF, 0xFF, 0xFF));
    assert(display_text_rgb(DISP_TEXT_BRIGHT_AMBER) == rgb(0xFF, 0xAF, 0x00));
    assert(display_text_rgb(DISP_TEXT_BRIGHT_GREEN) == rgb(0x55, 0xFF, 0x55));
    /* ANSI 37 is light gray and named Gray -- keeps BBC Micro / MSX authentic.
       White is its own entry and is actually white. */
    assert(display_text_rgb(DISP_TEXT_GRAY)  == rgb(0xAA, 0xAA, 0xAA));
    assert(display_text_rgb(DISP_TEXT_WHITE) == rgb(0xFF, 0xFF, 0xFF));
}

/* Palette index of colour preset `n`, counting presets from 0 as the PRESETS
   table does. Dynamic sits ahead of them at index 0. */
#define PAL(n) (DISP_PAL_PRESET0 + (n))

static void test_preset_contents(void) {
    /* Spot-check the ends and the two collision pairs from the spec. */
    assert(display_preset_bg(PAL(0))  == DISP_BG_BLACK);
    assert(display_preset_text(PAL(0)) == DISP_TEXT_BRIGHT_GREEN);     /* IBM PC (MDA) */
    assert(display_preset_bg(PAL(1))  == DISP_BG_BLACK);
    assert(display_preset_text(PAL(1)) == DISP_TEXT_GREEN);            /* Apple II Plus */
    assert(display_preset_bg(PAL(14)) == DISP_BG_BRIGHT_WHITE);
    assert(display_preset_text(PAL(14)) == DISP_TEXT_BLACK);           /* Mac Classic */
    /* Commodore 64 (6) and Atari 800 (9) share a combo but not a name. */
    assert(display_preset_bg(PAL(6)) == display_preset_bg(PAL(9)));
    assert(display_preset_text(PAL(6)) == display_preset_text(PAL(9)));
    assert(strcmp(display_preset_name(PAL(6)), display_preset_name(PAL(9))) != 0);
    /* IBM PC MDA (0) and Commodore PET (4) likewise. */
    assert(display_preset_bg(PAL(0)) == display_preset_bg(PAL(4)));
    assert(display_preset_text(PAL(0)) == display_preset_text(PAL(4)));
    assert(strcmp(display_preset_name(PAL(0)), display_preset_name(PAL(4))) != 0);
}

static void test_defaults_and_palette_name(void) {
    DisplayState d;
    /* No art registered, so Dynamic has nothing to show and the default falls to
       the first colour preset. The Dynamic default is covered by
       test_dynamic_palette. */
    display_set_images(NULL, 0);
    display_defaults(&d);
    assert(d.palette == PAL(0));                   /* IBM PC (MDA Monitor) */
    assert(d.bg == DISP_BG_BLACK);
    assert(d.text == DISP_TEXT_BRIGHT_GREEN);
    assert(strcmp(display_palette_name(&d), display_preset_name(PAL(0))) == 0);

    /* Diverge from the preset -> Custom; restore -> the name comes back. */
    d.text = DISP_TEXT_CYAN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    d.text = DISP_TEXT_BRIGHT_GREEN;
    assert(strcmp(display_palette_name(&d), display_preset_name(PAL(0))) == 0);

    /* The stored index disambiguates the collision: same colors, different name. */
    d.palette = PAL(4);
    assert(strcmp(display_palette_name(&d), display_preset_name(PAL(4))) == 0);
}

static const char *const IMAGES[] = { "HOUSE.TGA", "CAVE.TGA" };

/* A genuinely over-length list for the clamp check below. It used to be enough
   to hand display_set_images a two-entry array and claim DISP_IMAGE_MAX + 5,
   because nothing dereferenced the names at registration time. It does now --
   the Dynamic palette's slot is re-resolved against the new list on every scan,
   since that slot indexes the very array being replaced -- so a count larger
   than the array reads off the end. A caller cannot be caught lying about the
   length, so the fixture stops lying instead; the clamp is what this is
   testing, not the reaction to a bad caller.

   Filled at run time rather than written out: DISP_IMAGE_MAX is forty now, and
   a hand-written literal drifts into mostly-NULL padding the moment that number
   moves again -- at which point it stops proving the clamp works and starts
   proving the NULL guard does. */
#define OVERLONG_N (DISP_IMAGE_MAX + 5)
static char        g_overlong_text[OVERLONG_N][DISP_IMAGE_NAME_MAX];
static const char *g_overlong[OVERLONG_N];

static void build_overlong(void) {
    int i;
    for (i = 0; i < OVERLONG_N; i++) {
        char *s = g_overlong_text[i];
        s[0] = 'F'; s[1] = 'I'; s[2] = 'L';
        s[3] = (char) ('0' + (i / 10));
        s[4] = (char) ('0' + (i % 10));
        s[5] = '.'; s[6] = 'T'; s[7] = 'G'; s[8] = 'A'; s[9] = '\0';
        g_overlong[i] = s;
    }
}

static void test_image_registration(void) {
    display_set_images(NULL, 0);
    assert(display_image_count() == 0);
    assert(display_bg_count() == DISP_BG_COLOR_N);

    display_set_images(IMAGES, 2);
    assert(display_image_count() == 2);
    /* Registering images does not widen the Background row: it is colors only. */
    assert(display_bg_count() == DISP_BG_COLOR_N);

    /* Over-cap registration clamps rather than overflowing. */
    build_overlong();
    display_set_images(g_overlong, OVERLONG_N);
    assert(display_image_count() == DISP_IMAGE_MAX);
    display_set_images(IMAGES, 2);
}

static void test_bg_name_and_is_image(void) {
    DisplayState d;
    display_set_images(IMAGES, 2);
    /* Start from a colour preset rather than the defaults: with art registered
       the default is Dynamic, which carries a picture by definition, and the
       point here is that the Background row names a colour whether or not one
       is showing. */
    display_defaults(&d);
    d.palette = PAL(0);
    d.bg      = display_preset_bg(PAL(0));
    d.text    = display_preset_text(PAL(0));
    d.image   = DISP_IMAGE_NONE;
    assert(!display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "Black") == 0);

    /* An image is a separate field now; the Background row keeps naming a
       color, which is what shows through the menu frames over the picture. */
    d.image = 0;
    assert(display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "Black") == 0);
    /* Default palette is a color preset, so carrying an image reads Custom. */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
}

static void test_cycle_bg_stays_in_colors(void) {
    DisplayState d;
    int i;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    d.text = DISP_TEXT_BRIGHT_AMBER;   /* matches no bg color: guard inactive */
    d.bg = DISP_BG_BRIGHT_WHITE;       /* last color */

    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_BLACK);     /* wraps straight back, no images */
    display_cycle_bg(&d, -1);
    assert(d.bg == DISP_BG_BRIGHT_WHITE);

    /* Every reachable value is a color, in both directions. */
    for (i = 0; i < 40; i++) { display_cycle_bg(&d, 1);  assert(d.bg < DISP_BG_COLOR_N); }
    for (i = 0; i < 40; i++) { display_cycle_bg(&d, -1); assert(d.bg < DISP_BG_COLOR_N); }

    /* Cycling the background never disturbs the picture on top of it. */
    d.image = 1;
    display_cycle_bg(&d, 1);
    assert(d.image == 1);
}

static void test_legibility_guard(void) {
    DisplayState d;
    int i;
    display_set_images(NULL, 0);          /* colors only: guard fully active */
    display_defaults(&d);

    /* Black text must never be able to sit on the Black background. */
    d.text = DISP_TEXT_BLACK;
    d.bg   = DISP_BG_BRIGHT_WHITE;
    for (i = 0; i < 40; i++) {
        display_cycle_bg(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
    for (i = 0; i < 40; i++) {
        display_cycle_bg(&d, -1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }

    /* Same guard from the text side: Green text vs the Green background. */
    d.bg   = DISP_BG_GREEN;
    d.text = DISP_TEXT_BLACK;
    for (i = 0; i < 40; i++) {
        display_cycle_text(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
    for (i = 0; i < 40; i++) {
        display_cycle_text(&d, -1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
}

static void test_guard_follows_bg_color_under_image(void) {
    DisplayState d;
    int i, seen_black = 0;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    /* The guard is about the background *color*, which is still black here, so
       Black text stays unreachable even with a picture over it. */
    d.image = 0;
    d.bg    = DISP_BG_BLACK;
    for (i = 0; i < DISP_TEXT_N; i++) {
        display_cycle_text(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
        if (d.text == DISP_TEXT_BLACK) seen_black = 1;
    }
    assert(!seen_black);
}

static void test_cycle_palette(void) {
    DisplayState d;
    /* No art on this disc, so Dynamic (index 0) is unreachable and cycling steps
       straight over it -- including across both wraps. */
    display_set_images(NULL, 0);
    display_defaults(&d);                  /* first colour preset */
    assert(d.palette == PAL(0));

    display_cycle_palette(&d, 1);
    assert(d.palette == PAL(1));
    assert(d.bg == display_preset_bg(PAL(1)) && d.text == display_preset_text(PAL(1)));
    assert(strcmp(display_palette_name(&d), "Apple II Plus") == 0);

    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(0));
    assert(strcmp(display_palette_name(&d), "IBM PC (MDA)") == 0);

    /* Wraps at both ends, skipping Dynamic on the way past. */
    d.palette = PAL(DISP_PRESET_N - 1);    /* last colour preset */
    d.bg = display_preset_bg(d.palette); d.text = display_preset_text(d.palette);
    display_cycle_palette(&d, 1);
    assert(d.palette == PAL(0));
    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));

    /* From a Custom state, forward lands on the first preset and back on the
       last -- again stepping over Dynamic rather than selecting it. */
    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;               /* now Custom */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_cycle_palette(&d, 1);
    assert(d.palette == PAL(0));
    assert(d.bg == display_preset_bg(PAL(0)) && d.text == display_preset_text(PAL(0)));

    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;
    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));

    /* Selecting a preset always yields a legible pair. */
    {
        int i;
        for (i = DISP_PAL_PRESET0; i <= DISP_PRESET_N; i++) {
            assert(display_bg_rgb(display_preset_bg(i))
                != display_text_rgb(display_preset_text(i)));
        }
    }
}

static void test_encode_decode_roundtrip(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int n;

    display_set_images(NULL, 0);
    display_defaults(&a);
    a.palette = PAL(6); a.bg = display_preset_bg(PAL(6)); a.text = display_preset_text(PAL(6));

    n = display_encode(&a, buf);
    assert(n == DISP_BLOB_BYTES);
    assert(buf[0] == 4);            /* sentinel: + the Dynamic palette */

    assert(display_decode(buf, n, &b) == 1);
    assert(b.palette == a.palette && b.bg == a.bg && b.text == a.text);
}

static void test_collisions_roundtrip(void) {
    /* The regression the stored palette index exists to prevent: identical
       colors must still reload as the machine the player picked. */
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int pairs[2][2] = { { PAL(6), PAL(9) }, { PAL(0), PAL(4) } };
    int p, s;

    display_set_images(NULL, 0);
    for (p = 0; p < 2; p++) {
        for (s = 0; s < 2; s++) {
            int idx = pairs[p][s];
            a.palette = idx;
            a.bg = display_preset_bg(idx);
            a.text = display_preset_text(idx);
            display_encode(&a, buf);
            assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
            assert(b.palette == idx);
            assert(strcmp(display_palette_name(&b), display_preset_name(idx)) == 0);
        }
    }
}

static void test_custom_state_roundtrips(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    display_defaults(&a);
    a.text = DISP_TEXT_CYAN;                     /* diverged -> Custom */
    assert(strcmp(display_palette_name(&a), "Custom") == 0);

    display_encode(&a, buf);
    assert(buf[1] == PAL(0));                    /* the machine index survives */
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.text == DISP_TEXT_CYAN);
    assert(strcmp(display_palette_name(&b), "Custom") == 0);
}

static void test_decode_rejects_bad_input(void) {
    DisplayState d, def;
    unsigned char buf[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    display_defaults(&def);

    /* Absent block. */
    assert(display_decode(NULL, 0, &d) == 0);
    assert(d.palette == def.palette && d.bg == def.bg && d.text == def.text);

    /* Truncated block. */
    buf[0] = 1; buf[1] = 3; buf[2] = 0;
    assert(display_decode(buf, 3, &d) == 0);
    assert(d.palette == def.palette);

    /* Wrong sentinel. */
    buf[0] = 9; buf[1] = 3; buf[2] = 2; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.palette == def.palette);

    /* Out-of-range palette, background, and text each fall back. */
    buf[0] = 1; buf[1] = 99; buf[2] = 2; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.palette == def.palette);

    buf[0] = 1; buf[1] = 3; buf[2] = 99; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.bg == def.bg);

    buf[0] = 1; buf[1] = 3; buf[2] = 2; buf[3] = 99;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.text == def.text);
}

static void test_decode_missing_image_falls_back(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];

    /* Saved while two images were on the disc... */
    display_set_images(IMAGES, 2);
    a.palette = PAL(11);           /* ZX Spectrum: Light Gray bg, Black text */
    a.bg = DISP_BG_BLACK;
    a.image = 0;                   /* first image slot (exists at encode time) */
    a.text = DISP_TEXT_BLACK;
    display_encode(&a, buf);

    /* ...reloaded on a disc with none: the background falls back to Black,
       which would clash with the surviving Black text and blank the screen.
       The pairwise guard in display_decode catches this and restores both
       fields from the saved palette's own pair (ZX Spectrum: Light Gray/Black)
       instead of leaving Black-on-Black. */
    display_set_images(NULL, 0);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 0);
    assert(!display_is_image(&b));
    assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
    assert(b.palette == PAL(11));              /* palette byte survived */
    assert(b.bg == DISP_BG_LIGHT_GRAY);         /* restored from ZX Spectrum's own pair */
    assert(b.text == DISP_TEXT_BLACK);          /* restored from ZX Spectrum's own pair */
    display_set_images(IMAGES, 2);
}

static void test_decode_missing_image_never_clashes(void) {
    /* General form of the regression above: any saved image background paired
       with a text color that also exists as a background color must decode,
       with no images registered, to a legible pair -- for every palette, since
       the restored bg/text come from PRESETS[d->palette]. Black and Green are
       the two colors present in both the background and text tables. */
    static const int clashing_texts[2] = { DISP_TEXT_BLACK, DISP_TEXT_GREEN };
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int p, t;

    for (p = 0; p < DISP_PRESET_N; p++) {
        for (t = 0; t < 2; t++) {
            display_set_images(IMAGES, 2);
            a.palette = PAL(p);
            a.bg = DISP_BG_BLACK;
            a.image = 0;                        /* first image slot */
            a.text = clashing_texts[t];
            display_encode(&a, buf);

            display_set_images(NULL, 0);
            display_decode(buf, DISP_BLOB_BYTES, &b);
            assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
        }
    }
    display_set_images(IMAGES, 2);
}

static void test_decode_multi_field_corruption(void) {
    DisplayState d, def;
    unsigned char buf[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    display_defaults(&def);

    /* Valid sentinel and length, valid non-default background, but both
       palette and text are out of range. Both corrupt fields fall back
       independently while the valid background is accepted. */
    buf[0] = 1;                 /* valid sentinel */
    buf[1] = 99;                /* out-of-range palette */
    buf[2] = DISP_BG_AMBER;     /* valid, non-default background */
    buf[3] = 99;                /* out-of-range text */
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.bg == DISP_BG_AMBER);              /* valid field accepted */
    assert(d.palette == def.palette);           /* corrupt field fell back */
    assert(d.text == def.text);                 /* corrupt field fell back */
}

static void test_palette_count_is_fixed(void) {
    /* The row used to grow by one per picture. It does not any more: thirty-seven
       of them made a fifty-four-entry cycler where most steps read the disc. Every
       picture is still reachable, through the mood that owns it. */
    static const char *const names[] = { "WILDER1.TGA", "TOWN1.TGA" };
    const int fixed = 1 + DISP_PRESET_N;
    display_set_images(NULL, 0);
    assert(display_palette_count() == fixed);
    display_set_images(names, 2);
    assert(display_palette_count() == fixed);
    display_set_images(NULL, 0);
    assert(display_palette_count() == fixed);
}

static void test_dynamic_is_the_only_entry_with_a_picture(void) {
    static const char *const names[] = { "WILDER1.TGA", "TOWN1.TGA" };
    int i;
    display_set_images(names, 2);
    /* Every colour preset is a colour preset: no picture hiding behind any of
       them, and no picture-bearing entry past the end of the row either. */
    for (i = DISP_PAL_PRESET0; i < display_palette_count(); i++) {
        assert(display_preset_image(i) == DISP_IMAGE_NONE);
        assert(display_preset_name(i)[0] != '\0');
    }
    /* Dynamic is the exception, by definition -- its picture comes from whatever
       mood the engine last announced. */
    assert(display_preset_image(DISP_PAL_DYNAMIC) == display_dynamic_slot());
    assert(display_preset_image(DISP_PAL_DYNAMIC) != DISP_IMAGE_NONE);
    assert(display_preset_bg(DISP_PAL_DYNAMIC)    == DISP_BG_BLACK);
    assert(display_preset_text(DISP_PAL_DYNAMIC)  == DISP_TEXT_WHITE);
    display_set_images(NULL, 0);
}

static void test_cycle_palette_wraps_past_the_last_preset(void) {
    static const char *const names[] = { "WILDER1.TGA", "TOWN1.TGA" };
    DisplayState d;
    int i, seen_dynamic = 0;
    display_set_images(names, 2);
    display_defaults(&d);

    /* Forward off the last colour preset wraps straight to Dynamic. There used to
       be one entry per picture in between; walking into them is what this asserts
       no longer happens. */
    d.palette = PAL(DISP_PRESET_N - 1);
    d.bg      = display_preset_bg(d.palette);
    d.text    = display_preset_text(d.palette);
    d.image   = DISP_IMAGE_NONE;
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.image   == display_dynamic_slot());
    assert(d.bg      == DISP_BG_BLACK);
    assert(d.text    == DISP_TEXT_WHITE);

    /* And backward off Dynamic reaches the last colour preset, not a picture. */
    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));
    assert(d.image   == DISP_IMAGE_NONE);

    /* One full lap visits every entry exactly once and Dynamic exactly once, so
       the row has no picture entries left anywhere in it. */
    for (i = 0; i < display_palette_count(); i++) {
        display_cycle_palette(&d, 1);
        if (d.palette == DISP_PAL_DYNAMIC) seen_dynamic++;
        else assert(display_preset_image(d.palette) == DISP_IMAGE_NONE);
    }
    assert(seen_dynamic == 1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));
    display_set_images(NULL, 0);
}

static void test_cycle_palette_from_custom_with_images(void) {
    /* Regression: the Custom re-entry branch of display_cycle_palette must
       land on whatever the last entry of the row actually is. It used to be the
       last image preset; now the row ends at the last colour preset whether or
       not the disc has art, and the re-entry has to follow that. */
    static const char *const names[] = { "WILDER1.TGA", "TOWN1.TGA" };
    DisplayState d;
    display_set_images(names, 2);

    /* Genuine Custom state: a legible pair matching no preset's, and no picture
       (image is part of what display_palette_name compares, so it has to be set
       rather than left indeterminate). */
    d.palette = PAL(0);
    d.bg      = DISP_BG_GREEN;
    d.text    = DISP_TEXT_WHITE;
    d.image   = DISP_IMAGE_NONE;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);

    /* Backward from Custom lands on the last colour preset. */
    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));
    assert(d.bg      == display_preset_bg(PAL(DISP_PRESET_N - 1)));
    assert(d.text    == display_preset_text(PAL(DISP_PRESET_N - 1)));

    /* Forward from Custom enters at the head of the row, which is Dynamic now
       that this disc has art. */
    d.palette = PAL(0);
    d.bg      = DISP_BG_GREEN;
    d.text    = DISP_TEXT_WHITE;
    d.image   = DISP_IMAGE_NONE;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.bg      == display_preset_bg(DISP_PAL_DYNAMIC));
    assert(d.text    == display_preset_text(DISP_PAL_DYNAMIC));

    display_set_images(NULL, 0);
}

static void test_dynamic_name_shown_not_custom(void) {
    /* What the retired per-picture entries used to check, moved onto the one
       entry that still carries a picture. */
    static const char *const names[] = { "WILDER1.TGA" };
    DisplayState d;
    display_set_images(names, 1);
    display_defaults(&d);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(strcmp(display_palette_name(&d), "Dynamic") == 0);
    /* Diverging from the pinned trio reads as Custom, same as colour presets. */
    d.text = DISP_TEXT_GREEN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    d.text = DISP_TEXT_WHITE;
    d.bg   = DISP_BG_BLUE;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    d.bg    = DISP_BG_BLACK;
    d.image = DISP_IMAGE_NONE;              /* Dynamic without its picture */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_set_images(NULL, 0);
}

static void test_pinned_picture_blob_migrates_to_dynamic(void) {
    /* A blob saved back when the Palette row let one picture be pinned. There is
       no index for that any more, so it lands on Dynamic -- the nearest thing the
       row still offers to "I wanted a picture here" -- and reports itself as not
       restored verbatim, because the player is getting something other than what
       they saved. */
    static const char *const two[] = { "WILDER1.TGA", "TOWN1.TGA" };
    DisplayState d;
    unsigned char blob[DISP_BLOB_BYTES];
    int i;

    display_set_images(two, 2);
    for (i = 0; i < DISP_BLOB_BYTES; i++) blob[i] = 0;
    blob[0] = 4;
    blob[1] = 0xFF;                         /* the retired "this named picture" */
    blob[2] = DISP_BG_BLACK;
    blob[3] = DISP_TEXT_WHITE;
    for (i = 0; two[1][i]; i++) blob[4 + i] = (unsigned char) two[1][i];

    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);
    assert(d.palette == DISP_PAL_DYNAMIC);
    /* The name it stored is NOT honoured: Dynamic's picture is whatever mood the
       player resumes in, which is the whole point of the setting. */
    assert(d.image == display_dynamic_slot());
    assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));

    /* Same blob on a disc with no art at all: Dynamic has nothing to show, so it
       is not selected either, and what is left still has to be legible. */
    display_set_images(NULL, 0);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);
    assert(d.palette != DISP_PAL_DYNAMIC);
    assert(d.palette >= 0 && d.palette < display_palette_count());
    assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
}

/* --- image identity across discs ------------------------------------------
   The saved blob used to store only a slot index, so a disc whose TGA set had
   changed silently resolved that index to a different picture. These pin the
   behaviour to the file name instead. */

static void test_decode_resolves_image_after_reorder(void) {
    static const char *const before[] = { "AMIGA.TGA", "FOREST.TGA", "CASTLE.TGA" };
    static const char *const after[]  = { "CASTLE.TGA", "AMIGA.TGA", "FOREST.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    /* Save CASTLE, which is slot 2 on the disc we saved from. The palette is a
       plain colour preset: no index selects a picture any more, so what is under
       test is the name resolver alone, which legacy blobs still go through. */
    display_set_images(before, 3);
    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLACK;
    saved.image   = 2;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Same three images, different scan order: CASTLE is slot 0 now. Resolving
       positionally would hand back FOREST. */
    display_set_images(after, 3);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.image == 0);
    assert(strcmp(display_image_file(d.image), "CASTLE.TGA") == 0);
    display_set_images(NULL, 0);
}

static void test_decode_follows_image_when_earlier_one_removed(void) {
    static const char *const before[] = { "AMIGA.TGA", "FOREST.TGA", "CASTLE.TGA" };
    static const char *const after[]  = { "AMIGA.TGA", "CASTLE.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(before, 3);
    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLACK;
    saved.image   = 2;                        /* CASTLE */
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Dropping FOREST shifts CASTLE from slot 2 to slot 1. The old index 2 is
       still in range on this disc, so a range check alone would accept it and
       show nothing at all. */
    display_set_images(after, 2);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(strcmp(display_image_file(d.image), "CASTLE.TGA") == 0);
    display_set_images(NULL, 0);
}

static void test_decode_image_gone_falls_back(void) {
    static const char *const before[] = { "AMIGA.TGA", "FOREST.TGA" };
    static const char *const after[]  = { "AMIGA.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(before, 2);
    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLACK;
    saved.image   = 1;                        /* FOREST */
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    display_set_images(after, 1);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);   /* reports the fallback */
    /* The point is that nothing dangles: the picture that is gone must not still
       be referenced, and whatever is left has to be a coherent appearance. It is
       no longer "no picture at all" -- the default is Dynamic now, which carries
       one -- so the check is that the reference resolves on THIS disc. */
    assert(d.palette >= 0 && d.palette < display_palette_count());
    assert(d.image == DISP_IMAGE_NONE
           || (d.image >= 0 && d.image < display_image_count()));
    if (d.image != DISP_IMAGE_NONE)
        assert(strcmp(display_image_file(d.image), "FOREST.TGA") != 0);
    assert(d.bg < DISP_BG_COLOR_N);
    assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    display_set_images(NULL, 0);
}

static void test_color_state_needs_no_image(void) {
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    saved.palette = PAL(3);
    saved.bg      = display_preset_bg(PAL(3));
    saved.text    = display_preset_text(PAL(3));
    saved.image   = DISP_IMAGE_NONE;   /* encode reads this; leaving it indeterminate
                                          made the stored name a coin toss */
    display_encode(&saved, blob);

    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.palette == PAL(3) && d.bg == saved.bg && d.text == saved.text);
}

static void test_legacy_blob_still_decodes(void) {
    /* A blob written before names were stored: sentinel 1, four bytes,
       positional. Must keep working rather than resetting someone's colors.

       It also predates Dynamic taking index 0, so the stored 5 means the SIXTH
       colour preset, which is PAL(5) in the current row. Decoding it as a bare 5
       would hand the player the preset next door -- silently, and only visible
       after a reboot. */
    unsigned char legacy[4];
    DisplayState d;

    display_set_images(NULL, 0);
    legacy[0] = 1;
    legacy[1] = 5;                                          /* old-space index */
    legacy[2] = (unsigned char) display_preset_bg(PAL(5));
    legacy[3] = (unsigned char) display_preset_text(PAL(5));
    assert(display_decode(legacy, 4, &d) == 1);
    assert(d.palette == PAL(5));
    assert(d.bg == display_preset_bg(PAL(5)));
    assert(d.text == display_preset_text(PAL(5)));
    assert(strcmp(display_palette_name(&d), display_preset_name(PAL(5))) == 0);
}

static void test_legacy_blob_image_index_rejected(void) {
    /* A legacy blob naming an image slot carries no name to verify it with, so
       it cannot be trusted to mean the same picture. Fall back rather than
       guess. The byte it stored is past the end of the colour presets, which is
       what the sentinel-1 branch checks -- it has no other way to tell a slot
       number from a preset number. */
    unsigned char legacy[4];
    static const char *const one[] = { "WILDER1.TGA" };
    DisplayState d, def;

    display_set_images(one, 1);
    display_defaults(&def);
    legacy[0] = 1;
    legacy[1] = DISP_PRESET_N + 1;          /* an image slot in the old numbering */
    legacy[2] = 0xFF;                       /* sentinel-1 form: out-of-range bg */
    legacy[3] = DISP_TEXT_WHITE;
    assert(display_decode(legacy, 4, &d) == 0);
    /* What must not happen is the slot byte being honoured as a picture choice.
       The result is the default appearance -- which does carry a picture now
       that Dynamic is the default, so this compares against the defaults rather
       than asserting there is no image at all. */
    assert(d.palette == def.palette);
    assert(d.image   == def.image);
    display_set_images(NULL, 0);
}

static void test_decode_truncated_name_block(void) {
    static const char *const one[] = { "AMIGA.TGA" };
    DisplayState d, def, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(one, 1);
    display_defaults(&def);
    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLACK;
    saved.image   = 0;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Sentinel says a name follows, but the block is cut short. Nothing in it is
       trustworthy, so the whole thing is refused and the defaults stand -- which
       do carry a picture now that Dynamic is the default. */
    assert(display_decode(blob, DISP_BLOB_BYTES - 1, &d) == 0);
    assert(d.palette == def.palette);
    assert(d.image   == def.image);
    display_set_images(NULL, 0);
}

/* --- images as their own field --------------------------------------------- */

static void test_image_label_drops_extension_and_capitalizes(void) {
    static const char *const names[] = { "HOUSE.TGA", "TYPEWRTR.TGA", "CMPLAB.TGA" };
    display_set_images(names, 3);
    assert(strcmp(display_image_label(0), "House")    == 0);
    assert(strcmp(display_image_label(1), "Typewrtr") == 0);
    assert(strcmp(display_image_label(2), "Cmplab")   == 0);
    /* The file name itself is untouched -- it still has to open on the disc. */
    assert(strcmp(display_image_file(0), "HOUSE.TGA") == 0);
    /* Unregistered slots are empty, not garbage. */
    assert(display_image_label(3)[0] == '\0');
    assert(display_image_label(-1)[0] == '\0');
    display_set_images(NULL, 0);
}

static void test_two_labels_live_at_once(void) {
    /* The Display page prints one label while resolving another, so a single
       static buffer would make both read the same. */
    static const char *const names[] = { "HOUSE.TGA", "FOREST.TGA" };
    const char *a, *b;
    display_set_images(names, 2);
    a = display_image_label(0);
    b = display_image_label(1);
    assert(strcmp(a, "House")  == 0);
    assert(strcmp(b, "Forest") == 0);
    display_set_images(NULL, 0);
}

static void test_selecting_dynamic_resets_bg_and_text(void) {
    static const char *const names[] = { "WILDER1.TGA" };
    DisplayState d;
    display_set_images(names, 1);

    /* Come from a loud colour pair, so the reset is visible. Monochrome P3 is the
       last preset, so one step forward wraps onto Dynamic. */
    d.palette = PAL(DISP_PRESET_N - 1);     /* Monochrome P3: black on amber */
    d.bg      = display_preset_bg(d.palette);
    d.text    = display_preset_text(d.palette);
    d.image   = DISP_IMAGE_NONE;
    assert(d.bg == DISP_BG_AMBER);

    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.image   == display_dynamic_slot());
    assert(d.bg      == DISP_BG_BLACK);
    assert(d.text    == DISP_TEXT_WHITE);
    display_set_images(NULL, 0);
}

static void test_leaving_dynamic_clears_the_image(void) {
    static const char *const names[] = { "WILDER1.TGA" };
    DisplayState d;
    display_set_images(names, 1);
    display_defaults(&d);

    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(display_is_image(&d));

    display_cycle_palette(&d, 1);           /* on to the first colour preset */
    assert(d.palette == PAL(0));
    assert(d.image   == DISP_IMAGE_NONE);
    assert(!display_is_image(&d));
    display_set_images(NULL, 0);
}

static void test_bg_color_under_image_survives_a_save(void) {
    /* The color beneath a picture is what shows through the menu frames, so it
       has to round-trip independently of the picture. */
    static const char *const names[] = { "WILDER1.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(names, 1);
    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLUE;           /* deliberately not the preset's black */
    saved.text    = DISP_TEXT_WHITE;
    saved.image   = 0;
    display_encode(&saved, blob);

    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.image == 0);
    assert(d.bg    == DISP_BG_BLUE);
    assert(d.text  == DISP_TEXT_WHITE);
    /* Diverged from the preset's black, so it reads Custom. */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_set_images(NULL, 0);
}

static void test_sentinel2_blob_decodes_over_black(void) {
    /* The oldest named format packed the image into bg, so no colour was stored
       under it. It must still load rather than being thrown away -- black is what
       a picture was always paired with, and that part is still honoured. The
       picture itself is not: its palette marker is the retired pinned-picture one,
       so this lands on Dynamic like every other blob carrying it. */
    static const char *const names[] = { "WILDER1.TGA" };
    unsigned char old[DISP_BLOB_BYTES];
    DisplayState d;
    int i;

    display_set_images(names, 1);
    for (i = 0; i < DISP_BLOB_BYTES; i++) old[i] = 0;
    old[0] = 2;                             /* previous sentinel */
    old[1] = 0xFF;                          /* palette: the named image */
    old[2] = 0xFF;                          /* bg: the named image */
    old[3] = DISP_TEXT_WHITE;
    for (i = 0; names[0][i]; i++) old[4 + i] = (unsigned char) names[0][i];

    assert(display_decode(old, DISP_BLOB_BYTES, &d) == 0);
    assert(d.bg      == DISP_BG_BLACK);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.image   == display_dynamic_slot());
    display_set_images(NULL, 0);
}


/* --- the Dynamic palette --------------------------------------------------
   The pictures the disc actually ships, in the alphabetical order the CD scan
   yields them, so slot numbers here match the real thing. Kept in step with
   cd/data/TGA by saturn/tests/test_category_art.py, which reads the pools out of
   display.c and checks every name against the folder. */
static const char *const IMGS[] = {
    "DESERT1.TGA",  "DESERT2.TGA",  "DESERT3.TGA",
    "DUNGN1.TGA",   "DUNGN2.TGA",   "DUNGN3.TGA",
    "HORROR1.TGA",  "HORROR2.TGA",  "HORROR3.TGA",  "HORROR4.TGA",
    "HOUSE1.TGA",   "HOUSE2.TGA",   "HOUSE3.TGA",
    "MAGIC1.TGA",   "MAGIC2.TGA",   "MAGIC3.TGA",
    "MYSTERY1.TGA", "MYSTERY2.TGA", "MYSTERY3.TGA",
    "NAUTICA1.TGA", "NAUTICA2.TGA", "NAUTICA3.TGA",
    "SCIFI1.TGA",   "SCIFI2.TGA",   "SCIFI3.TGA",
    "TOWN1.TGA",    "TOWN2.TGA",    "TOWN3.TGA",
    "UNDRGRN1.TGA", "UNDRGRN2.TGA", "UNDRGRN3.TGA",
    "WATER1.TGA",   "WATER2.TGA",   "WATER3.TGA",
    "WILDER1.TGA",  "WILDER2.TGA",  "WILDER3.TGA"
};
#define IMGS_N ((int) (sizeof(IMGS) / sizeof(IMGS[0])))

static void test_category_art(void) {
    int cat, named = 0;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        const char *f = display_category_image(cat);
        if (f == NULL) continue;
        named++;
        assert(f[0] != '\0');
        assert(strlen(f) <= DISP_IMAGE_NAME_MAX - 1);   /* fits the save blob's field */
    }
    /* Twelve categories carry art. Three deliberately do not, and each NULL means
       "hold whatever is showing" rather than "picture missing":
         TC_DANGER / TC_TRIUMPH  moments, not places -- the music moves for them
                                 while the wallpaper stays on the room.
         TC_NEUTRAL              the nothing-matched answer. A room that named
                                 nothing has said nothing about how it looks, so
                                 keeping the previous picture beats cutting to an
                                 arbitrary one. TC_HOUSE exists so the domestic
                                 keywords have a real category to win instead of
                                 borrowing this one. */
    assert(named == 12);
    assert(display_category_image(TC_DANGER)  == NULL);
    assert(display_category_image(TC_TRIUMPH) == NULL);
    assert(display_category_image(TC_NEUTRAL) == NULL);
    assert(display_category_image(TC_HOUSE)   != NULL);   /* what Dynamic seeds from */
    assert(display_category_image(TC_TOWN)    != NULL);
    /* ...and a house is not a town: separate categories, separate pictures. */
    assert(strcmp(display_category_image(TC_HOUSE),
                  display_category_image(TC_TOWN)) != 0);

    /* Out of range is "keep current", never a stray pointer. */
    assert(display_category_image(-1) == NULL);
    assert(display_category_image(TEXT_NUM_CATEGORIES) == NULL);

    /* Pool sizes line up with what has a picture at all. */
    assert(display_category_image_count(TC_DANGER)  == 0);
    assert(display_category_image_count(TC_TRIUMPH) == 0);
    assert(display_category_image_count(-1) == 0);
    assert(display_category_image_count(TEXT_NUM_CATEGORIES) == 0);
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        if (display_category_image(cat) == NULL) assert(display_category_image_count(cat) == 0);
        else                                     assert(display_category_image_count(cat) >= 1);
    }

    /* Every place category can actually rotate. A pool of one is legal to the
       model and was what shipped first, but it means the track rotates under an
       unchanged wallpaper -- which looks exactly like the art side being broken,
       so it is pinned here rather than left to be noticed. (That the pools also
       do not share pictures is checked by test_category_art.py, which can read
       the whole table at once instead of walking it through the rotation.) */
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        if (display_category_image(cat) == NULL) continue;
        assert(display_category_image_count(cat) >= 2);
    }
}

static void test_category_rotation(void) {
    int before, after, cat;

    display_set_images(IMGS, IMGS_N);

    /* Rotation moves within a category, never to the same picture twice in a
       row, and only where the category actually has somewhere to go. */
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        if (display_category_image_count(cat) < 2) continue;
        display_set_dynamic_category(cat);
        before = display_dynamic_slot();
        display_rotate_dynamic_category(cat);
        after = display_dynamic_slot();
        assert(after != before);
        assert(after >= 0 && after < display_image_count());
    }

    /* A category with one picture (or none) holds it: the track rotates
       underneath an unchanged wallpaper rather than flickering to itself. */
    display_set_dynamic_category(TC_TOWN);
    before = display_dynamic_slot();
    if (display_category_image_count(TC_TOWN) < 2) {
        display_rotate_dynamic_category(TC_TOWN);
        assert(display_dynamic_slot() == before);
    }

    /* An event category has no pool at all, so rotating it is inert -- the
       wallpaper stays on whatever room the player is actually in. */
    display_set_dynamic_category(TC_MYSTERY);
    before = display_dynamic_slot();
    display_rotate_dynamic_category(TC_DANGER);
    assert(display_dynamic_slot() == before);

    /* Out of range is inert too, rather than walking off the table. */
    display_rotate_dynamic_category(-1);
    display_rotate_dynamic_category(TEXT_NUM_CATEGORIES);
    assert(display_dynamic_slot() == before);
}

static void test_dynamic_palette(void) {
    DisplayState d;
    int i;

    display_set_images(IMGS, IMGS_N);

    /* Index 0 is Dynamic, then the colour presets, and that is the whole row --
       registering art does not lengthen it. */
    assert(display_palette_count() == 1 + DISP_PRESET_N);
    assert(strcmp(display_preset_name(DISP_PAL_DYNAMIC), "Dynamic") == 0);
    assert(display_preset_bg(DISP_PAL_DYNAMIC)   == DISP_BG_BLACK);
    assert(display_preset_text(DISP_PAL_DYNAMIC) == DISP_TEXT_WHITE);

    /* The colour presets shifted up by one and still read correctly. */
    for (i = 0; i < DISP_PRESET_N; i++) {
        assert(display_preset_name(PAL(i))[0] != '\0');
        assert(strlen(display_preset_name(PAL(i))) <= 16);
        assert(display_preset_image(PAL(i)) == DISP_IMAGE_NONE);
    }
    /* Dynamic resolves through the category table, and holds on an event. Each
       category is asked for fresh, so this reads whichever entry of its pool is
       current rather than assuming the pool is still on its first. */
    display_set_dynamic_category(TC_TOWN);
    assert(strcmp(display_image_file(display_dynamic_slot()),
                  display_category_image(TC_TOWN)) == 0);
    {
        const char *held = display_category_image(TC_TOWN);
        display_set_dynamic_category(TC_DANGER);      /* no art: hold */
        assert(strcmp(display_image_file(display_dynamic_slot()), held) == 0);
    }
    display_set_dynamic_category(TC_SCIFI);
    assert(strcmp(display_image_file(display_dynamic_slot()),
                  display_category_image(TC_SCIFI)) == 0);
    {
        const char *held = display_category_image(TC_SCIFI);
        display_set_dynamic_category(-1);             /* out of range: hold */
        assert(strcmp(display_image_file(display_dynamic_slot()), held) == 0);
    }

    /* The title screen's shuffle reaches every picture in the pool and only ever
       lands inside it -- any value is legal, including ones far past the size. */
    {
        int n = display_category_image_count(TC_HOUSE);
        int seen[8], nseen = 0, k, j;
        assert(n >= 2 && n <= 8);
        for (k = 0; k < n * 4 + 7; k++) {
            const char *pick;
            int slot;
            display_shuffle_category(TC_HOUSE, (unsigned int) k);
            pick = display_category_image(TC_HOUSE);
            assert(pick != NULL);
            slot = -1;
            for (j = 0; j < IMGS_N; j++) if (strcmp(IMGS[j], pick) == 0) slot = j;
            assert(slot >= 0);                       /* always a real disc picture */
            /* k and k+n must agree: the reduction is modulo the pool size. */
            {
                const char *again;
                display_shuffle_category(TC_HOUSE, (unsigned int) (k + n));
                again = display_category_image(TC_HOUSE);
                assert(strcmp(pick, again) == 0);
            }
            for (j = 0; j < nseen; j++) if (seen[j] == slot) break;
            if (j == nseen && nseen < 8) seen[nseen++] = slot;
        }
        assert(nseen == n);                          /* every picture is reachable */
    }
    /* A category with no pool shrugs it off rather than dividing by its size. */
    display_shuffle_category(TC_NEUTRAL, 3);
    display_shuffle_category(TC_DANGER, 3);
    display_shuffle_category(-1, 3);
    display_shuffle_category(TEXT_NUM_CATEGORIES, 3);

    /* A house is its own mood, and lands on house art rather than the town's. */
    display_set_dynamic_category(TC_HOUSE);
    assert(strcmp(display_image_file(display_dynamic_slot()),
                  display_category_image(TC_HOUSE)) == 0);
    {
        /* ...and an unclassified room after it holds that house picture instead of
           cutting away. This is the whole reason TC_NEUTRAL carries no art: in
           these games the rooms that match no keyword ("Living Room", "Attic") are
           usually inside the building you just walked into. */
        const char *held = display_category_image(TC_HOUSE);
        display_set_dynamic_category(TC_NEUTRAL);
        assert(strcmp(display_image_file(display_dynamic_slot()), held) == 0);
    }

    /* Dynamic is the default, and its image tracks the category. */
    display_defaults(&d);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.image == display_dynamic_slot());
    assert(display_is_image(&d));
    assert(strcmp(display_palette_name(&d), "Dynamic") == 0);

    /* Cycling forward off Dynamic lands on the first colour preset, and back
       returns to it. */
    display_cycle_palette(&d, +1);
    assert(d.palette == PAL(0));
    assert(d.image == DISP_IMAGE_NONE);
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PAL_DYNAMIC);

    /* A disc whose art lacks the neutral picture still shows something, rather
       than selecting Dynamic over an empty wallpaper. */
    {
        static const char *const noneutral[] = { "WILDER1.TGA", "TOWN1.TGA" };
        display_set_images(noneutral, 2);
        assert(display_dynamic_slot() != DISP_IMAGE_NONE);
        display_defaults(&d);
        assert(d.palette == DISP_PAL_DYNAMIC);
        assert(display_is_image(&d));
    }

    /* With no art at all, Dynamic is skipped and is not the default. */
    display_set_images(NULL, 0);
    assert(display_dynamic_slot() == DISP_IMAGE_NONE);
    display_defaults(&d);
    assert(d.palette == PAL(0));
    assert(d.image == DISP_IMAGE_NONE);
    display_cycle_palette(&d, -1);            /* would land on 0 without the skip */
    assert(d.palette != DISP_PAL_DYNAMIC);

    display_set_images(IMGS, IMGS_N);              /* leave the model as we found it */
}

static void test_blob_roundtrip(void) {
    unsigned char buf[DISP_BLOB_BYTES];
    DisplayState a, b;

    display_set_images(IMGS, IMGS_N);

    /* Dynamic survives a round trip. */
    display_defaults(&a);
    assert(a.palette == DISP_PAL_DYNAMIC);
    assert(display_encode(&a, buf) == DISP_BLOB_BYTES);
    assert(buf[0] == 4);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == DISP_PAL_DYNAMIC);
    assert(b.bg == a.bg && b.text == a.text);
    assert(b.image == display_dynamic_slot());

    /* A colour preset survives a round trip in the new space. */
    a.palette = PAL(5);
    a.bg = display_preset_bg(PAL(5)); a.text = display_preset_text(PAL(5));
    a.image = DISP_IMAGE_NONE;
    display_encode(&a, buf);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == PAL(5));

    /* A picture carried alongside a colour preset still round-trips by name.
       Nothing in the UI builds that state any more -- the row has no per-picture
       entry to reach it from -- but the name field and its resolver are still in
       the format, and a legacy blob arrives through exactly this path. */
    a.palette = PAL(0);
    a.bg = DISP_BG_BLACK; a.text = DISP_TEXT_WHITE; a.image = 3;
    display_encode(&a, buf);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == PAL(0));
    assert(b.image == 3);
    assert(strcmp(display_image_file(b.image), IMGS[3]) == 0);

    /* A sentinel-3 blob written before Dynamic existed: its colour preset index
       is in the OLD space and must shift up by one, or every saved appearance
       silently moves one entry along the row. */
    {
        unsigned char old[DISP_BLOB_BYTES];
        int i;
        for (i = 0; i < DISP_BLOB_BYTES; i++) old[i] = 0;
        old[0] = 3;                          /* pre-Dynamic sentinel */
        old[1] = 5;                          /* old colour-preset index 5 */
        old[2] = (unsigned char) display_preset_bg(PAL(5));
        old[3] = (unsigned char) display_preset_text(PAL(5));
        assert(display_decode(old, DISP_BLOB_BYTES, &b) == 1);
        assert(b.palette == PAL(5));
        assert(b.image == DISP_IMAGE_NONE);
    }

    /* Dynamic reloaded onto a disc with no art has nothing to show, so it is
       refused rather than selected over an empty wallpaper. */
    display_defaults(&a);
    display_encode(&a, buf);
    display_set_images(NULL, 0);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 0);
    assert(b.palette != DISP_PAL_DYNAMIC);
    display_set_images(IMGS, IMGS_N);
}

int main(void) {
    test_tables_well_formed();
    test_known_colors();
    test_preset_contents();
    test_defaults_and_palette_name();
    test_image_registration();
    test_bg_name_and_is_image();
    test_cycle_bg_stays_in_colors();
    test_legibility_guard();
    test_guard_follows_bg_color_under_image();
    test_cycle_palette();
    test_palette_count_is_fixed();
    test_dynamic_is_the_only_entry_with_a_picture();
    test_cycle_palette_wraps_past_the_last_preset();
    test_cycle_palette_from_custom_with_images();
    test_dynamic_name_shown_not_custom();
    test_pinned_picture_blob_migrates_to_dynamic();
    test_decode_resolves_image_after_reorder();
    test_decode_follows_image_when_earlier_one_removed();
    test_decode_image_gone_falls_back();
    test_color_state_needs_no_image();
    test_legacy_blob_still_decodes();
    test_legacy_blob_image_index_rejected();
    test_decode_truncated_name_block();
    test_image_label_drops_extension_and_capitalizes();
    test_two_labels_live_at_once();
    test_selecting_dynamic_resets_bg_and_text();
    test_leaving_dynamic_clears_the_image();
    test_bg_color_under_image_survives_a_save();
    test_sentinel2_blob_decodes_over_black();
    test_encode_decode_roundtrip();
    test_collisions_roundtrip();
    test_custom_state_roundtrips();
    test_decode_rejects_bad_input();
    test_decode_missing_image_falls_back();
    test_decode_missing_image_never_clashes();
    test_decode_multi_field_corruption();
    test_category_art();
    test_category_rotation();
    test_dynamic_palette();
    test_blob_roundtrip();
    printf("test_display: OK\n");
    return 0;
}
