#ifndef ABSTRACTINVENTORYSOURCE_H
#define ABSTRACTINVENTORYSOURCE_H

#pragma GCC optimize("O1")

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QCoro/QCoroTask>

#include "MarketplaceTypes.h"

// A fulfillment/inventory source (Amazon FBA today, Octopia fulfillment
// later): holds the physical stock and ships marketplace orders.
// Implementations are produced by an AbstractInventorySourceFactory
// registered via DECLARE_INVENTORY_SOURCE_FACTORY (Recorder pattern).
class AbstractInventorySource
{
public:
    virtual ~AbstractInventorySource() = default;

    virtual QString id()          const = 0; // unique, e.g. "amazon_fba_eu"
    virtual QString displayName() const = 0; // e.g. "Amazon FBA EU"
    virtual QString lastError()   const = 0;
    virtual void    clearLastError()    = 0;

    // ── Inventory & sales ───────────────────────────────────────────────
    // Cold-start bulk fetch — may be slow (Amazon: MYI report, 1-2 min).
    // filterSkus empty = everything.
    virtual QCoro::Task<void> fetchAllInventory(QStringList filterSkus,
                                                QList<StockRecord> *out,
                                                ProgressFn onProgress) = 0;
    // Live targeted fetch — fast, fresh (Amazon: FBA Inventory API).
    virtual QCoro::Task<void> fetchInventory(QStringList skus,
                                             QList<StockRecord> *out,
                                             ProgressFn onProgress) = 0;
    // Units ordered for one SKU over the last `days` days (-1 on failure).
    virtual QCoro::Task<void> fetchSalesUnits(QString sku, int days, int *out) = 0;

    // ── Outbound fulfillment ────────────────────────────────────────────
    virtual QCoro::Task<void> fetchTracking(QString fulfillmentOrderId,
                                            TrackingInfo *out) = 0;
    // Exact payload createFulfillmentOrder() would send — for confirmation UIs.
    virtual QJsonObject previewFulfillmentOrder(FulfillmentRequest request) const = 0;
    virtual QCoro::Task<bool> createFulfillmentOrder(FulfillmentRequest request) = 0;
};

#endif // ABSTRACTINVENTORYSOURCE_H
