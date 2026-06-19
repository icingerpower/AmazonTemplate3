#ifndef AMAZONWARNINGSAPI_H
#define AMAZONWARNINGSAPI_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QHash>
#include <QByteArray>
#include <QDateTime>

#include <QCoro/QCoroTask>

class QNetworkAccessManager;
struct WarningRow;

class AmazonWarningsApi : public QObject
{
    Q_OBJECT
public:
    explicit AmazonWarningsApi(const QString& lwaClientId,
                               const QString& lwaClientSecret,
                               const QString& lwaRefreshTokenEu,
                               const QString& lwaRefreshTokenNa,
                               const QString& lwaRefreshTokenJp,
                               const QString& sellerIdEu = {},
                               const QString& sellerIdNa = {},
                               const QString& sellerIdJp = {},
                               QObject* parent = nullptr);
    ~AmazonWarningsApi() override;

    // Main entry point: enumerates FBA listings for the given marketplace, then
    // calls the Listings Items API on each one and appends to *out one
    // WarningRow per violation found.
    //
    // maxWarnings: stop after accumulating this many violations (0 = unlimited).
    // Returns QCoro::Task<void> to avoid the GCC 13 coroutine ICE on
    // non-trivially-destructible Task<T> return types.
    QCoro::Task<void> fetchViolations(QString marketplaceId,
                                      QList<WarningRow>* out,
                                      int maxWarnings = 0);

    // Enrich pasted rows: runs FBA report to get SKU, then fetches listing data.
    // rows[i].asin and rows[i].attributeId must be pre-filled.
    QCoro::Task<void> enrichPastedRows(QString marketplaceId, QList<WarningRow>* rows);

    // Fetch the productType for a listing via the Listings Items API summaries.
    // *productType is empty on error. GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchListingProductType(QString marketplaceId,
                                               QString sku,
                                               QString* productType);

    // Fallback: fetch productType from the Catalog Items API.
    // Works even for inactive/out-of-stock listings where the Listings API
    // omits productType from summaries. GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchProductTypeFromCatalog(QString marketplaceId,
                                                   QString asin,
                                                   QString* productType,
                                                   QString* classificationId = nullptr,
                                                   QString* classificationDisplayName = nullptr);

    // Fetch the valid enum values for a given attributeId via the Product Type Definitions
    // API + JSON Schema. *out is empty if not found or the attribute accepts free text.
    // Schema is cached per (productType:marketplaceId) for the session.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchAttributeEnumValues(QString marketplaceId,
                                                QString productType,
                                                QString attributeId,
                                                QStringList* out);

    // PATCH a single text attribute on a listing via the Listings Items API.
    // value is patched as {"value": value, "marketplace_id": marketplaceId}.
    // Sets *success = true on HTTP 200/202. GCC 13 ICE workaround: params by value.
    QCoro::Task<void> patchListingAttribute(QString marketplaceId,
                                             QString sku,
                                             QString productType,
                                             QString attributeId,
                                             QString value,
                                             bool* success);

    QString lastError() const { return m_lastError; }
    void    clearLastError()  { m_lastError.clear(); }

signals:
    // Emitted during fetchViolations for progress dialog display.
    void logMessage(const QString &message);
    // current == 0 && total == 0 → indeterminate (step 1 / inventory scan).
    // total > 0 → determinate progress (step 2 / per-listing checks).
    void progressChanged(int current, int total);

public:
    // Returns the seller ID configured for the given marketplace's region,
    // or an empty string if no credentials are configured for that region.
    // Used by callers to enumerate "available" marketplaces.
    QString sellerIdForMarketplace(const QString& marketplaceId) const;

private:
    // Per-marketplace endpoint lookup (same mapping as AmazonCatalogApi).
    static QString endpointForMarketplace(const QString& marketplaceId);

    // Maps marketplace ID to LWA region name ("EU", "NA", "JP").
    static QString lwaRegionForMarketplace(const QString& marketplaceId);

    // LWA token (cached 55 minutes per region). lwaRegion is "EU", "NA", or "JP".
    // Uses an output parameter (instead of co_return QString) to avoid GCC 13
    // coroutine ICE on non-trivially-destructible Task<T> return types.
    QCoro::Task<void> _getAccessToken(QString lwaRegion, QString* out);

    // Extracted step 1 of fetchViolations: run FBA report → ASIN→SKU map.
    // *out is empty on error (error already logged via logMessage signal).
    QCoro::Task<void> _fetchFbaAsinToSku(QString marketplaceId, QHash<QString, QString>* out);

    // Fallback for enrichPastedRows: enumerate ALL listings (including inactive/sold-out)
    // via GET_MERCHANT_LISTINGS_ALL_DATA report. Adds only new ASINs to *out (never overwrites).
    QCoro::Task<void> _fetchAllListingsAsinToSku(QString marketplaceId,
                                                  QHash<QString, QString>* out);

    QNetworkAccessManager* _nam();

    // Credentials
    QString m_lwaClientId;
    QString m_lwaClientSecret;
    QString m_lwaRefreshTokenEu;
    QString m_lwaRefreshTokenNa;
    QString m_lwaRefreshTokenJp;
    QString m_sellerIdEu;
    QString m_sellerIdNa;
    QString m_sellerIdJp;

    // LWA access token cache — one entry per geographic region (EU / NA / JP)
    QString   m_accessTokenEu;   QDateTime m_accessTokenExpiryEu;
    QString   m_accessTokenNa;   QDateTime m_accessTokenExpiryNa;
    QString   m_accessTokenJp;   QDateTime m_accessTokenExpiryJp;

    QNetworkAccessManager* m_nam = nullptr;

    // Cache for product type JSON schemas — key: "productType:marketplaceId"
    QHash<QString, QByteArray> m_schemaCache;

    // Rate-limiting: track when the last per-listing GET was sent so we can
    // insert a short pause before the next one and stay under Amazon's quota.
    QDateTime m_lastRequestTime;

    QString m_lastError;
};

#endif // AMAZONWARNINGSAPI_H
