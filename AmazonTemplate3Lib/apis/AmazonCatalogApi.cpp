// GCC 13 ICE workaround: coroutines with non-trivially-destructible locals in
// the frame trigger a bug in build_special_member_call (cp/call.cc:11096).
// Forcing O1 avoids the affected code path in the coroutine lowering pass.
// In addition, every coroutine in this translation unit returns
// QCoro::Task<void> and communicates its result via an output parameter, to
// avoid co_awaiting a Task<T> whose T is non-trivially destructible (another
// form of the same GCC 13 bug).
#pragma GCC optimize("O1")
#include "AmazonCatalogApi.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QTimer>
#include <QEventLoop>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <QCoro/QCoroNetworkReply>

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QString AmazonCatalogApi::endpointForMarketplace(const QString& marketplaceId)
{
    // EU marketplaces
    static const QStringList euIds = {
        "A1F83G8C2ARO7P", // UK
        "A1PA6795UKMFR9", // DE
        "A13V1IB3VIYZZH", // FR
        "A1RKKUPIHCS9HS", // ES
        "APJ6JRA9NG5V4",  // IT
        "A1805IZSGTT6HS", // NL
        "A2NODRKZP88ZB9", // SE
        "A1C3SOZRARQ6R3", // PL
        "AMEN7PMS3EDWL",  // BE
        "A28R8C7NBKEWEA"  // IE
    };
    static const QStringList naIds = {
        "ATVPDKIKX0DER",  // US
        "A2EUQ1WTGCTBG2", // CA
        "A1AM78C64UM0Y8"  // MX
    };
    static const QStringList jpIds = {
        "A1VC38T7YXB528"  // JP
    };
    if (euIds.contains(marketplaceId))
        return QStringLiteral("sellingpartnerapi-eu.amazon.com");
    if (naIds.contains(marketplaceId))
        return QStringLiteral("sellingpartnerapi-na.amazon.com");
    if (jpIds.contains(marketplaceId))
        return QStringLiteral("sellingpartnerapi-fe.amazon.com");
    // Default: EU
    return QStringLiteral("sellingpartnerapi-eu.amazon.com");
}

QString AmazonCatalogApi::lwaRegionForMarketplace(const QString& marketplaceId)
{
    static const QStringList naIds = {
        "ATVPDKIKX0DER", "A2EUQ1WTGCTBG2", "A1AM78C64UM0Y8"
    };
    static const QStringList jpIds = {
        "A1VC38T7YXB528"
    };
    if (naIds.contains(marketplaceId)) return QStringLiteral("NA");
    if (jpIds.contains(marketplaceId)) return QStringLiteral("JP");
    return QStringLiteral("EU"); // all European marketplaces + default
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AmazonCatalogApi::AmazonCatalogApi(const QString& lwaClientId,
                                   const QString& lwaClientSecret,
                                   const QString& lwaRefreshTokenEu,
                                   const QString& lwaRefreshTokenNa,
                                   const QString& lwaRefreshTokenJp,
                                   const QString& sellerIdEu,
                                   const QString& sellerIdNa,
                                   const QString& sellerIdJp,
                                   QObject* parent)
    : QObject(parent)
    , m_lwaClientId(lwaClientId)
    , m_lwaClientSecret(lwaClientSecret)
    , m_lwaRefreshTokenEu(lwaRefreshTokenEu)
    , m_lwaRefreshTokenNa(lwaRefreshTokenNa)
    , m_lwaRefreshTokenJp(lwaRefreshTokenJp)
    , m_sellerIdEu(sellerIdEu)
    , m_sellerIdNa(sellerIdNa)
    , m_sellerIdJp(sellerIdJp)
{
}

AmazonCatalogApi::~AmazonCatalogApi() = default;

QNetworkAccessManager* AmazonCatalogApi::_nam()
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    return m_nam;
}

