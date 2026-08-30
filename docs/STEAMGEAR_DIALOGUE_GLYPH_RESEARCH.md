# Steamgear Mash — Dialogue transcription & unmapped-glyph research

Working notes for finishing the **original-Japanese transcription** of the in-game dialogue,
and for resolving the handful of font glyphs not yet in the kana map. The *English
translation* is already done and patched; this doc is purely for research / completeness.

## How the dialogue text is stored (recap)

- Dialogue lives in `0.BIN` at **`0x5AF40`–`0x5BBB2`** (HWRAM `0x0605EF40`–`0x0605FBB2`),
  102 fixed-width lines. Worklist: `analysis/steamgear_dialogue_worklist.json`.
- Each on-screen character is a **big-endian u16 code = glyph_index × 4**, drawn by
  `FUN_06005020`. Line breaks: `0xFFFF` = newline, `0x0000` = box/message end. Lines are
  space-padded with glyph **470** (the dialogue-font space).
- **Two fonts, two glyph numberings:**
  - *Thin menu font* — glyph = ASCII (`A`=65). Used for menus, the title screen, and the
    save-record rows (e.g. the `キロク` katakana = thin-font katakana). Resident at VDP2
    VRAM base 0.
  - *Chunky dialogue font* — glyph = **ASCII + 438** (`space`=470, `!`=471, `A`=503…). Used
    for all dialogue boxes incl. the Mina cutscene. Kana/kanji are glyphs **≥ ~564**.
- **The `キロク` (record) labels are the thin font; the unmapped dialogue glyphs below are the
  chunky font — different sets, so they are unrelated.**

## Reading the chunky font from a savestate

`analysis/render_glyphs.py` / `font_gray.py` render glyph indices straight from a savestate's
VDP2 VRAM, **assuming the font sits at VRAM `0x14000` with glyph G at `0x14000 + G*0x80`**
(16×16, 4bpp, stored as 4× 8×8 quadrants TL,TR,BL,BR). This is true on the **menu / dialogue
savestate** that the original kana map was built from.

⚠️ **Gotcha (2026-06-12):** on an *in-game gameplay* savestate the chunky font is loaded at a
**different VRAM base and layout**, so the `0x14000` tools render garbage there. The thin 8×8
font sits near VRAM 0 on that screen (4 thin glyphs pack into one 16×16 contact cell, which
*looks* like readable ASCII but isn't the dialogue font). **To read the 5 glyphs below: use a
title-menu / dialogue-box savestate where the font is at `0x14000`** (then
`python analysis/render_glyphs.py 0 out.png "571,590,627,648,687"`), or first locate the
chunky-font base in the gameplay state by finding the box's NBG tilemap (cell = glyph×4).

## Kana map status

`analysis/steamgear_kana_map.json` — 101 entries, built by clean-context inference + reading
the game's own "ROLL" tilemap from a savestate. Known examples: 706=マ 667=シ 569=う 576=が
635=よ 565=あ 612=ば 679=ツ 691=ハ 700=ヘ 669=ス; 483=ー(dash) 470=space 471=！.
**Still UNMAPPED (the 5 below):** glyphs 571, 590, 627, 648, 687.

## The 5 unmapped glyphs, in context

Decoded Japanese (mapped glyphs shown; `{gNNN}` = still unmapped) paired with the shipped
English. Offsets are the line's `0.BIN` file offset.

### `{g687}` — a villain's sentence-ending verbal tic `〜{g687}ョロ`
- L84 `0x5B984`: `あんく{g687}ョロね。アタシたちの じゃまを`
- L85 `0x5B9AA`: `するのはいけない{g687}ョロよ！`
- English: **"DON'T GET IN OUR WAY!"**
- Note: other characters' tics are `…さンス` (1st villain) and `…でシュ` (Mash's robot). This
  third character uses `…{g687}ョロ`. Identifying the catchphrase pins `{g687}`.

