"""Steamgear Mash (Japan) `0.BIN` UI-string extraction, worklist, and patching.

The game's UI text is drawn by `FUN_06004e6c(x, y, color, char *str)` over
**null-terminated ASCII strings**, and the engine renders ASCII English natively
(the font has Latin glyphs). `0.BIN` is loaded to HWRAM `0x06004000`, so:

        HWRAM address = 0x06004000 + file_offset

The main UI string table lives around file offset `0x45000`-`0x46800` (credits,
"HIT ANY KEY TO START", "TYPE 1/2/3", "HOUR MIN SEC", "NEW GAME", asset filenames…).

This module finds those strings, builds a translation **worklist** with a per-string
byte **budget** (how many bytes are free before the next string), and patches new
ASCII back in safely (it refuses edits that would overflow the budget). Japanese
(kana-coded) strings use byte codes outside ASCII and are surfaced separately so they
can be located and overwritten with ASCII English once their charset is mapped.

Reinsertion: edit `0.BIN`, then ship it as a Sega Saturn Patcher `.ssp` (rebuilds
EDC/ECC), the same flow used for the Waialae `A.BIN` patch.
"""

import json
import struct

LOAD_BASE = 0x06004000
UI_TABLE_REGION = (0x45000, 0x46800)

# bytes that count as "real" UI text (printable ASCII)
_PRINTABLE = set(range(0x20, 0x7F))
# characters that make a run look like genuine label text (vs. incidental code)
_TEXTY = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?:'\"-/()")


def _runs(data, lo, hi, min_len, bounded):
    """Yield (offset, bytes) for maximal printable-ASCII runs in data[lo:hi].

    bounded=True keeps only runs that are NUL-terminated (and NUL/region-preceded),
    i.e. real string-table entries — this removes incidental printable code bytes.
    """
    hi = len(data) if hi is None else min(hi, len(data))
    i = lo
    while i < hi:
        if data[i] in _PRINTABLE:
            j = i
            while j < hi and data[j] in _PRINTABLE:
                j += 1
            if j - i >= min_len:
                ok = True
                if bounded:
                    nul_after = (j < len(data) and data[j] == 0)
                    nul_before = (i == lo or data[i - 1] == 0)
                    ok = nul_after and nul_before
                if ok:
                    yield i, data[i:j]
            i = j
        else:
            i += 1


def find_strings(data, lo=0, hi=None, min_len=3, bounded=False):
    """Return [(offset, text)] for printable-ASCII runs (decoded latin-1)."""
    return [(off, b.decode("latin-1")) for off, b in _runs(data, lo, hi, min_len, bounded)]


_ASSET_SUFFIXES = (".BIN", ".CPK", ".TSK", ".DMP", ".TXT", ".PRG")


def classify(text):
    """'filename' (asset path — do NOT translate) or 'ui' (translatable label)."""
    up = text.strip().upper()
    if any(up.endswith(s) for s in _ASSET_SUFFIXES):
        return "filename"
    return "ui"


def is_texty(s, threshold=0.8):
    """True if a run looks like real UI label text (mostly letters/spaces/punct)."""
    if not s:
        return False
    good = sum(1 for c in s.encode("latin-1") if c in _TEXTY)
    return good / len(s) >= threshold and any(ch.isalpha() for ch in s)


def worklist(data, region=UI_TABLE_REGION, min_len=3, texty_only=True,
             bounded=True):
    """Build a translation worklist for a region.

    Each entry: {offset, addr, text, kind, budget, translation}.
      offset      file offset in 0.BIN
      addr        HWRAM address (0x06004000 + offset)
      text        current ASCII string
      kind        'ui' (translate) or 'filename' (asset path — leave alone)
      budget      bytes available before the next string starts (max new length
                  INCLUDING the NUL terminator; i.e. new text <= budget-1 chars)
      translation "" placeholder for the English replacement
    """
    lo, hi = region
    found = find_strings(data, lo, hi, min_len, bounded=bounded)
    if texty_only:
        found = [(o, s) for o, s in found if is_texty(s)]
    out = []
    for idx, (off, text) in enumerate(found):
        # budget = gap to the next found string (so trailing NUL padding is usable)
        nxt = found[idx + 1][0] if idx + 1 < len(found) else min(off + len(text) + 256, hi)
        budget = nxt - off
        out.append({
            "offset": off,
            "addr": LOAD_BASE + off,
            "text": text,
            "kind": classify(text),
            "budget": budget,
            "translation": "",
        })
    return out


def dump_worklist(data, path, region=UI_TABLE_REGION, **kw):
    """Write the worklist to a JSON file the user can fill in `translation` fields."""
    wl = worklist(data, region, **kw)
    meta = {"_note": "Fill 'translation' (ASCII). Must be <= budget-1 chars. "
                     "Offsets are 0.BIN file offsets; addr = HWRAM. Leave blank to keep.",
            "strings": wl}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    return wl


