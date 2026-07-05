#include "TemuTargetMarketplace.h"

#include <QDebug>
#include <QJsonObject>
#include <QSettings>

#include "../apis/TemuInventoryApi.h"
#include "AbstractTargetMarketplaceFactory.h"

TemuTargetMarketplace::TemuTargetMarketplace(const QString &appKey, const QString &appSecret,
                                             const QString &country, const QString &label,
                                             const QString &token,
                                             const QString &proxyHost, int proxyPort,
                                             const QString &proxyUser, const QString &proxyPassword)
    : m_country(country)
    , m_label(label)
    , m_api(new TemuInventoryApi(appKey, appSecret, token,
                                 proxyHost, proxyPort, proxyUser, proxyPassword))
{
    qDebug() << "TemuTargetMarketplace created:" << id()
             << "proxy:" << (proxyHost.isEmpty() ? QStringLiteral("none") : proxyHost);
}

TemuTargetMarketplace::~TemuTargetMarketplace()
{
    delete m_api;
}

QString TemuTargetMarketplace::id() const
{
    return QStringLiteral("temu_%1_%2").arg(m_country, m_label);
}

QString TemuTargetMarketplace::displayName() const
{
    return QStringLiteral("Temu %1 – %2").arg(m_country, m_label);
}

QString TemuTargetMarketplace::lastError() const
{
    return m_api->lastError();
}

QCoro::Task<void> TemuTargetMarketplace::fetchInventory(QStringList skus, QHash<QString,int> *out)
{
    co_await m_api->fetchInventory(skus, out);
}

QCoro::Task<void> TemuTargetMarketplace::fetchSales(QStringList skus, int days, QHash<QString,int> *out)
{
    co_await m_api->fetchSales(skus, days, out);
}

QCoro::Task<void> TemuTargetMarketplace::updateInventory(QHash<QString,int> qtyBySku, ProgressFn onProgress)
{
    co_await m_api->updateInventory(qtyBySku, onProgress);
}

QCoro::Task<QList<MarketOrder>> TemuTargetMarketplace::fetchUnshippedOrders()
{
    const QList<TemuInventoryApi::TemuOrder> temuOrders = co_await m_api->fetchUnshippedOrders();

    QList<MarketOrder> out;
    out.reserve(temuOrders.size());
    for (const auto &t : temuOrders) {
        MarketOrder o;
        o.marketplaceId = id();
        o.orderId       = t.parentOrderSn;
        o.itemId        = t.orderSn;
        o.sku           = t.sku;
        o.goodsId       = t.goodsId;
        o.skuId         = t.skuId;
        o.quantity      = t.quantity;
        o.status        = t.status;
        out.append(o);
    }
    qDebug() << "TemuTargetMarketplace" << id() << "unshipped orders:" << out.size();
    co_return out;
}

QCoro::Task<void> TemuTargetMarketplace::fetchOrderAddress(QString orderId, ShippingAddress *out)
{
    *out = ShippingAddress{};
    QJsonObject addr;
    co_await m_api->fetchOrderAddress(orderId, &addr);
    if (!m_api->lastError().isEmpty() || addr.isEmpty())
        co_return;

    out->name         = addr.value(QStringLiteral("receiptName")).toString();
    out->addressLine1 = addr.value(QStringLiteral("addressLine1")).toString();
    out->addressLine2 = addr.value(QStringLiteral("addressLine2")).toString();
    out->city         = addr.value(QStringLiteral("regionName3")).toString();
    out->region       = addr.value(QStringLiteral("regionName2")).toString();
    out->postalCode   = addr.value(QStringLiteral("postCode")).toString();
    out->countryCode  = m_country; // regionName1 is a localized name; store country is ISO
    out->phone        = addr.value(QStringLiteral("mobile")).toString();
    co_return;
}

QCoro::Task<bool> TemuTargetMarketplace::confirmShipment(MarketOrder order, TrackingInfo tracking,
                                                         ProgressFn onProgress)
{
    const bool ok = co_await m_api->shipOrder(order.orderId, order.itemId,
                                              order.goodsId, order.skuId, order.quantity,
                                              tracking.trackingNumber, tracking.carrierName,
                                              m_country, onProgress);
    co_return ok;
}

// ---------------------------------------------------------------------------
// Factory — one instance per configured store in the "TemuApi/stores" array.
// ---------------------------------------------------------------------------

QList<AbstractTargetMarketplace *> TemuTargetMarketplaceFactory::createInstances(QSettings *settings) const
{
        QList<AbstractTargetMarketplace *> out;
        const QString appKey    = settings->value(QStringLiteral("TemuApi/appKey")).toString();
        const QString appSecret = settings->value(QStringLiteral("TemuApi/appSecret")).toString();
        if (appKey.isEmpty() || appSecret.isEmpty()) {
            qDebug() << "TemuTargetMarketplaceFactory: no app key/secret configured — 0 stores";
            return out;
        }

        const int size = settings->beginReadArray(QStringLiteral("TemuApi/stores"));
        for (int i = 0; i < size; ++i) {
            settings->setArrayIndex(i);
            const QString token = settings->value(QStringLiteral("token")).toString();
            if (token.isEmpty()) {
                qDebug() << "TemuTargetMarketplaceFactory: store" << i << "has no token — skipped";
                continue;
            }
            out.append(new TemuTargetMarketplace(
                appKey, appSecret,
                settings->value(QStringLiteral("country")).toString(),
                settings->value(QStringLiteral("label")).toString(),
                token,
                settings->value(QStringLiteral("proxyHost")).toString(),
                settings->value(QStringLiteral("proxyPort")).toInt(),
                settings->value(QStringLiteral("proxyUser")).toString(),
                settings->value(QStringLiteral("proxyPassword")).toString()));
        }
        settings->endArray();
        qDebug() << "TemuTargetMarketplaceFactory: created" << out.size() << "store(s)";
        return out;
}
