# AmazonTemplate3

AmazonTemplate3 is a Qt-based desktop application designed for managing Amazon and Temu product listing templates. It automates the process of reading, filling, translating, and validating product attribute data across multiple marketplaces and languages.

## Workflow Rules

- **Never commit without explicit user permission.** At most, ask if you may commit. Do not commit autonomously even after a successful build or fix.
- **Never commit untested code**, even if the build passes.

## Core Technologies
- **Language:** C++20
- **Framework:** Qt 6.7.3 (Widgets, Network, Core, Test)
- **Asynchronous Programming:** QCoro 6 (for C++ coroutines)
- **Excel Processing:** QXlsx (QXlsxQt6)
- **AI Integration:** OpenAI Responses API (GPT-5-mini default) + AI CLIs (Claude, Gemini, Antigravity CLI `agy`, Codex, etc. via `AbstractCli`)
- **Web Automation:** Node.js + Playwright persistent-context worker (`case-worker/`) for Amazon Seller Central automation
- **Build System:** CMake

## Project Structure

- `AmazonTemplate3/`: Contains the main application code (Qt Widgets GUI).
- `AmazonTemplate3Lib/`: The core business logic, compiled as a static library.
- `AmazonTemplate3Tests/`: Unit tests for the library components, using Qt Test.
- `case-worker/`: Node.js / Playwright automation for Seller Central (case lobby, GSPR compliance, reviews).
- `common/` (external/sibling): Shared modules for OpenAI client, AI CLIs, utilities, types, and working directory management.
- `compat/`: Compatibility headers (e.g., for QXlsx).

### Key Components in `AmazonTemplate3Lib`

- **`TemplateFiller`**: The central engine that orchestrates the template filling process.
- **`AbstractFiller` / Hierarchy**: A plugin-like architecture for filling different types of attributes (e.g., `FillerTitle`, `FillerBulletPoints`, `FillerSelectable`, `FillerSize`, `FillerPrice`, `FillerKeywords`).
- **`OpenAi2`**: A singleton HTTP client for the OpenAI API with support for caching, retries, and model escalation.
- **`CaseWorkerRunner`**: Spawns `case-worker/src/oneshot.ts` processes for headless/headed Playwright automation against Seller Central.
- **Table Models**: Persisted via `QSettings`, these manage mandatory attributes, equivalent values across marketplaces, and replacement rules (`AttributesMandatoryTable`, `AttributeEquivalentTable`, `AttributeFlagsTable`, `AttributeValueReplacedTable`, `AiFailureTable`).

## Building and Running

### Prerequisites
- Qt 6.7.3 (installed at `/home/cedric/Qt/6.7.3/gcc_64` or configurable via `CMAKE_PREFIX_PATH`).
- `libvulkan-dev` (required by Qt on Linux).
- `QXlsx` (must be installed/available in CMake search path).

### Build Commands

**Release Build:**
```bash
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/cedric/Qt/6.7.3/gcc_64
cmake --build build-release
```

**Debug Build (using Preset):**
```bash
cmake --preset gravity-debug
cmake --build build-gravity-debug
```

**Test Binary Only:**
```bash
cmake --build build-release --target AmazonTemplate3Tests
```

## Testing

The project uses the Qt Test framework. Each test file compiles to its own executable.

### Running Tests
```bash
cd build-release && ctest
```

### Mocking OpenAI in Tests
When writing tests that involve `OpenAi2`, ensure `OPENAI2_UNIT_TESTS` is defined (it is automatically set for the `_Tests` library variant):
- Always call `ai->resetForTests()` at the start of each test.
- Use `setFakeTransport(...)` or `setTransportForTests(...)` to mock network responses — never make real HTTP in unit tests.
- Simulate async responses with `QTimer::singleShot(0, ...)`.
- Always guard `QEventLoop` with a `QTimer` timeout to prevent test hangs.
- Fatal errors (starting with `"fatal:"`) cause `OpenAi2` to stop retrying immediately.

## Critical Development Lessons & Gotchas

### 1. CLI `runPrompt` Crash (SIGSEGV)
- **NEVER** `co_await cli->runPrompt(prompt)` directly! GCC's coroutine frame analysis can destroy the task handle before `QProcess::finished` completes, causing a SIGSEGV.
- **ALWAYS** invoke the CLI via `runPromptAsync` bridged to a `QFuture`, then `co_await` the future:
```cpp
QPromise<CliRunResult> promise; promise.start();
QFuture<CliRunResult> future = promise.future();
{
    auto sp = QSharedPointer<QPromise<CliRunResult>>::create(std::move(promise));
    cli->runPromptAsync(prompt, /*workDir,*/ context, [sp](CliRunResult r) mutable {
        sp->addResult(std::move(r)); sp->finish();
    });
}
const CliRunResult r = co_await qCoro(future).result();
```
(Requires `#include <QCoro/QCoroFuture>`, `<QPromise>`, `<QFuture>`).
- **Fire-and-forget Tasks**: Calling a `QCoro::Task<>` slot and discarding the return value also frees the frame early. Store the returned Task in a member (e.g. `m_task = _foo();`) to keep it alive until completion.

### 2. AI CLI Output Parsing
- `AbstractCli::extractTextFromOutput()` is **ONLY** for `translationPromptArgs()` stream-json output (e.g. Claude).
- For `runPrompt()` / `runPromptAsync()`, output is plain text — use `CliRunResult.output` directly (trim / extract JSON yourself).

### 3. OpenAi2 Callbacks Must Never Throw
- Never throw exceptions (e.g. `ExceptionTemplate::raise()`) from `OpenAi2` step callbacks (`onLastError`, `validate`, `apply`, `validateBestReply`).
- They execute inside `QNetworkReply::finished` slot lambdas; an uncaught exception escapes the Qt event loop and calls `std::terminate` (SIGABRT).
- In callbacks, log with `qWarning()`, record to `AiFailureTable`, and return true.
- Catch `ExceptionOpenAiError` / `ExceptionOpenAiNotInitialized` explicitly (they are `QException`s, NOT subclasses of `ExceptionTemplate`).

### 4. Amazon SP-API Notes
- **LWA only, no SigV4:** Since 2023-10-02, SP-API requests only require `x-amz-access-token` header (OAuth2 refresh-token exchange). No AWS IAM credentials or SigV4.
- **Refresh tokens must be regenerated after adding app roles:** LWA tokens bake in roles at grant time. Adding a role (like "Product Listing" or "Amazon Fulfillment") requires re-authorizing in Solution Provider Portal to get a new refresh token, otherwise Amazon returns 403 `AccessDeniedException`.
- **Diagnostics:** Non-200 responses are dumped to `/tmp/sp-api-{ASIN}-{timestamp}.txt`.

### 5. A+ Content Upload API Lessons
- Uploads API is NA-only (`sellingpartnerapi-na.amazon.com`), uploads to S3 form-body params.
- Use EBC document format, not EMC. Child ASINs only. Avoid em-dashes (`—`).

### 6. Working Directory Layout
```
{workingDir}/
  sizing/
    {ASIN}-{simplified-title}/
      settings.ini
      {ASIN}_main.jpg
  stores/
    {marketplaceId}.json
  reviews/
    cache/
      reviews.json
      images/
        {asin}.jpg
```

