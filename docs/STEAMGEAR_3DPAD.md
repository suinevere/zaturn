# Steamgear Mash — Saturn 3D Control Pad (analog) support

Goal: add analog-stick movement to Steamgear Mash, replicating the **Touge King the Spirits
3D Pad** patch technique. The 3D pad's *digital* buttons already work on any game; this adds
the *analog stick* by injecting digital d-pad presses derived from the analog axes.

## The Touge technique (fully decoded — the reference)

Patch = `MON\RACE.BIN` only (a BSDIFF40 binary diff inside the `.ssp`; `ip.bin` is the
patcher's rebuild copy and is **unchanged** — the IP.BIN peripheral field does NOT gate analog
data). RACE.BIN loads at HWRAM **`0x06003000`** (derived from the hook literal).

Two edits:
1. **Hook (12 bytes @ file `0x3B8A0`)** — the input function's *epilogue*
   (`lds.l @r15+,pr; mov.l @r15+,r12; mov.l @r15+,r13; rts; mov.l @r15+,r14`) was replaced with:
   ```
   mov.l @(pc+8),r2   ; r2 = 0x0604F57C  (the new routine)
   jsr   @r2
   nop
   nop
   .long 0x0604F57C   ; literal
   ```
2. **New routine (124 bytes @ file `0x4C57C` = HWRAM `0x0604F57C`)**, parked in zero-padding:
   - `base[index*size]` → r4 = pointer to the per-controller struct.
   - `if (byte@(1,r4) != 6) return;`  ← **peripheral ID 6 = analog/3D pad**.
   - reads the **analog axis byte @(4,r4)** (and a button/hat byte @(3,r4)), and **clears
     direction bits in the digital button byte @(2,r4)** using thresholds `0x68`/`0x98`
     around centre `0x80` (deadzone). Saturn pad bits are active-low → clearing a bit =
     "pressed".
   - ends with the **original epilogue** (relocated here), so it returns from the original
     function normally.

Net: before the input function returns, the analog steering is converted into the digital
left/right (and up/down) the game already reads. ~136 bytes total, no IP.BIN change.

Apply/inspect helper (Python):
```python
import bz2, struct
def bsdiff40(old, patch):
    cl,dl,nl=[struct.unpack('<q',patch[8+8*i:16+8*i])[0] for i in range(3)]
    ctrl=bz2.decompress(patch[32:32+cl]); diff=bz2.decompress(patch[32+cl:32+cl+dl]); extra=bz2.decompress(patch[32+cl+dl:])
    rd=lambda b,o:(lambda v:-(v&0x7fffffffffffffff) if v>>63 else v)(struct.unpack('<Q',b[o:o+8])[0])
    new=bytearray(nl); op=co=dp=xp=ep=0
    while op<nl:
        x=rd(ctrl,co);y=rd(ctrl,co+8);z=rd(ctrl,co+16);co+=24
        for i in range(x): new[op+i]=(old[dp+i]+diff[ep+i])&0xff
        op+=x;ep+=x;dp+=x; new[op:op+y]=extra[xp:xp+y];op+=y;xp+=y;dp+=z
    return bytes(new)
```

## Steamgear Mash — findings so far

### ✅ It reads the SMPC peripheral data itself (analog is fetchable)
Steamgear has its own raw **SMPC INTBACK** peripheral read (not just digital). Referenced
SMPC regs in `0.BIN`: COMREG `0x2010001F`, SR/SF `0x20100061/63`, IREG0 `0x20100001`, and the
**OREG output registers** `0x20100021` (OREG0) + `0x20100031–0x2010003F` (OREG8–15). The OREG
pointer table is a literal pool at file `0xD8678`.

### ✅ Input subsystem location
The whole peripheral/input subsystem is the function cluster at **file `0xD8290–0xD8E40`**
(HWRAM `0x060DC290–0x060DCE40`). Its working RAM is around **`0x060EFB60–0x060EFC00`** (BSS —
past the 0.BIN disc image, so only visible in a savestate). E.g. `0x060EFBC0` in RAM holds a
copy of the OREG pointer `0x20100031`.

### ✅ The fill captures the analog axes
The per-port store (file `~0xD85CE–0xD85FC`) writes into a struct at `r3`:
- `mov.w r0,@(2,r3)` — **16-bit digital button word** (built by OR-ing OREG bytes; raw =
  active-low).
- `mov.l r2,@(4,r3)` — **4 bytes** = OREG12–15 → for a 3D pad these are the **analog X/Y/Z/?**
  axes (SGL `PerAnalog` style: `+0 id, +1 size, +2 data, +4 x, +5 y, +6 z`).
- `mov.b r0,@(8,r3)` — a status byte (OREG0).

So when a 3D pad (analog mode) is connected, the analog axes land at struct `+4/+5/+6`. ✔

### ⚠️ Still to pin (the make-or-break details)
1. **The per-port struct base (`r3`)** — `r3` is set further up the fill function (likely a
   caller-passed pointer to `Per[port]`); the working pointers at `0x060EFBC0` are *not* it.
2. **The analog peripheral ID** to test at struct `+0/+1` (Touge used `6`; Steamgear's value
   needs confirming — Saturn analog 3D pad SMPC ID is `0x16`).
3. **Button-word polarity + direction bit masks** (raw SMPC is active-low: KU/KD/KL/KR =
   bits in OREG; confirm which bits the player-movement code reads).

