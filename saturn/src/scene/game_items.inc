/*----------------------
 | game_items.inc
 | Description: GENERATED FILE -- do not edit by hand; produced by
 |   tools/gen_items.py. Which of OITEM.CZ's nineteen item pictures
 |   each object gets, keyed by release and serial. picture is a
 |   0-based index into the container; an object with no row has no
 |   picture and takes the blank plate. Zork I is the only game with a
 |   table: these are portraits of its own treasures, not a pool other
 |   stories could draw from.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short obj;
    unsigned char  picture;
} ItemPicture;
typedef struct {
    unsigned short     release;
    const char        *serial;
    const ItemPicture *items;
    unsigned short     count;
} GameItemMap;
#define ITEM_GAME_N 1
#define ITEM_PIC_N 19
static const ItemPicture ITEMS_ZORK1[19] = {
    { 84, 3 },
    { 85, 13 },
    { 87, 7 },
    { 101, 18 },
    { 104, 16 },
    { 116, 12 },
    { 125, 2 },
    { 137, 10 },
    { 139, 4 },
    { 149, 11 },
    { 163, 8 },
    { 165, 5 },
    { 170, 9 },
    { 171, 6 },
    { 188, 17 },
    { 191, 14 },
    { 208, 1 },
    { 209, 0 },
    { 231, 15 },
};
static const GameItemMap GAME_ITEM_MAP[ITEM_GAME_N] = {
    { 88, "840726", ITEMS_ZORK1, 19 },
};
