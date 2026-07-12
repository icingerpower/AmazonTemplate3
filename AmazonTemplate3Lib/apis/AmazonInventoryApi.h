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

    // Per-SKU inventory age / health, parsed from GET_FBA_INVENTORY_PLANNING_DATA.
    // Age buckets hold the number of units that have been in the FBA network for
    // the given number of days. daysOfSupply / unitsSold90 are -1 when the report
    // did not carry the column. Eligibility thresholds are applied by the caller.
    struct InventoryAge {
        QString sku;
        QString asin;
        QString productName;    // report's product-name (fallback title)
        int     available = 0;  // total available units
        // Age buckets (units). The report's granularity varies by store; coarse
        // buckets are filled by summing whatever fine buckets are present.
        int age0_90    = 0;
        int age91_180  = 0;
        int age181_270 = 0;
        int age271_365 = 0;
        int age365plus = 0;
        int daysOfSupply = -1;  // estimated days of supply (-1 = unknown)
        int unitsSold90  = -1;  // units shipped in the last 90 days (-1 = unknown)

        // Total on-hand units across all age buckets (basis for "% aged").
        int totalBucketUnits() const {
            return age0_90 + age91_180 + age181_270 + age271_365 + age365plus;
        }

        // Units that have been stored for at least the given number of days.
        // A bucket is counted only when its lower bound >= days, i.e. every unit
        // in it is at least `days` old. Bucket lower bounds: 0 / 91 / 181 / 271 / 366.
        int unitsStoredAtLeastDays(int days) const {
            int u = 0;
            if (days <= 0)   u += age0_90;
            if (days <= 91)  u += age91_180;
            if (days <= 181) u += age181_270;
            if (days <= 271) u += age271_365;
            if (days <= 366) u += age365plus;
            return u;
        }
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

    // Fetch per-SKU inventory age / days-of-supply / 90-day sales via the
    // GET_FBA_INVENTORY_PLANNING_DATA report (FBA Manage Inventory Health).
    // This is the only source of inventory-age data — the live FBA Inventory
    // API does not expose age. filterSkus: if non-empty, only these SKUs are
    // included in *out. Results appended to *out. Output param avoids GCC 13 ICE.
    QCoro::Task<void> fetchInventoryAgeReport(
        QStringList filterSkus,
        QList<InventoryAge> *out,
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

    // Create a report of the given type, poll to completion, download and
    // decompress the document. Decompressed bytes written to *outContent
    // (left empty on failure, with m_lastError set). Output param avoids GCC 13 ICE.
    QCoro::Task<void> _runReport(QString reportType,
                                 QByteArray *outContent,
                                 std::function<void(const QString &)> onProgress);
};

#endif // AMAZONINVENTORYAPI_H
