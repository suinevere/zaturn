/*----------------------
 | saturn_glue.h
 | Description: The interface between the C Z-Machine core (mojozork) and the
 |   Saturn client -- the interpreter hooks the core calls through its
 |   ZMachineState function pointers, the boot/run entry points main.cxx calls, and
 |   the accessor exposing the loaded story to the typeahead. The hooks are
 |   implemented in saturn_glue.cxx; mojo_boot/mojo_run in mojozork_saturn.c;
 |   saturn_sound_effect in sound.cxx.
 | Author: suinevere
 | Dependencies: stdint.h, stddef.h
 ----------------------*/
#ifndef SATURN_GLUE_H
#define SATURN_GLUE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | saturn_writestr / saturn_readline / saturn_die
 | Description: The core's text-output, line-input, and fatal-halt hooks (in
 |   saturn_glue.cxx). Signatures MUST match the ZMachineState function pointers:
 |   writestr void(*)(const char*, size_t), readline void(*)(char*, int), die
 |   void(*)(const char*, ...). saturn_die does not return.
 | Author: suinevere
 ----------------------*/
void saturn_writestr(const char *str, size_t slen);
void saturn_readline(char *buf, int maxlen);
#if defined(__GNUC__) || defined(__clang__)
void saturn_die(const char *fmt, ...) __attribute__((noreturn));
#else
void saturn_die(const char *fmt, ...);
#endif

/*----------------------
 | saturn_read_story_file / saturn_read_story_prefix
 | Description: Re-read the loaded story from CD (there is no fopen on Saturn).
 |   saturn_read_story_file takes the whole len-byte image, for opcode_restart.
 |   saturn_read_story_prefix takes only the first `want` bytes of a storylen-byte
 |   file, which is all save/restore need: the Quetzal-style delta only covers
 |   dynamic memory, so a ~12 KB prefix stands in for a ~130 KB image. Both return
 |   1 on success, 0 on failure.
 | Author: suinevere
 ----------------------*/
int saturn_read_story_file(uint8_t *buf, uint32_t len);
int saturn_read_story_prefix(uint8_t *buf, uint32_t storylen, uint32_t want);

/*----------------------
 | saturn_scratch_alloc / saturn_scratch_free
 | Description: Allocator for the interpreter's large, short-lived save/restore
 |   scratch buffers, backed by Low Work RAM instead of the C heap. The C heap is
 |   the ~304 KB of High Work RAM left over after the binary, and by the time a
 |   player saves it already holds the story image plus the typeahead trie (115-200
 |   KB, measured), so a malloc of that size fails and the save opcode branches
 |   false with no UI shown. LWRAM is a separate 1 MB zone with no other claimants.
 |   saturn_scratch_free ignores NULL.
 | Author: suinevere
 ----------------------*/
void *saturn_scratch_alloc(uint32_t size);
void saturn_scratch_free(void *ptr);

/*----------------------
 | saturn_save_blob / saturn_load_blob / saturn_save_tail
 | Description: Save/restore the Z-machine state to Saturn backup memory, presenting
 |   the on-screen device/slot menu (in saturn_glue.cxx). Return 1 on success, 0 on
 |   cancel-or-fail.
 |
 |   One backup record per slot, holding the story blob and the map after it. They
 |   used to be two files -- the slot's own name and the same name with an 'M'
 |   appended -- which cost a second directory entry and a second header block on a
 |   device that counts both, and gave a restore two ways to half-succeed.
 |
 |   No new container was needed to put them in one record: the story blob is
 |   already self-describing (see SAVE_BLOB_MAX for its layout), so the map simply
 |   follows it and save_blob_len finds the seam. The interpreter reads from the
 |   front and stops at its own stack, so it never sees the tail.
 |
 |   save is handed a writable buffer and its capacity so it can append in place;
 |   saturn_save_tail is how the caller knows how much room to leave. load is given
 |   the buffer's capacity for the same reason -- it clears it before reading, so a
 |   record with no map leaves zeroes where a map header would be rather than
 |   whatever the scratch allocation happened to hold.
 | Author: suinevere
 ----------------------*/
