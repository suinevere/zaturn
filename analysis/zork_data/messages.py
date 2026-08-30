"""msg777 index -> English. Values are SEGMENT LISTS: str = literal English (word-wrapped at 16,
'\n' = paragraph break), int = 0x0e object token (dict234 index, renders the English dict word),
('r',idx) = 0x1e room token. Tokens render via dict234 (translated in dict_words.py).

Conventions used here:
 * The leading {room} association token each barrier message carries in JP is DROPPED (it would
   print an unwanted room-name prefix; Infocom barriers show only the text).
 * JP grammatical abbreviation control codes (<03> <11> <13> <16> ...) are replaced by full English.
 * No apostrophes (the renderer has no ' glyph) -> "CANNOT", "POSEIDONS", etc.
Decode source: analysis/zork_msg_decode.py (decode_segments shows token indices).
TODO (deferred): <1c>-separated message BANKS (0,339,360,363,468,754,755,756,764,771,774) need a raw
0x1c separator segment; and the two giant texts msg674 (dam guidebook) / msg680 (paper ad)."""

MSGS = {
    # ---- room-title messages (engine uses these as room name header on entry) ----
    # JP format: "<room name><barrier text>" -- we keep only the room name.
    # [0] and [15] are multi-bank clearing/grating barrier entries handled in the banks section.
    3:   "BEHIND HOUSE",            # 家の裏
    6:   "FOREST",                  # 森
    9:   "FOREST",                  # 森
    12:  "FOREST",                  # 森
    18:  "FOREST",                  # 森
    21:  "NORTH OF HOUSE",          # 家の北
    24:  "FOREST PATH",             # 森の小道
    27:  "SOUTH OF HOUSE",          # 家の南
    30:  "BARROW ENTRANCE",         # 石塚
    33:  "UP A TREE",               # 木の上
    36:  "WEST OF HOUSE",           # 家の西
    39:  "ATTIC",                   # 屋根裏部屋
    42:  "KITCHEN",                 # 台所
    45:  "LIVING ROOM",             # リビングルーム
    48:  "CELLAR",                  # 地下室
    51:  "EAST OF CHASM",           # 裂け目の東
    54:  "GALLERY",                 # ギャラリー
    57:  "STUDIO",                  # アトリエ
    60:  "TROLL ROOM",              # トロルの部屋
    63:  "DEAD END",                # 行き止まり
    66:  "DEAD END",                # 行き止まり
    69:  "DEAD END",                # 行き止まり
    72:  "DEAD END",                # 行き止まり
    75:  "GRATING ROOM",            # 鉄格子の部屋
    78:  "MAZE",                    # 迷路
    81:  "MAZE",                    # 迷路
    84:  "MAZE",                    # 迷路
    87:  "MAZE",                    # 迷路
    90:  "MAZE",                    # 迷路
    93:  "MAZE",                    # 迷路
    96:  "MAZE",                    # 迷路
    99:  "MAZE",                    # 迷路
    102: "MAZE",                    # 迷路
    105: "MAZE",                    # 迷路
    108: "MAZE",                    # 迷路
    111: "MAZE",                    # 迷路
    114: "MAZE",                    # 迷路
    117: "MAZE",                    # 迷路
    120: "MAZE",                    # 迷路
    126: "NARROW PASSAGE",          # 奇妙な通路
    129: "TREASURE ROOM",           # 宝の部屋
    132: "STREAM",                  # 小川
    135: "RESERVOIR",               # 貯水池
    138: "RESERVOIR NORTH",         # 貯水池北
    141: "RESERVOIR SOUTH",         # 貯水池南
    144: "STREAM VIEW",             # 小川の見える所
    147: "ATLANTIS ROOM",           # アトランティスの部屋
    150: "COLD PASSAGE",            # 寒い通路
    153: "MIRROR ROOM",             # 鏡の間
    156: "MIRROR ROOM",             # 鏡の間
    159: "NARROW PASSAGE",          # 狭い通路
    162: "SHAFT",                   # たて穴
    165: "SHAFT",                   # たて穴
    168: "WINDING PASSAGE",         # 曲がりくねった通路
    171: "CURVED PASSAGE",          # 湾曲した通路
    174: "CHASM",                   # 裂け目
    177: "DAMP CAVE",               # じめじめした部屋
    180: "DEEP CANYON",             # 深い峡谷
    183: "EAST-WEST PASSAGE",       # 東西の通路
    186: "LOUD ROOM",               # うるさい部屋
    189: "NORTH-SOUTH PASSAGE",     # 南北の道
    192: "ROUND ROOM",              # 円形の部屋
    195: "ENTRANCE TO HADES",       # 冥界の入口
    198: "LAND OF THE LIVING DEAD", # 死者の国
    201: "DOME ROOM",               # ドームの部屋
    204: "EGYPT ROOM",              # ファラオの部屋
    207: "ENGRAVINGS CAVE",         # 彫刻のある部屋
    210: "TEMPLE",                  # 寺院
    213: "ALTAR",                   # 祭壇
    216: "TORCH ROOM",              # たいまつの部屋
    219: "DAM LOBBY",               # ダムのロビー
    222: "FLOOD CONTROL DAM",       # ダム
    225: "CONTROL ROOM",            # 制御室
    228: "ARAGAIN FALLS",           # アラゲイン滝
    231: "CANYON BOTTOM",           # 峡谷の下
    234: "CANYON VIEW",             # 峡谷の見える所
    237: "LEDGE",                   # 岩だな
    240: "BASE OF DAM",             # ダムの底
    243: "END OF RAINBOW",          # 虹の果て
    246: "ON THE RAINBOW",          # 虹の上
    249: "FRIGID RIVER",            # フリジッド河
    252: "FRIGID RIVER",            # フリジッド河
    255: "FRIGID RIVER",            # フリジッド河
    258: "FRIGID RIVER",            # フリジッド河
    261: "FRIGID RIVER",            # フリジッド河
    264: "SANDY BEACH",             # 砂浜
    267: "SANDY CAVE",              # 砂のほら穴
    270: "SHORE",                   # 岸
    273: "WHITE CLIFFS BEACH",      # 白い絶壁の河原
    279: "BAT ROOM",                # コウモリの部屋
    282: "DEAD END",                # 行き止まり
    285: "GAS ROOM",                # ガス室
    288: "LADDER BOTTOM",           # はしごの下
    291: "LADDER TOP",              # はしごの上
    294: "DRAFTY ROOM",             # 風の吹く部屋
    297: "MACHINE ROOM",            # マシン室
    300: "MINE ENTRANCE",           # 炭坑の入口
    303: "SHAFT ROOM",              # たて穴の部屋
    306: "FOUL ROOM",               # 悪臭の部屋
    309: "SQUEAKY ROOM",            # 音がする部屋
    312: "TIMBER ROOM",             # 材木置き場
    315: "COAL MINE",               # 炭坑
    318: "COAL MINE",               # 炭坑
    321: "COAL MINE",               # 炭坑
    324: "COAL MINE",               # 炭坑
    327: "SLIDE ROOM",              # すべり台の部屋
    333: "GRANITE WALL",            # みかげ石の壁
    # ---- barriers / blocked-move messages ----
    123: "THE EAST WALL IS SOLID ROCK, AND IT WILL NOT LET YOU PASS.",
    336: ["THE ", 0xe1, " HERE IS TOO HARD TO DIG."],
    423: "SAY WHETHER YOU WANT TO GO UP OR DOWN.",
    763: "ALL THE WINDOWS ARE BOARDED UP.",
    765: ["A ", 0x23, " FELLED BY A STORM BLOCKS THE WAY. THE VIEW IS WONDERFUL, BUT JUMPING DOWN WOULD BE FRIGHTENING."],
    766: ["THERE IS NO ", 0x23, " HERE THAT YOU CAN CLIMB. TO GO FARTHER WEST YOU WILL NEED A MACHETE."],
    767: ["THE ", 0x86, " FENDS YOU OFF MENACINGLY."],
    768: ["THIS IS A WINDING PATH, PART OF THE ", 0xa4, ". ALL THE PATHS LOOK ALIKE."],
    769: "YOU DROWN.",
    770: "THE CHANNEL IS TOO NARROW.",
    772: ["THE ", 0x8c, " KEEP YOU FROM CLIMBING UP TO THE ", 0xc4, "."],
    773: ["THE SHORE BELOW THE ", 0x8c, "."],
    775: "THE CURRENT IS TOO STRONG TO GO UPSTREAM.",
    776: ["YOU CANNOT GET THROUGH THIS ", 0xe5, " CARRYING ALL THAT."],
    # ---- item descriptions ----
    479: ["WELCOME TO THE WORLD OF ", 0xe9, "!\n\nAND SO BEGINS AN INCREDIBLE TREASURE HUNT. HOW MANY TREASURES CAN YOU FIT IN THE TROPHY CASE? IT ALL DEPENDS ON YOUR WITS."],
    487: ["INSIDE THE EGG IS A ", 0x93, ". ITS EYES ARE RUBIES AND ITS BEAK IS SILVER. THROUGH A CRYSTAL WINDOW UNDER THE LEFT WING YOU CAN SEE INTRICATE MACHINERY, THOUGH IT IS NOT MOVING."],
    490: ["IN THE ", 0x8f, " IS A LARGE EGG ENCRUSTED WITH PRECIOUS ", 0x1a, ". A CHILDLESS ", 0x20, " MUST HAVE BROUGHT IT HERE. THE EGG IS WORKED IN GOLD AND SET WITH LAPIS AND PEARL, AND HINGED WITH A DELICATE CLASP. IT LOOKS VERY FRAGILE."],
    505: ["ON A BRANCH BESIDE YOU IS A SMALL ", 0x8f, "."],
    511: ["A BOTTLE IS ON THE ", 0x39, "."],
    523: ["A BATTERY-POWERED LAMP IS ON THE ", 0x41, "."],
    529: ["INSIDE THE ", 0x41, " IS A PARCHMENT THAT APPEARS TO BE A MAP."],
    530: ["THE MAP SHOWS A ", 0x22, " WITH THREE ", 0x9c, "S. THE LARGEST HAS A HOUSE; THREE ROADS LEAD FROM IT, AND ONE RUNNING SOUTHWEST IS LABELED: TO THE ", 0x4c, "."],
    532: ["IN A CORNER IS A LARGE COIL OF ", 0x70, "."],
    538: ["ON THE ", 0x39, " IS A LONG BROWN SACK. IT SMELLS OF HOT PEPPERS."],
    541: ["ABOVE THE ", 0x41, " HANGS A VERY OLD ", 0x7e, "."],
    554: "THE ENGRAVING READS: THIS PLACE HAS BEEN SEALED FOR A CERTAIN REASON.",
    559: ["TACKED LOOSELY TO THE WALL IS A PAPER ", 0x8a, "."],
    560: ["CONGRATULATIONS!\n\nYOU ARE THE PRIVILEGED OWNER OF ", 0xe9, " I, A GREAT UNDERGROUND EMPIRE THAT COMES COMPLETE WITH EVERYTHING NEEDED TO MAINTAIN IT. UNDER NORMAL USE, ", 0xe9, " I WILL RUN FOR MANY MONTHS WITHOUT BREAKING DOWN."],
    562: ["THIS COULD BE THE CHANCE OF A LIFETIME. ON THE FAR WALL HANGS A ", 0x61, " OF UNMATCHED BEAUTY."],
    574: ["HERE IS A USELESS, BURNED-OUT ", 0x11, " LEFT BY SOME DEAD ADVENTURER."],
    580: ["BESIDE THE ", 0x55, " IS A ", 0x71, "."],
    592: ["HALF BURIED IN THE MUD IS AN ", 0x85, " FILLED WITH ", 0x1a, "."],
    601: ["ON THE ", 0xc4, " LIES POSEIDONS OWN ", 0x45, "."],
    628: ["ON THE ", 0x31, " LIES A LARGE ", 0x32, ", OPEN TO PAGE 569."],
    631: ["AT EACH END OF THE ", 0x31, " A ", 0x62, " BURNS."],
    638: ["ON THE NATURAL STONE OF THE CAVE WALL AN UNKNOWN HAND HAS CARVED RELIEFS DEPICTING THE FAITH OF THE ANCIENT ", 0xe9, " PEOPLE. THEY SEEM TO COPY THE GREAT RELIGIOUS RITES OF THE TIME, BUT A LATER AGE, FINDING THEM BLASPHEMOUS, COVERED THEM WITH EQUALLY FINE CARVINGS."],
    644: ["THE ", 0x6a, " IS WRITTEN IN AN ARCHAIC SCRIPT RARELY USED TODAY. IT REBUKES THE ABSENT-MINDED, THE WORM-LIKE, AND THE FICKLE. ONE WHO RECITES THE ", 0x6a, " TO ITS LAST WORD IS CARRIED OFF TO THE LAND OF THE DEAD."],
    649: ["AN OLD ", 0x34, " THAT MAY ONCE HAVE BEEN EGYPTIAN LIES IN THE COFFIN, SET WITH BRIGHT RAINBOW ", 0x1a, " AND TAPERING TO A POINT. YOU MAY NOT NEED IT, BUT IF YOU WAVE IT, IT MIGHT EVEN PART THE SEA."],
    652: ["ON THE ", 0x82, " BURNS AN IVORY ", 0x83, "."],
    673: ["THE ", 0x84, "S ON THE FRONT DESK ARE TITLED: ", 0x0a, "."],
    695: "FROBOZZ MAGIC GUNK COMPANY\nALL-PURPOSE GUNK",
    703: ["THERE IS A ", 0x6f, " HERE (PROBABLY A WARNING)."],
    712: "AT THE END OF THE RAINBOW IS A POT OF GOLD.",
    # ---- boat label (single, multi-line) ----
    443: ["- FROBOZZ MAGIC ", 0x17, " COMPANY -\n\nHELLO, ", 0xe0, "!\n\nINSTRUCTIONS:\nTO SET OUT ON THE WATER, SHOUT LAUNCH.\nTO COME ASHORE, SHOUT LAND, OR SHOUT THE DIRECTION OF THE ", 0xc4, " YOU WANT.\n\nWARRANTY:\nTHIS ", 0x17, " IS GUARANTEED AGAINST ALL DEFECTS FOR 76 MILLISECONDS FROM PURCHASE, OR UNTIL FIRST USE, WHICHEVER COMES FIRST.\n\nWARNING:\nTHIS ", 0x17, " IS MADE OF THIN PLASTIC.\n\nGOOD LUCK!"],
    # ---- <1c> message BANKS (sub-messages joined by ('ctrl',0x1c); keep the exact separator count) ----
    0:   ["THE ", 0x50, " IS CLOSED.", ("ctrl", 0x1c), "YOU CANNOT GO THAT WAY.", ("ctrl", 0x1c)],
    15:  ["THE ", 0x50, " IS CLOSED.", ("ctrl", 0x1c), "YOU CANNOT GO THAT WAY.", ("ctrl", 0x1c)],
    339: ["THE ", 0xe2, " IS A SINISTER PRESENCE THAT LURKS IN THE DARK PLACES OF THE EARTH. ITS FAVORITE FOOD IS ADVENTURERS, BUT IT FEARS THE LIGHT OF DAY. NO ONE HAS SEEN A ", 0xe2, " BY DAYLIGHT, AND FEW HAVE SURVIVED ITS JAWS TO TELL THE TALE.", ("ctrl", 0x1c),
          "THERE IS NONE HERE, BUT AT LEAST ONE ", 0xe2, " LURKS IN THE NEARBY DARKNESS. DO NOT PUT OUT YOUR LIGHT!", ("ctrl", 0x1c),
          "IT HIDES ITSELF, BUT A ", 0xe2, " ALWAYS LURKS IN THE NEARBY DARKNESS.", ("ctrl", 0x1c)],
    360: ["YOU MUST SPECIFY A DIRECTION TO GO.", ("ctrl", 0x1c), "I CANNOT HELP YOU WITH THAT.", ("ctrl", 0x1c), "YOU CANNOT DO THAT.", ("ctrl", 0x1c)],
    468: ["THERE IS NO SUCH THING HERE!", ("ctrl", 0x1c), "NOTHING LIKE THAT IS HERE."],
    # ---- long flavor texts ----
    674: [0x0a, "\n\n", 0x0a, " WAS BUILT IN THE YEAR 783 GUE TO HOLD BACK THE RAGING ", 0x09,
          ". THIS GREAT WORK WAS COMMISSIONED BY THE RENOWNED LORD DIMWIT FLATHEAD AT A COST OF 37 MILLION ", 0xe6,
          ". THE REMARKABLE STRUCTURE USED 100,000 CUBIC METERS OF CONCRETE; IT STANDS 77 METERS HIGH AT THE CENTER AND 58 METERS WIDE AT THE TOP. THE LAKE IT CREATES HOLDS 460 MILLION CUBIC METERS, COVERS 10,000 SQUARE METERS, AND IS 12,000 METERS AROUND.\n\n", 0x0a,
          " TOOK 112 DAYS FROM GROUNDBREAKING TO COMPLETION. THE LABOR FORCE NUMBERED 384 SLAVES, 34 SLAVE-DRIVERS, 12 ENGINEERS, 2 PIGEONS, AND 1 PARTRIDGE IN A PEACH ", 0x23,
          ". THE MANAGEMENT TEAM RAN TO 2,345 OFFICIALS, 2,347 SECRETARIES (2 OF WHOM COULD ACTUALLY TYPE), 12,256 PAPER-SHUFFLERS, 52,469 STAMP-LICKERS, AND 245,193 RED-TAPE THREADERS, AND IT CONSUMED A MILLION ", 0x23,
          "S FELLED NEARBY.\n\nFOR YOUR TOUR, LET US NOTE A FEW OF THE MORE INTERESTING FEATURES OF ", 0x0a,
          ":\n\n1) YOU START AT THIS POINT IN THE ", 0xc0, ". YOU WILL SOON NOTICE THAT TO YOUR RIGHT..."],
    680: "\n(CLOSE THE COVER BEFORE STRIKING A LIGHT!)\n\nYOU TOO CAN MAKE A FORTUNE WITH PAPER SHUFFLING!\n\nMR. ANDERSON OF TOWN B, CITY A, SAYS: BEFORE I TRIED THIS, I AGONIZED OVER EVERY DECISION. BUT NOW THAT I HAVE LEARNED THE GREAT UNDERGROUND EMPIRE METHOD, I CAN SEE WHAT TRULY MATTERS AND FIND THE BEST PATH OUT OF ANY MUDDLE.\n\nDOCTOR BLANK ALSO SAYS: JUST TEN DAYS AGO I LOOKED FORWARD ONLY TO THE DEAD-END WORK OF BEING A DOCTOR. NOW MY FUTURE IS ASSURED, AND I MIGHT EVEN GRASP AN ENORMOUS FORTUNE IN ZORKMIDS.\n\nTHE METHOD CANNOT PROMISE DREAM RESULTS FOR EVERYONE. BUT WHEN YOU GAIN SOMETHING FROM IT, YOUR FUTURE IS SURE TO SHINE MORE BRIGHTLY!",
    # ---- room-content "objects present here" sub-windows (the engine's look-description path) ----
    # Each entry = a chain of ('r',flag) conditional-present markers (0x1e xx, preserved exactly)
    # then the English clause for the last flag's object. Mirrors the already-translated leaf
    # descriptions (538/601/703/562/592/695) which the engine also calls standalone. The JP grammar
    # abbreviation bytes (<c05><c06><c11><c18>...) are folded into the English sentence.
    # NOTE: conditional-walk semantics inferred from data, not from RE -- verify on emulator.
    # brown sack on the table (== leaf 538):
    417: [("r", 0x36), ("r", 0x38), ("r", 0x39), ("r", 0x3a), "ON THE ", 0x39, " IS A LONG BROWN SACK. IT SMELLS OF HOT PEPPERS."],
    507: [("r", 0x39), ("r", 0x3a), "ON THE ", 0x39, " IS A LONG BROWN SACK. IT SMELLS OF HOT PEPPERS."],
    516: [("r", 0x38), ("r", 0x39), ("r", 0x3a), "ON THE ", 0x39, " IS A LONG BROWN SACK. IT SMELLS OF HOT PEPPERS."],
    537: [("r", 0x3a), "ON THE ", 0x39, " IS A LONG BROWN SACK. IT SMELLS OF HOT PEPPERS."],
    # poseidons trident on the shore (== leaf 601):
    513: [("r", 0x44), ("r", 0x45), "ON THE ", 0xc4, " LIES POSEIDONS OWN ", 0x45, "."],
    600: [("r", 0x45), "ON THE ", 0xc4, " LIES POSEIDONS OWN ", 0x45, "."],
    # red buoy here (== leaf 703):
    411: [("r", 0x6c), ("r", 0x6e), ("r", 0x6f), "THERE IS A ", 0x6f, " HERE (PROBABLY A WARNING)."],
    414: [("r", 0x6e), ("r", 0x6f), "THERE IS A ", 0x6f, " HERE (PROBABLY A WARNING)."],
    645: [("r", 0x6b), ("r", 0x6c), ("r", 0x6e), ("r", 0x6f), "THERE IS A ", 0x6f, " HERE (PROBABLY A WARNING)."],
    702: [("r", 0x6f), "THERE IS A ", 0x6f, " HERE (PROBABLY A WARNING)."],
    # gallery painting "chance of a lifetime" (first sentence of leaf 562):
    561: [("r", 0x61), "THIS COULD BE THE CHANCE OF A LIFETIME."],
    594: [("r", 0x60), ("r", 0x61), "THIS COULD BE THE CHANCE OF A LIFETIME."],
    597: [("r", 0x60), ("r", 0x61), "THIS COULD BE THE CHANCE OF A LIFETIME."],
    # troll room: troll blocks exits / old trunk of jewels (== leaf 592) / gunk tube (== leaf 695):
    564: [("r", 0x86), "A FILTHY ", 0x86, " BRANDISHING A ", 0x2e, " BLOCKS ALL THE EXITS FROM THE ROOM.",
          ("r", 0x85), "HALF BURIED IN THE MUD IS AN ", 0x85, " FILLED WITH ", 0x1a, ". THE ", 0x85, " BULGES WITH SORTED ", 0x1a, ".",
          ("r", 0x87), "IT LOOKS LIKE A ", 0x87, " OF ", 0x1d, " PASTE.",
          ("r", 0x06), "FROBOZZ MAGIC GUNK COMPANY\nALL-PURPOSE GUNK"],
    591: [("r", 0x85), "HALF BURIED IN THE MUD IS AN ", 0x85, " FILLED WITH ", 0x1a, ". THE ", 0x85, " BULGES WITH SORTED ", 0x1a, ".",
          ("r", 0x87), "IT LOOKS LIKE A ", 0x87, " OF ", 0x1d, " PASTE.",
          ("r", 0x06), "FROBOZZ MAGIC GUNK COMPANY\nALL-PURPOSE GUNK"],
    # egyptian room: sceptre (== leaf 649) then brown sack on table (== leaf 538):
    450: [("r", 0x33), ("r", 0x34),
          "A POINTED ", 0x34, " DECORATED WITH ", 0x1a, ". YOU MAY NOT NEED IT, BUT IF YOU WAVE IT, IT MIGHT EVEN PART THE SEA.",
          "AN OLD ", 0x34, " THAT MAY ONCE HAVE BEEN EGYPTIAN LIES IN THE COFFIN, SET WITH BRIGHT RAINBOW ", 0x1a, " AND TAPERING TO A POINT. YOU MAY NOT NEED IT, BUT IF YOU WAVE IT, IT MIGHT EVEN PART THE SEA.",
          ("r", 0x35), ("r", 0x36), ("r", 0x38), ("r", 0x39), ("r", 0x3a),
          "ON THE ", 0x39, " IS A LONG BROWN SACK. IT SMELLS OF HOT PEPPERS."],
    # egyptian treasures: bottle on table / gold coffin / large diamond / jade statue:
    669: [("r", 0x47), ("r", 0x48), ("r", 0x49), ("r", 0x4a), ("r", 0x4b), ("r", 0x4c), ("r", 0x4d),
          "A BOTTLE IS ON THE ", 0x39, ".",
          ("r", 0xe8), ("r", 0x4f), "HERE IS THE ", 0x4f, " USED FOR THE BURIAL OF RAMESES II.",
          ("r", 0x06), ("r", 0x50), ("r", 0x51), ("r", 0x52), "A ", 0x15, " (AND VERY BEAUTIFUL) ", 0x14, ".",
          ("r", 0x06), ("r", 0x53), "THERE IS A SMALL STATUE HERE, A BEAUTIFUL FIGURE CARVED FROM JADE."],
    # altar area: candles (== leaf 631) / leaves / plastic / platinum bar / pot of gold (712) / prayer (644) / buoy (703):
    630: [("r", 0x62), "AT EACH END OF THE ", 0x31, " A ", 0x62, " BURNS.",
          ("r", 0x63), ("r", 0x64), ("r", 0x65), "ON THE ", 0xe1, " IS A ", 0x65, ".",
          ("r", 0x66), ("r", 0x67), "HERE IS A FOLDED PILE OF PLASTIC WITH A SMALL VALVE ATTACHED.",
          ("r", 0x68), "ON THE ", 0xe1, " IS A LARGE ", 0x68, ".",
          ("r", 0x69), "AT THE END OF THE RAINBOW IS A POT OF GOLD.",
          ("r", 0x6a), "THE ", 0x6a, " IS WRITTEN IN AN ARCHAIC SCRIPT RARELY USED TODAY. IT REBUKES THE ABSENT-MINDED, THE WORM-LIKE, AND THE FICKLE. ONE WHO RECITES THE ", 0x6a, " TO ITS LAST WORD IS CARRIED OFF TO THE LAND OF THE DEAD.",
          ("r", 0x6b), ("r", 0x6c), ("r", 0x6e), ("r", 0x6f), "THERE IS A ", 0x6f, " HERE (PROBABLY A WARNING)."],
    # land of the dead: crystal skull rolling on the ground:
    501: [("r", 0x24), ("r", 0x25), ("r", 0x26), ("r", 0x27), ("r", 0x28), ("r", 0x29),
          "ROLLING HERE IS A MAGNIFICENT ", 0x29, ". IT SEEMS TO SMILE AT YOU."],
    # treasure: engraved silver chalice / poseidons trident on the shore (== leaf 601):
    690: [("r", 0x3b), ("r", 0x3c), ("r", 0x3e), ("r", 0x3f), ("r", 0x40), ("r", 0x41), ("r", 0x42), ("r", 0x43),
          "THE ", 0x43, " IS DELICATELY ENGRAVED.",
          ("r", 0x44), ("r", 0x45), "ON THE ", 0xc4, " LIES POSEIDONS OWN ", 0x45, "."],
    # living room: object contents (bank 1) + leaflet/paper reading text (bank 2, after the 0x1c):
    519: [("r", 0x54), "A ", 0x54, " IS ON THE ", 0x39, ".",
          ("r", 0x55), ("r", 0x56), "HERE IS A USELESS, BURNED-OUT ", 0x11, " LEFT BY SOME DEAD ADVENTURER.",
          ("r", 0x57), "AN OLD ", 0x57, " BULGING WITH COINS IS HERE.",
          ("r", 0x58), "A BATTERY-POWERED ", 0x11, " IS ON THE ", 0x41, ".",
          ("r", 0x06), ("r", 0x59), ("r", 0x5a),
          "A ", 0x5a, " HAS FALLEN ON THE ", 0xe1, ". WELCOME TO THE WORLD OF ", 0xe9,
          "!\n\nAND SO BEGINS AN INCREDIBLE TREASURE HUNT. HOW MANY TREASURES CAN YOU FIT IN THE TROPHY CASE? IT ALL DEPENDS ON YOUR WITS.",
          ("ctrl", 0x1c),
          ("r", 0x5b), ("r", 0x5c), ("r", 0x5d), ("r", 0x5e), ("r", 0x5f),
          "HERE IS A ", 0x5f, ". ON THE FOLDED COVER IS WRITTEN: VISIT BEAUTIFUL ", 0x0a, ".",
          "\n(CLOSE THE COVER BEFORE STRIKING A LIGHT!)\n\nYOU TOO CAN MAKE A FORTUNE WITH PAPER SHUFFLING!\n\nMR. ANDERSON OF TOWN B, CITY A, SAYS: BEFORE I TRIED THIS, I AGONIZED OVER EVERY DECISION. BUT NOW THAT I HAVE LEARNED THE GREAT UNDERGROUND EMPIRE METHOD, I CAN SEE WHAT TRULY MATTERS AND FIND THE BEST PATH OUT OF ANY MUDDLE.\n\nDOCTOR BLANK ALSO SAYS: JUST TEN DAYS AGO I LOOKED FORWARD ONLY TO THE DEAD-END WORK OF BEING A DOCTOR. NOW MY FUTURE IS ASSURED, AND I MIGHT EVEN GRASP AN ENORMOUS FORTUNE IN ZORKMIDS."],
    # maze/treasure room: rope / rusty knife / sealed engraving / elvish sword / map / boat label:
    531: [("r", 0x70), "IN A CORNER IS A LARGE COIL OF ", 0x70, ".",
          ("r", 0x71), "BESIDE THE ", 0x55, " IS A ", 0x71, ".",
          ("r", 0x72), ("r", 0x73), ("r", 0x74), ("r", 0x75), ("r", 0x76), ("r", 0x77),
          ("r", 0x78), ("r", 0x79), ("r", 0x7a), ("r", 0x7b), ("r", 0x7c), ("r", 0x7d),
          "THE ENGRAVING READS: THIS PLACE HAS BEEN SEALED FOR A CERTAIN REASON.",
          ("r", 0x7e), "ABOVE THE ", 0x41, " HANGS A VERY OLD ", 0x7e, ".",
          ("r", 0x7f), "INSIDE THE ", 0x41, " IS A PARCHMENT THAT APPEARS TO BE A MAP. THE MAP SHOWS A ", 0x22, " WITH THREE ", 0x9c, "S. THE LARGEST HAS A HOUSE; THREE ROADS LEAD FROM IT, AND ONE RUNNING SOUTHWEST IS LABELED: TO THE ", 0x4c, ".",
          ("r", 0x80), "- FROBOZZ MAGIC ", 0x17, " COMPANY -\n\nHELLO, ", 0xe0, "!\n\nINSTRUCTIONS:\nTO SET OUT ON THE WATER, SHOUT LAUNCH.\nTO COME ASHORE, SHOUT LAND, OR SHOUT THE DIRECTION OF THE ", 0xc4, " YOU WANT.\n\nWARRANTY:\nTHIS ", 0x17, " IS GUARANTEED AGAINST ALL DEFECTS FOR 76 MILLISECONDS FROM PURCHASE, OR UNTIL FIRST USE, WHICHEVER COMES FIRST.\n\nWARNING:\nTHIS ", 0x17, " IS MADE OF THIN PLASTIC.\n\nGOOD LUCK!"],
    # altar / temple: red-hot bell on the ground / black book open to the commandment (== leaf 628):
    723: [("r", 0x2c), ("r", 0x2d), "RED-HOT.",
          ("r", 0x2d), "A RED-HOT ", 0x2d, " LIES ON THE ", 0xe1, ".",
          ("r", 0x2e), ("r", 0x2f), ("r", 0x30), ("r", 0x31), ("r", 0x32),
          "ON THE ", 0x31, " LIES A LARGE ", 0x32, ", OPEN TO PAGE 569.\n\nCOMMANDMENT #12592\n\nOH YE WHO GO ABOUT SAYING TO EACH: HELLO SAILOR:\nDOST THOU KNOW THE MAGNITUDE OF THY SIN BEFORE THE GODS?\nYEA, VERILY, THOU SHALT BE GROUND BETWEEN TWO STONES.\nSHALL THE ANGRY GODS CAST THY BODY INTO THE WHIRLPOOL?\nSURELY THY EYE SHALL BE PUT OUT WITH A SHARP STICK!\nEVEN UNTO THE ENDS OF THE EARTH SHALT THOU WANDER, AND\nUNTO THE LAND OF THE DEAD SHALT THOU BE SENT AT LAST.\nSURELY THOU SHALT REPENT OF THY CUNNING."],
    # engravings cave: carved wall (== leaf 638) / paper manual + congratulations (== leaves 559,560):
    681: [("r", 0x88), ("r", 0x89),
          "HERE IS A WALL WITH OLD CARVINGS. ON THE NATURAL STONE OF THE CAVE WALL AN UNKNOWN HAND HAS CARVED RELIEFS DEPICTING THE FAITH OF THE ANCIENT ", 0xe9, " PEOPLE. THEY SEEM TO COPY THE GREAT RELIGIOUS RITES OF THE TIME, BUT A LATER AGE, FINDING THEM BLASPHEMOUS, COVERED THEM WITH EQUALLY FINE CARVINGS.",
          ("r", 0x8a), "TACKED LOOSELY TO THE WALL IS A PAPER ", 0x8a, ". CONGRATULATIONS!\n\nYOU ARE THE PRIVILEGED OWNER OF ", 0xe9, " I, A GREAT UNDERGROUND EMPIRE THAT COMES COMPLETE WITH EVERYTHING NEEDED TO MAINTAIN IT. UNDER NORMAL USE, ", 0xe9, " I WILL RUN FOR MANY MONTHS WITHOUT BREAKING DOWN."],
    # up-a-tree / nest+egg+canary (== leaves 505,490,487) / barrow / house exteriors / forest:
    387: [("r", 0x8b), ("r", 0x8c), ("r", 0x8d), ("r", 0x8e), ("r", 0x8f),
          "ON A BRANCH BESIDE YOU IS A SMALL ", 0x8f, ".",
          ("r", 0x90), "IN THE ", 0x8f, " IS A LARGE EGG ENCRUSTED WITH PRECIOUS ", 0x1a, ". A CHILDLESS ", 0x20, " MUST HAVE BROUGHT IT HERE. THE EGG IS WORKED IN GOLD AND SET WITH LAPIS AND PEARL, AND HINGED WITH A DELICATE CLASP. IT LOOKS VERY FRAGILE.",
          ("r", 0x91), "HERE IS A BROKEN EGG.",
          ("r", 0x92), ("r", 0x93), "INSIDE THE EGG IS A ", 0x93, ". ITS EYES ARE RUBIES AND ITS BEAK IS SILVER. THROUGH A CRYSTAL WINDOW UNDER THE LEFT WING YOU CAN SEE INTRICATE MACHINERY, THOUGH IT IS NOT MOVING.",
          ("r", 0x94), "INSIDE THE EGG IS A ", 0x93, ". IT SEEMS TO HAVE BEEN BADLY MISTREATED RECENTLY: THE EYES SET TO HOLD ", 0x1a, " ARE EMPTY, AND THE SILVER BEAK IS CRUSHED. THROUGH THE CRYSTAL WINDOW UNDER THE LEFT WING YOU SEE THE WRECKAGE OF INTRICATE MACHINERY. IT IS A MYSTERY HOW ITS MAINSPRING CAME TO BE SPRUNG.",
          ("r", 0x96), "THE ", 0x4a, " IS BOARDED UP AND THE ", 0x1c, "S CANNOT BE REMOVED. YOU STAND BEFORE AN IMPOSING ", 0x4c, ". ON ITS WEST FACE IS A LARGE ", 0x4b, ". IT IS PITCH DARK INSIDE, SO YOU CANNOT TELL WHAT IS THERE.",
          ("r", 0x4c), "YOU ARE ON THE NORTH SIDE OF THE ", 0x21, ". THERE IS NO ", 0x4a, " ON THIS SIDE, AND ALL THE WALL WINDOWS ARE BOARDED UP. A NARROW PATH WINDS NORTH AMONG THE ", 0x23, "S.",
          ("r", 0x97), "ALL THE WINDOWS ARE BOARDED UP. YOU ARE ON THE SOUTH SIDE OF THE ", 0x21, ". THERE IS NO ", 0x4a, " ON THIS SIDE, AND ALL THE WALL WINDOWS ARE BOARDED UP.",
          ("r", 0x98), ("r", 0xff), ("r", 0x99), "THIS IS A ", 0x22, " WITH ", 0x23, "S GROWING DENSELY. FAINT SUNLIGHT ENTERS FROM THE EAST. THERE ARE NO ", 0x23, "S HERE THAT YOU CAN CLIMB. TO GO FARTHER WEST YOU WILL NEED A MACHETE. A DIM ", 0x22, " SURROUNDED BY HUGE ", 0x23, "S.",
          ("r", 0xff)],
    # dam lobby: thief / ivory torch on pedestal (== leaf 652) / guidebook + full dam history (== leaves 673,674):
    612: [("r", 0x81), "A SUSPICIOUS-LOOKING INDIVIDUAL LEANS AGAINST THE WALL. HE CARRIES A ", 0x7a, " AND WEARS A ", 0x7b, " THAT COULD KILL A MAN WITH A SINGLE THRUST.",
          ("r", 0x82), ("r", 0x83), "ON THE ", 0x82, " BURNS AN IVORY ", 0x83, ".",
          ("r", 0x84), "THE ", 0x84, "S ON THE FRONT DESK ARE TITLED: ", 0x0a, ".\n\n", 0x0a,
          " WAS BUILT IN THE YEAR 783 GUE TO HOLD BACK THE RAGING ", 0x09,
          ". THIS GREAT WORK WAS COMMISSIONED BY THE RENOWNED LORD DIMWIT FLATHEAD AT A COST OF 37 MILLION ", 0xe6,
          ". THE REMARKABLE STRUCTURE USED 100,000 CUBIC METERS OF CONCRETE; IT STANDS 77 METERS HIGH AT THE CENTER AND 58 METERS WIDE AT THE TOP. THE LAKE IT CREATES HOLDS 460 MILLION CUBIC METERS, COVERS 10,000 SQUARE METERS, AND IS 12,000 METERS AROUND.\n\n", 0x0a,
          " TOOK 112 DAYS FROM GROUNDBREAKING TO COMPLETION. THE LABOR FORCE NUMBERED 384 SLAVES, 34 SLAVE-DRIVERS, 12 ENGINEERS, 2 PIGEONS, AND 1 PARTRIDGE IN A PEACH ", 0x23,
          ". THE MANAGEMENT TEAM RAN TO 2,345 OFFICIALS, 2,347 SECRETARIES (2 OF WHOM COULD ACTUALLY TYPE), 12,256 PAPER-SHUFFLERS, 52,469 STAMP-LICKERS, AND 245,193 RED-TAPE THREADERS, AND IT CONSUMED A MILLION ", 0x23,
          "S FELLED NEARBY.\n\nFOR YOUR TOUR, LET US NOTE A FEW OF THE MORE INTERESTING FEATURES OF ", 0x0a,
          ":\n\n1) YOU START AT THIS POINT IN THE ", 0xc0, ". YOU WILL SOON NOTICE THAT TO YOUR RIGHT..."],
    # "no such thing here" (== leaf 468), with leading conditional flags + the 0x1c bank separator:
    345: [("r", 0xda), ("r", 0xdb), ("r", 0xdc), ("r", 0xdd), "THERE IS NO SUCH THING HERE!", ("ctrl", 0x1c), "NOTHING LIKE THAT IS HERE."],
    348: [("r", 0xdc), ("r", 0xdd), "THERE IS NO SUCH THING HERE!", ("ctrl", 0x1c), "NOTHING LIKE THAT IS HERE."],
    408: [("r", 0xdb), ("r", 0xdc), ("r", 0xdd), "THERE IS NO SUCH THING HERE!", ("ctrl", 0x1c), "NOTHING LIKE THAT IS HERE."],
    363: [("r", 0xe0), "THEN SPEAK WITH THE ", 0xe0, "."],
    # hot-pepper sandwich (lunch):
    525: [("r", 0x2b), "A PEPPER-STUFFED ", 0x2b, ".", ("r", 0x06), "RED PEPPER"],
    # open trap door at your feet:
    750: ["AN OPEN ", 0x48, " IS AT YOUR FEET.", ("r", 0xff)],
    # thief departs carrying the bag (751 continues into 752/753):
    751: ["STILL CARRYING THE ", 0x7a, ", THE ", 0x81, " WALKS AWAY, AND ONLY LATER DO YOU REALIZE"],
    752: "THAT HE HAS STOLEN SOMETHING.",
    753: "THAT HE HAS CARRIED OFF SOMETHING VALUABLE FROM THE ROOM.",
    # thief gives up and leaves (two 0x1c banks):
    754: ["FINDING NO TREASURE, THE ", 0x81, " LEAVES IN DISGUST.", ("ctrl", 0x1c),
          "A GAUNT, STARVING GENTLEMAN WALKS ABOUT CARRYING A ", 0x7a, ". FINDING NO TREASURE, HE LEAVES IN A FOUL MOOD.", ("ctrl", 0x1c),
          ("r", 0xff), ("r", 0xff), ("r", 0xff)],
    # sceptre loses its glow (one 0x1c bank):
    755: ["THE ", 0x34, " STOPS GLOWING AND LOSES ITS REMARKABLE POWER.", ("ctrl", 0x1c), "SOMETHING MAY HAPPEN, BUT"],
    756: "THERE IS NOTHING TO FILL IT WITH.",
    # mountains / forest path with the great tree:
    764: ["THE ", 0x24, " IS TOO STEEP TO CLIMB.", ("r", 0xff), "A MASS OF BRUSH BLOCKS THE WAY EAST.",
          ("r", 0xff), "A PATH LEADS INTO A DIM ", 0x22, ". IT RUNS NORTH AND SOUTH. AT THE EDGE OF THE PATH STANDS AN UNUSUALLY LARGE ", 0x23, " SPREADING ITS LOW BRANCHES.",
          ("r", 0x9a), ("r", 0x9b), "YOU CANNOT CLIMB ANY HIGHER.", ("r", 0x9c)],
    # land of the dead / hades / temple room descriptions:
    771: ["AN UNSEEN FORCE PREVENTS YOU FROM ENTERING THE GATE.",
          ("r", 0xff), "YOU COME TO THE ", 0xbb, ". YOU HEAR THE WAILS AND MOANS OF THOUSANDS OF SOULS. IN ONE CORNER ARE PILED THE REMAINS OF ADVENTURERS WHO CAME BEFORE. A ", 0xe5, " LEADS NORTH.",
          ("r", 0xbb), "YOU ENTER A LOW ROOM WITH PATHS LEADING NORTHWEST AND EAST.",
          ("r", 0xbc), "A ROOM LIKE AN EGYPTIAN TOMB. TO THE WEST A ", 0xdf, " LEADS UP.",
          ("r", 0xbd), ("r", 0xbe), "YOU CANNOT GO DOWN WITHOUT BREAKING A BONE.",
          ("r", 0xbf), "YOU CANNOT REACH THE ", 0x70, ". YOU ARE AT THE NORTH END OF A LARGE TEMPLE. ON THE EAST WALL IS AN ANCIENT INSCRIPTION, PROBABLY A ", 0x6a, " IN A LANGUAGE LONG UNUSED. BELOW THE ", 0x6a, " A ", 0xdf, " LEADS DOWN. THE WEST WALL IS SOLID ", 0x02, ". THE NORTH EXIT IS FRAMED BY LARGE MARBLE PILLARS."],
    # white cliffs / shore / frigid river (276 is a sub-window of this):
    774: ["THE PATH IS VERY NARROW.",
          ("r", 0xff), "A NARROW, ROCKY STRIP OF SHORE BESIDE THE ", 0x8b, ". A THIN PATH RUNS NORTH ALONG THE ", 0xc4, ".",
          ("r", 0xff), ("r", 0xff), "HERE THE ", 0x6e, " FLOWS SWIFT AND VERY VIOLENT. THE EAST BANK IS A ", 0xc5, ". BELOW THE ", 0x8b, " ON THE WEST IS A SMALL SHOAL.",
          ("r", 0xff), "YOU CAN REACH THE ", 0xc4, " ON EITHER SIDE."],
    276: [("r", 0xff), ("r", 0xff), "HERE THE ", 0x6e, " FLOWS SWIFT AND VERY VIOLENT. THE EAST BANK IS A ", 0xc5, ". BELOW THE ", 0x8b, " ON THE WEST IS A SMALL SHOAL.",
          ("r", 0xff), "YOU CAN REACH THE ", 0xc4, " ON EITHER SIDE."],
}
