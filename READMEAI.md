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

---

## Amazon SP-API — A+ Content Upload

This section documents every hard-won lesson from implementing the A+ content upload pipeline. Read this before touching `AmazonAplusApi.cpp` or any A+ upload code.

### Authentication

- **LWA only, no SigV4.** Since October 2023 SP-API no longer requires AWS IAM or Signature V4. Every request only needs `x-amz-access-token` obtained via a standard OAuth2 LWA refresh-token exchange.
- **Refresh tokens encode roles.** If you add a new SP-API application role (e.g. "A+ Content Management"), all existing refresh tokens are invalid for that role — they were issued before the role existed. Re-authorize in [solutionproviderportal.amazon.com](https://solutionproviderportal.amazon.com) to get fresh tokens. Symptom: HTTP 403 `AccessDeniedException` on every call for the new role.

### Regional Endpoints

| Region | Endpoint host |
|--------|--------------|
| EU (FR, DE, IT, ES, NL, BE, …) | `sellingpartnerapi-eu.amazon.com` |
| NA (US, CA, MX) | `sellingpartnerapi-na.amazon.com` |
| JP | `sellingpartnerapi-fe.amazon.com` |

**Critical rule: use the endpoint that matches the target marketplace, except for image upload (see below).**

### Image Upload via SP-API Uploads API

This step was by far the most painful. Concrete rules:

1. **The Uploads API is NA-only.** The path `/uploads/2020-11-01/uploadDestinations/...` does not exist on the EU endpoint. Always POST to `sellingpartnerapi-na.amazon.com` for this step, even when your A+ content targets EU marketplaces. Use your NA access token and the NA marketplace ID (`ATVPDKIKX0DER` = US) for this request.

2. **Do NOT URL-encode the resource path.** The resource path appended to `/uploads/2020-11-01/uploadDestinations/` must be passed **unencoded**, e.g.:
   ```
   /uploads/2020-11-01/uploadDestinations/aplus/2020-11-01/contentDocuments
   ```
   Qt's `QUrl::setPath()` will percent-encode slashes by default — use `QUrl::setPath(path, QUrl::TolerantMode)` or build the URL string manually.

3. **The S3 POST: send all query params as form fields, not in the URL.** Amazon returns a presigned S3 POST URL like `https://s3.amazonaws.com/bucket?X-Amz-Signature=...&key=...`. All the query parameters (`key`, `X-Amz-Credential`, `X-Amz-Signature`, `policy`, etc.) **must be posted as multipart form fields**, not kept in the URL query string. Sending them in the URL causes S3 to return HTTP 400 "Conflicting query string parameters". Strip the query from the URL, POST to the bare base URL, and add every original query parameter as a `text/plain` form part first, then append the image bytes as the last part (order matters for S3 signature verification).

4. **S3 POST returns 204 on success, not 200.** Do not treat 204 as an error.

5. **The `uploadDestinationId` returned by step 1 is cross-region.** It can be used in `createContentDocument` calls targeting any marketplace (EU, NA, JP) even though it was created via the NA endpoint.

### Building the A+ Content Document

#### `contentType` field

Use `"EBC"` (Enhanced Brand Content = 3P seller A+). `"EMC"` is vendor-only — the API will reject it with a cryptic error if you use it for a seller account.

#### Module structure for full-width images

The working module type is `STANDARD_HEADER_IMAGE_TEXT`. Its JSON structure is:

```json
{
  "contentModuleType": "STANDARD_HEADER_IMAGE_TEXT",
  "standardHeaderImageText": {
    "headline": {                  // optional; omit key entirely if not needed
      "value": "TITLE TEXT",
      "decoratorSet": []
    },
    "block": {
      "image": {
        "uploadDestinationId": "<id from Uploads API>",
        "altText": "description",  // REQUIRED — null altText causes validation error
        "imageCropSpecification": {
          "size": {
            "width":  { "value": 970, "units": "pixels" },
            "height": { "value": 600, "units": "pixels" }
          },
          "offset": {
            "x": { "value": 0, "units": "pixels" },
            "y": { "value": 0, "units": "pixels" }
          }
        }
      },
      "body": { "textList": [] }
    }
  }
}
```

Modules that were tried and rejected by the API:
- `STANDARD_SINGLE_SIDE_IMAGE` — max 300×300 px, not suitable for full-width charts
- `standardImageCaption` — not a valid property name; the API returns the full list of valid names if you use a wrong one
- `imageBlock` — not a valid property; the correct key is `block`

#### Text modules (`STANDARD_TEXT`)

The `body` is a `textList` array of `TextItem` objects:

```json
{
  "value": "text here",
  "decoratorSet": [
    { "type": "STYLE_BOLD", "offset": 0, "length": 10 }
  ]
}
```

Known working decorator types: `STYLE_BOLD`, `STYLE_ITALIC`.

**`STYLE_LINEBREAK` + `\n` in value = double spacing.** If you embed a `\n` character in `value`, Amazon treats it as a full paragraph break. Adding a `STYLE_LINEBREAK` decorator at the same position adds a *second* break — the result is extra-wide spacing. To have a question and answer in the same paragraph (tight spacing), join them with a plain space and no newline: `"question text answer text"`.

#### Module headline spacing

The `headline` field of `STANDARD_HEADER_IMAGE_TEXT` is rendered by Amazon's CSS with a fixed large gap between the headline text and the image below. This gap cannot be reduced via the API. If spacing matters, consider baking the title text directly into the image instead of using the headline field.

#### 5-module limit

A+ content documents are capped at **5 modules**. Plan your layout before building the JSON.

### ASIN Association

- **Use child ASINs, not the parent variation ASIN.** The parent ASIN (the variation family root) does not exist as a catalog item. Sending it in `associateAsinWithDocument` returns "ASIN does not exist in the catalog". Always associate with the child ASINs (the specific color/size variants).
- **Per-color association.** When a product has color variants, associate each A+ document with the child ASINs for that specific color only.

### Validation Endpoint (`validateContentDocumentAsinRelations`)

- This endpoint requires **Brand Registry**. If the account does not have Brand Registry, it returns HTTP 403. This is not a fatal error — treat it as a warning and proceed to `submitForApproval` anyway.
- The endpoint validates content quality (image dimensions, guideline compliance) and ASIN existence. Errors here mean submission will also fail.

### Community Guidelines

Amazon's content checker rejects certain Unicode characters:
- **Em dash (U+2014 `—`) and en dash (U+2013 `–`) in headline fields** trigger "violates community guidelines". Replace with a plain hyphen `-` before setting any headline value.
- This applies to the `headline.value` field inside modules. Body text (`textList`) is more lenient.

### Image Dimensions

- `STANDARD_HEADER_IMAGE_TEXT` expects a minimum image height of **600px**. Amazon may warn (but not block) if the image is shorter.
- Padding short images (e.g. a size chart table) to 600px with white fill creates visible white space in the rendered A+ page. Instead, pad to only the minimum needed and keep the table top-aligned — the natural bottom padding is far less intrusive.
- The size chart image rendered by `AbstractSizeCategory::renderImage` is typically ~300–400px tall at 970px width. Padding it to exactly 600px adds 200–300px of dead white space at the bottom of the module.

### Full Upload Sequence

```
1. POST  /uploads/2020-11-01/uploadDestinations/...  (NA endpoint, NA token)
         → uploadDestinationId

2. POST  image bytes to S3 presigned URL             (query params as form fields)
         → HTTP 204 on success

3. POST  /aplus/2020-11-01/contentDocuments          (target region endpoint)
         body: { contentType: "EBC", contentDocument: { modules: [...] } }
         → contentReferenceKey

4. POST  /aplus/2020-11-01/contentDocumentAsinRelations  (target region)
         body: { asinSet: [<child ASINs only>] }

5. POST  /aplus/2020-11-01/contentDocumentAsinRelations/validate  (target region)
         → errors / warnings (HTTP 403 = non-fatal, no Brand Registry)

6. POST  /aplus/2020-11-01/contentDocuments/{key}/approvalSubmissions  (target region)
```

Steps 3–6 use the endpoint matching the target marketplace region. Step 1–2 always use NA.
