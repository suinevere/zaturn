---
name: controls-and-panel-interface-handoff
description: Eleven commits reworking the Controls page, the panel's buttons and chrome, and the display border — two confirmed on screen, the other nine only syntax-checked.
metadata:
  type: project
---

Landed on `main` as a single squashed commit on top of `b47b2fb`. Continues the
work [[command-panel-and-dim-handoff]] describes; that entry's "never been
compiled or run" is now **stale** — see the note at its head.

The eleven original commits, each with its own reasoning in its message, are kept
at the local tag **`pre-squash/controls-panel-2026-08-13`** — `git log --oneline
b47b2fb..pre-squash/controls-panel-2026-08-13` reads them. That tag is local
only; if this machine is lost so is the granular history, and the squashed
message plus this entry are what remain. What follows is only what neither can
say.

## What the owner has actually seen

The owner ran builds during this session. Confirmed working on screen:

- **Panel Recall** (`X+Up/Dn` loading a past command into the panel's line), and
  the cursor holding still under a chord shift.
- **The black display border** (`border_use_black`).

Everything else is syntax-checked only (CD and NETBIN, DEBUG and release) plus
host tests. Two things were seen incidentally rather than verified: the panel
renders with its black backing, and the word box was showing `look` twice, which
is what prompted the verb-dedup change. **Do not read that as sign-off on the rest** —
nobody has confirmed the Controls page layout, the Interface row, the A/C split,
or the bottom border.

## Decisions the owner made, so they are not re-litigated

Asked and answered this session; treat as settled:

1. **Panel rows are remappable, over one shared mapping.** The Controls page is
   two *views* of `g_face_btn`/`g_chord_slot`, not two tables. No save-format
   change. The cost is documented at `CTL_PANEL`/`CTL_KBD` in `menu_pages.cxx`:
   remapping a shown row onto a hidden row's slot still swaps, invisibly.
2. **Gameplay keeps nothing.** The Interface row moved out of it entirely;
   Controls is now the only editor for `g_cmd_iface`.
3. **The panel's A/C split**: A submits the line, C picks the highlighted word,
   direction or command. Auto-submit on a completed grammar chain stays, so A is
   for sending a line the chain has not finished with.
4. **The bottom border carries nothing** — no hints, no focus highlight. Both
   rows are one `CV_BORDER` string.

## Things that will bite

**`SL_XUD` put a chord on X, which is also the default Space button.** In the
Keyboard interface `X+Up` types a space *and* recalls; `history_load` overwrites
the line so it is invisible, but it is real. Reasoned about in the `SL_*` enum
comment in `input.h`. The panel has no Space, which is why X was free there.

**The image-suppressing VDP2 window moved out of `menu.cxx`** into
`console_view.c` (`image_window_box`/`on`/`off`), because the in-game strip and
menu boxes now share it. Only one rectangle exists in hardware; they never
collide because menus are modal. `image_window_on` cancels the window-off that
`~MenuBacking` owes to the next text flush — remove that and closing a menu
switches the window off underneath a game frame that just armed it.

**`border_use_black` reaches into SGL by address.** The derivation is in the
`SGL_TVMD_SHADOW` comment in `console_view.cxx`, cross-checked five ways. It
works, but nothing enforces it: an SGL version bump could move the block and it
would fail silently, as a coloured border rather than a crash.

**`CV_VERB_CORE` was `[16]` iterated by a hardcoded `16`.** Now implicit-size
with `CV_VERB_CORE_N`. Removing an entry under the old form put a null into
`room_model_has_word`.

**The word list has two verb sources.** Curated table *and* everything of
`TYPE_VERB` in the story dictionary. Dropping a word from the table alone does
not remove it — `cv_in_cmd_module` filters the trie tail too.

## Open

- **Panel recall's preposition case.** A recalled two-word line lands on
  `CP_SLOT_DONE`; a verb that takes a preposition needs one extra Back. The
  panel cannot know without a pick to ask the trie on. Left as-is.
- **`cp_back` decrements the slot blindly**, so backing out of a two-word line
  lands on `NOUN2` rather than `NOUN`. Pre-existing, affects hand-built lines
  equally, not introduced here.
- **No on-screen hint names A=send.** All three module borders were full and are
  now deliberately empty; the Controls page is the only place it is written down.
- **The netbin Controls page now configures an interface netbin does not have.**
  `test_netbin_lift.py` had been failing on `controls_page` since before this
  session — `netbin_pages.cxx` kept a reduced copy that never gained the
  Panel/Keyboard Swap row. It was fixed by re-lifting verbatim, which is what
  the test demands, but the cost is that netbin's page now opens on the Panel
  view (`g_cmd_iface` defaults to `IFACE_PANEL`) and lists Type Word, Cycle
  Module and a Panel/Keyboard Swap for a build with no command panel in it. The
  alternative, if that reads badly on screen, is to drop `controls_page` from
  that test's pairs and let netbin keep a deliberately reduced page — but then
  nothing guards the other three lifted bodies from drifting the same way.
- The four owner decisions in [[command-panel-and-dim-handoff]] are untouched.

## Verification state

Host tests pass: `test_command_panel` (with new `cp_submit` and `cp_load_line`
cases), `test_command_rose`, `test_menu_layout`, `test_bg_dim`, and
`test_title_bg_dim.py`. `test_display` still needs `HEAD`'s `category_art.inc`,
not the working tree's — the owner's art regeneration is still uncommitted and
untouched, as it has been all along.

## Suggested skills

- **`superpowers:verification-before-completion`** — nine of eleven commits are
  unseen. Do not let a claim through without the owner running it.
- **`diagnosing-bugs`** if the hardware run misbehaves, and read
  [[verify-before-claiming-root-cause]] first. This session had a live example:
  a resolution fix that was correct and changed nothing visible, because
  [[back-screen-colour-fills-the-border]] made both states render identically.
  A no-op on screen is not proof a change was wrong.
- **`code-review`** with base `b47b2fb` — this went to `main` unreviewed and
  mostly unseen, so the review is owed rather than optional. `menu_pages.cxx`
  and `command_view.cxx` carry most of the risk.

## Hard rules

The owner runs every build and every emulator session — [[user-runs-all-builds]],
[[never-edit-mednafen-config]]. Cross-compile with
`sh saturn/syntax-check.sh <file>`, and `NETBIN=1` for anything in the netbin
source list. If the owner reports a fix did not work, that is ground truth:
research the mechanism, do not question whether they tested it.
