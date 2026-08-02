#include "../src/video/loading_text.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void test_fixed_lines_match_exactly(void) {
    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build("ZORK I", lines);

    assert(strcmp(lines[0], "**** SEGA SATURN 32-BIT OS V1.00 ****") == 0);
    assert(strcmp(lines[1], "") == 0);
    assert(strcmp(lines[2], "2048K RAM SYSTEM  2093056 SYS BYTES FREE") == 0);
    assert(strcmp(lines[3], "") == 0);
    assert(strcmp(lines[4], "READY!") == 0);
    assert(strcmp(lines[6], "") == 0);
    assert(strcmp(lines[8], "LOADING FROM CD-ROM BLOCK...") == 0);
    assert(strcmp(lines[9], "READY!") == 0);
    assert(strcmp(lines[10], "RUN") == 0);
}

static void test_short_title_appears_in_full(void) {
    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build("ZORK I", lines);

    assert(strcmp(lines[5], "LOAD ZORK I,8,1") == 0);
    assert(strcmp(lines[7], "SEARCHING FOR ZORK I") == 0);
}

static void test_max_length_title_load_line_fits_exactly(void) {
    /* 31 chars -- the catalogue's MENU_ROW_TEXT_MAX cap on a display title. */
    const char *title31 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";
    assert(strlen(title31) == 31);

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title31, lines);

    assert(strcmp(lines[5], "LOAD ABCDEFGHIJKLMNOPQRSTUVWXYZ01234,8,1") == 0);
    assert(strlen(lines[5]) == 40);
}

static void test_max_length_title_truncates_on_searching_line(void) {
    const char *title31 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title31, lines);

    /* "SEARCHING FOR " is 14 cols, leaving a 26-char budget. */
    assert(strcmp(lines[7], "SEARCHING FOR ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0);
    assert(strlen(lines[7]) == 40);
}

static void test_overlong_title_truncates_safely(void) {
    char title60[61];
    memset(title60, 'A', 60);
    title60[60] = '\0';

    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(title60, lines);

    assert(strlen(lines[5]) == 40);
    assert(strlen(lines[7]) == 40);
    assert(strcmp(lines[5], "LOAD AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA,8,1") == 0);
    assert(strcmp(lines[7], "SEARCHING FOR AAAAAAAAAAAAAAAAAAAAAAAAAA") == 0);
}

static void test_null_title_treated_as_empty(void) {
    char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
    loading_text_build(NULL, lines);

    assert(strcmp(lines[5], "LOAD ,8,1") == 0);
    assert(strcmp(lines[7], "SEARCHING FOR ") == 0);
}

static void test_no_line_ever_exceeds_console_width(void) {
    const char *titles[] = { "", "ZORK I", "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234" };
    for (int t = 0; t < 3; t++) {
        char lines[LOADING_TEXT_LINES][LOADING_TEXT_COLS + 1];
        loading_text_build(titles[t], lines);
        for (int row = 0; row < LOADING_TEXT_LINES; row++) {
            assert(strlen(lines[row]) <= LOADING_TEXT_COLS);
        }
    }
}

int main(void) {
    test_fixed_lines_match_exactly();
    test_short_title_appears_in_full();
    test_max_length_title_load_line_fits_exactly();
    test_max_length_title_truncates_on_searching_line();
    test_overlong_title_truncates_safely();
    test_null_title_treated_as_empty();
    test_no_line_ever_exceeds_console_width();
    printf("test_loading_text: OK\n");
    return 0;
}
