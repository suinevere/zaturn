/*----------------------
 | test_cgl.c
 | Description: The C port of the CGL decoder against checksums taken from the
 |   Python decoder in analysis/zork_cgl.py, over the real archives in
 |   analysis/zork_bg/raw. Run from the repository root:
 |   gcc -O2 -I saturn/src -o /tmp/tcgl \
 |       saturn/tests/test_cgl.c saturn/src/video/cgl.c && /tmp/tcgl
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video/cgl.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static unsigned long fnv1a(const unsigned char *p, unsigned long n) {
    unsigned long h = 2166136261UL;
    unsigned long i;
    for (i = 0; i < n; i++) { h ^= p[i]; h = (h * 16777619UL) & 0xFFFFFFFFUL; }
    return h;
}

typedef struct {
    const char   *archive;
    int           frame;
    unsigned long offset;
    unsigned long length;
    unsigned long pixel_sum;
    unsigned long pal_sum;
} CglExpect;

static const CglExpect EXPECT[] = {
#include "fixtures/cgl_sums.inc"
};
#define EXPECT_N ((int) (sizeof(EXPECT) / sizeof(EXPECT[0])))

static unsigned char *slurp(const char *path, unsigned long *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (unsigned char *) malloc((size_t) n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t) n, f) != (size_t) n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (unsigned long) n;
    return buf;
}

int main(void) {
    static unsigned char pixels[CGL_FRAME_BYTES];
    static unsigned char palbytes[512];
    unsigned short pal[256];
    char path[256];
    int i;

    check(EXPECT_N == 75, "the fixture covers all 75 frames in the archives");

    for (i = 0; i < EXPECT_N; i++) {
        unsigned long flen = 0, got;
        unsigned char *file;
        int j;

        sprintf(path, "analysis/zork_bg/raw/%s", EXPECT[i].archive);
        file = slurp(path, &flen);
        if (!file) { printf("FAIL cannot read %s\n", path); fails++; continue; }
        check(EXPECT[i].offset + EXPECT[i].length <= flen,
              "the frame record lies inside the archive");

        got = cgl_decode(file + EXPECT[i].offset, EXPECT[i].length,
                         pixels, sizeof(pixels));
        check(got == CGL_FRAME_BYTES, "a frame decodes to exactly 320x240 bytes");
        check(fnv1a(pixels, CGL_FRAME_BYTES) == EXPECT[i].pixel_sum,
              "decoded pixels match the Python decoder");

        cgl_palette(file + EXPECT[i].offset, pal);
        for (j = 0; j < 256; j++) {
            palbytes[j * 2]     = (unsigned char) (pal[j] & 0xff);
            palbytes[j * 2 + 1] = (unsigned char) ((pal[j] >> 8) & 0xff);
        }
        check(fnv1a(palbytes, sizeof(palbytes)) == EXPECT[i].pal_sum,
              "palette words match the Python decoder");
        for (j = 0; j < 256; j++) {
            if ((pal[j] & 0x8000u) == 0) {
                check(0, "every palette word is opaque");
                break;
            }
        }

        free(file);
    }

    {
        unsigned char junk[16];
        memset(junk, 0, sizeof(junk));
        check(cgl_decode(junk, sizeof(junk), pixels, sizeof(pixels)) == 0,
              "a record shorter than a palette plus header is refused");
    }
    {
        static unsigned char rec[CGL_PAL_BYTES + 8];
        memset(rec, 0, sizeof(rec));
        rec[CGL_PAL_BYTES + 2] = 0x10;
        check(cgl_decode(rec, sizeof(rec), pixels, 16) == 0,
              "a declared size larger than the destination is refused");
    }
    {
        static unsigned char rec[CGL_PAL_BYTES + 8];
        memset(rec, 0, sizeof(rec));
        check(cgl_decode(rec, sizeof(rec), pixels, sizeof(pixels)) == 0,
              "a declared size of zero is refused");
    }
    check(cgl_decode(0, 100, pixels, sizeof(pixels)) == 0, "a null record is refused");

    printf(fails ? "%d FAILED\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
