/*----------------------
 | test_oitem.c
 | Description: The C port of the OITEM.CZ decoder against checksums taken from
 |   the Python decoder in tools/gen_oitem.py, over the real archive in
 |   analysis/zork_bg/raw. Run from the repository root:
 |   gcc -O2 -I saturn/src -I saturn/tests -o /tmp/toitem \
 |       saturn/tests/test_oitem.c saturn/src/video/oitem.c \
 |       saturn/src/video/cgl.c && /tmp/toitem
 | Author: suinevere
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "video/oitem.h"
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
    int           picture;
    unsigned long pixel_sum;
    unsigned long pal_sum;
} OitemExpect;

static const OitemExpect EXPECT[] = {
#include "fixtures/oitem_sums.inc"
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
    static unsigned char pixels[OITEM_PIC_BYTES];
    static unsigned char palbytes[OITEM_PAL_BYTES];
    unsigned short clut[256];
    unsigned long len = 0;
    unsigned char *blob = slurp("analysis/zork_bg/raw/OITEM.CZ", &len);
    int i;

    if (blob == NULL) { printf("FAIL cannot read analysis/zork_bg/raw/OITEM.CZ\n"); return 1; }

    check(oitem_count() == EXPECT_N, "oitem_count matches the fixture row count");
    check(EXPECT_N == 19, "the fixture carries nineteen pictures");

    for (i = 0; i < EXPECT_N; i++) {
        int j;
        check(oitem_decode(blob, len, EXPECT[i].picture, pixels, clut) == 1,
              "oitem_decode accepts a valid picture");
        for (j = 0; j < 256; j++) {
            palbytes[j * 2]     = (unsigned char) (clut[j] & 0xff);
            palbytes[j * 2 + 1] = (unsigned char) ((clut[j] >> 8) & 0xff);
        }
        if (fnv1a(pixels, OITEM_PIC_BYTES) != EXPECT[i].pixel_sum) {
            printf("FAIL picture %d pixels\n", EXPECT[i].picture); fails++;
        }
        if (fnv1a(palbytes, OITEM_PAL_BYTES) != EXPECT[i].pal_sum) {
            printf("FAIL picture %d palette\n", EXPECT[i].picture); fails++;
        }
    }

    check(oitem_decode(NULL, len, 0, pixels, clut) == 0, "refuses a null archive");
    check(oitem_decode(blob, len, 0, NULL, clut) == 0, "refuses a null pixel buffer");
    check(oitem_decode(blob, len, 0, pixels, NULL) == 0, "refuses a null palette");
    check(oitem_decode(blob, len, -1, pixels, clut) == 0, "refuses a negative index");
    check(oitem_decode(blob, len, OITEM_PIC_N, pixels, clut) == 0, "refuses an index past the end");
    check(oitem_decode(blob, 100, 0, pixels, clut) == 0, "refuses an archive too short for the record");

    free(blob);
    printf(fails ? "%d FAILURES\n" : "all pass\n", fails);
    return fails ? 1 : 0;
}
