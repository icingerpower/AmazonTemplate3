#ifndef ABSTRACTINVENTORYSOURCE_H
#define ABSTRACTINVENTORYSOURCE_H

#include <QHash>
#include <QString>
#include <QCoro/QCoroTask>

class AbstractInventorySource
{
public:
    virtual ~AbstractInventorySource() = default;
    virtual QString id()          const = 0;
    virtual QString displayName() const = 0;
    // Returns SKU → available quantity. Empty map on failure.
    virtual QCoro::Task<QHash<QString, int>> fetchInventory() const = 0;
};

#endif // ABSTRACTINVENTORYSOURCE_H
