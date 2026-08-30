# Myst (USA, Saturn) — 3D Control Pad support recon

> **STATUS: ANALOG PAUSED (2026-06-20).** Digital 3D-pad already works natively (D-pad moves
> cursor). Analog stick blocked: Myst never stages the analog (0x16) batch in OREG/RAM
> per-frame (see "WALL" below). Resuming requires modifying Myst's INTBACK. All cursor/write/
> hook/tooling work is solved and documented; pick up here if revisited.

Goal (user): add **3D Control Pad analog-stick** cursor control to Myst (which already
has mouse + digital-pad support), then add **D-pad accelerated movement** ("quick jumps"
around the screen). Same class of work as the Steamgear Mash 3D-pad patch — input-loop
RE, no translation/codec work.

## Disc / image facts

- `game_originals/Myst (USA)/` — `Track 1.bin` (MODE1/2352, 585 MB) + `Track 2.bin` (AUDIO) + `.cue`.
- IP header: HWID `SEGA SEGASATURN`, Maker `SEGA TP T-81`, Product **T-8101H**, V1.001,
  date 19950707, area **U**, device CD-1/1.
- **Declared peripherals: `JM`** = J (Control Pad) + M (Mouse). **No `E` (analog/3D pad)** —
  this is exactly why the 3D pad's analog stick does nothing; only its digital mode is seen.
- IP load params: master stack `0x2607fffc`, **1st read addr `0x06010000`**.

## Main executable

- **`/A.BIN`** (426344 bytes) is the main program. Extracted to `work/myst/A.BIN`.
  - **Load base = `0x06010000`.** Entry trampoline at file 0x00: set R15=`0x0607fffc`,
    `JMP 0x06010014`. So file offset N ↔ address `0x06010000 + N`.
- `/SEGA` (2.17 MB) also present (likely additional engine/data; extraction pending — not
  yet confirmed to hold input code).
- Other dirs: `/CH` (CH*.DAT node images), `/CPK` (Cinepak FMV), `/COM` `/SND` (audio),
  `/MY /DU /ME /ST /SE` (per-Age data) + `*_DATA.BIN`.

## Input acquisition (FOUND, static)

SMPC peripheral read routine at **`0x06047fa0`–`0x06048060`** (file 0x37fa0). It reads SMPC
output registers and copies them into a RAM peripheral buffer:

| SMPC reg (addr)        | → RAM dest (base `0x0605ad64`) |
|------------------------|--------------------------------|
| OREG8  `0x20100031`    | dest[0]                        |
| OREG9  `0x20100033`    | dest[1]                        |
| OREG10 `0x20100035`    | dest[2]                        |
| OREG11 `0x20100037`    | dest[3]                        |
| OREG12 `0x20100039`    | dest[4]                        |
| OREG13 `0x2010003b`    | dest[5]                        |
| OREG14 `0x2010003d`    | dest[6]                        |
| OREG15 `0x2010003f`    | dest[7]                        |
| OREG0  `0x20100021`    | dest[8]                        |

Other SMPC use nearby: COMREG `0x2010001f` (file 0xbfb4), SF `0x20100063`, SR `0x20100061`
(file 0x37ecc), IREG0 `0x20100001`, DDR1 `0x20100079` (file 0x37ba4) — i.e. this code does a
manual SMPC INTBACK / direct-mode peripheral poll, not pure SGL `Per[]`.

### Input RAM buffer / state

- `0x0605ad60` — base pointer used by consumers (refs at `0x0601b1cc` lit-pool, `0x06047cdc`,
  `0x06047eb8`). **The cursor/mouse logic reads input through this base.**
- `0x0605ad64` — raw OREG snapshot (written above; 1 writer only).
- `0x0605ad70` — refs `0x06047bb8`, `0x0604808c`.
- `0x0605ada0` — flag/state (refs `0x06047bc0`, `0x06048090`, `0x06048ddc`); the routine at
  ~`0x06048048` tests bit7 of a byte and reverse-copies 7 bytes — peripheral-type handling
  (mouse vs pad reformatting).
