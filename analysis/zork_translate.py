#!/usr/bin/env python3
"""Zork I (Saturn JP) — English translation map + re-encoder (Tier-1).

Applies a (table,index)->English map by relocating each English string into a free pool in
0ZORK.BIN and repointing the table entry. English is encoded for the Saturn renderer:
  * letters/digits/punct -> ASCII (renderer converts to full-width; lowercase a-z does NOT
    render, so text is forced UPPERCASE)
  * ' ' (space) -> FULL-WIDTH space 0x81 0x40 (ASCII 0x20 is mangled)
  * '\n' -> 0x0c (paragraph break)
  * string terminated with 0x00
Dict words (234-table) are translated too, so they propagate everywhere they're referenced
(via 0x0e/0x1e). See docs/ZORK1_STRINGS_RECON.md.

build_patched_0zork() returns the patched 0ZORK.BIN bytes; analysis/zork_make_disc.py injects.
"""
import struct, os

BASE = 0x06004000
TBL = {                          # table name -> (file offset, n entries)
    "rooms98": (0x8b074, 98),
    "tbl33":   (0x8b2f4, 33),
    "dict234": (0x95a68, 234),
    "msg777":  (0x99be8, 777),
}
ORIG_SIZE = 0x9c4f8              # original 0ZORK.BIN size (640248)
# STABLE in-image pool. The old append-at-image-end pool (0x060a04f8+) sat INSIDE the engine's
# Z-machine workspace/heap (0x060a0000+) and was overwritten during play, corrupting on-demand
# reads (selectable object names rendered as garbage / wrong objects). A 54305-byte zero-filled
# region at file 0x64f34 (addr 0x06068f34) is read-only at runtime -- verified stable in a
# late-game savestate (mc7) -- so strings placed here survive. No image extension needed.
POOL = 0x64f34                   # addr 0x06068f34, inside the image's stable free zeros
POOL_SIZE = 0xD000               # 53 KB (region holds 54305 B of zeros)
POOL_END = POOL + POOL_SIZE
FWSP = b"\x81\x40"
LINE_W = 16                      # renderer wraps body text at 16 cells/row (measured from the
                                 # screen grid 0x060b4b18); used to keep object tokens unsplit.

# ---- translation maps (data split into zork_data/, one file per lane) -------
from zork_data.dict_words import DICT      # dict234 index -> English (propagates everywhere)
from zork_data.rooms import ROOMS          # rooms98 index -> English
from zork_data.messages import MSGS        # msg777 index -> English
from zork_data.loose import LOOSE, INPLACE # code-pointer strings / fixed-address overwrites
from zork_data.verbs import VERBS, MENU    # action-menu verbs + mode labels (mostly done)
from zork_data.nouns import NOUN           # 0x607c selectable-object table -> English (selection)

