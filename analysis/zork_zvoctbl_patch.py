#!/usr/bin/env python3
"""ZVOCTBL.DAT in-place keyword translator (Path B1: make English commands EXECUTE).

The Saturn Zork parser matches a normalized command word against ZVOCTBL keywords (hiragana
readings / kanji) and returns the matched record's id. English commands fail only because no
record carries an English keyword. We REPLACE a record's JP-reading keyword with the full-width
SJIS English equivalent, keeping the same id + POS flag — so e.g. "ＭＡＩＬＢＯＸ" resolves to the
exact id (0x025e) that "ゆうびんばこ" does today, and behaves identically through parser+executor.

Records are a fixed 23-byte stride [id u16-LE][POS][SJIS keyword <=20B NUL-pad], so English
(<=20B) keeps the file size identical -> patch in place. The loader re-buckets by class at boot
(proven: the stock ZVOCTBL already contains working scattered English entries like QUIT/LOAD/SAVE),
so a translated record stays reachable regardless of its file position.

POS: 0x80 noun, 0x20 adj, 0x02 verb, 0x40 verb-te, 0x10 direction.

Usage:  patched = patch_zvoctbl(open('work/zork1/ZVOCTBL.DAT','rb').read())
"""
import os, struct

REC0 = 0x3da
STRIDE = 23
KWMAX = STRIDE - 3            # 20 bytes available for the keyword

# --- full-width SJIS encoder (must byte-match the command-buffer / dict encoding; see
#     analysis/zork_translate.py enc_fw) ---
def enc_fw(s):
    out = bytearray()
    for c in s.upper():
        o = ord(c)
        if c == " ":            out += b"\x81\x40"
        elif "0" <= c <= "9":   out += bytes((0x82, 0x4f + o - 0x30))
        elif "A" <= c <= "Z":   out += bytes((0x82, 0x60 + o - 0x41))
        else:                   out += b"\x81\x40"
    return bytes(out)

