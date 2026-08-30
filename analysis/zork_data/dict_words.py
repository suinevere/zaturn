"""dict234 index -> English. Dict words PROPAGATE everywhere they're referenced (via
0x0e/0x1e tokens, and room titles/object names look them up), so this is the highest-leverage
lane. 234 entries total; keys are the dict index (hex). Decode JP via analysis/zork_msg_decode.py
(dict_word). Encoding/relocation handled by the builder (enc_fw full-width UPPERCASE).

Entries left UNtranslated on purpose: pure grammatical fragments used to compose messages at
runtime (e.g. 0x01 何も起こり, 0x05 が現れ, 0x06 がここに) and a handful of abbreviation-codec
garbled words whose JP can't be read cleanly. Object NAMES + ROOM TITLES are translated here so
every {room}/[obj] token renders English. Names match Infocom Zork I where known."""

DICT = {
    # ---- objects / items ----
    0x02: "GRANITE WALL",      # みかげ石
    0x09: "FRIGID RIVER",      # フリジッド河
    0x0a: "FLOOD CONTROL DAM", # 第３水量調整
    0x0b: "BRASS",             # 黄銅の (brass lantern)
    0x11: "LAMP",              # ランプ
    0x12: "BROKEN",            # 壊れた
    0x13: "BUTTON",            # ボタン
    0x14: "DIAMOND",           # ダイヤモンド
    0x15: "LARGE",             # 大きな
    0x16: "KNIFE",             # ナイフ
    0x17: "BOAT",              # ボート
    0x18: "MATCH",             # マッチ
    0x1a: "JEWELS",            # 宝石
    0x1b: "CARVING",           # 彫刻
    0x1c: "BOARD",             # 板
    0x1d: "TEETH",             # 歯
    0x1e: "WALLS",             # まわりの壁
    0x1f: "WALL",              # の壁
    0x20: "SONGBIRD",          # 青い鳥
    0x21: "WHITE HOUSE",       # 白い家
    0x22: "FOREST",            # 森
    0x23: "TREE",              # 木
    0x24: "MOUNTAIN",          # 山
    0x25: "WATER",             # 水
    0x26: "WINDOW",            # 窓
    0x27: "CHIMNEY",           # 煙突
    0x28: "GHOST",             # 幽霊
    0x29: "CRYSTAL SKULL",     # 頭蓋骨
    0x2a: "BASKET",            # カゴ
    0x2b: "SANDWICH",          # サンドイッチ
    0x2c: "BAT",               # コウモリ
    0x2d: "BELL",              # ベル
    0x2e: "BLOODY AXE",        # 血まみれの斧
    0x2f: "BOLT",              # ボルト
    0x30: "GREEN BUBBLE",      # 緑色のパイロット
    0x31: "ALTAR",             # 祭壇
    0x32: "BLACK BOOK",        # 黒い本
    0x34: "SCEPTRE",           # 杖
    0x35: "TIMBERS",           # ぼろぼろの木材
    0x36: "SLIDE",             # すべり台
    0x39: "TABLE",             # テーブル
    0x3a: "BROWN SACK",        # 茶色の袋
    0x3b: "TOOL CHEST",        # 道具箱
    0x3c: "YELLOW",            # 黄
    0x3e: "BROWN",             # 茶=
    0x3f: "RED",               # 赤=
    0x40: "BLUE",              # 青=
    0x41: "TROPHY CASE",       # トロフィーケース
    0x42: "RUG",               # じゅうたん
    0x43: "SILVER CHALICE",    # 銀の杯
    0x44: "GARLIC",            # ニンニク
    0x45: "CRYSTAL TRIDENT",   # Fトライデント
    0x46: "CRYSTAL",           # 水晶の
    0x47: "DAM",               # ダム
    0x48: "TRAP DOOR",         # 隠し扉
    0x49: "BOARDED WINDOW",    # 板を打ちつけた窓
    0x4a: "DOOR",              # ドア
    0x4b: "STONE BARRIER",     # 石のJ
    0x4c: "STONE BARROW",      # 石塚
    0x4d: "GLASS BOTTLE",      # ガラスのボトル
    0x4e: "CHASM",             # 裂け目
    0x4f: "GOLD COFFIN",       # 黄金の棺
    0x50: "GRATING",           # 鉄格子
    0x51: "AIR PUMP",          # 空気ポンプ
    0x53: "JADE FIGURINE",     # ヒスイの像
    0x54: "NASTY KNIFE",       # 不気味なナイフ (living-room table knife)
    0x55: "SKELETON",          # 骸骨
    0x57: "LEATHER BAG",       # 革の財布
    0x59: "EMERALD",           # エメラルド
    0x5a: "LEAFLET",           # 手紙 (the mailbox leaflet)
    0x5b: "LEAK",              # 水漏れ
    0x5c: "MACHINE",           # マシン
    0x5d: "MAGIC",             # 魔法の
    0x5e: "SMALL MAILBOX",     # 小さな郵便箱
    0x5f: "PAPER",             # 紙
    0x60: "MIRROR",            # 鏡
    0x61: "PAINTING",          # 絵
    0x62: "CANDLES",           # ろうそく
    0x63: "BROKEN GLASS",      # ガラスくず
    0x64: "BODY",              # 死体
    0x65: "PILE OF LEAVES",    # 落葉
    0x67: "PILE OF PLASTIC",   # 折りたたんだビニール
    0x68: "PLATINUM BAR",      # プラチナの棒
    0x69: "POT OF GOLD",       # 金の壷
    0x6a: "PRAYER",            # 祈りの言葉
    0x6b: "RAILING",           # 手すり
    0x6c: "RAINBOW",           # 虹
    0x6e: "RIVER",             # 河
    0x6f: "RED BUOY",          # 赤いブイ
    0x70: "ROPE",              # ロープ
    0x71: "RUSTY KNIFE",       # 錆びた
    0x72: "SAND",              # 砂
    0x73: "SAPPHIRE BRACELET", # サファイアのブレスレット
    0x74: "SCREWDRIVER",       # ドライバー
    0x75: "SKELETON KEY",      # ドクロの鍵
    0x76: "SHOVEL",            # シャベル
    0x77: "COAL",              # 石炭
    0x78: "LADDER",            # はしご
    0x79: "SCARAB",            # スカラベ
    0x7a: "BAG",               # 袋
    0x7b: "STILETTO",          # 短剣
    0x7c: "SWITCH",            # スイッチ
    0x7e: "ELVISH SWORD",      # 妖精の剣
    0x7f: "ANCIENT MAP",       # 古代の地図
    0x80: "WEATHERED LABEL",   # 日焼けしたラベル
    0x81: "THIEF",             # 泥棒
    0x82: "PEDESTAL",          # 台座
    0x83: "TORCH",             # たいまつ
    0x84: "GUIDEBOOK",         # ガイドブック
    0x85: "OLD TRUNK",         # 古いトランク
    0x86: "TROLL",             # トロル
    0x87: "TUBE",              # チューブ
    0x88: "GLUE",              # 接着剤
    0x8a: "MANUAL",            # 説明書
    0x8b: "CLIFF",             # 絶壁
    0x8c: "WHITE CLIFFS",      # 白い絶壁
    0x8d: "WRENCH",            # レンチ
    0x8e: "CONTROL PANEL",     # 制御パネル
    0x8f: "NEST",              # 鳥の巣
    0x90: "JEWELED EGG",       # をちりばめた卵
    0x91: "BROKEN EGG",        # 壊れたの卵
    0x92: "TOY",               # おもちゃ
    0x93: "CLOCKWORK CANARY",  # ぜんまい仕掛のカナリヤ
    0x94: "BROKEN CANARY",     # 壊れたぜんまい仕掛のカナリヤ
    # ---- room titles ----
    0x96: "WEST OF HOUSE",     # 家の西
    0x97: "BEHIND HOUSE",      # 家の東
    0x98: "NORTH OF HOUSE",    # 家の北
    0x99: "SOUTH OF HOUSE",    # 家の南
    0x9a: "FOREST PATH",       # の小道
    0x9b: "UP A TREE",         # 木の上
    0x9c: "CLEARING",          # 開拓地
    0x9d: "KITCHEN",           # 台所
    0x9e: "ATTIC",             # 屋根裏部屋
    0x9f: "LIVING ROOM",       # リビングルーム
    0xa0: "CELLAR",            # 地下室
    0xa1: "TROLL ROOM",        # 部屋 (after cellar)
    0xa2: "EAST OF CHASM",     # 裂け目の東
    0xa3: "STUDIO",            # アトリエ
    0xa4: "MAZE",              # 迷路
    0xa5: "DEAD END",          # 行き止まり
    0xa6: "GRATING ROOM",      # 部屋 (maze grating)
    0xa7: "CYCLOPS ROOM",      # の部屋
    0xa8: "STRANGE PASSAGE",   # 奇妙な通路
    0xa9: "TREASURE ROOM",     # 宝の部屋
    0xaa: "RESERVOIR SOUTH",   # 南
    0xab: "RESERVOIR",         # 貯水池
    0xac: "RESERVOIR NORTH",   # 北
    0xad: "STREAM VIEW",       # 小川の見える所
    0xaf: "SHAFT ROOM",        # たて穴
    0xb0: "COLD PASSAGE",      # 寒い通路
    0xb1: "NARROW PASSAGE",    # 狭い通路
    0xb2: "WINDING PASSAGE",   # 湾曲した通路
    0xb3: "TWISTING PASSAGE",  # 曲がりくねった通路
    0xb4: "ATLANTIS ROOM",     # アトランティスの部屋
    0xb5: "EAST WEST PASSAGE", # 東西の通路
    0xb6: "DEEP CANYON",       # 深い峡谷
    0xb7: "DAMP CAVE",         # じめじめした部屋
    0xb8: "LOUD ROOM",         # うるさい部屋
    0xb9: "NORTH SOUTH PASSAGE", # 南北の道
    0xba: "ENTRANCE TO HADES", # 冥界の入口
    0xbb: "LAND OF THE DEAD",  # 死者の国
    0xbd: "EGYPTIAN ROOM",     # ファラオの部屋
    0xbe: "DOME ROOM",         # ドームの部屋
    0xbf: "TORCH ROOM",        # 部屋
    0xc0: "DAM LOBBY",         # ダムのロビー
    0xc1: "MAINTENANCE ROOM",  # 制御室
    0xc2: "DAM BASE",          # ダムの底
    0xc3: "WHITE CLIFFS BEACH",# 白い絶壁の河原
    0xc4: "SHORE",             # 岸
    0xc5: "SANDY BEACH",       # 砂浜
    0xc6: "SANDY CAVE",        # 砂のほら穴
    0xc7: "ARAGAIN FALLS",     # アラゲイン滝
    0xc8: "ON THE RAINBOW",    # 虹の上
    0xc9: "END OF RAINBOW",    # 虹の果て
    0xca: "CANYON BOTTOM",     # 峡谷の下
    0xcb: "ROCKY LEDGE",       # 岩だな
    0xcc: "CANYON VIEW",       # 峡谷の見える所
    0xcf: "BAT ROOM",          # コウモリの部屋
    0xd0: "SHAFT ROOM",        # たて穴の部屋
    0xd1: "SMELLY ROOM",       # 悪臭の部屋
    0xd2: "GAS ROOM",          # ガス室
    0xd3: "LADDER TOP",        # はしごの上
    0xd4: "LADDER BOTTOM",     # はしごの下
    0xd5: "TIMBER ROOM",       # 材木置き場
    0xd6: "DRAFTY ROOM",       # 風の吹く部屋
    0xd7: "MACHINE ROOM",      # マシン室
    0xd8: "COAL MINE",         # 炭坑
    0xd9: "SLIDE ROOM",        # すべり台の部屋
    # ---- misc nouns referenced as tokens ----
    0xdf: "STAIRCASE",         # 階段
    0xe0: "SAILOR",            # 船のり
    0xe1: "GROUND",            # 地面
    0xe2: "GRUE",              # グルー
    0xe5: "PASSAGE",           # 通路
    0xe6: "ZORKMIDS",          # ゾークン
    0xe7: "HANDS",             # 両手
    0xe8: "CRACK",             # 亀裂
    0xe9: "ZORK",              # ＺＯＲＫ (msg479 "WORLD OF ZORK")
    # ---- batch: clean object/room names (2026-06-30 coverage audit) ----
    0x19: "PAINT",             # 絵の具
    0x37: "KITCHEN",           # 台所の
    0x38: "KITCHEN TABLE",     # 台所のテーブル
    0x52: "LARGE DIAMOND",     # 大きなダイヤモンド
    0x6d: "END OF RAINBOW",    # 虹の終り
    0x89: "CARVED WALL",       # 彫刻の壁
    0x95: "HOUSE",             # 家の
    0xae: "MIRROR ROOM",       # 鏡の間
    0xbc: "ENGRAVINGS CAVE",   # 彫刻のある部屋
    0xcd: "MINE ENTRANCE",     # 炭坑の入口
    0xce: "LOUD ROOM",         # 音がする部屋
}
