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
    /* This disc carries art, so display_defaults lands on Dynamic -- unreachable
       before display_defaults checked the disc's real per-mood counts via
       display_image_count(). */
    display_defaults(&d);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(d.bg == DISP_BG_BLACK);
    assert(d.text == DISP_TEXT_WHITE);
    assert(d.image == DISP_IMAGE_NONE);   /* no category set yet in this test */
    assert(strcmp(display_palette_name(&d), "Dynamic") == 0);

    /* Diverge from the preset -> Custom; restore -> the name comes back. */
    d.text = DISP_TEXT_CYAN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    d.text = DISP_TEXT_WHITE;
    assert(strcmp(display_palette_name(&d), "Dynamic") == 0);

    /* The stored index disambiguates the collision: same colors, different name. */
    d.palette = PAL(0);
    d.bg      = display_preset_bg(PAL(0));
    d.text    = display_preset_text(PAL(0));
    d.image   = DISP_IMAGE_NONE;
    assert(strcmp(display_palette_name(&d), display_preset_name(PAL(0))) == 0);
    d.palette = PAL(4);
    assert(strcmp(display_palette_name(&d), display_preset_name(PAL(4))) == 0);
}

static void test_bg_name_and_is_image(void) {
    DisplayState d;
    display_defaults(&d);
    d.palette = PAL(0);
    d.bg      = display_preset_bg(PAL(0));
    d.text    = display_preset_text(PAL(0));
    d.image   = DISP_IMAGE_NONE;
    assert(!display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "Black") == 0);

    /* The Background row keeps naming a colour even when the image field holds a
       real slot. */
    d.image = display_slot_make(TC_HOUSE, 1);
    assert(strcmp(display_bg_name(&d), "Black") == 0);
    assert(display_is_image(&d));
}

static void test_cycle_bg_stays_in_colors(void) {
    DisplayState d;
    int i;
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
    /* Dynamic (index 0) is a normal stop on the row: display_image_count() reads
       the disc's real per-mood counts, and this disc's art makes it reachable --
       nothing skips it any more. */
    display_defaults(&d);                  /* Dynamic */
    assert(d.palette == DISP_PAL_DYNAMIC);

    display_cycle_palette(&d, 1);
    assert(d.palette == PAL(0));
    assert(d.bg == display_preset_bg(PAL(0)) && d.text == display_preset_text(PAL(0)));
    assert(strcmp(display_palette_name(&d), "IBM PC (MDA)") == 0);

    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PAL_DYNAMIC);
    assert(strcmp(display_palette_name(&d), "Dynamic") == 0);

    /* Wraps at both ends; Dynamic is one of the stops, not skipped. */
    d.palette = PAL(DISP_PRESET_N - 1);    /* last colour preset */
    d.bg = display_preset_bg(d.palette); d.text = display_preset_text(d.palette);
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PAL_DYNAMIC);
    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));

    /* From a Custom state, cycling steps off the palette the custom was built on
       rather than re-entering at an end of the row. Forward from a custom of
       PAL(0) is therefore PAL(1), NOT PAL(0) again -- landing back on the base
       would spend the press undoing the player's colours and go nowhere. Built by
       hand rather than through display_defaults, which now lands on Dynamic. */
    d.palette = PAL(0);
    d.bg      = display_preset_bg(PAL(0));
    d.text    = DISP_TEXT_CYAN;            /* now Custom, base PAL(0) */
    d.image   = DISP_IMAGE_NONE;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_cycle_palette(&d, 1);
    assert(d.palette == PAL(1));
    assert(d.bg == display_preset_bg(PAL(1)) && d.text == display_preset_text(PAL(1)));

    /* Backward off that same base steps onto Dynamic, PAL(0)'s neighbour on the
       row now that this disc has art to show for it. */
    d.palette = PAL(0);
    d.bg      = display_preset_bg(PAL(0));
    d.text    = DISP_TEXT_CYAN;
    d.image   = DISP_IMAGE_NONE;
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PAL_DYNAMIC);

    /* One press always leaves Custom behind, from either direction. */
    d.palette = PAL(0);
    d.bg      = display_preset_bg(PAL(0));
    d.text    = DISP_TEXT_CYAN;
    d.image   = DISP_IMAGE_NONE;
    display_cycle_palette(&d, 1);
    assert(strcmp(display_palette_name(&d), "Custom") != 0);

    /* Selecting a preset always yields a legible pair. */
    {
        int i;
        for (i = DISP_PAL_PRESET0; i <= DISP_PRESET_N; i++) {
            assert(display_bg_rgb(display_preset_bg(i))
                != display_text_rgb(display_preset_text(i)));
        }
    }
}

