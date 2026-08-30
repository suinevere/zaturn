# Emulator savestates

Mednafen savestates for the discs under `cd/`. Tracked deliberately: they are
the only record of specific debugger captures, and losing an uncommitted one
means replaying the game to recreate it.

Filenames embed a hash of the disc image, so every rebuilt/patched disc gets a
new set. Read them with `analysis/zork_ui_rip.py`:

- `savestate_blocks(path)` walks the variable table.
- `vdp1_vram(path)` returns VDP1 VRAM plus CRAM.
- The master SH-2 registers are a 0x40-byte `R` variable (sixteen 32-bit
  little-endian values) immediately preceding the master `PC`; `SysRegs[2]`
  holds `PR`, which at a function-entry breakpoint is the caller.

Work RAM is stored with each 16-bit word byte-swapped relative to the SH-2
view, so read `u16` with `<H` or swap adjacent bytes first.
