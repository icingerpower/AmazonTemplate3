# AmazonTemplate3 project instructions

## Project context and imported memory

Read `CLAUDE.md` and `.codex/memory/MEMORY.md` when starting work in this
repository. Read the relevant topic notes from that index before changing a
subsystem. `READMEAI.md` contains additional development and A+ upload notes.
The import provenance and known stale details are in `.codex/memory/README.md`.

These notes are historical project context, not proof of current implementation
or API behavior. Check the current source and newer entries before acting on a
TODO, endpoint claim, path, or workaround. The setup corrections below supersede
the older build instructions in `CLAUDE.md` and `GEMINI.md`.

## Workflow

- Never create a Git commit without the user's explicit instruction to commit.
  A successful build or request to fix something is not permission to commit.
- Never commit untested code, even if it builds.
- Inspect `git status` before editing and preserve existing staged, unstaged,
  and untracked work. This repository may contain ongoing user changes.
- Keep credentials out of source, documentation, logs, and commits. Do not
  import `.env`, browser profiles, or authentication state into project notes.
- Keep useful new findings in the relevant `.codex/memory/` topic file and
  update its index when adding a topic. Clearly mark superseded findings.

## Repository map

AmazonTemplate3 is a C++20 / Qt Widgets desktop application for Amazon and Temu
listing templates, translation, validation, pricing, inventory, sizing, A+
content, and Seller Central workflows.

- `AmazonTemplate3/`: GUI executable; widgets and panes under `gui/`.
- `AmazonTemplate3Lib/`: business logic, table models, fillers, API clients,
  sizing/A+ workflows, and marketplace abstractions.
- `AmazonTemplate3Tests/`: Qt Test executables.
- `case-worker/`: TypeScript / Playwright Seller Central automation.
  The GUI invokes `src/oneshot.ts` through `CaseWorkerRunner` / `QProcess`;
  the TCP server remains a manual debugging tool.
- `../common/` relative to the repository root: shared OpenAI client, AI CLI,
  secrets, utilities, types, and working-directory code. CMake subdirectories
  refer to this sibling as `../../common/`.
- `compat/`: compatibility headers for dependencies.

## Build and validation

Dependencies include Qt 6 (Widgets, Network, Core, Gui, Test, DBus), QCoro6,
QXlsxQt6, OpenSSL, Zlib, and the sibling `common` repository.

On the machine inspected during import, Qt is installed at
`/home/cedric/Qt/6.8.3/gcc_64`; the older documented `6.7.3` path is absent.
The existing `build-release` cache resolves Qt 6.8.3 despite retaining an old
`CMAKE_PREFIX_PATH`. Prefer the existing configured build for routine work:

```bash
cmake --build build-release
cmake --build build-release --target AmazonTemplate3Tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build-release --output-on-failure
```

For a fresh offline build, from the repository root:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.8.3/gcc_64 \
  -DDO_REAL_TESTS=OFF -DDO_REAL_AMAZON_TESTS=OFF \
  -DOPEN_AI_API_KEY=offline-test-placeholder
cmake --build build-release
```

The placeholder satisfies an unconditional CMake key check even when real API
tests are disabled; it is not a working credential. Adapt the Qt path on other
machines. For a fresh debug build, use a separate build directory and
`-DCMAKE_BUILD_TYPE=Debug`. The only preset is named `debug` and lives under
`AmazonTemplate3/CMakePresets.json`; there is no `gravity-debug` preset or
root preset file, so the legacy root-level command does not work as documented.

For worker changes, run `npx tsc --noEmit` from `case-worker/` after dependencies
are installed (`npm ci`; its postinstall installs Chromium). See
`case-worker/README.md` for configuration and manual browser workflows.

Run the relevant tests for code changes. Keep real OpenAI and Amazon contract
tests opt-in. For OpenAi2 unit tests, reset the singleton, use fake transports,
simulate async replies with `QTimer::singleShot`, and guard event loops with
timeouts. `AttributeFlagsTableTests` currently exists as an executable but is
not registered with CTest; run it directly when changing that model.

## Critical lessons carried over from Claude

- Use `AbstractCli::runPromptAsync` bridged to `QFuture` instead of directly
  awaiting `runPrompt`; the notes document coroutine lifetime crashes. Keep
  asynchronous tasks alive through completion.
- For `runPromptAsync`, use `CliRunResult.output` directly.
  `extractTextFromOutput` is for translation stream-JSON output only.
- Never throw from OpenAi2 step callbacks running in Qt network slots. Record
  failures and handle the coroutine's exception at its caller; explicitly
  catch `ExceptionOpenAiError` and `ExceptionOpenAiNotInitialized` as needed.
- Keep marketplace synchronization independent of the inventory source;
  the recorded plan includes Octopia alongside Amazon FBA.
- Preserve draft review for Seller Central replies. Only the user marks cases
  done. Review the latest entries in `project_case_worker.md`; earlier entries
  and the original index describe superseded designs.
- Read the SP-API, A+, Temu, inventory, and variation notes before changing
  those integrations. Several notes record successful responses that did not
  persist changes, so verify resulting state when testing an authorized write.
