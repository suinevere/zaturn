"""Round-trip tests for the Saturn translation core, using a synthetic binary.

We build a tiny big-endian "game file": a 4-entry pointer table at offset 0
followed by four Shift-JIS strings, mimicking how a Saturn text bank is laid out.
Then we exercise scanning, pointer detection, both reinsertion strategies, and a
synthetic ISO9660 image.
"""

import struct

from saturn_translate import sjis, pointers, reinsert
from saturn_translate.project import TranslationProject, StringEntry
from saturn_translate.iso import SaturnImage, SECTOR_USER


BASE = 0x06000000


def build_text_bank():
    """Return (data, [string_offsets]) for a 4-string, big-endian text bank."""
    strings = ["こんにちは".encode("cp932") + b"\x00",
               "セーブしますか".encode("cp932") + b"\x00",
               "はい".encode("cp932") + b"\x00",
               "いいえ".encode("cp932") + b"\x00"]
    table_size = 4 * 4
    body = bytearray()
    offsets = []
    for s in strings:
        offsets.append(table_size + len(body))
        body += s
    table = b"".join(struct.pack(">I", BASE + off) for off in offsets)
    return bytearray(table + body), offsets


def test_sjis_scan_finds_all_strings():
    # The raw scanner is heuristic: a printable low pointer byte can glue onto
    # the first string, so we assert it recovers the Japanese *content* of every
    # string (exact offsets are validated via pointer-anchored decoding below).
    data, offsets = build_text_bank()
    hits = sjis.scan(bytes(data))
    texts = " ".join(h.text for h in hits)
    for expected in ("こんにちは", "セーブしますか", "はい", "いいえ"):
        assert expected in texts


def test_pointer_anchored_decoding_is_exact():
    # Anchoring extraction to known (pointer-table) offsets gives exact results.
    data, offsets = build_text_bank()
    expected = ["こんにちは", "セーブしますか", "はい", "いいえ"]
    for off, want in zip(offsets, expected):
        hit = sjis.decode_string_at(bytes(data), off)
        assert hit.offset == off
        assert hit.text == want
        assert hit.terminator == 0x00


def test_pointer_table_detection():
    data, offsets = build_text_bank()
    tables = pointers.detect_tables(bytes(data), offsets, min_entries=3)
    assert tables, "expected at least one pointer table"
    top = tables[0]
    assert top.table_offset == 0
    assert top.count == 4
    assert top.base == BASE
    assert top.entries == offsets


def test_find_pointers_to_specific_string():
    data, offsets = build_text_bank()
    hits = pointers.find_pointers_to(bytes(data), offsets[1], bases=(BASE,))
    assert len(hits) == 1
    assert hits[0].pointer_offset == 4  # second table slot


def test_inplace_reinsertion_fits_and_pads():
    data, offsets = build_text_bank()
    data2 = bytearray(data)
    # "こんにちは" is 5 kanji * 2 bytes = 10 bytes; "Hello" fits.
    edit = reinsert.Edit(offset=offsets[0], original_length=10, translation="Hello")
    res = reinsert.reinsert_in_place(data2, [edit])
    assert res.written == 1
    assert res.overflowed == []
    # English readable back
    assert data2[offsets[0]:offsets[0] + 5] == b"Hello"


def test_inplace_overflow_is_truncated():
    data, offsets = build_text_bank()
    data2 = bytearray(data)
    edit = reinsert.Edit(offset=offsets[2], original_length=4,
                         translation="A very long answer")
    res = reinsert.reinsert_in_place(data2, [edit])
    assert offsets[2] in res.overflowed


def test_repoint_reinsertion_rewrites_pointers():
    data, offsets = build_text_bank()
    data2 = bytearray(data)
    original_len = len(data2)
    edits = [
        reinsert.Edit(offset=offsets[3], original_length=6,
                      translation="No way, this is much longer",
                      pointer_offsets=[12]),  # 4th table slot
    ]
    res = reinsert.reinsert_repoint(data2, edits, base=BASE)
    assert res.repointed == 1
    assert len(data2) > original_len
    # pointer slot now resolves to the appended region
    new_ptr = struct.unpack_from(">I", data2, 12)[0]
    assert (new_ptr - BASE) == res.new_region_offset
    assert data2[res.new_region_offset:res.new_region_offset + 6] == b"No way"


def test_project_roundtrip(tmp_path):
    data, offsets = build_text_bank()
    proj = TranslationProject(source_file="bank.bin", base_address=BASE)
    for i, off in enumerate(offsets):
        proj.strings.append(StringEntry(id=i, offset=off, length=4, original="x"))
    p = tmp_path / "proj.json"
    proj.save(str(p))
    loaded = TranslationProject.load(str(p))
    assert loaded.base_address == BASE
    assert len(loaded.strings) == 4
    assert loaded.stats()["total_strings"] == 4