### Fastest way to finish (decisive step)
Capture a **savestate with the Saturn 3D Control Pad in *analog* mode connected** (Mednafen:
set the Saturn port to "3D Control Pad" / analog; or Yaba Sanshiro analog pad). Then dump
HWRAM `0x060EFB60–0x060EFC00` and the struct becomes concrete: the **id byte** (analog ID),
the **button word** (`+2`), and live **analog X/Y** (`+4/+5`) with the stick tilted. That
pins all three unknowns at once and confirms the game actually receives analog bytes.

### Then the hook (small)
With the struct known, the cleanest hook (Touge-style) is at the **fill point** (after `+2`
and `+4` are written, ~`0xD85FC`) or the input function's epilogue: `if (id==analogID) {
x=byte@+4; y=byte@+5; set/clear KU/KD/KL/KR bits in the +2 word by X/Y thresholds (~0x40/0xC0,
centre 0x80, deadzone). }`. Steamgear is top-down 8-way, so it's just X→left/right, Y→up/down
— simpler than Touge's steering. Park the routine in a `0.BIN` zero pad, redirect one
spot, reinject + recompute ECC (same pipeline as the translation patch).

## Status
Buffer region + input subsystem **found**; analog capture **confirmed in the fill code**.
Blocked on the exact struct base / analog ID / bit masks — get a 3D-pad-analog savestate to
resolve all three, then write the ~40–60 byte hook.

## Savestate verification (2026-06-13) — buffer found, pad was in DIGITAL mode

Compared slot 1 (digital pad) vs slot 2 ("3D pad enabled"); Mednafen savestate WorkRAMH is
16-bit **byte-swapped** (deswap before reading).

- **Game input buffer FOUND:** a 4-byte-per-port array at **`0x060EFBFC`** (port 0 = P1),
  format **`[id:u16][buttons:u16]`**, **double-buffered** (active-buffer pointers live at
  `0x060EFB78` and `0x060EFC10`; they swap every frame). Port 0 in both slots = **`0002 ffff`**
  = peripheral **ID `0x0002` (digital pad)**, buttons **`0xFFFF`** (idle, active-low → 0=pressed).
- The raw SMPC staging struct at `0x060EFB50` (`+1`=id, `+2`=buttons, `+4`=analog) was **all
  zero** in both saves (not populated at save time; data flows straight into the 4-byte buffer).
- **Slot 2's "3D pad" reported ID `0x02` = it was in DIGITAL mode**, so it delivered only the
  2 digital button bytes — **no analog axes**. (Analog/3D pad in *analog* mode reports ID
  `0x16` + 6 bytes: 2 digital + X + Y + R + L.)

**Consequence + next step:** the 4-byte/port buffer is digital-only, so the analog axes (when
present) live in the *raw SMPC* path, not here. To proceed we must first see the pad in
**ANALOG mode**: set the emulated 3D Control Pad's **mode switch to analog** (physical switch
on HW; in Mednafen the 3D pad has an analog/digital toggle), tilt the stick, and save. Then
the staging struct `0x060EFB50` should show **ID `0x16`** at `+1` and live **analog X/Y** at
`+4/+5`. That confirms Steamgear actually captures the analog bytes (its fill code reads up to
OREG15, so it should) and gives the exact values to threshold in the hook.

Open question this resolves: **does Steamgear's INTBACK fetch the full analog record, or only
the digital optimize-mode 2 bytes?** If ID stays `0x02` even in analog mode, the INTBACK
command itself requests digital-only and we'd also need to change the fetch parameters
(harder). If it becomes `0x16`, the Touge-style inject hook is straightforward.

## "UP held" test (2026-06-13) — UP bit found; analog still absent → verdict

Both slots saved with **UP held**:
- **slot1 (digital pad):** port0 `0x060EFBFC` = **`0002 EFFF`** → ID `0x02`, buttons `0xEFFF`.
  So **UP = button bit `0x1000`** (active-low) in the `+2` word. ← the inject target bit.
- **slot2 (3D pad, stick up):** port0 = **`0002 FFFF`** → ID `0x02`, buttons **idle**. The
  stick produced **no input** — no digital bit, no `0x16` ID, no analog bytes anywhere in
  HW/LW RAM.

INTBACK config (IREG table @ LWRAM `0x20201000`) is **identical in both** slots:
`IREG0=00 IREG1=00 IREG2=A0 IREG3=00` — a fixed peripheral fetch; the pad came back as ID
`0x02` (digital) regardless.

### Verdict: feasible but HARDER than Touge, and currently blocked
- Touge's game already stored the analog axis in its controller struct → the mod just read &
  injected. **Steamgear's input buffer is digital-only** (4 bytes/port = `[id:u16][buttons:u16]`
  at `0x060EFBFC`), and the empirical 3D-pad test produced **no analog data at all**.
- Two explanations, neither yet ruled out:
  1. The emulated pad was **not truly in analog mode** (it reported ID `0x02`, and stick-up
     mapped to nothing). A pad genuinely in analog mode would report ID `0x16` + axes, and
     Steamgear's OREG read (out to OREG15) *might* then capture them — retest needed with a
     pad confirmed reporting `0x16`.
  2. Steamgear's **INTBACK requests digital-optimized data**, so the SMPC downgrades the
     analog pad to ID `0x02` (digital fallback). If so, adding analog needs the **INTBACK
     fetch changed** (IREG mode + capture the 6-byte analog record) *before* any inject — a
     multi-part, riskier patch, not the small Touge-style hook.

### What's nailed down (reusable)
- Inject target: **port0 buttons word @ `0x060EFBFC+2`**, **active-low**, **UP=`0x1000`**
  (by analogy KU/KD/KL/KR are the `0x1000/0x2000/0x4000/0x8000` nibble — confirm D/L/R the
  same way with held-direction saves).
- The buffer is **double-buffered**; the active one is via the pointer at `0x060EFB78`/`+`.
- INTBACK issued at HWRAM `0x0600B972` (`COMREG=0x10`), IREG params from LWRAM `0x20201000`.

### Decisive next test
Get the pad **truly into analog mode** and confirm **ID `0x16`** appears (in the staging
struct `0x060EFB50+1` or the buffer). If `0x16` + analog axes show up → small inject hook is
viable. If it stays `0x02` → the INTBACK fetch must be changed first (bigger job). Until then,
the easy Touge-style port is **not** confirmed for Steamgear.

## Does IREG2=0xA0 block analog? — No (2026-06-13)

SMPC mechanics: **the peripheral reports its own ID; INTBACK cannot change a pad's type.**
- 3D Control Pad reports **ID 0x16** (analog, 6 data bytes) in analog MODE, **0x02** (digital,
  2 bytes) in digital MODE — set by the pad's physical/emulated MODE switch, not the command.
- INTBACK IREG bytes only set fetch **timing/length** (15- vs 255-byte optimize) and which
  ports to poll. There is **no IREG flag to downgrade analog→digital**. The SMPC relays the
  pad's native data. So IREG2=0xA0 coexists with analog fine.
- Steamgear stores `OREG9` (the peripheral ID) into the buffer's id field; slot2 showed `0x02`
  → the SMPC genuinely received a **digital-mode** pad, not an INTBACK filter effect.
- Steamgear already reads OREG out to OREG15 and copies 6 bytes (`+2` buttons, `+4` analog
  X/Y/Z) into the staging struct `0x060EFB50`. So **if the pad reports 0x16, the analog axes
  ARE captured** (port-1 data is at the front of the INTBACK record; not truncated).

**Conclusion:** the blocker is the **emulator pad mode** (it was reporting 0x02 = digital), not
the INTBACK config. Get the pad reporting **ID 0x16** → analog axes land in `0x060EFB50+4`, and
the small Touge-style inject hook (`+2` buttons word, UP=0x1000, etc.) is viable. No INTBACK
modification needed. If the pad *cannot* be made to report 0x16 in the emulator, test on real
hardware / a different emulator before concluding Steamgear can't see it.

## Snapshot 3 (2026-06-13) — ANALOG MODE CONFIRMED; axis location pending

With the pad in analog mode, port0 buffer `0x060EFBFC` id word changed **`0002` → `1006`**
(low byte **`0x06`** = analog-pad id, the same value Touge tests for). So:
- ✅ The emulated pad reports analog and **Steamgear's SMPC read receives it** — IREG2=0xA0
  does NOT block analog (confirmed empirically, as predicted).
- ✅ Inject target id check = **buffer `+1` low byte == 0x06** (analog), like Touge's `==6`.
- ⚠️ The **analog X/Y bytes aren't located yet.** Buffer entries are `1006 ffff` / `1003 ffff`
  (id + idle buttons); the analog axes were ~centred (≈0x80) and blend in. The analog-storing
  staging struct `0x060EFB50` is zero at snapshot time (filled only transiently in the SMPC
  interrupt). So either the axes live in an 8-byte entry at `+4` (ambiguous vs a 4-byte-entry
  reading) or are read straight from OREG and the hook must sit in the SMPC-read routine.

**Decisive next capture:** hold the **analog STICK (not d-pad) fully in ONE direction**
(e.g. all the way LEFT) and snapshot. The axis byte then reads an extreme (≈`0x00`/`0xFF`),
which pinpoints exactly where X/Y are stored (or proves they're discarded). Diff vs a centred
snapshot → exact offset + range → write the hook (`if id+1==0x06: X<lo→set LEFT(0x4000),
X>hi→RIGHT(0x8000), Y similarly UP=0x1000/DOWN=0x2000` in the `+2` button word, active-low).

## Snapshots up/down/left/right + shoulders (2026-06-13) — DEFINITIVE: analog stick is discarded

Clean per-input captures, port0 button word @ `0x060EFBFC+2` (idle `FFFF`, active-low):
| Input | button word | result |
|-------|-------------|--------|
| stick UP / DOWN / LEFT / RIGHT | `FFFF` | **no change — not stored** |
| shoulder-L | `FFF7` | bit `0x08` |
| shoulder-R | `FF7F` | bit `0x80` |

- The **analog stick produces nothing** in the game buffer; only the **digital** buttons land
  (d-pad in the high byte: UP=0x1000 etc.; shoulders in the low byte: L=0x08, R=0x80).
- The pad reports 6 data bytes (id `0x06`) but Steamgear's 4-byte/port buffer keeps only the
  **2 digital button bytes** and **discards the analog X/Y (bytes 3–4)**. Confirmed by the
  total absence of any analog gradient across all snapshots.

### Architecture verdict (final)
A simple Touge-style game-level inject (read analog from the controller struct) **won't work**
here, because the analog axes are never in a struct the game keeps. The hook must sit in the
**SMPC peripheral-read routine** (`0x060DC2xx–0x060DCE40`): read the analog X/Y bytes straight
from **OREG** right after INTBACK (deterministic SMPC position: record = `[status][id][btn1]
[btn2][X][Y][R][L]`, X/Y centred at 0x80), threshold them, and set the **d-pad bits in the
button word before it's stored** to `0x060EFBFC+2` (UP=0x1000, DOWN=0x2000, LEFT=0x4000,
RIGHT=0x8000, active-low → AND-clear to "press").

This is **doable but a bigger patch than Touge** (an SMPC-read hook + reading discarded OREG
bytes), and it must be **iterated by testing in the emulator** (savestates can't show the
transient analog values). Remaining work: locate the active OREG-read that feeds `0x060EFBFC`,
find the OREG offsets holding analog X/Y for port 1, write ~50–80 bytes of SH-2 in a 0.BIN zero
pad, redirect, reinject + ECC, and test that the stick moves the player.

**Status: feasibility CONFIRMED (pad delivers analog, d-pad bits known), but it's the
SMPC-read-hook path, not the easy game-level inject. Implementation is a focused asm+RE task
with emulator iteration.**

## Analog data path LOCATED (2026-06-13) — hook design

Re-read of clean per-input snapshots (up/down/left/right/shoulderL/shoulderR) showed the stick
directions move a **checksum** byte (`0x060EFB95`: UP=40 DN=46 LF=51 RT=56) inside a constant
signature — i.e. the analog data IS read (it changes the checksum) but only the checksum +
digital buttons persist. The raw axes are transient, exactly as predicted.

**Where the raw analog axes are:** the SMPC fill at `0x060DC5C0–0x060DC5FC` writes the staging
struct `0x060EFB50`:
- `+1` = OREG9 = **peripheral ID** (0x16 for analog)
- `+2` (u16) = OREG10/11 = **digital button word** (high byte = d-pad: U=0x10 D=0x20 L=0x40
  R=0x80; low byte = shoulders/triggers). active-low (0=pressed).
- `+4` = OREG12 = **analog X** ; `+5` = OREG13 = **analog Y** ; `+6/+7` = OREG14/15 (R/L trig).
  (Saturn analog: 0x00..0xFF, centre ~0x80.)

**Hook plan (Touge-style inject, at the fill):**
- **Site:** `0x060DC600` (file `0xD8600`). Displace 3 simple instrs `22d0 60f2 c90f`
  (`mov.b r13,@r2; mov.l @r15,r0; and #15,r0`) — none are PC-relative, and r2/r13 survive, r0
  is recomputed, r1/r3 are dead afterwards → safe to clobber in the hook.
