/* Build:
     gcc -O2 -I saturn/src -o /tmp/tdm saturn/tests/test_dash_map.c \
         saturn/src/video/dash_map.c && /tmp/tdm
   dash_map.c is deliberately free of SRL includes so this links on the host. */
#include "../src/video/dash_map.h"
#include <assert.h>
#include <stdio.h>

static int row_all(int y, unsigned char t) {
    int x;
    for (x = 0; x < DASH_COLS; x++) if (dash_cell(x, y) != t) return 0;
    return 1;
}

int main(void) {
    int x, y;

    dash_reset();

    /* Nothing is painted before the first build. */
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));
    assert(dash_dirty_bottom() < dash_dirty_top());

    /* An out-of-range variant is ignored rather than trusted. */
    dash_build(99, 19);
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    /* The command panel: nine rows from 19 to 27, closing at column 39. */
    dash_build(DASH_PANEL, 19);
    assert(dash_cell(0, 19)  == DT_CORNER_TL);
    assert(dash_cell(39, 19) == DT_CORNER_TR);
    assert(dash_cell(0, 27)  == DT_CORNER_BL);
    assert(dash_cell(39, 27) == DT_CORNER_BR);
    assert(row_all(18, DT_BLANK));
    assert(row_all(28, DT_BLANK));

    /* Every frame tile carries the field's marble behind it, so a horizontal
       run picks its tile by column and a vertical one by row. */
    assert(dash_cell(20, 19) == DT_TOP0 + (20 & 3));
    assert(dash_cell(21, 19) == DT_TOP0 + (21 & 3));
    assert(dash_cell(20, 27) == DT_BOTTOM0 + (20 & 3));
    assert(dash_cell(0, 22)  == DT_LEFT0 + (22 & 3));
    assert(dash_cell(0, 23)  == DT_LEFT0 + (23 & 3));
    assert(dash_cell(39, 22) == DT_RIGHT0 + (22 & 3));

    /* The field is addressed by the cell's own screen coordinates, so the
       stone is continuous across the panel and repeats every four cells. */
    assert(dash_cell(20, 22) == DT_FIELD0 + ((22 & 3) << 2) + (20 & 3));
    assert(dash_cell(24, 26) == DT_FIELD0 + ((26 & 3) << 2) + (24 & 3));
    assert(dash_cell(20, 22) == dash_cell(24, 26));

    /* Each module is its own box. A divider column closes the module on its
       left and opens the one on its right; the ten pixels that takes do not fit
       in one cell: the divider carries the left module's frame and the gap, and
       the module after it opens with a frame of its own in the next cell. */
    for (y = 20; y <= 26; y++) {
        assert(dash_cell(14, y) == DT_DIVIDER0 + (y & 3));
        assert(dash_cell(30, y) == DT_DIVIDER0 + (y & 3));
        assert(dash_cell(13, y) == DT_FIELD0 + ((y & 3) << 2) + (13 & 3));
        assert(dash_cell(15, y) == DT_MODLEFT0 + (y & 3));
        assert(dash_cell(31, y) == DT_MODLEFT0 + (y & 3));
    }
    assert(dash_cell(14, 19) == DT_TOP_DIVIDER);
    assert(dash_cell(14, 27) == DT_BOTTOM_DIVIDER);
    assert(dash_cell(13, 19) == DT_TOP0 + (13 & 3));
    assert(dash_cell(15, 19) == DT_TOP_MODLEFT);
    assert(dash_cell(13, 27) == DT_BOTTOM0 + (13 & 3));
    assert(dash_cell(15, 27) == DT_BOTTOM_MODLEFT);

    /* The command panel has no inner rule. */
    assert(dash_cell(20, 22) == DT_FIELD0 + ((22 & 3) << 2) + (20 & 3));

    /* The dirty span is the nine rows painted. */
    assert(dash_dirty_top() == 19);
    assert(dash_dirty_bottom() == 27);

    /* A repeat build with the same variant and base changes nothing. */
    dash_dirty_clear();
    dash_build(DASH_PANEL, 19);
    assert(dash_dirty_bottom() < dash_dirty_top());
    assert(dash_cell(0, 19) == DT_CORNER_TL);

    /* The keyboard strip closes one column earlier, and column 39 stays clear. */
    dash_build(DASH_GAMEKB, 19);
    assert(dash_cell(38, 19) == DT_CORNER_TR);
    assert(dash_cell(38, 27) == DT_CORNER_BR);
    assert(dash_cell(38, 21) == DT_RIGHT0 + (21 & 3));
    for (y = 19; y <= 27; y++) assert(dash_cell(39, y) == DT_BLANK);

    /* It has one divider and one inner rule, at screen row 22 -- content row 2,
       where render_game_keyboard used to print "-----". The rule belongs to the
       module right of the divider and stops at its frame. */
    assert(dash_cell(14, 22) == DT_DIVIDER0 + (22 & 3));
    assert(dash_cell(15, 22) == DT_RULE_MODLEFT);
    for (x = 16; x <= 37; x++) assert(dash_cell(x, 22) == DT_RULE0 + (x & 3));
    assert(dash_cell(38, 22) == DT_RULE_RIGHT);

    /* Left of the divider the rule row is ordinary content, so the rose keeps
       its marble and its left frame. */
    assert(dash_cell(0, 22) == DT_LEFT0 + (22 & 3));
    assert(dash_cell(7, 22) == DT_FIELD0 + ((22 & 3) << 2) + (7 & 3));

    /* The overlay is the panel's rectangle with no dividers at all, so the
       inventory box drawn over it meets one unbroken field. */
    dash_build(DASH_OVERLAY, 19);
    assert(dash_cell(0, 19)  == DT_CORNER_TL);
    assert(dash_cell(39, 19) == DT_CORNER_TR);
    for (y = 20; y <= 26; y++)
        for (x = 1; x <= 38; x++)
            assert(dash_cell(x, y) == DT_FIELD0 + ((y & 3) << 2) + (x & 3));

    /* The tall overlay is that rectangle five rows taller and split once, at
       column 27: the item list closes there and the picture module opens in
       column 28. That seam is the border between the list and the picture. */
    dash_dirty_clear();
    dash_build(DASH_OVERLAY_TALL, 16);
    assert(dash_cell(0, 16)  == DT_CORNER_TL);
    assert(dash_cell(39, 16) == DT_CORNER_TR);
    assert(dash_cell(0, 29)  == DT_CORNER_BL);
    assert(dash_cell(39, 29) == DT_CORNER_BR);
    assert(dash_cell(27, 16) == DT_TOP_DIVIDER);
    assert(dash_cell(27, 29) == DT_BOTTOM_DIVIDER);
    assert(dash_cell(28, 16) == DT_TOP_MODLEFT);
    assert(dash_cell(28, 29) == DT_BOTTOM_MODLEFT);
    for (y = 17; y <= 28; y++) {
        assert(dash_cell(0, y)  == DT_LEFT0 + (y & 3));
        assert(dash_cell(39, y) == DT_RIGHT0 + (y & 3));
        assert(dash_cell(27, y) == DT_DIVIDER0 + (y & 3));
        assert(dash_cell(28, y) == DT_MODLEFT0 + (y & 3));
        /* The list module's interior is one unbroken field, so the item list
           meets marble on every side. */
        for (x = 1; x <= 26; x++)
            assert(dash_cell(x, y) == DT_FIELD0 + ((y & 3) << 2) + (x & 3));
    }

    /* The picture module's interior is not: its outermost ring is a second
       frame, facing inward, closing hard against the eight by ten cells the
       64x80 picture occupies. Those cells stay field -- NBG1 draws over them. */
    assert(dash_cell(29, 17) == DT_PIC_TL);
    assert(dash_cell(38, 17) == DT_PIC_TR);
    assert(dash_cell(29, 28) == DT_PIC_BL);
    assert(dash_cell(38, 28) == DT_PIC_BR);
    for (x = 30; x <= 37; x++) {
        assert(dash_cell(x, 17) == DT_PIC_TOP0 + (x & 3));
        assert(dash_cell(x, 28) == DT_PIC_BOTTOM0 + (x & 3));
    }
    for (y = 18; y <= 27; y++) {
        assert(dash_cell(29, y) == DT_PIC_LEFT0 + (y & 3));
        assert(dash_cell(38, y) == DT_PIC_RIGHT0 + (y & 3));
        for (x = 30; x <= 37; x++)
            assert(dash_cell(x, y) == DT_FIELD0 + ((y & 3) << 2) + (x & 3));
    }
    assert(dash_dirty_top() == 16);
    assert(dash_dirty_bottom() == 29);
    assert(dash_input_up() == 1);

    /* Moving the panel clears the rows it left. */
    dash_build(DASH_PANEL, 19);
    dash_build(DASH_PANEL, 15);
    for (y = 24; y <= 27; y++) assert(row_all(y, DT_BLANK));
    assert(dash_cell(0, 15) == DT_CORNER_TL);

    /* A frame in which a renderer claimed the panel leaves it up. */
    dash_build(DASH_PANEL, 19);
    dash_dirty_clear();
    dash_frame_end();
    assert(dash_cell(0, 19) == DT_CORNER_TL);
    assert(dash_dirty_bottom() < dash_dirty_top());

    /* A frame in which nobody claimed it takes it down -- this is what keeps
       the marble from sitting behind a menu or the title screen. */
    dash_frame_end();
    for (y = 19; y <= 27; y++) assert(row_all(y, DT_BLANK));
    assert(dash_dirty_top() == 19);
    assert(dash_dirty_bottom() == 27);

    /* Once down it stays down, and costs nothing to keep down. */
    dash_dirty_clear();
    dash_frame_end();
    assert(dash_dirty_bottom() < dash_dirty_top());

    /* And it comes back when a renderer claims it again. */
    dash_build(DASH_GAMEKB, 19);
    dash_frame_end();
    assert(dash_cell(0, 19) == DT_CORNER_TL);
    assert(dash_cell(38, 19) == DT_CORNER_TR);

    /* A renderer redrawing the same variant and base every frame keeps the
       panel up: the idempotent early return must set g_touched too, not just
       the full repaint path, or the panel would be torn down underneath a
       renderer that never changed what it was drawing. The first build here
       is a genuine repaint (the panel was left showing DASH_GAMEKB above), so
       it is the second -- the one that lands on dash_build's idempotent early
       return -- whose g_touched matters to the frame_end that follows it. */
    dash_build(DASH_PANEL, 19);
    dash_frame_end();
    dash_build(DASH_PANEL, 19);
    dash_frame_end();
    assert(dash_cell(0, 19) == DT_CORNER_TL);
    assert(dash_cell(39, 19) == DT_CORNER_TR);

    /* dash_box_hold keeps a box up across a frame nobody redraws it on. A menu
       that has finished drawing does not draw again -- menu_message paints once
       and menu_wait then holds the screen until a key arrives. */
    dash_reset();
    dash_box(6, 4, 12, 8);
    dash_frame_end();                      /* claimed by the dash_box above */
    dash_box_hold();
    dash_frame_end();
    assert(dash_cell(6, 4) == DT_CORNER_TL);
    dash_box_hold();
    dash_frame_end();
    assert(dash_cell(6, 4) == DT_CORNER_TL);

    /* A menu box is a marble slab, not an outline: the same bevelled frame the
       gamepad panel wears and the same field inside it, so a menu does not show
       the wallpaper or the back colour through its middle. It used to paint the
       DT_BOX_* set over DT_BLANK, and nothing pins that it no longer does
       except this. The field tile is chosen by the cell's own coordinates, so
       the stone repeats every 32 pixels rather than every 8 -- check two
       interior cells a phase apart, or a constant would satisfy this. */
    {
        int bx = 6, by = 4, bw = 12, bh = 8;
        int ix, iy;
        assert(dash_cell(bx + bw - 1, by) == DT_CORNER_TR);
        assert(dash_cell(bx, by + bh - 1) == DT_CORNER_BL);
        assert(dash_cell(bx + bw - 1, by + bh - 1) == DT_CORNER_BR);
        assert(dash_cell(bx + 1, by) == (unsigned char) (DT_TOP0 + ((bx + 1) & 3)));
        assert(dash_cell(bx, by + 1) == (unsigned char) (DT_LEFT0 + ((by + 1) & 3)));
        for (iy = by + 1; iy < by + bh - 1; iy++)
            for (ix = bx + 1; ix < bx + bw - 1; ix++)
                assert(dash_cell(ix, iy)
                       == (unsigned char) (DT_FIELD0 + ((iy & 3) << 2) + (ix & 3)));
        assert(dash_cell(bx + 1, by + 1) != dash_cell(bx + 2, by + 1));
    }

    /* Without the hold it expires, which is what makes the hold load-bearing
       rather than decorative. */
    dash_frame_end();
    assert(dash_cell(6, 4) == DT_BLANK);

    /* The hold asks the layer what is on it, NOT whether a menu is open. That
       distinction is the whole bug it was written for: online_mode holds one
       MenuBacking for a whole telnet session, so a hold keyed on the backing
       refcount went on re-claiming the dial screen's box for the rest of the
       session and fought the strip for the layer every frame. Once the strip
       has claimed, a hold must do nothing at all. */
    dash_reset();
    dash_box(6, 4, 12, 8);
    dash_build(DASH_PANEL, 19);
    dash_box_hold();
    dash_frame_end();
    assert(dash_cell(0, 19) == DT_CORNER_TL);   /* strip still up */
    assert(dash_cell(6, 4) == DT_BLANK);        /* stale box did not return */
    for (y = 0; y < 6; y++) {
        dash_build(DASH_PANEL, 19);
        dash_box_hold();
        dash_frame_end();
        assert(dash_cell(0, 19) == DT_CORNER_TL);
        assert(dash_cell(6, 4) == DT_BLANK);
    }

    /* And a hold with nothing painted stays nothing painted. */
    dash_reset();
    dash_box_hold();
    dash_frame_end();
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    /* The map paints individual cells rather than one rectangle, so it gets
       its own claim: begin clears, paint sets, and nothing else survives. */
    dash_reset();
    dash_build(DASH_PANEL, 19);
    dash_map_begin();
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    dash_map_paint(4, 8, DT_ROOM);
    dash_map_paint(4, 12, DT_ROOM_HERE);
    dash_map_paint(4, 10, DT_LINK_V);
    assert(dash_cell(4, 8)  == DT_ROOM);
    assert(dash_cell(4, 12) == DT_ROOM_HERE);
    assert(dash_cell(4, 10) == DT_LINK_V);
    assert(dash_cell(5, 8)  == DT_BLANK);

    /* Out-of-range paints are dropped rather than trusted, so a view clipping
       at the screen edge needs no bounds test of its own. */
    dash_map_paint(-1, 5, DT_ROOM);
    dash_map_paint(DASH_COLS, 5, DT_ROOM);
    dash_map_paint(3, DASH_ROWS, DT_ROOM);
    assert(dash_cell(3, 5) == DT_BLANK);

    /* Leaving the map for another variant must erase every cell
       dash_map_paint could have touched, not just the rows the next variant
       repaints itself -- clear_painted has to know the map's clear extent is
       the whole shadow. Regression test for geom_of(DASH_VARIANT_MAP) running
       out of bounds against g_geom, which is sized for
       DASH_NONE..DASH_OVERLAY_TALL only. */
    dash_reset();
    dash_map_begin();
    dash_map_paint(4, 8, DT_ROOM);
    dash_map_paint(35, 30, DT_ROOM_HERE);
    dash_build(DASH_PANEL, 19);
    assert(dash_cell(4, 8)   == DT_BLANK);
    assert(dash_cell(35, 30) == DT_BLANK);

    /* Same bug, reached the way it actually happens: nobody reclaims the
       layer after dash_map_begin, so dash_frame_end expires it by building
       DASH_NONE -- which runs clear_painted while g_variant is still
       DASH_VARIANT_MAP, before dash_build reassigns it. */
    dash_reset();
    dash_map_begin();
    dash_map_paint(4, 8, DT_ROOM);
    dash_dirty_clear();
    dash_frame_end();                      /* claimed by begin above */
    dash_frame_end();                      /* nobody reclaims: map expires */
    assert(dash_cell(4, 8) == DT_BLANK);

    /* dash_map_hold keeps the map's claim on the layer alive across a frame
       nobody redraws it on, without repainting -- the map's counterpart to
       dash_box_hold, for a screen that draws once and then holds rather than
       recomputing and redrawing every frame. */
    dash_reset();
    dash_map_begin();
    dash_map_paint(4, 8, DT_ROOM);
    dash_frame_end();                      /* claimed by begin above */
    dash_map_hold();
    dash_frame_end();
    assert(dash_cell(4, 8) == DT_ROOM);
    dash_map_hold();
    dash_frame_end();
    assert(dash_cell(4, 8) == DT_ROOM);

    /* Without the hold it expires, which is what makes the hold load-bearing
       rather than decorative, same as the box. */
    dash_frame_end();
    assert(dash_cell(4, 8) == DT_BLANK);

    /* The hold does nothing when a map is not what is on the layer -- it must
       not resurrect a map the panel has since displaced, mirroring the box
       hold's own narrowness (see the DASH_BOX case above). */
    dash_reset();
    dash_map_begin();
    dash_map_paint(4, 8, DT_ROOM);
    dash_build(DASH_PANEL, 19);
    dash_map_hold();
    dash_frame_end();
    assert(dash_cell(0, 19) == DT_CORNER_TL);   /* strip still up */
    assert(dash_cell(4, 8)  == DT_BLANK);       /* stale map did not return */
    for (y = 0; y < 6; y++) {
        dash_build(DASH_PANEL, 19);
        dash_map_hold();
        dash_frame_end();
        assert(dash_cell(0, 19) == DT_CORNER_TL);
        assert(dash_cell(4, 8)  == DT_BLANK);
    }

    /* And a hold with nothing painted stays nothing painted. */
    dash_reset();
    dash_map_hold();
    dash_frame_end();
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    /* dash_hold_painted holds whichever variant is up, and says so. This is the
       one hold a caller that does not know what is on the layer can use: a fade
       ramp or a modal wait can be sitting over a box, over the map or over the
       strip, and holding the wrong one of those is what expired the marble out
       from under an in-game menu's last frame. */
    dash_reset();
    assert(dash_hold_painted() == 0);          /* empty layer: nothing to hold */
    dash_frame_end();
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    dash_box(6, 4, 28, 9);
    dash_frame_end();                          /* claimed by dash_box above */
    for (y = 0; y < 6; y++) {
        assert(dash_hold_painted() == 1);
        dash_frame_end();
        assert(dash_cell(6, 4) == DT_CORNER_TL);
    }

    dash_build(DASH_PANEL, 19);
    dash_frame_end();                          /* claimed by dash_build above */
    for (y = 0; y < 6; y++) {
        assert(dash_hold_painted() == 1);
        dash_frame_end();
        assert(dash_cell(0, 19) == DT_CORNER_TL);
    }

    dash_map_begin();
    dash_map_paint(4, 8, DT_ROOM);
    dash_frame_end();                          /* claimed by dash_map_begin */
    for (y = 0; y < 6; y++) {
        assert(dash_hold_painted() == 1);
        dash_frame_end();
        assert(dash_cell(4, 8) == DT_ROOM);
    }

    /* Stop holding and it expires, exactly as the variant-specific holds do --
       this is a hold, not a pin. */
    dash_frame_end();
    assert(dash_hold_painted() == 0);
    for (y = 0; y < DASH_ROWS; y++) assert(row_all(y, DT_BLANK));

    printf("test_dash_map: ok\n");
    return 0;
}
