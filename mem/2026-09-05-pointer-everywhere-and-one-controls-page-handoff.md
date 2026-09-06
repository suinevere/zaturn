---
name: pointer-everywhere-and-one-controls-page-handoff
description: Nine commits on art-v2 turning every menu, prompt and pageable screen into something a mouse or a gun can work, then rebuilding the whole input mapping and folding the Controls tree into one page; the mouse's real bug was a byte-wide mask on a sixteen-bit accumulator, and the whole session ran on owner reports rather than on anything seen here.
metadata:
  type: project
---

Nine commits on `art-v2`, ahead of `origin/main` and unpushed: `b5a6c50`,
`17ebaf0`, `75bd14a`, `3bb31c6`, `0cb43d6`, `a490fd2`, `490d285`, `7d26a31`,
`cef9236`. Each message carries its own reasoning; do not re-derive it here.

Read `docs/CONTROLS_MATRIX.md` first. It now carries the chord model, the Mouse
row, the one-page layout, the button pool, the Twin Stick's report and the
departures from the workbook, all of which changed this session. Then
[[saturn-mouse-is-a-16bit-accumulator]] before touching `read_mouse` again.

Supersedes [[controls-page-and-gun-targeting-handoff]] on the page's shape: the
device pager, the per-sheet submenus, the L+R interface swap and the Caps row it
describes are all gone. Its account of the cursor, the gun targets and the scroll
markers still stands.

## What the owner decided, in order, over the session

Each of these arrived as a correction to what had just been built. Treat as
settled; the reasoning is in the commit that carries it.

1. Right click and a gun shot off the screen are **Back**, everywhere, and that
   departs from the workbook's mouse column deliberately.
2. **No legends.** A yes/no box draws the words Yes and No; a slider draws the
   arrows a pointing device clicks. A legend naming buttons is unanswerable to a
   device that has none of them.
3. **Fades stay** between every menu and its children. The one report that read
   as "remove the fade" was a bug report about a missing one.
4. **One configuration for both interfaces.** The Actions rows no longer change
   shape with the Interface row.
5. **One button, one action**, over a pool of six (A B C X Y R). Z and L are
   outside it, carrying Map and the interface swap.
6. **No Mouse Mode switch.** One Mouse row per device naming what drives its
   cursor, Off first, hidden where there is nothing to choose.
7. **The caret chord is gone from the pad.** Moving the insertion point one
   character at a time is a keyboard's job.
8. **The Twin Stick profile is a chord** (L+R+Z+X), not a menu row, and its bit
   table is the owner's own -- no longer provisional.
9. **Caps Lock is a Gameplay row**, no description line.

## What is unverified

Nothing since `b5a6c50` has been seen running. The owner built once mid-session
(the BuildDrop map is from that build) and reported against it; everything after
`17ebaf0` is compiled-and-tested only.

- **The Twin Stick right stick** now drives the cursor off the owner's table, but
  no Twin Stick has been in anyone's hands. `TWIN_RS_*` and `TWIN_TOP_*` in
  `controller.cxx` are the one place to correct if it reads wrong.
- **The racing wheel** reading is untested: SRL exposes only Axis1 for a wheel and
  answers Axis2 with a zero it does not have, which is guarded, but the paddles
  being L and R is inference from the digital word, not a measurement.
- **Every layout arithmetic on the rebuilt Controls page** -- row positions, the
  box height, the pointer hit tests -- is unseen. The rows and their hit tests are
  computed from one set of `y_*` locals per page for exactly this reason.
- **The chord tap** (a press of the modifier released with no direction under it
  scrolls one line back) has never been felt. It is measured on the release; if it
  reads as a delay, that is why.

## Traps this session hit

- **The bash heredocs used to edit files collapse `\` to `\`.** A python patch
  script containing `'\0'` reached python as a NUL. Write `chr(92)` instead. A
  heredoc much past 150 lines also fails to parse; split it.
- **`sed -n '/a/,/b/p'` restarts matching** and prints several disjoint ranges,
  which reads as a corrupted file when it is not. Verify with `awk` or a real
  compile before "fixing" what it shows.
- **`test_netbin_lift.py` compares whole function bodies** between
  `menu_pages.cxx` and `net/netbin_pages.cxx`. Editing one and not the other is
  the failure; re-lift the body rather than hand-editing twice.
- **Two tests fail and are not this work**: `test_hwram_budget` (the image grew
  past the story headroom floor, measured before this session's changes) and
  `test_lwram_budget` (missing art archives). The hwram one reads BuildDrop's map,
  so it will not change until the owner builds again -- and this session measured
  **-11.8 KB** of text+data against a clean HEAD tree, mostly from routing 78
  menu call sites through one `pad_nav` instead of 78 inlined MultiPad scans.
- **The owner runs every build and every emulator session.** Cross-compile with
  `saturn/syntax-check.sh`, both plain and `NETBIN=1`.

## Open questions the owner has not answered

Raised in replies, not pressed:

- Should the Twin Stick profile **persist** across a boot? It does not today.
- The L+R+Z+X toggle is **silent**; an accidental press makes a control pad behave
  strangely with nothing on screen saying why.
- **Cancel on the Gameplay page does not revert a Caps toggle** (it takes effect on
  the press, as it did on Controls).
- The **Interface row** was kept, now that it only picks a starting surface.
- The **in-game command panel** (compass rose, word page, command list) is still
  pad-only. It is the one thing the workbook asks for that a mouse cannot work.

## Where the blob stands

`options.cxx` mapping sentinel is **6**. Sentinels 2-5 are read for structure only
and both mappings fall back to defaults: the button pool went four to six, the
chord slots changed vocabulary twice and lost an action. Nothing shipped ever
wrote a 4 or a 5.

## Suggested skills

- `superpowers:verification-before-completion` before any "this works" claim: this
  session's only real bug was found by disassembling `LIBSGL.A`, not by reading it.
- `superpowers:systematic-debugging` for the next owner report, and read
  [[verify-before-claiming-root-cause]] with it.
- `superpowers:test-driven-development` if the command panel is next -- it has no
  test coverage at all and three modules with their own cursors.
- `code-review` against `6f4b245` before pushing: nine commits, sixteen files.