- **Replace with:** `mov.l @(disp,pc),r0 ; jsr @r0 ; nop` (needs a 4-byte literal = hook addr
  within ~1 KB PC-range — the one fiddly bit; reuse a pool gap or a nearby free slot).
- **Hook routine** (in a zero pad, e.g. file `0xEA580` = HWRAM `0x060EE580`):
  ```
  mov.b r13,@r2                 ; displaced #1
  mov.l @(stg,pc),r3            ; r3 = 0x060EFB50
  mov.b @(1,r3),r0; extu.b r0,r0; cmp/eq #0x16,r0; bf done   ; analog only
  ; LEFT:  X(@4) <= 0x60 -> clear 0x40 in @(2,r3) high byte
  ; RIGHT: X     >= 0xA0 -> clear 0x80
  ; UP:    Y(@5) <= 0x60 -> clear 0x10
  ; DOWN:  Y     >= 0xA0 -> clear 0x20
  ;   (each: mov.b @(2,r3),r0; and #mask,r0; mov.b r0,@(2,r3))
  done:
  mov.l @r15,r0; and #15,r0     ; displaced #2/#3 (r0 = return value for next instr)
  rts ; nop
  stg: .long 0x060EFB50
  ```
- d-pad masks: left `0xBF`, right `0x7F`, up `0xEF`, down `0xDF` (clear = press, active-low).

