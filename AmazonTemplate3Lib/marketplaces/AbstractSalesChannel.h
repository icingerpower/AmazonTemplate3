#pragma once

#include <QDateTime>
#include <QString>
#include <QTimeZone>
#include <QCoro/QCoroTask>
#include <functional>
#include <memory>

struct SalesMonthCount
{
    int count = -1;
    QString error;
};

// Read-only sales capability, independent of inventory and fulfillment sources.
class AbstractSalesChannel
{
public:
    virtual ~AbstractSalesChannel() = default;
    virtual QString displayName() const = 0;
    virtual QString lastError() const = 0;
    // Count orders (not units/order lines) in [from, to). -1 means unavailable.
    virtual QCoro::Task<void> fetchOrderCount(QDateTime from, QDateTime to, int *out) = 0;
    virtual void setReadCancellation(std::shared_ptr<bool> cancelled) { m_cancelled = std::move(cancelled); }
    virtual void setReadProgress(std::function<void(const QString &)> progress) { m_progress = std::move(progress); }
    // Default for stores with single-interval APIs. Amazon overrides this with
    // one Month-granularity request for the entire requested date range.
    virtual QCoro::Task<void> fetchMonthlyOrderCounts(QList<QDate> months, QDateTime until,
                                                     QList<SalesMonthCount> *out)
    {
        *out = QList<SalesMonthCount>(months.size());
        for (int i = 0; i < months.size(); ++i) {
            if (m_cancelled && *m_cancelled) co_return;
            if (m_progress) m_progress(months[i].toString("MMM yyyy"));
            co_await fetchOrderCount(months[i].startOfDay(QTimeZone::UTC),
                qMin(months[i].addMonths(1).startOfDay(QTimeZone::UTC), until), &(*out)[i].count);
            (*out)[i].error = lastError();
            if (!(*out)[i].error.isEmpty()) (*out)[i].count = -1;
        }
    }
protected:
    std::shared_ptr<bool> m_cancelled;
    std::function<void(const QString &)> m_progress;
};
