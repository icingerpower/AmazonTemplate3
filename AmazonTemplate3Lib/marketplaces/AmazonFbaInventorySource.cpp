#include "AmazonFbaInventorySource.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>

#include "../apis/AmazonInventoryApi.h"
#include "AbstractInventorySourceFactory.h"

namespace {

// Amazon France marketplace ID (report creation + MCF order marketplace).
const QString kMarketplaceId = QStringLiteral("A13V1IB3VIYZZH");

// All EU Amazon marketplaces — the Sales API only accepts one per call,
// so we query each and sum. Querying an inactive marketplace returns 0.
const QStringList kEuMarketplaceIds = {
    QStringLiteral("A1PA6795UKMFR9"), // DE
    QStringLiteral("A13V1IB3VIYZZH"), // FR
    QStringLiteral("APJ6JRA9NG5V4"),  // IT
    QStringLiteral("A1RKKUPIHCS9HS"), // ES
    QStringLiteral("A1805IZSGTT6HS"), // NL
    QStringLiteral("A2NODRKZP88ZB9"), // SE
    QStringLiteral("A1C3SOZRARQ6R3"), // PL
    QStringLiteral("AMEN7PMS3EDWL"),  // BE
};

QList<StockRecord> toStockRecords(const QList<AmazonInventoryApi::InventorySummary> &summaries)
{
    QList<StockRecord> out;
    out.reserve(summaries.size());
    for (const auto &s : summaries) {
        StockRecord r;
        r.sku       = s.sku;
        r.asin      = s.asin;
        r.available = s.available;
        r.inbound   = s.inbound;
        out.append(r);
    }
    return out;
}

} // namespace

AmazonFbaInventorySource::AmazonFbaInventorySource(const QString &lwaClientId,
                                                   const QString &lwaClientSecret,
                                                   const QString &lwaRefreshToken,
                                                   const QString &sellerId)
    : m_api(new AmazonInventoryApi(lwaClientId, lwaClientSecret, lwaRefreshToken,
                                   sellerId, kMarketplaceId))
{
    qDebug() << "AmazonFbaInventorySource created (marketplace" << kMarketplaceId << ")";
}

AmazonFbaInventorySource::~AmazonFbaInventorySource()
{
    delete m_api;
}

QString AmazonFbaInventorySource::lastError() const
{
    return m_api->lastError();
}

void AmazonFbaInventorySource::clearLastError()
{
    m_api->clearLastError();
}

QCoro::Task<void> AmazonFbaInventorySource::fetchAllInventory(QStringList filterSkus,
                                                              QList<StockRecord> *out,
                                                              ProgressFn onProgress)
{
    out->clear();
    QList<AmazonInventoryApi::InventorySummary> summaries;
    co_await m_api->fetchFbaInventoryReport(filterSkus, &summaries, onProgress);
    *out = toStockRecords(summaries);
    co_return;
}

QCoro::Task<void> AmazonFbaInventorySource::fetchInventory(QStringList skus,
                                                           QList<StockRecord> *out,
                                                           ProgressFn onProgress)
{
    out->clear();
    QList<AmazonInventoryApi::InventorySummary> summaries;
    co_await m_api->fetchFbaInventory(skus, &summaries, onProgress);
    *out = toStockRecords(summaries);
    co_return;
}

QCoro::Task<void> AmazonFbaInventorySource::fetchSalesUnits(QString sku, int days, int *out)
{
    co_await m_api->fetchSalesUnits(sku, days, kEuMarketplaceIds, out);
}

