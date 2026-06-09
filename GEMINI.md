# AmazonTemplate3

AmazonTemplate3 is a Qt-based desktop application designed for managing Amazon and Temu product listing templates. It automates the process of reading, filling, translating, and validating product attribute data across multiple marketplaces and languages.

## Project Overview

The application reads source `.xlsx` templates, processes them using a set of specialized "fillers," and generates populated target templates. It leverages the OpenAI API (GPT models) for tasks that require natural language understanding or translation, such as generating bullet points, titles, and keywords.

### Core Technologies
- **Language:** C++20
- **Framework:** Qt 6.7.3 (Widgets, Network, Core, Test)
- **Asynchronous Programming:** QCoro 6 (for C++ coroutines)
- **Excel Processing:** QXlsx (QXlsxQt6)
- **AI Integration:** OpenAI Responses API (GPT-5-mini default)
- **Build System:** CMake

## Project Structure

- `AmazonTemplate3/`: Contains the main application code (Qt Widgets GUI).
- `AmazonTemplate3Lib/`: The core business logic, compiled as a static library.
- `AmazonTemplate3Tests/`: Unit tests for the library components, using Qt Test.
- `common/` (external/sibling): Shared modules for OpenAI client, utilities, types, and working directory management.
- `compat/`: Compatibility headers (e.g., for QXlsx).

### Key Components in `AmazonTemplate3Lib`

- **`TemplateFiller`**: The central engine that orchestrates the template filling process.
- **`AbstractFiller` / Hierarchy**: A plugin-like architecture for filling different types of attributes (e.g., `FillerTitle`, `FillerBulletPoints`, `FillerSelectable`, `FillerSize`).
- **`OpenAi2`**: A singleton HTTP client for the OpenAI API with support for caching, retries, and model escalation.
- **Table Models**: Persisted via `QSettings`, these manage mandatory attributes, equivalent values across marketplaces, and replacement rules.

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

## Testing

The project uses the Qt Test framework. Each test file in `AmazonTemplate3Tests/` typically corresponds to a specific class or feature.

### Running Tests
```bash
cd build-release
ctest
```

### Running Specific Tests
Individual test binaries can be found in the build directory, e.g.:
```bash
./build-release/AmazonTemplate3Tests/tst_templatefiller
```

### Mocking OpenAI in Tests
When writing tests that involve `OpenAi2`, ensure `OPENAI2_UNIT_TESTS` is defined (it is automatically set for the `_Tests` library variant). Use `ai->resetForTests()` and `setFakeTransport()` to mock network responses.

## Development Conventions

- **Coroutines:** Use `QCoro::Task<T>` for asynchronous operations, especially for network calls (OpenAI, Amazon SP-API) and file I/O.
- **Surgical Edits:** When modifying template filling logic, identify the specific `Filler` class responsible for the attribute.
- **Settings:** Persistent configuration and mapping tables are managed via `QSettings`. Use the provided table model classes (e.g., `AttributeEquivalentTable`) to interact with these settings.
- **SP-API:** Note that Amazon SP-API authentication uses LWA tokens only (no SigV4).

## Working Directory Structure

The application organizes product-specific data in a `sizing/` subdirectory within the chosen working directory:
```
{workingDir}/
  sizing/
    {ASIN}-{simplified-title}/
      settings.ini
      {ASIN}_main.jpg
```
`settings.ini` stores product-specific sizing configurations and measurements.
