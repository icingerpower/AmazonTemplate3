#ifndef MARKETPLACETYPES_H
#define MARKETPLACETYPES_H

// Shared record types exchanged between AbstractTargetMarketplace (Temu, …)
// and AbstractInventorySource (Amazon FBA, Octopia, …) implementations.
// Plain value structs — no behaviour, no platform-specific fields.

#include <QList>
#include <QString>
#include <functional>

// Progress/log callback used by long-running platform operations.
using ProgressFn = std::function<void(const QString &)>;

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
