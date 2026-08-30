#!/usr/bin/env python3
"""Zork I (Saturn JP) — ENGFLAG full English prose build (lowercase + period).

engflag mode (lang flag 0x060af984 forced on) already gives English MENUS + English PARSER natively,
so this build does NOT need the flag=0 Option-C noun/verb/menu machinery. It only translates the
PROSE that the engflag +0x1F renderer draws: rooms98, msg777, dict234 (referenced by 0x0e tokens),
and loose code-pointer strings.

Encoder for the engflag room-prose path (RE-confirmed): renderer draws glyph = font[(byte+0x1F)] off
the ASCII font, upper-casing lowercase input first. Reliable inputs:
  * letters -> stored (lower-0x1F) in 0x42-0x5B -> renders lowercase a-z  (escapes the toUpper)
  * space -> 0x20 ; newline -> 0x0c ; terminator 0x00
  * '.' -> input 0x41 ('A' letter byte -> clean +0x1F -> glyph slot 0x60, which we PAINT with '.')
Only slot 0x60 is a free, letter-reachable glyph, so PERIOD is the one supported symbol; other
punctuation is not renderable on this path and is dropped (word spacing preserved). Object/room
TOKENS (0x0e idx / 0x1e idx) are preserved verbatim so the engine expands them.

Reuses zork_data.{dict_words,rooms,messages,loose}. Applies engflag flag-flip + font mirror-fix
(bit-reverse INIT2/SINIT2) + paints '.' into glyph slot 0x60. Output: game_patched/zork1_engflag_full/
"""
import os, shutil, struct, math, sys
HERE = os.path.dirname(__file__)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
from saturn_translate import ecc
import zork_cgz as z
import zork_shim
from zork_data.dict_words import DICT
from zork_data.rooms import ROOMS
from zork_data.messages import MSGS
from zork_data.loose import LOOSE
from zork_data.responses import RESPONSES
from zork_zvoctbl_patch import READING_EN     # JP reading -> English noun name

ROOT = os.path.join(HERE, "..")
ZDIR = os.path.join(ROOT, "cd", "Zork I - The Great Underground Empire (Japan)")
SRC_T1 = os.path.join(ZDIR, "Zork I - The Great Underground Empire (Japan) (Track 01).bin")
OUTDIR = os.path.join(ROOT, "game_patched", "zork1_engflag_full")
OUT_T1 = os.path.join(OUTDIR, "Zork1 (engflag full) (Track 01).bin")
OUT_CUE = os.path.join(OUTDIR, "Zork1 (engflag full).cue")
SS, DO, USER, ROOTDIR_LBA = 2352, 16, 2048, 20

BASE = 0x06004000
TBL = {"rooms98": (0x8b074, 98), "dict234": (0x95a68, 234), "msg777": (0x99be8, 777)}
POOL, POOL_SIZE = 0x64f34, 0xD000
POOL_END = POOL + POOL_SIZE
LINE_W = 16
ENGFLAG = {0x5f16: (0xE000, 0xE001), 0x7798: (0xE000, 0xE001)}
FONT_BYTES = 0x1000
GLYPH = 16
PERIOD_SLOT, PERIOD_IN = 0x60, 0x41   # paint '.' into glyph 0x60; reached by input 0x41
BITREV = [int("{:08b}".format(i)[::-1], 2) for i in range(256)]
# Phase 1 (noun-select crash fix): run1 noun table (JP, 364 entries, addr 0x0607c9bc) and the
# flag!=0 command-path literal @0x0601fbc4 (=run2 0x0607cf6c, 296 entries). engflag indexes the
# noun table with a run1-space index (0..363); run2 has only 296 -> OOB crash. Repoint @0x0601fbc4
# to a 364-entry English table index-parallel to run1 so the index is always in range.
RUN1_FOFF, N1 = 0x789bc, 364
CMD_LIT_RUN2 = 0x1bbc4                 # file offset of literal @0x0601fbc4
# Phase 2 PoC shim: the parse-site TRANSCODE literal @0x0604550c (file 0x4550c) = 0x0604608a. Repoint
# it to a pool routine that rewrites the typed ASCII command -> JP reading (SJIS) so the JP tokenizer
# accepts it. Signature matches TRANSCODE (r4=dest 0x060b2dc8, r5=src 0x060a5a9c ASCII). PoC: LOOK->見る,
# else tail-jump to the real TRANSCODE (0x0604608a). Verified by disassembly.
TRANSCODE_ADDR = 0x0604608a
SHIM_LIT_FOFF = 0x4150c                 # 0x0604550c - BASE (parse-site TRANSCODE literal)


# ---- engflag encoder -------------------------------------------------------
def eg_bytes(ch):
    """One source character -> engflag stored bytes (may be empty for dropped punctuation)."""
    if ch == ".":            return bytes([PERIOD_IN])
    if ch.isalpha():         return bytes([(ord(ch.lower()) - 0x1F) & 0xFF])
    if ch.isdigit():         return b""            # digits unsupported on this path -> drop
    return b""                                     # other punctuation unsupported -> drop


def enc(s):
    out = bytearray()
    for ch in s:
        if ch == " ":    out.append(0x20)
        elif ch == "\n": out.append(0x0c)
        else:            out += eg_bytes(ch)
    out.append(0x00)
    return bytes(out)


def enc_fw(s):
    """Full-width SJIS (2 bytes/char), NUL-terminated — how the command box renders (JP font path)."""
    out = bytearray()
    for c in s.upper():
        o = ord(c)
        if c == " ":            out += b"\x81\x40"
        elif "0" <= c <= "9":   out += bytes((0x82, 0x4f + o - 0x30))
        elif "A" <= c <= "Z":   out += bytes((0x82, 0x60 + o - 0x41))
        else:                   out += b"\x81\x40"
    out.append(0x00)
    return bytes(out)


