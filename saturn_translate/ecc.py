"""CD-ROM Mode 1 EDC/ECC recomputation for 2352-byte raw sectors.

When you patch a byte in a Mode 1/2352 disc image, that sector's error-detection
(EDC) and error-correction (ECC P/Q) fields no longer match the user data.
Emulators and lenient ODEs (Fenrir, Saroo) ignore the mismatch, but accurate
CD-block emulation (e.g. Terraonion MODE) can treat it as a read error and hang.
This module recomputes EDC/ECC so the patched sector is valid everywhere.

The algorithm is the canonical one from Neill Corlett's ecmtools (ECMA-130),
ported to Python. Sector layout (Mode 1, 2352 bytes):

    0x000  12   sync (00 FF*10 00)
    0x00C   4   header (min, sec, frame [BCD], mode=01)
    0x010 2048  user data
    0x810   4   EDC (CRC-32, little-endian, over bytes 0x000..0x80F)
    0x814   8   reserved (zero)
    0x81C 172   ECC P-parity
    0x8C8 104   ECC Q-parity
"""

from __future__ import annotations

SECTOR_SIZE = 2352
EDC_OFFSET = 0x810
P_OFFSET = 0x81C
Q_OFFSET = 0x8C8

# ── lookup tables (built once) ─────────────────────────────────────────
_ecc_f = [0] * 256
_ecc_b = [0] * 256
_edc = [0] * 256


def _init_tables() -> None:
    for i in range(256):
        j = (i << 1) ^ (0x11D if (i & 0x80) else 0)
        j &= 0xFF
        _ecc_f[i] = j
        _ecc_b[i ^ j] = i
        edc = i
        for _ in range(8):
            edc = (edc >> 1) ^ (0xD8018001 if (edc & 1) else 0)
        _edc[i] = edc & 0xFFFFFFFF


_init_tables()


def edc_compute(data: bytes) -> int:
    """CRC-32 EDC over ``data`` (init 0)."""
    edc = 0
    for b in data:
        edc = (edc >> 8) ^ _edc[(edc ^ b) & 0xFF]
    return edc & 0xFFFFFFFF


def _ecc_block(sector: bytearray, major_count: int, minor_count: int,
               major_mult: int, minor_inc: int, dest: int) -> None:
    size = major_count * minor_count
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        ecc_a = 0
        ecc_b = 0
        for _ in range(minor_count):
            temp = sector[0x0C + index]
            index += minor_inc
            if index >= size:
                index -= size
            ecc_a ^= temp
            ecc_b ^= temp
            ecc_a = _ecc_f[ecc_a]
        ecc_a = _ecc_b[_ecc_f[ecc_a] ^ ecc_b]
        sector[dest + major] = ecc_a
        sector[dest + major + major_count] = ecc_a ^ ecc_b


def fix_sector(sector: bytearray) -> bytearray:
    """Recompute EDC + ECC P/Q for a Mode 1 sector in place. Returns it.

    Assumes a valid 12-byte sync and 4-byte header are already present (they are,
    in a real dump); only the EDC/ECC fields are rewritten from the current
    user data + header.
    """
    if len(sector) != SECTOR_SIZE:
        raise ValueError(f"sector must be {SECTOR_SIZE} bytes, got {len(sector)}")
    if sector[0x0F] != 0x01:
        raise ValueError("not a Mode 1 sector (mode byte at 0x0F != 0x01)")

    # EDC over bytes 0x000..0x80F
    edc = edc_compute(bytes(sector[0:EDC_OFFSET]))
    sector[EDC_OFFSET + 0] = edc & 0xFF
    sector[EDC_OFFSET + 1] = (edc >> 8) & 0xFF
    sector[EDC_OFFSET + 2] = (edc >> 16) & 0xFF
    sector[EDC_OFFSET + 3] = (edc >> 24) & 0xFF

    # reserved 8 bytes must be zero for the ECC computation
    for k in range(0x814, 0x81C):
        sector[k] = 0

    # ECC P (86 x 24) and Q (52 x 43), header kept (Mode 1 uses real address)
    _ecc_block(sector, 86, 24, 2, 86, P_OFFSET)
    _ecc_block(sector, 52, 43, 86, 88, Q_OFFSET)
    return sector


def sector_is_valid(sector: bytes) -> bool:
    """True if the sector's stored EDC/ECC already match its data."""
    test = bytearray(sector)
    fix_sector(test)
    return bytes(test) == bytes(sector)


def fix_image_at(image: bytearray, byte_offset: int) -> int:
    """Recompute EDC/ECC for the 2352 sector containing ``byte_offset``.

    ``image`` is mutated in place. Returns the sector index that was fixed.
    """
    sector_idx = byte_offset // SECTOR_SIZE
    start = sector_idx * SECTOR_SIZE
    sector = bytearray(image[start:start + SECTOR_SIZE])
    fix_sector(sector)
    image[start:start + SECTOR_SIZE] = sector
    return sector_idx