static void test_custom_on_dynamic_steps_forward(void) {
    /* The regression display_cycle_palette's own comment describes: a Custom
       built on Dynamic (index 0) must step onto PAL(0)/PAL(last), not re-select
       Dynamic and wipe the player's colours back to black and white. This disc
       carries art, so display_defaults lands on Dynamic and Custom-on-Dynamic is
       reachable. */
    DisplayState d;

    display_defaults(&d);
    assert(d.palette == DISP_PAL_DYNAMIC);
    d.text = DISP_TEXT_CYAN;               /* diverged -> Custom, base Dynamic */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);

    display_cycle_palette(&d, 1);
    assert(d.palette == PAL(0));
    assert(d.bg == display_preset_bg(PAL(0)) && d.text == display_preset_text(PAL(0)));

    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;
    display_cycle_palette(&d, -1);
    assert(d.palette == PAL(DISP_PRESET_N - 1));
    assert(d.bg == display_preset_bg(PAL(DISP_PRESET_N - 1))
        && d.text == display_preset_text(PAL(DISP_PRESET_N - 1)));
}

/* The MOJOOPTS blob locates its display block by finding a byte that is not one
   of this decoder's sentinels: options.cxx puts a gameplay block in front of it
   marked 5, and reads that byte to decide whether the display block starts there
   or two bytes later. That only works while 5 stays outside the sentinel space,
   so a future format bump has to skip it -- taking 5 here would make every blob
   written since decode as a display block two bytes early, silently. */
static void test_five_is_not_a_display_sentinel(void) {
    unsigned char blob[DISP_BLOB_BYTES];
    DisplayState d;
    int i;
    for (i = 0; i < DISP_BLOB_BYTES; i++) blob[i] = 0;
    blob[0] = 5;
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);
}

