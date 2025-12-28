# AmazonTemplate3 AI Developer Guide

## Build & Test Instructions

### 1. Building the Project
The project uses CMake. Recommended build type is RelWithDebInfo or Release for performance, though Debug is supported.

**Release Build:**
```bash
cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target AmazonTemplate3Tests
# Note: When invoking from a parent directory, use "../AmazonTemplate3/build-release" to keep the source tree clean.
```

### 2. Running Unit Tests
The main test suite is `AmazonTemplate3Tests`.

**Standard Run (Mocked Tests Only):**
```bash
./build-release/AmazonTemplate3Tests/AmazonTemplate3Tests
```

**Running Real Contract Tests:**
To run contract tests against the real OpenAI API, you must enable `DO_REAL_TESTS` and provide an API key at compile time.

```bash
cmake -B build-release -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DDO_REAL_TESTS=ON \
  -DOPEN_AI_API_KEY="sk-proj-..." 

cmake --build build-release --target AmazonTemplate3Tests
./build-release/AmazonTemplate3Tests/AmazonTemplate3Tests
```
*Note: If `DO_REAL_TESTS` is ON, tests will fail if the API key is invalid or missing.*

## Development Notes

### OpenAi2 Refactoring & Testing
- **Test Seam**: `OpenAi2` has `setTransportForTests` and `resetForTests`. ALWAYS use these in unit tests to mock network calls.
- **Fake Transport**: Use `TestOpenAi2::setFakeTransport` to simulate responses.
    - **Sync vs Async**: For simple checks, call the `ok` callback synchronously. For complex event loop tests, use `QTimer::singleShot(0, ...)` but ensure you manage the `QEventLoop` correctly (start/quit).
    - **Recursion**: `_runStepCollectN` and `askGptMultipleTime` use recursive lambdas. Ensure callbacks are fired to prevent hangs.
- **Hard Failures**: `OpenAi2` stops retrying immediately if an error message starts with `fatal:` (e.g., 401 Unauthorized).
- **Concurrency**: `QCoro` is being introduced (see `MandatoryAttributesManager.h`).
### Security Best Practices
- **Never commit API keys**: Do not store API keys in source files that are tracked by version control.
- **Do not compile in source directory**: Build artifacts should be placed in a separate build directory (e.g., `build-release`) to avoid polluting the source tree.


### Common Pitfalls
1.  **Test Timeouts**: Always use `QTimer` to enforce timeouts in `QEventLoop` tests.
2.  **Event Loop Hangs**: If a `QEventLoop` never quits, check if your fake transport or callback chain is broken.
3.  **Static/Singleton State**: `OpenAi2` is a singleton. ALWAYS call `ai->resetForTests()` at the start of `initTestCase` or individual test functions to clear state (especially `m_transport`).
4.  **Async Captures**: Avoid capturing `std::function` by reference in recursive lambdas; use `QSharedPointer` or copy.
