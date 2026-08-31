---
name: dynamic-default-title-brightness-and-one-shot-jingle-handoff
description: Dynamic is the shipped default and is reachable from the cold-boot Options menu, the title wallpaper no longer carries the in-game reading dim, the Options list follows the same layout conventions as every other menu, and the boot jingle plays once instead of looping.
metadata:
  type: project
---

Continues [[logo-timing-title-cgl-and-tga-removal-handoff]], which introduced
the title screen's CGL wallpaper and the six-second logo that three of these
five items are consequences of.

## What the owner asked for

1. Make Dynamic the default instead of IBM.
2. Dynamic is not showing in the cold-boot Options menu; it shows in game.
3. The Options list is not centred -- pushed to the right compared with other
   menus.
4. The title menu background is really dark.
5. Play the SUINEVERE opening cue once, then silence until the player presses a
   button to continue from the title.

## 1 and 2 are one bug, and it had a third face

`display_has_art()` reports whether the RUNNING GAME carries authored art. It
was being asked three separate questions it had no business answering:

- `display_defaults` fell back to IBM PC (MDA) whenever it read 0;
- `display_cycle_palette` stepped over Dynamic whenever it read 0;
- `display_decode` **refused a saved Dynamic** whenever it read 0.

The flag is 0 everywhere outside a running game -- the title screen, the mode
menu, the Options menu -- and options are loaded at the title. So a cold boot
defaulted to MDA, the Palette row skipped its own first entry, and a player who
did manage to select Dynamic in game had it silently reset on the next boot by
the third one. That third face was not in the report and would have made the
first two look unfixed.

All three gates are gone. `display_has_art()` now decides only whether a picture
is DRAWN, which is the question it can actually answer: `display_apply` still
checks `room_art_available()` before asking room_art to redraw, so Dynamic at
the menus correctly shows no picture.

**Consequence worth stating:** only Zork I has a presentation table
(`PRES_GAME_N` is 1). The other 31 stories now default to Dynamic, which for
them means black background and white text rather than MDA green. That is what
"Dynamic instead of IBM" means; it is not a regression.

## 4: the title wallpaper was carrying a reading dim

`main.cxx` applied `display_dim_offset(g_display.dim)` before the boot loop, so
the splash logo and the title picture were lit at the player's wallpaper dim.
The shipped default is `DISP_DIM_DEFAULT` = 3 = **-64**, a quarter of the way to
black.

That dim exists because a room picture at full brightness competes with the game
text printed over it. The boot screens have three lines of text between them and
are there to be looked at, so they now carry no dim at all: `title_bg_dim_set(0)`
inside the loop (inside, so a soft-reset return gets it too -- the setjmp target
is below the options load), and the player's own value is restored by the
`display_apply()` that already runs after the title fades out.

If it still reads dark on screen, the next lever is the pick itself rather than
the dim: the frames are Zork I room art and most of the eleven archives are
underground.

## 3: the Options list, measured

Two things in `options_menu` disagreed with `menu_select`, which every other
list goes through:

- the box was sized from a hardcoded `content_w = 18`, left over from a wider
  label set, while its rows are `label_w + MENU_DIGIT_COLS` (11 at the title) --
  a 22-column box around an 11-column list;
- the pad kept the digit columns even when the digits were hidden, and
  `menu_num` filled them with spaces, so the gutter became leading whitespace
  inside a centred block.

Computed with the real `menu_box_fit` (scratch program, not by eye):

```
                       box      pad   glyph columns   mid
mode select (nums)     8..30    19    10..25          17.5
mode select (kbd)      8..30    16    11..23          17.0
options (nums)  before 9..30    11    14..24          19.0
options (kbd)   before 9..30    11    17..24          20.5   <- 1 col right of centre
options (nums)  after  12..26   11    14..24          19.0
options (kbd)   after  12..26    8    15..22          18.5
```

**Be aware of what this did and did not fix.** The keyboard case was genuinely
off-centre and is now not. The gamepad case was already centred at 19.0 against
a screen centre of 19.5 -- what makes it *look* further right than the mode menu
is that its list is 8 columns narrower, so centring puts its left edge at 14
where the mode menu's is at 10. If that is the shift you were seeing, the fix is
a different one (a shared left edge across menus, not centring), and I have not
made it. See the question at the end of the session summary.

## 5: the jingle is a one-shot

`boot_music_vblank` re-triggered the sample at the end of every pass, so the
~21-second cue looped for as long as the title screen was up. It now stops and
stays stopped. `g_boot_music_looping` became `g_boot_music_running`, and a new
`g_boot_music_done` is what `boot_music_playing()` reports.

`done` rather than clearing the channel, deliberately: `boot_music_stop`'s buffer
scrub keys off `g_boot_music_channel >= 0` and still has to run on the way out
(the driver stages samples that survive a `StopSound`). Clearing the channel in
the V-blank would have skipped it. `title_and_seed` already guards its fade on
`boot_music_playing()`, so a cue that ended on its own is not ramped -- which
matters, because ramping with nothing playing walks the driver's master volume
to zero and the restore is dropped, leaving the machine silent for the rest of
the session (that hazard is written up in `title_and_seed`).

## A test bug this uncovered

`test_display.c` had two `DisplayState`s handed to `display_encode` without
being initialised -- `test_collisions_roundtrip` and
`test_color_state_needs_no_image`. `display_encode` writes `d->dim`, and
`display_decode` defaults an out-of-range dim byte, so both round-trips depended
on stack garbage happening to hold 0..6. They passed for the wrong reason and
started failing the moment the code above it changed what was left on the stack.
Both now seed through `display_defaults` first. Worth remembering: a failing
assertion in those two files may be a stale-stack problem rather than a real one.

## Verification actually performed

- `test_display.c` builds and passes on the host (it is the file that covers all
  three `display_has_art` gates).
- `test_menu_layout`, `test_bg_dim`, `test_presentation` pass. `test_cgl` fails
  for want of `analysis/zork_bg/raw/*.CGL`, which is not in the repo -- unrelated
  and pre-existing.
- 94 Python tests pass.
- `sh syntax-check.sh` clean in DEBUG and release on every changed source;
  `NETBIN=1` clean on `main.cxx`.
- A full `compile-cd.bat release`: 0 errors, links to an ELF, writes a
  7.8 MB ISO and a 32-track raw image.

**Nothing has been seen on screen.** In particular the brightness of the title
wallpaper without the dim, and how the one-shot cue's ending sounds against the
silence after it, are arguments from the source.

## A build size that looks alarming and is not

`BuildDrop/<CD_NAME>.bin` went from 8.9 MB on one build to 575 MB on the next,
and the .cue from 93 bytes to 1324. That is not the append corruption the README
warns about -- it is the disc's 31 CD-audio tracks. The first build after a fresh
checkout finds no `cd/music/*.wav.raw` and writes a data-track-only image while
generating them; every build after it has the audio to add. 32 `TRACK` lines and
a .bin that shrank slightly on the rebuild are what a replaced image looks like,
not an appended one. Checked before writing it down, because the first reading of
those numbers was that the README's footgun had fired.

## Next

- Boot the ISO and look at: the title wallpaper's brightness, whether Dynamic
  appears in Options from a cold boot, where the Options list sits against the
  mode menu, and the jingle ending into silence.
- Answer the Options-alignment question above before any further layout work.
- The three stale branches from [[cgl-only-presentation-handoff]] are still
  waiting to be deleted.
