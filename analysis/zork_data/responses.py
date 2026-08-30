"""Parser ACTION-RESPONSE / error messages -> English (Zork I Saturn JP, 0ZORK.BIN).

These are the command-result / rejection messages the parser prints after a verb runs
(e.g. the generic "you can't do that here", "too hot to take", "cannot open it"). They
live as 0x1c-delimited, 0x00-sub-delimited concatenated SJIS blocks in the ~0x0608f000..
0x06094000 region and are referenced by BIG-ENDIAN u32 pointers scattered across code
literal pools (NOT one master table). Each message is rendered by the SAME engflag
+0x1F prose renderer as room text, and rendering STOPS at the first 0x00 or 0x1c.

Because there is no single pointer table, these are translated IN PLACE: for each pointer
TARGET address below, the build overwrites [target, next 0x00/0x1c) with the engflag-encoded
English + a 0x00 terminator (must be <= the original span; leftover JP bytes past the 0x00
are never rendered). Every scattered pointer that references that target then shows English
with zero repointing. See zork_translate_engflag.py Phase 5.

Format (same as messages.py): value is a SEGMENT LIST.
  * str  = literal English (lowercase; '.' is the only renderable punctuation, others dropped;
           the renderer word-wraps at 16, so '\n' is rarely needed here)
  * int  = 0x0e object token (dict234 index -> renders the English object name)
Keys are ABSOLUTE addresses (load base 0x06004000) of the pointer target = message start.
JP source shown in the trailing comment.
"""

RESPONSES = {
    # ---- generic parse / action rejection (PRIORITY: the "can't do that" catch-all) ----
    0x6092712: ["that seems reasonable but you cannot do that here."],  # 道理にかなっているようですが、ここではそれは駄目ですね。
    # OUT-OF-SCOPE reply (take/read/examine an object not in scope). 27B span, delim 0x00,
    # generic (no object token). This is the "garbled error" seen on leaflet-out-of-scope.
    0x603c470: ["you cannot see that here."],         # ここには、見あたりません。
    # ---- verb success acks (found via content search; span-verified) ----
    0x6015a98: ["taken."],                               # 取りました。 (14B span, ptr @0x06015bd8) — take/get ack
    # NOTE: the REAL take-success ack renders from 0x06099143 (取り + ctrl 0x02 -> 取りました。, ptr
    # 0x602f774) via a DIFFERENT path (TRANSCODE mode 1 = ASCII->full-width verb engine, R7 caller in
    # LWRAM). Savestate proof: after take, 0x060af16c held the transcoded ack but it is NEVER drawn to
    # screen (take response is empty in every capture). In-place engflag edit there is mis-encoded
    # (became full-width ＵＰＰＬ) and still not displayed -> needs display-path RE, not a string edit.
    # PARKED: see docs/ZORK1_PICKER_VERB_FIX_HANDOFF.md P1.
    0x608fffd: ["why not use your head."],                # どうして頭を使わないのですか？
    0x609010f: ["you jest."],                             # ご冗談を。
    0x6090277: ["nice try."],                             # がんばったね。
    0x6093eb9: ["that is a good idea."],                  # それはいい考えですね。

    # ---- common verb-result failures ----
    0x6090555: ["the ", 45, " is too hot to take."],      # {o45}は焼けるように熱く、取ることはでき<03>
    0x6090658: ["you cannot enter."],                     # {o232}には入れません。
    0x6090946: ["too heavy."],                            # {o74}は重すぎます。
    0x60926c2: ["cannot put that in."],                   # {o135}の中には何も入れられ<03>
    0x6093fe9: ["cannot open it."],                       # この{o74}は開けられ<03>
    0x6091485: ["cannot lock from here."],                # こちら側からはカギを掛けられ<03>
    0x60914b7: ["the lock is too far."],                  # ここからはカギに手が届き<03>
    0x6090c1c: ["too laden to climb here."],              # 持ち物が多すぎて、ここを登ることはでき<03>
    0x6090c44: ["too dark to climb without a lamp."],     # ランプがなくては暗くて上がれないでしょう。
    0x6090beb: ["climbing empty handed is a bad idea."],  # 手ぶらで登るのは、いいアイデアとは言えませんね。

    # ---- state / flavor acknowledgements ----
    0x6092562: ["click."],                                # カチッ。
    0x6090537: ["creak."],                                # キィー！
    0x6090540: ["clang clang."],                          # カラーン、カラーン。
    0x609256b: ["empty."],                                # 箱の中はカラ<05>
    0x60926f7: ["it looks empty."],                       # {o135}は見たところ空の<04>
    0x6092579: ["the box crumbles at a touch."],          # 箱はとても汚く、触っただけで崩れてしまい<02>
    0x609001c: ["probably to the west."],                 # たぶん西の方でしょう。
    0x6090033: ["you were here just moments ago."],       # ここには、ほんの数分前にいましたね…
    0x6090067: ["it is right here. are you blind."],      # ここですよ！　目が見えないんですか？
}
