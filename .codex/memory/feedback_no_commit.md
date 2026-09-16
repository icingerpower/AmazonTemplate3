---
name: feedback-no-commit
description: "Never commit without explicit user permission — ask at most, never commit autonomously"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 2717d467-073b-4960-a01c-ed13c3072bdf
---

Never create a git commit without the user explicitly saying to commit. At most, ask "May I commit this?" Do not commit after a successful build, after a fix, or after any task completion unless the user said so.

**Why:** User was surprised by autonomous commits mid-session, especially when the code under commit turned out to be wrong. Also: never commit untested code, even if the build passes.

**How to apply:** Always wait for an explicit "commit this", "git add/commit", or equivalent instruction. "Commit on this new code" counts. "Fix the bug" does not.