#ifdef AMAZONCATALOGAPI_UNIT_TESTS
void AmazonCatalogApi::setMockForTests(MockFn mock)
{
    m_mock = std::move(mock);
}

void AmazonCatalogApi::resetForTests()
{
    m_mock = nullptr;
    m_accessTokenEu.clear(); m_accessTokenExpiryEu = QDateTime();
    m_accessTokenNa.clear(); m_accessTokenExpiryNa = QDateTime();
    m_accessTokenJp.clear(); m_accessTokenExpiryJp = QDateTime();
}
#endif

// ---------------------------------------------------------------------------
// LWA access token
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::_getAccessToken(QString lwaRegion, QString* out)
{
    // Select the per-region cache and refresh token via pointers
    // (raw pointers are trivially destructible — safe across co_await with GCC 13)
    const bool isNa = (lwaRegion == "NA");
    const bool isJp = (lwaRegion == "JP");
    QString*   const pToken   = isNa ? &m_accessTokenNa   : (isJp ? &m_accessTokenJp   : &m_accessTokenEu);
    QDateTime* const pExpiry  = isNa ? &m_accessTokenExpiryNa : (isJp ? &m_accessTokenExpiryJp : &m_accessTokenExpiryEu);
    const QString* const pRefresh = isNa ? &m_lwaRefreshTokenNa : (isJp ? &m_lwaRefreshTokenJp : &m_lwaRefreshTokenEu);

    if (!pToken->isEmpty() && pExpiry->isValid()
        && QDateTime::currentDateTimeUtc() < *pExpiry) {
        *out = *pToken;
        co_return;
    }

    // No token configured for this region — skip without touching m_lastError.
    if (pRefresh->isEmpty()) {
        qDebug() << "AmazonCatalogApi: no refresh token for region" << lwaRegion << "- skipping";
        co_return; // *out stays empty
    }

#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    if (m_mock) {
        *pToken  = QStringLiteral("mock-access-token");
        *pExpiry = QDateTime::currentDateTimeUtc().addSecs(55 * 60);
        *out = *pToken;
        co_return;
    }
