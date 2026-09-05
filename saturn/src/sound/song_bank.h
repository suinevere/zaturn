/*----------------------
 | song_bank.h
 | Description: Where a tune comes from, asked once so nothing else has to know
 |   there are two answers.
 |
 |   The netbin links every tune and this forwards straight to the generated
 |   data. The CD build links one and reads the rest out of /BG/MUSIC.PAT, a
 |   tune at a time, because on that target __heap_start follows .rodata: a
 |   linked tune is taken out of the heap the story is loaded into, and the
 |   whole catalogue there stopped the disc's largest story from loading at all.
 |
 |   One tune resident and not the catalogue, also measured: the catalogue is
 |   41,708 bytes and the Low Work RAM left in the worst in-game case -- an area
 |   archive, the typeahead trie, the save scratch, the item pane and the map
 |   parchment all at once -- is 25,998. One slot the size of the largest tune
 |   is 8,192, which fits with room to spare.
 |
 |   No SRL and no disc here. The reader is a callback the CD layer registers,
 |   which is what lets the parse -- the part that goes wrong quietly -- be a
 |   host test.
 | Author: suinevere
 | Dependencies: tracker.h
 ----------------------*/
#ifndef SONG_BANK_H
#define SONG_BANK_H

#include "tracker.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SongBankReadFn
 | Description: Reads whole sectors out of the catalogue file. A sector offset
 |   and not a byte one because that is what the drive takes, which is also why
 |   the file puts every tune on a sector of its own.
 | Author: suinevere
 ----------------------*/
typedef int (*SongBankReadFn)(int sector, int bytes, void *dest);

/*----------------------
 | song_bank_reset
 | Description: Forgets any bound catalogue, so every answer comes from the
 |   linked data again. What a failed load falls back to, and what the host
 |   tests start each case with.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void song_bank_reset(void);

/*----------------------
 | song_bank_bind
 | Description: Takes the catalogue's header sector and the slot the tunes will
 |   be read into. Validates the header before believing any of it -- magic,
 |   version, the shape matching what this build was compiled against, and a
 |   slot big enough for the largest tune the header declares -- because every
 |   number after this is used as an offset, and a file from another build would
 |   otherwise be read as pattern data.
 |
 |   Refusing is not a failure: the build keeps the tunes it links, which is how
 |   a disc with no MUSIC.PAT still has music.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: header -- the first MUSIC_PAT_HEADER_BYTES of the file; len -- how
 |   many were read; slot -- where tunes are loaded, long-aligned; slot_len --
 |   its size; read -- the sector reader
 | Returns: 1 when the catalogue is bound, 0 when it was refused
 ----------------------*/
int song_bank_bind(const unsigned char *header, unsigned long len,
                   unsigned char *slot, unsigned long slot_len,
                   SongBankReadFn read);

/*----------------------
 | song_bank_count
 | Description: How many tunes can be reached -- the catalogue's when one is
 |   bound, and what this build links when none is.
 | Author: suinevere
 | Dependencies: music_synth_data.h
 | Globals: N/A
 | Params: N/A
 | Returns: a count of at least 1
 ----------------------*/
int song_bank_count(void);

/*----------------------
 | song_bank_id
 | Description: A tune's short name, for a menu row.
 | Author: suinevere
 | Dependencies: music_synth_data.h
 | Globals: N/A
 | Params: index -- 0..song_bank_count()-1
 | Returns: a string valid until the catalogue is rebound
 ----------------------*/
const char *song_bank_id(int index);

/*----------------------
 | song_bank_for_track
 | Description: Which tune stands in for a CD-DA track. The bound catalogue
 |   answers over all of its tunes; without one this is the linked table, which
 |   clamps to what the build has.
 | Author: suinevere
 | Dependencies: music_synth_data.h
 | Globals: N/A
 | Params: track -- a CD-DA track number
 | Returns: a tune index
 ----------------------*/
int song_bank_for_track(int track);

/*----------------------
 | song_bank_at
 | Description: One tune, ready to play. A linked tune is returned from
 |   .rodata; anything else is read off the disc into the slot first, which
 |   costs a seek -- so this must not be called from an interrupt, and the
 |   caller must have stopped the tracker, because the tune being replaced is
 |   the one in the slot.
 |
 |   A read that fails returns the default rather than nothing: a tune that
 |   cannot be loaded should sound like the wrong tune, not like the music
 |   system being broken.
 | Author: suinevere
 | Dependencies: tracker.h, music_synth_data.h
 | Globals: N/A
 | Params: index -- 0..song_bank_count()-1
 | Returns: a song valid until the next call that loads a different one
 ----------------------*/
const TrackerSong *song_bank_at(int index);

/*----------------------
 | song_bank_resident
 | Description: Which tune is in the slot, for a caller deciding whether asking
 |   for one costs a seek.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: a tune index, or -1 when the slot is empty or unused
 ----------------------*/
int song_bank_resident(void);

#ifdef __cplusplus
}
#endif
#endif /* SONG_BANK_H */