- `0x0605adb0` — refs `0x06047cd8`, `0x06047eb4`, `0x06048094`.

## CURSOR LOCATED (savestate diff, 2026-06-19)

Method: 8 Mednafen savestates (`mcs/Myst (USA).e6e69c28….mc1`–`mc8`). Mapping —
mc1=D-pad↑, mc2=↓, mc3=←, mc4=→; mc5=analog↑, mc6=↓, mc7=←, mc8=→. States are
**gzip-compressed**; decompress → `MDFNSVST`; **HWRAM is stored 16-bit byte-swapped**.
Anchor HWRAM by finding the A.BIN input-routine signature (file 0x37fa0) under a swap16:
`HWRAM(0x06000000)` = `state_off(sig) − 0x47fa0`, then `swap16` the 1 MB region.
Helper: `analysis/myst_savestate.py` (to be added) — `load_hwram(n)` returns corrected RAM.

- **Cursor X = `0x06078000`** (u16, 0..319): left→0, right→319; vertical moves hold X.
- **Cursor Y = `0x06078002`** (u16, 0..~223): up→0, down→181; horizontal moves hold Y.
  (Clean adjacent struct at `0x06078000`, inside A.BIN's data tail — file off 0x68000.)
- **Analog stick moves nothing** (CONFIRMED): cursor identical (319,181) in all of mc5–mc8.
  So the analog data is not currently consumed — patch must add the read + the cursor feed.
- **Raw analog bytes are NOT in the HWRAM input buffer** (`0x0605ad64`) cleanly across mc5–8,
  implying Myst's SMPC INTBACK doesn't request analog data → the bytes sit in SMPC OREG only.
  **Open:** confirm whether the patch must also change the INTBACK command to pull analog
  data, or whether OREG already holds it (check SMPC state / read routine at `0x06047fa0`).
- **Cursor-write code:** the cursor addrs appear as **literal-pool constants** referenced by
  ~6 functions (pools at/near `0x06010a70`, `0x06011f44`, `0x06016030`, `0x0601292c`,
  `0x0601d21c`). The D-pad→cursor updater is one of these. Fastest disambiguation now =
  **write-watchpoint on `0x06078000`** (we have the address) → PC of the updater = the hook
  site for both deliverables.

## CURSOR MOVER LOCATED (watchpoint + static, 2026-06-19)

Write-watchpoint on `0x06078000` trips **every frame** (cursor struct rewritten each frame).
Break PC `0x06010458` with **R8 = `0x06078000`** = the store helper; the real logic is its
caller. Struct @`0x06078000`: **X = +0 (u16), Y = +2 (u16)**; e.g. mc4 = `01 3f 00 b5` = (319,181).

- **Mover function = `~0x0601d144`** (loads `&cursor`→R8 at `0x0601d162`). Cursor is kept in
  **8.8 fixed-point**; clamp block `~0x0601d420–0x0601d490` uses literals `0x00013f00` (319≪8
  = max X), `0x0000b500` (181≪8 = max Y), `0x00006500` (101≪8). It calls the store helper at
  `0x06010458` which writes the integer X/Y back to `0x06078000/2`.
- **THE HOOK — delta application at `0x0601d1f6–0x0601d200`:**
  `r6`=ptr to cursor working coord (8.8 FP); `r13`=direction index; step table @`sp+28`,
  bound table @`sp+52`. Core: `r1=*r6; r1 += step; *r6 = r1` then `cmp/gt` clamp.
  - **Acceleration / quick-jumps** = scale `step` (8.8 FP, ideal for sub-pixel ramp).
  - **Analog stick** = add a stick-derived term to `step`/`*r6` here.
- Input state pointer `r7` = **`0x06045610`**: button words at `@r7` / `@(2,r7)`, type/flag
  byte `@(4,r7)` (`cmp/eq #0x20` gate at `0x0601d1ac` → skip to `0x0601d466`).
- Cursor **working coordinate** ptr `r6` = **`0x0604584c`** (32-bit; the display copy at
  `0x06078000` is derived from it). Related cursor globals: `0x06045844/48/4c/50/54`.

### D-pad acceleration ALREADY EXISTS (user-confirmed + verified, 2026-06-19)
`r13` = **`0x06078086`** is an **acceleration-level counter** (held-duration). The loop does
`idx = *0x06078086; step = stepTable[idx]` (stepTable @`sp+28`) and `snapTable` @`sp+40`,
`boundTable` @`sp+52` — so longer hold → higher level → bigger step = built-in momentum.
Level read =2 in all captured held states. **=> Deliverable #2 (D-pad accel) is native to
Myst.** To make it *more* aggressive would mean patching the step-table build or raising the
level cap; otherwise nothing to add here. The real remaining feature is the **analog stick**.

**Still open for analog:** the stick value isn't in the HWRAM input buffer (Myst's INTBACK
doesn't request analog), so the patch must source analog X/Y — either widen the SMPC INTBACK
to pull analog bytes, or read the 3D-pad analog OREG directly — then feed it at the hook.
Mednafen note: write-watchpoint key is **CTRL+W** (paste addr) in this build; it fires every
frame because the mover runs unconditionally (step 0 when idle).

## ANALOG SOURCE FOUND — feature fully unblocked (2026-06-19)

Read the SMPC section of the analog savestates (mc5–8). The 3D-pad analog data **is present in
OREG** after Myst's own poll — Myst just never reads it. Port-1 peripheral data block (verified
byte-for-byte, `f1 16 ff ff X Y 00 00`):