static void test_encode_decode_roundtrip(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int n;

    display_defaults(&a);
    a.palette = PAL(6); a.bg = display_preset_bg(PAL(6)); a.text = display_preset_text(PAL(6));

    n = display_encode(&a, buf);
    assert(n == DISP_BLOB_BYTES);
    assert(buf[0] == 6);            /* current sentinel: + dim (was 4 before Task 6) */

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

    /* Built on a colour preset by hand rather than through display_defaults,
       which now lands on Dynamic -- the case that already has its own coverage
       in test_blob_roundtrip. */
    a.palette = PAL(0);
    a.bg      = display_preset_bg(PAL(0));
    a.text    = DISP_TEXT_CYAN;                   /* diverged -> Custom */
    a.image   = DISP_IMAGE_NONE;
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

static void test_decode_multi_field_corruption(void) {
    DisplayState d, def;
    unsigned char buf[DISP_BLOB_BYTES];

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
    /* The row used to grow by one per picture. It does not any more: thirty-
       seven of them made a fifty-four-entry cycler where most steps read the
       disc. Every picture is still reachable, through the mood that owns it. */
    assert(display_palette_count() == 1 + DISP_PRESET_N);
}

static void test_decode_missing_image_falls_back(void) {
    /* A name that cannot resolve, paired with a background/text pair that
       clashes once the picture behind it is gone: decode must still land on a
       legible pair, restored from the saved palette's own preset, with the
       palette byte itself surviving untouched. ZX Spectrum (Light Gray/Black)
       is the concrete case pinned here. Built by hand rather than round-tripped
       through a shrinking disc, since the disc's art no longer varies at
       runtime -- an unresolvable name is what "the picture is gone" now looks
       like. */
    unsigned char blob[DISP_BLOB_BYTES];
    DisplayState b;
    static const char *const name = "GONE.TGA";
    int i;

    for (i = 0; i < DISP_BLOB_BYTES; i++) blob[i] = 0;
    blob[0] = 4;
    blob[1] = (unsigned char) PAL(11);      /* ZX Spectrum */
    blob[2] = DISP_BG_BLACK;
    blob[3] = DISP_TEXT_BLACK;              /* clashes, image or not */
    for (i = 0; name[i]; i++) blob[4 + i] = (unsigned char) name[i];

    assert(display_decode(blob, DISP_BLOB_BYTES, &b) == 0);   /* reports the fallback */
    assert(!display_is_image(&b));
    assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
    assert(b.palette == PAL(11));              /* palette byte survived */
    assert(b.bg == DISP_BG_LIGHT_GRAY);         /* restored from ZX Spectrum's own pair */
    assert(b.text == DISP_TEXT_BLACK);          /* restored from ZX Spectrum's own pair */
}

static void test_decode_missing_image_never_clashes(void) {
    /* Any accepted palette plus a background/text pair that happens to clash
       must still decode to a legible pair -- restored from that palette's own
       preset. Black and Green are the two colors present in both the background
       and text tables, so they are what can collide. This no longer needs an
       image or a shrinking disc to set up: the clash check at the end of
       display_decode is unconditional, so a hand-built sentinel-1 block already
       exercises it. */
    static const int clashing_texts[2] = { DISP_TEXT_BLACK, DISP_TEXT_GREEN };
    DisplayState b;
    unsigned char buf[4];
    int p, t;

    for (p = 0; p < DISP_PRESET_N; p++) {
        for (t = 0; t < 2; t++) {
            buf[0] = 1;
            buf[1] = (unsigned char) p;
            buf[2] = DISP_BG_BLACK;
            buf[3] = (unsigned char) clashing_texts[t];
            display_decode(buf, 4, &b);
            assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
        }
    }
}

static void test_color_state_needs_no_image(void) {
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

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
    DisplayState d, def;

    display_defaults(&def);
    legacy[0] = 1;
    legacy[1] = DISP_PRESET_N + 1;          /* an image slot in the old numbering */
    legacy[2] = 0xFF;                       /* sentinel-1 form: out-of-range bg */
    legacy[3] = DISP_TEXT_WHITE;
    assert(display_decode(legacy, 4, &d) == 0);
    /* What must not happen is the slot byte being honoured as a picture choice. */
    assert(d.palette == def.palette);
    assert(d.image   == def.image);
}

static void test_decode_truncated_name_block(void) {
    DisplayState d, def, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_defaults(&def);
    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLACK;
    saved.image   = display_slot_make(TC_HOUSE, 1);
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Sentinel says a name follows, but the block is cut short. Nothing in it is
       trustworthy, so the whole thing is refused and the defaults stand. */
    assert(display_decode(blob, DISP_BLOB_BYTES - 1, &d) == 0);
    assert(d.palette == def.palette);
    assert(d.image   == def.image);
}

/* --- images as their own field --------------------------------------------- */

static void test_bg_color_under_image_survives_a_save(void) {
    /* The color beneath a picture is what shows through the menu frames, so it
       has to round-trip independently of the picture -- unlike the picture
       itself, which display_encode no longer stores for a colour preset (see
       its comment): the image field comes back DISP_IMAGE_NONE regardless of
       what was saved. */
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    saved.palette = PAL(0);
    saved.bg      = DISP_BG_BLUE;           /* deliberately not the preset's black */
    saved.text    = DISP_TEXT_WHITE;
    saved.image   = display_slot_make(TC_HOUSE, 1);
    display_encode(&saved, blob);

    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.image == DISP_IMAGE_NONE);
    assert(d.bg    == DISP_BG_BLUE);
    assert(d.text  == DISP_TEXT_WHITE);
    /* Diverged from the preset's black, so it reads Custom. */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
}

/* --- the mood -> picture table ---------------------------------------------
   Kept in step with cd/data/TGA by saturn/tests/test_category_art.py, which
   checks category_art.inc's counts against the folders actually on the disc. */

static void test_category_art(void) {
    int cat, named = 0;
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        const char *f = display_category_image(cat);
        if (f == NULL) continue;
        named++;
        assert(f[0] != '\0');
        /* Not DISP_IMAGE_NAME_MAX-1: an 8-letter mood's path ("UNDRGRND/07.TGA")
           is 15 characters, longer than the save-blob field now reserves (see
           DISP_IMAGE_NAME_MAX in display.h). 15 is what the synthesis buffer
           itself allows. */
        assert(strlen(f) <= 15);
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
       model, but it means the track rotates under an unchanged wallpaper --
       which looks exactly like the art side being broken, so it is pinned here
       rather than left to be noticed. */
    for (cat = 0; cat < TEXT_NUM_CATEGORIES; cat++) {
        if (display_category_image(cat) == NULL) continue;
        assert(display_category_image_count(cat) >= 2);
    }
}