# Address ranges used by the verb/menu reinsertion (builder logic, not translation data):
VERB_SEQ_LO, VERB_SEQ_HI = 0x21fb4, 0x22410      # JP verbs as a 0x00-separated list
VERB_TABLE_LO, VERB_TABLE_HI = 0x22410, 0x22b80  # verb display table (both copies), repoint
MENU_LO, MENU_HI = 0x20fb0, 0x21010              # action-menu mode labels (in-place SJIS)
# Option C (command execution): the command-builder caller FUN_0x0601fb40 appends nountable[index]
# to the parsed command via two table-base literals (@0x0601fbc4=run2 0x0607cf6c, @0x0601fbc8=run1
# 0x0607c9bc). Our English noun tables make the appended word English -> the JP parser rejects it.
# Redirect ONLY these two literals to JP copies of the tables (display/highlight read 0x607c via
# their own code, so the on-screen UI stays English; the command box shows the JP noun, consistent
# with the JP verb form it already uses). Verified from savestate captures slot6/slot7 (write-bp on
# 0x060ae100/0x060ae080 -> strcpy 0x0604e94c, caller PR 0x0601aee2, builder 0x0601ae92).
NOUN_RUN1_N, NOUN_RUN2_N = 364, 296              # entries in run1 (0x0607c9bc) / run2 (0x0607cf6c)
CMD_LIT_RUN2, CMD_LIT_RUN1 = 0x1bbc4, 0x1bbc8    # file offsets of the two command-path table literals
# Same problem for VERBS: the verb command-append caller (0x06027c08, builder via PR 0x0601aee2,
# stack 0x06016808->0x06027c08->0x0601ae92; captured slot9 R12=English verb in pool) reads its verb
# table base r14 from two literals @0x06027b0c=copy1 0x06026410 (sorted/menu list) and @0x06027b14=
# copy2 0x060266ec (bucketed). Both got repointed to English by the verb relocation, so the appended
# verb is English -> parser rejects it. Redirect ONLY these two command-path literals to a JP copy of
# the verb table (menu display reads copy1 via a different site, so it stays English).
VERBTBL_OFF, VERBTBL_LEN = 0x22410, 0x770        # verb table region 0x06026410..0x06026b80 (476 ptrs)
VERB_COPY2_REL = 0x2dc                            # 0x060266ec - 0x06026410 (copy2 offset within region)
# copy1 (0x06026410) is read at 5 sites. ONLY the capture-confirmed command-append site is redirected
# here: 0x06027b0c (slot-9 write-bp on the verb command slot 0x060ae180 traced through this fn). The
# "builder-nearby" heuristic mis-flagged extra sites and redirecting them turned the ACTION MENU itself
# Japanese (one of 0x23f34/0x241c0/0x2458c is the menu renderer) — so they are left English. If other
# sentence forms still append an English verb, pin the exact site with a write-bp capture (don't guess).
VERB_CMD_LITS = []   # DISABLED: the verb menu RENDER and the command-append share one table base
                     # (fn @0x06027b00 loop draws via 0x0600f520 AND appends via builder 0x0601ae92,
                     # both using r14), so redirecting the literal turns the action menu Japanese.
                     # Verb command execution needs an instruction-level patch (append-only) or a
                     # rebucketed English verb vocab -- see memory. Menu stays English for now.

# ---- encoder ---------------------------------------------------------------
def enc(s):
    out = bytearray()
    for ch in s.upper():
        if ch == " ":
            out += FWSP
        elif ch == "\n":
            out.append(0x0c)
        elif 0x20 < ord(ch) < 0x7f:
            out.append(ord(ch))
        else:
            out += FWSP
    out.append(0x00)
    return bytes(out)

def enc_fw(s):
    """FULL-WIDTH SJIS encoding (every char = 2 bytes), 0x00-terminated.

    Used for DICT words (object names / room titles). The selection + command-builder system
    is entirely 2-byte SJIS (verified in savestates: selected object, particle, and command box
    are all stored as concatenated full-width text). Encoding object names as 1-byte ASCII via
    enc() desyncs the per-char span/selection walk, so a multi-letter object name never resolves
    to one selectable cell (cursor sits on a single letter, unselectable). Full-width keeps each
    name a contiguous 2-byte run, so it highlights/selects as one unit like the JP kanji did."""
    out = bytearray()
    for c in s.upper():
        o = ord(c)
        if c == " ":          out += b"\x81\x40"      # full-width space
        elif "0" <= c <= "9": out += bytes((0x82, 0x4f + o - 0x30))
        elif "A" <= c <= "Z": out += bytes((0x82, 0x60 + o - 0x41))
        else:                 out += b"\x81\x40"
    out.append(0x00)
    return bytes(out)

def _enc_char(ch):
    if ch == " ":               return FWSP
    if 0x20 < ord(ch) < 0x7f:   return bytes([ord(ch)])
    return FWSP