### `{g627}{g590}` — a 2-char word (likely a place), spoken by the robot (`…でシュ`)
- L86 `0x5B9C6`: `これで{g627}{g590}の…にいけるでシュ。`
- L87 `0x5B9E8`: `ミーナちゃんを おいかけるんでシュ！`
- English: **"GO AFTER MINA! / CHASE HER DOWN!"**
- Note: literally "Now I can get to `{g627}{g590}`'s… — I'll chase Mina!". `{g627}{g590}` is
  probably うちゅう ("space") or the villains' base name.

### `{g648}` (scream) and `{g571}` (insult) — final boss's defeat line
- L98 `0x5BB5A`: `キ{g648}{g648}{g648}..！ どま{g571}なんかに まける`
- L99 `0x5BB80`: `みんか！！`  (= まけるもんか, "as if I'd lose!")
- English: **"GAAAH..! I WON'T / LOSE!"**
- Note: `キ{g648}{g648}{g648}` is a scream, `{g648}`×3 — almost certainly the long-vowel `ー`
  → "キーーー！". `どま{g571}なんか` is an insult, "a `{g571}` like you" (e.g. どチビ / おまえ).

## Full Mina cutscene (story climax) — JP ↔ EN, `0x5B87E`–`0x5BBB2`

Mash chases the kidnapped Mina through the bosses into the villains' space base. `…でシュ` =
robot sidekick, `…さンス`/`…{g687}ョロ` = villains. Box line 1 `0x5B87E` was the placeholder
`MINA!`; it is now patched to the two-line box **"I'LL SAVE YOU / MINA!"**.

| Off | English (shipped) | Japanese (decoded) |
|-----|-------------------|--------------------|
| 5B87E/88E | I'LL SAVE YOU / MINA! | ミーナちゃん！ / いま、たすけにいくっシュ！！ |
| 5B8B2 | YOU CAME THIS FAR! | よくここまできた…ンスね。でも ワタシ |
| 5B8D8 | I WON'T LOSE! | に かつなんてムリさンスよ！ |
| 5B8F4 | TOO BAD, BUT | はられた…ンスねー。ざんねんだけど、 |
| 5B91A | MINA ISN'T HERE! | ここに ミーナちゃんは いないさンスー！ |
| 5B940 | IS MINA HERE? | ミーナちゃんは ここでシュか？ |
| 5B95E | I'LL FIND HER!! | とにかく さがすんでシュよ！！ |
| 5B984 | DON'T GET IN OUR | あんく`{g687}`ョロね。アタシたちの じゃまを |
| 5B9AA | WAY! | するのはいけない`{g687}`ョロよ！ |
| 5B9C6 | GO AFTER MINA! | これで`{g627}{g590}`の…にいけるでシュ。 |
| 5B9E8 | CHASE HER DOWN! | ミーナちゃんを おいかけるんでシュ！ |
| 5BA0C | NOT BAD, MASH! | なかなか やるねマッシュくん。さあ、 |
| 5BA30 | LET'S END THIS! | けっちゃくを つけようじゃないか！ |
| 5BA52 | I LOST TO YOU?! | まさかキミに まけるとは。ミーナちゃん |
| 5BA78 | I'LL TAKE MINA! | は うちゅうきちに つれていくぞ！ |
| 5BA9A | NOW I CAN CHASE | これでシュ！これがあれば、うちゅう |
| 5BABE | INTO SPACE!! | だって おいかけられるでシュ！！ |
| 5BADE | I MADE IT! | はっと ついたでシュ。 |
| 5BAF4 | MINA IS HERE!! | ミーナちゃんは ここに いるでシュ！！ |
| 5BB18 | MADE IT THIS FAR? | よくここまで きたな。だが ミーナちゃん |
| 5BB3E | SHE'S MINE! | は ぜッタイに わたさないぞ！ |
| 5BB5A | GAAAH..! I WON'T | キ`{g648}{g648}{g648}`..！ どま`{g571}`なんかに まける |
| 5BB80 | LOSE! | みんか！！ |
| 5BB8C | BASE IS EXPLODING! | タイヘンでシュ！きちが バクハツするでシュ。 |
| 5BBB2 | HURRY!! | いそいで |