### Risks to resolve by EMULATOR TESTING (cannot be done from savestates)
1. **Is the `0x060EFB50` staging path the active one?** It reads as zero in every savestate
   (filled transiently in the SMPC ISR). If the live data actually flows a different route, the
   hook has no effect and we re-target the route that writes `0x060EFBFC`/the checksum.
2. **Analog polarity** (is X=0x00 left or right? Y up/down?) — flip thresholds if reversed.
3. **Thresholds / deadzone** (0x60/0xA0 are a starting guess).
4. **PC-rel literal slot** near `0x060DC600` for the hook address.

These need iterate-and-run in the emulator. RE is complete; remaining work is write-hook →
flash → test → adjust, a few rounds.

## Iteration-1 result + active path found (2026-06-13)

**Iter-1 (commit, patch `... 3D Control Pad (iter1).xdelta`):** boots fine, d-pad works, **stick
does nothing.** The trampoline + relocated SR-restore are correct (no crash) — but the hooked
function (the `0x0DC540` staging-fill writing `0x060EFB50`) is **NOT the live per-frame path**
(the staging struct reads zero in every savestate; the hook's id check just fails and skips).

**The actual live path (traced):**
- `0x0DD044` = per-frame peripheral processor. Holds the `0x060EFBFC` double-buffer
  (pointers `0x060EFC10/14`, bases `0x060EFBE0/...`), fills idle `0xFF`, swaps buffers, and
  `jsr`s the real decode.
- decode = **`0x0DC342`** → `0x0DC3CC`: a multi-stage **SMPC INTBACK state machine** (checks
  status flags `& 0x10`, advances a state counter, error/retry counters, `bsr 0x0DCD72`…).
  The analog bytes are read somewhere inside this machine and folded into the checksum at
  `0x060EFB95` (which is why the stick moves that byte) but only the digital buttons survive
  into `0x060EFBFC`.

**Implication:** re-targeting is not a one-liner. The analog access sits several layers into a
state machine, so iteration-2 needs to (a) pin the exact instruction where analog X/Y are in
registers/memory in the live path, (b) hook there, (c) emulator-test. Expect several rounds.
This is a meatier sub-project than the translation patches — the SMPC handler is intricate.

Status: feasibility still confirmed; iter-1 mechanics proven (clean trampoline/SR-restore);
blocked on locating the analog read inside the `0x0DC342` state machine.

## Active path is transient — use a Mednafen watchpoint (2026-06-13)

Traced the live path 0x0DD044 -> jsr 0x0DC342 (decode) -> 0x0DC3CC (INTBACK state machine) ->
sub-fns 0x0DCCF8 / 0x0DCD72 / 0x0DCDA0 / 0x0DCDEA (byte-copy). Findings:
- The live decode references only SMPC **status** regs (0x20100061 SR, 0x20100063 SF), not the
  OREG **data** regs directly — the data path is behind the state machine / a DMA-ish copy.
- Followed the copy-loop src/dst pointers (`[0x060EFBB4]`, `[0x060EFB78]`, ...): they only carry
  the **digital** record into 0x060EFBFC (`1006 ffff`). **No persistent buffer holds the raw
  analog X/Y** — it's read, folded into the checksum byte `0x060EFB95`, and dropped.

So a static blind hook can't reliably find the analog read. **Pin it with a Mednafen watchpoint**
(the technique that cracked the PIC codec):
1. In Mednafen's SH-2 debugger, set a **WRITE breakpoint on HWRAM `0x060EFB95`** (the checksum
   byte that provably changes with the analog stick: UP=40 DN=46 LF=51 RT=56).