# --- translation map: JP-reading keyword (as stored in ZVOCTBL) -> English ---
# Keys are the exact decoded shift_jis keyword of a noun(0x80)/adj(0x20) record. We translate to the
# SAME English the selection UI produces (analysis/zork_data nouns.py + dict_words.py), so the command
# word byte-matches. Synonyms converging on one English word is fine (the game accepts all readings ->
# whichever id matches, the object responds). A wrong/missing entry simply fails to match (harmless).
# Ambiguous bare single-kanji (は/手/口/...) are intentionally left Japanese to avoid mis-binding.
READING_EN = {
    # ---- objects / nouns ----
    "ゆうびんばこ": "MAILBOX", "郵便箱": "MAILBOX", "ゆうびんうけ": "MAILBOX",
    "ゆうびんぶつ": "MAIL", "郵便物": "MAIL", "ゆうびん": "MAIL", "郵便": "MAIL",
    "せつめいしょ": "LEAFLET", "説明書": "LEAFLET", "ぱんふれっと": "LEAFLET",
    "まにゅある": "MANUAL", "がいどぶっく": "GUIDEBOOK",
    "せっちゃくざい": "GLUE", "接着剤": "GLUE", "ぐるー": "GLUE",
    "いのりのことば": "PRAYER", "いのりの言葉": "PRAYER", "祈りの言葉": "PRAYER",
    "きとうぶん": "PRAYER", "きとう文": "PRAYER", "いのり": "PRAYER", "祈り": "PRAYER",
    "ぜんまい仕掛の": "CLOCKWORK", "ぜんまいじかけの": "CLOCKWORK", "ぜんまい仕掛けの": "CLOCKWORK",
    "ほうせきを散りばめた": "JEWELED", "ほうせきをちりばめた": "JEWELED", "宝石をちりばめた": "JEWELED", "ほうせきの": "JEWELED",
    "ちょうこくされた": "CARVING", "ちょうこく": "CARVING", "きざみ": "CARVING", "刻み": "CARVING", "刻": "CARVING",
    "びじゅつひん": "PAINTING", "美術品": "PAINTING", "絵": "PAINTING",
    "ぶれすれっと": "BRACELET", "さいくろぷす": "CYCLOPS", "ひとつめ": "CYCLOPS",
    "だいあもんど": "DIAMOND", "だいやもんど": "DIAMOND",
    "とらいでんと": "TRIDENT", "とらっぷどあ": "TRAP",
    "ぼーとのあな": "LEAK", "ぼーとの穴": "LEAK", "ボートの穴": "LEAK", "みず漏れ": "LEAK", "みずもれ": "LEAK", "水もれ": "LEAK", "もれ": "LEAK",
    "こわれている": "BROKEN", "壊れている": "BROKEN", "壊れた": "BROKEN", "こわれた": "BROKEN",
    "こがねむし": "SCARAB", "すからべ": "SCARAB",
    "かーぺっと": "RUG", "じゅうたん": "RUG",
    "がーりっく": "GARLIC", "にんにく": "GARLIC",
    "どらいばー": "SCREWDRIVER", "えめらるど": "EMERALD",
    "あくりょう": "GHOST", "ゆうれい": "GHOST", "幽霊": "GHOST",
    "てつごうし": "GRATING",
    "がらすくず": "GLASS", "がらす屑": "GLASS", "がらす": "GLASS",
    "かみまっち": "MATCH", "紙マッチ": "MATCH", "まっち": "MATCH",
    "さんみゃく": "MOUNTAIN", "山脈": "MOUNTAIN", "やま": "MOUNTAIN", "山": "MOUNTAIN",
    "まつばやし": "FOREST", "松林": "FOREST", "もり": "FOREST", "森": "FOREST", "はやし": "FOREST", "林": "FOREST",
    "きの手すり": "RAILING", "きのてすり": "RAILING", "木のてすり": "RAILING", "てすり": "RAILING",
    "さふぁいあ": "SAPPHIRE",
    "ずがいこつ": "SKULL", "頭蓋骨": "SKULL", "どくろ": "SKULL",
    "あおいとり": "SONGBIRD", "青い鳥": "SONGBIRD", "あおい鳥": "SONGBIRD",
    "どうぐばこ": "TOOL", "道具箱": "TOOL", "どうぐ箱": "TOOL", "どうぐ": "TOOL", "道具": "TOOL", "こうぐ": "TOOL", "工具": "TOOL",
    "ぞーくどる": "ZORKMIDS",
    "しんちゅう": "BRASS", "おうどう": "BRASS", "黄銅": "BRASS",
    "くりすたる": "CRYSTAL", "すいしょう": "CRYSTAL", "水晶": "CRYSTAL",
    "みどりいろ": "GREEN", "みどり": "GREEN", "緑": "GREEN", "緑色": "GREEN",
    "だいどころ": "KITCHEN", "台所": "KITCHEN",
    "ちょすいち": "RESERVOIR", "貯水池": "RESERVOIR",
    "さいだん": "ALTAR", "祭壇": "ALTAR",
    "いしづか": "BARROW", "つか": "BARROW",
    "こうもり": "BAT", "おもちゃ": "TOY",
    "かなりあ": "CANARY", "かなりや": "CANARY",
    "ろうそく": "CANDLES", "ろーそく": "CANDLES",
    "さかずき": "CHALICE", "杯": "CHALICE",
    "えんとつ": "CHIMNEY", "煙突": "CHIMNEY",
    "すべりだい": "SLIDE", "すべり台": "SLIDE",
    "がんぺき": "CLIFF", "ぜっぺき": "CLIFF", "絶壁": "CLIFF",
    "せきたん": "COAL", "石炭": "COAL",
    "おいはぎ": "THIEF", "強盗": "THIEF", "どろぼう": "THIEF",
    "おうごん": "GOLD", "黄金": "GOLD", "きん": "GOLD", "金": "GOLD",
    "りょうて": "HANDS", "両手": "HANDS",
    "ほうせき": "JEWELS",
    "いわだな": "LEDGE", "岩棚": "LEDGE",
    "ましーん": "MACHINE", "ましん": "MACHINE",
    "ぷらすちっく": "PLASTIC", "びにーる": "PLASTIC", "ぷらちな": "PLATINUM",
    "おりたたんだびにーる": "PLASTIC", "折りたたんだびにーる": "PLASTIC", "折畳んだビニール": "PLASTIC", "折畳んだびにーる": "PLASTIC",
    "さんどいっち": "SANDWICH",
    "ふなのり": "SAILOR", "船のり": "SAILOR", "すいふ": "SAILOR", "水夫": "SAILOR",
    "しゃべる": "SHOVEL", "しゃく": "SCEPTRE",
    "がいこつ": "SKELETON", "骸骨": "SKELETON",
    "かいだん": "STAIRCASE", "階段": "STAIRCASE",
    "たんけん": "STILETTO", "短剣": "STILETTO",
    "すいっち": "SWITCH", "てーぶる": "TABLE",
    "もくざい": "TIMBER", "木材": "TIMBER",
    "たいまつ": "TORCH", "とーち": "TORCH",
    "かくし扉": "DOOR", "かくし戸": "DOOR", "かくしど": "DOOR", "かくしとびら": "DOOR", "とびら": "DOOR", "扉": "DOOR", "どあ": "DOOR",
    "ざいほう": "TREASURE", "財宝": "TREASURE",
    "とらんく": "TRUNK", "ちゅーぶ": "TUBE",
    "あおいろ": "BLUE", "青色": "BLUE", "あお": "BLUE", "青": "BLUE",
    "ちゃいろ": "BROWN", "ちゃ色": "BROWN", "茶色": "BROWN", "ちゃ": "BROWN", "茶": "BROWN",
    "あかいろ": "RED", "赤色": "RED", "あか": "RED", "赤": "RED",
    "こだいの": "ANCIENT", "古代の": "ANCIENT",
    "したい": "BODY", "死体": "BODY", "遺体": "BODY",
    "ぼると": "BOLT", "ぼとる": "BOTTLE", "ぼたん": "BUTTON",
    "けーす": "CASE", "ひつぎ": "COFFIN", "棺": "COFFIN",
    "きれつ": "CRACK", "亀裂": "CRACK", "さけ目": "CRACK", "さけめ": "CRACK", "裂け目": "CRACK", "裂けめ": "CRACK",
    "たまご": "EGG", "卵": "EGG",
    "じめん": "GROUND", "地面": "GROUND",
    "ないふ": "KNIFE", "はもの": "KNIFE", "刃物": "KNIFE",
    "らべる": "LABEL", "はしご": "LADDER", "梯子": "LADDER",
    "らんぷ": "LAMP", "あかり": "LAMP",
    "おちば": "LEAVES", "落葉": "LEAVES",
    "まっぷ": "MAP", "ちず": "MAP", "地図": "MAP",
    "かがみ": "MIRROR", "鏡": "MIRROR", "みらー": "MIRROR",
    "ぱねる": "PANEL", "こみち": "PATH", "小道": "PATH", "みち": "PATH", "道": "PATH",
    "つうろ": "PASSAGE", "通路": "PASSAGE",
    "だいざ": "PEDESTAL", "台座": "PEDESTAL",
    "ぽんぷ": "PUMP", "空気ポンプ": "PUMP",
    "ろーぷ": "ROPE", "ふくろ": "SACK", "袋": "SACK",
    "つるぎ": "SWORD", "剣": "SWORD", "けん": "SWORD",
    "とろる": "TROLL", "こまど": "WINDOW", "小窓": "WINDOW", "まど": "WINDOW", "窓": "WINDOW",
    "れんち": "WRENCH", "どーむ": "DOME",
    "おがわ": "STREAM", "小川": "STREAM",
    "きいろ": "YELLOW", "き色": "YELLOW", "黄色": "YELLOW", "黄": "YELLOW",
    "まほう": "MAGIC", "魔法": "MAGIC",
    "かご": "BASKET", "篭": "BASKET",
    "べる": "BELL", "いた": "BOARD", "板": "BOARD",
    "ほん": "BOOK", "本": "BOOK", "ぶい": "BUOY", "だむ": "DAM",
    "みず": "WATER", "水": "WATER", "いえ": "HOUSE", "家": "HOUSE",
    "かぎ": "KEY", "鍵": "KEY", "かみ": "PAPER", "紙": "PAPER",
    "つぼ": "POT", "壷": "POT", "にじ": "RAINBOW", "虹": "RAINBOW",
    "かわ": "RIVER", "川": "RIVER", "河": "RIVER",
    "すな": "SAND", "砂": "SAND", "ぎん": "SILVER", "銀": "SILVER",
    "かべ": "WALL", "壁": "WALL", "がす": "GAS",
    "くろ": "BLACK", "黒": "BLACK",
    "歯": "TEETH", "石": "STONE", "西": "WEST", "巣": "NEST",
    "革": "LEATHER", "木": "TREE", "斧": "AXE",
    "ぼう": "BAR", "棒": "BAR",
    "おんけい": "TROPHY",          # 恩恵 reward -> trophy case context
    # ---- adjectives ----
    "ちいさな": "SMALL", "ちいさい": "SMALL", "小さな": "SMALL", "小さい": "SMALL",
    "おおきな": "LARGE", "おおきい": "LARGE", "大きな": "LARGE", "大きい": "LARGE",
    "しろい": "WHITE", "白い": "WHITE",
    "くろい": "BLACK", "黒い": "BLACK",
    "あおい": "BLUE", "青い": "BLUE",
    "あかい": "RED", "赤い": "RED",
    "きいろい": "YELLOW", "黄色い": "YELLOW",
    "ちゃいろい": "BROWN", "ちゃ色い": "BROWN", "茶色い": "BROWN",
    "ふるい": "OLD", "古い": "OLD",
    "せまい": "NARROW", "狭い": "NARROW",
    "さびた": "RUSTY", "錆びた": "RUSTY",
    "くさい": "SMELLY", "臭い": "SMELLY",
    "しんだ": "DEAD", "死んだ": "DEAD",
    "もくせいの": "WOODEN", "木製の": "WOODEN",
    "みかげいしの": "GRANITE", "みかげ石の": "GRANITE", "御影いしの": "GRANITE", "御影石の": "GRANITE",
    "ひすいの": "JADE",
    "えるふのような": "ELVISH", "えるふの様な": "ELVISH", "ようせいの": "ELVISH", "妖精の": "ELVISH",
    "ちまみれの": "BLOODY", "ち塗れの": "BLOODY",
    "いたを打ちつけた": "BOARDED", "いたをうちつけた": "BOARDED", "板をうちつけた": "BOARDED",
    "がらすのような": "GLASS", "がらすしつの": "GLASS", "がらす質の": "GLASS",
    "とろふぃー": "TROPHY", "せんりひんの": "TROPHY", "戦利品の": "TROPHY",
    "とらっぷ": "TRAP", "わなのある": "TRAP",
    "制御": "CONTROL", "塚": "BARROW",
}


