---
name: multizork-prompt-rewording-handoff
description: Every player-facing prompt in multizorkd reworded to the owner's dictation, the lobby rebuilt as one 40-column list, and the transcripts site stripped to transcripts; squashed to main as dc453f3, never built or run.
metadata:
  type: project
---

Squashed to `main` as `dc453f3` (pushed; pre-squash tip `319fb8d` is in the reflog if the twenty
individual commits are ever wanted). Sits directly on [[multizork-lobby-and-four-seats-handoff]],
whose three playtest risks are **still open and still unbuilt** — that handoff is not superseded.

Touched four files only: `saturn/multizorkd.c`, `saturn/multizork-transcripts.php`,
`saturn/tests/test_multizork_join.py`, `saturn/tests/test_multizork_lobby.py`. The commit message
and `git show dc453f3` carry the what; this file carries what they cannot.

**Nothing here has been compiled for real or executed.** Same reason as the previous handoff:
multizorkd wants POSIX and sqlite3, the box is Windows.

## How the work was driven

The owner dictated replacement text prompt by prompt, in fifteen numbered items plus four
follow-up chunks, working front to back through a session-inventory of every string. There is no
spec or plan document — the conversation was the spec. The numbering is gone now; the code is the
record.

## The compile gate (session-scratch, will not survive)

`gcc -fsyntax-only` against hand-written stub POSIX headers. It proves the file parses and
type-checks and nothing dangles. It says nothing about linking or runtime. Recipe, if it needs
rebuilding:

- Stub headers written to scratch: `poll.h`, `sys/socket.h`, `netdb.h`, and a `prelude.h` forced in
  with `-include` supplying `gid_t`/`uid_t`, `random`, `fcntl`, `unsetenv`, `F_GETFL`/`F_SETFL`,
  `O_NONBLOCK`, `AI_V4MAPPED`, `AI_NUMERICSERV`, `SIGPIPE`, `SIGQUIT`.
- Real `sqlite3.h` comes from msys64: `-isystem /c/msys64/mingw64/include`.
- `cd saturn && gcc -fsyntax-only -Wall -Winfinite-recursion -I. -I<stubs> -isystem /c/msys64/mingw64/include -include <stubs>/prelude.h multizorkd.c`

`-Winfinite-recursion` is in that line for a reason: a factoring of the lobby prompt into
`write_lobby_prompt` briefly left the helper calling itself, and plain `-Wall` passed it clean.
Keep the flag.

## Riskiest change, if a playtest goes wrong

`get_room_name` in `multizorkd.c` is the only place this work reached into the Z-machine rather
than editing a string. The story can only *print* a room name, so the function swaps
`GState->writestr` for a capture function, runs the property-table lookup `print_obj` uses, and
restores. It is guarded to object ids 1..255 because `getObjectPtr` calls `die()` outside that
range. It returns a pointer to one shared static buffer — two names cannot be live at once; the
two callers consume each before asking for the next. A garbled or empty room name in the
`left`/`entered` notices points here first.

Second-riskiest: the lobby's column arithmetic. Every row must land on exactly 40 characters —
`%3d) ` = 5, name 16 (clipped to 15), status 14, seats 5. Room names run to 22 characters, so
clipping is expected and by design; a *wrapped* line is the bug.

## Verification gap

Eight or so assertions across the two TCP suites were rewritten to follow the new wording, and four
cases were added (lobby numbering off-by-one, guest-leaves-to-lobby, host-closes-to-lobby,
empty-line-reprints). **None has ever executed.** They need a live daemon:
`MULTIZORK_ADDR=host:port python3 saturn/tests/test_multizork_lobby.py`. They skip silently when
nothing answers, so a green run against a dead port means nothing.

Two paths have no coverage at all and cannot get any from the TCP harness: the new-room failures
need a failed 92KB malloc or an unreachable SQLite.

## Open owner decisions

- `multizork-transcripts.php`: `$title` still reads `multizork`, so tabs say `multizork - game
  <room>`. Player pages still head `Transcript for player '<name>'` while game pages were shortened
  to the bare name.
- `multizork-transcripts.php:119` uses `strftime`, deprecated since PHP 8.1 and **removed in PHP 9**
  — it will fatal the game page's timestamps on a current runtime.
- The player transcript pages now have no route back to their game: the nav bar that linked them is
  gone and nothing replaced it.
- The `!!` global-chat notice reads `*** <name> voice echos "..." ***` — kept verbatim as dictated;
  `echos` and the missing possessive were left alone as possible deliberate flavour.

## Deployment note that is not in any file

`fail()` now emits an HTTP status and no body at all. nginx will only substitute its own error page
if the location block carries `fastcgi_intercept_errors on;` (or `proxy_intercept_errors` behind a
`proxy_pass`) **and** an `error_page 404 ...`. Without both, a bad URL renders a blank page rather
than an error page. Untested — there is no nginx config in this repo.

## Tooling hazard

The Bash tool collapses `\\` to `\` inside `<<'HEREDOC'` blocks. Any Python that must match literal
`\n` in C source silently fails to match, or worse, writes a real newline. Two edits were lost to
this before it was diagnosed. Write the script to a scratch `.py` file with the Write tool and run
it, or build the backslash with `chr(92)`. Compounding it: `multizorkd.c` is CRLF, so multi-line
patterns need `\r\n`, and `sed | cat -A` strips the CR and will lie to you about line endings.

## Suggested skills

- **`superpowers:systematic-debugging`** — the moment the owner's build produces a wrong prompt,
  a wrapped lobby line, or a bad room name. Symptom first; do not re-read this file and guess.
- **`superpowers:verification-before-completion`** — before telling the owner anything here works.
  The whole body of work is cross-compile-only; that claim must not soften by retelling.
- **`superpowers:test-driven-development`** — if the untested paths (new-room failures) get
  coverage, or if the next prompt change wants a test first.
- **`code-review`** — `dc453f3` is one squashed commit of 521 insertions across a reworded surface;
  reviewing it against the conversation is no longer possible, so review it against the code.

Do **not** reach for `superpowers:brainstorming` on a wording change: the owner dictates the text
and expects it applied, with slips flagged rather than smoothed. Several dictated lines were
self-contradictory or carried typos; the right move each time was to apply the sensible reading,
say plainly what was changed and why, and let the owner override.
