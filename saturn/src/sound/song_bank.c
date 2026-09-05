/*----------------------
 | song_bank.c
 | Description: Implementation of the song bank. See song_bank.h for why the
 |   disc read is a callback and not a call.
 | Author: suinevere
 | Dependencies: song_bank.h, music_synth_data.h, tracker.h
 ----------------------*/
#include "song_bank.h"
#include "music_synth_data.h"

/*----------------------
 | PAT_MAGIC_* / PAT_VERSION
 | Description: The catalogue file's first five bytes. The magic ends in 0x1A
 |   to match the disc's own PIC archives, which is a convention worth keeping
 |   where a file of ours sits in the same directory as theirs.
 | Author: suinevere
 ----------------------*/
#define PAT_MAGIC_0 'P'
#define PAT_MAGIC_1 'A'
#define PAT_MAGIC_2 'T'
#define PAT_MAGIC_3 0x1A
#define PAT_VERSION 1

/*----------------------
 | PAT_DIR_ENTRY / PAT_REC_HEAD
 | Description: The two fixed widths inside the file: a directory entry, and
 |   the two words every tune record starts with. Named because both are used
 |   as strides and a wrong one reads the next tune's bytes as this one's.
 | Author: suinevere
 ----------------------*/
#define PAT_DIR_ENTRY 12
#define PAT_REC_HEAD  4

/*----------------------
 | bank state
 | Description: The bound catalogue. g_header is the file's first sector, held
 |   for the session because the directory, the track table and the ids all
 |   live in it and a menu asks for them every frame. g_resident is which tune
 |   the slot holds, so re-asking for it costs nothing.
 | Author: suinevere
 ----------------------*/
static const unsigned char *g_header;
static unsigned char       *g_slot;
static unsigned long        g_slot_len;
static SongBankReadFn       g_read;
static int                  g_songs;
static int                  g_default;
static int                  g_track_min;
static int                  g_track_max;
static int                  g_dir_off;
static int                  g_trk_off;
static int                  g_id_off;
static int                  g_resident = -1;
static TrackerSong          g_song;

/*----------------------
 | rd16
 | Description: One big-endian word out of the file. Written out rather than
 |   cast because the header is a byte buffer at whatever alignment the read
 |   landed on, and an unaligned short load is an address error on the SH-2.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: p -- two bytes, high first
 | Returns: their value
 ----------------------*/
static int rd16(const unsigned char *p) {
    return ((int) p[0] << 8) | (int) p[1];
}

/*----------------------
 | dir_entry
 | Description: The directory row for one tune.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_header, g_dir_off
 | Params: index -- a tune
 | Returns: a pointer into the header sector
 ----------------------*/
static const unsigned char *dir_entry(int index) {
    return g_header + g_dir_off + index * PAT_DIR_ENTRY;
}

void song_bank_reset(void) {
    g_header = 0;
    g_slot = 0;
    g_slot_len = 0;
    g_read = 0;
    g_songs = 0;
    g_resident = -1;
}

int song_bank_bind(const unsigned char *header, unsigned long len,
                   unsigned char *slot, unsigned long slot_len,
                   SongBankReadFn read) {
    int songs, need, slot_want;
    song_bank_reset();
    if (!header || !slot || !read) return 0;
    if (len < 28) return 0;
    if (header[0] != PAT_MAGIC_0 || header[1] != PAT_MAGIC_1
        || header[2] != PAT_MAGIC_2 || header[3] != PAT_MAGIC_3) return 0;
    if (rd16(header + 4) != PAT_VERSION) return 0;

    songs = rd16(header + 6);
    /* The shape has to be the one this build was compiled against: the cells
       are cast in place and walked with MUSIC_SYNTH_ROWS and _CHANNELS, so a
       file written for a different shape would be read as the wrong notes
       rather than rejected. */
    if (rd16(header + 8) != MUSIC_SYNTH_ROWS) return 0;
    if (rd16(header + 10) != MUSIC_SYNTH_CHANNELS) return 0;
    if (songs < 1) return 0;

    g_default   = rd16(header + 12);
    g_track_min = rd16(header + 14);
    g_track_max = rd16(header + 16);
    slot_want   = rd16(header + 18);
    g_dir_off   = rd16(header + 20);
    g_trk_off   = rd16(header + 22);
    g_id_off    = rd16(header + 24);

    if (g_default < 0 || g_default >= songs) return 0;
    if (g_track_max < g_track_min) return 0;
    if ((unsigned long) slot_want > slot_len) return 0;

    /* Every offset the header names has to land inside the bytes actually read,
       or the first menu frame walks off the end of the buffer. */
    need = g_dir_off + songs * PAT_DIR_ENTRY;
    if (need > g_trk_off) return 0;
    need = g_trk_off + (g_track_max - g_track_min + 1);
    if (need > g_id_off) return 0;
    if ((unsigned long) g_id_off >= len) return 0;

    g_header = header;
    g_slot = slot;
    g_slot_len = slot_len;
    g_read = read;
    g_songs = songs;
    g_resident = -1;
    return 1;
}