static void test_virtual_slots(void) {
    /* Indices are checked against the count this disc actually carries, so the
       test does not go stale every time a mood gains a picture. */
    int n    = display_category_image_count(TC_HORROR);
    int slot = display_slot_make(TC_HORROR, n);
    assert(n >= 1 && n <= 99);   /* the two-digit filename below needs both ends */
    assert(slot == TC_HORROR * 100 + n);
    assert(display_slot_valid(slot));
    {
        char want[16];
        snprintf(want, sizeof(want), "HORROR/%02d.TGA", n);
        assert(strcmp(display_image_file(slot), want) == 0);
        assert(display_image_slot(want) == slot);
    }

    /* Index 0 is never a filename, one past the end is not carried, and the
       sparse space rejects between moods. */
    assert(display_slot_make(TC_HORROR, 0) == DISP_IMAGE_NONE);
    assert(display_slot_make(TC_HORROR, n + 1) == DISP_IMAGE_NONE);
    assert(!display_slot_valid(TC_HORROR * 100));
    assert(!display_slot_valid(-1));

    /* Categories that carry no art reject every index. */
    assert(display_slot_make(TC_NEUTRAL, 1) == DISP_IMAGE_NONE);
    assert(display_slot_make(TC_DANGER, 1)  == DISP_IMAGE_NONE);

    /* An old blob's flat name no longer resolves, which is the intended miss. */
    assert(display_image_slot("HOUSE1.TGA") == DISP_IMAGE_NONE);
    assert(display_image_slot("") == DISP_IMAGE_NONE);
    assert(display_image_slot(NULL) == DISP_IMAGE_NONE);

    /* Two live filenames at once: display_image_file must rotate its buffers. */
    {
        const char *a = display_image_file(display_slot_make(TC_HOUSE, 1));
        const char *b = display_image_file(display_slot_make(TC_TOWN, 2));
        assert(strcmp(a, "HOUSE/01.TGA") == 0);
        assert(strcmp(b, "TOWN/02.TGA") == 0);
    }
}

static void test_rotate_dynamic_category(void) {
    /* The branch's headline behaviour: walking a category's pool never lands
       back on the slot already showing. TC_HORROR is guaranteed >= 2 pictures by
       test_category_art, so every step must move. Observed through g_dyn_slot
       (no pin set), the field display_rotate_dynamic_category actually writes. */
    int cat = TC_HORROR;
    int n   = display_category_image_count(cat);
    int prev, i;
    assert(n >= 2);

    display_pin_dynamic_slot(DISP_IMAGE_NONE);
    display_set_dynamic_category(cat);
    prev = display_dynamic_slot();
    assert(display_slot_valid(prev));

    for (i = 0; i < n * 3; i++) {
        int now;
        display_rotate_dynamic_category(cat);
        now = display_dynamic_slot();
        assert(now != prev);          /* never re-lands on the current slot */
        assert(display_slot_valid(now));
        prev = now;
    }

    /* No-op when the pool cannot offer a different picture. A literal
       one-picture pool cannot occur on this disc -- test_category_art pins every
       category with art at >= 2 -- but TC_DANGER (no art at all, n == 0) shares
       the rotate function's n <= 1 guard and is the only reachable way to
       exercise it. */
    assert(display_category_image_count(TC_DANGER) == 0);
    display_rotate_dynamic_category(TC_DANGER);
    assert(display_dynamic_slot() == prev);
}

static void test_blob_roundtrip(void) {
    unsigned char buf[DISP_BLOB_BYTES];
    DisplayState a, b;

    /* A colour preset survives a round trip in the new space. */
    a.palette = PAL(5);
    a.bg = display_preset_bg(PAL(5)); a.text = display_preset_text(PAL(5));
    a.image = DISP_IMAGE_NONE;
    display_encode(&a, buf);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == PAL(5));

    /* A picture carried alongside a colour preset does NOT round-trip any
       more: no UI path can produce that combination (the Palette row offers
       only Dynamic and colour presets, and a colour preset carries
       DISP_IMAGE_NONE), so display_encode writes the name field empty
       regardless of what d->image holds. The blob still round-trips through
       decode -- palette, bg and text are unaffected, only the image is
       silently dropped, on purpose. */
    a.palette = PAL(0);
    a.bg = DISP_BG_BLACK; a.text = DISP_TEXT_WHITE;
    a.image = display_slot_make(TC_MAGIC, 2);
    display_encode(&a, buf);
    {
        int i;
        for (i = 0; i < DISP_IMAGE_NAME_MAX; i++) assert(buf[5 + i] == 0);   /* name field: after dim */
    }
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == PAL(0));
    assert(b.image == DISP_IMAGE_NONE);

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

    /* A blob carrying the Dynamic marker decodes cleanly now that this disc's
       art makes Dynamic reachable through display_defaults again. */
    a.palette = DISP_PAL_DYNAMIC;
    a.bg = DISP_BG_BLACK; a.text = DISP_TEXT_WHITE; a.image = DISP_IMAGE_NONE;
    display_encode(&a, buf);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.palette == DISP_PAL_DYNAMIC);
}

