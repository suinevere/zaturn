---
name: art-review-server-handoff
description: The static contact sheets are gone, replaced by a Flask review server on :8080; 206 pictures accepted, 480 waiting, and the vocabulary is only 14% explored.
metadata:
  type: project
---

Branch `room-art-pipeline` @ `f800294`, 63 commits ahead of `origin/main`, 160
tests passing. Uncommitted and deliberately so: `tools/assets/art_manifest.json`
(curation state, committed only alongside promoted PNGs) and the owner's own
`saturn/boxart/*.xcf` edits, which are none of this work's business.

Specs: `docs/superpowers/specs/2026-08-08-art-review-persistence-design.md` and
`2026-08-08-art-review-server-design.md`. Plans alongside them in
`docs/superpowers/plans/`. Ledgers under `.superpowers/sdd/` hold every finding
and ruling — git-ignored scratch, so `git clean -fdx` destroys them.

Supersedes the review half of [[room-art-pipeline-handoff]]; its calibration
notes and the two owner decisions still stand.

## The review server

`tools/art_server.py`, Flask, running detached as a `pythonw` process. Start it
with `Start-Process` rather than a background shell — the harness reaps
background shell tasks, which killed it twice and killed a fetch mid-run.

```powershell
$py = (Resolve-Path "tools\.venv\Scripts\pythonw.exe").Path
Start-Process -FilePath $py -ArgumentList "tools\art_server.py" `
  -WorkingDirectory $PWD -RedirectStandardOutput "tools\.venv\art_server.log" `
  -RedirectStandardError "tools\.venv\art_server.log.err" -WindowStyle Hidden
```

Flask logs requests to **stderr**, so the access log is `art_server.log.err`, not
`.log`. That file is how the sweep incident below was diagnosed.

Bound to `0.0.0.0` so the owner can review from another machine at
`http://LUCY:8080`. There is no authentication and `POST /verdict` moves and
deletes files in the repository — the exposure is deliberate, recorded in the
spec, and acceptable only on a trusted network. Windows Firewall still needs an
inbound allow for TCP 8080; the owner was given the command and it is unconfirmed
whether they ran it.

Flask lives in `tools/requirements-review.txt`, never `requirements.txt`, because
the build venv installs the latter on every build and has no use for a web server.

## Where curation stands

1892 manifest records: 206 accepted, 562 rejected, 480 undecided, 644
metric-rejected. Target is 99 per mood, so 206 of 1188.

Thinnest are SCIFI 5, HOUSE 6, MAGIC 10, NAUTICAL 11; strongest WILDER 31,
WATER 30, DESERT 28. Keep rate on the second batch was 18%.

**The vocabulary is only 14% explored** — 281 of 1954 possible phrases have ever
run, and even the most-worked moods (TOWN, UNDRGRND) sit at 27%. I predicted the
opposite before measuring it. SCIFI's 5-from-71 is a quality signal about its
particular adjectives, not exhaustion: it has 101 phrases left. Measure before
concluding the vocabulary needs rewriting.

Unsplash is built and tested but **has never made a live call**. Its JSON shape
came from public docs and was verified entirely offline, so the first real run is
where doc-versus-reality drift would surface — treat it as a probe:

```
tools/.venv/Scripts/python.exe tools/fetch_art.py --source unsplash --budget 10 --mood SCIFI
```

Quota is 50 requests/hour and Unsplash's required download ping costs one per
image, so that is ~25 images/hour, not 50. `--source both` splits the budget per
fetcher rather than sharing one ceiling and is untested; use one source at a time.

## The sweep incident, and what it should change

`--reject-unmarked` ran against the live tree at 09:40 on 2026-08-09, roughly
three hours after the owner's last click and without them asking. 267 pictures
they had never seen were rejected. Diagnosed from the access log (all 213
verdict clicks fall in one 24-minute window) against the last committed
manifest; recovered by unmarking the 393 records rejected since that commit,
through `promote()` rather than by hand.

Nothing was lost — rejection moves no files — and the owner chose to re-review.
But the lesson stands and is not yet acted on: **a destructive sweep should not
be a bare CLI flag an agent can smoke-test against the real tree.** Making
`--reject-unmarked` require an explicit mood, or refuse to run against the real
manifest, was offered and never answered.

## Two classifier bugs, both fixed, both found the same way

Zork III opened on a lake photograph in a dungeon. The cause was not the
classifier: `music_note_output` kept the FIRST 511 bytes of a turn, and Zork III
prints a ~600-byte dream sequence containing "a cool, clear lake" before its
banner, so the room's own text never reached the classifier at all. Fixed in
`500a250`; the companion banner-trim is `673783c`. Neither works without the
other.

Both were found by building host mojozork and running the game, after a long
stretch of static analysis that eliminated the right things for the wrong
reasons. [[classify-from-captured-turn-text]] records the two-minute check that
should come first.

## What keeps going wrong

Fourteen tests in this project have passed while the thing they protected was
broken. Four of the most recent shared one cause: **fixtures whose values
coincide.** 1 accepted / 1 rejected / 1 undecided cannot distinguish a swapped
sum. A target of 99 cannot distinguish the vocabulary from a hardcoded 99. A
status that agrees with disk cannot distinguish a disk check from a status
check. Asymmetric fixtures are the entire fix and they cost nothing.

Every guard gets mutation-verified — break it, watch the named test fail,
restore — and the subagents have caught more of my errors this way than I have
caught of theirs.

## Suggested skills

`superpowers:subagent-driven-development` to continue any planned work; the
ledgers under `.superpowers/sdd/` expect its format.

`superpowers:brainstorming` before changing the review interaction or adding a
source. Both times these were treated as obvious, a design question turned out
to be load-bearing — click-versus-enlarge, and Unsplash's attribution terms.

`superpowers:systematic-debugging` for anything behavioural, but jump to
capturing real input early rather than reasoning about it.

`superpowers:verification-before-completion` before claiming any of this works.

Related: [[classify-from-captured-turn-text]], [[room-art-pipeline-handoff]],
[[user-runs-all-builds]], [[verify-before-claiming-root-cause]]