| OREG | addr | value |
|------|------|-------|
| OREG0 | 0x20100021 | 0xf1 port status |
| OREG1 | 0x20100023 | 0x16 = 3D pad, **analog mode** |
| OREG2/3 | 25/27 | 0xff 0xff digital buttons |
| **OREG4** | **0x20100029** | **analog X** (0x00 L ─ 0x80 mid ─ 0xff R) |
| **OREG5** | **0x2010002b** | **analog Y** (0x00 U ─ 0x80 mid ─ 0xff D) |
| OREG6/7 | 2d/2f | R / L analog triggers |

(Confirmed across mc5–8: X +0x08e 0x80/0x80/0x00/0xff, Y +0x08f 0x00/0xff/0x80/0x80 for
up/dn/lf/rt.) Matches the OREG addrs from the Steamgear 3D-pad work. **No INTBACK change
needed** — analog already in OREG; Myst's poll (`0x06047fa0`) reads OREG8–15+OREG0 only, so it
ignores OREG2–7. To detect presence, check OREG1==0x16.

### Patch design (analog stick → cursor)
- **Capture:** in/after Myst's per-frame poll (`0x06047fa0` routine), if OREG1==0x16 read
  OREG4/OREG5, apply a deadzone around 0x80, scale to a per-frame delta, stash to a free
  HWRAM scratch (e.g. unused bytes near the cursor struct). (OREG persists between INTBACKs,
  but capturing in the poll where OREG is known-valid is safest.)
- **Feed:** add the analog delta into the cursor **working coordinate** `0x0604584c` (X) and
  its Y counterpart, reusing the mover's existing clamp (bounds 319/181, 8.8 FP) so units &
  bounds stay consistent. Cleanest hook = the mover (`0x0601d144`), independent of the D-pad
  bit test (stick sets no D-pad bits), so analog moves even with no D-pad held.
- **Build:** relocated/composable (new code in free space, branch-hook), recompute EDC/ECC,
  ship `.ssp`+xdelta+IPS. Declare `E` in IP peripherals (`JM`→`JME`) so 3D pad is listed.

### Still to map before writing bytes
1. The mover's **Y working-coord** address + the exact clamp site (X coord = `0x0604584c`;
   need the Y equiv among `0x06045844/48/50/54`).