def wrap_segments(segs, term=0x00):
    """Word-wrap a segment list (str | int-object-token | ('r',idx)-room-token | ('ctrl',b)).
    Keeps words and tokens unsplit across the 16-cell line, like the flag=0 pipeline, but emits
    engflag bytes for literals. Object/room tokens are emitted verbatim (0x0e/0x1e)."""
    seq = []; cur = []
    def flush():
        if cur: seq.append(("w", list(cur))); cur.clear()
    for part in segs:
        if isinstance(part, int):
            cur.append(("t", part))
        elif isinstance(part, tuple) and part[0] == "r":
            cur.append(("rt", part[1]))
        elif isinstance(part, tuple) and part[0] == "ctrl":
            flush(); seq.append(("raw", part[1]))
        else:
            for ch in part:
                if ch == " ":    flush(); seq.append(("sp",))
                elif ch == "\n": flush(); seq.append(("br",))
                else:            cur.append(("c", ch))
    flush()
    def wlen(units):
        return sum(1 if u[0] == "c" else len(DICT.get(u[1], "X")) for u in units)
    out = bytearray(); col = 0; pending_sp = False
    for it in seq:
        if it[0] == "sp":   pending_sp = True
        elif it[0] == "br": out.append(0x0c); col = 0; pending_sp = False
        elif it[0] == "raw": out.append(it[1]); col = 0; pending_sp = False
        else:
            L = wlen(it[1])
            if col > 0:
                if col + (1 if pending_sp else 0) + L > LINE_W:
                    out.append(0x0c); col = 0
                elif pending_sp:
                    out.append(0x20); col += 1
            pending_sp = False
            for u in it[1]:
                if u[0] == "c":    out += eg_bytes(u[1])
                elif u[0] == "rt": out += bytes([0x1e, u[1]])
                else:              out += bytes([0x0e, u[1]])
            col = col + L      # absolute (NOT % LINE_W): a word ending exactly at LINE_W must leave
                               # col==LINE_W so the next word triggers a real 0x0c break, not a silent
                               # merge ("standing"+"in" -> "standingin") from col wrapping to 0
    out.append(term)
    return bytes(out)


def _entries():
    for ix, txt in DICT.items():  yield ("dict234", ix, txt)
    for ix, txt in ROOMS.items(): yield ("rooms98", ix, txt)
    for ix, txt in MSGS.items():  yield ("msg777", ix, txt)