2. Tilt the analog stick; when it traps, note the **PC** (and a few instructions of context /
   the PR call chain). The code there is reading the analog bytes to fold into the checksum.
3. (Optional, even better) set a **READ watchpoint on the SMPC OREG analog reg** to catch the
   exact `mov.b @OREG,Rn` for X/Y and which OREG index they are.

With that PC + the OREG/source offset, the hook is a small inject (read X/Y -> AND-clear the
d-pad bits in 0x060EFBFC+2) placed at a point that actually runs. Iter-1's trampoline/SR-restore
mechanics are proven, so only the target needs to change.

---

## ITERATION 2 — DEFINITIVE: analog IS in OREG; the game discards X/Y (2026-06-13)

A/B savestate capture on the **3D-pad-patched disc** in **analog (MODE=on)**, 6 slots =
stick UP / DOWN / LEFT / RIGHT + shoulder L / R. Parsed WorkRAMH + WorkRAML + the savestate's
**SMPC OREG block** (Mednafen field `OREG`, 32 bytes; `analysis/ss_*` scripts).

### What the savestates proved
1. **iter-1 hook is INERT, not harmful.** It reads staging `0x060EFB51` for the id and does
   `cmp/eq #6`; that byte is `0x00` in every capture (the `0x060EFB50` region is a *ring/frame
   counter*, not a pad buffer — byte `0x060EFB95` just counts up 0x18→0x23→0x28→0x35→0x44→0x49).
   So the hook's id-check always fails → it does nothing but its relocated SR-restore. The d-pad
   being dead in analog mode is the **game/Mednafen baseline** (analog MODE routes the d-pad to
   the stick axes), NOT the patch.
2. **The game's pad buffer (`0x060EFBE0`, 4 mirror records `ff ff ff ff 10 00`) holds only the
   2 DIGITAL bytes d0/d1.** Pushing the stick U/D/L/R changes **nothing** there; only L/R
   shoulders flip Data[1] bits (L→`f7`, R→`7f`). No analog axis value exists anywhere in HWRAM
   or LWRAM (full scan = 0 candidates).
3. **The analog data is fully present and clean in the SMPC OREG report** (ID = `0x16`, size 6):

   ```
           OREG:  f1 16 [d0 d1   X    Y   Rt Lt]
   UP    :  ... 16 ff ff  80   00   00 00     Y=0x00
   DOWN  :  ... 16 ff ff  80   ff   00 00     Y=0xFF
   LEFT  :  ... 16 ff ff  00   80   00 00     X=0x00
   RIGHT :  ... 16 ff ff  ff   80   00 00     X=0xFF
   L-tr  :  ... 16 ff f7  80   80   00 ff
   R-tr  :  ... 16 ff 7f  80   80   ff 00
   ```
   **Calibration (exact): X 0x00=left · 0x80=center · 0xFF=right;  Y 0x00=up · 0x80=center ·
   0xFF=down.** d0 digital-dir bits (active-low) to clear: Up=0x10 Down=0x20 Left=0x40 Right=0x80.

