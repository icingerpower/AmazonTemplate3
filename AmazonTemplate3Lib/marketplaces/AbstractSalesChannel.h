#pragma once

#include <QDateTime>
#include <QString>
#include <QCoro/QCoroTask>

// Read-only sales capability, independent of inventory and fulfillment sources.
class AbstractSalesChannel
{
public:
    virtual ~AbstractSalesChannel() = default;
    virtual QString displayName() const = 0;
    virtual QString lastError() const = 0;
    // Count orders (not units/order lines) in [from, to). -1 means unavailable.
    virtual QCoro::Task<void> fetchOrderCount(QDateTime from, QDateTime to, int *out) = 0;
};
