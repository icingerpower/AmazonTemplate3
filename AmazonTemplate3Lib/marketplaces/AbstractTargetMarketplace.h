#ifndef ABSTRACTTARGETMARKETPLACE_H
#define ABSTRACTTARGETMARKETPLACE_H

#include <QHash>
#include <QString>
#include <QCoro/QCoroTask>

class AbstractTargetMarketplace
{
public:
    virtual ~AbstractTargetMarketplace() = default;
    virtual QString id()          const = 0;
    virtual QString displayName() const = 0;
    // Push SKU → qty. Returns true on success.
    virtual QCoro::Task<bool> updateInventory(const QHash<QString, int> &inventory) const = 0;
};

#endif // ABSTRACTTARGETMARKETPLACE_H
