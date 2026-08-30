"""Strings NOT reached via the rooms98/msg777/dict234 tables.

LOOSE  = {pointer_addr: value}  -- a string referenced by a single code-embedded BE pointer
         (e.g. West-of-House body). The builder relocates the string to the pool and patches
         the 4 bytes at pointer_addr. A value may be:
           str                 -- plain text (display only)
           (text, term_byte)   -- end with a custom terminator instead of 0x00 (e.g. 0x1c)
           [seg, seg, ...]     -- segment list: str = literal text, int = inline dict-word TOKEN
                                  (emits 0x0e idx). Object words MUST be tokens, not literal
                                  text, or they render but are NOT colored/selectable.
         To find one: locate the JP string in 0ZORK, then search for a BE pointer == its addr
         (search for pointers INTO the whole blob to catch sibling fragments).

INPLACE = {addr: (english, byte_budget)}  -- string the engine reads at a FIXED address;
         relocating does nothing, so overwrite the bytes (must fit budget, NUL-padded).
         (Currently empty: the WoH title/object copies were decoys; those were dict words.)"""

LOOSE = {
    # Body description. Object words = inline dict TOKENS (int = dict234 index) so they stay
    # colored + SELECTABLE: 0x21=WHITE HOUSE, 0x1c=BOARD, 0x4a=DOOR. (Token-vs-literal is what
    # broke selection — the original JP body has 0e21/0e4a/0e1c here; plain English stripped them.)
    0x06042d44: ["YOU ARE STANDING IN AN OPEN FIELD WEST OF A ", 0x21,
                 ", WITH A ", 0x1c, "ED FRONT ", 0x4a, "."],
    # Object-listing wrapper (SHARED by every "there is X here"): "ここには" + <obj> + "があります。"
    0x0602b430: "THERE IS A\n",         # prefix ここには; trailing \n (->0x0c) puts the object on a
                                        # fresh line so a long object name (SMALL MAILBOX) doesn't
                                        # straddle the 16-cell wrap and stays selectable.
    0x0602b438: ("\nHERE.", 0x1c),       # suffix があります; leading \n puts HERE. on its own line
    0x06030e04: ("\nHERE.", 0x1c),       # so a long object name + " HERE." can't overflow/split
}

INPLACE = {}
