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
#include <QCryptographicHash>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroTimer>
#include <zlib.h>

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

// Decompress gzip data (Amazon report downloads use real gzip, not Qt's custom format).
static QByteArray gunzip(const QByteArray &data)
{
    if (data.isEmpty()) return {};

    z_stream stream{};
    // windowBits = 15 + 16 tells zlib to expect a gzip header
    if (inflateInit2(&stream, 15 + 16) != Z_OK)
        return {};

    stream.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());

    QByteArray result;
    const int chunkSize = 64 * 1024;
    QByteArray chunk(chunkSize, '\0');

    int ret;
    do {
        stream.next_out  = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunkSize);
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&stream);
            return {};
        }
        result.append(chunk.constData(), chunkSize - static_cast<int>(stream.avail_out));
    } while (ret != Z_STREAM_END);

    inflateEnd(&stream);
    return result;
}

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
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(30'000); // 30 s — aborts silently-hanging requests
    }
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

    // Throttle: Amazon's catalog API allows ~2 req/s. Enforce at least 600 ms between
    // consecutive requests so a family with many children never triggers a 429.
    constexpr int kMinIntervalMs = 600;
    const int elapsed = m_lastRequestTime.isValid()
        ? static_cast<int>(m_lastRequestTime.msecsTo(QDateTime::currentDateTimeUtc()))
        : kMinIntervalMs;
    if (elapsed < kMinIntervalMs) {
        QTimer throttleTimer;
        throttleTimer.setSingleShot(true);
        throttleTimer.start(kMinIntervalMs - elapsed);
        co_await qCoro(&throttleTimer).waitForTimeout();
    }
    m_lastRequestTime = QDateTime::currentDateTimeUtc();

    qDebug() << "AmazonCatalogApi: GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
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
                                                          bool* success,
                                                          QString sizeChartAttr)
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

    const QString effectiveAttr = sizeChartAttr.isEmpty()
        ? QStringLiteral("size_chart_display") : sizeChartAttr;
    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/") + effectiveAttr},
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

        // On 400 "attribute path is not valid", do a GET on the listing to show
        // its actual productType + attribute names — helps find the correct path.
        QByteArray getBody;
        if (httpStatus == 400) {
            QString getToken;
            co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &getToken);
            if (!getToken.isEmpty()) {
                QUrl getUrl;
                getUrl.setScheme(QStringLiteral("https"));
                getUrl.setHost(endpoint);
                getUrl.setPath(urlPath);
                QUrlQuery getQuery;
                getQuery.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
                getQuery.addQueryItem(QStringLiteral("includedData"), QStringLiteral("summaries,attributes"));
                getUrl.setQuery(getQuery);
                QNetworkRequest getReq(getUrl);
                getReq.setRawHeader("x-amz-access-token", getToken.toUtf8());
                getReq.setRawHeader("accept", "application/json");
                QNetworkReply* getReply = _nam()->get(getReq);
                co_await qCoro(getReply).waitForFinished();
                getBody = getReply->readAll();
                getReply->deleteLater();
            }
        }

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
            if (!getBody.isEmpty())
                s << "\n=== LISTING GET (attributes + productType) ===\n"
                  << QString::fromUtf8(getBody) << "\n";
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

