# Zork I (Sega Saturn, Japan) — room background images

## What the backgrounds are

Every room background lives in one of eleven `B*.CGL` archives on data track 1.
A `.CGL` is a back-to-back chain of 4-byte-aligned records:

    [256-entry RGB555 LE CLUT = 512 bytes][LZSS stream -> 320x240 8bpp pixels]

The LZSS is the same Okumura variant the disc uses for `*.CGZ` / `*.SLD`
(4-byte LE decompressed size, 4 KiB ring initialised to 0, write pointer at
n-18) — see `analysis/zork_cgz.py`. Each frame carries its own palette, which
is why the areas are colour-graded differently. All 75 frames are 320x240.

| Archive | Frames | Area |
|---|---|---|
| `BHUS.CGL` | 6 | White House (exterior + interior) |
| `BWOD.CGL` | 4 | Forest |
| `BBAR.CGL` | 2 | Stone Barrow |
| `BCEL.CGL` | 15 | Cellar / Round Room / Cyclops |
| `BMAZ.CGL` | 3 | Maze |
| `BDAM.CGL` | 10 | Dam and Reservoir |
| `BMIR.CGL` | 4 | Mirror / cave passages |
| `BDED.CGL` | 2 | Hades (source dir `BHDS`) |
| `BTMP.CGL` | 6 | Temple / Egypt / Dome |
| `BRIV.CGL` | 14 | Frigid River / Canyon / Falls |
| `BMIN.CGL` | 9 | Coal Mine |

Decoder: `analysis/zork_cgl.py`. Extractor + table builder:
`analysis/zork_room_backgrounds.py` → `analysis/zork_bg/png/` (75 PNGs) and
`analysis/zork_bg/room_backgrounds.csv`.

## Item pictures

`OITEM.CZ` is the other image container on the disc, and it uses the same LZSS as
the backgrounds but a flat layout rather than interleaved palettes: nineteen
streams of 5120 bytes (64×80 at 8bpp), then nineteen 512-byte RGB555 CLUTs, one
per picture. These are the objects drawn in the inventory panel. The manifest in
`0ZORK.BIN` calls them `ETC\ITEM.CGD` + `ETC\ITEM.COL`.

The same extractor run writes them to `analysis/zork_ui/items/` (`item_00`–
`item_18`); the decoder lives in `analysis/zork_ui_rip.py`. The names the game
attaches to each picture are not decoded yet, so they are indexed in disc order.

Nothing else the HUD draws is file-backed — the mode tabs, movement compass,
command buttons, text plates and the map screen are all render-to-texture into
VDP1 VRAM, and the `ETC\MAPBG.CGD` / `ETC\WINDOW.CGD` style names in the same
manifest have no matching file in the ISO. Recovering those needs a Mednafen
savestate; see `analysis/zork_ui_rip.py` for the cel-slicing path.

## How a room picks its background

`0ZORK.BIN` holds one 16-byte presentation record per room at file offset
`0x75060` (address `0x06079060`), 110 rooms, indexed 0-109:

| Offset | Type | Meaning |
|---|---|---|
| +0 | u16 | CD-DA track number (0 = silence) |
| +2 | u16 | SE bank index (`SEALL SEMINA SEMINB SEMIR SEDAM SECEL SEHDS SERIV SEWOD SEMAZ SEBAR`) |
| +4 | u16 | area index, alphabetical (`BBAR BCEL BDAM BDED BHUS BMAZ BMIN BMIR BRIV BTMP BWOD`) |
| +6 | u16 | unused (always 0) |
| +8 | u32 | byte length of the frame record inside the archive |
| +12 | u32 | byte offset of the frame record inside the archive |

Confirmed against the loader at `0x0600b180`, which computes
`src = cgl_buffer + record[+12] + 512` — the `+512` steps over the frame's own
CLUT to reach the LZSS stream. The archive filename and its file size come from
two parallel 11-entry tables at `0x06079750` and `0x0607977c`, indexed by +4.

Room titles come from the game's own `msg777` table: entry `3 * room` is the
header printed on entry (`msg777[36]` = "West of House" → room 12). All 110
land on that stride. Four rooms (0, 5, 41, 92) keep their title inside a
multi-part `<1c>` message bank instead, and are resolved from the bank text plus
the room's area — those four are marked below.

Cross-checks that fell out for free: the two Mirror Rooms share one image, the
two Shafts share one, North and South of House share the boarded-window shot,
and the four Coal Mine rooms share one — exactly the rooms the original game
describes identically.

