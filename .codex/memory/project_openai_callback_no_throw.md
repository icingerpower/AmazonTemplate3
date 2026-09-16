---
name: openai-callback-no-throw
description: "Never throw from OpenAi2 step callbacks (onLastError, validate, apply) — they run inside QNetworkReply slots and any exception std::terminates the app"
metadata:
  node_type: memory
  type: project
  originSessionId: 2a1bf464-bdf8-49ae-b5a8-39f3f705e286
  modified: 2026-07-25T11:51:11.314Z
---

Never throw exceptions (e.g. `ExceptionTemplate::raise()`) from `OpenAi2` step callbacks: `onLastError`, `validate`, `apply`, `validateBestReply`. They execute inside `QNetworkReply::finished` slot lambdas; `OpenAi2::_callResponses`'s `doneErr`/`doneOk` wrappers re-throw (`OpenAi2.cpp` ~792), the exception escapes the Qt event loop, and the app aborts with SIGABRT via `std::terminate`.

**Why:** Qt cannot propagate C++ exceptions through the event loop / slot invocation.

**How to apply:** In callbacks, log with `qWarning()` and/or record to `AiFailureTable` then `return true` (see `FillerTitle.cpp` `onLastError` for the canonical pattern). Step failures already reach the caller safely: `askGptMultipleTimeAiCoro` reports `ExceptionOpenAiError` through its awaited `QFuture`, which re-throws inside the QCoro coroutine. Top-level coroutines like `PaneGenTemplate::generate()` must catch `ExceptionOpenAiError` / `ExceptionOpenAiNotInitialized` explicitly — they are `QException`s, NOT subclasses of `ExceptionTemplate`, so a `catch (ExceptionTemplate&)` misses them (fixed 2026-07-25 in FillerSize.cpp + PaneGenTemplate.cpp).