QCoro::Task<void> AmazonFbaInventorySource::fetchTracking(QString fulfillmentOrderId,
                                                          TrackingInfo *out)
{
    *out = TrackingInfo{};
    const QJsonObject detailedOrder = co_await m_api->getFulfillmentOrder(fulfillmentOrderId);
    if (!m_api->lastError().isEmpty())
        co_return;

    const QJsonArray shipments = detailedOrder.value(QStringLiteral("fulfillmentShipments")).toArray();
    for (const QJsonValue &shipVal : shipments) {
        const QJsonObject shipObj = shipVal.toObject();
        QJsonArray packages = shipObj.value(QStringLiteral("fulfillmentShipmentPackage")).toArray();
        if (packages.isEmpty())
            packages = shipObj.value(QStringLiteral("fulfillmentShipmentPackages")).toArray();
        for (const QJsonValue &pkgVal : packages) {
            const QJsonObject pkgObj = pkgVal.toObject();
            out->trackingNumber = pkgObj.value(QStringLiteral("trackingNumber")).toString();
            out->carrierName    = pkgObj.value(QStringLiteral("carrierCode")).toString();
            if (out->hasTracking())
                co_return;
        }
    }
    co_return;
}

QJsonObject AmazonFbaInventorySource::previewFulfillmentOrder(FulfillmentRequest request) const
{
    QJsonObject dest;
    dest.insert(QStringLiteral("name"), request.address.name);
    dest.insert(QStringLiteral("addressLine1"), request.address.addressLine1);
    if (!request.address.addressLine2.isEmpty())
        dest.insert(QStringLiteral("addressLine2"), request.address.addressLine2);
    dest.insert(QStringLiteral("city"), request.address.city);
    dest.insert(QStringLiteral("stateOrRegion"), request.address.region);
    dest.insert(QStringLiteral("postalCode"), request.address.postalCode);
    dest.insert(QStringLiteral("countryCode"), request.address.countryCode);
    if (!request.address.phone.isEmpty())
        dest.insert(QStringLiteral("phone"), request.address.phone);

    QJsonArray items;
    for (const FulfillmentItem &it : request.items) {
        QJsonObject item;
        item.insert(QStringLiteral("sellerSku"), it.sku);
        item.insert(QStringLiteral("sellerFulfillmentOrderItemId"), it.itemId);
        item.insert(QStringLiteral("quantity"), it.quantity);
        items.append(item);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("sellerFulfillmentOrderId"), request.fulfillmentOrderId);
    payload.insert(QStringLiteral("displayableOrderId"), request.fulfillmentOrderId);
    payload.insert(QStringLiteral("displayableOrderDate"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    payload.insert(QStringLiteral("displayableOrderComment"), request.comment);
    payload.insert(QStringLiteral("shippingSpeedCategory"), QStringLiteral("Standard"));
    payload.insert(QStringLiteral("marketplaceId"), kMarketplaceId);
    payload.insert(QStringLiteral("destinationAddress"), dest);
    payload.insert(QStringLiteral("items"), items);
    return payload;
}

QCoro::Task<bool> AmazonFbaInventorySource::createFulfillmentOrder(FulfillmentRequest request)
{
    const QJsonObject payload = previewFulfillmentOrder(request);
    const bool ok = co_await m_api->createFulfillmentOrder(payload);
    qDebug() << "AmazonFbaInventorySource::createFulfillmentOrder"
             << request.fulfillmentOrderId << "->" << (ok ? "OK" : m_api->lastError());
    co_return ok;
}

// ---------------------------------------------------------------------------
// Factory — one instance when the EU LWA credentials are configured.
// ---------------------------------------------------------------------------

QList<AbstractInventorySource *> AmazonFbaInventorySourceFactory::createInstances(QSettings *settings) const
{
    QList<AbstractInventorySource *> out;
    const QString clientId     = settings->value(QStringLiteral("AmazonApi/lwaClientId")).toString();
    const QString clientSecret = settings->value(QStringLiteral("AmazonApi/lwaClientSecret")).toString();
    const QString refreshToken = settings->value(QStringLiteral("AmazonApi/eu/lwaRefreshToken")).toString();
    const QString sellerId     = settings->value(QStringLiteral("AmazonApi/eu/sellerId")).toString();
    if (clientId.isEmpty() || clientSecret.isEmpty() || refreshToken.isEmpty()) {
        qDebug() << "AmazonFbaInventorySourceFactory: EU LWA credentials incomplete — 0 sources";
        return out;
    }
    out.append(new AmazonFbaInventorySource(clientId, clientSecret, refreshToken, sellerId));
    return out;
}
