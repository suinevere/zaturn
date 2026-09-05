/*----------------------
 | music_source.h
 | Description: Which of the two music sources the room engine is driving, and
 |   the one place that rewires it. The engine takes a backend plus four
 |   callbacks and every one of them has to change together -- swap the play
 |   function and leave is_playing pointing at CD-DA and the engine waits forever
 |   for a track that is not coming -- so binding them is a function and not five
 |   calls at a call site.
 |
 |   CD-only builds are the only callers. The netbin links no music.c: it has no
 |   disc, no room table and one source, and asks synth_should_play directly.
 | Author: suinevere
 | Dependencies: music.h, synth.h, app_state.h
 ----------------------*/
#ifndef MUSIC_SOURCE_H
#define MUSIC_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | music_source_bind
 | Description: Reads the player's preference and the disc, decides which source
 |   is really playing, and points the room engine at it. Call once the drive's
 |   TOC is certainly readable and not before -- music_cdda_has_audio caches its
 |   answer on the first ask, and asking early has already frozen the wrong one
 |   once. Safe to call again; music_source_select does.
 | Author: suinevere
 | Dependencies: music.h, synth.h
 | Globals: g_music_source, g_synth_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_source_bind(void);

/*----------------------
 | music_source_active
 | Description: What is actually playing, which is not always what the player
 |   asked for: a disc with no CD-DA plays the synth whatever the preference
 |   says. Everything that behaves differently per source asks this rather than
 |   reading g_music_source.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: MUSIC_SOURCE_CD or MUSIC_SOURCE_SYNTH
 ----------------------*/
int music_source_active(void);

/*----------------------
 | music_source_can_choose
 | Description: Whether there is a choice to offer. There is exactly when the
 |   disc carries CD-DA -- without it the synth is the only source and a Source
 |   row would be a control that does nothing.
 | Author: suinevere
 | Dependencies: music.h
 | Globals: N/A
 | Params: N/A
 | Returns: nonzero when both sources are available
 ----------------------*/
int music_source_can_choose(void);

/*----------------------
 | music_source_select
 | Description: Switches source and starts the new one where the old one left
 |   off -- the room's own track through the engine, or the menu draw when no
 |   room has been seen. Silences the source being left first: they write
 |   different hardware and nothing else would stop the one going quiet.
 |   A no-op when the source is already that one, so a Left/Right that lands
 |   back where it started does not restart the music.
 | Author: suinevere
 | Dependencies: music.h, synth.h
 | Globals: g_music_source
 | Params: source -- MUSIC_SOURCE_CD or MUSIC_SOURCE_SYNTH; a source the disc
 |   cannot provide is ignored
 | Returns: N/A
 ----------------------*/
void music_source_select(int source);

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_SOURCE_H */
