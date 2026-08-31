# Zork — Infocom Collection: asset kit

This kit builds a full Saturn disc — every Infocom game, the CD-DA audio, and
the room backgrounds — from the open-source base image. The three Zork games are
open-sourced and shipped here; everything else is downloaded by these scripts,
not shipped.

## Build

1. Ensure the base ISO is present at the path `CONFIG.ME`'s `BASE_ISO` names
   (shipped with the kit).
2. Run **`update.bat`** — double-click on Windows, or `bash update.bat` on
   Linux/macOS. It runs the three scripts below in order.
3. Burn or mount the `.cue` in the output folder `CONFIG.ME`'s `OUTPUT_DIR`
   names.

`update.bat` runs, in this order and for a reason:

| Script | What it does |
|---|---|
| `bg.bat` | Downloads the original Zork I (Japan) disc and lifts its eleven `B*.CGL` room-background archives into `BG/`. |
| `games.bat` | Downloads the Infocom set and injects it into the base ISO **together with `BG/`**, preserving the Saturn IP.BIN boot header, producing `<disc>.bin` + `.cue`. |
| `music.bat` | Splits the CD-DA tracks out of that same disc image and merges the final burnable disc. |

**The order is not a preference.** `games.bat` maps `BG/` to `/BG` in the same
`xorriso` commit that places the stories, so backgrounds staged after it would
never reach the disc; and `music.bat` promotes the data track `games.bat` wrote
to Track 01.

`bg.bat` and `music.bat` need the same download — the Japanese Zork I disc — and
share one cached copy in `cache/`, so it is fetched once. That cache is a few
hundred megabytes; delete it when you are done. Point `ZORK_DISC` in `CONFIG.ME`
at a local copy of that disc to skip the download entirely.

Each background archive is checked by size and SHA-256 before it is staged. This
is not a courtesy check: the per-room table in the interpreter records a byte
offset into each archive, so a different disc revision would not fail to open —
it would decode garbage.

`pvms.bat` is separate and is run by the interpreter's own build, not by
`update.bat`: it converts the boot jingle to `SPLASH.PCM`.

Linux/macOS use system `xorriso`/`dd`/`iso2raw`; Windows uses the bundled copies
in `bin/win/` (see `bin/README.md` for licenses).