static void test_preset_sweep_round_trips(void) {
    /* Every colour preset index, not just samples, must survive the current
       save form -- including the top of the row, PAL(DISP_PRESET_N), which the
       sentinel-6 decode's off-by-one (buf[1] < DISP_PRESET_N instead of <=)
       silently defaulted to Dynamic instead of rejecting or accepting. */
    int i;
    for (i = DISP_PAL_PRESET0; i <= DISP_PRESET_N; i++) {
        DisplayState a, b;
        unsigned char buf[DISP_BLOB_BYTES];

        a.palette = i;
        a.bg      = display_preset_bg(i);
        a.text    = display_preset_text(i);
        a.image   = DISP_IMAGE_NONE;
        display_encode(&a, buf);
        assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
        assert(b.palette == i);
        assert(b.bg == a.bg && b.text == a.text);
    }
}

static void test_sentinel_2_decode(void) {
    /* Of five historical save forms (1, 2, 3, 4, 6) no test decoded a sentinel-2
       blob before this: the oldest form that names a picture, and the one that
       packs the image into the bg byte (DISP_BLOB_IMAGE, 0xFF) instead of giving
       it a field of its own -- see the sentinel 2/3/4 branch's comment. The
       color beneath the picture was never saved in this form and comes back
       black. */
    unsigned char old[17];
    DisplayState d;
    static const char *const path = "HOUSE/01.TGA";
    int slot = display_slot_make(TC_HOUSE, 1);
    int i;

    for (i = 0; i < 17; i++) old[i] = 0;
    old[0] = 2;                              /* pre-Dynamic, image-packed-in-bg form */
    old[1] = 3;                              /* old-space preset index -> PAL(3) */
    old[2] = 0xFF;                           /* DISP_BLOB_IMAGE: the image marker */
    old[3] = DISP_TEXT_WHITE;
    for (i = 0; path[i]; i++) old[4 + i] = (unsigned char) path[i];

    assert(display_decode(old, 17, &d) == 1);
    assert(d.palette == PAL(3));
    assert(d.bg    == DISP_BG_BLACK);
    assert(d.text  == DISP_TEXT_WHITE);
    assert(d.image == slot);
    assert(d.dim   == DISP_DIM_NORMAL);
}

static void test_sparse_slot_space(void) {
    DisplayState d;
    int good = display_slot_make(TC_HOUSE, 1);
    int gap  = TC_HOUSE * 100;              /* index 0: never a file */

    /* The disc carries art, so the default appearance must be Dynamic -- before
       display_image_count() read the disc's real per-mood counts, every "any art
       at all" question answered no, and no room picture ever appeared. */
    assert(display_image_count() > 0);
    display_defaults(&d);
    assert(d.palette == DISP_PAL_DYNAMIC);

    d.image = good;
    assert(display_is_image(&d));           /* real slot, valid by lookup */
    d.image = gap;
    assert(!display_is_image(&d));          /* a naive < count test passes this */
    d.image = DISP_IMAGE_NONE;
    assert(!display_is_image(&d));

    display_pin_dynamic_slot(gap);
    assert(display_dynamic_slot() != gap);
    display_pin_dynamic_slot(good);
    assert(display_dynamic_slot() == good);
    display_pin_dynamic_slot(DISP_IMAGE_NONE);
}

