/*----------------------
 | dump_shape.c
 | Description: Host driver that prints sentence_shape.c's decode of a story's
 |   verb grammar, one line per row as "verb nobj prep1 prep2", for the Python
 |   oracle in test_sentence_shape.py to compare against its own decode.
 | Author: suinevere
 | Build: gcc -std=c11 -Wall -Wextra -o /tmp/tds.exe saturn/tests/dump_shape.c \
 |          saturn/src/input/sentence_shape.c saturn/src/input/typeahead.c \
 |          saturn/src/input/typeahead_extract.c \
 |          && /tmp/tds.exe saturn/cd/data/Z3/ZORK1.Z3
 ----------------------*/
#include "../src/input/sentence_shape.h"
#include "../src/input/typeahead_extract.h"
#include <stdio.h>
#include <stdlib.h>

void* typeahead_malloc(unsigned int size) { return malloc(size); }
void typeahead_free(void* ptr) { free(ptr); }

int main(int argc, char **argv) {
    FILE *f;
    long len;
    unsigned char *story;
    TrieNode *root;
    unsigned int p;
    int nsep, elen, count, k;
    if (argc < 2) return 2;
    f = fopen(argv[1], "rb");
    if (f == NULL) return 2;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    story = (unsigned char *) malloc((size_t) len);
    if (story == NULL || fread(story, 1, (size_t) len, f) != (size_t) len) return 2;
    fclose(f);

    root = create_trie_node();
    build_typeahead_from_story(root, story, (unsigned int) len);
    shape_build(story, (unsigned int) len, root);

    p = ((unsigned int) story[0x08] << 8) | story[0x09];
    nsep = story[p];
    p += 1 + (unsigned int) nsep;
    elen = story[p];
    p += 1;
    count = (int) (((unsigned int) story[p] << 8) | story[p + 1]);
    p += 2;
    for (k = 0; k < count; k++) {
        unsigned int off = p + (unsigned int) k * (unsigned int) elen;
        char text[12];
        ShapeRow rows[SHAPE_ROWS_MAX];
        int n, j;
        if ((story[off + 4] & 0x40) == 0) continue;
        {
            static const char a0[] = "      abcdefghijklmnopqrstuvwxyz";
            char buf[7];
            int w = 0, start, m;
            for (int half = 0; half < 2; half++) {
                unsigned int x = ((unsigned int) story[off + half * 2] << 8) | story[off + half * 2 + 1];
                buf[w++] = a0[(x >> 10) & 31];
                buf[w++] = a0[(x >> 5) & 31];
                buf[w++] = a0[x & 31];
            }
            buf[w] = '\0';
            while (w > 0 && buf[w - 1] == ' ') buf[--w] = '\0';
            start = 0;
            while (buf[start] == ' ') start++;
            m = 0;
            while (buf[start] != '\0') text[m++] = buf[start++];
            text[m] = '\0';
        }
        n = shape_verb_rows(text, rows, SHAPE_ROWS_MAX);
        for (j = 0; j < n; j++)
            printf("%s\t%d\t%d\t%d\n", text, rows[j].nobj, rows[j].prep1, rows[j].prep2);
    }
    shape_destroy();
    return 0;
}
