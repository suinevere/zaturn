# T&E Soft Saturn Golf — English Activation Playbook

How the T&E Soft golf engine (Saturn maker code **T-114**) stores language data,
how to force English when a built-in English build exists, and how to distribute
the result. Captured from the *Waialae no Kiseki* work; applies to the sibling
golf titles.

## The engine

Shared root files: `A.BIN` (boot dispatcher, loads to HWRAM `0x06054000`),
`EXEC.BIN`, `GOLF.BIN`, `SETUP.BIN`, `TUTORIAL.BIN`, `GUIDE.BIN`, `DEMO.BIN`, and
`*.DAT` (course / member / config data). Discs are **MODE1/2352**, the SH-2 CPUs
are **big-endian**, and on-screen text is drawn from a **custom font** (decodes to
private-use glyphs), so a plain Shift-JIS scan of the overlays returns noise — the
UI text is *not* trivially editable.

## Two language mechanisms

**1. Full dual build** — *Waialae no Kiseki - Extra 36 Holes* (T-11402G, 1996).
Because it descends from the Western "True Golf Classics: Waialae Country Club",
every code overlay ships twice:

```
A.BIN ──► LOAD.BIN  ──► EXEC.BIN  ──► GOLF.BIN / SETUP.BIN / ...     (Japanese)
      └─► ELOAD.BIN ──► EEXEC.BIN ──► EGOLF.BIN / ESETUP.BIN / ...   (English)
```

`A.BIN` contains a "Please Select Language" menu and a big-endian pointer pair
`[ptr→"LOAD.BIN"][ptr→"ELOAD.BIN"]`. **Forcing English = point the default slot at
ELOAD.BIN.** For Waialae that is 4 bytes at image offset `0x1C008`
(`06054748 → 06054754`). Confirmed working on Yaba Sanshiro and Saroo.

**2. Asset-pair toggle** — *Masters - Harukanaru Augusta 3* (T-11401G, 1995) and
*Junclassic C.C. & Rope Club* (T-11403G, 1997). **No** English code build, **no**
`LOAD.BIN/ELOAD.BIN`, **no** language menu. Instead, some media exists as `_E`/`_J`
pairs chosen at runtime:

- Augusta 3: `GOLF/1M_E.CPK` … `4M_E.CPK` (video) with `_J` twins.
- Jun Classic: `GOLF/CADDIE/0M_E.APC` … (caddie audio) + `_E.GDT` (graphics) with `_J` twins.

So these have *some* English assets and a language flag, but full English UI would
require the real translation pipeline (custom-font RE + text extract/translate/
reinsert). The one-pointer trick does **not** apply.

## Detecting the loader switch

`saturn-translate-cli force-language <track1.bin>` finds the pair by requiring the
two pointers to be **adjacent** *and* the inferred load base to be **page-aligned**.
This matters: `A.BIN` also has a menu-line pointer table whose entries are spaced the
same `0xC` bytes apart as the `LOAD.BIN`/`ELOAD.BIN` strings, so value-math alone
mistakes it for the loader pair — only the alignment check disambiguates. Sanity
check: the JP loader pointer value must occur **exactly once** in the whole image.

## Distribution

- **EDC/ECC:** a raw byte patch invalidates the sector's error-correction. Emulators,
  Fenrir and Saroo tolerate it; **Terraonion MODE freezes at the SEGA logo**. Use
  `force-language --fix-ecc` (recomputes the sector via `saturn_translate/ecc.py`,
  validated against the disc's own untouched sectors).
- **`.ssp` (Sega Saturn Patcher) is the cleanest route:** it's a renamed ZIP of only
  the **changed files**, and it rebuilds the disc + regenerates all EDC/ECC itself —
  so ship just the one changed file (Waialae = `A.BIN`, 228 KB; see
  `game_patched/ssp_source/A.BIN`). A huge `.ssp` means the whole disc/track was
  included.
- **xdelta/IPS** also provided for DeltaPatcher / IPS tools, but the raw image must
  match the original exactly.

## Quick triage of a new T&E golf disc

1. `force-language <track1>` → if it reports a `LOAD.BIN→ELOAD.BIN` flip, you have a
   full English build: patch it, `--fix-ecc`, done.
2. Else list files for `E*.BIN` overlays or `_E`/`_J` asset pairs. `_E`/`_J` pairs =
   partial English assets + a runtime flag (worth chasing the flag); no `E*` overlays
   and no menu = Japan-only, full translation required.
3. Check the IP header maker (`T-114`) and product code to place it in the series
   (T-11401G Augusta 3 · T-11402G Waialae · T-11403G Jun Classic).