2. A safe **free-RAM scratch** + a **relocation target** (free space in A.BIN's image).
3. Confirm working-coord units (integer vs 8.8 FP) so the analog delta scale matches.

## PATCH DRAFTED + assembly-verified (2026-06-19)

Builder: `analysis/myst_build_3dpad.py` (mini SH-2 assembler → `work/myst/A.patched.BIN`,
round-trip-verified via `saturn_translate.sh2`). All addresses confirmed above.
- **Trampoline @0x0601d144** (12 B): `mov.l @(1,pc),r0; jmp @r0; nop; nop; .long 0x0602799c`.
  Overwrites the 6 prologue pushes (r14..r9), which the routine replays before returning.
- **Routine @0x0602799c** (196 B; uses r0–r4 only): if `*0x20100023==0x16`, for each axis
  `v=*OREG-0x80`; if `|v|<=0x18` → 0 else `v<<3`; `coord=*work+v`; clamp `[0,max]`; store
  work (8.8 FP) + display (`>>8`). X: OREG 0x29, work 0x06045844, disp 0x06078000, max 0x13f00.
  Y: OREG 0x2b, work 0x06045848, disp 0x06078002, max 0xdf00. Returns to `0x0601d150`.
- **Tunables:** `DEADZONE=0x18`, `GAIN_SHIFT=3` (~4 px/frame full deflection), `YMAX=0xdf00`
  (223 — lower if the cursor over-travels a status bar).

### Remaining to ship/test
1. Flip IP peripheral string `JM`→`JME` (in IP.BIN / track sector, NOT A.BIN) so the 3D pad
   is advertised.
2. Inject `A.patched.BIN` into a copy of Track 01, **recompute EDC/ECC** (`ecc.py`), rebuild
   `.cue` → test in Mednafen (3D pad, analog mode) then hardware.
3. Tune deadzone/gain/YMAX on real input; verify analog + D-pad + mouse coexist and that
   hotspot-snap doesn't fight the stick.
4. Package: `.ssp` (changed A.BIN) + xdelta + IPS + readme (credit Suinevere Pendragon).

## TEST 1 RESULT (2026-06-19): hook+write CONFIRMED, OREG read is the bug
Diagnostic build (`MYST_DIAG_DRIFT=256`: unconditional X+=1px/frame at the mover hook) →
**cursor drifts right on hardware/emu.** So the trampoline fires every frame and writing
`0x06045844`(work)+`0x06078000`(disp) moves the cursor. The analog build failed solely
because **OREG does not hold the analog peripheral data at mover time** (gate
`*0x20100023==0x16` fails → routine skips).

Also learned: Myst's input struct `0x06045610` is **digital-only** (byte0 active-low
U/D/L/R = 0x10/20/40/80; `0x0604562e` = pressed-dir mask); analog states leave it 0xff/0x00.
And `0x0605ad64` (the OREG-copy buffer from the `0x06047fa0` routine) is **all zeros** — not
the live path. So analog must be captured from OREG **inside the actual controller-poll
routine** (where OREG is valid), stashed to scratch RAM, and read at the mover hook — OR the
poll routine synthesizes D-pad bits from the stick so Myst's own mover moves the cursor.

**Next:** find the live controller-poll (read-watchpoint on OREG2 `0x20100025` / analog-X
`0x20100029`); it may be in BIOS ROM (PC < 0x00080000) → then read analog from the BIOS
peripheral struct in RAM instead.