int song_bank_count(void) {
    return g_header ? g_songs : music_synth_song_count();
}

const char *song_bank_id(int index) {
    const unsigned char *e;
    if (!g_header) return music_synth_song_id(index);
    if (index < 0 || index >= g_songs) index = g_default;
    e = dir_entry(index);
    return (const char *) (g_header + g_id_off + rd16(e + 10));
}

int song_bank_for_track(int track) {
    if (!g_header) return music_synth_song_for_track(track);
    if (track < g_track_min || track > g_track_max) return g_default;
    return (int) g_header[g_trk_off + (track - g_track_min)];
}

int song_bank_resident(void) {
    return g_resident;
}

/*----------------------
 | slot_song
 | Description: Turns the bytes in the slot into a song the tracker can walk,
 |   checking that they describe one first. The cells are cast in place rather
 |   than copied: a TrackerCell is two chars, so there is no alignment to get
 |   wrong and no second copy to hold.
 | Author: suinevere
 | Dependencies: tracker.h
 | Globals: g_slot, g_slot_len, g_song
 | Params: entry -- the tune's directory row; bytes -- how many were read
 | Returns: the song, or 0 when the record does not describe one
 ----------------------*/
static const TrackerSong *slot_song(const unsigned char *entry, int bytes) {
    int patterns = rd16(g_slot);
    int order_len = rd16(g_slot + 2);
    long cells = (long) patterns * MUSIC_SYNTH_ROWS * MUSIC_SYNTH_CHANNELS;
    long need = PAT_REC_HEAD + cells * 2 + order_len;
    int i;

    if (patterns < 1 || order_len < 1) return 0;
    if (need > bytes || (unsigned long) need > g_slot_len) return 0;
    /* The record repeats what the directory said; disagreeing means one of the
       two was read from the wrong place, and playing either would be noise. */
    if (order_len != (int) entry[6]) return 0;
    for (i = 0; i < order_len; i++)
        if (g_slot[PAT_REC_HEAD + cells * 2 + i] >= patterns) return 0;

    g_song.cells      = (const TrackerCell *) (g_slot + PAT_REC_HEAD);
    g_song.rows       = MUSIC_SYNTH_ROWS;
    g_song.channels   = MUSIC_SYNTH_CHANNELS;
    g_song.order      = g_slot + PAT_REC_HEAD + cells * 2;
    g_song.order_len  = (unsigned char) order_len;
    g_song.loop_to    = entry[7];
    g_song.speed      = entry[8];
    g_song.speed_frac = entry[9];
    if (g_song.speed == 0) return 0;
    if (g_song.loop_to >= g_song.order_len) return 0;
    return &g_song;
}

const TrackerSong *song_bank_at(int index) {
    const unsigned char *e;
    int sector, sectors, bytes;

    if (!g_header) return music_synth_song_at(index);
    if (index < 0 || index >= g_songs) index = g_default;
    /* A tune this build links is served from .rodata. Cheaper than a seek, and
       it is what keeps the menus playing before the catalogue is bound and on a
       disc that never carried it. */
    if (index < MUSIC_SYNTH_SONGS) return music_synth_song_at(index);
    if (index == g_resident) return &g_song;

    e = dir_entry(index);
    sector  = rd16(e);
    sectors = rd16(e + 2);
    bytes   = rd16(e + 4);
    g_resident = -1;
    if (sectors < 1 || bytes < PAT_REC_HEAD
        || (unsigned long) (sectors * MUSIC_PAT_SECTOR) > g_slot_len
        || g_read(sector, sectors * MUSIC_PAT_SECTOR, g_slot) <= 0
        || !slot_song(e, bytes)) {
        return music_synth_song_at(MUSIC_SYNTH_DEFAULT);
    }
    g_resident = index;
    return &g_song;
}
