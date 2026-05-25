#ifndef AMAZONCATALOGAPI_H
#define AMAZONCATALOGAPI_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QByteArray>
#include <QStringList>

#include <functional>

#include <QCoro/QCoroTask>

class QNetworkAccessManager;
class QNetworkRequest;
class QUrlQuery;

class AmazonCatalogApi : public QObject
{
    Q_OBJECT
public:
    struct AsinItem {
        QString asin;
        QString sku;        // usually empty when loaded from ASIN only
        QString title;
        QString color;      // attribute "color" value (NOT "color_map")
        QString size;       // attribute "size" value
        bool    hasSizeTable = false; // true if "size_chart_node_id" attribute is non-empty
        QStringList bulletPoints;    // from attributes.bullet_point (or summaries fallback)
        QStringList materialAttrs;   // formatted "Label: value" strings for fabric/material keys
        QString     mainImageUrl;    // MAIN variant image URL from images[]
        QStringList allImageUrls;    // all image variant URLs in order (MAIN, PT01, PT02…)
    };

    struct VariationFamily {
        QString parentAsin;
        QString parentSku;  // usually empty when loaded from ASIN only
        QList<AsinItem> children;
    };

    explicit AmazonCatalogApi(const QString& lwaClientId,
                              const QString& lwaClientSecret,
                              const QString& lwaRefreshTokenEu,
                              const QString& lwaRefreshTokenNa,
                              const QString& lwaRefreshTokenJp,
                              const QString& sellerIdEu = {},
                              const QString& sellerIdNa = {},
                              const QString& sellerIdJp = {},
                              QObject* parent = nullptr);
    ~AmazonCatalogApi() override;

    // Main entry point: given any ASIN in a variation family, fills *out with the
    // full variation family (parent + all children with color/size/hasSizeTable).
    //
    // Returns QCoro::Task<void> (not Task<VariationFamily>) to avoid a GCC 13
    // coroutine ICE (build_special_member_call, cp/call.cc:11096) that is
    // triggered when co_awaiting a Task<T> whose T is non-trivially
    // destructible (here, VariationFamily contains QString/QList).
    QCoro::Task<void> fetchVariationFamily(const QString& asin,
                                           const QString& marketplaceId,
                                           VariationFamily* out);

    QString lastError() const { return m_lastError; }
    void    clearLastError()  { m_lastError.clear(); }

    // Returns the seller ID configured for the given marketplace's region.
    QString sellerIdForMarketplace(const QString& marketplaceId) const;

    // Lightweight existence check: returns true in *out if the ASIN is listed
    // in the given marketplace (one GET, no relationship traversal).
    // GCC 13 ICE workaround: params passed by value.
    QCoro::Task<void> checkAsinExists(QString asin, QString marketplaceId, bool* out);

    // PATCH size_chart_display for a single SKU via the Listings Items API.
    // headerCells: full header row — first cell is the label-column header (usually ""),
    //              remaining cells are size labels (e.g. "", "S", "M", "L").
    // dataRows: each QStringList has label in col 0, then one value per size column.
    // Sets *success = true on HTTP 200.
    // GCC 13 ICE workaround: all non-trivially-destructible params passed by value.
    QCoro::Task<void> patchListingSizeChart(QString marketplaceId,
                                            QString sku,
                                            QString productType,
                                            QStringList headerCells,
                                            QList<QStringList> dataRows,
                                            bool* success);

#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    // Mock is called instead of real HTTP. Receives the path
    // (e.g. "/catalog/2022-04-01/items/B0XXX") and query params map,
    // returns the JSON body as QByteArray.
    using MockFn = std::function<QByteArray(const QString& path,
                                            const QMap<QString,QString>& queryParams)>;
    void setMockForTests(MockFn mock);
    void resetForTests();
#endif

private:
    // Per-marketplace endpoint lookup
    static QString endpointForMarketplace(const QString& marketplaceId);

    // LWA token (cached 55 minutes per region). lwaRegion is "EU", "NA", or "JP".
    // Uses an output parameter (instead of co_return QString) to avoid GCC 13
    // coroutine ICE on non-trivially-destructible Task<T> return types.
    QCoro::Task<void> _getAccessToken(QString lwaRegion, QString* out);

    // Maps marketplace ID to LWA region name ("EU", "NA", "JP")
    static QString lwaRegionForMarketplace(const QString& marketplaceId);

    // Single HTTP GET to the catalog endpoint (includes LWA token + SigV4).
    // Writes the raw JSON body (empty on error) into *out.
    QCoro::Task<void> _doGet(const QString& marketplaceId,
                             const QString& asin,
                             const QStringList& includedData,
                             QByteArray* out);

    // Fetch a single item and fill it into *out
    // (passes by value: GCC 13 ICE workaround for coroutine frame with const-ref params)
    QCoro::Task<void> _fetchAsinItem(QString asin, QString marketplaceId, AsinItem* out);

    // Fallback when the direct /items/{asin} endpoint returns 403:
    // retries via GET /catalog/2022-04-01/items?identifiers=…&identifierType=ASIN
    // and extracts items[0] into the same JSON shape as the direct endpoint.
    QCoro::Task<void> _doSearchFallback(QString marketplaceId,
                                        QString asin,
                                        QStringList includedData,
                                        QByteArray* out);

    // Parse a JSON item document to extract relationships
    // Returns list of parent ASINs (from "parent" relationships) and
    // list of child ASINs (from "variation" relationships when parent).
    static void _parseRelationships(const QByteArray& jsonBody,
                                    QStringList* parentAsins,
                                    QStringList* childAsins);

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

    QString m_lastError;

#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    MockFn m_mock;
#endif
};

#endif // AMAZONCATALOGAPI_H