## TEST 2 RESULT + redesign (2026-06-19): OREG stale at mover → hook the POLL
DIAG_ABS (cursor = OREG analog, absolute, at mover hook) → cursor **resets to a fixed stock
position, ignores stick**. So OREG holds stale/constant data at mover time. Confirmed further:
exact-match search for the analog tuple (X `80 80 00 ff`, Y `00 ff 80 80`) across fresh
analog savestates found **zero** clean copies in HWRAM — the poll leaves no durable raw-analog
copy in game RAM (the only clean pair sits in Mednafen's SMPC/device state, not SH-2 RAM).

**Redesign (v0.2): single hook inside the controller poll**, where the read-watchpoint proved
OREG is valid (PC `0x06048034`, R0=`0x20100021`, reads analog X at `0x20100029`).
- **Hook = `0x0604803e`** (poll loop-exit). 10-byte trampoline `mov.l @(1,pc),r0; jmp @r0;
  nop; .long 0x0602799c` — fits `0x0604803e..0x06048047`, does NOT touch jump target
  `0x06048048`. Displaced 5 insns (`r2=0x0605ada0; mov.b r13,@r2; mov #2,r1; mov.b r1,@r11;
  bra 0x06048580`) are replayed by the routine; live r11/r13 preserved.
- Routine pushes r0–r4, gates on `OREG1==0x16`, reads OREG4/5 directly (valid here), applies
  deadzone+gain, moves cursor working+display coords, pops, replays, `jmp 0x06048580`.
- Builder updated (`analysis/myst_build_3dpad.py`); round-trip verified. **Awaiting hardware
  test.**

## TEST 3 + root cause (2026-06-19): live OREG stale at BOTH hooks; read the poll buffer
Debug builds (record OREG1/4/5+counter to scratch 0x06027c60) — **read from the correct
savestate (newest mtime; patched rebuilds change the disc hash → new save filename!)**:
- Mover hook: counter=180 (fires), but OREG = ID `0x20`, `0x19/0x18` (stale; ≈ DIAG_ABS stock).
- Poll hook `0x0604803e`: counter=216 (fires!), but **live OREG also stale** (`0x20`).
So live OREG is stale at every reachable hook. BUT the poll **loop** (watchpoint @0x06048034)
reads the analog into the buffer `*(0x0605adb0)` ([6]=ID, [3]=analog X, [2]=analog Y); the
3D-pad's batch (digital+analog together, ID `0x16`) is read every frame for the D-pad.

**v0.3 fix:** poll hook reads the **buffer** `*(0x0605adb0)` (not live OREG): gate `buf[6]==0x16`,
analog X=`buf[3]`, Y=`buf[2]`. Loop start index r9 varies (5 iters for analog batch → buf[2..6]
filled). Builder updated + round-trip verified; disc rebuilt. **Awaiting test.**

> Workflow note: Mednafen savestate files embed a disc-image hash → each rebuild lands saves
> under a new `…<hash>.mcN`; always read the newest-mtime save, not a fixed filename.

## TEST 4 (2026-06-19): analog batch NOT read per-frame — likely root obstacle
DEBUG_BUF at poll hook `0x0604803e` (read newest-mtime save, hash per build): fires=113
(right)/17(neutral), but **sawAnalog (buf[6]==0x16) = 0** — `buf[6]` is always `0x20`, never the
analog pad ID `0x16`. SMPC ground truth at the same instant = ID `0x16`, X `0xff` (pad HAS the
data). So Myst's per-frame poll reads a different INTBACK stage/"0x20 batch", **not the analog
pad's 0x16 data**; the `0x06048034` analog read the watchpoint caught was a one-off (controller
acquisition), not per-frame.

**Implication:** Myst never fetches the analog axes during normal play, so no per-frame
buffer/OREG source exists to read. Remaining options are heavier:
1. **Modify Myst's INTBACK request** so it fetches the analog pad's full data every frame, then
   read the analog from the resulting buffer. (Needs RE of the INTBACK setup — IREG/COMREG at
   `0x0601bfb4`/`0x06047xxx`.)