static void test_dim_table_and_blob(void) {
    DisplayState d, r;
    unsigned char buf[DISP_BLOB_BYTES];
    int i;

    assert(DISP_DIM_N == 7);
    assert(display_dim_offset(DISP_DIM_NORMAL) == 0);
    assert(display_dim_offset(0) ==  64);
    assert(display_dim_offset(1) ==  32);
    assert(display_dim_offset(3) == -32);
    assert(display_dim_offset(6) == -128);
    for (i = 0; i < DISP_DIM_N; i++) assert(display_dim_name(i)[0] != '\0');
    assert(display_dim_offset(-1) == 0);
    assert(display_dim_offset(DISP_DIM_N) == 0);

    display_defaults(&d);
    assert(d.dim == DISP_DIM_NORMAL);

    d.dim = 6;
    assert(display_encode(&d, buf) == DISP_BLOB_BYTES);
    /* Sentinel is 6, not 5: byte 5 at this position in the save is reserved for
       options.cxx's gameplay-block marker (see test_five_is_not_a_display_sentinel
       above), and 5 here would have collided with it. See DISP_BLOB_BYTES's
       comment in display.h. */
    assert(buf[0] == 6);
    assert(buf[4] == 6);
    assert(display_decode(buf, DISP_BLOB_BYTES, &r) == 1);
    assert(r.dim == 6);

    /* An out-of-range dim is defaulted, not trusted. */
    buf[4] = 99;
    assert(display_decode(buf, DISP_BLOB_BYTES, &r) == 0);
    assert(r.dim == DISP_DIM_NORMAL);
}

static void test_cycle_dim(void) {
    DisplayState d;
    int i;

    display_defaults(&d);
    assert(d.dim == DISP_DIM_NORMAL);

    /* Steps through every stop and wraps back to where it started. */
    for (i = 0; i < DISP_DIM_N; i++) {
        display_cycle_dim(&d, 1);
        assert(d.dim >= 0 && d.dim < DISP_DIM_N);
    }
    assert(d.dim == DISP_DIM_NORMAL);

    /* Wraps at the low end too. */
    d.dim = 0;
    display_cycle_dim(&d, -1);
    assert(d.dim == DISP_DIM_N - 1);
    display_cycle_dim(&d, 1);
    assert(d.dim == 0);
}

static void test_old_blobs_get_no_dim(void) {
    DisplayState d;
    unsigned char old[17];
    int i;

    for (i = 0; i < 17; i++) old[i] = 0;
    old[0] = 4;
    old[1] = DISP_PAL_PRESET0;
    old[2] = DISP_BG_BLACK;
    old[3] = DISP_TEXT_WHITE;

    /* A sentinel-4 blob is 17 bytes where a sentinel-6 blob is DISP_BLOB_BYTES
       (18). The old branch must keep measuring against the OLD size, or growing
       DISP_BLOB_BYTES silently rejects every save file already on a memory
       card. */
    assert(display_decode(old, 17, &d) == 1);
    assert(d.palette == DISP_PAL_PRESET0);
    assert(d.bg   == DISP_BG_BLACK);
    assert(d.text == DISP_TEXT_WHITE);
    assert(d.dim  == DISP_DIM_NORMAL);

    old[0] = 1;
    display_decode(old, 4, &d);
    assert(d.dim == DISP_DIM_NORMAL);
}

int main(void) {
    test_tables_well_formed();
    test_known_colors();
    test_preset_contents();
    test_defaults_and_palette_name();
    test_bg_name_and_is_image();
    test_cycle_bg_stays_in_colors();
    test_legibility_guard();
    test_guard_follows_bg_color_under_image();
    test_cycle_palette();
    test_custom_on_dynamic_steps_forward();
    test_five_is_not_a_display_sentinel();
    test_encode_decode_roundtrip();
    test_collisions_roundtrip();
    test_custom_state_roundtrips();
    test_decode_rejects_bad_input();
    test_decode_multi_field_corruption();
    test_palette_count_is_fixed();
    test_decode_missing_image_falls_back();
    test_decode_missing_image_never_clashes();
    test_color_state_needs_no_image();
    test_legacy_blob_still_decodes();
    test_legacy_blob_image_index_rejected();
    test_decode_truncated_name_block();
    test_bg_color_under_image_survives_a_save();
    test_category_art();
    test_virtual_slots();
    test_rotate_dynamic_category();
    test_blob_roundtrip();
    test_preset_sweep_round_trips();
    test_sentinel_2_decode();
    test_sparse_slot_space();
    test_dim_table_and_blob();
    test_cycle_dim();
    test_old_blobs_get_no_dim();
    printf("test_display: OK\n");
    return 0;
}