### Where the analog bytes live (fixed hardware — no fragile RAM buffer)
The parser (function `0x060DC540..0x060DC700`, the SAME function iter-1 sat in) walks the OREG
stream via `r14`, which is loaded from the literal **`0x20100021` (= OREG0) at file 0xD8698**.
SMPC OREGn = `0x20100021 + n*2`, so:
- OREG2 d0 = `0x20100025`, OREG3 d1 = `0x20100027`
- **OREG4 analog X = `0x20100029`**,  **OREG5 analog Y = `0x2010002B`**

The parser masks the size nibble (`and #15` → 6), stores d0/d1, then `r14 += 2` **past X/Y** —
the axes are read off the live OREG mirror and dropped. (There are several per-type copy
routines: `0xDCCD0` copies d0/d1/d2, the `0xDD0D4` family handles a size-0xF descriptor and
writes the mirror buffers `0x060EFBE0/EFBFC/EFC10/EFC14/EFC18/EFC1C` — the literal pool at
0xDD0B4 lists them. Which one feeds the player-movement read must be pinned dynamically.)

### Iteration-2 hook design (target = the OREG, not the staging buffer)
Read X/Y directly from the OREG mirror (rock-solid, fixed addresses) and AND-clear the d-pad
bits into d0 of the pad buffer the movement code reads:
```
r_smpc = 0x20100021
X = @(0x29 - 0x21, r_smpc)  ; 0x20100029
Y = @(0x2B - 0x21, r_smpc)  ; 0x2010002B
if X < 0x60: d0 &= ~0x40   (LEFT)
if X > 0xA0: d0 &= ~0x80   (RIGHT)
if Y < 0x60: d0 &= ~0x10   (UP)
if Y > 0xA0: d0 &= ~0x20   (DOWN)
```
**Open item (needs ONE Mednafen watchpoint):** confirm which buffer the per-frame movement
reads so the d0-clear isn't overwritten downstream. Recipe — in Mednafen debugger (Alt+D):
add a **Read** watchpoint on `20100029` (analog X). Push the stick; the trapped PC is the
parser instruction reading X. Then add a **Write** watchpoint on the pad-buffer d0 the movement
uses (try `060EFBE0`) to confirm the store the player input reads from. Hook just AFTER that
store. iter-1's trampoline + SR-restore mechanics are proven; only the target + the read source
(OREG instead of `0x060EFB50`) change.

### Mednafen watchpoint recipe — EXACT keys (from mednafen.github.io/documentation/debugger.html, v1.32)
Keys (confirmed): `ALT+D` master toggle · `ALT+1` CPU view · `ALT+3` memory editor ·
in CPU view: `R` run · `S` step · `SHIFT+R` edit READ breakpoints · `SHIFT+W` edit WRITE
breakpoints · `Space` toggle PC breakpoint · `SHIFT+Return` edit watch address.
Addresses are raw hex, **no `0x` prefix**. A read/write breakpoint entry is an address or a
range `START-END`; a single byte is just the address.

To pin iter-2:
1. Boot the 3dpad disc, reach actual gameplay (player on screen), pad MODE = analog.
2. `ALT+D`, then `ALT+1`.
3. `SHIFT+R` → type `060EFBE0` → Enter. (read watch on the pad buffer d0 the game consumes)
4. `R` to run; move the player. On break, note the PC = the movement code reading direction.
   If it never trips, retry with the mirrors `060EFBFC` / `060EFC10` / `060EFC14`.
5. Also `SHIFT+R` add `20100029` (analog X). On break, PC = the SMPC parser reading OREG;
   the d0/d1 store is a few instructions later — that's the hook point. (If `20100029` never
   trips, the SH-2 may issue it via a mirror — try `00100029` or `60100029`.)
Report the trapped PC for `060EFBE0` and for `20100029`; that's all I need to place the hook.

### ITERATION 2 — BUILT (needs emulator/hardware test)
Hook implemented in `analysis/apply_3dpad.py` and shipped to `game_patched/steamgear_mash_3dpad/`.
- **Why not a watchpoint:** a read-watch on either `0x20100029` or `0x060EFBE0` trips every frame
  (both are read unconditionally), and since the stick is *discarded* there is no downstream
  direction-handler to trap. So watchpointing can't isolate anything here — pinned statically.
- **Hook site:** `0x060DC522`, the SMPC driver's normal `rts` epilogue (the driver is dispatched
  via a function-pointer table, so there is no static caller to hook — the rts is the clean
  per-frame anchor). Trampoline `mov.l @(1,pc),r1; jmp @r1; nop; <0x060EE580>` overwrites the
  epilogue head; the displaced 10-instruction epilogue is relocated verbatim into the hook tail.
- **Hook (144 B @ 0x060EE580):** preserves r0; reads X=`*0x20100029`, Y=`*0x2010002B`; builds an
  active-low clear mask (X<0x60→LEFT 0x40, X≥0xA0→RIGHT 0x80, Y<0x60→UP 0x10, Y≥0xA0→DOWN 0x20);
  `not`s it and AND-clears d0 of all 4 live records (`0x060EFBE0/EC/FE`, `0x060EFC06`); restores
  r0; runs the relocated `rts`. Thresholds use `cmp/hs` with `extu.b`'d constants (0xA0/0x80
  sign-extend otherwise). Assembled by a two-pass mini-assembler and **disassembly-verified**.