def apply_edits(data, edits):
    """Apply translations to 0.BIN bytes.

    edits: iterable of (offset, new_text [, budget]). Writes ASCII + a NUL terminator,
    zero-padding the rest of the old string's footprint up to the next NUL. Raises
    ValueError if new_text + NUL exceeds the available budget.
    Returns a new bytearray.
    """
    buf = bytearray(data)
    for edit in edits:
        off, new_text = edit[0], edit[1]
        if not new_text:
            continue
        # available room = up to (and including) the original terminator run
        end = off
        while end < len(buf) and buf[end] != 0:
            end += 1
        # extend across trailing NUL padding (cheap slack), but stop before next text
        pad_end = end
        while pad_end < len(buf) and buf[pad_end] == 0:
            pad_end += 1
        budget = edit[2] if len(edit) > 2 else (pad_end - off)
        enc = new_text.encode("ascii")
        if len(enc) + 1 > budget:
            raise ValueError(
                f"0x{off:X}: {new_text!r} needs {len(enc)+1} bytes > budget {budget}")
        buf[off:off + len(enc)] = enc
        # NUL-terminate and clear the remainder of the old string's footprint
        for k in range(off + len(enc), max(end + 1, off + len(enc) + 1)):
            buf[k] = 0
    return buf


def apply_worklist_file(data, path):
    """Apply a filled-in worklist JSON (only non-empty `translation` fields).

    Handles both ASCII (kind!='wide') and 2-byte wide (kind=='wide') entries.
    """
    spec = json.load(open(path, encoding="utf-8"))
    ascii_edits = [(e["offset"], e["translation"], e.get("budget"))
                   for e in spec["strings"]
                   if e.get("translation") and e.get("kind") not in ("wide", "dialogue")]
    wide_edits = [(e["offset"], e["translation"], e.get("budget"))
                  for e in spec["strings"]
                  if e.get("translation") and e.get("kind") == "wide"]
    buf = bytearray(apply_wide_edits(apply_edits(data, ascii_edits), wide_edits))
    # dialogue: fixed-width, overwrite exactly ncodes, white font. Hard line-break
    # words (0xFFFF newline / 0xFFFE box boundary) inside a run are PRESERVED in place
    # and text flows around them, so a wide entry that wraps over two on-screen lines
    # (e.g. a 31-wide run = 12 + boundary + 18) keeps its break. Inline soft markers
    # (0xFFFD/0xFFFA/0xFFFB) are < 0xFFFE and get overwritten like normal slots.
    for e in spec["strings"]:
        if e.get("translation") and e.get("kind") == "dialogue":
            off, nc = e["offset"], e["ncodes"]
            orig = struct.unpack(">%dH" % nc, bytes(buf[off:off + nc * 2]))
            text = e["translation"]
            ti = 0
            for i in range(nc):
                if orig[i] >= 0xFFFE:           # keep hard break word in place
                    continue
                ch = text[ti] if ti < len(text) else fill_for_dialogue()
                ti += 1
                code = ((ord(ch) & 0x7F) + DIALOGUE_FONT_BASE) * WIDE_SCALE
                struct.pack_into(">H", buf, off + i * 2, code)
    return bytes(buf)


def fill_for_dialogue():
    return " "


# ── 2-byte "wide" text: FUN_06005020, code = glyph_index * 4 ─────────────────
# The menu/Japanese text routine writes 16-bit BE codes that are VDP2 char-numbers
# = glyph_index*4 (font glyphs live at VDP2 VRAM base 0). For Latin, code/4 == the
# ASCII value (proven: "GAME OVER"), so English is just code = ord(ch)*4.
WIDE_SCALE = 4

# The game has two fonts addressed by the SAME FUN_06005020 routine, distinguished only
# by the glyph index used. The thin "title/menu" font is ASCII-ordered from glyph 0
# (so 'A'=65). The chunky white "dialogue" font is ASCII-ordered from glyph 470 (space),
# i.e. glyph = ASCII + 438 (CONFIRMED by reading the game's "ROLL" tilemap: R,O,L,L =
# 520,517,514,514). Menu text -> font_base 0; dialogue text -> font_base 438 (white).
DIALOGUE_FONT_BASE = 438


def _u16be(data, o):
    return (data[o] << 8) | data[o + 1]


def decode_wide_codes(codes):
    """Render a list of u16 codes as text: ASCII glyphs become the char, kana/kanji
    become ``{gNNN}`` (the glyph index = code/4)."""
    out = []
    for c in codes:
        if c % WIDE_SCALE == 0:
            g = c // WIDE_SCALE
            out.append(chr(g) if 0x20 <= g < 0x7F else "{g%d}" % g)
        else:
            out.append("{?%04X}" % c)
    return "".join(out)