#endif

    QUrl url(QStringLiteral("https://api.amazon.com/auth/o2/token"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("grant_type", "refresh_token");
    body.addQueryItem("refresh_token", *pRefresh);
    body.addQueryItem("client_id", m_lwaClientId);
    body.addQueryItem("client_secret", m_lwaClientSecret);
    const QByteArray payload = body.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply* reply = _nam()->post(req, payload);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject obj = doc.object();
    *pToken = obj.value("access_token").toString();
    if (pToken->isEmpty()) {
        const QString errCode = obj.value("error").toString();
        const QString errDesc = obj.value("error_description").toString();
        m_lastError = errDesc.isEmpty() ? errCode : errDesc;
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("LWA token exchange failed");
        qWarning() << "AmazonCatalogApi: LWA token exchange failed for region" << lwaRegion
                   << ":" << m_lastError
                   << "Response:" << QString::fromUtf8(data.left(500));
    } else {
        qDebug() << "AmazonCatalogApi: LWA token obtained for region" << lwaRegion
                 << ", expires_in =" << obj.value("expires_in").toInt();
    }
    const int expiresIn = obj.value("expires_in").toInt(3600);
    const int cacheSecs = qMin(expiresIn - 300, 55 * 60);
    *pExpiry = QDateTime::currentDateTimeUtc().addSecs(qMax(cacheSecs, 60));
    *out = *pToken;
    co_return;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Case-insensitive raw-header lookup (Qt's rawHeader() is case-sensitive,
// but HTTP headers are case-insensitive per RFC 7230).
static QString rawHeaderCI(QNetworkReply* reply, const QByteArray& name)
{
    const QByteArray lower = name.toLower();
    for (const QByteArray& h : reply->rawHeaderList()) {
        if (h.toLower() == lower)
            return QString::fromUtf8(reply->rawHeader(h));
    }
    return {};
}

// Writes a full request+response diagnostic file to /tmp/sp-api-{asin}-{ts}.txt
// for attachment to Amazon support cases. The access token is truncated to
// avoid leaking credentials.
static void writeDiagnosticFile(const QNetworkRequest& req,
                                QNetworkReply*         reply,
                                const QByteArray&      responseBody,
                                const QString&         asin)
{
    const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
    const QString filePath = QStringLiteral("/tmp/sp-api-%1-%2.txt").arg(asin, ts);

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "AmazonCatalogApi: could not write diagnostic file" << filePath;
        return;
    }

    QTextStream s(&f);
    s << "=== REQUEST ===\n";
    s << "GET " << req.url().toString() << "\n\n";
    s << "Request headers:\n";
    for (const QByteArray& h : req.rawHeaderList()) {
        QByteArray val = req.rawHeader(h);
        if (h.toLower() == "x-amz-access-token" && val.size() > 20)
            val = val.left(20) + "...[truncated]";
        s << "  " << QString::fromUtf8(h) << ": " << QString::fromUtf8(val) << "\n";
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    s << "\n=== RESPONSE ===\n";
    s << "HTTP " << httpStatus << "\n\n";
    s << "Response headers:\n";
    for (const QByteArray& h : reply->rawHeaderList())
        s << "  " << QString::fromUtf8(h) << ": " << QString::fromUtf8(reply->rawHeader(h)) << "\n";
    s << "\nResponse body:\n";
    s << QString::fromUtf8(responseBody) << "\n";

    qDebug() << "AmazonCatalogApi: diagnostic file written to" << filePath;
}

// ---------------------------------------------------------------------------
// _doGet — single GET call to the catalog endpoint
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::_doGet(const QString& marketplaceId,
                                           const QString& asin,
                                           const QStringList& includedData,
                                           QByteArray* out)
{
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString path = QStringLiteral("/catalog/2022-04-01/items/") + asin;

    QUrlQuery query;
    query.addQueryItem("marketplaceIds", marketplaceId);
    query.addQueryItem("includedData", includedData.join(","));
    {
        const QString lwaReg = lwaRegionForMarketplace(marketplaceId);
        const bool isNa = (lwaReg == "NA");
        const bool isJp = (lwaReg == "JP");
        const QString& sel = isNa ? m_sellerIdNa : (isJp ? m_sellerIdJp : m_sellerIdEu);
        if (!sel.isEmpty())
            query.addQueryItem("sellerId", sel);
    }

#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    if (m_mock) {
        QMap<QString,QString> qp;
        for (const auto& kv : query.queryItems())
            qp.insert(kv.first, kv.second);
        *out = m_mock(path, qp);
        co_return;
    }
#endif

    QUrl url;
    url.setScheme("https");
    url.setHost(endpoint);
    url.setPath(path);
    url.setQuery(query);

    QNetworkRequest req(url);
    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        qWarning() << "AmazonCatalogApi: aborting GET, no access token";
        co_return;   // *out stays empty
    }
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "AmazonCatalogApi: GET" << url.toString()
                   << "HTTP" << httpStatus
                   << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
                   << "error:" << reply->errorString()
                   << "body:" << QString::fromUtf8(data.left(500));
        writeDiagnosticFile(req, reply, data, asin);
    } else {
        qDebug() << "AmazonCatalogApi: GET" << path << "HTTP" << httpStatus
                 << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
                 << "bytes:" << data.size();
    }
    reply->deleteLater();
    *out = data;

    // On 403, automatically retry via the search endpoint (different code path on Amazon's side)
    if (httpStatus == 403) {
        qDebug() << "AmazonCatalogApi: 403 on item endpoint for" << asin
                 << "- retrying via search endpoint";
        co_await _doSearchFallback(marketplaceId, asin, includedData, out);
    }
    co_return;
}