2. **Issue an independent SMPC peripheral read** in a per-frame hook (handshake/timing risk,
   may conflict with Myst's own SMPC use).
Pending decisive check: read-watchpoint on `0x20100029` during *steady* gameplay — does it
break per-frame (→ a per-frame analog read exists to hook) or not (→ option 1/2 required)?

Note: Myst already supports the 3D pad's **digital** mode (D-pad moves the cursor); only the
analog stick is missing.

## v0.4 — in-loop capture (2026-06-19)
Watchpoint on `0x20100029` repeatedly fires during steady play → analog **is** read every frame
at `0x06048034`, but the buffer is overwritten by later batches (so every stable read shows the
non-analog `0x20` batch). Only a hook **inside the loop, while OREG holds the 0x16 batch**, can
catch it. Two-hook design:
- **Capture** @ `0x06048038` (loop tail; store@`0x06048036` runs first) → routine @ `0x06027b00`:
  if live `OREG1==0x16` latch OREG4/5 → scratch `0x06027c60/61`, set flag `0x06027c62`; then
  replay loop control (`mov #7,r3; cmp/hs r3,r4; loop→0x06048024 else exit`) + the exit code
  (`r2=0x0605ada0; *r2=r13; r1=2; *r11=r1; →0x06048580`). Loop reloads r0–r3 each iter so only
  r4/r11/r13 must be preserved (they are).
- **Mover** @ `0x0601d144` → routine @ `0x0602799c`: if flag set, read scratch X/Y, deadzone+gain,
  move cursor. Builder round-trip verified; disc rebuilt. **Awaiting test.**

## v0.4 RESULT + WALL (2026-06-20): analog never staged in OREG per-frame
Loop-hook capture (0x06048038) fires (total 7–53), but **sawAnalog(OREG1==0x16)=0**, and the
recorded live values are **OREG1=0x20 AND OREG4=0x20** (both constant, do NOT track the stick;
RIGHT → OREG4=0x20 not 0xff). So during normal play Myst reads OREG every frame but the SMPC has
only a non-analog "0x20 batch" staged; the 3D-pad's 0x16 analog batch is fetched only
occasionally (controller acquisition). The watchpoint "repeatedly firing" on 0x20100029 only
meant the address is *read* each frame (value 0x20), not that analog is live.

**Conclusion:** there is **no per-frame source** of the analog axes in Myst — neither RAM nor
OREG. Every per-frame hook (mover, poll-exit, poll-loop) sees the 0x20 batch. Confirmed across
~6 hooks/15 build-test cycles. All the *rest* is solved (cursor at 0x06078000/0x06045844 (8.8FP),
write path proven via drift/abs diagnostics, hook mechanics, savestate-diff + assembler tooling).

**Only heavy options remain:**
1. **Modify Myst's per-frame INTBACK** so the SMPC stages the analog pad's full data every frame
   (RE the IREG/COMREG setup near 0x0601bfb4 / 0x06047xxx; risk: breaking digital input).
2. **Issue an independent SMPC peripheral read** each frame (handshake/timing; conflicts with
   Myst's own SMPC use).
Both are substantial + risky. Myst already supports the 3D pad's **digital** mode (D-pad moves
the cursor), so analog is a pure add-on. **Decision pending: pursue option 1, or pause analog.**

## What we still need (next steps)

1. **Cursor X/Y screen variables + the mouse→cursor update site.** Fast path = Mednafen
   watchpoint: run Myst with a mouse, move it, watch which RAM writes track the on-screen
   cursor; trace PR back to the update routine. (This is the method that cracked Steamgear.)
2. **Digital D-pad already moves the cursor** (CONFIRMED by user, 2026-06-19). So both
   deliverables converge on the existing D-pad→cursor routine: "accelerated movement /
   quick jumps" = amplify the existing step / add an auto-repeat ramp there; analog stick =
   feed that same cursor-move path as a velocity source. No new cursor path needed.
3. **3D-pad analog data path.** In analog mode the 3D pad reports peripheral ID `0x16` with
   data bytes: [digital1][digital2][analogX][analogY][analogR][analogL] (X/Y center `0x80`).
   Decide injection point:
   - (a) at the acquisition buffer (`0x0605ad64`): if ID==analog, synthesize mouse-style
     deltas from (analogX-0x80, analogY-0x80) so the existing cursor consumer just works; or
   - (b) at the cursor consumer: add a branch that reads analog bytes directly.

## Patch strategy (mirror Steamgear)

- Build as a **composable / relocated** patch: put new logic in free space, hook with a
  branch, keep it disjoint from any future work so patches stack (see Steamgear 3D-pad +
  translation composability).
- Also declare `E` in the IP peripheral string (`JM` → add `E`) so the BIOS/launchers list
  3D-pad support (cosmetic but correct).
- Distribute via `.ssp` (changed `A.BIN`) + xdelta + IPS, per release-bundle conventions;
  recompute EDC/ECC (`ecc.py`) for Terraonion MODE compatibility.

## Status: RECON STARTED — input acquisition located; cursor consumer + analog inject TBD.
