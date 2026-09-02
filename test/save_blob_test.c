/* Host unit tests for save_blob_len -- where the story blob in a save record
   ends, and therefore where the map that now shares that record begins.

   Worth its own file because both failure directions are silent: a length too
   long hands map_model_deserialize the middle of a Z-machine stack, and one too
   short loses the map without saying so. The blobs here are built to
   opcode_save's layout by hand rather than by calling the interpreter, so the
   test states the format independently of the code that reads it -- if the two
   ever disagree, that is exactly what should fail.

   Build:
     gcc -O2 -I saturn/src/engine -o /tmp/sb test/save_blob_test.c \
         saturn/src/engine/save_blob.c && ./sb */
#include <stdio.h>
#include <string.h>
#include "save_blob.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } }while(0)

#define MAP_MAGIC 0x4Du

static unsigned char g_blob[4096];

/*----------------------
 | wr32
 | Description: Writes a little-endian 32-bit field, the way opcode_save does.
 | Author: suinevere
 ----------------------*/
static void wr32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char) (v & 0xFF);
    p[1] = (unsigned char) ((v >> 8) & 0xFF);
    p[2] = (unsigned char) ((v >> 16) & 0xFF);
    p[3] = (unsigned char) ((v >> 24) & 0xFF);
}

/*----------------------
 | build
 | Description: Lays out a save blob with the given delta and stack sizes and
 |   returns the length it should measure. Fills the body with a byte that is
 |   NOT the map magic, so a test that finds a map has really found one.
 | Author: suinevere
 ----------------------*/
static unsigned int build(unsigned int rle, unsigned int spoff) {
    unsigned int len = SAVE_BLOB_HEADER + rle + spoff * 2u;
    memset(g_blob, 0xAB, sizeof g_blob);
    memcpy(g_blob, "MZSV1", 5);
    g_blob[5] = 0; g_blob[6] = 0;               /* dynlen, unread here */
    wr32(g_blob + 7, 0x1234);                   /* pc */
    wr32(g_blob + 11, spoff);
    g_blob[15] = 0; g_blob[16] = 0;             /* bp */
    wr32(g_blob + 17, rle);
    return len;
}

int main(void) {
    int fails = 0;
    unsigned int len;

    /* An ordinary blob measures itself. */
    len = build(300, 40);
    CHECK(save_blob_len(g_blob, sizeof g_blob) == len);
    CHECK(len == SAVE_BLOB_HEADER + 300 + 80);

    /* A save at the very start of a game: no delta, no stack. */
    len = build(0, 0);
    CHECK(save_blob_len(g_blob, sizeof g_blob) == SAVE_BLOB_HEADER);
    CHECK(len == SAVE_BLOB_HEADER);

    /* A blob that exactly fills the buffer still measures. */
    len = build(sizeof g_blob - SAVE_BLOB_HEADER, 0);
    CHECK(save_blob_len(g_blob, sizeof g_blob) == sizeof g_blob);
    CHECK(len == sizeof g_blob);

    /* The map goes straight after it, and is found there. */
    len = build(120, 9);
    g_blob[len] = MAP_MAGIC;
    CHECK(save_blob_len(g_blob, sizeof g_blob) == len);
    CHECK(g_blob[save_blob_len(g_blob, sizeof g_blob)] == MAP_MAGIC);

    /* Nothing appended: the byte past the blob is not mistaken for a map, which
       is why saturn_load_blob clears the buffer before reading into it. */
    len = build(120, 9);
    memset(g_blob + len, 0, sizeof g_blob - len);
    CHECK(g_blob[save_blob_len(g_blob, sizeof g_blob)] != MAP_MAGIC);

    /* Refusals. Each answers 0, which the callers read as "no seam here". */
    build(300, 40);
    CHECK(save_blob_len(0, sizeof g_blob) == 0);                 /* no blob */
    CHECK(save_blob_len(g_blob, 0) == 0);                        /* no capacity */
    CHECK(save_blob_len(g_blob, SAVE_BLOB_HEADER - 1) == 0);     /* header truncated */

    build(300, 40);
    g_blob[0] = 'X';
    CHECK(save_blob_len(g_blob, sizeof g_blob) == 0);            /* wrong magic */

    build(300, 40);
    g_blob[4] = 'Z';
    CHECK(save_blob_len(g_blob, sizeof g_blob) == 0);            /* magic, but not ours */

    /* Declared lengths that run past what was read. */
    len = build(300, 40);
    CHECK(save_blob_len(g_blob, len - 1) == 0);
    CHECK(save_blob_len(g_blob, len) == len);                    /* and exactly len is fine */

    build(0xFFFFFFFFu, 0);
    CHECK(save_blob_len(g_blob, sizeof g_blob) == 0);            /* absurd delta */

    build(0, 0xFFFFFFFFu);
    CHECK(save_blob_len(g_blob, sizeof g_blob) == 0);            /* absurd stack */

    /* A pair large enough to wrap 32 bits if they were added before checking.
       rle + spoff*2 == 2^32, so an unchecked sum would come out as 0 and the
       whole record would look like a bare header with a map right behind it. */
    build(0x80000000u, 0x40000000u);
    CHECK(save_blob_len(g_blob, sizeof g_blob) == 0);

    printf(fails ? "%d FAILURE(S)\n" : "all save blob tests passed\n", fails);
    return fails ? 1 : 0;
}