// ---------------------------------------------------------------------------
// _doSearchFallback — GET /catalog/2022-04-01/items?identifiers=…&identifierType=ASIN
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::_doSearchFallback(QString marketplaceId,
                                    QString asin,
                                    QStringList includedData,
                                    QByteArray* out)
{
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString path = QStringLiteral("/catalog/2022-04-01/items");

    QUrlQuery query;
    query.addQueryItem("identifiers",    asin);
    query.addQueryItem("identifierType", QStringLiteral("ASIN"));
    query.addQueryItem("marketplaceIds", marketplaceId);
    query.addQueryItem("includedData",   includedData.join(","));
    {
        const QString lwaReg = lwaRegionForMarketplace(marketplaceId);
        const bool isNa = (lwaReg == "NA");
        const bool isJp = (lwaReg == "JP");
        const QString& sel = isNa ? m_sellerIdNa : (isJp ? m_sellerIdJp : m_sellerIdEu);
        if (!sel.isEmpty())
            query.addQueryItem("sellerId", sel);
    }

    QUrl url;
    url.setScheme("https");
    url.setHost(endpoint);
    url.setPath(path);
    url.setQuery(query);

    QNetworkRequest req(url);
    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        co_return;
    }
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: search fallback GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray searchData = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");

    qDebug() << "AmazonCatalogApi: search fallback HTTP" << httpStatus
             << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
             << "bytes:" << searchData.size();

    if (httpStatus != 200 || searchData.isEmpty()) {
        qWarning() << "AmazonCatalogApi: search fallback failed HTTP" << httpStatus
                   << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
                   << "body:" << QString::fromUtf8(searchData.left(300));
        writeDiagnosticFile(req, reply, searchData, asin);
        reply->deleteLater();
        co_return;  // *out keeps the original 403 body
    }
    reply->deleteLater();

    // Extract items[0] — its structure mirrors the direct /items/{asin} response
    const QJsonArray items =
        QJsonDocument::fromJson(searchData).object().value("items").toArray();
    if (items.isEmpty()) {
        qWarning() << "AmazonCatalogApi: search fallback returned 0 items for ASIN" << asin;
        co_return;
    }
    *out = QJsonDocument(items.first().toObject()).toJson(QJsonDocument::Compact);
    co_return;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

void AmazonCatalogApi::_parseRelationships(const QByteArray& jsonBody,
                                           QStringList* parentAsins,
                                           QStringList* childAsins)
{
    if (parentAsins) parentAsins->clear();
    if (childAsins)  childAsins->clear();

    const QJsonDocument doc = QJsonDocument::fromJson(jsonBody);
    const QJsonObject root = doc.object();

    const QJsonArray relArr = root.value("relationships").toArray();
    // Each "relationships" entry has its own per-marketplace relationships[]
    for (const QJsonValue& relEntryVal : relArr) {
        const QJsonObject relEntry = relEntryVal.toObject();
        const QJsonArray inner = relEntry.value("relationships").toArray();
        for (const QJsonValue& relVal : inner) {
            const QJsonObject rel = relVal.toObject();
            const QString type = rel.value("type").toString();
            if (type == "VARIATION") {
                // parentAsins indicates this item is a child of those asins
                const QJsonArray parents = rel.value("parentAsins").toArray();
                for (const QJsonValue& pv : parents)
                    if (parentAsins) parentAsins->append(pv.toString());
                const QJsonArray children = rel.value("childAsins").toArray();
                for (const QJsonValue& cv : children)
                    if (childAsins) childAsins->append(cv.toString());
            }
        }
    }

    // Also support flat relationships[0].parentAsins/childAsins layout (mock convenience)
    if ((parentAsins && parentAsins->isEmpty()) || (childAsins && childAsins->isEmpty())) {
        if (!relArr.isEmpty()) {
            const QJsonObject first = relArr.first().toObject();
            if (parentAsins && parentAsins->isEmpty()) {
                const QJsonArray parents = first.value("parentAsins").toArray();
                for (const QJsonValue& pv : parents)
                    parentAsins->append(pv.toString());
            }
            if (childAsins && childAsins->isEmpty()) {
                const QJsonArray children = first.value("childAsins").toArray();
                for (const QJsonValue& cv : children)
                    childAsins->append(cv.toString());
            }
        }
    }
}