`BBAR_01.png` is the only frame no room references; it belongs to the
`HUS_BAR.TPG` transition / ending sequence.

## Room → image

| # | Room title | Archive | Frame | Image | CD track | SE bank |
|---|---|---|---|---|---|---|
| 0 | CLEARING * | BWOD.CGL | 1 | `BWOD_01.png` | 11 | SEWOD |
| 1 | BEHIND HOUSE | BHUS.CGL | 2 | `BHUS_02.png` | 10 | SEALL |
| 2 | FOREST | BWOD.CGL | 0 | `BWOD_00.png` | 11 | SEWOD |
| 3 | FOREST | BWOD.CGL | 0 | `BWOD_00.png` | 11 | SEWOD |
| 4 | FOREST | BWOD.CGL | 0 | `BWOD_00.png` | 11 | SEWOD |
| 5 | CLEARING * | BWOD.CGL | 1 | `BWOD_01.png` | 11 | SEWOD |
| 6 | FOREST | BWOD.CGL | 0 | `BWOD_00.png` | 11 | SEWOD |
| 7 | NORTH OF HOUSE | BHUS.CGL | 1 | `BHUS_01.png` | 10 | SEALL |
| 8 | FOREST PATH | BWOD.CGL | 2 | `BWOD_02.png` | 11 | SEWOD |
| 9 | SOUTH OF HOUSE | BHUS.CGL | 1 | `BHUS_01.png` | 10 | SEALL |
| 10 | BARROW ENTRANCE | BBAR.CGL | 0 | `BBAR_00.png` | 0 | SEBAR |
| 11 | UP A TREE | BWOD.CGL | 3 | `BWOD_03.png` | 11 | SEWOD |
| 12 | WEST OF HOUSE | BHUS.CGL | 0 | `BHUS_00.png` | 10 | SEALL |
| 13 | ATTIC | BHUS.CGL | 5 | `BHUS_05.png` | 10 | SEALL |
| 14 | KITCHEN | BHUS.CGL | 3 | `BHUS_03.png` | 10 | SEALL |
| 15 | LIVING ROOM | BHUS.CGL | 4 | `BHUS_04.png` | 10 | SEALL |
| 16 | CELLAR | BCEL.CGL | 7 | `BCEL_07.png` | 4 | SECEL |
| 17 | EAST OF CHASM | BCEL.CGL | 8 | `BCEL_08.png` | 4 | SECEL |
| 18 | GALLERY | BCEL.CGL | 9 | `BCEL_09.png` | 4 | SECEL |
| 19 | STUDIO | BCEL.CGL | 10 | `BCEL_10.png` | 4 | SECEL |
| 20 | TROLL ROOM | BCEL.CGL | 6 | `BCEL_06.png` | 4 | SECEL |
| 21 | DEAD END | BMAZ.CGL | 2 | `BMAZ_02.png` | 20 | SEMAZ |
| 22 | DEAD END | BMAZ.CGL | 2 | `BMAZ_02.png` | 20 | SEMAZ |
| 23 | DEAD END | BMAZ.CGL | 2 | `BMAZ_02.png` | 20 | SEMAZ |
| 24 | DEAD END | BMAZ.CGL | 2 | `BMAZ_02.png` | 20 | SEMAZ |
| 25 | GRATING ROOM | BCEL.CGL | 14 | `BCEL_14.png` | 0 | SEMAZ |
| 26 | MAZE | BMAZ.CGL | 0 | `BMAZ_00.png` | 20 | SEMAZ |
| 27 | MAZE | BMAZ.CGL | 0 | `BMAZ_00.png` | 20 | SEMAZ |
| 28 | MAZE | BMAZ.CGL | 0 | `BMAZ_00.png` | 20 | SEMAZ |
| 29 | MAZE | BMAZ.CGL | 0 | `BMAZ_00.png` | 20 | SEMAZ |
| 30 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 31 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 32 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 33 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 34 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 35 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 36 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 37 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 38 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 39 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 40 | MAZE | BMAZ.CGL | 1 | `BMAZ_01.png` | 20 | SEMAZ |
| 41 | CYCLOPS ROOM * | BCEL.CGL | 12 | `BCEL_12.png` | 4 | SECEL |
| 42 | NARROW PASSAGE | BCEL.CGL | 11 | `BCEL_11.png` | 4 | SECEL |
| 43 | TREASURE ROOM | BCEL.CGL | 13 | `BCEL_13.png` | 4 | SECEL |
| 44 | STREAM | BDAM.CGL | 8 | `BDAM_08.png` | 12 | SEDAM |
| 45 | RESERVOIR | BDAM.CGL | 6 | `BDAM_06.png` | 12 | SEDAM |
| 46 | RESERVOIR NORTH | BDAM.CGL | 5 | `BDAM_05.png` | 12 | SEDAM |
| 47 | RESERVOIR SOUTH | BDAM.CGL | 7 | `BDAM_07.png` | 12 | SEDAM |
| 48 | STREAM VIEW | BDAM.CGL | 9 | `BDAM_09.png` | 12 | SEDAM |
| 49 | ATLANTIS ROOM | BCEL.CGL | 1 | `BCEL_01.png` | 0 | SECEL |
| 50 | COLD PASSAGE | BMIR.CGL | 0 | `BMIR_00.png` | 3 | SEMIR |
| 51 | MIRROR ROOM | BMIR.CGL | 1 | `BMIR_01.png` | 3 | SEMIR |
| 52 | MIRROR ROOM | BMIR.CGL | 1 | `BMIR_01.png` | 3 | SEMIR |
| 53 | NARROW PASSAGE | BMIR.CGL | 0 | `BMIR_00.png` | 3 | SEMIR |
| 54 | SHAFT | BMIR.CGL | 2 | `BMIR_02.png` | 3 | SEMIR |
| 55 | SHAFT | BMIR.CGL | 2 | `BMIR_02.png` | 3 | SEMIR |
| 56 | WINDING PASSAGE | BMIR.CGL | 3 | `BMIR_03.png` | 3 | SEMIR |
| 57 | CURVED PASSAGE | BMIR.CGL | 3 | `BMIR_03.png` | 3 | SEMIR |
| 58 | CHASM | BCEL.CGL | 0 | `BCEL_00.png` | 4 | SECEL |
| 59 | DAMP CAVE | BRIV.CGL | 3 | `BRIV_03.png` | 6 | SERIV |
| 60 | DEEP CANYON | BDAM.CGL | 3 | `BDAM_03.png` | 8 | SEDAM |
| 61 | EAST-WEST PASSAGE | BCEL.CGL | 5 | `BCEL_05.png` | 4 | SECEL |
| 62 | LOUD ROOM | BDAM.CGL | 4 | `BDAM_04.png` | 8 | SEDAM |
| 63 | NORTH-SOUTH PASSAGE | BCEL.CGL | 3 | `BCEL_03.png` | 4 | SECEL |
| 64 | ROUND ROOM | BCEL.CGL | 4 | `BCEL_04.png` | 4 | SECEL |
| 65 | ENTRANCE TO HADES | BDED.CGL | 0 | `BDED_00.png` | 19 | SEHDS |
| 66 | LAND OF THE LIVING DEAD | BDED.CGL | 1 | `BDED_01.png` | 19 | SEHDS |
| 67 | DOME ROOM | BTMP.CGL | 1 | `BTMP_01.png` | 2 | SEALL |
| 68 | EGYPT ROOM | BTMP.CGL | 5 | `BTMP_05.png` | 2 | SEALL |
| 69 | ENGRAVINGS CAVE | BTMP.CGL | 0 | `BTMP_00.png` | 2 | SEALL |
| 70 | TEMPLE | BTMP.CGL | 3 | `BTMP_03.png` | 2 | SEALL |
| 71 | ALTAR | BTMP.CGL | 4 | `BTMP_04.png` | 2 | SEALL |
| 72 | TORCH ROOM | BTMP.CGL | 2 | `BTMP_02.png` | 2 | SEALL |
| 73 | DAM LOBBY | BDAM.CGL | 1 | `BDAM_01.png` | 8 | SEDAM |
| 74 | FLOOD CONTROL DAM | BDAM.CGL | 2 | `BDAM_02.png` | 8 | SEDAM |
| 75 | CONTROL ROOM | BDAM.CGL | 0 | `BDAM_00.png` | 8 | SEDAM |
| 76 | ARAGAIN FALLS | BRIV.CGL | 8 | `BRIV_08.png` | 6 | SERIV |
| 77 | CANYON BOTTOM | BRIV.CGL | 11 | `BRIV_11.png` | 6 | SERIV |
| 78 | CANYON VIEW | BRIV.CGL | 13 | `BRIV_13.png` | 6 | SERIV |
| 79 | LEDGE | BRIV.CGL | 12 | `BRIV_12.png` | 6 | SERIV |
| 80 | BASE OF DAM | BRIV.CGL | 0 | `BRIV_00.png` | 0 | SERIV |
| 81 | END OF RAINBOW | BRIV.CGL | 10 | `BRIV_10.png` | 6 | SERIV |
| 82 | ON THE RAINBOW | BRIV.CGL | 9 | `BRIV_09.png` | 6 | SERIV |
| 83 | FRIGID RIVER | BRIV.CGL | 1 | `BRIV_01.png` | 0 | SERIV |
| 84 | FRIGID RIVER | BRIV.CGL | 1 | `BRIV_01.png` | 0 | SERIV |
| 85 | FRIGID RIVER | BRIV.CGL | 1 | `BRIV_01.png` | 0 | SERIV |
| 86 | FRIGID RIVER | BRIV.CGL | 1 | `BRIV_01.png` | 0 | SERIV |
| 87 | FRIGID RIVER | BRIV.CGL | 2 | `BRIV_02.png` | 0 | SERIV |
| 88 | SANDY BEACH | BRIV.CGL | 6 | `BRIV_06.png` | 6 | SERIV |
| 89 | SANDY CAVE | BRIV.CGL | 7 | `BRIV_07.png` | 6 | SERIV |
| 90 | SHORE | BRIV.CGL | 5 | `BRIV_05.png` | 6 | SERIV |
| 91 | WHITE CLIFFS BEACH | BRIV.CGL | 4 | `BRIV_04.png` | 6 | SERIV |
| 92 | WHITE CLIFFS BEACH * | BRIV.CGL | 4 | `BRIV_04.png` | 6 | SERIV |
| 93 | BAT ROOM | BMIN.CGL | 1 | `BMIN_01.png` | 31 | SEMINA |
| 94 | DEAD END | BMIN.CGL | 5 | `BMIN_05.png` | 18 | SEMINA |
| 95 | GAS ROOM | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 96 | LADDER BOTTOM | BMIN.CGL | 3 | `BMIN_03.png` | 18 | SEMINB |
| 97 | LADDER TOP | BMIN.CGL | 4 | `BMIN_04.png` | 18 | SEMINB |
| 98 | DRAFTY ROOM | BMIN.CGL | 7 | `BMIN_07.png` | 18 | SEMINA |
| 99 | MACHINE ROOM | BMIN.CGL | 8 | `BMIN_08.png` | 18 | SEMINA |
| 100 | MINE ENTRANCE | BMIN.CGL | 0 | `BMIN_00.png` | 18 | SEMINA |
| 101 | SHAFT ROOM | BMIN.CGL | 2 | `BMIN_02.png` | 18 | SEMINA |
| 102 | FOUL ROOM | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 103 | SQUEAKY ROOM | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 104 | TIMBER ROOM | BMIN.CGL | 6 | `BMIN_06.png` | 18 | SEMINA |
| 105 | COAL MINE | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 106 | COAL MINE | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 107 | COAL MINE | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 108 | COAL MINE | BMIN.CGL | 1 | `BMIN_01.png` | 18 | SEMINA |
| 109 | SLIDE ROOM | BCEL.CGL | 2 | `BCEL_02.png` | 0 | SECEL |