# ---- tiny SH-2 assembler (for the menu-lag trampoline; resolves pc-rel mov.l + bt/bf) ----
class _Sh2Asm:
    def __init__(self, base):
        self.base = base; self.items = []
        self.pool_order = []; self.pool_vals = {}; self.labels = {}
    def emit(self, *items):
        for it in items:
            self.items.append(('raw', it) if isinstance(it, int) else it)
    def pool(self, *pairs):
        for i in range(0, len(pairs), 2):
            self.pool_order.append(pairs[i]); self.pool_vals[pairs[i]] = pairs[i + 1]
    def assemble(self):
        off = 0; code = []
        for it in self.items:
            if it[0] == 'label':
                self.labels[it[1]] = off
            else:
                code.append((it, off)); off += 2
        pool_start = off + (-(self.base + off)) % 4   # align pool to an absolute 4-byte boundary
                                                       # (pc-rel mov.l reads from (PC&~3)); works for any base
        pool_off = {n: pool_start + i * 4 for i, n in enumerate(self.pool_order)}
        buf = bytearray(pool_start + len(self.pool_order) * 4)
        for it, o in code:
            addr = self.base + o
            if it[0] == 'raw':
                w = it[1]
            elif it[0] == 'movl':
                _, name, reg = it
                pa = self.base + pool_off[name]; d = pa - ((addr & ~3) + 4)
                assert d >= 0 and d % 4 == 0 and d // 4 <= 255, "movl disp oob %s" % name
                w = 0xD000 | (reg << 8) | (d // 4)
            elif it[0] in ('bt', 'bf'):
                ta = self.base + self.labels[it[1]]; d = ta - (addr + 4)
                assert d % 2 == 0 and -128 <= d // 2 <= 127, "branch disp oob %s" % it[1]
                w = (0x8900 if it[0] == 'bt' else 0x8B00) | ((d // 2) & 0xFF)
            else:
                raise ValueError("bad item %r" % (it,))
            buf[o] = w >> 8; buf[o + 1] = w & 0xFF
        for n in self.pool_order:
            struct.pack_into(">I", buf, pool_off[n], self.pool_vals[n])
        return bytes(buf)


# ---- 0ZORK patcher ---------------------------------------------------------
def build_patched_0zork(orig, verbose=False):
    a = bytearray(orig)
    for foff, (want, new) in ENGFLAG.items():
        assert (a[foff] << 8) | a[foff + 1] == want, "engflag site 0x%x mismatch" % foff
        a[foff] = new >> 8; a[foff + 1] = new & 0xFF
    # DECISIVE BISECTION: engflag-only. Force the language flag and skip EVERY other patch. Display will be
    # garbled (English +0x1F renderer on untranslated JP text) -- ignore that; only test whether the direction
    # MENU moves non-north. Moves -> engflag=1 is fine, a data patch breaks it. Fails -> engflag=1 itself
    # activates a broken English direction path (find/reroute that engflag fork).
    _ENGFLAG_ONLY = False
    if _ENGFLAG_ONLY:
        if verbose:
            print("  ENGFLAG-ONLY (bisection): language flag forced, ALL other patches skipped")
        return bytes(a)
    # ---- Direction-MENU lag fix (v11): auto re-submit trampoline (replicate the manual 2nd select).
    # GROUND TRUTH (savestate dump @BP 0x06046C30): the menu's command buffer is BYTE-IDENTICAL to the
    # keyboard's (0082 0000..0000 FFFF, already terminated). The lag is purely that the keyboard drives
    # the per-frame feeder to drain the buffer to the 0xFFFF commit, and the menu (efa4==0) doesn't --
    # the manual 2nd select re-issues execute 0x06046700 and THAT commits. So: auto-replicate the 2nd
    # select ~1 frame later. Mechanism: the per-frame input handler 0x06045212 is dispatched via a
    # fn-ptr at pool 0x06045160 (jsr @0x06045138). Repoint that pointer to a trampoline in free ROM that
    # (1) calls the real handler, then (2) runs a PEND countdown; when PEND hits 0 AND efa5!=0 (command
    # still staged), redoes the menu pre-work (memcpy 0x060af0a0<-0x060adf1c 66B; fdc1=1) + execute
    # 0x06046700 -- exactly the 2nd select. The menu-submit site @0x060459f2 sets PEND=2 (fires next
    # frame). efa5!=0 guard => never re-fires an already-committed picker/keyboard command. PEND = 1 byte
    # in verified-free LWRAM 0x00230900. Trampoline reserved at the HEAD of the prose POOL (0x06068f34);
    # the pool cursor starts after it so prose/noun tables never overwrite it.
    TRAMP_ADDR = BASE + POOL                                # 0x06068f34
    _sh2 = _Sh2Asm(TRAMP_ADDR)
    _sh2.emit(
        0x4F22,                    # sts.l pr,@-r15
        ('movl', 'IH', 1),         # r1 = &real input handler
        0x410B, 0x0009,            # jsr @r1 ; nop            -> run real per-frame handler
        ('movl', 'PEND', 2),       # r2 = &PEND
        0x6320, 0x633C, 0x2338,    # r3=*PEND ; extu.b ; tst r3
        ('bt', 'DONE'),            # PEND==0 -> done
        0x73FF, 0x2230,            # r3-- ; *PEND=r3
        0x2338, ('bf', 'DONE'),    # tst r3 ; still>0 -> done
        ('movl', 'EFA4', 1),       # r1=&efa4  (efa4==1 = command still staged/armed)
        0x6110, 0x611C, 0x2118,    # r1=*efa4 ; extu.b ; tst
        ('bt', 'DONE'),            # efa4==0 (committed/idle) -> skip (guards double-fire)
        # restore the command id into 0x060adf1c[0] (the struct is zeroed after the 1st select)
        ('movl', 'SAVEDR12', 2),   # r2 = &SAVED_R12
        0x6121,                    # mov.w @r2,r1   -> r1 = saved command id
        ('movl', 'SRC', 3),        # r3 = 0x060adf1c
        0x2311,                    # mov.w r1,@r3   -> 0x060adf1c[0] = id
        # redo the menu pre-work + execute = the 2nd select
        ('movl', 'DST', 4),        # r4=0x060af0a0
        ('movl', 'SRC', 5),        # r5=0x060adf1c
        0xE642,                    # mov #66,r6
        ('movl', 'MEMCPY', 1), 0x410B, 0x0009,   # jsr memcpy(0x060af0a0<-0x060adf1c,66)
        ('movl', 'FDC1', 1), 0xE301, 0x2130,     # r1=&fdc1 ; r3=1 ; *fdc1=1
        ('movl', 'SRC', 4),        # r4=0x060adf1c
        ('movl', 'EXEC', 1), 0x410B, 0x0009,     # jsr execute 0x06046700
        ('label', 'DONE'),
        0x4F26, 0x000B, 0x0009,    # lds.l @r15+,pr ; rts ; nop(delay)
    )
    # PEND + SAVED_R12 scratch live INSIDE the reserved trampoline region (part of the loaded 0ZORK
    # image at 0x06068fb0/0x06068fb2), NOT in LWRAM: that region is zero in ROM (so PEND=0 at boot -> no
    # spurious fire) and the game never touches it (no room-load collision). Earlier LWRAM 0x00230900
    # scratch caused garbled text (boot garbage + room-load collision).
    PENDA = TRAMP_ADDR + 0x70                               # 0x06068fa4 (right after the 112B trampoline)
    SAVEDR12A = TRAMP_ADDR + 0x72                           # 0x06068fa6
    _sh2.pool('IH', 0x06045212, 'PEND', PENDA, 'EFA4', 0x0608EFA4,
              'SAVEDR12', SAVEDR12A, 'DST', 0x060AF0A0, 'SRC', 0x060ADF1C,
              'MEMCPY', 0x0605697C, 'FDC1', 0x0608FDC1, 'EXEC', 0x06046700)
    TRAMP_FOFF = POOL                                       # head of prose pool (file 0x64f34)
    tramp = _sh2.assemble()
    TRAMP_LEN = 0x74                                        # reserve trampoline code + pool + 4B scratch
    assert len(tramp) <= 0x70, "trampoline overruns scratch area (%d)" % len(tramp)
    assert all(x == 0 for x in a[TRAMP_FOFF:TRAMP_FOFF + TRAMP_LEN]), "trampoline region not free"
    a[TRAMP_FOFF:TRAMP_FOFF + len(tramp)] = tramp           # scratch bytes stay zero (PEND=0 at boot)
    # hook 1: repoint input-handler fn-ptr @0x06045160 -> trampoline
    assert struct.unpack_from(">I", a, 0x41160)[0] == 0x06045212, "ih fn-ptr site mismatch"
    struct.pack_into(">I", a, 0x41160, TRAMP_ADDR)
    # hook 2: menu-submit @0x060459f2 -> set PEND=2 (reuse dead pool 0x06045a40 for &PEND)
    assert (a[0x419f2] << 8 | a[0x419f3]) == 0x600C and (a[0x419f4] << 8 | a[0x419f5]) == 0x2008 \
        and (a[0x419fa] << 8 | a[0x419fb]) == 0xD111 \
        and struct.unpack_from(">I", a, 0x41a40)[0] == 0x060AF144, "menu PEND-set site mismatch"
    # DIAGNOSTIC toggle: when OFF, DON'T set PEND at menu-submit -> trampoline stays installed but dormant
    # (calls the real handler every frame, never re-submits) -> the direction menu reverts to native
    # 2-manual-select behavior. Tests whether the trampoline's auto-resubmit is what chains a 2nd command
    # for non-north (the "命令文をつなげられる" RB error). Native 2-select should move ALL directions.
    _MENU_LAG = True
    if _MENU_LAG:
        a[0x419f2] = 0xD1; a[0x419f3] = 0x13   # mov.l @(0x06045a40),r1  ; r1 = &PEND (0x06068fb0)
        a[0x419f4] = 0xE2; a[0x419f5] = 0x02   # mov #2,r2
        a[0x419f6] = 0x21; a[0x419f7] = 0x20   # mov.b r2,@r1            ; PEND = 2 (byte 0)
        a[0x419f8] = 0x60; a[0x419f9] = 0xC3   # mov r12,r0              ; r12 = command id (still live here)
        a[0x419fa] = 0x81; a[0x419fb] = 0x11   # mov.w r0,@(2,r1)        ; SAVED_R12 = r12 (0x06068fb2)
        a[0x419fc] = 0xE3; a[0x419fd] = 0xA4   # mov #-92,r3             ; epilogue r3 (unchanged)
        a[0x419fe] = 0x00; a[0x419ff] = 0x09   # nop
        struct.pack_into(">I", a, 0x41a40, PENDA)               # pool 0x06045a40 -> &PEND (0x06068fb0)
    # ---- Suppress the untranslated "no verb" parser error (renders as garbage on the engflag path).
    # The menu's Phase A (staged direction, seen as verbless) hits the no-verb path @0x06046b9c, which
    # loads the JP msg 0x06046630 + printer 0x060489ec and branches to 0x06046ef0 (jsr print). The
    # trampoline then commits the move, so that error is semantically wrong (the command DID work) and
    # shows as junk ("s  H") before the room. Retarget the branch 0x06046ef0 -> 0x06046ef4 so it SKIPS
    # the print jsr but keeps the rest (efa5=0 + return). Only affects verbless commands (valid keyboard
    # commands carry a verb via the shim, so they never reach here); the direction menu is the real case.
    assert (a[0x42ba0] << 8 | a[0x42ba1]) == 0xA1A6, "no-verb error bra site mismatch"
    a[0x42ba0] = 0xA1; a[0x42ba1] = 0xA8   # bra 0x06046ef0 (print) -> bra 0x06046ef4 (skip print)
    # ---- Suppress the go/direction "どこへ行くのですか?" prompt (the "s h" garbage) -- CONTROL-FLOW.
    # This is the build where NORTH moves cleanly (menu-north = just moves, no echo/garbage; typed = echoes
    # "go DIR" + moves). The incomplete-command QUESTION builder (fn 0x060480AC..0x060483b2) builds
    # "どこへ行くのですか?" and appends it; low bytes render as "s"(=行) "H"(=?) = "s h". Reached at 0x0604824a
    # when a command field is -1. Overwrite the dead append region there with: efa4=1; efa5=0; jmp to the real
    # stack-restore epilogue 0x060483a6 (can't jmp 0x06048394 -- its efa5 handling derefs r9, set at 0x06048366
    # which we skip). KNOWN LIMITATION: non-north DIRECTION MENU gives a chain-limit error here (the menu-lag
    # trampoline only stages north; non-north menu selection never populates 0x060adf1c -- see memory). Typed
    # non-north + menu-north work. User explicitly chose this (north-correct) build.
    # DIAGNOSTIC toggle: this suppress jmps past the entire append region INCLUDING the efa5==1 path's
    # staging memcpy (0x0605697c @0x06048382) and forces efa5=0 -- which is exactly the non-north menu
    # command-staging path. Suspected cause of the non-north menu RB error. When OFF, the "s h" go-prompt
    # returns but the menu staging is intact.
    _GO_PROMPT_SUPPRESS = True
    if _GO_PROMPT_SUPPRESS:
        SUP = _Sh2Asm(0x0604824A)
        SUP.emit(
            ('movl', 'EFA4', 2), 0xE101, 0x2210,   # r2=&efa4 ; r1=1 ; efa4=1
            ('movl', 'EFA5', 2), 0xE100, 0x2210,   # r2=&efa5 ; r1=0 ; efa5=0
            ('movl', 'EPI', 1), 0x412B, 0x0009,    # r1=0x060483a6 ; jmp @r1 ; nop(delay) -> stack pop + rts
        )
        SUP.pool('EFA4', 0x0608EFA4, 'EFA5', 0x0608EFA5, 'EPI', 0x060483A6)
        sup = SUP.assemble()
        assert (a[0x4424A] << 8 | a[0x4424B]) == 0x7118, "go-prompt suppress site mismatch"
        a[0x4424A:0x4424A + len(sup)] = sup
    if verbose:
        print("  MENU-LAG v11c: auto re-submit trampoline @0x%08X (%dB); ih-ptr@0x06045160 repointed; "
              "PEND/scratch @0x%08X (reserved, boot-zero); menu-submit@0x060459f2 sets PEND=2"
              % (TRAMP_ADDR, len(tramp), PENDA))
    cur = POOL + TRAMP_LEN
    n_ok = n_skip = 0
    def place_ptr(toff_ix, blob):
        nonlocal cur, n_ok, n_skip
        if cur + len(blob) > POOL_END:
            n_skip += 1; return None
        a[cur:cur + len(blob)] = blob; addr = BASE + cur
        struct.pack_into(">I", a, toff_ix, addr); cur += len(blob); n_ok += 1; return addr
    for tname, ix, txt in _entries():
        blob = wrap_segments(txt if isinstance(txt, list) else [txt])
        toff, n = TBL[tname]; assert 0 <= ix < n
        place_ptr(toff + ix * 4, blob)
    for ptr_addr, val in LOOSE.items():
        if isinstance(val, list):
            blob = wrap_segments(val)
        elif isinstance(val, tuple):
            txt, term = val; blob = enc(txt)[:-1] + bytes([term])
        else:
            blob = enc(val)
        place_ptr(ptr_addr - BASE, blob)  # loose: ptr slot is at (ptr_addr-BASE)
    if verbose:
        print("  prose relocated: %d ok, %d skipped (pool full); pool used %d/%d B"
              % (n_ok, n_skip, cur - POOL, POOL_END - POOL))
    # ---- Phase 1: noun-select crash fix (parallel English noun table, index-parallel to run1) ----
    straddr = {}; ptrs = []; n_en = 0
    for i in range(N1):
        v = struct.unpack_from(">I", a, RUN1_FOFF + i * 4)[0]
        en = None
        f = v - BASE
        if 0 <= f < len(a):
            e = a.find(b"\x00", f)
            try: en = READING_EN.get(a[f:e].decode("shift_jis"))
            except Exception: en = None
        if en:
            if en not in straddr:
                blob = enc_fw(en)
                if cur + len(blob) > POOL_END:
                    raise RuntimeError("pool full building noun table")
                a[cur:cur + len(blob)] = blob; straddr[en] = BASE + cur; cur += len(blob)
            ptrs.append(straddr[en]); n_en += 1
        else:
            ptrs.append(v)                        # keep stock JP pointer where no English name
    cur = (cur + 3) & ~3
    table_addr = BASE + cur
    if cur + N1 * 4 > POOL_END:
        raise RuntimeError("pool full: noun table")
    for p in ptrs:
        struct.pack_into(">I", a, cur, p); cur += 4
    struct.pack_into(">I", a, CMD_LIT_RUN2, table_addr)   # @0x0601fbc4 -> parallel English table
    if verbose:
        print("  noun crash-fix: 364-entry English table @0x%08x (%d English, %d JP); @0x0601fbc4 repointed; pool %d/%d B"
              % (table_addr, n_en, N1 - n_en, cur - POOL, POOL_END - POOL))
    # ---- Phase 2: EN->JP command shim — now a TWO-STAGE routine (phrase + word-by-word). Placed in the
    # 8KB gap in Phase 4 because its word table (~3KB) is too big for the safe pool. Just check the site. ----
    assert struct.unpack_from(">I", a, SHIM_LIT_FOFF)[0] == TRANSCODE_ADDR, "shim literal site mismatch"
    # ---- Phase 3: word-picker crash fix — route English scanner through the proven JP path ----
    # Scanner FUN_0x0601ff14 forks on the engflag @0x0601ff5a (bf/s): English (engflag!=0, T=0) takes
    # the dormant/unfinished English path @0x0601ff62 which reads its vocab through 0x0607cf6c and dies
    # instantly on picker-open (before the selector ever draws). Nop the branch so BOTH languages fall
    # through to the working Japanese path @0x0602012c (the JP picker is known-good). 1 byte, revertible.
    PICKER_BR_FOFF = 0x1bf5a
    assert a[PICKER_BR_FOFF] == 0x8F and a[PICKER_BR_FOFF + 1] == 0x02, "picker branch site mismatch"
    a[PICKER_BR_FOFF] = 0x00; a[PICKER_BR_FOFF + 1] = 0x09   # bf/s 0x0601ff62 -> nop
    if verbose:
        print("  PICKER: engflag scanner branch @0x0601ff5a nop'd -> English routed to JP path @0x0602012c")
    # ---- Phase 3b: route the OTHER command-vocab engflag forks to JP (fixes non-north direction menu) ----
    # The scanner/vocab subsystem (0x0601Fxxx) has 3 more engflag forks besides the scanner one. In English
    # mode they select the UNFINISHED English vocab (8-byte record stride / table 0x060AD772) instead of the
    # complete JP tables (16-byte / 0x060B897E). The stock JP game moves west fine; our engflag=1 build sends
    # non-north directions into the dead English vocab -> token-count 0 -> chain-limit RB error. Our commands
    # are internally JP (via the shim), so command parsing must use the JP vocab. Route these to JP too:
    #   0x0601fec4 bf/s (engflag!=0 -> stride 8) -> nop  => always fall through to stride 16 (JP)
    #   0x0601fd56 bt/s 0x0601fd7c (engflag==0 -> JP table) -> bra  => always take JP table 0x060B897E
    _FORCE_JP_PARSE = True
    if _FORCE_JP_PARSE:
        assert a[0x1bec4] == 0x8F and a[0x1bec5] == 0x01, "stride fork 0x0601fec4 site mismatch"
        a[0x1bec4] = 0x00; a[0x1bec5] = 0x09    # bf/s 0x0601feca -> nop  (force JP stride 16)
        assert a[0x1bd56] == 0x8D and a[0x1bd57] == 0x11, "table fork 0x0601fd56 site mismatch"
        a[0x1bd56] = 0xA0; a[0x1bd57] = 0x11    # bt/s 0x0601fd7c -> bra 0x0601fd7c (force JP table)
        if verbose:
            print("  FORCE-JP-PARSE: vocab forks 0x0601fec4 (stride) + 0x0601fd56 (table) routed to JP")
    # ---- Phase 4: English picker recognition — attr-format vocab + repoint ----
    # DISABLED. The recognition CONCEPT is proven (repointing the vocab literal @0x060202a0 to an
    # attr-format [0x0082][char] English dict made the match succeed on English words). BUT: (1) the
    # POOL (0x06068f34+0xD000) is NOT actually free past ~0x06071204 — the game uses that RAM at
    # runtime (a 0x88-stride structure from ~0x06072af7 up, plus large buffers), so the ~19.7KB of
    # ROM zeros I counted are LIVE RAM. Placing the 11.5KB dict there clobbered the background/text
    # buffers -> missing background + crash. (2) Even once dict placement is safe, a recognized word
    # triggers the resident resolution dispatch (PR=0x06000956 -> null jump) during render-time noun
    # marking -> crash. So recognition needs BOTH a truly-free home for the dict AND interception of
    # the resident dispatch. Keep OFF until both are solved.  Data source: zork_data/picker_en_words.py
    # Placement fix: the dict must live in genuinely-free RAM. The old pool tail (0x06071204+) is LIVE
    # at runtime (0x88-stride buffer) -> overwriting it caused the background loss + crash. Use the 8KB
    # gap @0x06022f80 (file 0x1ef80) which is zero in BOTH the ROM and a working runtime state, and sits
    # OUTSIDE the BSS region (<0x0605e984). TEST MODE first: a small West-of-House word set to validate
    # that safe placement stops the crash (and, ideally, snaps words to yellow).
    ENABLE_PICKER_DICT = True
    # The RESOLVER reads the matched index from the JAPANESE table 0x0607c9bc (NOT the English 0x0607cf6c
    # I first used). Verified: recognition matched mailbox at English-index 152 but 0x0607c9bc[152]=森林
    # -> resolved to "forest". FIX: build the recognition dict in 0x0607c9bc (resolver) ORDER, placing the
    # ENGLISH translation of each JP word at its slot (via READING_EN). Then recognition-index ==
    # resolution-index, so a picked word resolves to itself. RESOLVE_ORDER[i] = english|None (frozen from
    # 0x0607c9bc + READING_EN). 363 slots, 95 English, rest dummy. mailbox -> index 11.
    PICK_DICT_FOFF = 0x1ef80        # addr 0x06022f80, 8192B free (ROM+runtime), non-BSS
    PICK_DICT_CAP = 0x2000
    if ENABLE_PICKER_DICT:
        from zork_data.picker_resolve_order import RESOLVE_ORDER
        def enc_pick(w):
            b = bytearray()
            for ch in w:
                if ch == ' ': b += b"\x00\x20"
                else:         b += b"\x00\x82\x00" + bytes([ord(ch) & 0xFF])
            return bytes(b) + b"\x00\x00"
        DUMMY = b"\x00\x82\x00\x01\x00\x00"        # [0x0082][0x0001] -> never matches room text; non-empty
        assert all(x == 0 for x in a[PICK_DICT_FOFF:PICK_DICT_FOFF + PICK_DICT_CAP]), "picker dict target not free"
        c = PICK_DICT_FOFF
        word_addrs = []; nreal = 0
        for en in RESOLVE_ORDER:                     # resolver order: recognition idx == resolution idx
            if en:
                blob = enc_pick(en); nreal += 1
            else:
                blob = DUMMY
            a[c:c + len(blob)] = blob; word_addrs.append(BASE + c); c += len(blob)
        term_word = BASE + c; a[c:c + 2] = b"\x00\x00"; c += 2
        c = (c + 3) & ~3
        pick_tab = BASE + c
        for adr in word_addrs:
            struct.pack_into(">I", a, c, adr); c += 4
        struct.pack_into(">I", a, c, term_word); c += 4
        assert c - PICK_DICT_FOFF <= PICK_DICT_CAP, "picker dict overflow gap (%d>%d)" % (c - PICK_DICT_FOFF, PICK_DICT_CAP)
        struct.pack_into(">I", a, 0x1c2a0, pick_tab)           # 0x060202a0: match table -> resolver-order English dict
        if verbose:
            print("  PICKER DICT (resolver-order): %d slots (%d English, rest dummy) @0x%08x file 0x%05x, %d/%d B; repointed"
                  % (len(RESOLVE_ORDER), nreal, pick_tab, PICK_DICT_FOFF, c - PICK_DICT_FOFF, PICK_DICT_CAP))
        # ---- box-write hook: full-width (0x82,low=char+0x1f) -> ASCII in the box builder's copy ----
        # Picker resolution writes the resolved noun into the WIDE box as full-width SJIS; box builder
        # FUN_0x0601ae92 strcpy's wide->ascii slot 0x060ae080 (strcpy=0x0604e94c @lit 0x0601afbc). That
        # ascii slot feeds display + parse buffer 0x060a5a9c. Repoint the strcpy to a converting copy so
        # the noun becomes single-byte ASCII (displays right AND parses via the shim). Keyboard input is
        # plain ASCII (no 0x82) -> passes through unchanged.
        #   loop: mov.b @r5+,r0; cmp/eq #-126,r0; bf nf; mov.b @r5+,r0; add #-31,r0;
        #   nf: mov.b r0,@r4; tst r0,r0; bt end; add #1,r4; bra loop; nop; end: rts; nop
        # (mov.b @r5+,r0 auto-increments r5, so full-width consumes 2 src bytes, ascii 1; r4 bumped by hand)
        conv = bytes.fromhex("6054 8882 8b01 6054 70e1 2400 2008 8902 7401 aff5 0009 000b 0009".replace(" ", ""))
        c = (c + 1) & ~1
        conv_addr = BASE + c
        a[c:c + len(conv)] = conv; c += len(conv)
        assert c - PICK_DICT_FOFF <= PICK_DICT_CAP, "conv routine overflow gap"
        BUILDER_STRCPY_LIT = 0x16fbc                            # 0x0601afbc holds 0x0604e94c
        assert struct.unpack_from(">I", a, BUILDER_STRCPY_LIT)[0] == 0x0604e94c, "builder strcpy literal mismatch"
        struct.pack_into(">I", a, BUILDER_STRCPY_LIT, conv_addr)
        if verbose:
            print("  BOX-WRITE HOOK: builder strcpy 0x0604e94c -> convert-copy @0x%08x (full-width->ascii)" % conv_addr)
        # ---- Phase 2 (deferred): TWO-STAGE EN->JP shim in the gap (phrase table + word table + routine) ----
        c = (c + 3) & ~3
        ptbl = zork_shim.build_table(zork_shim.PHRASES)
        phrase_addr = BASE + c; a[c:c + len(ptbl)] = ptbl; c += len(ptbl)
        budget = PICK_DICT_CAP - (c - PICK_DICT_FOFF) - 300     # reserve for word-table terminator + routine
        wtbl = bytearray(); nwords = 0
        for en, jp in zork_shim.WORDS:
            ent = en.encode("ascii") + b"\x00" + jp.encode("shift_jis") + b"\x00"
            if len(wtbl) + len(ent) + 1 > budget: break
            wtbl += ent; nwords += 1
        wtbl += b"\x00"
        c = (c + 3) & ~3
        word_addr = BASE + c; a[c:c + len(wtbl)] = wtbl; c += len(wtbl)
        c = (c + 3) & ~3
        shim_addr = BASE + c
        code = zork_shim.assemble(phrase_addr, word_addr, TRANSCODE_ADDR)
        a[c:c + len(code)] = code; c += len(code)
        assert c - PICK_DICT_FOFF <= PICK_DICT_CAP, "shim overflow gap (%d>%d)" % (c - PICK_DICT_FOFF, PICK_DICT_CAP)
        struct.pack_into(">I", a, SHIM_LIT_FOFF, shim_addr)    # repoint parse TRANSCODE -> two-stage shim
        if verbose:
            print("  SHIM (2-stage): routine @0x%08x phrase@0x%08x word@0x%08x (%d/%d words, %dB tbl); gap %d/%d B"
                  % (shim_addr, phrase_addr, word_addr, nwords, len(zork_shim.WORDS), len(wtbl), c - PICK_DICT_FOFF, PICK_DICT_CAP))
        # ---- GAP-ECHO: blank the JP command echo after every command ----
        # The shim writes the JP command (e.g. きた行く) into 0x060b2dc8, which is BOTH the tokenizer
        # input AND the on-screen command-echo line -> it renders as garbage ("s  H") after every
        # command (keyboard + menu). The tokenizer consumes 0x060b2dc8 WITHIN the handler frame; the
        # menu-lag feeder/execute (0x06046700) reads tokens + the staged struct, NOT this buffer -- so
        # blanking 0x060b2dc8 AFTER the handler is safe. Rechain the per-frame input-handler fn-ptr
        # 0x06045160 -> gap_echo, which calls the pool trampoline (menu re-submit + real handler) and
        # then blanks 0x060b2dc8 iff its first byte is a JP lead byte (>=0x80). Keyboard-typed ASCII
        # (<0x80, e.g. "go north" while typing) is left intact. No pool growth (routine lives in the gap).
        c = (c + 3) & ~3
        ge = _Sh2Asm(BASE + c)
        ge.emit(
            0x4F22,                    # sts.l pr,@-r15
            ('movl', 'TRAMP', 1), 0x410B, 0x0009,   # jsr pool trampoline (real handler + menu PEND logic)
            ('movl', 'ECHO', 2),       # r2 = 0x060b2dc8
            0x6020, 0x600C, 0xC880,    # r0 = *echo(byte) ; extu.b ; tst #0x80,r0
            ('bt', 'GDONE'),           # high bit clear (ASCII) -> keep (English echo)
            0xE000, 0x2200,            # r0=0 ; *echo = 0  (blank the JP echo string)
            ('label', 'GDONE'),
            0x4F26, 0x000B, 0x0009,    # lds.l @r15+,pr ; rts ; nop
        )
        ge.pool('TRAMP', TRAMP_ADDR, 'ECHO', 0x060B2DC8)
        gcode = ge.assemble()
        assert c + len(gcode) - PICK_DICT_FOFF <= PICK_DICT_CAP, "gap-echo overflow gap"
        ge_addr = BASE + c
        a[c:c + len(gcode)] = gcode; c += len(gcode)
        struct.pack_into(">I", a, 0x41160, ge_addr)   # ih fn-ptr -> gap_echo (was -> pool trampoline)
        if verbose:
            print("  GAP-ECHO: fn-ptr 0x06045160 -> gap_echo @0x%08x (%dB); blanks JP command echo 0x060b2dc8"
                  % (ge_addr, len(gcode)))
        # DIAGNOSTIC: gap_echo blanks 0x060b2dc8 every frame after the handler; safe for typed (consumed
        # same-frame) but the MENU is lagged (trampoline re-executes a later frame) -> if the menu command
        # lives in 0x060b2dc8, gap_echo destroys it before the delayed tokenize -> suspected non-north menu
        # break. When OFF, repoint the ih fn-ptr straight to the trampoline (north's fix kept, no blanking).
        _GAP_ECHO = True   # (ruled out as the non-north menu cause; kept ON to keep north's echo clean)
        if not _GAP_ECHO:
            struct.pack_into(">I", a, 0x41160, TRAMP_ADDR)  # ih fn-ptr -> trampoline direct (bypass gap_echo)
            if verbose:
                print("  GAP-ECHO: DISABLED (diagnostic) -> ih fn-ptr 0x06045160 -> trampoline direct")
    # DECISIVE BISECTION: force the input-handler fn-ptr fully native (bypass the trampoline wrap AND
    # gap_echo entirely) so the direction menu runs exactly as the stock game. If non-north menu MOVES
    # here -> an input-hook (trampoline wrap / gap_echo) is the cause. If it still RB-errors -> the cause
    # is a command-path/vocab patch (shim / picker / box-write / noun repoint) or prose/responses.
    _INPUT_HOOKS = True
    if not _INPUT_HOOKS:
        struct.pack_into(">I", a, 0x41160, 0x06045212)   # native input handler, no wrap/gap_echo
        if verbose:
            print("  INPUT-HOOKS: DISABLED (bisection) -> ih fn-ptr 0x06045160 -> native 0x06045212")
    # ---- Phase 5: parser ACTION-RESPONSE / error messages -> English (IN PLACE) ----
    # The command-result / rejection messages (generic "can't do that here", "too hot to take",
    # "cannot open it", ...) live as 0x1c-delimited, 0x00-sub-delimited SJIS blocks in ~0x0608f000..
    # 0x06094000, referenced by pointers scattered across code literal pools (no master table). The
    # engflag +0x1F prose renderer draws them and STOPS at the first 0x00 or 0x1c. So each is
    # translated IN PLACE: overwrite [target, next 0x00/0x1c) with engflag English + a 0x00
    # terminator (blob <= original span; leftover JP past the 0x00 is never rendered). Every
    # scattered pointer to that target then shows English -- no repointing. Data: zork_data/responses.py
    n_resp = 0
    for addr, segs in RESPONSES.items():
        fo = addr - BASE
        assert 0 <= fo < len(a), "response addr 0x%08x out of range" % addr
        end = fo
        while end < len(a) and a[end] not in (0x00, 0x1c):
            end += 1
        blob = wrap_segments(segs if isinstance(segs, list) else [segs])
        assert fo + len(blob) <= end, ("response 0x%08x overflow: %d > span %d"
                                       % (addr, len(blob), end - fo))
        a[fo:fo + len(blob)] = blob
        n_resp += 1
    if verbose:
        print("  RESPONSES: %d parser action/error messages translated in place" % n_resp)
    assert len(a) == len(orig)
    return bytes(a)


# ---- disc assembly ---------------------------------------------------------
def find_dir_entry(tb, name):
    raw = b"".join(tb[(ROOTDIR_LBA + s) * SS + DO:(ROOTDIR_LBA + s) * SS + DO + 2048] for s in range(2))
    i = 0
    while i < len(raw):
        ln = raw[i]
        if ln == 0:
            i = (i // 2048 + 1) * 2048; continue
        nlen = raw[i + 32]; nm = raw[i + 33:i + 33 + nlen]
        if nm.split(b";")[0] == name.encode():
            sec = i // 2048; off_in = i % 2048
            fo = (ROOTDIR_LBA + sec) * SS + DO + off_in
            return fo, struct.unpack("<I", raw[i + 2:i + 6])[0], struct.unpack("<I", raw[i + 10:i + 14])[0]
        i += ln
    raise KeyError(name)


def reecc(tb, sec):
    s = bytearray(tb[sec * SS:sec * SS + SS]); ecc.fix_sector(s); tb[sec * SS:sec * SS + SS] = s


def write_file_in_disc(tb, name, newbytes):
    fo, lba, size = find_dir_entry(tb, name)
    assert len(newbytes) == size, "%s size %d != %d" % (name, len(newbytes), size)
    for s in range(math.ceil(size / USER)):
        chunk = bytearray(newbytes[s * USER:(s + 1) * USER]); chunk += b"\x00" * (USER - len(chunk))
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = chunk
        reecc(tb, lba + s)


def patch_sld_font(tb, name):
    fo, lba, size = find_dir_entry(tb, name)
    nsec = math.ceil(size / USER)
    raw = b"".join(tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] for s in range(nsec))[:size]
    dec = bytearray(z.decompress(raw))
    for i in range(FONT_BYTES):
        dec[i] = BITREV[dec[i]]
    src = ord(".")
    img = dec[src * GLYPH:src * GLYPH + GLYPH]
    assert any(img), "'.' glyph blank in %s" % name
    dec[PERIOD_SLOT * GLYPH:PERIOD_SLOT * GLYPH + GLYPH] = img
    comp = z.compress(bytes(dec))
    assert z.decompress(comp)[:len(dec)] == bytes(dec), "%s font round-trip" % name
    assert math.ceil(len(comp) / USER) == nsec, "%s recompress overflow" % name
    padded = comp + b"\x00" * (nsec * USER - len(comp))
    for s in range(nsec):
        tb[(lba + s) * SS + DO:(lba + s) * SS + DO + USER] = padded[s * USER:(s + 1) * USER]
        reecc(tb, lba + s)
    struct.pack_into("<I", tb, fo + 10, len(comp)); struct.pack_into(">I", tb, fo + 14, len(comp))


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    shutil.copyfile(SRC_T1, OUT_T1)
    tb = bytearray(open(OUT_T1, "rb").read())
    _fo, zlba, zsize = find_dir_entry(tb, "0ZORK.BIN")
    nsec = math.ceil(zsize / USER)
    orig0 = bytes(b"".join(tb[(zlba + s) * SS + DO:(zlba + s) * SS + DO + USER] for s in range(nsec))[:zsize])
    write_file_in_disc(tb, "0ZORK.BIN", build_patched_0zork(orig0, verbose=True))
    patch_sld_font(tb, "INIT2.SLD")
    patch_sld_font(tb, "SINIT2.SLD")
    for s in range(2):
        reecc(tb, ROOTDIR_LBA + s)
    open(OUT_T1, "wb").write(tb)

    lines = ['FILE "%s" BINARY' % os.path.basename(OUT_T1), '  TRACK 01 MODE1/2352', '    INDEX 01 00:00:00']
    for n in range(2, 33):
        fn = "Zork I - The Great Underground Empire (Japan) (Track %02d).bin" % n
        dst = os.path.join(OUTDIR, fn)
        if not os.path.exists(dst):
            try: os.link(os.path.join(ZDIR, fn), dst)
            except OSError: shutil.copyfile(os.path.join(ZDIR, fn), dst)
        lines += ['FILE "%s" BINARY' % fn, '  TRACK %02d AUDIO' % n,
                  '    INDEX 00 00:00:00', '    INDEX 01 00:02:00' if n == 2 else '    INDEX 01 00:01:74']
    open(OUT_CUE, "w", newline="\r\n").write("\n".join(lines) + "\n")
    print("OUTPUT:", OUTDIR)


if __name__ == "__main__":
    main()