def find_wide_strings(data, lo=0, hi=None, min_codes=2, gmin=0x20, gmax=0x200):
    """Find 0x0000-terminated runs of 16-bit BE codes that are glyph*4
    (code%4==0, glyph in [gmin,gmax)). Returns [(offset, [codes])]; each run must be
    preceded by a 0x0000 word (a real table entry, not incidental data)."""
    hi = len(data) if hi is None else min(hi, len(data))
    out = []
    i = lo
    while i + 2 <= hi:
        v = _u16be(data, i)
        if v % WIDE_SCALE == 0 and gmin <= v // WIDE_SCALE < gmax:
            j, codes = i, []
            while j + 2 <= hi:
                w = _u16be(data, j)
                if w % WIDE_SCALE == 0 and gmin <= w // WIDE_SCALE < gmax:
                    codes.append(w); j += 2
                else:
                    break
            if (len(codes) >= min_codes and j + 2 <= hi and _u16be(data, j) == 0
                    and (i < 2 or _u16be(data, i - 2) == 0)):
                out.append((i, codes))
            i = j + 2
        else:
            i += 2
    return out


def encode_wide(text):
    """Encode an ASCII string as wide codes (char*4, big-endian, 0x0000-terminated)."""
    out = bytearray()
    for ch in text:
        out += struct.pack(">H", (ord(ch) & 0x7F) * WIDE_SCALE)
    out += b"\x00\x00"
    return bytes(out)


def apply_wide_edits(data, edits):
    """Apply English translations to 2-byte wide strings in 0.BIN.

    edits: (offset, ascii_text [, budget]). Writes ``encode_wide(text)`` (2 bytes/char +
    2-byte NUL), zero-filling the rest of the old string's footprint. ValueError on overflow.
    """
    buf = bytearray(data)
    for edit in edits:
        off, new_text = edit[0], edit[1]
        if not new_text:
            continue
        # old footprint = up to and including the 0x0000 terminator, plus NUL padding
        end = off
        while end + 1 < len(buf) and _u16be(buf, end) != 0:
            end += 2
        pad_end = end
        while pad_end + 1 < len(buf) and _u16be(buf, pad_end) == 0:
            pad_end += 2
        budget = edit[2] if len(edit) > 2 else (pad_end - off)
        enc = encode_wide(new_text)
        if len(enc) > budget:
            raise ValueError(
                f"0x{off:X}: {new_text!r} needs {len(enc)} bytes > budget {budget}")
        buf[off:off + len(enc)] = enc
        for k in range(off + len(enc), end + 2):
            buf[k] = 0
    return buf


def find_dialogue_lines(data, lo, hi):
    """Split a dialogue block into lines on 0x0000 / 0xFFFF delimiters (the in-game text
    boxes use fixed-width, delimiter-separated lines — not NUL-terminated strings).
    Returns [(offset, [u16 codes])] for each run of non-delimiter words."""
    out = []
    i = lo
    while i + 2 <= hi:
        v = _u16be(data, i)
        if v in (0x0000, 0xFFFF):
            i += 2
            continue
        j, codes = i, []
        while j + 2 <= hi:
            w = _u16be(data, j)
            if w in (0x0000, 0xFFFF):
                break
            codes.append(w)
            j += 2
        out.append((i, codes))
        i = j + 2
    return out


def encode_wide_fixed(text, ncodes, fill=" ", font_base=0):
    """Exactly ``ncodes`` 16-bit codes (code = (ord(ch)+font_base)*4), padded with ``fill``,
    NO terminator — for overwriting a fixed-width dialogue line in place (delimiters kept).
    font_base=0 -> thin title font; font_base=438 -> chunky white dialogue font."""
    s = text[:ncodes].ljust(ncodes, fill)
    return b"".join(struct.pack(">H", ((ord(c) & 0x7F) + font_base) * WIDE_SCALE) for c in s)


def dialogue_worklist(data, lo, hi):
    """Worklist for the fixed-width dialogue block (lines split on 0xFFFF/0x0000).
    Each entry: {offset, addr, kind:'dialogue', ncodes (line width), glyphs, text, translation}.
    Apply writes exactly ncodes codes (English = code*4), so the line width is preserved."""
    out = []
    for off, codes in find_dialogue_lines(data, lo, hi):
        out.append({
            "offset": off,
            "addr": LOAD_BASE + off,
            "kind": "dialogue",
            "ncodes": len(codes),
            "glyphs": [c // WIDE_SCALE for c in codes],
            "text": decode_wide_codes(codes),
            "translation": "",
        })
    return out


def wide_worklist(data, region, gmin=0x20, gmax=0x200, min_codes=2):
    """Worklist for 2-byte menu strings. Entries carry kind='wide', the decoded
    ``text`` (ASCII + {gNNN} kana placeholders), glyph index list, and byte budget."""
    lo, hi = region
    found = find_wide_strings(data, lo, hi, min_codes, gmin, gmax)
    out = []
    for idx, (off, codes) in enumerate(found):
        nxt = found[idx + 1][0] if idx + 1 < len(found) else min(off + len(codes) * 2 + 64, hi)
        out.append({
            "offset": off,
            "addr": LOAD_BASE + off,
            "kind": "wide",
            "glyphs": [c // WIDE_SCALE for c in codes],
            "text": decode_wide_codes(codes),
            "budget": nxt - off,
            "translation": "",
        })
    return out