def wrap_segments(segs, term=0x00):
    """Build-time WORD WRAP for a body segment list (str literal | int dict-token).

    The on-screen renderer CHAR-wraps at LINE_W cells, splitting words mid-word (FIEL|D) and
    splitting object tokens across rows (which makes them unselectable). We pre-wrap here: group
    the text into atomic 'words' (a run with no internal space; a word may mix literal chars and a
    dict token, e.g. {BOARD}+ED), and greedily place them, emitting a 0x0c line break before any
    word that wouldn't fit the current 16-cell line. Words never split; tokens stay on one line.
    A space in a DICT token (e.g. "WHITE HOUSE") does NOT break the token (it's atomic). Returns
    the encoded blob ending with `term` (0x00, or a custom terminator)."""
    seq = []; cur = []                                   # seq: ('w',units)|('sp',)|('br',)
    def flush():
        if cur: seq.append(("w", list(cur))); cur.clear()
    for part in segs:
        if isinstance(part, int):                       # int -> 0x0e object token
            cur.append(("t", part))
        elif isinstance(part, tuple) and part[0] == "r": # ('r', idx) -> 0x1e room token
            cur.append(("rt", part[1]))
        elif isinstance(part, tuple) and part[0] == "ctrl":  # ('ctrl', byte) -> raw control byte
            flush(); seq.append(("raw", part[1]))             # e.g. 0x1c sub-message separator in a bank
        else:
            for ch in part.upper():
                if ch == " ":   flush(); seq.append(("sp",))
                elif ch == "\n": flush(); seq.append(("br",))
                else:           cur.append(("c", ch))
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
                    out.append(0x0c); col = 0                 # break: word starts a fresh line
                elif pending_sp:
                    out += FWSP; col += 1
            pending_sp = False
            for u in it[1]:
                if u[0] == "c":    out += _enc_char(u[1])
                elif u[0] == "rt": out += bytes([0x1e, u[1]])   # room token
                else:              out += bytes([0x0e, u[1]])   # object token

            col = (col + L) % LINE_W
    out.append(term)
    return bytes(out)

def _entries():
    for ix, txt in DICT.items():   yield ("dict234", ix, txt)
    for ix, txt in ROOMS.items():  yield ("rooms98", ix, txt)
    for ix, txt in MSGS.items():   yield ("msg777", ix, txt)