*(Decode caveat: the kana map is partly inferred, so a few characters in the JP column may be
off by one glyph — e.g. some `きた`/`やる` spots — treat oddities as map gaps, not the game's
text. The English column is the authoritative shipped translation.)*

## To finish this

1. Grab a **title-menu / dialogue-box savestate** (font at VRAM `0x14000`).
2. `python analysis/render_glyphs.py <slot> out.png "571,590,627,648,687"` and read the kana.
3. Add them to `analysis/steamgear_kana_map.json`; re-decode the worklist to clean the JP column.

---

# Complete original → translation reference (all patched text)

Original Japanese paired with the shipped English, for every string the patch changes.
`{gNNN}` = a font glyph not yet in the kana map (the JP is otherwise reconstructed; a few
inferred kana may be off by one — the English column is authoritative).

## Title menu + sound (thin menu font)
| 0.BIN off | Original (JP) | English |
|-----------|---------------|---------|
| 0x45690 | (start prompt) | PRESS START BUTTON |
| 0x49C90 | ニューゲームスタート | NEW GAME |
| 0x49CA8 | ボタンセット | BUTTON |
| 0x49CB8 | サウンドモード | SOUND |
| 0x49CCA | ステージセレクト | STAGE SEL |
| 0x49D10 | タイトルにもどる | TO TITLE |
| 0x49D24 | ステレオ | STEREO |
| 0x49D2E | モノラル | MONO |

## Save-record rows (thin menu font)
| Element | Original | English / result |
|---------|----------|------------------|
| Row label | キロク1〜4 (キロク = 記録 "record") | label dropped; rows show ` 1`〜` 4` shifted left 3 |
| Column header | HOUR / MIN / SEC (already Latin) | kept; moved left to align |

## BUTTON-config action labels (thin menu font)
| 0.BIN off | Original (JP) | Meaning / button | English |
|-----------|---------------|------------------|---------|
| 0x49D4A | ショット | shoot (A) | SHOT |
| 0x49D60 | ジャンプ | jump (B) | JUMP |
| 0x49D6C | ウエポン | special weapon, blue bar (C) | WEAPON |
| 0x49D78 | 武器選択 | change special weapon (X/Z) | CHG WPN |
| 0x49D84 | 移動選択 | use skill (L) | SKILL |
| 0x49D90 | 移動決定 | swap/use special skill (R) | CHG SKL |
| 0x49D9C | 方向ボタン | direction pad | D-PAD |
| 0x49DA8 | リセット | reset | RESET |
| 0x49DB4 | スタートボタン＝決定 | start = confirm | START = OK |

## Boot backup-RAM dialog (chunky dialogue font)
| 0.BIN off | Original (JP) | English |
|-----------|---------------|---------|
| selection (0x5B6EC…) | 現在 …どちらを バックアップに つかいますか？ | BACKUP MEMORY CART / IS INSERTED. / WOULD YOU LIKE TO / SAVE TO SYSTEM OR / CARTRIDGE? |
| choices | A＝カートリッジ / C＝本体 | A CARTRIDGE / C SYSTEM |
| console confirm (0x5B7DE) | 本体RAMをきろくのバックアップに、つかいます。 | SAVING GAME TO / SYSTEM. |
| cartridge confirm (0x5B812) | カートリッジRAMをきろくのバックアップに、つかいます。 | SAVING GAME TO / CARTRIDGE. |

## In-game dialogue — hints & system messages (chunky dialogue font)
Lines L1–L62 (the Mina cutscene L76–L101 is in the table earlier in this doc). `…でシュ` =
Mash's robot sidekick speech tic.