# ── synthetic ISO9660 image ────────────────────────────────────────────
def build_iso_image(file_name: b"TEXT.BIN", file_data=b"hello-data"):
    """Construct a minimal valid 2048-byte ISO9660 image with one file."""
    n_sectors = 20
    img = bytearray(SECTOR_USER * n_sectors)
    # Saturn IP header so layout detection succeeds.
    img[0:16] = b"SEGA SEGASATURN "

    file_lba = 19
    img[file_lba * SECTOR_USER:file_lba * SECTOR_USER + len(file_data)] = file_data

    # Root directory at sector 18.
    root_lba = 18
    root = bytearray()

    def dir_record(name: bytes, lba: int, size: int, is_dir: bool):
        name_len = len(name)
        rec_len = 33 + name_len
        if rec_len % 2:
            rec_len += 1  # padding to even length
        rec = bytearray(rec_len)
        rec[0] = rec_len
        struct.pack_into("<I", rec, 2, lba)
        struct.pack_into(">I", rec, 6, lba)
        struct.pack_into("<I", rec, 10, size)
        struct.pack_into(">I", rec, 14, size)
        rec[25] = 0x02 if is_dir else 0x00
        rec[32] = name_len
        rec[33:33 + name_len] = name
        return bytes(rec)

    root += dir_record(b"\x00", root_lba, SECTOR_USER, True)   # .
    root += dir_record(b"\x01", root_lba, SECTOR_USER, True)   # ..
    root += dir_record(file_name, file_lba, len(file_data), False)
    img[root_lba * SECTOR_USER:root_lba * SECTOR_USER + len(root)] = root

    # Primary Volume Descriptor at sector 16.
    pvd = bytearray(SECTOR_USER)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    root_record = dir_record(b"\x00", root_lba, SECTOR_USER, True)
    pvd[156:156 + len(root_record)] = root_record
    img[16 * SECTOR_USER:17 * SECTOR_USER] = pvd

    return bytes(img)


def test_vcdiff_roundtrip_single_and_multi():
    from saturn_translate import vcdiff
    src = bytes(range(256)) * 40  # 10240 bytes
    # single 4-byte edit
    e1 = [vcdiff.Edit(offset=1000, old_len=4, data=b"\xde\xad\xbe\xef")]
    p1 = vcdiff.encode(src, e1)
    exp1 = bytearray(src); exp1[1000:1004] = b"\xde\xad\xbe\xef"
    assert vcdiff.decode(src, p1) == bytes(exp1)
    # multiple edits incl. a length change
    e2 = [vcdiff.Edit(50, 2, b"XY"), vcdiff.Edit(2000, 1, b"ZZZ")]
    p2 = vcdiff.encode(src, e2)
    exp2 = bytearray(src)
    exp2[2000:2001] = b"ZZZ"   # apply right-to-left to keep offsets valid
    exp2[50:52] = b"XY"
    assert vcdiff.decode(src, p2) == bytes(exp2)


def test_ips_encode_basic():
    from saturn_translate import ips
    p = ips.encode([ips.Record(offset=0x1C00B, data=b"\x54")])
    assert p[:5] == b"PATCH" and p[-3:] == b"EOF"
    # one record: 3-byte offset + 2-byte size + 1 data byte
    assert p[5:8] == b"\x01\xc0\x0b" and p[8:10] == b"\x00\x01" and p[10:11] == b"\x54"


def test_ecc_roundtrip_and_detect():
    from saturn_translate import ecc
    # Build a Mode 1 sector with valid sync + header, recompute ECC, and confirm
    # it then validates; then corrupt a data byte and confirm it's detected.
    sec = bytearray(2352)
    sec[0] = 0x00
    for k in range(1, 11):
        sec[k] = 0xFF
    sec[11] = 0x00
    sec[0x0C:0x10] = bytes([0x00, 0x02, 0x00, 0x01])  # MSF + mode 1
    for i in range(2048):
        sec[0x10 + i] = (i * 7) & 0xFF
    ecc.fix_sector(sec)
    assert ecc.sector_is_valid(bytes(sec))
    bad = bytearray(sec)
    bad[0x10] ^= 0xFF
    assert not ecc.sector_is_valid(bytes(bad))
    # fixing the corrupted sector makes it valid again
    ecc.fix_sector(bad)
    assert ecc.sector_is_valid(bytes(bad))


def test_sh2_disasm_known_opcodes():
    from saturn_translate import sh2
    # (opcode, expected mnemonic prefix) — checks the decoder on canonical SH-2.
    cases = {
        0x0009: "nop", 0x000b: "rts", 0x6432: "mov.l @r3,r4",
        0xe6ff: "mov #-1,r6", 0x7401: "add #1,r4", 0x2448: "tst r4,r4",
        0x3432: "cmp/hs r3,r4", 0x412b: "jmp @r1", 0x410b: "jsr @r1",
        0x4f22: "sts.l pr,@-r15", 0x6df6: "mov.l @r15+,r13", 0x4010: "dt r0",
        0x4001: "shlr r0", 0x4000: "shll r0",
    }
    for word, expect in cases.items():
        got = sh2.disasm(word, 0).text
        assert got.startswith(expect), f"{word:#06x}: got {got!r}, want {expect!r}"
    # branch target math (bra disp is sign-extended *2 + pc+4)
    bra = sh2.disasm(0xa008, 0x06004010)
    assert bra.is_branch and bra.target == 0x06004024


