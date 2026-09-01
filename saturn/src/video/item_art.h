/*----------------------
 | item_art.h
 | Description: The item pictures' hardware half: reading OITEM.CZ off the
 |   disc, decompressing one 64x80 picture and putting it on NBG1 in the
 |   inventory overlay's pane. The decoding is in oitem.c and the binding in
 |   scene/items.h; this is only the policy and the SRL calls, the way
 |   room_art.h is for the room backgrounds.
 |
 |   The archive is 40,840 bytes and is read once, when the story is selected,
 |   and held for the session. It used to be read on every inventory open and
 |   freed on every close, which put a blocking disc seek in front of a menu the
 |   player opens constantly and cut the CD-DA track every time. The budget is
 |   unchanged by holding it: it always had to fit alongside the typeahead trie,
 |   the resident area archive and the save scratch, since the window it was
 |   resident in sat inside all three. saturn/tests/test_lwram_budget.py is
 |   where that arithmetic is held.
 |
 |   Zork I only. items_available is what gates it, and a story without a
 |   table never opens the archive at all. A story that has a table but whose
 |   archive is not on the disc is gated the same way, one step later: see
 |   item_art_available.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef ITEM_ART_H
#define ITEM_ART_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | ITEM_ART_X / ITEM_ART_Y
 | Description: Where the picture's top-left pixel sits on screen: columns
 |   30..37 and rows 18..27 of the 40x30 text grid, which is the middle of the
 |   overlay's picture module with the module's second frame closed hard around
 |   it. Written into the NBG1 container at this offset with the layer
 |   positioned at (0,0), so there is no scroll arithmetic and the rest of the
 |   container stays index 0 -- which VDP2 reads as transparent. This 64x80
 |   window is the whole of what this module ever paints: a picture, or the same
 |   window filled black for a carried item that has none.
 |     The row is fixed rather than passed because the tall strip's top is: the
 |   display is thirty rows, console_height reserves the input line, the two
 |   frame rows and the strip's seven, and the tall overlay's rise takes five
 |   more, which puts its first content row at 17 and the picture one below it.
 |   saturn/tests/test_overlay_layout.c is where that arithmetic is held
 |   against panel_layout.h.
 | Author: suinevere
 ----------------------*/
#define ITEM_ART_X 240
#define ITEM_ART_Y 144

/*----------------------
 | item_art_set_game
 | Description: Tells the module which story is running, once, when it is
 |   selected. Held rather than passed per call because the overlay renderer
 |   runs where the story identity is not in scope. Passing a story with no
 |   authored items is how the pane is turned off again.
 |
 |   Reads the archive here too, for a story that has one -- this is the one
 |   moment in the session where a 40 KB disc read is free, with the loading
 |   screen up and the music not yet started. It is also the only attempt: a
 |   refusal is not an error, it just leaves item_art_available false, and the
 |   inventory shows the plain list for the rest of the session rather than
 |   going back to the drive at every open to ask again.
 | Author: suinevere
 | Dependencies: scene/items.h, item_art_open
 | Globals: g_release, g_serial, g_have_game
 | Params: release -- Z-machine release; serial -- 6-char serial
 | Returns: N/A
 ----------------------*/
void item_art_set_game(unsigned int release, const char *serial);

/*----------------------
 | item_art_available
 | Description: Whether a picture can actually be put on screen: the story has
 |   an authored table AND the archive those pictures live in is resident. The
 |   one call that decides whether the inventory overlay takes its tall geometry
 |   with a picture module or its plain list-only one.
 |
 |   Both halves are load-bearing. A story with a table but no OITEM.CZ on the
 |   disc -- a build whose staging step did not run, or a disc somebody made
 |   themselves -- would otherwise get the split module and a frame that is
 |   black for every item in the game, which reads as a broken feature rather
 |   than an absent one. Asked after item_art_set_game has already tried the
 |   read, so this is settled for the session by the time any overlay opens.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_have_game, g_archive
 | Params: N/A
 | Returns: 1 when a picture could be shown, 0 otherwise
 ----------------------*/
int item_art_available(void);

/*----------------------
 | item_art_open / item_art_close
 | Description: Make the archive resident and free it again. open is called by
 |   item_art_set_game, so the read lands in the game load where the drive is
 |   already busy and the music has not started; close is called on the way back
 |   to the title, and also blanks the pane, since nothing should be left on a
 |   layer whose owner has gone. Both are idempotent. Nothing else calls open:
 |   the load-time read is the only one, and item_art_available reports whether
 |   it took.
 |
 |   open restores the CD to the story directory before returning whenever it
 |   stepped out of it, which is the obligation every post-selection detour
 |   owes. It also holds the music across the read: a data seek silences CD-DA,
 |   and an unheld track comes back as a fresh pass -- the drive is stopped and
 |   picked up again on the frame it stopped on instead, so what the player
 |   hears when the inventory opens is a skip rather than a restart.
 | Author: suinevere
 | Dependencies: SRL, title.h, music.h
 | Globals: g_archive, g_archive_len
 | Params: N/A
 | Returns: open returns 1 when the archive is resident, 0 on any refusal
 ----------------------*/
int  item_art_open(void);
void item_art_close(void);

/*----------------------
 | item_art_show
 | Description: Puts one object's picture on the pane, centred on the plate. An
 |   object with no bound picture takes the plate to black through
 |   item_art_blank and returns 0, which is a success from the player's point of
 |   view and is deliberately not distinguished from it in the return value,
 |   because no caller has anything different to do.
 |
 |   Skips the decode and the upload when the picture is the one already
 |   showing, so walking the cursor up and down a list of the same item costs
 |   nothing.
 |
 |   Every failure -- no game set, an archive that will not open, a stream that
 |   will not decode -- holds the picture already showing and says nothing on
 |   screen. Art is decoration; a failed load must never blank the screen or
 |   stop the game.
 | Author: suinevere
 | Dependencies: SRL, oitem.h, scene/items.h
 | Globals: g_archive, g_archive_len, g_cur_picture
 | Params: obj -- the carried object's number
 | Returns: 1 when a picture is on the pane, 0 when it was blanked or held
 ----------------------*/
int item_art_show(unsigned int obj);

/*----------------------
 | item_art_blank
 | Description: Fills the picture's own 64x80 window with opaque black, for a
 |   carried object the story has no picture for. That window is a hole in the
 |   strip's stone that a picture normally fills, so leaving it transparent
 |   would show marble inside a frame that is telling the player there is
 |   nothing to show; black says the frame is empty, and fills it to exactly the
 |   size a picture would. Idempotent, so the renderer may call it every frame.
 |
 |   It owns the whole palette while it is up -- no picture is on the layer to
 |   share it with -- so it loads an all-black one rather than reserving an
 |   index out of a picture's own 256, any of which a picture may be using.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cur_picture, g_layer_up
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void item_art_blank(void);

/*----------------------
 | item_art_hide
 | Description: Takes the picture's window back to transparent without freeing
 |   the archive, for every frame the overlay is not up. One thing is on this
 |   layer at a time, the contract dash_map already holds for NBG2. This is the
 |   pane gone, as against item_art_blank's pane present and empty.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_cur_picture
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void item_art_hide(void);

#ifdef __cplusplus
}
#endif
#endif /* ITEM_ART_H */
