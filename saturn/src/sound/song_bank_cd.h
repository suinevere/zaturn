/*----------------------
 | song_bank_cd.h
 | Description: The disc half of the song bank -- the part that needs SRL, and
 |   so the part the host tests cannot reach. Everything that can be tested
 |   without a drive is in song_bank.c.
 |
 |   CD build only. The netbin links every tune and has no disc to read.
 | Author: suinevere
 | Dependencies: song_bank.h
 ----------------------*/
#ifndef SONG_BANK_CD_H
#define SONG_BANK_CD_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | song_bank_cd_open
 | Description: Claims the Low Work RAM the catalogue needs, reads its header
 |   sector off the disc and binds it. Call once, while the drive is free and
 |   after the TOC has certainly been read -- the same window music_source_bind
 |   is called in, and for the same reason.
 |
 |   Every refusal is silent and leaves the build playing the tunes it links,
 |   which is what a disc with no MUSIC.PAT already does. There is no error to
 |   report to a player: the music is quieter in variety, not broken.
 | Author: suinevere
 | Dependencies: SRL, song_bank.h, title.h
 | Globals: N/A
 | Params: N/A
 | Returns: 1 when the catalogue is bound, 0 otherwise
 ----------------------*/
int song_bank_cd_open(void);

#ifdef __cplusplus
}
#endif
#endif /* SONG_BANK_CD_H */
