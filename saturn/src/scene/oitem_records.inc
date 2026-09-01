/*----------------------
 | oitem_records.inc
 | Description: GENERATED FILE -- do not edit by hand; produced by
 |   tools/gen_oitem.py. The byte offset and length of every LZSS
 |   record in the Japanese Zork I disc's OITEM.CZ. Records 0..18 are
 |   64x80 8bpp pictures; records 19..37 are their RGB555 CLUTs, one
 |   per picture, so picture i pairs with record OITEM_PIC_N + i.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned long offset;
    unsigned long length;
} OitemRecord;
#define OITEM_RECORD_N 38
#define OITEM_PIC_N 19
#define OITEM_PIC_BYTES 5120
#define OITEM_PAL_BYTES 512
static const OitemRecord OITEM_RECORDS[OITEM_RECORD_N] = {
    { 0UL, 1309UL },
    { 1312UL, 1183UL },
    { 2496UL, 1406UL },
    { 3904UL, 1573UL },
    { 5480UL, 1503UL },
    { 6984UL, 2399UL },
    { 9384UL, 2047UL },
    { 11432UL, 1629UL },
    { 13064UL, 2356UL },
    { 15420UL, 1905UL },
    { 17328UL, 1988UL },
    { 19316UL, 2474UL },
    { 21792UL, 1433UL },
    { 23228UL, 1089UL },
    { 24320UL, 1407UL },
    { 25728UL, 2700UL },
    { 28428UL, 1311UL },
    { 29740UL, 1410UL },
    { 31152UL, 1826UL },
    { 32980UL, 130UL },
    { 33112UL, 151UL },
    { 33264UL, 409UL },
    { 33676UL, 580UL },
    { 34256UL, 328UL },
    { 34584UL, 580UL },
    { 35164UL, 580UL },
    { 35744UL, 528UL },
    { 36272UL, 124UL },
    { 36396UL, 445UL },
    { 36844UL, 411UL },
    { 37256UL, 539UL },
    { 37796UL, 551UL },
    { 38348UL, 268UL },
    { 38616UL, 338UL },
    { 38956UL, 572UL },
    { 39528UL, 353UL },
    { 39884UL, 580UL },
    { 40464UL, 374UL },
};
