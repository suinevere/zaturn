# Steamgear Mash — 3D Control Pad support recon

Goal: replicate the **Touge King the Spirits** "3D Pad" patch technique for Steamgear
Mash. Touge's patch is a code-cave hook on the per-frame pad routine that **synthesizes
digital D-pad/button presses from the analog 3D Control Pad** (gated on the analog-pad
ID), so a game that only reads digital input responds to the analog stick.

## Reference: how the Touge King patch works

Distributed as a Sega Saturn Patcher `.ssp` (renamed ZIP) — see
`game_originals/Touge King the Spirits (Japan)/Touge KING The Spirits 3D Pad.zip`.
Changes exactly one file: `MON\RACE.BIN` (the in-race overlay, 512000 bytes, load base
`0x06003000`). The diff is a `BSDIFF40` (`RACE.BIN._DFR`).

Two regions:
1. **Hook** at file `0x3B8A0` / addr `0x0603E8A0` — overwrites a function epilogue with
   `mov.l @(pc),r2; jmp @r2; nop; nop; .long 0x0604F57C` (jump to the cave).
2. **Cave** at file `0x4C57C` / addr `0x0604F57C` (124 bytes in trailing zero padding).
   It computes `entry = padbase + (port_index+2)*stride` from three game globals, then
   **only acts if `entry[1] == 6`** (the 6-byte analog report = 3D pad connected) and
   OR-injects digital bits into the digital-direction byte `entry[2]` (active-low):

   | 3D-pad input | Condition | Digital bit cleared in `entry[2]` |
   |---|---|---|
   | `entry[3]` bit `0x08` (R trigger) | pressed | `0x10` (Up) |
   | `entry[3]` bit `0x80` (L trigger) | pressed | `0x20` (Down) |
   | `entry[4]` analog X | `< 0x68` | `0x40` (Left) |
   | `entry[4]` analog X | `>= 0x98` | `0x80` (Right) |

   Center `0x80` ± 24 = deadzone. Ends by replaying the 6 displaced epilogue bytes
   (`lds.l @r15+,macl; mov.l @r15+,r12/r13; rts; mov.l @r15+,r14`) so control returns
   normally. Three trailing literals hold the global addresses
   (`0x060772D4` port index, `0x060772DC` stride, `0x060772E8` pad-base pointer).

A reusable bspatch + diff-region tool is in `_tmp_touge/` (not committed):
`bspatch.py`, `diffregions.py`.

## Steamgear Mash — findings (`game_originals/Steamgear Mash (Japan)/0.BIN`)

`0.BIN` = main executable, **load base `0x06004000`**, size `0xECFAA` (970666 B),
spans `0x06004000`–`0x060F0FAA` (fits in 1 MB HWRAM). Built with **SGL** (the Sega
peripheral library is linked in but Ghidra left it undecompiled — no `FUN_060dc*`
names). Game logic in `0.c` reads **no `0x060ef*` global directly** — consistent with
the function-pointer dispatch noted elsewhere in this project that defeats static
tracing.

### Pad / SMPC subsystem (the "pad-read routine")

- **SGL Per library** occupies `~0x060DB470`–`0x060DCD70`. Library accessors
  `FUN_060db470 / 060db538 / 060db5a0 / 060db684 / 060db750 / 060dbfb8` are called from
  all over the game. (`FUN_060db538`, the most frequent call, is actually an SGL
  **frame-sync** — set flag + busy-wait on `0x060ee54c`–`0x060ee560` — *not* the pad
  read.)
- **Per-frame "collect all peripherals" = `FUN_060dc342`**, called by `FUN_060dd044`.
  This is the SGL INTBACK-result processor and the **structural analogue of Touge's
  hooked routine**. Prologue at `0x060dc342` loads its **peripheral data-buffer base
  = `0x060efbb4`** (r12, from literal `@0x060dc394`). It branches on a mode byte and
  writes 2-byte-per-port entries into the buffer.
- Low-level INTBACK handler reads **SMPC SR `0x20100061`** (literal `@0x060dc498`);
  also references `0x2010005F`/`0x20100063` near file `0xEA574`.
- **Peripheral state/data block = `0x060efb5C`–`0x060efbE0`** in HWRAM (the buffer base
  `0x060efbb4` lives here). `0x060efb4c` is shared with the CD-block code — *not* pad.
- Note: the `0x06008xxx` cluster that also touches SMPC-looking constants is the
  **CD-block file loader** (BCD TOC conversion, `SG_MASH_001` volume label, HIRQ
  handshakes), not pad input.

**Hook target (recommended):** the tail of `FUN_060dc342` (or `FUN_060dd044`), mirroring
Touge — read the analog bytes from the SGL 3D-pad record in the `0x060efbb4` buffer,
synthesize digital bits, gate on the analog-pad ID. The exact per-port record layout
(stride, offset of id / digital byte / analog axes within the buffer) and the precise
read site still need confirmation — fastest via the project's proven **Mednafen/Yaba
watchpoint** (PC breakpoint on `0x20100061` read / write-watch on `0x060efbb4`) when
input is first polled.

### Code cave

⚠️ **The large zero regions are NOT free** — they're SGL/system BSS:
- `0x0605FDE0`–`0x060D39A4` (490 KB) — work/decompression buffer.
- `0x060EE580`–`0x060ECD34` (10 KB) — **SGL BSS**: holds the peripheral data buffer
  (`0x060efbb4`) and sync flags (`0x060ee54c`). Do not use.

✅ **Safe caves = inter-function alignment padding in the code section:**

| file | addr | bytes |
|---|---|---|
| `0x049816` | `0x0604D816` | 105 |
| `0x04A11C` | `0x0604E11C` | 98 |
| `0x049758` | `0x0604D758` | 92 |
| `0x046C90` | `0x0604AC90` | 51 |
| `0x04550A` | `0x0604950A` | 53 |

Touge's routine was 124 B; Steamgear (an action/platformer) likely needs only
stick→4/8-direction mapping, which is smaller. **Best single cave: `0x0604D816`,
105 bytes.** If more is needed, chain `0x0604D758` (92) + `0x0604D816` (105), or split
the routine.

## Next steps
1. Watchpoint to confirm the per-port record layout in the `0x060efbb4` buffer (id /
   digital byte(s) / analog axis offsets) and the exact read site to hook.
2. Decide the mapping (likely analog stick X/Y → D-pad, with deadzone).
3. Write the cave at `0x0604D816`, hook the tail of `FUN_060dc342`, replay displaced
   instructions, gate on the analog-pad ID.
4. Build/diff, verify against ground truth (emulator + hardware ODE), recompute EDC/ECC
   if shipping MODE1/2352, distribute as `.ssp`.
