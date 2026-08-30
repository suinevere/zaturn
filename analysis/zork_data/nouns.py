"""Selectable-object NOUN table (the 0x607c pointer table -> 0x601c JP word strings).

This is the table the SELECTION/HIGHLIGHT system uses (proven by a Mednafen write-watchpoint
on the object-name buffer 0x060ae080: selecting the mailbox runs a byte-copy from R12=0x0601cf34,
i.e. the noun-table entry, into 0x060ae080). The room DISPLAY uses dict234 (0e tokens), but
SELECTION reads these noun strings.

** The table is a SORTED binary-search parser vocabulary ** (ordered by Japanese reading:
篭, 裂け目, 林, ...). A 3-entry probe (white house / board / small mailbox) CRASHED the game on
entering OBJECT mode: the scan at a low-mem resident routine (PC 0x06000952, called from
0x0601fcaa) walked the table from its start (R10=0x0607c9bc) and, at the first out-of-order
English entry, the binary-search invariant broke -> bad pointer -> wild address (R2=0x8276 = the
'Ｗ' of WHITE HOUSE used as an offset) -> crash. So this table CANNOT be partially translated.

Correct approach (TODO): translate ALL ~660 entries (run1 0x607c9bc..0x607cf68 = 364 entries;
run2 0x607cfe8..0x607d484 = 296) to full-width English, then RE-SORT the whole table so it stays
ordered under whatever comparison the search uses (full-width A-Z = SJIS 0x8260..0x8279, so plain
ASCII/alphabetical order = SJIS byte order). Repoint every slot in sorted order. Also verify the
scan only DEREFERENCES the pointers (pool pointers ok) vs. does arithmetic on them.

NOUN = {pointer_addr: english}. Empty until the full-table rewrite is built (partial = crash)."""

# REAL CAUSE FOUND (2026-06-26): the de-risk crash was an ALIGNMENT error, not pointer arithmetic.
# The selection compare reads each noun via mov.w (@0x06020242 / @0x0602025e), so the noun string
# must be 2-byte aligned; the de-risk string landed at an ODD pool address (0x06069617) -> SH-2
# address-error exception -> hang at 0x06000952. Matching is LINEAR string-compare (no sort needed).
# Fix: the builder now 2-byte-aligns pool nouns (cur += cur&1). So the pool IS usable for nouns and
# we can scale to all ~660. English here must match the displayed dict234 word (linear byte compare).
# Probe: 3 West-of-House nouns (aligned) -> confirm selection works in English before the full table.
NOUN = {
    0x0607ca8c: "WHITE HOUSE",     # -> matches dict234[0x21]
    0x0607ca80: "BOARD",           # -> matches dict234[0x1c]
    0x0607cc30: "SMALL MAILBOX",   # -> matches dict234[0x5e] (multi-word works once nouns are
                                   # even-length-terminated; the wrapper break keeps it on one line)
    0x0607ce50: "DOOR",            # ドア  -> matches dict234[0x4a] display, so DOOR is selectable
    0x0607ca04: "WOODEN DOOR",     # 木のドア
    0x0607ca70: "DOOR",            # 扉
    0x0607cc48: "LEAFLET",         # 手紙 (run1 idx163) -> matches dict234[0x5a]=LEAFLET so it selects
}
