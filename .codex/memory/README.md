# Claude memory import

Imported on 2026-09-14 for AmazonTemplate3.

## Sources

- Repository instructions: `../../CLAUDE.md` (retained in place and referenced
  by the root `AGENTS.md`).
- Additional repository notes: `../../READMEAI.md` and `../../GEMINI.md`
  (retained in place).
- Claude project memory:
  `/home/cedric/.claude/projects/-home-cedric-Applications-AmazonTemplate3/memory/`.

All 15 Markdown files from that project memory directory were copied verbatim:
the [original index](MEMORY.md), one user feedback note, and 13 project notes.
Original filenames and metadata are preserved. Trailing whitespace was normalized
when preparing the 2026-09-16 commit; later findings were appended to topic notes. No Claude session transcripts,
settings, credentials, unrelated project memories, or browser state were copied.
The repository's `.claude/` directory was empty at import time.

The root `AGENTS.md` directs Codex to read these files. This is a project-local
snapshot, not an automatic synchronization with Claude's memory directory.
Future edits to these copies do not update the originals, and vice versa.

## Interpretation and verified corrections

- Treat the original index and topic files as historical notes. Later entries
  within a topic may supersede earlier statements, and current source takes
  precedence over stale implementation claims.
- The case-worker index still says the Qt side is TODO and mentions autoSend.
  The topic's later entries and current files describe the implemented
  `PaneCases` / `CaseWorkerRunner` one-shot flow and review dialog.
- Temu notes retain explicitly superseded experiments. Read the later solved
  findings before reusing an old request shape or assuming a blocker remains.
- Historical wiki-style links are retained verbatim; some use hyphens where
  actual filenames use underscores. Use `MEMORY.md` for Markdown file links.
- Qt 6.8.3 is installed locally; the documented Qt 6.7.3 path is missing.
  `build-release/CMakeCache.txt` resolves Qt 6.8.3 and disables real API tests.
- Shared code is in the sibling `../common/` from the repository root.
- There is no root CMake preset file. The legacy preset is inside the GUI
  subdirectory; use the root configure commands in `AGENTS.md` instead.
- Fresh test configuration requires `OPEN_AI_API_KEY` even with real tests
  disabled. An offline placeholder satisfies that configure-time check.

Only documentation was added during initialization. Existing pricing work was
preserved. Import validation checks file equality and index links; no application
build, test suite, live API call, or Seller Central action is part of this import.