QCoro::Task<void> AmazonCatalogApi::patchListingImage(QString marketplaceId,
                                                      QString sku,
                                                      QString productType,
                                                      QByteArray jpegData,
                                                      int imageIndex,
                                                      bool* success)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    const QString endpoint = endpointForMarketplace(marketplaceId);

    // Step 1: Create upload destination
    const QByteArray md5Bytes = QCryptographicHash::hash(jpegData, QCryptographicHash::Md5);
    const QString contentMd5 = QString::fromLatin1(md5Bytes.toBase64());

    // Build the upload-destination URL. The resource path contains literal slashes that
    // must be percent-encoded as %2F in the URL path segment. QUrl::setPath() uses
    // DecodedMode by default and would re-encode '%' → '%25', corrupting the URL.
    // Use QUrl::fromEncoded() so the pre-encoded %2F is preserved intact.
    const QString resource = QStringLiteral("listings/items/%1/%2").arg(sellerId, sku);
    const QString encodedResource = QString::fromUtf8(
        QUrl::toPercentEncoding(resource, {}, QByteArrayLiteral("/")));
    const QUrl uploadsUrl = QUrl::fromEncoded(
        QStringLiteral("https://%1/uploads/2020-11-01/uploadDestinations/%2?marketplaceIds=%3")
            .arg(endpoint, encodedResource, marketplaceId)
            .toUtf8());

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    const QJsonObject uploadsBodyObj{
        {QStringLiteral("contentType"), QStringLiteral("image/jpeg")},
        {QStringLiteral("contentMD5"),  contentMd5}
    };
    const QByteArray uploadsBodyBytes = QJsonDocument(uploadsBodyObj).toJson(QJsonDocument::Compact);

    QNetworkRequest uploadsReq(uploadsUrl);
    uploadsReq.setRawHeader("x-amz-access-token", token.toUtf8());
    uploadsReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    uploadsReq.setRawHeader("accept", "application/json");

    const QString uploadsTs = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    qDebug() << "AmazonCatalogApi: POST upload destination" << uploadsUrl.toString();
    QNetworkReply* uploadsReply = _nam()->post(uploadsReq, uploadsBodyBytes);
    co_await qCoro(uploadsReply).waitForFinished();

    const QByteArray uploadsData = uploadsReply->readAll();
    const int uploadsStatus = uploadsReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString uploadsRequestId = rawHeaderCI(uploadsReply, "x-amzn-RequestId");
    uploadsReply->deleteLater();

    qDebug() << "AmazonCatalogApi: upload destination HTTP" << uploadsStatus
             << "RequestId:" << uploadsRequestId
             << "response:" << QString::fromUtf8(uploadsData.left(300));

    if (uploadsStatus != 201 && uploadsStatus != 200) {
        m_lastError = QStringLiteral("Upload destination HTTP %1 for SKU %2: %3")
                          .arg(uploadsStatus).arg(sku, QString::fromUtf8(uploadsData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        const QString tsFile = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        const QString diagPath = QStringLiteral("/tmp/sp-api-img-upload-%1-%2.txt").arg(sku, tsFile);
        QFile f(diagPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "=== API OPERATION ===\n"
              << "createUploadDestinationForResource\n\n"
              << "=== REQUEST ===\n"
              << "Timestamp:  " << uploadsTs << "\n"
              << "POST " << uploadsUrl.toString() << "\n"
              << "x-amz-access-token: " << token.left(20) << "...(truncated)\n"
              << "Content-Type: application/json\n"
              << "accept: application/json\n\n"
              << "Body:\n" << QString::fromUtf8(uploadsBodyBytes) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << uploadsStatus << "\n"
              << "x-amzn-RequestId: " << uploadsRequestId << "\n\n"
              << QString::fromUtf8(uploadsData) << "\n";
        }
        co_return;
    }

    const QJsonObject uploadsDoc = QJsonDocument::fromJson(uploadsData).object();
    const QJsonObject payload = uploadsDoc.value(QStringLiteral("payload")).toObject();
    const QString uploadDestinationId = payload.value(QStringLiteral("uploadDestinationId")).toString();
    const QString presignedUrl = payload.value(QStringLiteral("url")).toString();
    const QJsonObject extraHeaders = payload.value(QStringLiteral("headers")).toObject();

    if (uploadDestinationId.isEmpty() || presignedUrl.isEmpty()) {
        m_lastError = QStringLiteral("Upload destination missing uploadDestinationId or url for SKU %1: %2")
                          .arg(sku, QString::fromUtf8(uploadsData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    // Step 2: PUT image to the presigned URL
    const QUrl presignedUrlObj(presignedUrl);
    QNetworkRequest putReq(presignedUrlObj);
    putReq.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");
    for (auto it = extraHeaders.constBegin(); it != extraHeaders.constEnd(); ++it)
        putReq.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

    qDebug() << "AmazonCatalogApi: PUT image" << jpegData.size() << "bytes to presigned URL";
    QNetworkReply* putReply = _nam()->put(putReq, jpegData);
    co_await qCoro(putReply).waitForFinished();

    const QByteArray putData = putReply->readAll();
    const int putStatus = putReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError putError = putReply->error();
    putReply->deleteLater();

    qDebug() << "AmazonCatalogApi: PUT image HTTP" << putStatus;

    if (putStatus != 200 && putStatus != 204 && putError != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("PUT image HTTP %1 for SKU %2: %3")
                          .arg(putStatus).arg(sku, QString::fromUtf8(putData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    // Step 3: Determine target image slot
    int targetIndex = imageIndex; // >= 0: use directly

    if (imageIndex == -1 || imageIndex == -2) {
        const QString listingPath = QStringLiteral("/listings/2021-08-01/items/%1/%2")
                                        .arg(sellerId, sku);
        QUrlQuery listingQuery;
        listingQuery.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
        listingQuery.addQueryItem(QStringLiteral("includedData"), QStringLiteral("attributes"));

        QUrl listingUrl;
        listingUrl.setScheme(QStringLiteral("https"));
        listingUrl.setHost(endpoint);
        listingUrl.setPath(listingPath);
        listingUrl.setQuery(listingQuery);

        co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
        if (token.isEmpty()) {
            m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
            co_return;
        }

        QNetworkRequest getReq(listingUrl);
        getReq.setRawHeader("x-amz-access-token", token.toUtf8());
        getReq.setRawHeader("accept", "application/json");

        qDebug() << "AmazonCatalogApi: GET listing for image slot detection" << listingUrl.toString();
        QNetworkReply* getReply = _nam()->get(getReq);
        co_await qCoro(getReply).waitForFinished();

        const QByteArray getData = getReply->readAll();
        const int getStatus = getReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        getReply->deleteLater();

        qDebug() << "AmazonCatalogApi: GET listing HTTP" << getStatus
                 << "response:" << QString::fromUtf8(getData.left(300));

        int highestFilledSlot = -1; // 0-based
        if (getStatus == 200) {
            const QJsonObject getDoc = QJsonDocument::fromJson(getData).object();
            const QJsonObject attrs = getDoc.value(QStringLiteral("attributes")).toObject();
            for (int i = 0; i < 8; ++i) {
                const QString attrName = QStringLiteral("other_product_image_locator_%1").arg(i + 1);
                if (!attrs.value(attrName).toArray().isEmpty())
                    highestFilledSlot = i;
            }
        } else {
            qWarning() << "AmazonCatalogApi: failed to GET listing for image slot, defaulting to slot 0";
        }

        if (imageIndex == -1)
            targetIndex = qMin(highestFilledSlot + 1, 7); // append: next after last
        else
            targetIndex = qMax(highestFilledSlot, 0);     // replace last
    }

    // Step 4: PATCH listing with the uploaded image
    const int slotNumber = targetIndex + 1; // 1-based
    const QString attrName = QStringLiteral("other_product_image_locator_%1").arg(slotNumber);
    const QString variantName = QStringLiteral("PT%1").arg(slotNumber, 2, 10, QLatin1Char('0'));

    const QJsonObject imageValue{
        {QStringLiteral("marketplace_id"), marketplaceId},
        {QStringLiteral("images"), QJsonArray{
            QJsonObject{
                {QStringLiteral("link"),    uploadDestinationId},
                {QStringLiteral("variant"), variantName}
            }
        }}
    };

    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/%1").arg(attrName)},
        {QStringLiteral("value"), QJsonArray{imageValue}}
    };

    const QJsonObject patchBodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     QJsonArray{patch}}
    };
    const QByteArray patchBodyBytes = QJsonDocument(patchBodyObj).toJson(QJsonDocument::Compact);

    const QString patchPath = QStringLiteral("/listings/2021-08-01/items/%1/%2")
                                  .arg(sellerId, sku);
    QUrlQuery patchQuery;
    patchQuery.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);

    QUrl patchUrl;
    patchUrl.setScheme(QStringLiteral("https"));
    patchUrl.setHost(endpoint);
    patchUrl.setPath(patchPath);
    patchUrl.setQuery(patchQuery);

    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    QNetworkRequest patchReq(patchUrl);
    patchReq.setRawHeader("x-amz-access-token", token.toUtf8());
    patchReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    patchReq.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: PATCH image" << patchUrl.toString()
             << "slot:" << slotNumber << attrName << variantName
             << "body:" << patchBodyBytes.left(200);
    QNetworkReply* patchReply = _nam()->sendCustomRequest(patchReq, "PATCH", patchBodyBytes);
    co_await qCoro(patchReply).waitForFinished();

    const QByteArray patchData = patchReply->readAll();
    const int patchStatus = patchReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(patchReply, "x-amzn-RequestId");
    const QNetworkReply::NetworkError patchError = patchReply->error();
    patchReply->deleteLater();

    qDebug() << "AmazonCatalogApi: PATCH image HTTP" << patchStatus
             << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
             << "response:" << QString::fromUtf8(patchData.left(300));

    if (patchStatus >= 400 || patchError != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("HTTP %1 patching image for SKU %2: %3")
                          .arg(patchStatus).arg(sku, QString::fromUtf8(patchData.left(300)));
        qWarning() << "AmazonCatalogApi: image PATCH failed for" << sku << ":" << m_lastError;
        const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        const QString diagPath = QStringLiteral("/tmp/sp-api-img-patch-%1-%2.txt").arg(sku, ts);
        QFile f(diagPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "=== REQUEST ===\n"
              << "PATCH " << patchUrl.toString() << "\n\n"
              << "Request body:\n" << QString::fromUtf8(patchBodyBytes) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << patchStatus << "\n\n"
              << QString::fromUtf8(patchData) << "\n";
        }
        co_return;
    }

    *success = true;
    co_return;
}

// ---------------------------------------------------------------------------
// fetchAllFbaSkus — paginated FBA inventory → ASIN-to-SKU map
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchAllFbaSkus(QString marketplaceId,
                                                    QHash<QString, QString>* asinToSku)
{
    asinToSku->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);

    QString nextToken;
    int pageCount = 0;
    static const int kMaxPages = 100;

    do {
        QString token;
        co_await _getAccessToken(lwaReg, &token);
        if (token.isEmpty()) co_return;

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(QStringLiteral("/fba/inventory/v1/summaries"));

        QUrlQuery q;
        q.addQueryItem(QStringLiteral("granularityType"), QStringLiteral("Marketplace"));
        q.addQueryItem(QStringLiteral("granularityId"),   marketplaceId);
        q.addQueryItem(QStringLiteral("marketplaceIds"),  marketplaceId);
        if (!nextToken.isEmpty())
            q.addQueryItem(QStringLiteral("nextToken"), nextToken);
        url.setQuery(q);

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");

        qDebug() << "AmazonCatalogApi: fetchAllFbaSkus page" << (pageCount + 1)
                 << url.toString();
        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (status != 200) {
            qWarning() << "AmazonCatalogApi: fetchAllFbaSkus HTTP" << status
                       << QString::fromUtf8(data.left(300));
            break;
        }

        const QJsonObject root = QJsonDocument::fromJson(data).object();
        const QJsonArray summaries = root.value(QStringLiteral("payload")).toObject()
                                         .value(QStringLiteral("inventorySummaries")).toArray();
        for (const QJsonValue& v : summaries) {
            const QJsonObject obj = v.toObject();
            const QString asin    = obj.value(QStringLiteral("asin")).toString();
            const QString sku     = obj.value(QStringLiteral("sellerSku")).toString();
            if (!asin.isEmpty() && !sku.isEmpty() && !asinToSku->contains(asin))
                asinToSku->insert(asin, sku);
        }

        nextToken = root.value(QStringLiteral("pagination")).toObject()
                        .value(QStringLiteral("nextToken")).toString();
        ++pageCount;

        qDebug() << "AmazonCatalogApi: fetchAllFbaSkus page" << pageCount
                 << "got" << summaries.size() << "items, map size" << asinToSku->size()
                 << (nextToken.isEmpty() ? "(last page)" : "(more pages)");

    } while (!nextToken.isEmpty() && pageCount < kMaxPages);

    qDebug() << "AmazonCatalogApi: fetchAllFbaSkus complete, pages=" << pageCount
             << "total ASINs=" << asinToSku->size();
    co_return;
}

// ---------------------------------------------------------------------------
// fetchAllSkusViaReport — Reports API GET_MERCHANT_LISTINGS_ALL_DATA
// Works for both FBA and MFN listings.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchAllSkusViaReport(QString marketplaceId,
                                                           QHash<QString, QString>* asinToSku)
{
    asinToSku->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);

    // --- Step 1: Create the report ---
    QString token;
    co_await _getAccessToken(lwaReg, &token);
    if (token.isEmpty()) { co_return; }

    {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(QStringLiteral("/reports/2021-06-30/reports"));

        QJsonObject body;
        body[QStringLiteral("reportType")]    = QStringLiteral("GET_MERCHANT_LISTINGS_ALL_DATA");
        body[QStringLiteral("marketplaceIds")] = QJsonArray{ marketplaceId };

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        qDebug() << "AmazonCatalogApi: requesting GET_MERCHANT_LISTINGS_ALL_DATA report for" << marketplaceId;
        QNetworkReply* reply = _nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (status != 202) {
            m_lastError = QStringLiteral("Report creation failed: HTTP %1 — %2")
                          .arg(status).arg(QString::fromUtf8(data.left(400)));
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        const QString reportId = QJsonDocument::fromJson(data).object()
                                     .value(QStringLiteral("reportId")).toString();
        if (reportId.isEmpty()) {
            m_lastError = QStringLiteral("Report creation: no reportId in response — ")
                          + QString::fromUtf8(data.left(300));
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        qDebug() << "AmazonCatalogApi: reportId =" << reportId;

        // --- Step 2: Poll until DONE (max 3 minutes, 5-second intervals) ---
        static const int kMaxPolls   = 36; // 36 × 5 s = 3 min
        static const int kPollMs     = 5000;
        QString reportDocumentId;

        for (int poll = 0; poll < kMaxPolls; ++poll) {
            // Wait 5 seconds between polls (first poll also waits — report is never instant)
            QTimer timer;
            timer.setSingleShot(true);
            timer.start(kPollMs);
            co_await qCoro(&timer).waitForTimeout();

            co_await _getAccessToken(lwaReg, &token);
            if (token.isEmpty()) { co_return; }

            QUrl pollUrl;
            pollUrl.setScheme(QStringLiteral("https"));
            pollUrl.setHost(endpoint);
            pollUrl.setPath(QStringLiteral("/reports/2021-06-30/reports/") + reportId);

            QNetworkRequest pollReq(pollUrl);
            pollReq.setRawHeader("x-amz-access-token", token.toUtf8());
            pollReq.setRawHeader("accept", "application/json");

            QNetworkReply* pollReply = _nam()->get(pollReq);
            co_await qCoro(pollReply).waitForFinished();

            const QByteArray pollData = pollReply->readAll();
            const int pollStatus = pollReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            pollReply->deleteLater();

            if (pollStatus != 200) {
                qWarning() << "AmazonCatalogApi: report poll HTTP" << pollStatus
                           << QString::fromUtf8(pollData.left(200));
                continue;
            }

            const QJsonObject pollObj = QJsonDocument::fromJson(pollData).object();
            const QString procStatus  = pollObj.value(QStringLiteral("processingStatus")).toString();
            qDebug() << "AmazonCatalogApi: report poll" << (poll + 1)
                     << "processingStatus =" << procStatus;

            if (procStatus == QStringLiteral("DONE")) {
                reportDocumentId = pollObj.value(QStringLiteral("reportDocumentId")).toString();
                break;
            }
            if (procStatus == QStringLiteral("FATAL") || procStatus == QStringLiteral("CANCELLED")) {
                m_lastError = QStringLiteral("Report processing failed: ") + procStatus;
                qWarning() << "AmazonCatalogApi:" << m_lastError;
                co_return;
            }
            // IN_QUEUE or IN_PROGRESS — keep polling
        }

        if (reportDocumentId.isEmpty()) {
            m_lastError = QStringLiteral("Report did not complete within 3 minutes");
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        qDebug() << "AmazonCatalogApi: reportDocumentId =" << reportDocumentId;

        // --- Step 3: Get the download URL ---
        co_await _getAccessToken(lwaReg, &token);
        if (token.isEmpty()) { co_return; }

        QUrl docUrl;
        docUrl.setScheme(QStringLiteral("https"));
        docUrl.setHost(endpoint);
        docUrl.setPath(QStringLiteral("/reports/2021-06-30/documents/") + reportDocumentId);

        QNetworkRequest docReq(docUrl);
        docReq.setRawHeader("x-amz-access-token", token.toUtf8());
        docReq.setRawHeader("accept", "application/json");

        QNetworkReply* docReply = _nam()->get(docReq);
        co_await qCoro(docReply).waitForFinished();

        const QByteArray docData = docReply->readAll();
        const int docStatus = docReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        docReply->deleteLater();

        if (docStatus != 200) {
            m_lastError = QStringLiteral("Report document fetch failed: HTTP %1 — %2")
                          .arg(docStatus).arg(QString::fromUtf8(docData.left(300)));
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        const QJsonObject docObj      = QJsonDocument::fromJson(docData).object();
        const QString downloadUrl     = docObj.value(QStringLiteral("url")).toString();
        const QString compression     = docObj.value(QStringLiteral("compressionAlgorithm")).toString();

        if (downloadUrl.isEmpty()) {
            m_lastError = QStringLiteral("Report document: no download URL — ")
                          + QString::fromUtf8(docData.left(300));
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        // --- Step 4: Download the TSV (presigned S3, no auth header needed) ---
        const QUrl dlUrlObj(downloadUrl);
        QNetworkRequest dlReq(dlUrlObj);
        QNetworkReply* dlReply = _nam()->get(dlReq);
        co_await qCoro(dlReply).waitForFinished();

        QByteArray tsvData = dlReply->readAll();
        const int dlStatus = dlReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        dlReply->deleteLater();

        if (dlStatus != 200) {
            m_lastError = QStringLiteral("Report download failed: HTTP %1").arg(dlStatus);
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        if (compression == QStringLiteral("GZIP")) {
            const QByteArray decompressed = gunzip(tsvData);
            if (decompressed.isEmpty()) {
                m_lastError = QStringLiteral("Report decompression failed (gzip inflate error)");
                qWarning() << "AmazonCatalogApi:" << m_lastError;
                co_return;
            }
            tsvData = decompressed;
        }

        // --- Step 5: Parse the TSV ---
        // First line is the header; find seller-sku and asin1 column indices.
        const QString text = QString::fromUtf8(tsvData);
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

        if (lines.isEmpty()) {
            m_lastError = QStringLiteral("Report TSV is empty");
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        const QStringList headers = lines.at(0).split(QLatin1Char('\t'));
        const int skuCol  = headers.indexOf(QStringLiteral("seller-sku"));
        // Report format varies by region/type: some have "asin1", others "product-id"
        int asinCol = headers.indexOf(QStringLiteral("asin1"));
        if (asinCol < 0) asinCol = headers.indexOf(QStringLiteral("product-id"));
        // "product-id-type" column: 1 = ASIN; absent means assume ASIN
        const int typeCol = headers.indexOf(QStringLiteral("product-id-type"));

        if (skuCol < 0 || asinCol < 0) {
            m_lastError = QStringLiteral("Report TSV: could not find seller-sku/asin1 columns. Headers: ")
                          + lines.at(0).left(300);
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
            if (cols.size() <= qMax(skuCol, asinCol)) continue;
            // Skip rows where product-id-type != 1 (ASIN) — e.g. UPC/EAN rows
            if (typeCol >= 0 && typeCol < cols.size()
                    && !cols.at(typeCol).trimmed().isEmpty()
                    && cols.at(typeCol).trimmed() != QStringLiteral("1"))
                continue;
            const QString sku  = cols.at(skuCol).trimmed();
            const QString asin = cols.at(asinCol).trimmed();
            if (!sku.isEmpty() && !asin.isEmpty() && !asinToSku->contains(asin))
                asinToSku->insert(asin, sku);
        }

        qDebug() << "AmazonCatalogApi: fetchAllSkusViaReport complete, total ASINs ="
                 << asinToSku->size();
    }
    co_return;
}

// ---------------------------------------------------------------------------
// fetchListingProductType — GET /listings/2021-08-01/items/{sellerId}/{sku}
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchListingProductType(QString marketplaceId,
                                                             QString sku,
                                                             QString* productType)
{
    productType->clear();

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchListingProductType: no seller ID for" << marketplaceId;
        co_return;
    }

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString path = QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    query.addQueryItem(QStringLiteral("includedData"), QStringLiteral("summaries"));

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(path);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) { co_return; }

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: fetchListingProductType GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus != 200 || data.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchListingProductType: HTTP" << httpStatus
                   << "for SKU" << sku;
        co_return;
    }

    const QJsonArray summaries =
        QJsonDocument::fromJson(data).object().value(QStringLiteral("summaries")).toArray();
    if (summaries.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchListingProductType: no summaries in response for" << sku;
        co_return;
    }

    *productType = summaries.first().toObject().value(QStringLiteral("productType")).toString();
    qDebug() << "AmazonCatalogApi::fetchListingProductType: SKU" << sku
             << "→ productType =" << *productType;
    co_return;
}

// ---------------------------------------------------------------------------
// fetchAsinBySku — GET /listings/2021-08-01/items/{sellerId}/{sku}
// Extracts summaries[0].asin. Falls back across regions if not found.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchAsinBySku(QString marketplaceId,
                                                    QString sku,
                                                    QString* asin)
{
    asin->clear();

    // Build the list of marketplaces to try: preferred first, then EU/NA/JP fallbacks.
    QStringList marketplacesToTry;
    marketplacesToTry << marketplaceId;
    for (const QString &fallback : { QStringLiteral("A1F83G8C2ARO7P"),   // EU/UK
                                      QStringLiteral("ATVPDKIKX0DER"),    // NA/US
                                      QStringLiteral("A1VC38T7YXB528") }) // JP
    {
        if (!marketplacesToTry.contains(fallback))
            marketplacesToTry << fallback;
    }

    for (const QString &mpId : marketplacesToTry) {
        const QString sellerId = sellerIdForMarketplace(mpId);
        if (sellerId.isEmpty()) {
            qDebug() << "AmazonCatalogApi::fetchAsinBySku: no seller ID for"
                     << mpId << "— skipping";
            continue;
        }

        const QString endpoint = endpointForMarketplace(mpId);
        const QString path = QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku);

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("marketplaceIds"), mpId);
        query.addQueryItem(QStringLiteral("includedData"), QStringLiteral("summaries"));

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(path);
        url.setQuery(query);

        QString token;
        co_await _getAccessToken(lwaRegionForMarketplace(mpId), &token);
        if (token.isEmpty()) { continue; }

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");

        qDebug() << "AmazonCatalogApi: fetchAsinBySku GET" << url.toString();
        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (httpStatus != 200 || data.isEmpty()) {
            qDebug() << "AmazonCatalogApi::fetchAsinBySku: HTTP" << httpStatus
                     << "for SKU" << sku << "in" << mpId;
            continue;
        }

        const QJsonArray summaries =
            QJsonDocument::fromJson(data).object().value(QStringLiteral("summaries")).toArray();
        if (summaries.isEmpty()) {
            qDebug() << "AmazonCatalogApi::fetchAsinBySku: no summaries for"
                     << sku << "in" << mpId;
            continue;
        }

        const QString found =
            summaries.first().toObject().value(QStringLiteral("asin")).toString();
        if (!found.isEmpty()) {
            *asin = found;
            qDebug() << "AmazonCatalogApi::fetchAsinBySku: SKU" << sku
                     << "→ ASIN =" << *asin << "(marketplace" << mpId << ")";
            co_return;
        }
    }

    qWarning() << "AmazonCatalogApi::fetchAsinBySku: SKU" << sku
               << "not found in any region";
    co_return;
}

// ---------------------------------------------------------------------------
// fetchSizeChartAttributeName — Product Type Definitions API + S3 schema
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchSizeChartAttributeName(QString marketplaceId,
                                                                  QString productType,
                                                                  QString* attrName)
{
    attrName->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString path = QStringLiteral("/definitions/2020-09-01/productTypes/%1").arg(productType);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    query.addQueryItem(QStringLiteral("requirements"), QStringLiteral("LISTING"));

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(path);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) { co_return; }

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: fetchSizeChartAttributeName GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus != 200 || data.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchSizeChartAttributeName: productTypes HTTP"
                   << httpStatus << "for type" << productType;
        co_return;
    }

    // Extract the S3 schema URL from schema.link.resource
    const QString schemaUrl = QJsonDocument::fromJson(data).object()
        .value(QStringLiteral("schema")).toObject()
        .value(QStringLiteral("link")).toObject()
        .value(QStringLiteral("resource")).toString();

    if (schemaUrl.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchSizeChartAttributeName: no schema URL for"
                   << productType;
        co_return;
    }

    // Download the JSON Schema from S3 (presigned URL — no auth header needed)
    const QUrl schemaUrlObj(schemaUrl);
    QNetworkRequest schemaReq(schemaUrlObj);
    qDebug() << "AmazonCatalogApi: downloading product type schema from S3";
    QNetworkReply* schemaReply = _nam()->get(schemaReq);
    co_await qCoro(schemaReply).waitForFinished();

    const QByteArray schemaData = schemaReply->readAll();
    const int schemaStatus = schemaReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    schemaReply->deleteLater();

    if (schemaStatus != 200 || schemaData.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchSizeChartAttributeName: schema download HTTP"
                   << schemaStatus;
        co_return;
    }

    // Save raw schema to /tmp for manual inspection (one file per marketplace)
    const QString diagPath = QStringLiteral("/tmp/sp-api-schema-%1-%2.json").arg(productType, marketplaceId);
    {
        QFile sf(diagPath);
        if (sf.open(QIODevice::WriteOnly))
            sf.write(schemaData);
    }
    qDebug() << "AmazonCatalogApi: schema for" << productType << "saved to" << diagPath;

    const QJsonObject root = QJsonDocument::fromJson(schemaData).object();

    // Collect the set of attribute property objects to search, trying several
    // schema layouts that Amazon uses (plain, wrapped in allOf, no "attributes" wrapper):
    //   1. properties.attributes.properties   (most common for writable listing attrs)
    //   2. properties                          (some types skip the "attributes" wrapper)
    //   3. allOf[n].properties.attributes.properties
    //   4. allOf[n].properties
    QList<QJsonObject> candidates;

    const QJsonObject topProps = root.value(QStringLiteral("properties")).toObject();
    const QJsonObject attrNode = topProps.value(QStringLiteral("attributes")).toObject();

    // Path 1
    const QJsonObject path1 = attrNode.value(QStringLiteral("properties")).toObject();
    if (!path1.isEmpty()) candidates << path1;

    // Path 2
    if (!topProps.isEmpty()) candidates << topProps;

    // Path 3 + 4 — iterate allOf array
    const QJsonArray allOf = root.value(QStringLiteral("allOf")).toArray();
    for (const QJsonValue &v : allOf) {
        const QJsonObject entry = v.toObject();
        const QJsonObject ep = entry.value(QStringLiteral("properties")).toObject();
        const QJsonObject ea = ep.value(QStringLiteral("attributes")).toObject();
        const QJsonObject eap = ea.value(QStringLiteral("properties")).toObject();
        if (!eap.isEmpty()) candidates << eap;
        if (!ep.isEmpty())  candidates << ep;
    }

    // Helper: find first key containing "size_chart" then "size"+"chart"
    auto searchSizeChart = [](const QJsonObject &props) -> QString {
        for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
            if (it.key().contains(QStringLiteral("size_chart"), Qt::CaseInsensitive))
                return it.key();
        }
        for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
            const QString k = it.key().toLower();
            if (k.contains(QLatin1String("size")) && k.contains(QLatin1String("chart")))
                return it.key();
        }
        return {};
    };

    // Also collect all "size"-related keys for diagnostics
    QStringList sizeKeys;
    for (const QJsonObject &props : candidates) {
        for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
            const QString k = it.key().toLower();
            if (k.contains(QLatin1String("size")) && !sizeKeys.contains(it.key()))
                sizeKeys << it.key();
        }
    }
    qDebug() << "AmazonCatalogApi::fetchSizeChartAttributeName: size-related attrs for"
             << productType << ":" << sizeKeys;

    for (const QJsonObject &props : candidates) {
        const QString found = searchSizeChart(props);
        if (!found.isEmpty()) {
            *attrName = found;
            qDebug() << "AmazonCatalogApi::fetchSizeChartAttributeName: found" << *attrName;
            co_return;
        }
    }

    qWarning() << "AmazonCatalogApi::fetchSizeChartAttributeName: no size chart attr for"
               << productType << "in" << marketplaceId
               << "— schema saved to" << diagPath
               << "— size-related keys:" << sizeKeys;
    co_return;
}
