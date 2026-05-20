# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Application Does

AmazonTemplate3 is a Qt desktop tool for Amazon (and Temu) product listing template management. It reads product attribute data from Excel (`.xlsx`) source templates, fills/translates/validates attribute values across multiple marketplace country/language variants, and writes populated templates back to disk. It integrates with the OpenAI API (GPT models) to automatically generate or translate attribute values that cannot be derived mechanically.

## Build

Requires Qt 6.7.3 at `/home/cedric/Qt/6.7.3/gcc_64` and `libvulkan-dev` (`apt install libvulkan-dev`).

**Release build:**
```bash
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64
cmake --build build-release
```

**Debug build (Ninja, via preset):**
```bash
cmake --preset gravity-debug
cmake --build build-gravity-debug
```

**Test binary only:**
```bash
cmake --build build-release --target AmazonTemplate3Tests
```

**Real OpenAI contract tests (requires API key):**
```bash
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64 \
  -DDO_REAL_TESTS=ON -DOPEN_AI_API_KEY="sk-proj-..."
cmake --build build-release --target AmazonTemplate3Tests
```

## Tests

Test framework: Qt Test (`QTest` / `QTEST_MAIN`). Each test file compiles to its own executable.

**Run all tests:**
```bash
cd build-release && ctest
```

**Run a specific test binary directly:**
```bash
./build-release/AmazonTemplate3Tests/AmazonTemplate3Tests
./build-release/AmazonTemplate3Tests/TemplateFillerTests
./build-release/AmazonTemplate3Tests/AttributesMandatoryTableTests
# etc.
```

### OpenAi2 Test Seam

`OpenAi2` is a singleton gated by `#ifdef OPENAI2_UNIT_TESTS` (set automatically for the `_Tests` library variant). Rules for writing tests that involve it:

- Always call `ai->resetForTests()` at the start of each test.
- Use `setTransportForTests(...)` or the `setFakeTransport(...)` helper to mock all network calls — never make real HTTP in unit tests.
- Simulate async responses with `QTimer::singleShot(0, ...)`.
- Always guard `QEventLoop` with a `QTimer` timeout to prevent test hangs.
- Fatal errors (responses starting with `"fatal:"`) cause `OpenAi2` to stop retrying immediately — use them when you want single-shot failures.

## Architecture

### Module Layout

```
AmazonTemplate3/       ← Qt Widgets GUI executable
AmazonTemplate3Lib/    ← Static library (all business logic)
AmazonTemplate3Tests/  ← Test executables (one per test class)
../../common/
  openai/              ← OpenAi2 HTTP client singleton
  utils/               ← CSV, file, string utilities
  workingdirectory/    ← WorkingDirectoryManager, DialogOpenConfig
  types/               ← Shared type definitions
```

### Key Components

**`TemplateFiller`** (`AmazonTemplate3Lib/`) — The core engine. Reads source `.xlsx` via QXlsx, extracts field IDs/values/SKUs/product types, then orchestrates filling by iterating `ALL_FILLERS_SORTED` to find the right filler for each field. Returns `QCoro::Task<void>` for async operations.

**Filler hierarchy** (`AmazonTemplate3Lib/fillers/`) — `AbstractFiller` base with `canFill()` / `fill()` (both `QCoro::Task`). Concrete fillers: `FillerTitle`, `FillerBulletPoints`, `FillerKeywords`, `FillerText`, `FillerSelectable`, `FillerSize`, `FillerPrice`, `FillerCopy`. `FillerSelectable` is the most complex — it queries AI for value equivalence across marketplaces/languages and uses `DialogAttributes` as a human-review callback.

**`OpenAi2`** (`common/openai/`) — Singleton HTTP client for the OpenAI Responses API. Supports sequential step queues, multi-attempt collection (`StepMultipleAsk`), AI-judged best reply (`StepMultipleAskAi`), and batch calls. Features: response caching keyed by `cachingKey`, retry with model escalation (falls back to `gptModelAfterHalfFailure`), concurrency throttling, and a `m_blockedUntilMs` circuit breaker.

**Table models** (all `QAbstractTableModel` subclasses in `AmazonTemplate3Lib/`) — Persisted to `QSettings`:
- `AttributesMandatoryTable` — mandatory fields per product type
- `AttributeEquivalentTable` — maps attribute values between marketplaces
- `AttributeFlagsTable` — flag bits (ChildOnly, NoAI, SameValue, Copy, MandatoryAmazon, MandatoryTemu…)
- `AttributeValueReplacedTable` — explicit value substitution rules
- `AiFailureTable` — per-field AI errors

**`Attribute`** (`AmazonTemplate3Lib/Attribute.h`) — Value object holding per-marketplace flags and possible values as a nested hash: `marketplace → countryCode → langCode → category → QSet<QString>`. Supports `AMAZON_V01`, `AMAZON_V02`, and `TEMU_EN`.

### Data Flow

```
DialogOpenConfig (pick working directory)
  → MainWindow initializes OpenAi2 with API key from QSettings
  → User selects source .xlsx + target .xlsx
  → TemplateFiller::buildAttributes() reads field IDs, possible values, flags
  → TemplateFiller::fillValues() [QCoro coroutine]
       → For each field: iterates ALL_FILLERS_SORTED → canFill() → fill()
       → fill() applies rules or calls OpenAi2 for AI content
       → Results written back to target .xlsx via QXlsx
```

### Key Technologies

| Dependency | Role |
|---|---|
| Qt 6.7.3 (Widgets, Network, Core, Test) | GUI, event loop, networking, settings |
| QXlsx (QXlsxQt6) | Read/write `.xlsx` files |
| QCoro 6 | C++ coroutines over Qt async machinery |
| OpenAI Responses API | AI content generation (GPT-5-mini default, gpt-5.2 fallback) |
| C++20 | Language standard |

### Important Compile-Time Definitions

Defined in `AmazonTemplate3Lib/CMakeLists.txt`:
- `COL_SEP=","` — CSV column separator
- `CELL_SEP="\073"` — cell separator (`;`)
- `OPENAI2_UNIT_TESTS` — set in the `_Tests` library variant to expose `OpenAi2` test hooks

## Amazon SP-API Notes

### Authentication — LWA only, no SigV4

Since **October 2, 2023**, Amazon SP-API no longer requires AWS IAM or AWS Signature Version 4. Requests only need the `x-amz-access-token` header (obtained via LWA OAuth2 refresh-token exchange). The `_signRequest` / `regionForMarketplace` methods have been removed from `AmazonCatalogApi`. There are no AWS IAM credentials in the app settings.

### Refresh tokens must be regenerated after adding app roles

**This has burned us before.** LWA refresh tokens encode the SP-API application's roles at the time of authorization. If you add a new role to the SP-API app (e.g. "Product Listing") and the existing refresh tokens were obtained *before* that role was added, those tokens will not grant access to the new role's APIs — Amazon returns HTTP 403 `AccessDeniedException` on every call.

**Fix:** re-authorize the app for the affected region in [solutionproviderportal.amazon.com](https://solutionproviderportal.amazon.com) to get fresh refresh tokens, then update them in the app's Settings pane.

Symptom: all EU/NA marketplaces return `Access to requested resource is denied` while JP works (or vice-versa), because the JP token was obtained after the role change but the EU/NA tokens were not.

### Diagnostic files

`AmazonCatalogApi` automatically writes a full request+response dump to `/tmp/sp-api-{ASIN}-{timestamp}.txt` whenever a non-200 response is received. Attach this file to Amazon support cases — it contains all headers (access token truncated) and the response body.
