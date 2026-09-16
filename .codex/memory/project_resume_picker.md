---
name: resume-picker-all-projects-hang
description: "/resume \"all projects\" (Ctrl+A) hangs forever at >1000 project dirs; script-generated per-ASIN dirs archived 2026-08-02"
metadata:
  node_type: memory
  type: project
  originSessionId: 6de6099e-d990-41ef-bafc-03be0c1b4ce3
  modified: 2026-08-02T09:56:42.131Z
---

The /resume picker's "show all projects" mode (Ctrl+A) hangs forever on "Refreshing…" when `~/.claude/projects/` contains >1000 project dirs (verified: hangs at 1160, works at 8; Claude Code 2.1.215 and 2.1.220). No file corruption involved — all session .jsonl files were valid; project-scoped mode always worked.

**Why:** The user's Qt app shells out to the claude CLI with cwd set to per-ASIN Dropbox folders (`~/Dropbox/freelancers/projects/workingDirectory/amazonTemplate3/warnings/{country}/{ASIN}`), creating a new project dir in `~/.claude/projects/` per ASIN — ~1150 accumulated, hundreds new per week.

**How to apply:**
- 1153 script-generated dirs were moved to `~/.claude/projects-archive-20260802/` (rollback: `mv ~/.claude/projects-archive-20260802/* ~/.claude/projects/`).
- The count regrows as the warning scripts run; re-archive periodically (`mv ~/.claude/projects/-home-cedric-Dropbox-* <archive>/`), or change the app to run the CLI from a fixed cwd.
- When diagnosing picker "hangs" via `script`-captured pty output, beware: the TUI redraws only changed cells, so an unchanged result list looks like a hang in the captured log.
