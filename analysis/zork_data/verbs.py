"""Action-menu verbs and mode labels (mostly DONE).

VERBS = JP verb -> English. Relocated to the pool as FULL-WIDTH English and the verb display
        table (0x22410, both copies) is repointed by JP-string match. 183 verbs.
MENU  = JP mode label -> English, rewritten IN-PLACE as full-width SJIS (the menu uses the raw
        font, not the ASCII->full-width path), padded to the JP byte length.
(Address ranges for both live in zork_translate.py: VERB_TABLE_LO/HI, MENU_LO/HI.)"""

VERBS = {
 "あかりをつける":"LIGHT","悪魔払いをする":"EXORCISE","開ける":"OPEN","上げる":"RAISE","与える":"GIVE",
 "歩き回る":"WANDER","歩く":"WALK","言う":"SAY","行く":"GO","いっぱいにする":"FILL","移動する":"MOVE",
 "祈る":"PRAY","入れる":"INSERT","上に乗る":"MOUNT","動かす":"BUDGE","打つ":"HIT","奪う":"SEIZE",
 "うるさい":"BE LOUD","演じる":"PLAY","追う":"CHASE","大声をあげる":"SHOUT","応答する":"REACT","置く":"PUT",
 "起こす":"WAKE","押す":"PUSH","落とす":"DROP","泳いで渡る":"SWIM ACROSS","泳ぐ":"SWIM","降りる":"DESCEND",
 "下ろす":"LOWER","掲げる":"HOLD UP","鍵を開ける":"UNLOCK","鍵を掛ける":"LOCK","鍵をはずす":"UNDO LOCK",
 "数える":"COUNT","勝つ":"WIN","聞く":"ASK","刻む":"CARVE","キスする":"KISS","傷つける":"HURT","切る":"CUT",
 "きれいにする":"CLEAN","空気を入れる":"INFLATE","空気を抜く":"DEFLATE","加える":"ADD","消す":"EXTINGUISH",
 "蹴る":"KICK","後悔する":"REPENT","攻撃する":"ATTACK","後退する":"RETREAT","強奪する":"ROB","呼吸する":"BREATHE",
 "こじ開ける":"PRY","こする":"RUB","答える":"ANSWER","転がす":"ROLL","殺す":"KILL","壊す":"BREAK","捜す":"SEARCH",
 "叫ぶ":"SCREAM","下げる":"LET DOWN","刺す":"STAB","去る":"LEAVE","触る":"TOUCH","下を覗く":"LOOK UNDER",
 "閉める":"CLOSE","自由にする":"FREE","呪文を唱える":"CAST","上陸":"LAND","調べる":"EXAMINE","進水":"LAUNCH",
 "スキップする":"SKIP","捨てる":"DISCARD","スプレーする":"SPRAY","ずらす":"SHIFT","座る":"SIT","ぜんまいを巻く":"WIND",
 "栓をする":"PLUG","注ぐ":"POUR","外に出る":"EXIT","滞在する":"STAY","抱きしめる":"HUG","たたく":"TAP","立つ":"STAND",
 "食べる":"EAT","突き刺す":"PIERCE","突く":"POKE","作る":"MAKE","点ける":"TURN ON","告げる":"TELL","つつく":"PROD",
 "つぶやく":"MUTTER","出る":"GO OUT","点灯する":"ILLUMINATE","通る":"PASS","溶かす":"MELT","閉じる":"SHUT","どなる":"YELL",
 "跳び越える":"JUMP OVER","跳び越す":"LEAP","跳ぶ":"JUMP","灯す":"KINDLE","取り上げる":"SNATCH","取り出す":"REMOVE",
 "取る":"TAKE","中に入る":"ENTER","投げる":"THROW","なでる":"PET","鳴らす":"RING","臭いをかぐ":"SMELL","臭う":"SNIFF",
 "握る":"GRIP","塗る":"APPLY","ねじを巻く":"WIND UP","覗き込む":"PEER","覗く":"PEEK","望む":"WISH","ノックする":"KNOCK",
 "ののしる":"CURSE","登る":"CLIMB","飲む":"DRINK","乗り込む":"BOARD","乗る":"RIDE","呪う":"DAMN","入る":"GO IN",
 "破壊する":"DESTROY","爆破する":"BLAST","はずす":"DETACH","話す":"TALK","跳ねる":"HOP","パンクさせる":"PUNCTURE",
 "引き上げる":"HOIST","引く":"PULL","引っ張る":"TUG","開く":"OPEN UP","拾う":"PICK UP","火をつける":"IGNITE",
 "火を灯す":"BURN","ふきかける":"SPRITZ","吹き消す":"BLOW OUT","吹き込む":"BLOW IN","膨らませる":"BLOW UP","塞ぐ":"BLOCK",
 "振り上げる":"SWING","振り返る":"TURN AROUND","振り回す":"WAVE","振り向く":"TURN","振る":"SHAKE","触れる":"FEEL",
 "返事する":"REPLY","返答する":"RESPOND","ほどく":"UNTIE","掘る":"DIG","巻く":"COIL","待つ":"WAIT","魔法をかける":"ENCHANT",
 "魔法を解く":"DISENCHANT","回す":"ROTATE","見上げる":"LOOK UP","見下ろす":"LOOK DOWN","磨く":"POLISH","水に降ろす":"SET AFLOAT",
 "水をかける":"DOUSE","満たす":"FILL UP","見つける":"FIND","見回す":"LOOK AROUND","見る":"LOOK",
 "むしゃむしゃ食べる":"DEVOUR","結ぶ":"TIE","命じる":"ORDER","命令する":"COMMAND","めくる":"FLIP","持ち上げる":"LIFT",
 "燃やす":"BURN UP","やっつける":"DEFEAT","揺らす":"ROCK","横切る":"CROSS","呼ぶ":"CALL","読む":"READ","寄りかかる":"LEAN",
 "ロックする":"LOCK UP","渡す":"HAND","渡る":"GO ACROSS",
}

MENU = {  # full-width: <= JP char count
    "動　作": "ACT", "★一文字入力": "LETTER", "★動詞選択": "VERBS", "★登録語": "MEMO",
}