- **Build:** 150 bytes changed (`0xD8522..0xEA60F`), 2 sectors ECC-fixed, xdelta round-trips.
- **If the stick still does nothing:** the analog INTBACK completion likely returns via the
  tail-call exit `0x060DC50E` (→`0x060DCC84`) instead of this rts — retarget `SITE` there. If it
  *crashes*, the exit isn't reached with the assumed stack/regs — revert and use the parser-store
  approach. The change is inert if the path isn't taken (same failure mode as iter-1, no harm).

---

## ITERATION 3 — REAL ROOT CAUSE FOUND; the analog stick was NEVER the blocker (2026-06-14)

Every prior iteration chased the analog **stick** on the false premise that "the game discards the
analog pad's digital buttons; the dead d-pad in analog mode is the baseline." **That premise was
wrong** — it came from only ever pushing the *stick* in analog mode (which leaves the digital byte
idle). A fresh 4-slot capture pushing the **physical D-PAD in analog vs digital mode** overturned it.

### Evidence (savestate set `710edf3c…` mc1–4; that disc = pristine original 0.BIN, RAM match 65536/65536)
| slot | mode (OREG id) | input | OREG d0 | game buffer `0x060EFBE0` d0 |
|------|----------------|-------|---------|------------------------------|
| mc1 | analog 0x16 | D-pad UP   | `ef` (UP)   | `ef` |
| mc2 | digital 0x02 | D-pad UP   | `ef` (UP)   | `ef` |
| mc3 | analog 0x16 | D-pad DOWN | `df` (DOWN) | `df` |
| mc4 | digital 0x02 | D-pad DOWN | `df` (DOWN) | `df` |

In analog mode the d-pad bits **are present and byte-identical to digital mode**, both in OREG and in
the game's own pad buffer. The only systematic difference is a per-port flag (`padrec[+4]` = `0x10`
analog / `0x00` digital; buffer id word `0x1006` vs `0x0002`).

A scan for "decoded only in digital mode" (analog UP==analog DOWN, digital UP!=digital DOWN) pinned
the game's master input word at **`0x060769C4`** (active-high: UP=`0x10` DOWN=`0x20` …): correct in
digital mode, **`0x00` in analog mode**. Its source is `0x060834CC`, written by the HIGH-LEVEL input
decoder at `0x06004090`.

### The gate (exact instruction)
```
060040bc: mov.l @(..410c),r4   ; r4 = 0x060834CC  (decoded-input dest)
060040be: mov.l @r14,r2        ; r2 = active pad record ptr
060040c0: mov.b @(4,r2),r0     ; r0 = padrec[+4]  (analog flag: 0x10 analog / 0x00 digital)
060040c6: tst  r3,r3
060040c8: bf   0x06004110      ; if flag != 0  -> SKIP decode               <-- THE BUG (port 0)
060040ca: mov.w @r3,r2 ; not r2,r2 ; mov.w r2,@r4   ; *0x060834CC = ~padrec[+0] (active-low->high)
06004110: mov #0,r2 ; mov.w r2,@r5 ; mov.w r2,@r4   ; skip path explicitly ZEROES the input
06004128: bf   0x06004146      ; identical gate for port 1 (record at padrec+6) <-- BUG (port 1)
```
Steamgear Mash (1995) predates the 3D Control Pad (1996): its input code sees the unknown "analog"
peripheral type and stubs it out as no-input — even though SGL already filled the digital buttons.

### The fix (`analysis/apply_3dpad_gate.py`) — supersedes the failed SMPC hooks  ✅ CONFIRMED WORKING (emulator, 2026-06-14)
NOP both gate branches so the existing digital decode runs for the analog pad too (it reads
`padrec[+0]`, proven to hold the correct d-pad bits in analog mode):

| 0.BIN file | HWRAM | was | becomes |
|------------|-------|-----|---------|
| `0x0C8` | `0x060040C8` | `8B22` (bf) | `0009` (nop) |
| `0x128` | `0x06004128` | `8B0D` (bf) | `0009` (nop) |

4 bytes, all in one sector (LBA 22); EDC/ECC recomputed; xdelta round-trips. This is the HIGH-LEVEL
game input read the iter-2 README said to target (not the fragile SMPC interrupt driver). Result: in
analog mode ALL digital buttons (d-pad/face/shoulders) work. Shipped to
`game_patched/steamgear_mash_3dpad/`. **CONFIRMED WORKING** in the emulator (2026-06-14): d-pad moves
the player in analog mode. (To repro: boot the .cue FRESH — a savestate has the old code in RAM — pad
MODE=analog.)