def records(v):
    o = REC0
    while o + STRIDE <= len(v):
        idv = struct.unpack("<H", v[o:o+2])[0]
        flag = v[o+2]
        kw = v[o+3:o+STRIDE].split(b"\x00")[0]
        yield o, idv, flag, kw
        o += STRIDE


# Savestate-confirmed correct id per English word (overrides the canonical-id heuristic).
FORCE_ID = {
    "MAILBOX": 0x025e,   # ゆうびんばこ
    "SMALL":   0x0319,   # ちいさな (attributive form the object name uses)
}


def patch_zvoctbl(v, mapping=READING_EN, verbose=False):
    """Translate noun/adj keywords to English IN PLACE.

    CRITICAL: each English word must resolve to exactly ONE id. Multiple readings may map to the same
    English word; if they carry DIFFERENT ids the lookup would return an inconsistent/wrong id and the
    command fails. So per English word we pick ONE canonical id (FORCE_ID override, else the id of the
    first reading in map order = the primary reading) and translate ONLY records that carry that id.
    Same id shared by different English words is fine (that's how the original vocabulary works)."""
    out = bytearray(v)

    # 1) choose the canonical id for each English word = id of its FIRST reading in map order
    #    (the map deliberately lists each word's primary reading first).
    reading2id = {}
    for o, idv, flag, kw in records(v):
        if flag in (0x80, 0x20):
            reading2id.setdefault(kw.decode("shift_jis", "ignore"), idv)
    canon = {}
    for reading, en in mapping.items():
        if en not in canon and reading in reading2id:
            canon[en] = reading2id[reading]
    canon.update({w: i for w, i in FORCE_ID.items() if w in canon})

    # 2) translate only records whose (English, id) matches the canonical choice
    hits = skipped_ambig = 0
    for o, idv, flag, kw in records(v):
        if flag not in (0x80, 0x20):
            continue
        try:
            s = kw.decode("shift_jis")
        except Exception:
            continue
        en = mapping.get(s)
        if en is None:
            continue
        if idv != canon.get(en):                   # a same-word-different-id duplicate -> leave JP
            skipped_ambig += 1
            continue
        enc = enc_fw(en)
        if len(enc) > KWMAX:
            print("  SKIP oversize %r (%d > %d B) for %r" % (en, len(enc), KWMAX, s))
            continue
        field = bytearray(KWMAX)
        field[:len(enc)] = enc                      # rest stays 0x00 (terminator/pad)
        out[o+3:o+STRIDE] = field
        hits += 1
        if verbose:
            print("  id=0x%04x pos=0x%02x  %-12s -> %-10s" % (idv, flag, s, en))
    assert len(out) == len(v), "size changed!"
    if verbose:
        print("ZVOCTBL: %d records translated, %d distinct English words, %d ambiguous-id dupes left JP"
              % (hits, len(canon), skipped_ambig))
    return bytes(out)


def load_zvoctbl():
    p = os.path.join(os.path.dirname(__file__), "..", "work", "zork1", "ZVOCTBL.DAT")
    return open(p, "rb").read()


if __name__ == "__main__":
    import io, sys
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
    v = load_zvoctbl()
    patch_zvoctbl(v, verbose=True)
