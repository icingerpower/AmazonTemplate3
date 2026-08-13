#ifndef MARKETPLACETYPES_H
#define MARKETPLACETYPES_H

// Shared record types exchanged between AbstractTargetMarketplace (Temu, …)
// and AbstractInventorySource (Amazon FBA, Octopia, …) implementations.
// Plain value structs — no behaviour, no platform-specific fields.

#include <QList>
#include <QString>
#include <QtGlobal>
#include <functional>

// Progress/log callback used by long-running platform operations.
using ProgressFn = std::function<void(const QString &)>;

// Sentinel returned by estimatedDaysOfSupply() when there were no sales in the
// window (stock won't run out on current trend) — displayed as "∞".
constexpr int kInfiniteDaysOfSupply = 999;

// Estimated days until stock runs out, from available units and units sold
// over the last `windowDays` days. -1 when unavailable/unsold are unknown (<0).
// Single source of truth for this math — used by both the marketplace-sync
// "days of supply" column and any other pane that needs the same estimate, so
// they never silently disagree with each other.
inline int estimatedDaysOfSupply(int available, int unitsSoldInWindow, int windowDays)
{
    if (available < 0) return -1;
    if (available == 0) return 0;
    if (unitsSoldInWindow < 0) return -1;
    if (unitsSoldInWindow == 0) return kInfiniteDaysOfSupply;
    const double perDay = double(unitsSoldInWindow) / double(windowDays);
    return qRound(available / perDay);
}

// Default inventory-coverage target used across the app's stock-health UI
// (Store tab's Stock/Recommended columns): ~4 months. A seller-chosen policy,
// not an Amazon-published figure — see recommendedInventoryForDays() below.
constexpr int kFourMonthCoverageDays = 120;

// Units needed on hand to cover `targetDays` of sales, given units sold over
// the last `windowDays` days. The mathematical inverse of estimatedDaysOfSupply()
// — same sales-rate math, so the two stay consistent with each other. There is
// no official Amazon constant for this: "targetDays" is a coverage policy the
// caller chooses (e.g. 120 days ≈ 4 months), not a value Amazon publishes.
// -1 when unitsSoldInWindow is unknown (<0); 0 when there were no sales at all.
inline int recommendedInventoryForDays(int unitsSoldInWindow, int windowDays, int targetDays)
{
    if (unitsSoldInWindow < 0) return -1;
    if (unitsSoldInWindow == 0) return 0;
    const double perDay = double(unitsSoldInWindow) / double(windowDays);
    return qRound(perDay * targetDays);
}

// Consignee address of a marketplace order.
struct ShippingAddress {
    QString name;
    QString addressLine1;
    QString addressLine2;
    QString city;
    QString region;      // state / province / département
    QString postalCode;
    QString countryCode; // ISO 3166-1 alpha-2, e.g. "FR"
    QString phone;

    bool isValid() const { return !name.isEmpty() && !addressLine1.isEmpty()
                                  && !postalCode.isEmpty(); }
};

// One order line on a target marketplace, pending fulfillment.
struct MarketOrder {
    QString marketplaceId; // AbstractTargetMarketplace::id() it came from
    QString orderId;       // marketplace parent/order id (Temu: parentOrderSn)
    QString itemId;        // marketplace order-line id  (Temu: orderSn)
    QString sku;           // seller SKU (shared with the fulfillment source)
    qint64  goodsId = 0;   // marketplace-internal product id
    qint64  skuId   = 0;   // marketplace-internal sku id
    int     quantity = 0;
    QString status;
};

// Inventory of one SKU at a fulfillment source.
struct StockRecord {
    QString sku;
    QString asin;          // source-internal product id (Amazon: ASIN)
    int     available = -1; // sellable units (-1 = unknown)
    int     inbound   = -1;
};

// Tracking of a fulfilled (or in-progress) outbound order.
struct TrackingInfo {
    QString trackingNumber;
    QString carrierName;

    bool hasTracking() const { return !trackingNumber.isEmpty(); }
};

// Request to create an outbound order at a fulfillment source.
struct FulfillmentItem {
    QString sku;
    QString itemId;        // becomes the source's order-line id
    int     quantity = 0;
};
struct FulfillmentRequest {
    QString fulfillmentOrderId; // e.g. "temu-PO-069-…" (orderIdPrefix + "-" + orderId)
    QString comment;            // displayable order comment
    ShippingAddress address;
    QList<FulfillmentItem> items;
};

#endif // MARKETPLACETYPES_H
