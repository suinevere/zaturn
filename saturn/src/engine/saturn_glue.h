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
 | saturn_save_blob / saturn_load_blob
 | Description: Save/restore the Z-machine state to Saturn backup memory, presenting
 |   the on-screen device/slot menu (in saturn_glue.cxx). Return 1 on success, 0 on
 |   cancel-or-fail.
 | Author: suinevere
 ----------------------*/
int saturn_save_blob(const uint8_t *data, uint32_t len);
int saturn_load_blob(uint8_t *buf, uint32_t maxlen);

/*----------------------
 | mojo_boot / mojo_run
 | Description: The interpreter entry points (in mojozork_saturn.c): boot wires the
 |   hooks and loads the story with a seed; run executes until the game quits.
 | Author: suinevere
 ----------------------*/
void mojo_boot(uint8_t *story, uint32_t len, int seed);
void mojo_run(void);

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

#ifdef __cplusplus
}
#endif
#endif /* SATURN_GLUE_H */
