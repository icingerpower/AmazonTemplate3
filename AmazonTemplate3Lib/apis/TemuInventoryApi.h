#ifndef TEMUINVENTORYAPI_H
#define TEMUINVENTORYAPI_H

#pragma GCC optimize("O1")

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QCoro/QCoroTask>
#include <functional>

class QNetworkAccessManager;

class TemuInventoryApi : public QObject
{
    Q_OBJECT
public:
    explicit TemuInventoryApi(QString appKey, QString appSecret, QString accessToken, QObject *parent = nullptr);
    explicit TemuInventoryApi(QString appKey, QString appSecret, QString accessToken,
                              QString proxyHost, int proxyPort, QString proxyUser, QString proxyPassword,
                              QObject *parent = nullptr);

    // SKU → available quantity. Empty `skus` = no filter: return every SKU
    // listed in the store (keys use the store's original skuSn casing).
    QCoro::Task<void> fetchInventory(QStringList skus, QHash<QString,int> *out);

    // SKU → units sold over `days` calendar days
    QCoro::Task<void> fetchSales(QStringList skus, int days, QHash<QString,int> *out);

    // Push inventory to Temu. qtyBySku: SKU → desired quantity.
    // Only single-SKU goods can be updated; multi-SKU goods are logged and skipped.
    // onProgress is called with each status line (optional, for progress dialogs).
    QCoro::Task<void> updateInventory(const QHash<QString,int> &qtyBySku,
                                      std::function<void(const QString&)> onProgress = {});

    struct TemuOrder {
        QString parentOrderSn;
        QString orderSn;
        qint64 goodsId = 0;
        qint64 skuId = 0;
        int quantity = 0;
        QString status;
        QString sku;
    };
    QCoro::Task<QList<TemuOrder>> fetchUnshippedOrders();
    // Shipping address of an order (bg.order.shippinginfo.v2.get):
    // receiptName, addressLine1/2, regionName1 (country) / 2 (region) /
    // 3 (city), postCode, mobile.
    QCoro::Task<void> fetchOrderAddress(const QString &parentOrderSn, QJsonObject *out);
    QCoro::Task<QJsonArray> fetchLogisticsCompanies();
    // countryCode (e.g. "FR") disambiguates country-specific Temu carriers
    // like swiship(FR) when the source carrier is "Amazon Logistics".
    QCoro::Task<bool> shipOrder(const QString &parentOrderSn, const QString &orderSn,
                                qint64 goodsId, qint64 skuId, int quantity,
                                const QString &trackingNumber, const QString &carrierName,
                                const QString &countryCode = {},
                                std::function<void(const QString&)> onProgress = {});

    QString lastError() const { return m_lastError; }

private:
    QString m_appKey;
    QString m_appSecret;
    QString m_accessToken;
    QString m_proxyHost;
    int     m_proxyPort = 0;
    QString m_proxyUser;
    QString m_proxyPassword;
    QString m_lastError;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *_nam();

    QCoro::Task<void> _postRequest(const QString &method, const QJsonObject &businessParams, QJsonObject *resultOut);

    // Per-SKU stock scan via bg.local.goods.sku.list.query.
    // Key of *out is the seller SKU (skuSn) lowercased for case-insensitive lookup.
    struct SkuStockInfo {
        qint64  goodsId = 0;
        qint64  skuId   = 0;
        int     stock   = 0;
        QString skuSn;   // original casing
    };
    QCoro::Task<void> _scanSkuStocks(QHash<QString, SkuStockInfo> *out,
                                     std::function<void(const QString&)> onProgress = {});
};

#endif // TEMUINVENTORYAPI_H
