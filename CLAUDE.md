# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

The project requires Qt (installed via the Qt installer). Set `CMAKE_PREFIX_PATH` to the Qt installation:

```bash
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64
cmake --build build-release
```

To build only the test target:
```bash
cmake --build build-release --target AmazonTemplate3Tests
```

Prerequisites: `sudo apt install libvulkan-dev`

## Tests

**Standard run (mocked, no network):**
```bash
./build-release/AmazonTemplate3Tests/AmazonTemplate3Tests
```

**With real OpenAI contract tests:**
```bash
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64 \
  -DDO_REAL_TESTS=ON -DOPEN_AI_API_KEY="sk-proj-..."
cmake --build build-release --target AmazonTemplate3Tests
./build-release/AmazonTemplate3Tests/AmazonTemplate3Tests
```

There are also individual test executables built alongside `AmazonTemplate3Tests` (e.g., `FillerSizeTests`, `TemplateFillerTests`).

## Architecture

**AmazonTemplate3** is a Qt6 desktop application (C++20) that fills Amazon marketplace product listing templates (Excel files) using AI-assisted content generation via OpenAI.

### Three CMake targets

- **AmazonTemplate3Lib** — static library containing all business logic
- **AmazonTemplate3** — Qt GUI executable that links the library
- **AmazonTemplate3Tests** — test suite (Qt Test framework)

Both the executable and tests depend on shared code from `../../common/` (sibling directory), providing `OpenAi2`, `WorkingDirectoryManager`, and utility types.

### Core domain (AmazonTemplate3Lib)

**`TemplateFiller`** is the central orchestrator. It reads an Excel template (via QXlsx), extracts `Attribute` objects representing product fields, then dispatches to typed fillers.

**`AbstractFiller`** and its eight concrete implementations use the Strategy pattern to fill different attribute types:
- `FillerTitle`, `FillerBulletPoints`, `FillerKeywords`, `FillerText` — text generation via OpenAI
- `FillerSelectable` — picks from a constrained set of allowed values
- `FillerSize`, `FillerPrice` — structured/numeric field handling
- `FillerCopy` — copies values from equivalent attributes

**Attribute management tables** control the fill process:
- `AttributesMandatoryTable` / `AttributesMandatoryAiTable` — which fields are required per marketplace
- `AttributeEquivalentTable` — maps equivalent field names across locales/marketplaces
- `AttributeFlagsTable` — per-attribute behavioral flags
- `AttributeValueReplacedTable` — value normalization/replacement rules
- `AiFailureTable` — tracks fields where AI generation failed

### GUI (AmazonTemplate3)

`MainWindow` orchestrates the workflow. Dialogs:
- `DialogAttributes` — edit attribute values
- `DialogExtractInfos` — extract product info from source material
- `DialogValidateMandatory` — validate required fields before export
- `DialogAddPossibleValues` / `DialogAddValueToReplace` — configure domain rules

Entry point: `AmazonTemplate3/main.cpp` — registers Qt meta types, shows config dialog, applies dark orange theme.

### Async model

All AI calls are async via **QCoro** (`QCoro::Task<>`). The `OpenAi2` singleton wraps the OpenAI HTTP API. In tests, use `OpenAi2::setTransportForTests()` / `TestOpenAi2::setFakeTransport()` to mock network calls, and always call `ai->resetForTests()` in `initTestCase()` to clear singleton state.

### Testing notes

- Fake transports can fire callbacks synchronously or via `QTimer::singleShot(0, ...)` for event loop tests.
- Always set a `QTimer` timeout to prevent `QEventLoop` hangs when a callback chain breaks.
- Avoid capturing `std::function` by reference in recursive lambdas — use `QSharedPointer` or copy capture.
- `OpenAi2` stops retrying immediately on errors prefixed with `fatal:` (e.g., 401 Unauthorized).
