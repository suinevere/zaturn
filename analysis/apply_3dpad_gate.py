#!/usr/bin/env python3
"""Steamgear Mash — make the 3D Control Pad's ANALOG MODE work (digital buttons first).

ROOT CAUSE (pinned by savestate + static analysis, supersedes iter-1/iter-2):
  The 3D Control Pad in analog mode reports peripheral ID 0x16 and DOES deliver the
  digital d-pad / face / shoulder bits in the normal button byte (proven: in analog mode
  the SMPC OREG d0 and the game's pad buffer hold ef=UP, df=DOWN, bf=LEFT, 7f=RIGHT —
  byte-identical to digital mode). Nothing about the analog stick was ever the blocker.

  The game's HIGH-LEVEL input decoder zeroes the input for an analog pad. At 0x06004090:
    060040c0: mov.b @(4,r2),r0     ; padrec[+4] = analog-mode flag (0x10 analog / 0x00 digital)
    060040c8: bf  0x06004110       ; if flag != 0  -> SKIP decode, write 0 to the input word
    060040ca: ... *0x060834CC = ~padrec[+0]   ; (digital decode: active-low raw -> active-high)
  i.e. Steamgear Mash (1995) predates the 3D Control Pad (1996); its input code treats the
  unknown "analog" peripheral type as "no input" instead of reading the buttons it already has.
  The decoded word 0x060834CC is copied to the master input var 0x060769C4 read game-wide.

THE FIX: NOP the two peripheral-type gate branches (port 0 and port 1) so the existing
  digital decode runs for the analog pad too. Surgical, no code cave, no trampoline, no
  SMPC-interrupt hook (that path broke all input in iter-2). Two halfwords change, both in
  0.BIN's first 2048-byte sector.

    file 0x0C8  (HWRAM 0x060040C8):  8B22 (bf 0x06004110)  -> 0009 (nop)   ; port 0 gate
    file 0x128  (HWRAM 0x06004128):  8B0D (bf 0x06004146)  -> 0009 (nop)   ; port 1 gate

Result: in analog mode all the pad's DIGITAL buttons (d-pad, face, shoulders) work, exactly
as in digital mode. (Analog-stick -> d-pad synthesis is a separate later step; the master
input word is now honored, so that step becomes a small additive hook.)
"""
import sys, struct

GATES = [
    (0x0C8, 0x8B22, "port 0 analog-type gate (bf 0x06004110)"),
    (0x128, 0x8B0D, "port 1 analog-type gate (bf 0x06004146)"),
]
NOP = 0x0009

def build(in_bin, out_bin):
    b = bytearray(open(in_bin, "rb").read())
    for off, expect, desc in GATES:
        got = struct.unpack_from(">H", b, off)[0]
        assert got == expect, f"@0x{off:X}: expected {expect:04X} ({desc}), found {got:04X} — wrong 0.BIN?"
        struct.pack_into(">H", b, off, NOP)
        print(f"  patched 0x{off:03X} (HWRAM 0x{0x06004000+off:08X}): {expect:04X} -> {NOP:04X}  [{desc}]")
    open(out_bin, "wb").write(b)
    print(f"wrote {out_bin} ({len(b)} bytes); {len(GATES)} gate branch(es) NOPed.")

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "game_originals/Steamgear Mash (Japan)/0.BIN"
    dst = sys.argv[2] if len(sys.argv) > 2 else "game_patched/steamgear_mash_3dpad/0_3dpad.BIN"
    build(src, dst)
