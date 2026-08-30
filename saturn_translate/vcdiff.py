"""Minimal multi-window VCDIFF (xdelta3-compatible) encoder for ROM patches.

Produces standard RFC 3284 VCDIFF output that xdelta3 / DeltaPatcher accept, with
no secondary compression. Designed for ROM/disc patches: you supply the source
bytes and a list of edits, and it emits a patch that COPYs the unchanged runs
from the source and ADDs the changed bytes.

xdelta3 enforces a hard maximum window size (``XD3_HARDMAXWINSIZE``); a single
window spanning a whole 500 MB disc is rejected with
"hard window size exceeded: XD3_INVALID_INPUT". So the target is split into
windows of ``window_size`` bytes (default 8 MB, xdelta3's own default), each its
own VCD_SOURCE window copying from the matching source region. Output is validated
by round-trip: :func:`decode` reproduces the patched target exactly.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

VCD_MAGIC = b"\xD6\xC3\xC4\x00"  # 'V'|0x80,'C'|0x80,'D'|0x80, version 0
OP_ADD0 = 1       # ADD, size coded separately
OP_COPY0 = 19     # COPY, mode 0 (SELF), size coded separately

DEFAULT_WINDOW = 1 << 23  # 8 MiB, xdelta3 default; well under the hard max


@dataclass
class Edit:
    """Replace ``old_len`` bytes at ``offset`` with ``data`` (in the source)."""
    offset: int
    old_len: int
    data: bytes


def _enc_int(n: int) -> bytes:
    """RFC 3284 variable-length integer: base-128, most-significant byte first."""
    if n < 0:
        raise ValueError("negative integer")
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def encode(source: bytes, edits: list[Edit], window_size: int = DEFAULT_WINDOW) -> bytes:
    """Return a multi-window VCDIFF patch transforming ``source`` per ``edits``."""
    edits = sorted(edits, key=lambda e: e.offset)
    src_len = len(source)

    # Build an op stream over the target: COPY(src_pos,len) / ADD(bytes).
    ops: deque = deque()
    pos = 0
    target_len = 0
    for e in edits:
        if e.offset < pos:
            raise ValueError(f"overlapping edit at {e.offset}")
        if e.offset > pos:
            ops.append(("C", pos, e.offset - pos))
            target_len += e.offset - pos
        if e.data:
            ops.append(("A", e.data))
            target_len += len(e.data)
        pos = e.offset + e.old_len
    if pos < src_len:
        ops.append(("C", pos, src_len - pos))
        target_len += src_len - pos

    out = bytearray(VCD_MAGIC)
    out.append(0x00)  # Hdr_Indicator: no secondary compressor, no app header

    tpos = 0
    while tpos < target_len:
        wlen = min(window_size, target_len - tpos)

        # Pull ops covering exactly wlen bytes, splitting at the boundary.
        win_ops = []
        filled = 0
        while filled < wlen:
            op = ops.popleft()
            if op[0] == "C":
                _, sp, ln = op
                take = min(ln, wlen - filled)
                win_ops.append(("C", sp, take))
                if take < ln:
                    ops.appendleft(("C", sp + take, ln - take))
                filled += take
            else:
                _, d = op
                take = min(len(d), wlen - filled)
                win_ops.append(("A", d[:take]))
                if take < len(d):
                    ops.appendleft(("A", d[take:]))
                filled += take

        copies = [o for o in win_ops if o[0] == "C"]
        if copies:
            seg_min = min(o[1] for o in copies)
            seg_max = max(o[1] + o[2] for o in copies)
        else:
            seg_min = seg_max = 0
        seg_len = seg_max - seg_min

        data = bytearray()
        instr = bytearray()
        addrs = bytearray()
        for o in win_ops:
            if o[0] == "C":
                _, sp, ln = o
                instr.append(OP_COPY0)
                instr += _enc_int(ln)
                addrs += _enc_int(sp - seg_min)   # mode 0: address within source segment
            else:
                _, d = o
                instr.append(OP_ADD0)
                instr += _enc_int(len(d))
                data += d

        delta_body = (
            _enc_int(wlen) + b"\x00"
            + _enc_int(len(data)) + _enc_int(len(instr)) + _enc_int(len(addrs))
            + bytes(data) + bytes(instr) + bytes(addrs)
        )
        if seg_len > 0:
            window = (b"\x01" + _enc_int(seg_len) + _enc_int(seg_min)
                      + _enc_int(len(delta_body)) + delta_body)
        else:
            window = b"\x00" + _enc_int(len(delta_body)) + delta_body
        out += window
        tpos += wlen

    return bytes(out)


def decode(source: bytes, patch: bytes) -> bytes:
    """Apply a patch produced by :func:`encode` (used for self-verification)."""
    def dec_int(b: bytes, i: int) -> tuple[int, int]:
        n = 0
        while True:
            x = b[i]; i += 1
            n = (n << 7) | (x & 0x7F)
            if not (x & 0x80):
                return n, i

    assert patch[0:4] == VCD_MAGIC and patch[4] == 0
    i = 5
    out = bytearray()
    while i < len(patch):
        win_ind = patch[i]; i += 1
        if win_ind & 0x01:        # VCD_SOURCE
            seg_len, i = dec_int(patch, i)
            seg_pos, i = dec_int(patch, i)
            seg = source[seg_pos:seg_pos + seg_len]
        elif win_ind & 0x02:      # VCD_TARGET
            seg_len, i = dec_int(patch, i)
            seg_pos, i = dec_int(patch, i)
            seg = out[seg_pos:seg_pos + seg_len]
        else:
            seg = b""
            seg_len = 0
        _dlen, i = dec_int(patch, i)
        _tlen, i = dec_int(patch, i)
        assert patch[i] == 0; i += 1
        ld, i = dec_int(patch, i)
        li, i = dec_int(patch, i)
        la, i = dec_int(patch, i)
        dsec = patch[i:i + ld]; i += ld
        isec = patch[i:i + li]; i += li
        asec = patch[i:i + la]; i += la

        win = bytearray()
        di = ii = ai = 0
        while ii < len(isec):
            op = isec[ii]; ii += 1
            if op == OP_COPY0:
                size, ii = dec_int(isec, ii)
                a, ai = dec_int(asec, ai)
                if a < seg_len:
                    win += seg[a:a + size]
                else:
                    start = a - seg_len
                    win += win[start:start + size]
            elif op == OP_ADD0:
                size, ii = dec_int(isec, ii)
                win += dsec[di:di + size]; di += size
            else:
                raise ValueError(f"unsupported opcode {op}")
        out += win
    return bytes(out)
