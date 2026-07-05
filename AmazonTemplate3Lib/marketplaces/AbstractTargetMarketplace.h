#ifndef ABSTRACTTARGETMARKETPLACE_H
#define ABSTRACTTARGETMARKETPLACE_H

#pragma GCC optimize("O1")

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QCoro/QCoroTask>

#include "MarketplaceTypes.h"

// A target marketplace (Temu store, …) where products are listed and orders
// arrive. Implementations are produced by an AbstractTargetMarketplaceFactory
// registered via DECLARE_TARGET_MARKETPLACE_FACTORY (Recorder pattern).
class AbstractTargetMarketplace
{
public:
    virtual ~AbstractTargetMarketplace() = default;

    virtual QString id()            const = 0; // unique, e.g. "temu_FR_Icinger"
    virtual QString displayName()   const = 0; // e.g. "Temu FR – Icinger"
    virtual QString countryCode()   const = 0; // e.g. "FR"
    // Prefix of fulfillment order ids created from this marketplace's orders:
    // fulfillmentOrderId = orderIdPrefix() + "-" + MarketOrder::orderId.
    virtual QString orderIdPrefix() const = 0; // e.g. "temu"
    virtual QString lastError()     const = 0;

    // ── Inventory & sales ───────────────────────────────────────────────
    // SKU → available quantity. Empty `skus` = every SKU listed in the store.
    virtual QCoro::Task<void> fetchInventory(QStringList skus,
                                             QHash<QString,int> *out) = 0;
    // SKU → units sold over the last `days` days.
    virtual QCoro::Task<void> fetchSales(QStringList skus, int days,
                                         QHash<QString,int> *out) = 0;
    // Push SKU → desired quantity to the store.
    virtual QCoro::Task<void> updateInventory(QHash<QString,int> qtyBySku,
                                              ProgressFn onProgress) = 0;

    // ── Orders ──────────────────────────────────────────────────────────
    virtual QCoro::Task<QList<MarketOrder>> fetchUnshippedOrders() = 0;
    virtual QCoro::Task<void> fetchOrderAddress(QString orderId,
                                                ShippingAddress *out) = 0;
    // Mark the order shipped on the marketplace with the given tracking.
    virtual QCoro::Task<bool> confirmShipment(MarketOrder order,
                                              TrackingInfo tracking,
                                              ProgressFn onProgress) = 0;
};

#endif // ABSTRACTTARGETMARKETPLACE_H