`*` title inferred from a multi-part message bank rather than a plain `msg777[3n]` string.

## Image → rooms

| Image | Rooms |
|---|---|
| `BBAR_00.png` | 10 BARROW ENTRANCE |
| `BCEL_00.png` | 58 CHASM |
| `BCEL_01.png` | 49 ATLANTIS ROOM |
| `BCEL_02.png` | 109 SLIDE ROOM |
| `BCEL_03.png` | 63 NORTH-SOUTH PASSAGE |
| `BCEL_04.png` | 64 ROUND ROOM |
| `BCEL_05.png` | 61 EAST-WEST PASSAGE |
| `BCEL_06.png` | 20 TROLL ROOM |
| `BCEL_07.png` | 16 CELLAR |
| `BCEL_08.png` | 17 EAST OF CHASM |
| `BCEL_09.png` | 18 GALLERY |
| `BCEL_10.png` | 19 STUDIO |
| `BCEL_11.png` | 42 NARROW PASSAGE |
| `BCEL_12.png` | 41 CYCLOPS ROOM |
| `BCEL_13.png` | 43 TREASURE ROOM |
| `BCEL_14.png` | 25 GRATING ROOM |
| `BDAM_00.png` | 75 CONTROL ROOM |
| `BDAM_01.png` | 73 DAM LOBBY |
| `BDAM_02.png` | 74 FLOOD CONTROL DAM |
| `BDAM_03.png` | 60 DEEP CANYON |
| `BDAM_04.png` | 62 LOUD ROOM |
| `BDAM_05.png` | 46 RESERVOIR NORTH |
| `BDAM_06.png` | 45 RESERVOIR |
| `BDAM_07.png` | 47 RESERVOIR SOUTH |
| `BDAM_08.png` | 44 STREAM |
| `BDAM_09.png` | 48 STREAM VIEW |
| `BDED_00.png` | 65 ENTRANCE TO HADES |
| `BDED_01.png` | 66 LAND OF THE LIVING DEAD |
| `BHUS_00.png` | 12 WEST OF HOUSE |
| `BHUS_01.png` | 7 NORTH OF HOUSE; 9 SOUTH OF HOUSE |
| `BHUS_02.png` | 1 BEHIND HOUSE |
| `BHUS_03.png` | 14 KITCHEN |
| `BHUS_04.png` | 15 LIVING ROOM |
| `BHUS_05.png` | 13 ATTIC |
| `BMAZ_00.png` | 26 MAZE; 27 MAZE; 28 MAZE; 29 MAZE |
| `BMAZ_01.png` | 30 MAZE; 31 MAZE; 32 MAZE; 33 MAZE; 34 MAZE; 35 MAZE; 36 MAZE; 37 MAZE; 38 MAZE; 39 MAZE; 40 MAZE |
| `BMAZ_02.png` | 21 DEAD END; 22 DEAD END; 23 DEAD END; 24 DEAD END |
| `BMIN_00.png` | 100 MINE ENTRANCE |
| `BMIN_01.png` | 93 BAT ROOM; 95 GAS ROOM; 102 FOUL ROOM; 103 SQUEAKY ROOM; 105 COAL MINE; 106 COAL MINE; 107 COAL MINE; 108 COAL MINE |
| `BMIN_02.png` | 101 SHAFT ROOM |
| `BMIN_03.png` | 96 LADDER BOTTOM |
| `BMIN_04.png` | 97 LADDER TOP |
| `BMIN_05.png` | 94 DEAD END |
| `BMIN_06.png` | 104 TIMBER ROOM |
| `BMIN_07.png` | 98 DRAFTY ROOM |
| `BMIN_08.png` | 99 MACHINE ROOM |
| `BMIR_00.png` | 50 COLD PASSAGE; 53 NARROW PASSAGE |
| `BMIR_01.png` | 51 MIRROR ROOM; 52 MIRROR ROOM |
| `BMIR_02.png` | 54 SHAFT; 55 SHAFT |
| `BMIR_03.png` | 56 WINDING PASSAGE; 57 CURVED PASSAGE |
| `BRIV_00.png` | 80 BASE OF DAM |
| `BRIV_01.png` | 83 FRIGID RIVER; 84 FRIGID RIVER; 85 FRIGID RIVER; 86 FRIGID RIVER |
| `BRIV_02.png` | 87 FRIGID RIVER |
| `BRIV_03.png` | 59 DAMP CAVE |
| `BRIV_04.png` | 91 WHITE CLIFFS BEACH; 92 WHITE CLIFFS BEACH |
| `BRIV_05.png` | 90 SHORE |
| `BRIV_06.png` | 88 SANDY BEACH |
| `BRIV_07.png` | 89 SANDY CAVE |
| `BRIV_08.png` | 76 ARAGAIN FALLS |
| `BRIV_09.png` | 82 ON THE RAINBOW |
| `BRIV_10.png` | 81 END OF RAINBOW |
| `BRIV_11.png` | 77 CANYON BOTTOM |
| `BRIV_12.png` | 79 LEDGE |
| `BRIV_13.png` | 78 CANYON VIEW |
| `BTMP_00.png` | 69 ENGRAVINGS CAVE |
| `BTMP_01.png` | 67 DOME ROOM |
| `BTMP_02.png` | 72 TORCH ROOM |
| `BTMP_03.png` | 70 TEMPLE |
| `BTMP_04.png` | 71 ALTAR |
| `BTMP_05.png` | 68 EGYPT ROOM |
| `BWOD_00.png` | 2 FOREST; 3 FOREST; 4 FOREST; 6 FOREST |
| `BWOD_01.png` | 0 CLEARING; 5 CLEARING |
| `BWOD_02.png` | 8 FOREST PATH |
| `BWOD_03.png` | 11 UP A TREE |
| `BBAR_01.png` | *(unused by any room)* |
