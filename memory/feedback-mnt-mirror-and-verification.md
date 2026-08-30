---
name: feedback-mnt-mirror-and-verification
description: Working approach for this repo — mirror sync quirk and patch verification
metadata:
  type: feedback
---

Two working norms established on this project:

**1. The bash/`mnt` mirror lags and truncates large/recent Write+Edits.** Files edited
via the file tools sometimes show up truncated or with null bytes when read from the
Linux `mnt` mount (the authoritative Windows copy is fine).
**Why:** Cowork sandbox mirror sync delay. **How to apply:** to run/test code, copy
`saturn_translate` into `/tmp/run2` and run with
`PYTHONPATH=/tmp/run2 PYTHONPYCACHEPREFIX=/tmp/pycX`; if a module shows BAD on
`ast.parse`, rewrite that one file into /tmp via heredoc. Verify authoritative files
with the editor's Read tool, not bash. (Under the Claude Code CLI on the real local
filesystem this quirk shouldn't apply.)

**2. Always verify a patch against ground truth, not just self-consistency.** A
self-verifying xdelta only proves the encoder round-trips, not that the edit is
semantically right. Two real bugs were caught only by comparing the auto-detector's
output to a hand-verified offset: a coincidental pointer match, and a base-alignment
ambiguity with the menu pointer table. For Saturn language patches, confirm the JP
loader pointer value is unique in the image and the inferred load base is
page-aligned. See [[reference-tande-english-activation]].