| L | 0.BIN off | English | Original (JP, decoded) |
|---|-----------|---------|------------------------|
| 1/2 | 0x5AF50 | USE FIRE SHOT / TO BREAK THIS. | ダメでシュ。ファイヤーショット じゃないとこわせないでシュ。 |
| 3/4 | 0x5AF8E | ONLY A PUNCH CAN / BREAK IT. | このブロックは、パンチじゃないと こわせないでシュ。 |
| 5/6 | 0x5AFC4 | LUCKY! THIS PUNCH / BREAKS GREEN. | ラッキーでシュ。このパンチでグリーン ブロックをこわせるでシュ。 |
| 7/8 | 0x5B006 | FIRE SHOT BREAKS / WARP BLOCKS TOO. | …でシュ。ファイヤーショットで ワープブロックをこわせるでシュ。 |
| 9/10 | 0x5B04C | ICE SHOT BREAKS / BLUE BLOCKS. | アイスショットでブルーブロックをこわ すでシュ。 |
| 11/12 | 0x5B094 | FIVE SHOT FIRES / FIVE! | ファイブショットでシュ。5はつのタマ をうつんでシュ。 |
| 13/14 | 0x5B0CC | YOU CANNOT START / THE GAME. | …データの{破損?}でゲームをスタートする ことはできません。 |
| 15/16 | 0x5B102 | CLEAR DATA, / START A NEW GAME. | このデータをクリアします。 あたらしくゲームをはじめてください。 |
| 17/18 | 0x5B144 | HEART SHOT REFILLS / MY ENERGY! | はっく！ハートショットでシュ。ボクの エネルギーをかいふくするっシュ。 |
| 19/20 | 0x5B18C | HOMING MISSILE! / CHASE ENEMIES. | シュ○○！ホーミングミサイルでシュ。 テキをおいかけるタマっシュ。 |
| 21/22 | 0x5B1D0 | MISSILE BREAKS THE / BLACK BLOCKS! | ランブ○ミサイルでシュ。くろいブロッ クもこわしちゃうんでシュ。 |
| 23/24 | 0x5B212 | ROLL! SPIN AROUND / TO MOVE. | ROLLっシュ。グルグルまわりながら うごけるんでシュ。 |
| 25/26 | 0x5B24C | BACK! YOU CAN MOVE / BACKWARD | BACKでシュ。うしろをむいたまま、 うごくんでシュ。 |
| 27/28 | 0x5B284 | SIGHT! TAKE AIM AT / ENEMIES. | SIGHTでシュ。○ートでテキにねら いをつけるっシュ。 |
| 29/30 | 0x5B2BE | DASH! YOU CAN DO A / DASH JUMP. | DASHでシュ。ダッシュは、ダッシュ ジャンプができるんでシュよ。 |
| 31–39 | 0x5B302 | CANNOT SAVE NOW. … PRESS START TO BEGIN. | このままゲームをはじめると、データを 記録できません。データの記録には71 ブロックがいつようです。…セガサターン本体の 記録データツールをすすめ、ほかの ゲームのデータをけすか、カートリッジ RAMにきろーしてからゲームをはじめ てください。…スタートボタンをおすとこのままゲーム をはじめます。 |
| 40–43 | 0x5B468 | THIS SAVE DATA CAN'T START / A GAME. CLEAR IT FOR A NEW GAME. | データの{破損}でゲームをスタート することはできません。このデータをクリアします。 あたらしくゲームをはじめてください。 |
| 44–51 | 0x5B4E0 | THIS DATA CAN'T RUN A GAME. CLEAR IT & START A NEW GAME | このデータでゲームをスタートすること はできません。このデータをクリアしま す。あたらしくゲームをはじめてく ださい。 |
| 52/53 | 0x5B5AE | SAVE DATA IS FULL. / YOU CANNOT SAVE. | データがいっぱいでセーブボックスうご かないでシュ。セーブできないでシュ。 |
| 54 | 0x5B5FA | SAVE NOW? | セーブしまシュか？ |
| 55/57/59/62 | 0x5B60E… | A,C=OK   B=CANCEL | A、C○○＝○○ B○○＝キャンセル |
| 56 | 0x5B634 | SAVE WHERE? | どこにセーブしまシュか？ |
| 61 | 0x5B6B6 | OK? | いいでシュか？ |

*(Remaining `{gNNN}`/`○` in the JP column are unmapped chunky-font glyphs — notably the
記録/データ/ゲーム/ボ kana clusters in the save-system lines; see the kana-map TODO above.)*
