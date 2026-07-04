#ifndef AMAZONINVENTORYAPI_H
#define AMAZONINVENTORYAPI_H

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <QJsonArray>
#include <QJsonObject>

#include <functional>

#include <QCoro/QCoroTask>

class QNetworkAccessManager;

class AmazonInventoryApi : public QObject
{
    Q_OBJECT
public:
    struct InventorySummary {
        QString sku;
        QString asin;
        int     available = 0;  // fulfillableQuantity
        int     inbound   = 0;  // working + shipped + receiving
    };

    explicit AmazonInventoryApi(const QString &lwaClientId,
                                const QString &lwaClientSecret,
                                const QString &lwaRefreshToken,
                                const QString &sellerId,
                                const QString &marketplaceId,
                                QObject *parent = nullptr);

    // Fetch FBA inventory summaries via the live FBA Inventory API (fresher
    // than the MYI report, which lags during FC transfers). Chunked by 50 SKUs.
    // available = fulfillable + FC-transfer (pendingTransshipment) units.
    // Results written into *out (appended). Output param avoids GCC 13 ICE.
    QCoro::Task<void> fetchFbaInventory(QStringList skus,
                                        QList<InventorySummary> *out,
                                        std::function<void(const QString &)> onProgress = nullptr);

    // Fetch FBA inventory for all active FBA SKUs via the Reports API.
    // Slower but more reliable than the direct inventory endpoint.
    // filterSkus: if non-empty, only these SKUs are included in *out.
    // onProgress: optional callback called with status strings during polling.
    QCoro::Task<void> fetchFbaInventoryReport(
        QStringList filterSkus,
        QList<InventorySummary> *out,
        std::function<void(const QString &)> onProgress = nullptr);

    // Fetch total units ordered for one SKU over the last `days` calendar days,
    // summed across all provided marketplace IDs (one API call per marketplace).
    // Result written into *out (-1 if every marketplace call failed).
    QCoro::Task<void> fetchSalesUnits(QString sku, int days,
                                      QStringList marketplaceIds, int *out);

    // MCF Outbound fulfillment orders
    QCoro::Task<QJsonArray> fetchFulfillmentOrders(const QDateTime &startDateTime);
    QCoro::Task<QJsonObject> getFulfillmentOrder(const QString &sellerFulfillmentOrderId);
    // Create an MCF outbound order. payload = CreateFulfillmentOrderRequest
    // (sellerFulfillmentOrderId, destinationAddress, items…). True on success.
    QCoro::Task<bool> createFulfillmentOrder(const QJsonObject &payload);

    QString lastError() const { return m_lastError; }
    void clearLastError() { m_lastError.clear(); }

private:
    QString m_lwaClientId;
    QString m_lwaClientSecret;
    QString m_lwaRefreshToken;
    QString m_sellerId;
    QString m_marketplaceId;

    QString   m_accessToken;
    QDateTime m_accessTokenExpiry;
    QString   m_lastError;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *_nam();

    QCoro::Task<void> _getAccessToken(QString *out);
};

#endif // AMAZONINVENTORYAPI_H
