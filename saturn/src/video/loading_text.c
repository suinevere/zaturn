/*----------------------
 | loading_text.c
 | Description: See loading_text.h.
 | Author: suinevere
 | Dependencies: loading_text.h
 ----------------------*/
#include "loading_text.h"

/*----------------------
 | put_line
 | Description: Copies `prefix`, then as much of `title` as fits in the
 |   columns left over after `prefix` and `suffix`, then `suffix`, into
 |   `out` (a LOADING_TEXT_COLS+1 byte row), NUL-terminated. Every copy loop
 |   is bounded by LOADING_TEXT_COLS as a hard backstop, independent of the
 |   budget arithmetic, since this writes into a fixed-size caller buffer.
 | Author: suinevere
 | Params: out -- LOADING_TEXT_COLS+1 bytes; prefix, title, suffix -- NUL-
 |   terminated, none may be NULL (title is NULL-checked by the caller)
 | Returns: N/A
 ----------------------*/
static void put_line(char *out, const char *prefix, const char *title, const char *suffix) {
    int i = 0;
    for (const char *p = prefix; *p && i < LOADING_TEXT_COLS; p++) out[i++] = *p;

    int suffix_len = 0;
    while (suffix[suffix_len]) suffix_len++;

    int budget = LOADING_TEXT_COLS - i - suffix_len;
    if (budget < 0) budget = 0;
    for (const char *t = title; *t && budget > 0 && i < LOADING_TEXT_COLS; t++, budget--) out[i++] = *t;

    for (const char *s = suffix; *s && i < LOADING_TEXT_COLS; s++) out[i++] = *s;
    out[i] = '\0';
}

/*----------------------
 | loading_text_build
 | Description: See loading_text.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: title -- the story's title; lines -- destination block, filled in full
 | Returns: N/A
 ----------------------*/
void loading_text_build(const char *title, char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1]) {
    if (!title) title = "";

    put_line(lines[0],  "**** SEGA SATURN 32-BIT OS V1.00 ****", "", "");
    put_line(lines[1],  "", "", "");
    put_line(lines[2],  "2048K RAM SYSTEM  2093056 SYS BYTES FREE", "", "");
    put_line(lines[3],  "", "", "");
    put_line(lines[4],  "READY!", "", "");
    put_line(lines[5],  "LOAD ", title, ",8,1");
    put_line(lines[6],  "", "", "");
    put_line(lines[7],  "SEARCHING FOR ", title, "");
    put_line(lines[8],  "LOADING FROM CD-ROM BLOCK...", "", "");
    put_line(lines[9],  "READY!", "", "");
    put_line(lines[10], "RUN", "", "");
}
