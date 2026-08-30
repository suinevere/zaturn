#!/usr/bin/env python3
"""Set menu translations, apply ASCII + menu worklists to 0.BIN, write patched 0.BIN."""
import sys, os, json
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, root)
from saturn_translate import sgtext

A = os.path.join(root, "analysis")
zero = os.path.join(root, "game_originals", "Steamgear Mash (Japan)", "0.BIN")
ascii_wl = os.path.join(A, "steamgear_0bin_worklist.json")
menu_wl = os.path.join(A, "steamgear_menu_worklist.json")
dlg_wl = os.path.join(A, "steamgear_dialogue_worklist.json")
out0 = os.path.join(root, "game_patched", "steamgear_mash_translation", "0.BIN")

# proposed budget-fitting English, keyed by 0.BIN offset
MENU = {
    # leading space preserved (originals start with a space glyph; dropping it clips
    # the first letter). budget = (bytes-2)/2 chars incl. the leading space.
    0x49C90: " NEW GAME",   # ニューゲームスタート (24B / 11ch)
    0x49CA8: " BUTTON",     # ボタンセット       (16B / 7ch)
    0x49CB8: " SOUND",      # サウンドモード     (18B / 8ch)
    0x49CCA: " STAGE SEL",  # ステージセレクト   (22B / 10ch)
    0x49D10: " TO TITLE",   # タイトルにもどる   (20B / 9ch)
    # 0x49DA8 RESET is relocated (with a leading space) in the BTN_RELOC block, not in place.
    # NOTE: the SOUND toggle labels ステレオ/モノラル are NOT edited in place — they are
    # relocated below (see RELOCATE) so "STEREO" (6 glyphs) fits and both widths match.
}
# Stereo/Mono toggle, relocated. Both labels are pointer-referenced (a 2-entry table at
# 0.BIN 0xA0BC=STR, 0xA0C0=MONO). Their in-place slots are only 4 glyphs wide
# (ステレオ/モノラル, 5 codes incl. NUL), so full "STEREO" (6) won't fit in place, and a
# 3-glyph "STR" leaves MONO's 4th glyph on screen during the redraw (the "STRO" bug).
# Fix: write both into a padding gap and repoint; pad MONO to STEREO's width so toggling
# either way fully overwrites the previous label.
RELOCATE = {
    0x49D24: "STEREO",   # STR pointer  @0xA0BC
    0x49D2E: "MONO  ",   # MONO pointer @0xA0C0 (padded to 6 glyphs = STEREO width)
}
RELOC_BASE = 0x49820         # inside the 105-byte zero-pad gap at 0x49816
RELOC_PTRS = {0x49D24: 0xA0BC, 0x49D2E: 0xA0C0}

spec = json.load(open(menu_wl, encoding="utf-8"))
for e in spec["strings"]:
    if e["offset"] in MENU:
        e["translation"] = MENU[e["offset"]]
    elif e["offset"] in RELOCATE:
        e["translation"] = ""   # leave the original bytes in place; we repoint instead
