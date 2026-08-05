#ifndef AMAZONCATALOGAPI_H
#define AMAZONCATALOGAPI_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QDate>
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
        QString brand;      // attribute "brand" value (summaries fallback)
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

    // Per-listing item for store browsing (Brand → Category → Gender → Age)
    struct StoreItem {
        QString asin;
        QString sku;
        QString title;
        QString brand;
        QString category;    // productType from summaries
        QString gender;      // target_gender[0].value
        QString age;         // age_range_description[0].value
        QString color;       // color_name[0].value (color[0].value fallback)
        QString sizeValue;   // size[0].value (size_name[0].value fallback) — raw string
        QString mainImageUrl; // from summaries[0].mainImage.link
        QDate   createdDate; // summaries[0].createdDate (ISO 8601 date part)
        int     inventory = 0; // quantity from merchant listings report
        QSet<QString> existsInMarketplaces; // marketplaceIds where this SKU is listed
        bool          isParent      = false; // true for variation parents (no color/size)
        bool          manuallyMoved = false; // user moved this item to a different tree node
    };

    explicit AmazonCatalogApi(const QString& lwaClientId,
                              const QString& lwaClientSecret,
                              const QString& lwaRefreshTokenEu,
                              const QString& lwaRefreshTokenNa,
                              const QString& lwaRefreshTokenJp,
                              const QString& sellerIdEu = {},
                              const QString& sellerIdNa = {},
                              const QString& sellerIdJp = {},
                              const QString& imgbbApiKey = {},
                              QObject* parent = nullptr);
    ~AmazonCatalogApi() override;

    // Main entry point: given any ASIN in a variation family, fills *out with the
    // full variation family (parent + all children with color/size/hasSizeTable).
    //
    // Returns QCoro::Task<void> (not Task<VariationFamily>) to avoid a GCC 13
    // coroutine ICE (build_special_member_call, cp/call.cc:11096) that is
    // triggered when co_awaiting a Task<T> whose T is non-trivially
    // destructible (here, VariationFamily contains QString/QList).
    // preserveMarketplaceColour: when true, each child's colour is left exactly
    // as the queried marketplace reports it (localized, e.g. "Bleu Roi" on FR)
    // and is NOT cross-filled from other markets nor normalized to UK English.
    // Use for per-country variation names. Default (false) normalizes colour to
    // UK English so A+ element IDs stay stable across sessions.
    QCoro::Task<void> fetchVariationFamily(const QString& asin,
                                           const QString& marketplaceId,
                                           VariationFamily* out,
                                           bool preserveMarketplaceColour = false);

    QString lastError() const { return m_lastError; }
    void    clearLastError()  { m_lastError.clear(); }

    // Returns the seller ID configured for the given marketplace's region.
    QString sellerIdForMarketplace(const QString& marketplaceId) const;
    // Maps a marketplace ID to its LWA region name ("EU", "NA", "JP").
    static QString lwaRegionForMarketplace(const QString& marketplaceId);

    // Lightweight existence check: returns true in *out if the ASIN is listed
    // in the given marketplace (one GET, no relationship traversal).
    // GCC 13 ICE workaround: params passed by value.
    QCoro::Task<void> checkAsinExists(QString asin, QString marketplaceId, bool* out);

    // Per-marketplace child health snapshot: parent assignment + image count + size label.
    // Used by BrokenChildTable to populate its per-marketplace columns.
    // GCC 13 ICE workaround: params passed by value.
    struct ChildHealthInfo {
        bool    exists     = false;  // false if ASIN not found in this marketplace
        QString parentAsin;
        QString size;
        int     imageCount = 0;
    };
    QCoro::Task<void> fetchChildHealth(QString asin, QString marketplaceId,
                                       ChildHealthInfo* out);

    // Fetches apparel-specific attributes from the Catalog Items API (by ASIN).
    // Keys differ from the Listings Items API: "color" not "color_name", "size" not "apparel_size".
    // Out fields are left empty if the attribute is absent or the call fails.
    struct CatalogApparelAttrs {
        QString color;       // "color" attribute
        QString sizeSystem;  // "apparel_size_system"
        QString sizeClass;   // "apparel_size_class"
        QString gender;      // "target_gender"
        QString ageRange;    // "age_range_description"
        QString bodyType;    // "apparel_body_type"
        QString heightType;  // "apparel_height_type"
    };
    QCoro::Task<void> fetchCatalogApparelAttrs(QString marketplaceId, QString asin,
                                               CatalogApparelAttrs* out);

    // Package weight (grams) and dimensions (cm) from the listing's
    // item_package_weight / item_package_dimensions attributes. Any field left
    // at 0 was absent. Units are converted to g / cm.
    struct PackageDims {
        double weightG = 0;
        double lengthCm = 0;
        double widthCm = 0;
        double heightCm = 0;
        QString gtin;          // EAN/UPC/GTIN from externally_assigned_product_identifier
        QString originCountry; // country_of_origin (value as returned, e.g. "China")
    };
    QCoro::Task<void> fetchPackageDims(QString marketplaceId, QString asin, PackageDims* out);

    // Localized title + bullet points for an ASIN on a specific marketplace,
    // so each Temu country store can be filled with its own language's text.
    QCoro::Task<void> fetchListingText(QString marketplaceId, QString asin,
                                       QString* title, QStringList* bullets);

    // Search the catalog for a product matching keywords that has at least one of wantedAttrs
    // filled. Writes the first matching attrs into *out and the ASIN into *foundAsin.
    QCoro::Task<void> searchCatalogForApparelAttrs(QString marketplaceId,
                                                   const QString &keywords,
                                                   const QStringList &wantedAttrs,
                                                   CatalogApparelAttrs* out,
                                                   QString* foundAsin);

    // Fetch image CDN URLs for asin in marketplace, in variant order (MAIN first,
    // then PT01, PT02…). One URL per variant, best representative size.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchItemImages(QString asin, QString marketplaceId,
                                      QStringList* imageUrls);

    // PATCH parentage_level=parent on a parent/virtual listing so it has a
    // recognised presence on the target marketplace before children link to it.
    // GCC 13 ICE workaround: params by value.
    // variationTheme: if non-empty, also PATCHes /attributes/variation_theme.
    QCoro::Task<void> patchListingAsParent(QString marketplaceId,
                                            QString parentSku,
                                            QString productType,
                                            QString variationTheme,
                                            QString* detailsOut = nullptr);

    // PATCH parentage_level=child + child_parent_sku_relationship on a child
    // listing. Used by the "Fix parents" workflow.
    // GCC 13 ICE workaround: params by value.
    // variationTheme: if non-empty, also PATCHes /attributes/variation_theme.
    // detailsOut (optional): receives a human-readable summary of the Amazon
    // response — HTTP status, Amazon's "status" field, submissionId, issues.
    // Populated on both success and failure so the caller can log and persist it.
    QCoro::Task<void> patchListingParent(QString marketplaceId,
                                         QString childSku,
                                         QString productType,
                                         QString parentSku,
                                         QString variationTheme,
                                         QString color,
                                         QString size,
                                         QString sizeSystem,
                                         const QHash<QString,QString> &extraAttrs,
                                         bool* success,
                                         QString* detailsOut = nullptr);

    // True for SKUs Amazon generates for refurbished/graded offers (they start
    // with "amzn", e.g. "amzn.gr.CJLY…-GD"). Those must never be picked when
    // resolving the seller's own SKU for an ASIN.
    static bool isRefurbishedSku(const QString &sku)
    { return sku.startsWith(QLatin1String("amzn"), Qt::CaseInsensitive); }

    // Fetches the parent SKU for a child listing by reading its variation
    // relationships. Virtual parent ASINs never appear in listing reports, so
    // this is the only reliable way to resolve a parent SKU from a child SKU.
    // *parentSkuOut is empty on error or when no parent relationship is found.
    // GCC 13 ICE workaround: params by value.
    // rawResponseOut (optional): receives the raw JSON body for diagnostic logging
    // when parentSkuOut comes back empty.
    QCoro::Task<void> fetchParentSku(QString marketplaceId,
                                     QString childSku,
                                     QString* parentSkuOut,
                                     QString* rawResponseOut = nullptr);

    // Fetches brand_name and variation_theme from a listing's attributes.
    // Used to populate flat-file feeds without inventing data.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchListingBrandAndTheme(QString marketplaceId,
                                                 QString sku,
                                                 QString* brand,
                                                 QString* variationTheme);

    struct VariationFeedEntry {
        QString sku;
        QString asin;       // may be empty for parent rows with per-marketplace ASINs
        bool    isParent = false;
        QString parentSku;  // empty for parent rows
        QString gtin;       // EAN/UPC/GTIN — preferred over asin for external_product_id
        QString gtinType;   // "ean", "upc", "gtin14" — lowercase SP-API enum value
    };

    // Fetches the best available GTIN (EAN preferred, then UPC, then GTIN14) for an
    // ASIN via the Catalog Items API identifiers. Sets *gtin and *gtinType to empty
    // strings if no non-ASIN identifier is found.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchAsinGtin(QString asin, QString marketplaceId,
                                    QString* gtin, QString* gtinType);

    // GET /listings/2021-08-01/items/{sellerId}/{sku}?includedData=attributes
    // Reads externally_assigned_product_identifier from the seller's own listing.
    // Tries the primary marketplace, then other same-region markets, then the other region.
    // diagLog (optional): filled with per-marketplace attempt results for UI logging.
    QCoro::Task<void> fetchListingGtin(QString marketplaceId, QString sku,
                                       QString* gtin, QString* gtinType,
                                       QString* diagLog = nullptr);

    // Fetches all listing attributes for a single SKU on a single marketplace.
    // Returns the raw attributes object (SP-API format) in *attrs, or empty on failure.
    QCoro::Task<void> fetchListingAttributes(QString marketplaceId, QString sku,
                                             QJsonObject* attrs);

    // Diagnostic result for a single SKU × marketplace. The `issues` list contains
    // Amazon's ASYNCHRONOUS validation errors — the real reason a previously
    // ACCEPTED submission never materialized.
    struct ListingCheck {
        bool        exists = false;   // false on HTTP 404 (SKU not listed on this marketplace)
        QString     status;           // summaries[].status joined: "BUYABLE", "DISCOVERABLE", …
        QString     itemName;         // summaries[].itemName
        QString     productType;      // summaries[].productType — the listing's ACTUAL product
                                      // type on this marketplace (may differ per marketplace!)
        QStringList issues;           // "[severity code] message (attributeNames)"
        QString     parentSku;        // relationships VARIATION parentSkus[0] (when SKU is a child)
        QStringList childSkus;        // relationships VARIATION childSkus (when SKU is a parent)
        QString     variationTheme;   // relationships variationTheme.theme (e.g. "SIZE/COLOR")
    };

    // GET /listings/2021-08-01/items/{sellerId}/{sku}
    //     ?includedData=issues,relationships,summaries&issueLocale=en_US
    // The single most useful diagnostic call: exposes per-marketplace listing
    // status, validation issues and the ACTUAL variation relationships Amazon has.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> checkListing(QString marketplaceId, QString sku,
                                   ListingCheck* out);

    // Fetches the Product Type Definitions schema for productType on marketplaceId,
    // dumps it to /tmp (path in *dumpPath) and extracts the allowed enum values of
    // apparel_size.size_system / .size_class as "value (display name)" strings.
    // This is the AUTHORITATIVE source for which size_system codes a marketplace
    // accepts — listing read-backs can contain stale/invalid internal codes.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchApparelSizeSchemaInfo(QString marketplaceId,
                                                 QString productType,
                                                 QStringList* sizeSystems,
                                                 QStringList* sizeClasses,
                                                 QString* dumpPath);

    // Top-level attribute names defined by the product type schema on this
    // marketplace (cached). Attribute names DIFFER between product types:
    // e.g. APPAREL uses `size`/`color`, DRESS-like types use `apparel_size`/
    // `color_name`. Empty set on fetch failure.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchProductTypeSchemaProps(QString marketplaceId,
                                                  QString productType,
                                                  QSet<QString>* propsOut);

    // Raw allowed enum values of a composite size attribute's size_system and
    // size_class sub-fields (e.g. apparel_size or shapewear_size). Unlike
    // fetchApparelSizeSchemaInfo these are the bare enum tokens (no display
    // names) — usable directly as feed values. Empty on failure.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchCompositeSizeEnums(QString marketplaceId,
                                              QString productType,
                                              QString compositeAttr,
                                              QStringList* sizeSystems,
                                              QStringList* sizeClasses);

    // Direct Listings Items API PATCH with op=delete — removes the given stored
    // entries of an attribute. Unlike JSON_LISTINGS_FEED (which does not support
    // delete), the direct PATCH endpoint does. Used to clean legacy attributes
    // that the current product type schema no longer defines (e.g. a stale
    // apparel_size on an APPAREL listing) or wrong-language entries.
    // storedValue: the exact entries to delete (fetch them first).
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> deleteListingAttribute(QString marketplaceId, QString sku,
                                             QString productType, QString attribute,
                                             QJsonArray storedValue,
                                             bool* success,
                                             QString* detailsOut = nullptr);

    // Builds and uploads a JSON_LISTINGS_FEED variation relationship feed via the Feeds API.
    // Fetches nothing — all data is passed in. Polls until DONE (3 min max).
    // Returns a human-readable status string in *resultOut.
    // GCC 13 ICE workaround: all non-trivially-destructible params by value.
    // productType: SP-API product type string (e.g. "CLOTHING", "SHIRT").
    QCoro::Task<void> uploadVariationFeed(QStringList marketplaceIds,
                                           QString productType,
                                           QString variationTheme,
                                           QList<VariationFeedEntry> entries,
                                           QString* resultOut);

    // Generic JSON_LISTINGS_FEED submission with pre-built messages.
    // Handles feed document creation, S3 upload, submission, polling and
    // result-report summary. Writes the submitted body to /tmp for diagnosis.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> submitJsonListingsFeed(QStringList marketplaceIds,
                                              QJsonArray messages,
                                              QString* resultOut);

    // PATCH all image slots on a listing using CDN URLs (no binary upload).
    // imageUrls[0] → main_product_image_locator,
    // imageUrls[1..] → other_product_image_locator_1..8 (clipped to 8 others).
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> patchListingImageUrls(QString marketplaceId,
                                            QString sku,
                                            QString productType,
                                            QStringList imageUrls,
                                            bool* success);

    // PATCH the size chart attribute for a single SKU via the Listings Items API.
    // headerCells: full header row — first cell is the label-column header (usually ""),
    //              remaining cells are size labels (e.g. "", "S", "M", "L").
    // dataRows: each QStringList has label in col 0, then one value per size column.
    // sizeChartAttr: attribute name to PATCH (e.g. "size_chart_display"). If empty,
    //                defaults to "size_chart_display".
    // Sets *success = true on HTTP 200.
    // GCC 13 ICE workaround: all non-trivially-destructible params passed by value.
    QCoro::Task<void> patchListingSizeChart(QString marketplaceId,
                                            QString sku,
                                            QString productType,
                                            QStringList headerCells,
                                            QList<QStringList> dataRows,
                                            bool* success,
                                            QString sizeChartAttr = {});

    // Upload a JPEG image to a listing's additional image slot via the SP-API
    // Uploads API + Listings Items PATCH.
    // imageIndex: -1 = append at end (auto-detects first empty slot),
    //             -2 = replace last (auto-detects last used slot),
    //             >= 0 = replace at that 0-based slot (0 → PT01 / other_product_image_locator_1).
    // GCC 13 ICE workaround: all non-trivially-destructible params passed by value.
    QCoro::Task<void> patchListingImage(QString marketplaceId,
                                        QString sku,
                                        QString productType,
                                        QByteArray jpegData,
                                        int imageIndex,
                                        bool* success);

    // Upload a JPEG as the MAIN product image (main_product_image_locator).
    // Uses imgBB to host the binary, then PATCHes the listing with the public URL.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> patchListingMainImage(QString marketplaceId,
                                            QString sku,
                                            QString productType,
                                            QByteArray jpegData,
                                            bool* success);

    // Fetch ALL FBA inventory items for a marketplace, filling *asinToSku with
    // ASIN → sellerSku pairs. Handles pagination automatically.
    // FBA items only; MFN items will not appear.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchAllFbaSkus(QString marketplaceId,
                                      QHash<QString, QString>* asinToSku);

    // Fetch ALL listings (FBA + MFN) via the Reports API GET_MERCHANT_LISTINGS_ALL_DATA.
    // Requests the report, polls until ready (up to ~3 min), downloads and parses the TSV.
    // Fills *asinToSku with ASIN → sellerSku pairs.
    // Optionally fills *asinToInventory with ASIN → quantity-available.
    // GCC 13 ICE workaround: params by value.
    // asinToGtin: optional; filled with ASIN → {gtin, gtinType} from non-ASIN rows in the report.
    QCoro::Task<void> fetchAllSkusViaReport(QString marketplaceId,
                                            QHash<QString, QString>* asinToSku,
                                            QHash<QString, int>* asinToInventory = nullptr,
                                            QHash<QString, QPair<QString,QString>>* asinToGtin = nullptr);

    // Retrieve the productType of a seller listing via the Listings Items API summaries.
    // *productType is empty on error or when the listing is not found.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchListingProductType(QString marketplaceId,
                                              QString sku,
                                              QString* productType);

    // Retrieve the ASIN for a seller listing given its SKU via the Listings Items API.
    // Tries marketplaceId first; falls back to EU, NA, JP if not found.
    // *asin is empty on error or when the listing is not found in any region.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchAsinBySku(QString marketplaceId,
                                     QString sku,
                                     QString* asin);

    // Query the Product Type Definitions API to find the name of the size chart
    // attribute for the given productType (e.g. "size_chart_display" for SHIRT).
    // Downloads and searches the JSON Schema from the S3 URL returned by the API.
    // *attrName is empty when no size chart attribute exists for that product type.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchSizeChartAttributeName(QString marketplaceId,
                                                   QString productType,
                                                   QString* attrName);

    // Fetch brand/category/gender/age attributes for a single listing SKU.
    // The caller must pre-set item->asin and item->sku; this fills title/brand/category/gender/age.
    // allMarketplaceIds: if non-empty, the API is queried for all those IDs at once so
    // item->existsInMarketplaces is populated with every marketplace where the SKU is listed.
    // Must all belong to the same SP-API region as marketplaceId.
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> fetchListingAttributes(QString marketplaceId,
                                              QString sku,
                                              StoreItem* item,
                                              QStringList allMarketplaceIds = {});

    // Key: marketplaceId → { asin → units sold }.
    // Uses GET_FLAT_FILE_ALL_ORDERS_DATA_BY_ORDER_DATE_GENERAL.
    QCoro::Task<void> fetchSalesReport(QStringList marketplaceIds,
                                        QDateTime dataStartTime,
                                        QDateTime dataEndTime,
                                        QHash<QString, QHash<QString,int>>* mpToAsinUnits);

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
    // Product type schema cache: "productType:marketplaceId" → raw schema JSON.
    QHash<QString, QByteArray> m_ptSchemaCache;

    // Downloads (or serves from cache) the raw product type schema JSON.
    // Empty *out on failure. GCC 13 ICE workaround: params by value.
    QCoro::Task<void> _fetchPtSchema(QString marketplaceId, QString productType,
                                     QByteArray* out);

    // Per-marketplace endpoint lookup
    static QString endpointForMarketplace(const QString& marketplaceId);

    // LWA token (cached 55 minutes per region). lwaRegion is "EU", "NA", or "JP".
    // Uses an output parameter (instead of co_return QString) to avoid GCC 13
    // coroutine ICE on non-trivially-destructible Task<T> return types.
    QCoro::Task<void> _getAccessToken(QString lwaRegion, QString* out);

    // Single HTTP GET to the catalog endpoint (includes LWA token + SigV4).
    // Writes the raw JSON body (empty on error) into *out.
    QCoro::Task<void> _doGet(const QString& marketplaceId,
                             const QString& asin,
                             const QStringList& includedData,
                             QByteArray* out);

    // Fetch a single item and fill it into *out
    // (passes by value: GCC 13 ICE workaround for coroutine frame with const-ref params)
    QCoro::Task<void> _fetchAsinItem(QString asin, QString marketplaceId, AsinItem* out);

    QCoro::Task<void> _fetchListingAttributesFull(QString marketplaceId, QString sku, StoreItem* item,
                                                   QStringList allMarketplaceIds = {});

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
    QString m_imgbbApiKey;

    // LWA access token cache — one entry per geographic region (EU / NA / JP)
    QString   m_accessTokenEu;   QDateTime m_accessTokenExpiryEu;
    QString   m_accessTokenNa;   QDateTime m_accessTokenExpiryNa;
    QString   m_accessTokenJp;   QDateTime m_accessTokenExpiryJp;

    QNetworkAccessManager* m_nam = nullptr;

    // Rate-limiting: track when the last catalog GET was sent so we can
    // insert a short pause before the next one and stay under Amazon's quota.
    QDateTime m_lastRequestTime;

    QString m_lastError;

#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    MockFn m_mock;
#endif
};

#endif // AMAZONCATALOGAPI_H