def test_iso_list_and_extract():
    data = b"this-is-the-game-text"
    img_bytes = build_iso_image(b"TEXT.BIN", data)
    img = SaturnImage(img_bytes)
    assert img.sector_size == SECTOR_USER
    files = img.list_files()
    names = {f.name for f in files}
    assert "TEXT.BIN" in names
    extracted = img.extract("/TEXT.BIN")
    assert extracted[:len(data)] == data


def test_sgfont_glyph_roundtrip():
    """Steamgear font codec must round-trip a glyph byte-exact (TL,TR,BL,BR cells)."""
    from saturn_translate import sgfont
    import random
    random.seed(1)
    grid = [[random.randint(0, 15) for _ in range(16)] for _ in range(16)]
    assert sgfont.decode_glyph(sgfont.encode_glyph(grid)) == grid
    data = sgfont.encode_glyph(grid)
    assert sgfont.encode_glyph(sgfont.decode_glyph(data)) == data
    assert len(data) == sgfont.GLYPH_BYTES


def test_sgfont_english_glyph():
    """English glyphs render into the game's 16x16 stroke-nibble format."""
    from saturn_translate import sgfont
    a = sgfont.english_glyph("A")
    assert len(a) == sgfont.GLYPH_BYTES and any(a)
    grid = sgfont.decode_glyph(a)
    assert any(sgfont.STROKE_NIBBLE in row for row in grid)
    assert not any(sgfont.english_glyph(" "))          # space is blank
    assert sgfont.english_glyph("a") == sgfont.english_glyph("A")  # case-folds


def test_sgtext_extract_classify_patch():
    """0.BIN UI-string extraction, filename classification, and safe patching."""
    from saturn_translate import sgtext
    # synthetic: a filename, a UI label, and trailing pad — all NUL-bounded
    blob = b"\x00" + b"BOOT.BIN\x00" + b"HIT START\x00\x00\x00\x00" + b"\x01\x02"
    base = 1
    found = dict(sgtext.find_strings(blob, bounded=True, min_len=3))
    assert "BOOT.BIN" in found.values() and "HIT START" in found.values()
    assert sgtext.classify("BOOT.BIN") == "filename"
    assert sgtext.classify("HIT START") == "ui"
    # patch the UI label (fits) ; offset of "HIT START"
    off = blob.index(b"HIT START")
    out = sgtext.apply_edits(blob, [(off, "GO")])
    assert out[off:off + 3] == b"GO\x00"
    # overflow is rejected (no room for an over-long replacement)
    try:
        sgtext.apply_edits(blob, [(off, "WAY TOO LONG TO FIT HERE AT ALL")])
        assert False, "expected overflow ValueError"
    except ValueError:
        pass


def test_sgtext_wide_codec():
    """2-byte menu codec: code = glyph_index*4; Latin -> ASCII; round-trips + patches."""
    from saturn_translate import sgtext
    # encode "GAME  OVER" -> codes are ord(ch)*4, big-endian, NUL-terminated
    enc = sgtext.encode_wide("GAME  OVER")
    assert enc == bytes.fromhex("011c0104013401140080008001 3c0158011401480000".replace(" ", ""))
    # decode_wide_codes recovers ASCII for Latin glyphs
    codes = [struct.unpack(">H", enc[i:i + 2])[0] for i in range(0, len(enc) - 2, 2)]
    assert sgtext.decode_wide_codes(codes) == "GAME  OVER"
    # kana glyph -> {gNNN}
    assert sgtext.decode_wide_codes([230 * 4]) == "{g230}"
    # find a NUL-bounded wide run
    blob = b"\x00\x00" + enc + b"\x00\x00"
    found = sgtext.find_wide_strings(blob)
    assert found and sgtext.decode_wide_codes(found[0][1]) == "GAME  OVER"
    # apply_wide_edits replaces in place, length-safe
    out = sgtext.apply_wide_edits(blob, [(2, "OK")])
    assert out[2:8] == sgtext.encode_wide("OK")[:6]
    try:
        sgtext.apply_wide_edits(blob, [(2, "THIS IS FAR TOO LONG FOR THE SLOT")])
        assert False, "expected overflow ValueError"
    except ValueError:
        pass


def test_sgtext_dialogue():
    """Dialogue: fixed-width wide encode (no terminator) + delimiter splitting."""
    from saturn_translate import sgtext
    e = sgtext.encode_wide_fixed("HI", 4)        # H,I + 2 space pad, code=ord*4, BE, no NUL
    assert e == bytes.fromhex("0120012400800080")
    # delimiter-separated lines (0xFFFF / 0x0000)
    blob = (sgtext.encode_wide_fixed("AB", 2) + b"\xff\xff"
            + sgtext.encode_wide_fixed("CD", 2) + b"\x00\x00")
    lines = sgtext.find_dialogue_lines(blob, 0, len(blob))
    assert [sgtext.decode_wide_codes(c) for _, c in lines] == ["AB", "CD"]