### Menus — analog stick needs a synthesized "edge" (2026-06-14)
In-game movement reads the HELD word `0x060769C4`; menus also read the EDGE word `0x060769C8`
(`~padrec[+2]`, newly-pressed). Vertical menus auto-repeat off a slow hold-counter (threshold 20 at
`0x0600D0E8`/`0x0600D14A`); the **settings controller-mode left/right** (sole L/R reader, at
`0x0603AD50`, masks `0x4000`) toggles **purely on the edge**. The analog stick produces a held signal
but **no edge** (it's not in `padrec[+2]`), so up/down were sluggish and left/right dead in menus.

Fix (`apply_3dpad_stick.py`, two-cave version): the hook now also synthesizes an edge — `edge = stick
& ~prev` with a 1-byte persistent `prev` at `0x06004114` — and folds it into `0x060834CE` (→
`0x060769C8`) by clearing those bits in the displaced `r2` so the original `not` at `0x060040DA` sets
them. One pulse per stick "flick" → menus respond like a tapped d-pad; d-pad unaffected (it has its own
edge). Routine spans Cave A `0x0604D818` (build mask) → Cave B `0x0604E11C` (held + edge + return).
**Needs emu test.** Note: in-game X is inverted, so the settings L/R *sense* may need a flip (easy).

### Composable with the English translation (2026-06-14) — ✅ CONFIRMED WORKING, no combined release needed
The relocated-cave build was user-confirmed working on the emulator (analog mode: d-pad, stick
movement, menus, Option-screen controller-mode left/right). The release xdelta/IPS are generated from
that exact track.

**L/R unified (2026-06-14, ✅ CONFIRMED WORKING):** in-game and menu left/right had used OPPOSITE X senses
(held stick-left=0x80, edge stick-left=0x40) — they feed the same game word so they must match. With
both patches in use the in-game L/R was wrong / menu right. Fixed: held and edge both use the natural
mapping (stick-left -> LEFT 0x40<<8, stick-right -> RIGHT 0x80<<8); the cave_swap xor #0xC0 was removed.

**Combined release:** `releases/Steamgear Mash (Japan) (English + 3D Control Pad V1.0)/` bundles BOTH
patches (steamgear-english + steamgear-3dpad, each .xdelta and .ips) + readme. Builder:
`analysis/make_release_combined.py` (verifies round-trips, sector-disjointness, valid combined ECC, and
stacking in both orders). Standalone 3D-pad-only release also refreshed.

The translation rewrites 0.BIN sectors `{19,20,138,146,147,181,182,183}` and even reuses the old
`0x0604D816` padding for text, so the stick hook's caves were relocated into translation-FREE code
caves: **mask `0x0604E11C` (sec148) → held/edge `0x0604AC90` (sec141) → swap/return `0x0605E448`
(sec180)** (+ gate/trampoline/`prev` in sector 0). 3D-pad now touches 0.BIN sectors `{0,141,148,180}`
/ Track-01 LBA `{22,163,170,202}` — disjoint from the translation's LBA `{41,42,160,168,169,203,204,
205}`. Verified: the two xdeltas stack in any order (the 3D-pad xdelta decoded on top of the translated
track == the merged disc), every touched sector passes EDC/ECC, and `apply_3dpad_stick.py` applies
cleanly to BOTH the original and translated `0.BIN`. The project VCDIFF uses plain `VCD_SOURCE` copies
with no source checksum, so DeltaPatcher applies the 3D-pad patch on either disc.

### Earlier: analog STICK → movement
With the master input word (`0x060769C4`) now honored in analog mode, stick support becomes a small
additive hook: read X=`0x20100029`/Y=`0x2010002B` (X 00=left/80=ctr/FF=right; Y 00=up/80=ctr/FF=down)
and OR the active-high direction bits into the decoded word. No SMPC-driver hook needed.

### STICK STEP BUILT (`analysis/apply_3dpad_stick.py`) — needs emu/HW test
Implements exactly the above. Applies the gate NOPs PLUS a trampoline at `0x060040D4` (right after the
buttons store) → a 96-byte cave at `0x0604E11C` (verified inter-function zero padding; the trampoline's
cave-address literal is parked in the now-dead skip-path at `0x06004110`, 4-aligned). The cave:
1. **Gates on `padrec[+4]`** — only runs for an analog pad on port 0. (For a digital pad, OREG4/5 hold
   the *next port's* header, not axes, so reading them would inject garbage; the gate skips straight to
   the displaced instrs, leaving digital behaviour identical.)
2. Reads X=`*0x20100029`, Y=`*0x2010002B`; builds an active-high dir mask (X<`0x60`→LEFT `0x40`,
   X≥`0xA0`→RIGHT `0x80`, Y<`0x60`→UP `0x10`, Y≥`0xA0`→DOWN `0x20`; deadzone `0x60`/`0xA0` around centre
   `0x80`); ORs it into the decoded word at `0x060834CC`.
3. Runs the 3 displaced instrs (`mov.l @r14,r3; mov.w @(2,r3),r0; mov r0,r2`) and returns to `0x060040DA`.

Stick directions are ADDITIVE with the d-pad (both work). Build = 106 bytes changed, 2 sectors (LBA 22
decoder + LBA 170 cave), ECC recomputed, xdelta round-trips. Disassembly-verified. Reuses iter-1/2's
proven trampoline mechanics but at the SAFE high-level decoder, not the SMPC driver. Shipped to
`game_patched/steamgear_mash_3dpad/`.

**✅ CONFIRMED WORKING (emulator, 2026-06-14)** — stick moves the player in-game and advances the menu
cursor (the main menu reads the same input word `0x060769C4`, so hold the stick and its auto-repeat
advances). Two fixes during bring-up:
- **Bit lane:** the decoder builds the word as `~(d0<<8 | d1)`, so direction bits are in the HIGH byte
  (LEFT `0x4000` etc.). The cave builds a low-byte mask then `shll8`'s it before OR (a low-byte mask set
  `0x060769C4=0x0040`, which the game ignores). 
- **X inversion:** the in-game X axis reads inverted vs the d-pad sense, so the X branches were swapped
  (stick-left → `0x80<<8`, stick-right → `0x40<<8`). Y was already correct.
