# -*- coding: utf-8 -*-
"""EN->JP command shim: TWO-STAGE SH-2 routine + assembler + simulator (validation).

Signature = TRANSCODE (r4=dest, r5=src ASCII, r6=mode).
  Stage 1 (phrase): if the whole src command matches a PHRASE entry's english (case-insensitive,
    optional single trailing space), copy that entry's JP and rts. Handles KEYBOARD SVO commands
    ("open mailbox" -> 郵便箱を開ける, "look" -> 見る) that don't translate word-by-word.
  Stage 2 (word-by-word): otherwise split src on spaces and translate each word via the WORD table
    ([en\\0][jp\\0]...0x00), appending each JP with no spaces. Handles the noun-PICKER's SOV commands
    ("mailbox in open" -> 郵便箱を開ける) since picker order already IS Japanese order.
  Fallback: if a word is unknown, restore orig r4/r5 and tail-jump to the real TRANSCODE (0x0604608a).

assemble(phrase_addr, word_addr, transcode_addr) -> bytes (routine + [phrase][word][tc] literals).
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(__file__))

TRANSCODE_ADDR = 0x0604608a

# ---- Stage-1 phrase table: whole English command (SVO/keyboard) -> Japanese ----
PHRASES = [
    ("look",         "見る"),
    ("open mailbox", "郵便箱を開ける"),
]

# ---- Stage-2 word table: single English word -> Japanese (noun+particle+verb, picker) ----
# common action-menu verbs, placed early so the gap-size truncation never drops them
COMMON_VERBS = ["open", "close", "take", "get", "drop", "put", "examine", "look", "read", "move",
                "push", "pull", "turn", "enter", "exit", "attack", "kill", "eat", "drink", "throw",
                "light", "unlock", "lock", "climb", "wave", "touch", "ring", "burn", "pour", "tie",
                "cut", "dig", "board", "fill", "give", "raise", "lower", "wear", "remove", "kick",
                "knock", "smell", "count", "pray", "wind", "squeeze", "shake", "search", "drink"]

def _load_words():
    try:
        from zork_data.picker_en2jp import NOUNS, VERBS, PARTICLES, DIRECTIONS, MOVEMENT_VERBS, MOVE_VERB
    except Exception:
        return [("mailbox", "郵便箱"), ("in", "を"), ("and", "を"), ("open", "開ける")]
    out = []; seen = set()
    def add(en, jp, allow_empty=False):
        e = (en or "").lower()
        if e and (jp or allow_empty) and e not in seen and all(ord(c) < 128 for c in e):
            out.append((e, jp)); seen.add(e)
    # Particle is OPTIONAL: particles carry NO reading (empty), and every noun BAKES IN its object
    # particle を. So "mailbox open" == "mailbox in open" == any particle -> all give 郵便箱を開ける.
    for en in PARTICLES: add(en, "", allow_empty=True)          # particles -> empty (ignored)
    # Directions + movement-verb elision (added EARLY so they survive truncation AND pre-empt the
    # 'go'/'walk' entries in VERBS via the seen-set). "go west" -> ""+にし = にし.
    for en in MOVEMENT_VERBS: add(en, "", allow_empty=True)     # movement verb -> empty (elided)
    for en, jp in DIRECTIONS.items(): add(en, jp + MOVE_VERB)   # direction + 行く (bare dir = "no verb")
    for v in COMMON_VERBS:                                       # common verbs (survive truncation)
        if v in VERBS: add(v, VERBS[v])
    for en, jp in sorted(NOUNS.items()): add(en, jp + "を")      # noun carries its を
    for en, jp in sorted(VERBS.items()): add(en, jp)             # remaining/rare verbs last (may truncate)
    return out

WORDS = _load_words()

def build_table(pairs):
    out = bytearray()
    for en, jp in pairs:
        out += en.encode("ascii") + b"\x00" + jp.encode("shift_jis") + b"\x00"
    out += b"\x00"                      # end marker (empty english)
    return bytes(out)

# ---- label-based SH-2 assembler ----
def _rn(s): return int(s[1:])

def assemble(phrase_addr, word_addr, transcode_addr):
    P = [
      # ===== Stage 1: whole-command phrase match (keyboard SVO) =====
      ('movl_pc', 'r3', 'TBL1'),
      ('L', 'p_next'),
      ('movb_at', 'r0', 'r3'), ('tst', 'r0', 'r0'), ('bt', 'wbw'),   # phrase table end -> word-by-word
      ('mov', 'r1', 'r5'), ('mov', 'r2', 'r3'),
      ('L', 'p_cmp'),
      ('movb_at', 'r7', 'r2'), ('tst', 'r7', 'r7'), ('bt', 'p_end'),
      ('movb_at', 'r0', 'r1'), ('or20', 'r0'), ('cmpeq', 'r0', 'r7'), ('bf', 'p_mis'),
      ('addi', 'r1', 1), ('bra', 'p_cmp'), ('addi', 'r2', 1),
      ('L', 'p_end'),
      ('movb_at', 'r0', 'r1'), ('tst', 'r0', 'r0'), ('bt', 'p_do'),
      ('cmpeqi', 'r0', 0x20), ('bf', 'p_mis'),
      ('L', 'p_do'), ('addi', 'r2', 1),
      ('L', 'p_cpy'),
      ('movb_at', 'r0', 'r2'), ('movb_st', 'r0', 'r4'), ('tst', 'r0', 'r0'), ('bt', 'p_done'),
      ('addi', 'r4', 1), ('bra', 'p_cpy'), ('addi', 'r2', 1),
      ('L', 'p_done'), ('rts',), ('nop',),
      ('L', 'p_mis'),
      ('L', 'p_skA'), ('movb_at', 'r0', 'r3'), ('addi', 'r3', 1), ('tst', 'r0', 'r0'), ('bf', 'p_skA'),
      ('L', 'p_skB'), ('movb_at', 'r0', 'r3'), ('addi', 'r3', 1), ('tst', 'r0', 'r0'), ('bf', 'p_skB'),
      ('bra', 'p_next'), ('nop',),
      # ===== Stage 2: word-by-word (picker SOV) =====
      ('L', 'wbw'),
      ('push', 'r5'), ('push', 'r4'),            # save orig src, dest (r15:[dest][src])
      ('L', 'outer'),
      ('movb_at', 'r0', 'r5'), ('tst', 'r0', 'r0'), ('bt', 'wdone'),   # src end -> success
      ('movl_pc', 'r3', 'TBL2'),
      ('L', 'entry'),
      ('movb_at', 'r0', 'r3'), ('tst', 'r0', 'r0'), ('bt', 'fback'),   # word table end -> fallback
      ('mov', 'r1', 'r5'), ('mov', 'r2', 'r3'),
      ('L', 'wcmp'),
      ('movb_at', 'r7', 'r2'), ('tst', 'r7', 'r7'), ('bt', 'bound'),
      ('movb_at', 'r0', 'r1'), ('or20', 'r0'), ('cmpeq', 'r0', 'r7'), ('bf', 'nextp'),
      ('addi', 'r1', 1), ('bra', 'wcmp'), ('addi', 'r2', 1),
      ('L', 'bound'),
      ('movb_at', 'r0', 'r1'), ('tst', 'r0', 'r0'), ('bt', 'match'),
      ('cmpeqi', 'r0', 0x20), ('bt', 'match'),
      ('L', 'nextp'),
      ('L', 'skA'), ('movb_at', 'r0', 'r3'), ('addi', 'r3', 1), ('tst', 'r0', 'r0'), ('bf', 'skA'),
      ('L', 'skB'), ('movb_at', 'r0', 'r3'), ('addi', 'r3', 1), ('tst', 'r0', 'r0'), ('bf', 'skB'),
      ('bra', 'entry'), ('nop',),
      ('L', 'match'),
      ('mov', 'r2', 'r3'),
      ('L', 'skC'), ('movb_at', 'r0', 'r2'), ('addi', 'r2', 1), ('tst', 'r0', 'r0'), ('bf', 'skC'),
      ('L', 'cpj'),
      ('movb_at', 'r0', 'r2'), ('tst', 'r0', 'r0'), ('bt', 'cpjd'),
      ('movb_st', 'r0', 'r4'), ('addi', 'r4', 1), ('bra', 'cpj'), ('addi', 'r2', 1),
      ('L', 'cpjd'),
      ('mov', 'r5', 'r1'),
      ('L', 'cpjsp'),                                        # skip ALL trailing spaces, not just one
      ('movb_at', 'r0', 'r5'), ('cmpeqi', 'r0', 0x20), ('bf', 'outer'),   # non-space (letter/NUL) -> outer
      ('addi', 'r5', 1), ('bra', 'cpjsp'), ('nop',),        # space -> skip and re-check (menu pads "GO NE   ")
      ('L', 'wdone'),
      ('movi', 'r0', 0), ('movb_st', 'r0', 'r4'),
      ('addi', 'r15', 8), ('rts',), ('nop',),
      ('L', 'fback'),
      ('pop', 'r4'), ('pop', 'r5'),
      ('movl_pc', 'r0', 'TC'), ('jmp', 'r0'), ('nop',),
    ]
    labels = {}; off = 0
    for ins in P:
        if ins[0] == 'L': labels[ins[1]] = off; continue
        off += 2
    code_len = off; lit_off = (code_len + 3) & ~3
    lit = {'TBL1': lit_off, 'TBL2': lit_off + 4, 'TC': lit_off + 8}
    words = []; off = 0
    for ins in P:
        op = ins[0]
        if op == 'L': continue
        pc = off
        if op == 'movl_pc':
            n = _rn(ins[1]); la = lit[ins[2]]; disp = (la - ((pc + 4) & ~3)) // 4
            assert 0 <= disp < 256, (ins, disp); words.append(0xD000 | (n << 8) | disp)
        elif op == 'movb_at': words.append(0x6000 | (_rn(ins[1]) << 8) | (_rn(ins[2]) << 4))
        elif op == 'movb_st': words.append(0x2000 | (_rn(ins[2]) << 8) | (_rn(ins[1]) << 4))
        elif op == 'mov':     words.append(0x6003 | (_rn(ins[1]) << 8) | (_rn(ins[2]) << 4))
        elif op == 'movi':    words.append(0xE000 | (_rn(ins[1]) << 8) | (ins[2] & 0xff))
        elif op == 'or20':    words.append(0xCB00 | 0x20)
        elif op == 'tst':     words.append(0x2008 | (_rn(ins[1]) << 8) | (_rn(ins[2]) << 4))
        elif op == 'cmpeq':   words.append(0x3000 | (_rn(ins[1]) << 8) | (_rn(ins[2]) << 4))
        elif op == 'cmpeqi':  words.append(0x8800 | (ins[2] & 0xff))
        elif op == 'addi':    words.append(0x7000 | (_rn(ins[1]) << 8) | (ins[2] & 0xff))
        elif op == 'push':    words.append(0x2006 | (15 << 8) | (_rn(ins[1]) << 4))
        elif op == 'pop':     words.append(0x6006 | (_rn(ins[1]) << 8) | (15 << 4))
        elif op in ('bt', 'bf'):
            disp = (labels[ins[1]] - (pc + 4)) // 2; assert -128 <= disp < 128, (ins, disp)
            words.append((0x8900 if op == 'bt' else 0x8B00) | (disp & 0xff))
        elif op == 'bra':
            disp = (labels[ins[1]] - (pc + 4)) // 2; assert -2048 <= disp < 2048, (ins, disp)
            words.append(0xA000 | (disp & 0xFFF))
        elif op == 'jmp':     words.append(0x402B | (_rn(ins[1]) << 8))
        elif op == 'rts':     words.append(0x000B)
        elif op == 'nop':     words.append(0x0009)
        else: raise ValueError(op)
        off += 2
    b = b"".join(struct.pack(">H", w) for w in words)
    b += b"\x00" * (lit_off - code_len)
    b += struct.pack(">I", phrase_addr) + struct.pack(">I", word_addr) + struct.pack(">I", transcode_addr)
    return b

# ---- simulator ----
def simulate(routine, rbase, ptable, ptbase, wtable, wtbase, src, tc):
    mem = {}
    for i, by in enumerate(routine): mem[rbase + i] = by
    for i, by in enumerate(ptable):  mem[ptbase + i] = by
    for i, by in enumerate(wtable):  mem[wtbase + i] = by
    SRC = 0x00300000; DST = 0x00310000; SP = 0x0020FF00
    for i, by in enumerate(src + b"\x00"): mem[SRC + i] = by
    R = [0] * 16; R[4] = DST; R[5] = SRC; R[6] = 0; R[15] = SP; T = [0]
    def rb(a):
        v = mem.get(a, 0); return v - 256 if v & 0x80 else v
    def rl(a): return (mem.get(a, 0) << 24) | (mem.get(a + 1, 0) << 16) | (mem.get(a + 2, 0) << 8) | mem.get(a + 3, 0)
    def wl(a, v):
        for k in range(4): mem[a + 3 - k] = (v >> (8 * k)) & 0xff
    def read_dst():
        o = DST; s = bytearray()
        while mem.get(o, 0) != 0: s.append(mem[o]); o += 1
        return bytes(s)
    pc = rbase; steps = 0
    def ex(w, pcx):
        top = w >> 12
        if w == 0x0009: return
        if top == 0x6 and (w & 0xF) == 0x0: R[(w >> 8) & 0xF] = rb(R[(w >> 4) & 0xF]) & 0xffffffff; return
        if top == 0x2 and (w & 0xF) == 0x0: mem[R[(w >> 8) & 0xF] & 0xffffffff] = R[(w >> 4) & 0xF] & 0xff; return
        if top == 0x6 and (w & 0xF) == 0x3: R[(w >> 8) & 0xF] = R[(w >> 4) & 0xF]; return
        if top == 0x6 and (w & 0xF) == 0x6: n = (w >> 8) & 0xF; R[n] = rl(R[15]); R[15] = (R[15] + 4) & 0xffffffff; return
        if top == 0x2 and (w & 0xF) == 0x6: R[15] = (R[15] - 4) & 0xffffffff; wl(R[15], R[(w >> 4) & 0xF] & 0xffffffff); return
        if top == 0xE: v = w & 0xff; R[(w >> 8) & 0xF] = v - 256 if v & 0x80 else v; return
        if (w & 0xFF00) == 0xCB00: R[0] = R[0] | (w & 0xff); return
        if top == 0x2 and (w & 0xF) == 0x8: T[0] = 1 if (R[(w >> 8) & 0xF] & R[(w >> 4) & 0xF] & 0xffffffff) == 0 else 0; return
        if top == 0x3 and (w & 0xF) == 0x0: T[0] = 1 if (R[(w >> 8) & 0xF] & 0xffffffff) == (R[(w >> 4) & 0xF] & 0xffffffff) else 0; return
        if (w & 0xFF00) == 0x8800: i = w & 0xff; v = i - 256 if i & 0x80 else i; T[0] = 1 if (R[0] == v or R[0] == (v & 0xffffffff)) else 0; return
        if top == 0x7: i = w & 0xff; R[(w >> 8) & 0xF] = (R[(w >> 8) & 0xF] + (i - 256 if i & 0x80 else i)) & 0xffffffff; return
        if top == 0xD: n = (w >> 8) & 0xF; d = w & 0xff; R[n] = rl(((pcx + 4) & ~3) + d * 4); return
        raise ValueError("unhandled 0x%04x" % w)
    while steps < 50000:
        steps += 1; pcx = pc; w = (mem.get(pc, 0) << 8) | mem.get(pc + 1, 0); top = w >> 12; npc = pc + 2
        if w == 0x000B or (top == 0x4 and (w & 0xFF) == 0x2B) or top == 0xA:
            dw = (mem.get(pc + 2, 0) << 8) | mem.get(pc + 3, 0); ex(dw, pc + 2)
            if w == 0x000B: return read_dst(), None
            if top == 0x4:
                tgt = R[(w >> 8) & 0xF] & 0xffffffff
                return read_dst(), ("TC_FALLBACK" if tgt == tc else "jmp 0x%08x" % tgt)
            d = w & 0xFFF; d = d - 0x1000 if d & 0x800 else d; pc = pc + 4 + d * 2; continue
        if (w & 0xFF00) == 0x8900:
            if T[0]: d = w & 0xff; pc = pc + 4 + ((d - 256 if d & 0x80 else d) * 2); continue
            pc = npc; continue
        if (w & 0xFF00) == 0x8B00:
            if not T[0]: d = w & 0xff; pc = pc + 4 + ((d - 256 if d & 0x80 else d) * 2); continue
            pc = npc; continue
        ex(w, pcx); pc = npc
    return read_dst(), "TIMEOUT"


if __name__ == "__main__":
    PB = 0x06072000; WB = 0x06073000; RB = 0x06071000
    ptbl = build_table(PHRASES); wtbl = build_table(WORDS)
    code = assemble(PB, WB, TRANSCODE_ADDR)
    print("routine=%dB  phrase-table=%dB  word-table=%dB  (%d words)" % (len(code), len(ptbl), len(wtbl), len(WORDS)))
    cases = [
        (b"look",             "見る"),          # keyboard SVO (phrase)
        (b"look ",            "見る"),
        (b"open mailbox",     "郵便箱を開ける"),   # keyboard SVO (phrase)
        (b"open mailbox ",    "郵便箱を開ける"),
        (b"mailbox in open ", "郵便箱を開ける"),   # picker SOV, with particle
        (b"MAILBOX in open ", "郵便箱を開ける"),
        (b"mailbox open ",    "郵便箱を開ける"),   # NO particle (baked-in を) -> same result
        (b"leaflet take ",    "ぱんふれっとを取る"),  # leaflet binds ぱんふれっと(0x01f9), not 説明書
        (b"leaflet and open", "ぱんふれっとを開ける"),
        (b"go west",          "にし行く"),        # navigation: verb baked into direction (SOV)
        (b"go west ",         "にし行く"),
        (b"west",             "にし行く"),        # bare direction still gets the verb
        (b"go north",         "きた行く"),
        (b"go south ",        "みなみ行く"),
        (b"go east",          "ひがし行く"),
        (b"n",                "きた行く"),        # short form
        (b"up",               "うえ行く"),
        (b"ne ",              "ほくとう行く"),
        (b"GO NE   ",         "ほくとう行く"),     # exact 8-byte menu string (trailing spaces)
        (b"xyzzy",            None),            # unknown -> fallback
        (b"mailbox and frobnicate", None),
    ]
    okall = True
    for src, exp in cases:
        out, note = simulate(code, RB, ptbl, PB, wtbl, WB, src, TRANSCODE_ADDR)
        got = out.decode("shift_jis", "replace") if out else ""
        ok = (note == "TC_FALLBACK") if exp is None else (got == exp and note is None)
        okall &= ok
        print("  %-24r -> %-10s note=%-12s %s" % (src.decode(), got, note, "OK" if ok else "*** FAIL exp=%r" % exp))
    print("ALL OK" if okall else "*** FAILURES")