int saturn_save_blob(uint8_t *data, uint32_t len, uint32_t cap);
int saturn_load_blob(uint8_t *buf, uint32_t maxlen);
uint32_t saturn_save_tail(void);

/*----------------------
 | SAVE_DYNAMIC_MAX / SAVE_BLOB_MAX
 | Description: The largest a save blob can come out at, for the boot-time check
 |   that asks whether backup memory can take a first save at all. That check runs
 |   before a story is chosen, so it cannot read the story's own dynamic-memory
 |   size and has to carry a ceiling instead.
 |
 |   SAVE_DYNAMIC_MAX is that ceiling: 14 KB, above the largest dynamic memory of
 |   the 31 stories in /Z3 (Planetfall, 0x37D0 = 14288 bytes). A story with more
 |   dynamic memory than this is not refused -- it saves exactly as before -- it
 |   just means the boot check under-counted what it would cost.
 |
 |   SAVE_BLOB_MAX applies opcode_save's format to that ceiling: a 21-byte header
 |   (magic, dynlen, pc, sp, bp, rle length), the delta, and the used stack. The
 |   delta's worst case is 1.5 bytes per dynamic byte, not 1: a zero run costs two
 |   bytes and a differing byte costs one, so the most expensive input is one that
 |   alternates between them and can never be coalesced into longer runs. The stack
 |   is the whole 2048-entry array at two bytes each -- 4096 -- since a save may be
 |   taken at any depth.
 | Author: suinevere
 ----------------------*/
#define SAVE_DYNAMIC_MAX 0x3800u
#define SAVE_BLOB_MAX    (21u + SAVE_DYNAMIC_MAX + (SAVE_DYNAMIC_MAX + 1u) / 2u + 4096u)

/*----------------------
 | mojo_boot / mojo_run
 | Description: The interpreter entry points (in mojozork_saturn.c): boot wires the
 |   hooks and loads the story with a seed; run executes until the game quits.
 | Author: suinevere
 ----------------------*/
void mojo_boot(uint8_t *story, uint32_t len, int seed);
void mojo_run(void);

/*----------------------
 | mojo_release
 | Description: Frees the loaded story image (in mojozork_saturn.c) and clears
 |   the interpreter's pointers into it. The C heap is ~194 KB and a story is up
 |   to 129 KB, so nothing between one session and the next -- the title
 |   wallpaper's own decode included -- fits beside an image that was not given
 |   back. A no-op before any story has been booted.
 | Author: suinevere
 ----------------------*/
void mojo_release(void);

/*----------------------
 | saturn_sound_effect
 | Description: The Z-machine sound_effect hook (in sound.cxx).
 | Author: suinevere
 ----------------------*/
void saturn_sound_effect(int number, int effect, int volume);

/*----------------------
 | saturn_story_data
 | Description: The loaded story image for runtime typeahead extraction; NULL (len
 |   0) before a story is loaded.
 | Author: suinevere
 ----------------------*/
const uint8_t* saturn_story_data(uint32_t* len_out);

/*----------------------
 | saturn_typeahead_build
 | Description: Builds the local prompt's typeahead trie for the story already
 |   loaded, if it is not built for that story and difficulty already.
 |
 |   The prompt would do this itself on the first turn. Calling it at game start
 |   instead is about allocation order: the trie is several hundred KB of Low Work
 |   RAM, and the background-art cache is warmed at game start too. Whichever runs
 |   first gets honest free space and the other has to be guessed at -- so the trie
 |   goes first, and the cache takes exactly what is genuinely left.
 | Author: suinevere
 ----------------------*/
void saturn_typeahead_build(void);

/*----------------------
 | saturn_typeahead_release
 | Description: Frees the local prompt's typeahead trie and forgets the story it
 |   was built for. Call on the soft reset: the trie is ~300 KB of Low Work RAM
 |   belonging to a game that has ended, and the boot jingle cannot be reloaded
 |   beside it and the online trie both. The next game rebuilds on its first turn.
 | Author: suinevere
 ----------------------*/
void saturn_typeahead_release(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_GLUE_H */
