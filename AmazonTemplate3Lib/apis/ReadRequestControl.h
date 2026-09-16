#pragma once
#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroTimer>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QTimer>
#include <memory>

namespace AmazonRead {
// Transfer timeouts alone do not impose a wall-clock deadline. Keep read-only
// requests bounded, including authentication, and let Cancel abort the wait.
inline QCoro::Task<void> wait(QNetworkReply *reply, std::shared_ptr<bool> cancelled = {},
                              int timeoutMs = 60000) {
    QTimer deadline, cancellation;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(&cancellation, &QTimer::timeout, reply, [reply, cancelled] {
        if (cancelled && *cancelled)
            reply->abort();
    });
    deadline.start(timeoutMs);
    if (cancelled)
        cancellation.start(100);
    if (cancelled && *cancelled)
        reply->abort();
    if (!reply->isFinished())
        co_await qCoro(reply).waitForFinished();
}
inline QCoro::Task<void> delay(int milliseconds, std::shared_ptr<bool> cancelled) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds && !(cancelled && *cancelled)) {
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(qMin(100, milliseconds - int(elapsed.elapsed())));
        co_await qCoro(timer).waitForTimeout();
    }
}
} // namespace AmazonRead
