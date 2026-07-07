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

    // GPSR compliance entities registered in the seller account
    // (bg.local.goods.compliance.info.fill.list.query).
    // complianceInfoType: 2 = EU responsible person (GSPR rep), 3 = manufacturer,
    // 4 = after-sales responsible person.
    struct RepEntity {
        qint64  repId = 0;
        QString name;
        QString address; // single-line summary for tooltips
    };
    static constexpr int COMPLIANCE_TYPE_GSPR_REP     = 2;
    static constexpr int COMPLIANCE_TYPE_MANUFACTURER = 3;
    QCoro::Task<void> fetchComplianceEntities(int complianceInfoType, QList<RepEntity> *out);

    // ----- Product create/update (bg.local.goods.*) -------------------------

    // An existing goods matched by seller SKU (temu.local.goods.list.retrieve).
    struct ExistingGoods {
        bool    found = false;
        qint64  goodsId = 0;
        QString catId;
        QString catName; // full category path when resolvable
        QHash<QString, qint64> skuIdBySkuSn; // outSkuSn → skuId
    };
    QCoro::Task<void> lookupGoods(const QStringList &outSkuSns, ExistingGoods *out);

    // One category attribute definition (bg.local.goods.template.get).
    struct CategoryAttr {
        qint64  pid = 0;
        qint64  templatePid = 0;
        qint64  refPid = 0;
        QString name;
        bool    required = false;
        int     controlType = 1;        // 1 = pick from values, 0 = free number/text
        qint64  parentTemplatePid = 0;  // 0 = always; else conditional on that parent
        // Allowed values (value text → vid) when controlType == 1.
        QList<QPair<QString, qint64>> values;
    };
    QCoro::Task<void> fetchCategoryTemplate(qint64 catId, QList<CategoryAttr> *out);

    // One node of the category tree (bg.local.goods.cats.get).
    struct CatNode {
        qint64  catId = 0;
        QString catName;
        bool    leaf = false;
    };
    // Children of parentCatId (0 = the top-level categories).
    QCoro::Task<void> fetchCategories(qint64 parentCatId, QList<CatNode> *out);

    // Category suggestion (bg.local.goods.category.recommend). Returns a list
    // of CANDIDATE leaf category ids (not a path). Names must be resolved via
    // resolveCategoryPaths(); Temu exposes no id→name endpoint.
    QCoro::Task<void> recommendCategory(const QString &goodsName, const QString &description,
                                        const QString &imageUrl, QList<qint64> *candidateCatIds);

    // Resolves the full "A › B › Leaf" path name for each target id by crawling
    // the category tree from the root (bg.local.goods.cats.get), bounded to
    // maxCalls fetches. Fills idToPath for the ids it locates; the internal
    // node cache is reused across calls on the same instance.
    QCoro::Task<void> resolveCategoryPaths(const QList<qint64> &targetIds, int maxCalls,
                                           QHash<qint64, QString> *idToPath);

    // Re-hosts a publicly reachable image URL onto Temu's CDN
    // (bg.local.goods.image.upload). Returns the Temu CDN URL (empty on error).
    QCoro::Task<QString> uploadImageToTemu(const QString &publicUrl);

    // First configured shipping/cost template id for this store (empty if none).
    QCoro::Task<void> fetchFreightTemplateId(QString *out);

    // Generates (or fetches) a custom variation spec id under a parent spec
    // (bg.local.goods.spec.id.get). parentSpecId e.g. 1001=Color, 3001=Size.
    QCoro::Task<void> generateSpecId(qint64 catId, qint64 parentSpecId,
                                     const QString &childSpecName, qint64 *specIdOut);

    // Create (bg.local.goods.add) or update (bg.local.goods.update) a goods.
    // payload holds the assembled objects; on update, goodsId is injected.
    // Returns the goodsId on success, 0 on failure (see lastError()).
    QCoro::Task<qint64> publishGoods(const QJsonObject &payload, bool isUpdate, qint64 goodsId);

    // Create a product via the V3 publishing API (temu.local.goods.v3.add) —
    // the simpler, current path: image URLs are fetched by Temu, the category
    // is given by name (extCatName), attributes/variations are free name/value,
    // and price is basePrice/listPrice objects. Returns goodsId (0 on failure).
    QCoro::Task<qint64> publishGoodsV3(const QJsonObject &payload);

    // Partially updates an existing goods (bg.local.goods.partial.update): pass
    // goodsId + the fields to change (goodsBasic, bulletPoints, …). Note Temu
    // rejects edits until the product finishes processing (~10 min after
    // creation) with 150010205. Returns false on error (see lastError()).
    QCoro::Task<bool> updateGoodsPartial(qint64 goodsId, const QJsonObject &fields);

    // Submits GPSR compliance for a created goods (bg.local.goods.compliance.edit).
    // Verified shapes: manufacturer (repType 3) + EU responsible person (repType 2)
    // via gpsrInfo, and Product Identification (templateId 51) via extraTemplate.
    // Any repId of 0 is skipped; an empty productIdentifier is skipped. Returns
    // false on error (see lastError()).
    QCoro::Task<bool> submitCompliance(qint64 goodsId, qint64 manufacturerRepId,
                                       qint64 gsprRepId,
                                       const QString &productIdentifier = {});

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

    // Category-path cache reused across resolveCategoryPaths() calls.
    QHash<qint64, QString> m_catPathCache;

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