static QString firstAttrValue(const QJsonObject& attrs, const QString& key)
{
    const QJsonArray arr = attrs.value(key).toArray();
    if (arr.isEmpty()) return {};
    const QJsonObject obj = arr.first().toObject();
    if (obj.contains("value"))
        return obj.value("value").toString();
    return arr.first().toString();
}

static QStringList allAttrValues(const QJsonObject& attrs, const QString& key)
{
    QStringList result;
    for (const QJsonValue& v : attrs.value(key).toArray()) {
        const QString val = v.toObject().value("value").toString();
        if (!val.isEmpty())
            result << val;
    }
    return result;
}

static AmazonCatalogApi::AsinItem parseAsinItem(const QString& asin, const QByteArray& data)
{
    AmazonCatalogApi::AsinItem item;
    item.asin = asin;
    if (data.isEmpty())
        return item;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();

    const QJsonArray summaries = root.value("summaries").toArray();
    if (!summaries.isEmpty()) {
        const QJsonObject sum = summaries.first().toObject();
        item.title = sum.value("itemName").toString();
        if (item.title.isEmpty())
            item.title = sum.value("title").toString();
    }

    const QJsonObject attrs = root.value("attributes").toObject();
    item.color = firstAttrValue(attrs, "color");
    item.size  = firstAttrValue(attrs, "size");
    item.hasSizeTable = !attrs.value("size_chart_node_id").toArray().isEmpty();

    // Bullet points — try attributes.bullet_point first, fall back to summaries
    for (const QJsonValue& v : attrs.value("bullet_point").toArray()) {
        const QString val = v.toObject().value("value").toString();
        if (!val.isEmpty() && !item.bulletPoints.contains(val))
            item.bulletPoints << val;
    }
    if (item.bulletPoints.isEmpty() && !summaries.isEmpty()) {
        for (const QJsonValue& v : summaries.first().toObject().value("bulletPoints").toArray()) {
            const QString val = v.toString();
            if (!val.isEmpty())
                item.bulletPoints << val;
        }
    }

    // Material / fabric attributes
    static const QList<QPair<QString,QString>> kMatKeys = {
        {"material_type",        QStringLiteral("Material")},
        {"fabric_type",          QStringLiteral("Fabric")},
        {"material_composition", QStringLiteral("Composition")},
        {"outer_material_type",  QStringLiteral("Outer material")},
        {"inner_material_type",  QStringLiteral("Inner material")},
        {"sole_material",        QStringLiteral("Sole")},
        {"lining_description",   QStringLiteral("Lining")},
        {"material_feature",     QStringLiteral("Feature")},
    };
    for (const auto& [key, label] : kMatKeys) {
        const QStringList vals = allAttrValues(attrs, key);
        if (!vals.isEmpty())
            item.materialAttrs << label + QStringLiteral(": ") + vals.join(QStringLiteral(", "));
    }

    // Collect one URL per image angle (MAIN, PT01, PT02…). Amazon returns the same
    // angle at several resolutions (75, 300, 500, 2000 px). We keep the resolution
    // closest to 500 px from above (i.e. ≥500 preferred, smallest among those).
    // If no entry is ≥500 px we fall back to the largest available.
    //
    // bestByVariant: variant → {url, height}  (height = max(w,h) of chosen entry)
    struct BestImg { QString url; int size = 0; };
    QMap<QString, BestImg> bestByVariant;
    QStringList variantOrder; // preserves insertion order

    const QJsonArray imageSets = root.value("images").toArray();
    for (const QJsonValue &setVal : imageSets) {
        const QJsonObject set = setVal.toObject();
        const QString setAsin = set.value(QStringLiteral("asin")).toString();
        if (!setAsin.isEmpty() && setAsin != asin)
            continue;
        const QJsonArray imgs = set.value(QStringLiteral("images")).toArray();
        for (const QJsonValue &v : imgs) {
            const QJsonObject img = v.toObject();
            const QString link    = img.value(QStringLiteral("link")).toString();
            const QString variant = img.value(QStringLiteral("variant")).toString();
            if (link.isEmpty() || variant.isEmpty())
                continue;
            const int w = img.value(QStringLiteral("width")).toInt();
            const int h = img.value(QStringLiteral("height")).toInt();
            const int sz = qMax(w, h);

            if (!bestByVariant.contains(variant))
                variantOrder << variant;

            BestImg &best = bestByVariant[variant];
            if (best.url.isEmpty()) {
                best = {link, sz};
            } else {
                // Prefer ≥500 px; among those, smaller is better (less bandwidth).
                // Among sub-500 entries, larger is better.
                const bool curGe500  = (best.size >= 500);
                const bool newGe500  = (sz        >= 500);
                const bool replace =
                    (!curGe500 && newGe500)                        // upgrade to ≥500
                    || (curGe500 && newGe500 && sz < best.size)    // both ≥500, pick smaller
                    || (!curGe500 && !newGe500 && sz > best.size); // both <500, pick larger
                if (replace)
                    best = {link, sz};
            }
        }
        if (!bestByVariant.isEmpty())
            break;
    }

    // Rebuild allImageUrls in original variant order (MAIN first, then PT01, PT02…)
    for (const QString &variant : variantOrder) {
        const QString &url = bestByVariant[variant].url;
        item.allImageUrls << url;
        if (variant == QStringLiteral("MAIN"))
            item.mainImageUrl = url;
    }

    return item;
}