# ---- builder ---------------------------------------------------------------
def build_patched_0zork(verbose=False):
    here = os.path.dirname(__file__)
    a = bytearray(open(os.path.join(here, "..", "work", "zork1", "0ZORK.BIN"), "rb").read())
    if len(a) < POOL_END:                       # extend with the appended pool (zeros)
        a += b"\x00" * (POOL_END - len(a))
    # snapshot the ORIGINAL (all-JP) noun pointer tables BEFORE the NOUN loop repoints entries to
    # English. Option C (command execution) needs JP copies of these for the command-build path.
    ORIG_RUN1 = bytes(a[0x789bc:0x789bc + NOUN_RUN1_N * 4])   # 0x0607c9bc, 364 entries
    ORIG_RUN2 = bytes(a[0x78f6c:0x78f6c + NOUN_RUN2_N * 4])   # 0x0607cf6c, 296 entries
    ORIG_VERBTBL = bytes(a[VERBTBL_OFF:VERBTBL_OFF + VERBTBL_LEN])  # JP verb table (before relocation)
    cur = POOL
    for tname, ix, txt in _entries():
        # DICT words = selectable object names -> FULL-WIDTH SJIS (see enc_fw). ROOMS/MSGS = body
        # text -> word-wrapped (txt may be a str, or a segment list [str | int-token] for object
        # words). wrap_segments handles the 16-cell line so nothing splits and tokens stay whole.
        if tname == "dict234":
            blob = enc_fw(txt)
        else:
            blob = wrap_segments(txt if isinstance(txt, list) else [txt])
        if cur + len(blob) > POOL_END:
            print("  SKIP %-8s[%3d] (pool full, +%d B) -- needs bigger pool" % (tname, ix, len(blob)))
            continue
        a[cur:cur + len(blob)] = blob
        addr = BASE + cur
        toff, _ = TBL[tname]
        struct.pack_into(">I", a, toff + ix * 4, addr)
        if verbose:
            print("  %-8s[%3d] -> 0x%08x (%d B) %r" % (tname, ix, addr, len(blob), txt[:40]))
        cur += len(blob)
    # loose pointers: relocate string to pool + patch the 4-byte BE code pointer
    for ptr_addr, val in LOOSE.items():
        if isinstance(val, list):
            # segment list: str = literal text, int = inline dict-word TOKEN (0x0e idx). The token
            # is what makes a word a colored, SELECTABLE object. wrap_segments() word-wraps at the
            # 16-cell line so no word splits mid-word and no object token straddles two rows.
            blob = wrap_segments(val)
        elif isinstance(val, tuple):
            txt, term = val
            blob = enc(txt)[:-1] + bytes([term])   # custom terminator (e.g. 0x1c)
        else:
            txt = val; blob = enc(txt)
        if cur + len(blob) > POOL_END:
            print("  SKIP loose @0x%08x (pool full)" % ptr_addr); continue
        a[cur:cur + len(blob)] = blob
        addr = BASE + cur
        struct.pack_into(">I", a, ptr_addr - BASE, addr)
        if verbose:
            print("  loose   @0x%08x -> 0x%08x (%d B) %r" % (ptr_addr, addr, len(blob), txt[:40]))
        cur += len(blob)
    # NOUN table (0x607c selectable-object pointers): relocate FULL-WIDTH English to the pool +
    # repoint the slot. This is the table SELECTION reads (copied into 0x060ae080 on highlight/
    # select); English here must match the displayed dict234 word so the highlight lookup hits.
    for ptr_addr, eng in NOUN.items():
        # str -> full-width English; bytes -> raw SJIS as-is (used by the relocation de-risk test)
        blob = (bytes(eng) + b"\x00") if isinstance(eng, (bytes, bytearray)) else enc_fw(eng)
        if len(blob) & 1:
            blob += b"\x00"     # EVEN length: the compare detects noun-end by reading a 16-bit
                                # WORD (mov.w; tst). enc_fw is odd-length, so a 1-byte terminator
                                # pairs with the next string's first byte -> term word != 0 ->
                                # the noun never "ends" -> never matches. Pad to a full 0x0000 word.
        cur += cur & 1          # 2-BYTE ALIGN start too: the compare reads nouns via mov.w; an
                                # odd address = SH-2 address error -> hang.
        if cur + len(blob) > POOL_END:
            print("  SKIP noun @0x%08x (pool full)" % ptr_addr); continue
        a[cur:cur + len(blob)] = blob
        addr = BASE + cur
        struct.pack_into(">I", a, ptr_addr - BASE, addr)
        if verbose:
            print("  noun    @0x%08x -> 0x%08x (%d B) %r" % (ptr_addr, addr, len(blob), eng))
        cur += len(blob)
    # in-place string overwrites (fixed-address reads)
    for addr, (txt, budget) in INPLACE.items():
        blob = enc(txt)
        if len(blob) > budget:
            print("  SKIP inplace @0x%08x (%d > %d B)" % (addr, len(blob), budget)); continue
        fo = addr - BASE
        a[fo:fo + budget] = blob + b"\x00" * (budget - len(blob))  # NUL-pad the slot
        if verbose:
            print("  inplace @0x%08x (%d/%d B) %r" % (addr, len(blob), budget, txt))
    # in-place: rewrite JP entries with FULL-WIDTH SJIS English (menu renders raw font, NOT the
    # ASCII->full-width path rooms use). Each char = 2 bytes; pad with full-width space to byte len.
    def enc_fixed(s):
        out = bytearray()
        for c in s.upper():
            o = ord(c)
            if c == " ":          out += b"\x81\x40"
            elif "0" <= c <= "9": out += bytes((0x82, 0x4f + o - 0x30))
            elif "A" <= c <= "Z": out += bytes((0x82, 0x60 + o - 0x41))
            else:                 out += b"\x81\x40"
        return bytes(out)
    def inplace(mapping, lo, hi, label):
        done = 0; skipped = []
        region = bytes(a[lo:hi])
        for jp, eng in mapping.items():
            jb = jp.encode("shift_jis")
            idx = region.find(b"\x00" + jb + b"\x00")
            off = (lo + idx + 1) if idx >= 0 else (lo if region[:len(jb)] == jb else -1)
            if off < 0:
                skipped.append((jp, "?")); continue
            eb = enc_fixed(eng); gap = len(jb) - len(eb)
            if gap < 0 or gap % 2:
                skipped.append((jp, "len %d vs %s" % (len(jb), eng))); continue
            a[off:off + len(jb)] = eb + FWSP * (gap // 2); done += 1
        if verbose:
            print("  %s in-place: %d done, %d skipped %s" % (label, done, len(skipped), skipped[:5]))
    # verbs: relocate to pool as full-width English + repoint the display table (both copies)
    vcache = {}; vptrs = 0
    for off in range(VERB_TABLE_LO, VERB_TABLE_HI, 4):
        v = struct.unpack(">I", a[off:off + 4])[0]; p = v - BASE
        if not (0 <= p < ORIG_SIZE):
            continue
        end = a.find(b"\x00", p)
        if end < 0 or end - p > 20:
            continue
        jp = a[p:end].decode("shift_jis", "replace")
        if jp in VERBS:
            if jp not in vcache:
                blob = enc_fixed(VERBS[jp]) + b"\x00"
                a[cur:cur + len(blob)] = blob; vcache[jp] = BASE + cur; cur += len(blob)
            struct.pack_into(">I", a, off, vcache[jp]); vptrs += 1
    if verbose:
        print("  verbs relocated: %d unique, %d table ptrs repointed" % (len(vcache), vptrs))
    inplace(MENU, MENU_LO, MENU_HI, "menu")
    # OPTION C: place JP copies of the noun tables in the pool and repoint the command-path literals
    # so a SELECTED object appends its original Japanese keyword (which the parser accepts) instead of
    # the English display name. The copies hold the ORIGINAL pointers (JP strings are untouched in the
    # image), so command[index] -> JP for every noun while the English UI (0x607c) is unchanged.
    cur = (cur + 3) & ~3                                   # mov.l @(r0,r8) needs a 4-byte-aligned base
    need = len(ORIG_RUN1) + len(ORIG_RUN2)
    if cur + need > POOL_END:
        print("  SKIP option-C noun tables (pool full, +%d B)" % need)
    else:
        run1_addr = BASE + cur
        a[cur:cur + len(ORIG_RUN1)] = ORIG_RUN1; cur += len(ORIG_RUN1)
        run2_addr = BASE + cur
        a[cur:cur + len(ORIG_RUN2)] = ORIG_RUN2; cur += len(ORIG_RUN2)
        struct.pack_into(">I", a, CMD_LIT_RUN1, run1_addr)   # @0x0601fbc8 (was 0x0607c9bc)
        struct.pack_into(">I", a, CMD_LIT_RUN2, run2_addr)   # @0x0601fbc4 (was 0x0607cf6c)
        if verbose:
            print("  option-C: cmd noun path -> JP copies run1=0x%08x run2=0x%08x" % (run1_addr, run2_addr))
    # OPTION C (verbs): same redirect for the verb command-append. Place a JP copy of the verb table
    # and repoint the two command-path literals (copy1 @0x06027b0c, copy2 @0x06027b14) so the appended
    # verb is the original Japanese (parser-accepted) while the action menu (separate copy1 reader)
    # stays English.
    cur = (cur + 3) & ~3
    if cur + len(ORIG_VERBTBL) > POOL_END:
        print("  SKIP option-C verb table (pool full, +%d B)" % len(ORIG_VERBTBL))
    else:
        vtbl_addr = BASE + cur
        a[cur:cur + len(ORIG_VERBTBL)] = ORIG_VERBTBL; cur += len(ORIG_VERBTBL)
        # VERB append-only patch (CORRECT model). In fn @0x06027b00, r14 = a stack-local array that is a
        # FULL copy of the English verb table copy1 (verified stack[i]==copy1[i]); the render loop draws
        # it (= the English action menu) and the append does r8=r14+idx*4 then builder reads @r8, where
        # idx (r12+r10) is the ABSOLUTE verb index. So redirect ONLY the append to the JP copy by the
        # absolute index: replace `add r14,r8` with r8 = JP_copy_base + idx*4 (NOT sp+delta — sp is
        # runtime-variable, which is why the delta attempt produced garbage). Menu render still reads
        # the English stack array, so the menu stays English; the parsed verb becomes Japanese.
        # Drop the now-in-the-way immediate per-word draw call (command box still draws from the array).
        # Repurpose the freed draw-fn literal @0x06027ca4 to hold JP_copy_base.
        struct.pack_into(">I", a, 0x23ca4, vtbl_addr)      # literal @0x06027ca4: draw-fn -> JP_copy_base
        struct.pack_into(">H", a, 0x23bfc, 0xD029)         # 0x06027bfc: mov.l @(0x29,pc),r0 (r0=JP base)
        struct.pack_into(">H", a, 0x23bfe, 0x380c)         # 0x06027bfe: add r0,r8  (r8 = JP_copy+idx*4)
        struct.pack_into(">H", a, 0x23c00, 0x0009)         # 0x06027c00: nop  (was draw-fn load)
        struct.pack_into(">H", a, 0x23c04, 0x0009)         # 0x06027c04: nop  (drop immediate draw jsr)
        if verbose:
            print("  option-C: cmd verb append -> JP copy @0x%08x by abs idx (menu English)" % vtbl_addr)
    if verbose:
        print("pool used: %d / %d bytes" % (cur - POOL, POOL_END - POOL))
    return bytes(a)

if __name__ == "__main__":
    build_patched_0zork(verbose=True)
