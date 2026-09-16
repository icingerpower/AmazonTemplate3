---
name: project-cli-runprompt-crash
description: Never co_await AbstractCli::runPrompt() directly — it crashes; use the runPromptAsync→QFuture bridge
metadata:
  node_type: memory
  type: project
  originSessionId: 285dea6a-7db5-4318-a959-5f55a547ac0d
---

`co_await cli->runPrompt(prompt)` (the QCoro::Task<CliRunResult> overload of `AbstractCli`) **crashes with SIGSEGV**. GCC's coroutine frame analysis may call `~Task()` → `handle.destroy()` while the `QProcess::finished` signal is still queued, so the QCoroSignal resume lambda runs on a freed frame.

**Always** invoke the CLI via the async API bridged to a QFuture, then co_await the future:
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
Needs `#include <QCoro/QCoroFuture>`, `<QPromise>`, `<QFuture>`. See `PaneSizing.cpp` (~line 3127 comment) and `DialogTemuCreateProduct::_runCli`.

Separately: fire-and-forget coroutines (calling a `QCoro::Task<>` slot and discarding the returned Task) also free the frame — store the returned Task in a member (pattern: `m_uploadTask = _foo();`) to keep it alive until it completes.