QCoro::Task<void>
AmazonCatalogApi::checkAsinExists(QString asin, QString marketplaceId, bool* out)
{
    *out = false;
    // GCC 13 ICE workaround: use static to keep QStringList out of coroutine frame.
    static const QStringList kData = {QStringLiteral("summaries")};
    QByteArray body;
    co_await _doGet(marketplaceId, asin, kData, &body);
    if (body.isEmpty())
        co_return;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    *out = doc.isObject() && !doc.object().value(QStringLiteral("asin")).toString().isEmpty();
    co_return;
}

QCoro::Task<void>
AmazonCatalogApi::_fetchAsinItem(QString asin, QString marketplaceId, AsinItem* out)
{
    static const QStringList kSumAttr =
        QStringList() << "summaries" << "attributes" << "images";
    QByteArray data;
    co_await _doGet(marketplaceId, asin, kSumAttr, &data);
    *out = parseAsinItem(asin, data);
}

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

QString AmazonCatalogApi::sellerIdForMarketplace(const QString& marketplaceId) const
{
    const QString region = lwaRegionForMarketplace(marketplaceId);
    if (region == "NA") return m_sellerIdNa;
    if (region == "JP") return m_sellerIdJp;
    return m_sellerIdEu;
}

// ---------------------------------------------------------------------------
// patchListingSizeChart — PATCH /listings/2021-08-01/items/{sellerId}/{sku}
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::patchListingSizeChart(QString marketplaceId,
                                                          QString sku,
                                                          QString productType,
                                                          QStringList headerCells,
                                                          QList<QStringList> dataRows,
                                                          bool* success)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    // Build size_chart JSON
    QJsonArray headerArr;
    for (const QString& h : headerCells)
        headerArr.append(QJsonObject{{QStringLiteral("text"), h}});

    QJsonArray dataRowsArr;
    for (const QStringList& row : dataRows) {
        QJsonArray cells;
        for (const QString& c : row)
            cells.append(QJsonObject{{QStringLiteral("text"), c}});
        dataRowsArr.append(QJsonObject{{QStringLiteral("cells"), cells}});
    }

    const QJsonObject sizeChart{
        {QStringLiteral("title"),      QStringLiteral("Size Chart")},
        {QStringLiteral("header_row"), QJsonObject{{QStringLiteral("cells"), headerArr}}},
        {QStringLiteral("data_rows"),  dataRowsArr}
    };

    const QJsonObject attrValue{
        {QStringLiteral("marketplace_id"), marketplaceId},
        {QStringLiteral("size_chart"),     sizeChart}
    };

    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/size_chart_display")},
        {QStringLiteral("value"), QJsonArray{attrValue}}
    };

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     QJsonArray{patch}}
    };
    const QByteArray jsonBody = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    // Build URL: /listings/2021-08-01/items/{sellerId}/{sku}?marketplaceIds={marketplaceId}
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString urlPath  = QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(urlPath);
    url.setQuery(query);

    // Acquire LWA token
    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: PATCH" << url.toString()
             << "body:" << jsonBody.left(200);
    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    reply->deleteLater();

    qDebug() << "AmazonCatalogApi: PATCH" << urlPath
             << "HTTP" << httpStatus
             << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
             << "response:" << QString::fromUtf8(data.left(300));

    if (httpStatus >= 400 || reply->error() != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("HTTP %1 for SKU %2: %3")
                          .arg(httpStatus).arg(sku, QString::fromUtf8(data.left(300)));
        qWarning() << "AmazonCatalogApi: PATCH failed for" << sku << ":" << m_lastError;

        // Write diagnostic file
        const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        const QString diagPath = QStringLiteral("/tmp/sp-api-patch-%1-%2.txt").arg(sku, ts);
        QFile f(diagPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "=== REQUEST ===\n"
              << "PATCH " << url.toString() << "\n\n"
              << "Request body:\n" << QString::fromUtf8(jsonBody) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << httpStatus << "\n\n"
              << QString::fromUtf8(data) << "\n";
            qDebug() << "AmazonCatalogApi: diagnostic written to" << diagPath;
        }
        co_return;
    }

    *success = true;
    co_return;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::fetchVariationFamily(const QString& asin,
                                       const QString& marketplaceId,
                                       VariationFamily* out)
{
    // Declare all non-trivially-destructible locals up front so none of them
    // become temporaries spanning a suspension point (GCC 13 ICE workaround).
    VariationFamily family;
    QByteArray firstBody;
    QStringList parentAsins;
    QStringList childAsins;
    QString parentAsinToUse;
    QStringList finalChildAsins;
    QByteArray parentBody;
    QStringList unusedParents;
    QStringList uniqueChildAsins;
    AsinItem fallback;

    static const QStringList kRelOnly =
        QStringList() << "relationships";

    // Step 1: fetch the given ASIN to determine if it's a child or the parent.
    co_await _doGet(marketplaceId, asin, kRelOnly, &firstBody);
    if (firstBody.isEmpty()) {
        *out = std::move(family);
        co_return;
    }

    // Detect Amazon API error response (e.g. 403 Unauthorized, 400 Bad Request)
    {
        const QJsonDocument errDoc = QJsonDocument::fromJson(firstBody);
        const QJsonArray errs = errDoc.object().value("errors").toArray();
        if (!errs.isEmpty()) {
            const QJsonObject e = errs.first().toObject();
            QString msg = e.value("message").toString();
            if (msg.isEmpty()) msg = e.value("code").toString();
            if (msg.isEmpty()) msg = QStringLiteral("Amazon API error");
            if (msg.contains(QStringLiteral("Access to requested resource is denied"), Qt::CaseInsensitive))
                msg += QStringLiteral(" — check: (1) SP-API app has 'Catalog Items' role enabled,"
                                      " or (2) this brand restricts catalog access to its own account");
            m_lastError = msg;
            qWarning() << "AmazonCatalogApi: API error for ASIN" << asin << ":" << m_lastError;
            *out = std::move(family);
            co_return;
        }
    }

    _parseRelationships(firstBody, &parentAsins, &childAsins);
    qDebug() << "AmazonCatalogApi: ASIN" << asin
             << "parentAsins:" << parentAsins
             << "childAsins:" << childAsins;

    if (!parentAsins.isEmpty()) {
        // The given ASIN is a child -> fetch its parent
        parentAsinToUse = parentAsins.first();
    } else {
        // Treat the given ASIN as the parent
        parentAsinToUse = asin;
    }
    family.parentAsin = parentAsinToUse;

    // Step 2: fetch parent to get its childAsins list (only if we don't have it yet).
    if (parentAsinToUse == asin) {
        finalChildAsins = childAsins;
    } else {
        co_await _doGet(marketplaceId, parentAsinToUse, kRelOnly, &parentBody);
        _parseRelationships(parentBody, &unusedParents, &finalChildAsins);
    }

    // Deduplicate while preserving order
    for (const QString& c : finalChildAsins) {
        if (!c.isEmpty() && !uniqueChildAsins.contains(c))
            uniqueChildAsins.append(c);
    }

    // Step 3: fetch each child ASIN sequentially
    for (const QString& childAsin : uniqueChildAsins) {
        AsinItem item;
        co_await _fetchAsinItem(childAsin, marketplaceId, &item);
        family.children.append(std::move(item));
    }

    // Step 3b: for children still missing color or size, retry from FR then DE.
    // Amazon sometimes omits attributes on non-primary marketplaces; FR and DE
    // tend to have the most complete catalogue data for EU products.
    static const QStringList kAttrFallbacks = {
        QStringLiteral("A13V1IB3VIYZZH"), // FR
        QStringLiteral("A1PA6795UKMFR9"), // DE
    };
    for (auto& child : family.children) {
        if (!child.color.isEmpty() && !child.size.isEmpty())
            continue;
        for (const QString& fbMp : kAttrFallbacks) {
            if (fbMp == marketplaceId)
                continue;
            fallback = AsinItem{};
            co_await _fetchAsinItem(child.asin, fbMp, &fallback);
            if (child.color.isEmpty() && !fallback.color.isEmpty())
                child.color = fallback.color;
            if (child.size.isEmpty() && !fallback.size.isEmpty())
                child.size = fallback.size;
            // Also take more images from this marketplace if the primary gave fewer
            if (fallback.allImageUrls.size() > child.allImageUrls.size())
                child.allImageUrls = std::move(fallback.allImageUrls);
            if (!child.color.isEmpty() && !child.size.isEmpty())
                break;
        }
    }

    // Step 4: if the first child has only one image (MAIN only), fetch the parent ASIN
    // to obtain all product angles (PT01, PT02…). Amazon's catalog API typically
    // associates secondary images with the parent, not individual child ASINs.
    if (!family.children.isEmpty()
            && family.children.first().allImageUrls.size() <= 1
            && !family.parentAsin.isEmpty()) {
        qDebug() << "AmazonCatalogApi: first child has"
                 << family.children.first().allImageUrls.size()
                 << "image(s) — fetching parent" << family.parentAsin << "for all angles";
        AsinItem parentItem;
        co_await _fetchAsinItem(family.parentAsin, marketplaceId, &parentItem);
        if (parentItem.allImageUrls.size() > family.children.first().allImageUrls.size()) {
            qDebug() << "AmazonCatalogApi: using parent's"
                     << parentItem.allImageUrls.size() << "image(s) instead";
            family.children.first().allImageUrls = std::move(parentItem.allImageUrls);
            if (family.children.first().mainImageUrl.isEmpty())
                family.children.first().mainImageUrl = parentItem.mainImageUrl;
        }
    }

    qDebug() << "AmazonCatalogApi: fetchVariationFamily done. parent ="
             << family.parentAsin << "children:" << family.children.size()
             << "first child images:" << (family.children.isEmpty() ? 0 : family.children.first().allImageUrls.size());
    *out = std::move(family);
    co_return;
}