json.dump(spec, open(menu_wl, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
print(f"set {len(MENU)} menu translations; relocating {len(RELOCATE)} toggle labels")

import struct
data = open(zero, "rb").read()
buf = sgtext.apply_worklist_file(data, ascii_wl)   # PRESS START BUTTON
buf = sgtext.apply_worklist_file(buf, menu_wl)      # menu items
# tutorial move-hint boxes (Fire Shot/Punch/ROLL/BACK/SIGHT/DASH + warp box). These are
# kind=dialogue entries -> white chunky font (font_base=438), fixed width per line. Boot
# dialog (L63-L73) and the Mina box (L76+) stay handled by the explicit BOOT/DLG edits
# below, so the offsets don't overlap.
buf = sgtext.apply_worklist_file(buf, dlg_wl)       # tutorial hint dialogue

# dialogue test: overwrite the ミーナちゃん line at 0.BIN 0x5B88E with English (fixed width,
# delimiters kept) to prove Latin renders in the dialogue scene.
buf = bytearray(buf)

# relocate the Stereo/Mono toggle labels into the padding gap and repoint
off = RELOC_BASE
for slot, eng in RELOCATE.items():
    enc = sgtext.encode_wide(eng)          # 2 bytes/glyph + 2-byte NUL
    buf[off:off + len(enc)] = enc
    struct.pack_into(">I", buf, RELOC_PTRS[slot], sgtext.LOAD_BASE + off)
    print(f"  relocate 0x{slot:05X} -> {eng!r} @0.BIN 0x{off:05X} "
          f"(HWRAM 0x{sgtext.LOAD_BASE + off:08X}); ptr@0x{RELOC_PTRS[slot]:05X}")
    off += len(enc)
assert off <= 0x49816 + 105, "relocation overran the padding gap!"

# BUTTON-config action labels: relocate each to full-length English with a LEADING SPACE.
# Why the leading space: the selection cursor covers the first tile of the hovered row, so a
# label whose first glyph is a letter loses it ("EAPON"/"ESET"). The JP originals all had a
# leading space (the cursor hit the space). These full words exceed the 5-glyph in-place
# slots, so relocate them. CRITICAL: the target must be RESIDENT in HWRAM — only the part of
# 0.BIN around the menu block (~<=0x4A1xx) is mapped where the menu pointer table reads; the
# big pad at 0x5BDE0 is NOT resident at BUTTON-screen draw time (labels went blank). Use the
# file-zero pads that sit INSIDE the resident menu block: 0x49758 (92 B) and the tail of the
# 0x49816 gap after STEREO/MONO (0x4983C..0x4987F, 67 B). 武器選択 has 2 pointers (X/Y/Z).
BTN_SPANS = [(0x49758, 0x49758 + 92), (0x4983C, 0x4987F)]   # (start, end) resident zero pads
_si = 0
def btn_alloc(nbytes):
    global _si
    s, e = BTN_SPANS[_si]
    if s + nbytes > e:
        _si += 1
        s, e = BTN_SPANS[_si]
    BTN_SPANS[_si] = (s + nbytes, e)
    return s
# Per-button action labels, now that the right-hand columns are pushed out (below) to give a
# wider label field. Mapping confirmed from the user's button descriptions:
#   A=ショット shoot, B=ジャンプ jump, C=ウエポン special weapon (blue bar),
#   X/Z=武器選択 change special weapon, L=移動選択 skill, R=移動決定 swap/use skill,
#   方向ボタン=d-pad. 武器選択 has 2 pointers (X and Z) -> same "CHG WPN" string.
BTN_RELOC = [
    ([0xA32C],          " WEAPON"),    # ウエポン   (C) special weapon
    ([0xA330, 0xA414],  " CHG WPN"),   # 武器選択   (X/Z) change special weapon (numbered 1/2)
    ([0xA420],          " SKILL"),     # 移動選択   (L) use skill
    ([0xA424],          " CHG SKL"),   # 移動決定   (R) swap/use special skill
    ([0xA428],          " D-PAD"),     # 方向ボタン direction pad
    ([0xA4E4],          " RESET"),     # リセット
]
for ptrs, eng in BTN_RELOC:
    enc = sgtext.encode_wide(eng)
    a = btn_alloc(len(enc))
    buf[a:a + len(enc)] = enc
    for p in ptrs:
        struct.pack_into(">I", buf, p, sgtext.LOAD_BASE + a)
    print(f"  reloc {eng!r:>11} @0.BIN 0x{a:05X} (HWRAM 0x{sgtext.LOAD_BASE + a:08X}); ptr {['0x%X'%p for p in ptrs]}")

# Push the three right-hand columns outward to widen the label field. The button-config render
# code draws each with a HARDCODED x-immediate `mov #N,r4` (SH-2 bytes E4 NN) at a draw-call
# delay slot. Three columns (left->right on screen): button letters (wide map x=16), the row
# numbers 1/2/3 (ascii map x=28), and TYPE 1/2/3 (ascii map x=30). Bump them together so they
# stay ordered and clear the labels. (Ascii tiles are half the width of wide tiles, so TYPE/
# numbers move in ascii units; tune visually.)
BTN_COL_MOVES = [
    # (name, [draw sites], old_x, new_x)
    ("button-letter", [0xA0EE, 0xA212, 0xA244, 0xA29C, 0xA2CE, 0xA30E, 0xA364, 0xA3A4, 0xA3D6], 16, 18),
    ("row-number",    [0xA2DE, 0xA31E, 0xA374], 28, 34),
    ("TYPE-label",    [0xA40C, 0xA448, 0xA45A], 30, 31),
]
for name, sites, old_x, new_x in BTN_COL_MOVES:
    for off in sites:
        assert buf[off] == 0xE4 and buf[off + 1] == old_x, \
            f"expected E4{old_x:02X} (mov #{old_x},r4) at 0x{off:05X}, got {buf[off]:02X}{buf[off+1]:02X}"
        buf[off + 1] = new_x
    print(f"  {name} column x={old_x}->{new_x} at {len(sites)} draw sites")

# ── Title-menu save-record rows: drop the "キロク" (record) label, pull the row left 3 ──
# The 4 save-slot rows are one wide-text buffer (0x060C2A0C) built as
#   [0]space [2]キ [4]ロ [6]ク [8]slot# [10]: [12,14]HH [16]: [18,20]MM [22]: [24,26]SS [28]NUL
# drawn at coarse x=2. Two routines fill the time field into that buffer: FUN_0600e6e0
# (slot has data -> digits) and FUN_0600e754 (empty -> dashes), both at offsets 12..26.
# Shifting the slot#, colons, terminator AND both fill routines left by 6 bytes (3 glyphs)
# overwrites キロク with the shifted content -> rows read " 1:--:--:--" pulled left 3.
# Each edit is a mov.w r0,@(disp,rN) where the low nibble = disp/2; verify before patching.
KIROKU_SHIFT = [
    # (file_off, old_u16, new_u16, note)   prologue buffer build (r14):
    (0x9EAA, 0x81EB, 0x81E8, "colon @22->@16"),
    (0x9EAE, 0x81E8, 0x81E5, "colon @16->@10"),
    (0x9EB2, 0x81E5, 0x81E2, "colon @10->@4"),
    (0x9EBA, 0x81EE, 0x81EB, "NUL  @28->@22"),
    (0x9EE0, 0x81E4, 0x81E1, "slot# @8->@2 (row1)"),
    (0x9F52, 0x81E4, 0x81E1, "slot# @8->@2 (row2)"),
    (0x9F8A, 0x81E4, 0x81E1, "slot# @8->@2 (row3)"),
    (0x9FC2, 0x81E4, 0x81E1, "slot# @8->@2 (row4)"),
    # FUN_0600e6e0 (digit fill, r5):
    (0xA6F8, 0x8156, 0x8153, "HH10 @12->@6"),
    (0xA708, 0x8157, 0x8154, "HH01 @14->@8"),
    (0xA71C, 0x8159, 0x8156, "MM10 @18->@12"),
    (0xA72C, 0x815A, 0x8157, "MM01 @20->@14"),
    (0xA740, 0x815C, 0x8159, "SS10 @24->@18"),
    (0xA752, 0x815D, 0x815A, "SS01 @26->@20"),
    # FUN_0600e754 (dash fill, r4):
    (0xA770, 0x8146, 0x8143, "HH10 @12->@6"),
    (0xA76A, 0x8147, 0x8144, "HH01 @14->@8"),
    (0xA766, 0x8149, 0x8146, "MM10 @18->@12"),
    (0xA762, 0x814A, 0x8147, "MM01 @20->@14"),
    (0xA75E, 0x814C, 0x8149, "SS10 @24->@18"),
    (0xA75A, 0x814D, 0x814A, "SS01 @26->@20"),
]
for off, old, new in [(o, a, b) for o, a, b, _ in KIROKU_SHIFT]:
    cur = struct.unpack_from(">H", buf, off)[0]
    assert cur == old, f"KIROKU_SHIFT: expected 0x{old:04X} at 0x{off:05X}, got 0x{cur:04X}"
    struct.pack_into(">H", buf, off, new)
# HOUR MIN SEC header (fine layer) left 6 tiles (doubled vs the 3-col row shift): x=16->10
assert buf[0x9EC2] == 0xE4 and buf[0x9EC3] == 0x10, "HOUR/MIN/SEC header x immediate moved"
buf[0x9EC3] = 0x0A
print(f"  kiroku rows: dropped label + shifted left 3 ({len(KIROKU_SHIFT)} edits); HOUR/MIN/SEC x=16->10")

# BUTTON-config (controller) action labels: editable menu text in 0.BIN (the ONDATA font
# DMAs into VDP2 VRAM base 0 with char#==glyph, so the menu wide-text routine draws them;
# code = ord*4 big-endian, 0x0000 terminator). Confirmed via slot-0 savestate forensics:
# 0x49D60=ジャンプ, 0x49D6C=ウエポン; ショット sits inside 0x49D4A after 5 icon glyphs, so its
# kana live at 0x49D56. ≤5 glyphs/slot. The 4 kanji labels (0x49D78/84/90/9C) + bottom bar
# (0x49DB4) were read from zoomed screenshots:
#   武器選択 weapon-select (X/Y/Z), 移動選択 move-select (L), 移動決定 move-confirm (R),
#   方向ボタン direction-pad, スタートボタン＝決定 = the bottom bar.
# Edited IN PLACE (not relocated) so all pointers update at once (武器選択 has 2) and the
# English stays within the JP label width — no overflow into the button-letter column.
# Each slot is 6 words (5 glyphs + NUL); writing 5 chars overwrites space+4kana, NUL kept.
def enc_menu(s):
    return b"".join(struct.pack(">H", (ord(c) & 0x7F) * 4) for c in s)
BTNCFG = {
    0x49D56: ("SHOT", False),    # ショット  (4 glyphs in place after the icon+space; cursor
                                 #            covers the icon at the slot start, so SHOT is safe)
    0x49D60: (" JUMP", True),    # ジャンプ  (leading space so the cursor covers the space)
    # ウエポン/武器選択/移動選択/移動決定/方向ボタン/リセット are all relocated above with a leading
    # space (full words W.SELECT etc. exceed the 5-glyph in-place slots).
    0x49DB4: ("START = OK", False),  # スタートボタン＝決定  bottom info bar (not a cursor row)
}
for boff, (eng, term) in BTNCFG.items():
    enc = enc_menu(eng) + (b"\x00\x00" if term else b"")
    buf[boff:boff + len(enc)] = enc
    print(f"  btncfg 0x{boff:05X} -> {eng!r}")

# Dialogue (Mina cutscene box). Each pattern-name word = code | (color<<16); `color`
# is the per-call palette, so line 1 (0x5B87E) is drawn WHITE and line 2 (0x5B88E)
# YELLOW by the box routine — translating the white line yields white English for free.
# items: str -> Latin glyphs (code=ord*4); int -> explicit dialogue-font glyph index
# (471=！, 470=space-fill). Fixed width: exactly ncodes codes, delimiters preserved.
def enc_codes(items, ncodes):
    out = []
    for it in items:
        if isinstance(it, str):
            out += [(ord(c) & 0x7F) * 4 for c in it]
        else:
            out.append(it * 4)
    out = (out + [470 * 4] * ncodes)[:ncodes]   # 470 = space-fill glyph
    return b"".join(struct.pack(">H", c) for c in out)

# dg(): native dialogue-FONT Latin. CONFIRMED dialogue glyph = ASCII + 438 by reading
# the game's own "ROLL" tilemap (R,O,L,L = glyphs 520,517,514,514). Returns explicit
# glyph indices so the chunky white dialogue font is used (not the thin title font 65-90).
# ' ' -> 470 (space) and '!' -> 471 fall out of +438 automatically.
def dg(s):
    return [ord(c) + 438 for c in s]

# Mina box (two lines, both white). Original: [7 codes ミーナちゃん！][FFFF][17 codes][0000]
# in region 0x5B87E..0x5B8B2. The FFFF line-break is just data, so repartition: line 1 gets
# the long line, line 2 the short, line break moved accordingly, 0000-terminate, zero-pad rest.
def write_dlg_box(start, end, line1, line2):
    words = [(ord(c) + 438) * 4 for c in line1] + [0xFFFF] \
          + [(ord(c) + 438) * 4 for c in line2] + [0x0000]
    span = (end - start) // 2
    assert len(words) <= span, f"box overflow: {len(words)} > {span} codes"
    words += [0x0000] * (span - len(words))
    for i, w in enumerate(words):
        struct.pack_into(">H", buf, start + i * 2, w)
    print(f"Mina box 0x{start:05X}: {line1!r} / {line2!r} ({len(words)} words)")
write_dlg_box(0x5B87E, 0x5B8B2, "I'LL SAVE YOU", "MINA!")

# Startup "black screen" backup-RAM boot dialog (white dialogue font). Three messages:
#  - selection screen (-0017): 現在…どちらをバックアップに…？ + C/A button choices
#  - console confirm  (-0018): 本体RAMをきろくのバックアップに、つかいます。
#  - cartridge confirm(-0016): カートリッジRAMをきろくのバックアップに、つかいます。
# Each FFFF-delimited run is one display line; translate in place, exact code count.
# Safe charset only (A-Z + space) since ASCII punctuation glyphs are unverified.
# fixed=N forces width (for 0x5B3F0, the カートリッジ tail of the 0x5B3D8 entry — the
# first 12 codes of that entry belong to another message and stay untouched); fixed=None
# walks to the next delimiter.
BOOT = [
    # selection screen (-0017): A button = cartridge, C button = system
    (0x5B6EC, None, "BACKUP MEMORY CART"),
    (0x5B712, None, "IS INSERTED."),
    (0x5B732, None, "WOULD YOU LIKE TO"),
    (0x5B758, None, "SAVE TO SYSTEM OR"),
    # 0x5B77E "CARTRIDGE?" is handled by the line-break fix below (it gets shifted)
    (0x5B798, None, "  A CARTRIDGE"),
    (0x5B7BA, None, "  C SYSTEM"),
    # NOTE: the two confirm screens (console 0x5B7DE / cartridge 0x5B812) are relocated
    # below (they need more room than their in-place slots) — not edited in place here.
]
for boff, fixed, eng in BOOT:
    if fixed is not None:
        n = fixed
    else:
        n = 0
        while struct.unpack_from(">H", buf, boff + n * 2)[0] not in (0x0000, 0xFFFF, 0xFFFE):
            n += 1
    assert len(eng) <= n, f"0x{boff:05X}: {eng!r} ({len(eng)}) > {n} codes"
    buf[boff:boff + n * 2] = sgtext.encode_wide_fixed(eng, n, font_base=438)
print(f"boot dialog: {len(BOOT)} runs translated")

# Line breaks (FFFF=newline, FFFF FFFF=blank line). Originally there is a blank line
# BETWEEN "...OR" and "CARTRIDGE?" and only a single break after it. Move that blank to
# AFTER "CARTRIDGE?" so the question reads "...SYSTEM OR / CARTRIDGE?" then a gap before
# the A/C choices. Same byte count: shift CARTRIDGE? 2 bytes earlier.
#   was: [FFFF][FFFF][CARTRIDGE? 24B][FFFF]   ->   [FFFF][CARTRIDGE? 24B][FFFF][FFFF]
seg = (struct.pack(">H", 0xFFFF)
       + sgtext.encode_wide_fixed("CARTRIDGE?", 12, font_base=438)
       + struct.pack(">HH", 0xFFFF, 0xFFFF))
assert len(seg) == 30
buf[0x5B77A:0x5B77A + len(seg)] = seg
print("selection screen: moved blank line to after CARTRIDGE?")

# Backup-confirm screens (console 0x5B7DE, cartridge 0x5B812). Each is a 0x0000-terminated
# message (0xFFFF = newline) read from its pointer-table entry. Rewrite IN PLACE with
# rebalanced line breaks so the new wording fits the original slot — no relocation, the
# original pointers stay valid.
def boot_msg(lines):
    out = b""
    for i, ln in enumerate(lines):
        if i:
            out += struct.pack(">H", 0xFFFF)
        out += sgtext.encode_wide_fixed(ln, len(ln), font_base=438)
    return out + struct.pack(">H", 0x0000)

def write_msg_inplace(start, end, lines):
    m = boot_msg(lines)
    assert start + len(m) <= end, f"msg ({len(m)}B) too long for slot 0x{start:05X}-0x{end:05X}"
    buf[start:end] = m + b"\x00" * (end - start - len(m))
    print(f"  confirm @0.BIN 0x{start:05X} <- {' / '.join(lines)}")

write_msg_inplace(0x5B7DE, 0x5B812, ["SAVING GAME TO", "SYSTEM."])     # console
write_msg_inplace(0x5B812, 0x5B84E, ["SAVING GAME TO", "CARTRIDGE."])  # cartridge
assert len(buf) == len(data), "size changed!"
os.makedirs(os.path.dirname(out0), exist_ok=True)
open(out0, "wb").write(buf)

# report what changed
diffs = [i for i in range(len(data)) if data[i] != buf[i]]
print(f"wrote {out0}: {len(diffs)} bytes changed across "
      f"{len(set(d // 0x800 for d in diffs))} 0.BIN regions")
for off, txt in sorted([(0x45690, "PRESS START BUTTON")] + list(MENU.items())):
    print(f"  0x{off:05X} -> {txt!r}")
