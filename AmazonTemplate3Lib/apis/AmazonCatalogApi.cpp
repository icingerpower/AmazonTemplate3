// GCC 13 ICE workaround: coroutines with non-trivially-destructible locals in
// the frame trigger a bug in build_special_member_call (cp/call.cc:11096).
// Forcing O1 avoids the affected code path in the coroutine lowering pass.
// In addition, every coroutine in this translation unit returns
// QCoro::Task<void> and communicates its result via an output parameter, to
// avoid co_awaiting a Task<T> whose T is non-trivially destructible (another
// form of the same GCC 13 bug).
#pragma GCC optimize("O1")
#include "AmazonCatalogApi.h"
#include "AmazonMarketplace.h"

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
#include <QRegularExpression>

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
                                   const QString& imgbbApiKey,
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
    , m_imgbbApiKey(imgbbApiKey)
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

    for (int retry = 0; retry < 5; ++retry) {
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

        if (httpStatus == 200) {
            qDebug() << "AmazonCatalogApi: GET" << path << "HTTP" << httpStatus
                     << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
                     << "bytes:" << data.size();
            *out = data;
            reply->deleteLater();
            co_return;
        }

        if (httpStatus == 429 && retry < 4) {
            qDebug() << "AmazonCatalogApi: 429 Quota Exceeded for" << asin << "— retrying in 3s...";
            reply->deleteLater();
            QTimer t;
            t.setSingleShot(true);
            t.start(3000);
            co_await qCoro(&t).waitForTimeout();
            continue;
        }

        // Permanent error or final retry failed
        qWarning() << "AmazonCatalogApi: GET" << url.toString()
                   << "HTTP" << httpStatus
                   << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
                   << "error:" << reply->errorString()
                   << "body:" << QString::fromUtf8(data.left(500));
        writeDiagnosticFile(req, reply, data, asin);
        reply->deleteLater();
        *out = data;

        // On 403, automatically retry via the search endpoint (different code path on Amazon's side)
        if (httpStatus == 403) {
            qDebug() << "AmazonCatalogApi: 403 on item endpoint for" << asin
                     << "- retrying via search endpoint";
            co_await _doSearchFallback(marketplaceId, asin, includedData, out);
        }
        break;
    }
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

// Like firstAttrValue but for enum fields that must be submitted in English (e.g. target_gender,
// age_range_description). Prefers entries with an English language_tag (en_*), then entries with
// no language_tag, then falls back to the first entry.
static QString firstEnglishAttrValue(const QJsonObject& attrs, const QString& key)
{
    const QJsonArray arr = attrs.value(key).toArray();
    if (arr.isEmpty()) return {};
    QString noLangFallback, anyFallback;
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        const QString val  = obj.value(QStringLiteral("value")).toString();
        if (val.isEmpty()) continue;
        const QString lang = obj.value(QStringLiteral("language_tag")).toString();
        if (lang.startsWith(QStringLiteral("en"), Qt::CaseInsensitive))
            return val;
        if (lang.isEmpty() && noLangFallback.isEmpty())
            noLangFallback = val;
        if (anyFallback.isEmpty())
            anyFallback = val;
    }
    return noLangFallback.isEmpty() ? anyFallback : noLangFallback;
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

    // Log the full attributes JSON for debugging SKU/parent resolution issues.
    // We only log it for a few specific ASINs to avoid flooding the console.
    static const QSet<QString> kDebugAsins = {QStringLiteral("B099H27PWY"), QStringLiteral("B09TXXSZ6V")};
    if (kDebugAsins.contains(asin)) {
        qDebug() << "AmazonCatalogApi: full JSON for" << asin << ":"
                 << QJsonDocument(root).toJson(QJsonDocument::Indented);
    }

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
        {"shoe_width",           QStringLiteral("Shoe width")},
    };
    for (const auto& [key, label] : kMatKeys) {
        const QStringList vals = allAttrValues(attrs, key);
        if (!vals.isEmpty())
            item.materialAttrs << label + QStringLiteral(": ") + vals.join(QStringLiteral(", "));
    }

    // Collect one URL per image angle (MAIN, PT01, PT02…). Amazon returns the same
    // angle at several resolutions (75, 300, 500, 2000 px). We keep the resolution
    // closest to 1500 px from above (i.e. ≥1500 preferred, smallest among those).
    // If no entry is ≥1500 px we fall back to the largest available.
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
                // Prefer ≥1500 px; among those, smaller is better (less bandwidth).
                // Among sub-1500 entries, larger is better.
                const bool curGe1500  = (best.size >= 1500);
                const bool newGe1500  = (sz        >= 1500);
                const bool replace =
                    (!curGe1500 && newGe1500)                        // upgrade to ≥1500
                    || (curGe1500 && newGe1500 && sz < best.size)    // both ≥1500, pick smaller
                    || (!curGe1500 && !newGe1500 && sz > best.size); // both <1500, pick larger
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

QCoro::Task<void>
AmazonCatalogApi::fetchChildHealth(QString asin, QString marketplaceId, ChildHealthInfo* out)
{
    out->exists = false;
    out->parentAsin.clear();
    out->size.clear();
    out->imageCount = 0;

    static const QStringList kData =
        QStringList() << "relationships" << "images" << "attributes";
    QByteArray body;
    co_await _doGet(marketplaceId, asin, kData, &body);
    if (body.isEmpty())
        co_return;

    // ASIN is present in this marketplace.
    out->exists = true;

    // Parent ASIN from relationships
    QStringList parentAsins;
    _parseRelationships(body, &parentAsins, nullptr);
    if (!parentAsins.isEmpty())
        out->parentAsin = parentAsins.first();

    const QJsonObject root = QJsonDocument::fromJson(body).object();

    // Size from attributes
    const QJsonObject attrs = root.value(QStringLiteral("attributes")).toObject();
    out->size = firstAttrValue(attrs, QStringLiteral("size"));

    // Count distinct image variants for this ASIN (MAIN, PT01, PT02…)
    const QJsonArray imageSets = root.value(QStringLiteral("images")).toArray();
    QSet<QString> variants;
    for (const QJsonValue &setVal : imageSets) {
        const QJsonObject set = setVal.toObject();
        const QString setAsin = set.value(QStringLiteral("asin")).toString();
        if (!setAsin.isEmpty() && setAsin != asin)
            continue;
        for (const QJsonValue &v : set.value(QStringLiteral("images")).toArray()) {
            const QString variant = v.toObject().value(QStringLiteral("variant")).toString();
            if (!variant.isEmpty())
                variants.insert(variant);
        }
        if (!variants.isEmpty())
            break;
    }
    out->imageCount = static_cast<int>(variants.size());
    co_return;
}

// ---------------------------------------------------------------------------
// fetchCatalogApparelAttrs
// ---------------------------------------------------------------------------
QCoro::Task<void> AmazonCatalogApi::fetchCatalogApparelAttrs(
    QString marketplaceId, QString asin, CatalogApparelAttrs* out)
{
    static const QStringList kData{QStringLiteral("attributes")};
    QByteArray body;
    co_await _doGet(marketplaceId, asin, kData, &body);
    if (body.isEmpty()) co_return;

    const QJsonObject attrs =
        QJsonDocument::fromJson(body).object().value(QStringLiteral("attributes")).toObject();
    out->color      = firstAttrValue(attrs, QStringLiteral("color"));
    // apparel_size_system: catalog API does not expose it reliably (garment_size_country returns
    // country codes like "DE", not valid size-system values like "EU"). Leave empty if absent.
    out->sizeSystem = firstAttrValue(attrs, QStringLiteral("apparel_size_system"));
    // apparel_size_class, apparel_body_type, apparel_height_type: not available in Catalog API.
    out->sizeClass  = firstAttrValue(attrs, QStringLiteral("apparel_size_class"));
    // target_gender and age_range_description may be returned in the local language (e.g.
    // "Erwachsene" instead of "Adult"). Use the English entry when multiple language_tags exist.
    out->gender     = firstEnglishAttrValue(attrs, QStringLiteral("target_gender"));
    out->ageRange   = firstEnglishAttrValue(attrs, QStringLiteral("age_range_description"));
    out->bodyType   = firstAttrValue(attrs, QStringLiteral("apparel_body_type"));
    out->heightType = firstAttrValue(attrs, QStringLiteral("apparel_height_type"));
    co_return;
}

// searchCatalogForApparelAttrs — keyword search for a similar product that has the wanted attrs
// ---------------------------------------------------------------------------
QCoro::Task<void> AmazonCatalogApi::searchCatalogForApparelAttrs(
    QString marketplaceId, const QString &keywords,
    const QStringList &wantedAttrs,
    CatalogApparelAttrs* out, QString* foundAsin)
{
    if (foundAsin) foundAsin->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);

    // No sellerId here: a seller-scoped search only finds their own products,
    // defeating the purpose of finding a similar product in the broader catalog.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"),       keywords);
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    query.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("attributes"));
    query.addQueryItem(QStringLiteral("pageSize"),       QStringLiteral("10"));

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/catalog/2022-04-01/items"));
    url.setQuery(query);

    QNetworkRequest req(url);
    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: searchCatalogForApparelAttrs" << url.toString();
    QNetworkReply *reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus != 200 || data.isEmpty()) {
        qWarning() << "AmazonCatalogApi: searchCatalogForApparelAttrs HTTP" << httpStatus;
        co_return;
    }

    const QJsonArray items =
        QJsonDocument::fromJson(data).object().value(QStringLiteral("items")).toArray();

    qDebug() << "searchCatalogForApparelAttrs: got" << items.size() << "items";
    if (!items.isEmpty()) {
        // Log first item's attributes to verify the key names returned by this API.
        const QJsonObject firstAttrs = items.first().toObject().value(QStringLiteral("attributes")).toObject();
        qDebug() << "searchCatalogForApparelAttrs: first item ASIN"
                 << items.first().toObject().value(QStringLiteral("asin")).toString()
                 << "attrs keys:" << firstAttrs.keys()
                 << "| raw:" << QJsonDocument(firstAttrs).toJson(QJsonDocument::Compact).left(500);
    }

    // Return first item that has at least one of the wanted attrs filled.
    for (const QJsonValue &val : items) {
        const QJsonObject item  = val.toObject();
        const QJsonObject attrs = item.value(QStringLiteral("attributes")).toObject();
        CatalogApparelAttrs candidate;
        candidate.sizeSystem = firstAttrValue(attrs, QStringLiteral("apparel_size_system"));
        candidate.sizeClass  = firstAttrValue(attrs, QStringLiteral("apparel_size_class"));
        candidate.gender     = firstEnglishAttrValue(attrs, QStringLiteral("target_gender"));
        candidate.ageRange   = firstEnglishAttrValue(attrs, QStringLiteral("age_range_description"));
        candidate.bodyType   = firstAttrValue(attrs, QStringLiteral("apparel_body_type"));
        candidate.heightType = firstAttrValue(attrs, QStringLiteral("apparel_height_type"));

        bool hasAny = false;
        if (wantedAttrs.contains(QStringLiteral("apparel_size_system"))   && !candidate.sizeSystem.isEmpty()) hasAny = true;
        if (wantedAttrs.contains(QStringLiteral("apparel_size_class"))    && !candidate.sizeClass.isEmpty())  hasAny = true;
        if (wantedAttrs.contains(QStringLiteral("target_gender"))         && !candidate.gender.isEmpty())     hasAny = true;
        if (wantedAttrs.contains(QStringLiteral("age_range_description")) && !candidate.ageRange.isEmpty())   hasAny = true;
        if (wantedAttrs.contains(QStringLiteral("apparel_body_type"))     && !candidate.bodyType.isEmpty())   hasAny = true;
        if (wantedAttrs.contains(QStringLiteral("apparel_height_type"))   && !candidate.heightType.isEmpty()) hasAny = true;

        if (hasAny) {
            *out = candidate;
            if (foundAsin) *foundAsin = item.value(QStringLiteral("asin")).toString();
            co_return;
        }
    }
    co_return;
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
// fetchItemImages — GET /catalog/2022-04-01/items/{asin}?includedData=images
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::fetchItemImages(QString asin, QString marketplaceId,
                                  QStringList* imageUrls)
{
    imageUrls->clear();

    static const QStringList kImagesData = QStringList() << "images";
    QByteArray body;
    co_await _doGet(marketplaceId, asin, kImagesData, &body);
    if (body.isEmpty())
        co_return;

    const QJsonObject root = QJsonDocument::fromJson(body).object();

    // Same heuristic as parseAsinItem: among images for the same variant, prefer
    // ≥1500 px (and smaller within that bucket); fall back to largest sub-1500.
    struct BestImg { QString url; int size = 0; };
    QMap<QString, BestImg> bestByVariant;
    QStringList variantOrder; // preserves insertion order

    const QJsonArray imageSets = root.value(QStringLiteral("images")).toArray();
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
            const int w  = img.value(QStringLiteral("width")).toInt();
            const int h  = img.value(QStringLiteral("height")).toInt();
            const int sz = qMax(w, h);

            if (!bestByVariant.contains(variant))
                variantOrder << variant;

            BestImg &best = bestByVariant[variant];
            if (best.url.isEmpty()) {
                best = {link, sz};
            } else {
                const bool curGe1500 = (best.size >= 1500);
                const bool newGe1500 = (sz        >= 1500);
                const bool replace =
                    (!curGe1500 && newGe1500)
                    || (curGe1500 && newGe1500 && sz < best.size)
                    || (!curGe1500 && !newGe1500 && sz > best.size);
                if (replace)
                    best = {link, sz};
            }
        }
        if (!bestByVariant.isEmpty())
            break;
    }

    // MAIN first (if present), then PT01, PT02… in original insertion order.
    QStringList ordered;
    if (bestByVariant.contains(QStringLiteral("MAIN")))
        ordered << QStringLiteral("MAIN");
    for (const QString &v : variantOrder)
        if (v != QStringLiteral("MAIN"))
            ordered << v;

    for (const QString &v : ordered)
        *imageUrls << bestByVariant.value(v).url;

    co_return;
}

// ---------------------------------------------------------------------------
// patchListingAsParent — PATCH parentage_level=parent on the parent/virtual listing
// so it has a recognised presence on the marketplace before children link to it.
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::patchListingAsParent(QString marketplaceId, QString parentSku,
                                        QString productType, QString variationTheme,
                                        QString* detailsOut)
{
    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) co_return;

    const QJsonObject parentageValue{
        {QStringLiteral("value"),          QStringLiteral("parent")},
        {QStringLiteral("marketplace_id"), marketplaceId},
    };
    const QJsonObject patchParentage{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/parentage_level")},
        {QStringLiteral("value"), QJsonArray{parentageValue}},
    };

    QJsonArray patches{patchParentage};

    // variation_theme uses the "name" key (enum key like "SIZE/COLOR"), not "value".
    // Required on the parent listing for Amazon to recognise it as a variation parent.
    if (!variationTheme.isEmpty()) {
        const QJsonObject themeValue{
            {QStringLiteral("name"),           variationTheme},
            {QStringLiteral("marketplace_id"), marketplaceId},
        };
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  QStringLiteral("/attributes/variation_theme")},
            {QStringLiteral("value"), QJsonArray{themeValue}},
        });
    }

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     patches},
    };
    const QByteArray jsonBody = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString urlPath  = QStringLiteral("/listings/2021-08-01/items/%1/%2")
                                 .arg(sellerId, parentSku);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(urlPath);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data    = reply->readAll();
    const int httpStatus     = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    const QJsonObject respObj  = QJsonDocument::fromJson(data).object();
    const QString amazonStatus = respObj.value(QStringLiteral("status")).toString();
    const QString submissionId = respObj.value(QStringLiteral("submissionId")).toString();

    if (detailsOut) {
        *detailsOut = QStringLiteral("HTTP %1 | Amazon status: %2 | submissionId: %3")
                          .arg(httpStatus)
                          .arg(amazonStatus.isEmpty() ? QStringLiteral("(none)") : amazonStatus,
                               submissionId.isEmpty() ? QStringLiteral("(none)") : submissionId);
    }

    qDebug() << "AmazonCatalogApi: patchListingAsParent" << parentSku
             << "on" << marketplaceId << "HTTP" << httpStatus << amazonStatus;
    co_return;
}

// patchListingParent — PATCH parentage_level=child + child_parent_sku_relationship
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::patchListingParent(QString marketplaceId, QString childSku,
                                     QString productType, QString parentSku,
                                     QString variationTheme,
                                     QString color, QString size, QString sizeSystem,
                                     const QHash<QString,QString> &extraAttrs,
                                     bool* success, QString* detailsOut)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    // Patch #1: /attributes/parentage_level → "child"
    const QJsonObject parentageValue{
        {QStringLiteral("value"),          QStringLiteral("child")},
        {QStringLiteral("marketplace_id"), marketplaceId},
    };
    const QJsonObject patchParentage{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/parentage_level")},
        {QStringLiteral("value"), QJsonArray{parentageValue}},
    };

    // Patch #2: /attributes/child_parent_sku_relationship (singular) → variation parent.
    // Amazon rejects the plural form child_parent_sku_relationships for most product types;
    // the correct attribute name is the singular child_parent_sku_relationship.
    const QJsonObject relValue{
        {QStringLiteral("child_relationship_type"), QStringLiteral("variation")},
        {QStringLiteral("parent_sku"),              parentSku},
        {QStringLiteral("marketplace_id"),          marketplaceId},
    };
    const QJsonObject patchRel{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/child_parent_sku_relationship")},
        {QStringLiteral("value"), QJsonArray{relValue}},
    };

    QJsonArray patches{patchParentage, patchRel};

    // variation_theme on child uses the "name" key (enum key like "SIZE/COLOR").
    if (!variationTheme.isEmpty()) {
        const QJsonObject themeValue{
            {QStringLiteral("name"),           variationTheme},
            {QStringLiteral("marketplace_id"), marketplaceId},
        };
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  QStringLiteral("/attributes/variation_theme")},
            {QStringLiteral("value"), QJsonArray{themeValue}},
        });
    }

    if (!color.isEmpty()) {
        const QJsonObject cv{{QStringLiteral("value"), color},
                             {QStringLiteral("marketplace_id"), marketplaceId}};
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  QStringLiteral("/attributes/color_name")},
            {QStringLiteral("value"), QJsonArray{cv}},
        });
    }

    if (!size.isEmpty()) {
        const QJsonObject sv{{QStringLiteral("value"), size},
                             {QStringLiteral("marketplace_id"), marketplaceId}};
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  QStringLiteral("/attributes/apparel_size")},
            {QStringLiteral("value"), QJsonArray{sv}},
        });
    }

    if (!sizeSystem.isEmpty()) {
        const QJsonObject ssv{{QStringLiteral("value"), sizeSystem},
                              {QStringLiteral("marketplace_id"), marketplaceId}};
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  QStringLiteral("/attributes/apparel_size_system")},
            {QStringLiteral("value"), QJsonArray{ssv}},
        });
    }

    // Extra apparel attrs (apparel_size_class, target_gender, age_range_description, …)
    static const QHash<QString,QString> kExtraAttrPaths{
        {QStringLiteral("apparel_size_class"),    QStringLiteral("/attributes/apparel_size_class")},
        {QStringLiteral("target_gender"),         QStringLiteral("/attributes/target_gender")},
        {QStringLiteral("age_range_description"), QStringLiteral("/attributes/age_range_description")},
        {QStringLiteral("apparel_body_type"),     QStringLiteral("/attributes/apparel_body_type")},
        {QStringLiteral("apparel_height_type"),   QStringLiteral("/attributes/apparel_height_type")},
    };
    for (auto it = extraAttrs.constBegin(); it != extraAttrs.constEnd(); ++it) {
        if (it.value().isEmpty()) continue;
        const QString attrPath = kExtraAttrPaths.value(it.key());
        if (attrPath.isEmpty()) continue;
        const QJsonObject av{{QStringLiteral("value"), it.value()},
                             {QStringLiteral("marketplace_id"), marketplaceId}};
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  attrPath},
            {QStringLiteral("value"), QJsonArray{av}},
        });
    }

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     patches},
    };
    const QByteArray jsonBody = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString urlPath  = QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, childSku);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(urlPath);
    url.setQuery(query);

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

    qDebug() << "AmazonCatalogApi: PATCH(parent)" << url.toString()
             << "body:" << jsonBody.left(300);
    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data    = reply->readAll();
    const int httpStatus     = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId  = rawHeaderCI(reply, "x-amzn-RequestId");
    const QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "AmazonCatalogApi: PATCH(parent)" << urlPath
             << "HTTP" << httpStatus
             << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
             << "response:" << QString::fromUtf8(data.left(400));

    // Parse Amazon's response body regardless of HTTP status — it contains
    // "status" (ACCEPTED/INVALID), "submissionId", and "issues" array.
    const QJsonObject respObj   = QJsonDocument::fromJson(data).object();
    const QString amazonStatus  = respObj.value(QStringLiteral("status")).toString();
    const QString submissionId  = respObj.value(QStringLiteral("submissionId")).toString();
    const QJsonArray issues     = respObj.value(QStringLiteral("issues")).toArray();

    QStringList issueSummaries;
    for (const QJsonValue &iv : issues) {
        const QJsonObject iobj = iv.toObject();
        const QString sev = iobj.value(QStringLiteral("severity")).toString();
        const QString msg = iobj.value(QStringLiteral("message")).toString();
        if (!msg.isEmpty())
            issueSummaries << QStringLiteral("[%1] %2").arg(sev, msg);
    }

    // Build a human-readable summary for the caller.
    QString details = QStringLiteral("HTTP %1 | Amazon status: %2 | submissionId: %3")
                          .arg(httpStatus)
                          .arg(amazonStatus.isEmpty() ? QStringLiteral("(none)") : amazonStatus,
                               submissionId.isEmpty()  ? QStringLiteral("(none)") : submissionId);
    if (!issueSummaries.isEmpty())
        details += QStringLiteral(" | issues: ") + issueSummaries.join(QStringLiteral(" ; "));
    if (detailsOut) *detailsOut = details;

    if (httpStatus >= 400 || netErr != QNetworkReply::NoError
            || amazonStatus == QStringLiteral("INVALID")) {
        m_lastError = QStringLiteral("HTTP %1 for SKU %2: %3")
                          .arg(httpStatus).arg(childSku, QString::fromUtf8(data.left(300)));
        qWarning() << "AmazonCatalogApi: PATCH(parent) failed for" << childSku << ":" << details;

        const QString ts = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
        const QString diagPath = QStringLiteral("/tmp/sp-api-patch-parent-%1-%2.txt").arg(childSku, ts);
        QFile f(diagPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "=== REQUEST ===\n"
              << "PATCH " << url.toString() << "\n\n"
              << "Request body:\n" << QString::fromUtf8(jsonBody) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << httpStatus << "\n\n"
              << QString::fromUtf8(data) << "\n";
        }
        co_return;
    }

    *success = true;
    co_return;
}

// ---------------------------------------------------------------------------
// fetchListingBrandAndTheme — GET /listings/…/{sku}?includedData=attributes
// Extracts attributes.brand[0].value and attributes.variation_theme[0].value.
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::fetchListingBrandAndTheme(QString marketplaceId,
                                            QString sku,
                                            QString* brand,
                                            QString* variationTheme)
{
    brand->clear();
    variationTheme->clear();

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchListingBrandAndTheme: no seller ID for"
                   << marketplaceId;
        co_return;
    }

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    q.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("attributes"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: fetchListingBrandAndTheme GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus != 200 || data.isEmpty()) {
        qWarning() << "AmazonCatalogApi::fetchListingBrandAndTheme: HTTP" << httpStatus
                   << "for SKU" << sku << QString::fromUtf8(data.left(200));
        co_return;
    }

    const QJsonObject attrs = QJsonDocument::fromJson(data).object()
                                  .value(QStringLiteral("attributes")).toObject();

    const QJsonArray brandArr = attrs.value(QStringLiteral("brand")).toArray();
    if (!brandArr.isEmpty()) {
        *brand = brandArr.first().toObject().value(QStringLiteral("value")).toString();
    }

    const QJsonArray themeArr = attrs.value(QStringLiteral("variation_theme")).toArray();
    if (!themeArr.isEmpty()) {
        *variationTheme = themeArr.first().toObject().value(QStringLiteral("value")).toString();
        // Some marketplaces store the variation_theme under a "name" key instead of "value".
        if (variationTheme->isEmpty()) {
            *variationTheme = themeArr.first().toObject().value(QStringLiteral("name")).toString();
        }
    }

    qDebug() << "AmazonCatalogApi::fetchListingBrandAndTheme: SKU" << sku
             << "→ brand =" << *brand << "| variation_theme =" << *variationTheme;
    co_return;
}

// fetchListingAttributes — GET /listings/2021-08-01/items/{sellerId}/{sku}?includedData=attributes
// Fetches all attributes for a single SKU on a single marketplace.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchListingAttributes(
    QString marketplaceId, QString sku, QJsonObject* attrs)
{
    *attrs = QJsonObject{};
    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) co_return;

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    q.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("attributes"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (status != 200) {
        qWarning() << "fetchListingAttributes:" << sku << "HTTP" << status << data.left(200);
        co_return;
    }
    *attrs = QJsonDocument::fromJson(data).object()
                 .value(QStringLiteral("attributes")).toObject();
}

// ---------------------------------------------------------------------------
// checkListing — GET /listings/…/{sku}?includedData=issues,relationships,summaries
// Diagnostic: exposes Amazon's asynchronous validation issues and the actual
// variation relationships. A PATCH/feed can return ACCEPTED yet fail async
// validation — the failure reasons only ever appear here.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::checkListing(QString marketplaceId, QString sku,
                                                 ListingCheck* out)
{
    *out = ListingCheck{};
    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) co_return;

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    q.addQueryItem(QStringLiteral("includedData"),
                   QStringLiteral("issues,relationships,summaries"));
    q.addQueryItem(QStringLiteral("issueLocale"), QStringLiteral("en_US"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (status == 404) {
        // SKU has no listing on this marketplace — the key insight when a
        // child references a parent SKU that simply doesn't exist there.
        co_return;
    }
    if (status != 200) {
        qWarning() << "checkListing:" << sku << "HTTP" << status << data.left(300);
        out->issues << QStringLiteral("[HTTP %1] %2")
                           .arg(status).arg(QString::fromUtf8(data.left(200)));
        co_return;
    }

    out->exists = true;
    const QJsonObject root = QJsonDocument::fromJson(data).object();

    // summaries → status flags + item name
    const QJsonArray summaries = root.value(QStringLiteral("summaries")).toArray();
    if (!summaries.isEmpty()) {
        const QJsonObject s0 = summaries.first().toObject();
        QStringList statusFlags;
        for (const QJsonValue &sv : s0.value(QStringLiteral("status")).toArray())
            statusFlags << sv.toString();
        out->status      = statusFlags.join(QStringLiteral(","));
        out->itemName    = s0.value(QStringLiteral("itemName")).toString();
        out->productType = s0.value(QStringLiteral("productType")).toString();
    }

    // issues → human-readable list
    for (const QJsonValue &iv : root.value(QStringLiteral("issues")).toArray()) {
        const QJsonObject iobj = iv.toObject();
        QStringList attrNames;
        for (const QJsonValue &av : iobj.value(QStringLiteral("attributeNames")).toArray())
            attrNames << av.toString();
        QString line = QStringLiteral("[%1 %2] %3")
                           .arg(iobj.value(QStringLiteral("severity")).toString(),
                                iobj.value(QStringLiteral("code")).toString(),
                                iobj.value(QStringLiteral("message")).toString());
        if (!attrNames.isEmpty())
            line += QStringLiteral(" (attrs: %1)").arg(attrNames.join(QStringLiteral(", ")));
        out->issues << line;
    }

    // relationships → actual variation family membership
    for (const QJsonValue &rv : root.value(QStringLiteral("relationships")).toArray()) {
        const QJsonObject robj = rv.toObject();
        for (const QJsonValue &relv : robj.value(QStringLiteral("relationships")).toArray()) {
            const QJsonObject rel = relv.toObject();
            if (rel.value(QStringLiteral("type")).toString() != QStringLiteral("VARIATION"))
                continue;
            const QJsonArray parents = rel.value(QStringLiteral("parentSkus")).toArray();
            if (!parents.isEmpty() && out->parentSku.isEmpty())
                out->parentSku = parents.first().toString();
            for (const QJsonValue &cv : rel.value(QStringLiteral("childSkus")).toArray())
                out->childSkus << cv.toString();
            const QJsonObject theme = rel.value(QStringLiteral("variationTheme")).toObject();
            if (out->variationTheme.isEmpty())
                out->variationTheme = theme.value(QStringLiteral("theme")).toString();
        }
    }
    co_return;
}

// ---------------------------------------------------------------------------
// fetchApparelSizeSchemaInfo — Product Type Definitions API schema inspection
// ---------------------------------------------------------------------------

// Depth-first search for the schema node describing property `key` (looks
// inside every "properties" object at any depth).
static QJsonObject findSchemaProperty(const QJsonValue &node, const QString &key)
{
    if (node.isObject()) {
        const QJsonObject o = node.toObject();
        const QJsonObject props = o.value(QStringLiteral("properties")).toObject();
        if (props.contains(key))
            return props.value(key).toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            const QJsonObject r = findSchemaProperty(it.value(), key);
            if (!r.isEmpty()) return r;
        }
    } else if (node.isArray()) {
        for (const QJsonValue &v : node.toArray()) {
            const QJsonObject r = findSchemaProperty(v, key);
            if (!r.isEmpty()) return r;
        }
    }
    return {};
}

// Collects every enum/enumNames pair found anywhere under `node` as
// value → display-name entries.
static void collectEnumPairs(const QJsonValue &node, QMap<QString, QString> *out)
{
    if (node.isObject()) {
        const QJsonObject o = node.toObject();
        const QJsonArray en    = o.value(QStringLiteral("enum")).toArray();
        const QJsonArray names = o.value(QStringLiteral("enumNames")).toArray();
        for (int i = 0; i < en.size(); ++i)
            out->insert(en.at(i).toVariant().toString(),
                        i < names.size() ? names.at(i).toString() : QString());
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.key() == QLatin1String("enum") || it.key() == QLatin1String("enumNames"))
                continue;
            collectEnumPairs(it.value(), out);
        }
    } else if (node.isArray()) {
        for (const QJsonValue &v : node.toArray())
            collectEnumPairs(v, out);
    }
}

QCoro::Task<void> AmazonCatalogApi::_fetchPtSchema(QString marketplaceId,
                                                   QString productType,
                                                   QByteArray* out)
{
    out->clear();
    if (productType.isEmpty()) co_return;

    const QString cacheKey = productType + QLatin1Char(':') + marketplaceId;
    if (m_ptSchemaCache.contains(cacheKey)) {
        *out = m_ptSchemaCache.value(cacheKey);
        co_return;
    }

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    // Step 1 — product type definition → presigned schema URL
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/definitions/2020-09-01/productTypes/%1").arg(productType));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    q.addQueryItem(QStringLiteral("requirements"),   QStringLiteral("LISTING"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();
    const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    if (st != 200) {
        qWarning() << "_fetchPtSchema: definitions HTTP" << st
                   << "for" << productType << ":" << data.left(200);
        co_return;
    }

    const QString schemaUrl = QJsonDocument::fromJson(data).object()
        .value(QStringLiteral("schema")).toObject()
        .value(QStringLiteral("link")).toObject()
        .value(QStringLiteral("resource")).toString();
    if (schemaUrl.isEmpty()) co_return;

    // Step 2 — download the schema (presigned S3 URL, no auth headers)
    QNetworkReply* sReply = _nam()->get(QNetworkRequest(QUrl(schemaUrl)));
    co_await qCoro(sReply).waitForFinished();
    const int sSt = sReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray schemaData = sReply->readAll();
    sReply->deleteLater();
    if (sSt != 200 || schemaData.isEmpty()) {
        qWarning() << "_fetchPtSchema: schema download HTTP" << sSt;
        co_return;
    }
    m_ptSchemaCache.insert(cacheKey, schemaData);
    *out = schemaData;
    co_return;
}

QCoro::Task<void> AmazonCatalogApi::fetchProductTypeSchemaProps(
    QString marketplaceId, QString productType, QSet<QString>* propsOut)
{
    propsOut->clear();
    QByteArray schemaData;
    co_await _fetchPtSchema(marketplaceId, productType, &schemaData);
    if (schemaData.isEmpty()) co_return;

    const QJsonObject root  = QJsonDocument::fromJson(schemaData).object();
    const QJsonObject props = root.value(QStringLiteral("properties")).toObject();
    for (auto it = props.constBegin(); it != props.constEnd(); ++it)
        propsOut->insert(it.key());
    co_return;
}

QCoro::Task<void> AmazonCatalogApi::fetchApparelSizeSchemaInfo(
    QString marketplaceId, QString productType,
    QStringList* sizeSystems, QStringList* sizeClasses, QString* dumpPath)
{
    sizeSystems->clear();
    sizeClasses->clear();
    if (dumpPath) dumpPath->clear();

    QByteArray schemaData;
    co_await _fetchPtSchema(marketplaceId, productType, &schemaData);
    if (schemaData.isEmpty()) co_return;
    const QJsonObject root = QJsonDocument::fromJson(schemaData).object();

    // Dump the apparel_size subtree + full schema for manual inspection.
    const QJsonObject apparelSizeNode = findSchemaProperty(root, QStringLiteral("apparel_size"));
    if (dumpPath) {
        *dumpPath = QStringLiteral("/tmp/sp-api-schema-%1-%2.json").arg(productType, marketplaceId);
        QFile f(*dumpPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("// apparel_size subtree:\n");
            f.write(QJsonDocument(apparelSizeNode).toJson(QJsonDocument::Indented));
            f.write("\n// full schema:\n");
            f.write(schemaData);
        }
    }
    if (apparelSizeNode.isEmpty()) {
        qWarning() << "fetchApparelSizeSchemaInfo: no apparel_size in schema for"
                   << productType << marketplaceId;
        co_return;
    }

    QMap<QString, QString> sysPairs, clsPairs;
    collectEnumPairs(findSchemaProperty(apparelSizeNode, QStringLiteral("size_system")), &sysPairs);
    collectEnumPairs(findSchemaProperty(apparelSizeNode, QStringLiteral("size_class")),  &clsPairs);
    for (auto it = sysPairs.constBegin(); it != sysPairs.constEnd(); ++it)
        *sizeSystems << (it.value().isEmpty() ? it.key()
                                              : QStringLiteral("%1 (%2)").arg(it.key(), it.value()));
    for (auto it = clsPairs.constBegin(); it != clsPairs.constEnd(); ++it)
        *sizeClasses << (it.value().isEmpty() ? it.key()
                                              : QStringLiteral("%1 (%2)").arg(it.key(), it.value()));
    co_return;
}

// ---------------------------------------------------------------------------
// deleteListingAttribute — direct PATCH op=delete on a single listing attribute
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::deleteListingAttribute(
    QString marketplaceId, QString sku, QString productType, QString attribute,
    QJsonArray storedValue, bool* success, QString* detailsOut)
{
    *success = false;
    if (detailsOut) detailsOut->clear();

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) co_return;

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"), QJsonArray{QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("delete")},
            {QStringLiteral("path"),  QStringLiteral("/attributes/") + attribute},
            {QStringLiteral("value"), storedValue},
        }}},
    };
    const QByteArray jsonBody = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    q.addQueryItem(QStringLiteral("issueLocale"),    QStringLiteral("en_US"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: PATCH(delete" << attribute << ")" << sku << jsonBody.left(300);
    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    const QJsonObject respObj  = QJsonDocument::fromJson(data).object();
    const QString amazonStatus = respObj.value(QStringLiteral("status")).toString();
    QStringList issueSummaries;
    for (const QJsonValue &iv : respObj.value(QStringLiteral("issues")).toArray()) {
        const QJsonObject iobj = iv.toObject();
        const QString msg = iobj.value(QStringLiteral("message")).toString();
        if (!msg.isEmpty())
            issueSummaries << QStringLiteral("[%1] %2")
                                  .arg(iobj.value(QStringLiteral("severity")).toString(), msg);
    }
    QString details = QStringLiteral("HTTP %1 | %2").arg(httpStatus)
                          .arg(amazonStatus.isEmpty() ? QStringLiteral("(none)") : amazonStatus);
    if (!issueSummaries.isEmpty())
        details += QStringLiteral(" | ") + issueSummaries.join(QStringLiteral(" ; "));
    if (detailsOut) *detailsOut = details;

    *success = (httpStatus < 400 && amazonStatus != QStringLiteral("INVALID"));
    if (!*success)
        qWarning() << "deleteListingAttribute failed for" << sku << "/" << attribute << ":" << details;
    co_return;
}

// ---------------------------------------------------------------------------
// fetchListingGtin — GET /listings/2021-08-01/items/{sellerId}/{sku}?includedData=attributes
// Reads externally_assigned_product_identifier from the seller's own listing data.
// Tries primary marketplace first, then other same-region markets, then the other region.
// This is the most reliable source when the seller submitted EANs in their flat file.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchListingGtin(QString marketplaceId, QString sku,
                                                      QString* gtin, QString* gtinType,
                                                      QString* diagLog)
{
    gtin->clear();
    gtinType->clear();
    if (diagLog) diagLog->clear();

    static const QStringList kEuMarkets{
        QStringLiteral("A1PA6795UKMFR9"), // DE
        QStringLiteral("A1F83G8C2ARO7P"), // UK
        QStringLiteral("A13V1IB3VIYZZH"), // FR
        QStringLiteral("A1RKKUPIHCS9HS"), // ES
        QStringLiteral("APJ6JRA9NG5V4"),  // IT
    };
    static const QStringList kNaMarkets{
        QStringLiteral("ATVPDKIKX0DER"),  // US
        QStringLiteral("A2EUQ1WTGCTBG2"), // CA
    };
    // EAN preferred, then bare GTIN (Amazon's own type name), then UPC, then GTIN14.
    static const QStringList kPriority{
        QStringLiteral("ean"), QStringLiteral("gtin"),
        QStringLiteral("upc"), QStringLiteral("gtin14"),
    };

    static const QHash<QString, QString> kMpName{
        {QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("DE")},
        {QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("UK")},
        {QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("FR")},
        {QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("ES")},
        {QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("IT")},
        {QStringLiteral("ATVPDKIKX0DER"),  QStringLiteral("US")},
        {QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("CA")},
    };

    const bool primaryIsNa = lwaRegionForMarketplace(marketplaceId) == QLatin1String("NA");
    QStringList marketsToTry;
    marketsToTry << marketplaceId;
    for (const QString &m : (primaryIsNa ? kNaMarkets : kEuMarkets))
        if (m != marketplaceId) marketsToTry << m;
    for (const QString &m : (primaryIsNa ? kEuMarkets : kNaMarkets))
        marketsToTry << m;

    QStringList diagLines;

    for (const QString &mpId : marketsToTry) {
        const QString mpName = kMpName.value(mpId, mpId);
        const QString sellerId = sellerIdForMarketplace(mpId);
        if (sellerId.isEmpty()) {
            diagLines << mpName + QStringLiteral(":no-seller-id");
            continue;
        }

        QString token;
        co_await _getAccessToken(lwaRegionForMarketplace(mpId), &token);
        if (token.isEmpty()) {
            diagLines << mpName + QStringLiteral(":no-token");
            continue;
        }

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpointForMarketplace(mpId));
        url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));

        QUrlQuery q;
        q.addQueryItem(QStringLiteral("marketplaceIds"), mpId);
        q.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("attributes"));
        url.setQuery(q);

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");

        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (httpStatus != 200 || data.isEmpty()) {
            diagLines << mpName + QStringLiteral(":HTTP") + QString::number(httpStatus);
            qDebug() << "fetchListingGtin:" << sku << "HTTP" << httpStatus << "for" << mpId;
            continue;
        }

        const QJsonObject attrs = QJsonDocument::fromJson(data).object()
                                      .value(QStringLiteral("attributes")).toObject();
        const QJsonArray extIds = attrs.value(
            QStringLiteral("externally_assigned_product_identifier")).toArray();

        // Log all attribute keys available — helps diagnose if EAN is stored differently
        const QStringList attrKeys = attrs.keys();
        qDebug() << "fetchListingGtin:" << sku << mpId << "attributes keys:" << attrKeys;

        if (extIds.isEmpty()) {
            diagLines << mpName + QStringLiteral(":200-no-ext-id(attrs:") + attrKeys.join(QLatin1Char(',')) + QLatin1Char(')');
            continue;
        }

        // Amazon listing attributes use several formats for structured sub-objects:
        //   Flat1:  {"type_of_product_id":"ean","product_id":"...","marketplace_id":"..."}
        //   Nested: {"value":{"type_of_product_id":"ean","product_id":"..."},"marketplace_id":"..."}
        //   Flat2:  {"type":"gtin","value":"03666412035794","marketplace_id":"..."}   ← seen in practice
        // Try all three. Log the first entry structure for diagnosis.
        auto extractId = [](const QJsonObject &entry) -> QPair<QString,QString> {
            QString typeId    = entry.value(QStringLiteral("type_of_product_id")).toString();
            QString productId = entry.value(QStringLiteral("product_id")).toString();
            if (typeId.isEmpty()) {
                const QJsonObject nested = entry.value(QStringLiteral("value")).toObject();
                if (!nested.isEmpty()) {
                    typeId    = nested.value(QStringLiteral("type_of_product_id")).toString();
                    productId = nested.value(QStringLiteral("product_id")).toString();
                }
            }
            if (typeId.isEmpty()) {
                // Flat2: "type" + "value" as plain strings
                typeId    = entry.value(QStringLiteral("type")).toString();
                productId = entry.value(QStringLiteral("value")).toString();
            }
            return {typeId, productId};
        };

        // Collect all types present for diagnosis
        QStringList foundTypes;
        for (const QJsonValue &v : extIds) {
            const auto [t, p] = extractId(v.toObject());
            foundTypes << (t.isEmpty() ? QStringLiteral("?") : t);
        }
        // Raw first entry (for diagLog, to expose unknown structure)
        const QString rawFirst = extIds.isEmpty() ? QString{} : QString::fromUtf8(
            QJsonDocument(extIds.first().toObject()).toJson(QJsonDocument::Compact));
        qDebug() << "fetchListingGtin:" << sku << mpId << "first ext-id entry:" << rawFirst;

        for (const QString &wantedType : kPriority) {
            for (const QJsonValue &v : extIds) {
                const auto [typeId, productId] = extractId(v.toObject());
                if (typeId == wantedType && !productId.isEmpty()) {
                    *gtin     = productId;
                    *gtinType = wantedType;
                    qDebug() << "fetchListingGtin:" << sku << "→" << *gtinType << *gtin
                             << "from marketplace" << mpId;
                    if (diagLog) *diagLog = mpName + QStringLiteral(":") + *gtinType;
                    co_return;
                }
            }
        }

        // Include raw first entry only if types are all unknown (helps diagnose new formats)
        const bool allUnknown = std::all_of(foundTypes.begin(), foundTypes.end(),
                                            [](const QString &t){ return t == QLatin1String("?"); });
        const QString suffix = (allUnknown && !rawFirst.isEmpty())
            ? QStringLiteral(" raw=") + rawFirst : QString{};
        diagLines << mpName + QStringLiteral(":200-types(") + foundTypes.join(QLatin1Char(','))
                             + QLatin1Char(')') + suffix;
        qDebug() << "fetchListingGtin:" << sku << "no EAN/UPC/GTIN14 in"
                 << extIds.size() << "identifier(s) from" << mpId
                 << "types:" << foundTypes;
    }

    if (diagLog) *diagLog = QStringLiteral("not found: ") + diagLines.join(QStringLiteral(" | "));
    qWarning() << "fetchListingGtin:" << sku << "no EAN/UPC/GTIN14 in listing attributes (any marketplace):"
               << diagLines.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------------------
// uploadVariationFeed — JSON_LISTINGS_FEED via the Feeds API.
// POST_FLAT_FILE_LISTINGS_DATA was sunset March 31 2025; JSON_LISTINGS_FEED is the replacement.
//
// Three-step flow (Amazon Feeds API 2021-06-30):
//   1. POST /feeds/2021-06-30/documents       → presigned S3 URL + feedDocumentId
//   2. PUT  {presigned S3 URL}                → upload the JSON bytes
//   3. POST /feeds/2021-06-30/feeds           → submit feedId for processing
//   4. GET  /feeds/2021-06-30/feeds/{feedId}  → poll until DONE / FATAL / CANCELLED
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::uploadVariationFeed(QStringList marketplaceIds,
                                       QString productType,
                                       QString variationTheme,
                                       QList<VariationFeedEntry> entries,
                                       QString* resultOut)
{
    resultOut->clear();

    if (entries.isEmpty()) {
        *resultOut = QStringLiteral("ERROR: no entries");
        co_return;
    }

    // -------------------------------------------------------------------
    // Build the JSON_LISTINGS_FEED body
    // -------------------------------------------------------------------
    // One message per SKU — a SKU may appear only once per feed (error 4002010
    // "SKU provided is duplicated" otherwise). The feed's marketplaceIds header
    // (set in createFeed, step 3) determines which marketplaces the updates apply
    // to; no marketplace_id is needed inside the attribute values.

    QJsonArray messages;
    int messageId = 1;
    for (const VariationFeedEntry &e : entries) {
        QJsonArray patches;

        // external_product_id: use GTIN (EAN/UPC) if available, fall back to ASIN.
        const bool hasGtin = !e.gtin.isEmpty() && !e.gtinType.isEmpty();
        if (!hasGtin && !e.asin.isEmpty()) {
            qWarning() << "uploadVariationFeed: no GTIN for SKU" << e.sku
                       << "— falling back to ASIN" << e.asin;
        }
        const QString idValue = hasGtin ? e.gtin    : e.asin;
        const QString idType  = hasGtin ? e.gtinType : QStringLiteral("asin");
        if (!idValue.isEmpty()) {
            patches.append(QJsonObject{
                {QStringLiteral("op"),   QStringLiteral("replace")},
                {QStringLiteral("path"), QStringLiteral("/attributes/externally_assigned_product_identifier")},
                {QStringLiteral("value"), QJsonArray{QJsonObject{
                    {QStringLiteral("type_of_product_id"), idType},
                    {QStringLiteral("product_id"),         idValue},
                }}},
            });
        }

        patches.append(QJsonObject{
            {QStringLiteral("op"),   QStringLiteral("replace")},
            {QStringLiteral("path"), QStringLiteral("/attributes/parentage_level")},
            {QStringLiteral("value"), QJsonArray{QJsonObject{
                {QStringLiteral("value"), e.isParent ? QStringLiteral("parent") : QStringLiteral("child")},
            }}},
        });

        if (!e.isParent && !e.parentSku.isEmpty()) {
            patches.append(QJsonObject{
                {QStringLiteral("op"),   QStringLiteral("replace")},
                {QStringLiteral("path"), QStringLiteral("/attributes/child_parent_sku_relationship")},
                {QStringLiteral("value"), QJsonArray{QJsonObject{
                    {QStringLiteral("child_relationship_type"), QStringLiteral("variation")},
                    {QStringLiteral("parent_sku"),              e.parentSku},
                }}},
            });
        }

        if (!variationTheme.isEmpty()) {
            patches.append(QJsonObject{
                {QStringLiteral("op"),   QStringLiteral("replace")},
                {QStringLiteral("path"), QStringLiteral("/attributes/variation_theme")},
                {QStringLiteral("value"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"), variationTheme},
                }}},
            });
        }

        messages.append(QJsonObject{
            {QStringLiteral("messageId"),     messageId++},
            {QStringLiteral("sku"),           e.sku},
            {QStringLiteral("operationType"), QStringLiteral("PATCH")},
            {QStringLiteral("productType"),   productType},
            {QStringLiteral("patches"),       patches},
        });
    }

    co_await submitJsonListingsFeed(marketplaceIds, messages, resultOut);
    co_return;
}

// ---------------------------------------------------------------------------
// submitJsonListingsFeed — generic JSON_LISTINGS_FEED submission.
// Takes pre-built feed messages; handles document creation, S3 upload, feed
// submission, polling and result-report download/summary.
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::submitJsonListingsFeed(QStringList marketplaceIds,
                                          QJsonArray messages,
                                          QString* resultOut)
{
    resultOut->clear();

    if (marketplaceIds.isEmpty()) {
        *resultOut = QStringLiteral("ERROR: no marketplaceIds");
        co_return;
    }
    if (messages.isEmpty()) {
        *resultOut = QStringLiteral("ERROR: no messages");
        co_return;
    }

    // Region is determined by the first marketplace (all marketplaceIds in a
    // single feed must belong to the same region).
    const QString primaryMpId  = marketplaceIds.first();
    const QString endpointHost = endpointForMarketplace(primaryMpId);
    const QString lwaRegion    = lwaRegionForMarketplace(primaryMpId);
    const QString sellerId     = sellerIdForMarketplace(primaryMpId);

    QString token;
    co_await _getAccessToken(lwaRegion, &token);
    if (token.isEmpty()) {
        *resultOut = QStringLiteral("ERROR: failed to obtain LWA access token");
        co_return;
    }

    const QJsonObject feedBody{
        {QStringLiteral("header"), QJsonObject{
            {QStringLiteral("sellerId"),    sellerId},
            {QStringLiteral("version"),     QStringLiteral("2.0")},
            {QStringLiteral("issueLocale"), QStringLiteral("en_US")},
        }},
        {QStringLiteral("messages"), messages},
    };
    const QByteArray feedBytes = QJsonDocument(feedBody).toJson(QJsonDocument::Compact);

    // Keep a copy of every submitted feed on disk for diagnosis.
    {
        const QString ts = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
        QFile f(QStringLiteral("/tmp/sp-api-feed-body-%1.json").arg(ts));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(feedBody).toJson(QJsonDocument::Indented));
    }

    qDebug() << "AmazonCatalogApi::submitJsonListingsFeed: JSON body (" << feedBytes.size() << "bytes):\n"
             << QString::fromUtf8(feedBytes);

    // 1. Create the feed document (request a presigned upload URL)
    // -------------------------------------------------------------------
    QString feedDocumentId;
    QString uploadUrl;
    {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpointHost);
        url.setPath(QStringLiteral("/feeds/2021-06-30/documents"));

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");
        req.setRawHeader("User-Agent", "AmazonTemplate3/1.0 (Language=C++; Platform=Linux)");
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        // feedType is NOT sent here — only contentType belongs in this body.
        // feedType is declared later in POST /feeds (step 3).
        const QJsonObject body{
            {QStringLiteral("contentType"), QStringLiteral("application/json; charset=UTF-8")},
        };
        const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);

        qDebug() << "AmazonCatalogApi::uploadVariationFeed: POST feedDocuments"
                 << url.toString() << "body:" << bodyBytes;
        QNetworkReply* reply = _nam()->post(req, bodyBytes);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data       = reply->readAll();
        const int httpStatus        = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString requestId     = QString::fromUtf8(reply->rawHeader("x-amzn-RequestId"));
        const QString errorType     = QString::fromUtf8(reply->rawHeader("x-amzn-ErrorType"));
        const QList<QByteArray> responseHeaderNames = reply->rawHeaderList();

        if (httpStatus != 201) {
            // Dump full request + response to /tmp for Amazon support cases.
            const QString dumpPath = QStringLiteral("/tmp/sp-api-feeds-%1.txt")
                                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
            QFile dumpFile(dumpPath);
            if (dumpFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream ts(&dumpFile);
                ts << "=== REQUEST ===\n";
                ts << "POST " << url.toString() << "\n";
                ts << "x-amz-access-token: " << token.left(20) << "...[truncated]\n";
                ts << "accept: application/json\n";
                ts << "User-Agent: AmazonTemplate3/1.0 (Language=C++; Platform=Linux)\n";
                ts << "Content-Type: application/json\n";
                ts << "Body: " << bodyBytes << "\n\n";
                ts << "=== RESPONSE ===\n";
                ts << "HTTP " << httpStatus << "\n";
                for (const QByteArray &name : responseHeaderNames)
                    ts << name << ": " << reply->rawHeader(name) << "\n";
                ts << "\nBody:\n" << data << "\n";
            }
            qWarning() << "AmazonCatalogApi::uploadVariationFeed: dump written to" << dumpPath;

            *resultOut = QStringLiteral(
                "ERROR: createFeedDocument HTTP %1 "
                "(requestId=%2 errorType=%3) — %4\nDump: %5")
                    .arg(httpStatus)
                    .arg(requestId, errorType, QString::fromUtf8(data), dumpPath);
            qWarning() << "AmazonCatalogApi::uploadVariationFeed:" << *resultOut;
            reply->deleteLater();
            co_return;
        }
        reply->deleteLater();

        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        feedDocumentId = obj.value(QStringLiteral("feedDocumentId")).toString();
        uploadUrl      = obj.value(QStringLiteral("url")).toString();

        if (feedDocumentId.isEmpty() || uploadUrl.isEmpty()) {
            *resultOut = QStringLiteral("ERROR: createFeedDocument missing id/url — %1")
                             .arg(QString::fromUtf8(data.left(400)));
            qWarning() << "AmazonCatalogApi::uploadVariationFeed:" << *resultOut;
            co_return;
        }
    }

    // -------------------------------------------------------------------
    // 2. PUT the JSON body to the presigned S3 URL (no auth headers).
    // -------------------------------------------------------------------
    {
        QNetworkRequest s3Req((QUrl(uploadUrl)));
        // Must match exactly the contentType declared in createFeedDocument —
        // the presigned URL has this value baked into its signature.
        s3Req.setHeader(QNetworkRequest::ContentTypeHeader,
                        QStringLiteral("application/json; charset=UTF-8"));

        qDebug() << "AmazonCatalogApi::uploadVariationFeed: PUT to S3 url=" << uploadUrl
                 << " bytes=" << feedBytes.size();
        QNetworkReply* s3Reply = _nam()->put(s3Req, feedBytes);
        co_await qCoro(s3Reply).waitForFinished();

        const QByteArray s3Data = s3Reply->readAll();
        const int s3Status      = s3Reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        s3Reply->deleteLater();

        if (s3Status != 200) {
            *resultOut = QStringLiteral("ERROR: S3 PUT HTTP %1 — %2")
                             .arg(s3Status)
                             .arg(QString::fromUtf8(s3Data.left(400)));
            qWarning() << "AmazonCatalogApi::uploadVariationFeed:" << *resultOut;
            co_return;
        }
    }

    // -------------------------------------------------------------------
    // 3. Submit the feed.
    // -------------------------------------------------------------------
    QString feedId;
    {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpointHost);
        url.setPath(QStringLiteral("/feeds/2021-06-30/feeds"));

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

        QJsonArray mpArr;
        for (const QString &m : marketplaceIds) mpArr.append(m);
        const QJsonObject body{
            {QStringLiteral("feedType"),            QStringLiteral("JSON_LISTINGS_FEED")},
            {QStringLiteral("inputFeedDocumentId"), feedDocumentId},
            {QStringLiteral("marketplaceIds"),      mpArr},
        };
        const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);

        qDebug() << "AmazonCatalogApi::uploadVariationFeed: POST feeds"
                 << url.toString() << "body:" << bodyBytes;
        QNetworkReply* reply = _nam()->post(req, bodyBytes);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (httpStatus != 202) {
            *resultOut = QStringLiteral("ERROR: createFeed HTTP %1 — %2")
                             .arg(httpStatus)
                             .arg(QString::fromUtf8(data.left(400)));
            qWarning() << "AmazonCatalogApi::uploadVariationFeed:" << *resultOut;
            co_return;
        }

        feedId = QJsonDocument::fromJson(data).object()
                     .value(QStringLiteral("feedId")).toString();
        if (feedId.isEmpty()) {
            *resultOut = QStringLiteral("ERROR: createFeed missing feedId — %1")
                             .arg(QString::fromUtf8(data.left(400)));
            qWarning() << "AmazonCatalogApi::uploadVariationFeed:" << *resultOut;
            co_return;
        }
    }

    qDebug() << "AmazonCatalogApi::uploadVariationFeed: feedId =" << feedId
             << "feedDocumentId =" << feedDocumentId;

    // -------------------------------------------------------------------
    // 4. Poll status — 36 × 5 s = 3 min max.
    // -------------------------------------------------------------------
    const int maxPolls    = 36;
    const int pollDelayMs = 5000;
    QString processingStatus;
    QString resultFeedDocumentId;
    QString lastBody;

    for (int i = 0; i < maxPolls; ++i) {
        // Wait first so the feed has time to be registered.
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(pollDelayMs);
        co_await qCoro(&timer).waitForTimeout();

        // Re-fetch the token in case it expired during polling.
        QString pollToken;
        co_await _getAccessToken(lwaRegion, &pollToken);
        if (pollToken.isEmpty()) {
            *resultOut = QStringLiteral("ERROR: token refresh failed during polling");
            co_return;
        }

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpointHost);
        url.setPath(QStringLiteral("/feeds/2021-06-30/feeds/%1").arg(feedId));

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", pollToken.toUtf8());
        req.setRawHeader("accept", "application/json");

        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (httpStatus != 200) {
            qWarning() << "AmazonCatalogApi::uploadVariationFeed: poll HTTP" << httpStatus
                       << QString::fromUtf8(data.left(200));
            continue;
        }

        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        processingStatus      = obj.value(QStringLiteral("processingStatus")).toString();
        resultFeedDocumentId  = obj.value(QStringLiteral("resultFeedDocumentId")).toString();
        lastBody              = QString::fromUtf8(data);

        qDebug() << "AmazonCatalogApi::uploadVariationFeed: poll" << (i + 1) << "/" << maxPolls
                 << "status =" << processingStatus;

        if (processingStatus == QStringLiteral("DONE")
            || processingStatus == QStringLiteral("FATAL")
            || processingStatus == QStringLiteral("CANCELLED"))
        {
            break;
        }
    }

    if (processingStatus != QStringLiteral("DONE")) {
        if (processingStatus == QStringLiteral("FATAL") || processingStatus == QStringLiteral("CANCELLED"))
            *resultOut = QStringLiteral("%1 | feedId: %2 | body: %3").arg(processingStatus, feedId, lastBody.left(400));
        else
            *resultOut = QStringLiteral("TIMEOUT (status=%1) | feedId: %2").arg(processingStatus, feedId);
        co_return;
    }

    // DONE — fetch and summarise the result document so per-message errors are visible.
    if (resultFeedDocumentId.isEmpty()) {
        *resultOut = QStringLiteral("DONE | feedId: %1 | (no result document)").arg(feedId);
        co_return;
    }

    QString resultSummary;
    {
        // GET /feeds/2021-06-30/documents/{feedDocumentId} → presigned download URL
        QString pollToken;
        co_await _getAccessToken(lwaRegion, &pollToken);

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpointHost);
        url.setPath(QStringLiteral("/feeds/2021-06-30/documents/%1").arg(resultFeedDocumentId));

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", pollToken.toUtf8());
        req.setRawHeader("accept", "application/json");

        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();
        const QByteArray docData = reply->readAll();
        reply->deleteLater();

        const QJsonObject docMeta = QJsonDocument::fromJson(docData).object();
        const QString downloadUrl = docMeta.value(QStringLiteral("url")).toString();
        const QString compression = docMeta.value(QStringLiteral("compressionAlgorithm")).toString();

        if (!downloadUrl.isEmpty()) {
            QNetworkReply* dlReply = _nam()->get(QNetworkRequest(QUrl(downloadUrl)));
            co_await qCoro(dlReply).waitForFinished();
            const int dlStatus = dlReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray resultJson = dlReply->readAll();
            dlReply->deleteLater();

            qDebug() << "uploadVariationFeed: result document HTTP" << dlStatus
                     << "size" << resultJson.size()
                     << "compressionAlgorithm:" << (compression.isEmpty() ? "(none)" : compression)
                     << "first bytes:" << resultJson.left(80);

            // Decompress if Amazon stored the result document as GZIP
            if (compression.compare(QStringLiteral("GZIP"), Qt::CaseInsensitive) == 0
                    || (resultJson.size() >= 2
                        && (uchar)resultJson[0] == 0x1F && (uchar)resultJson[1] == 0x8B)) {
                const QByteArray decompressed = gunzip(resultJson);
                if (decompressed.isEmpty()) {
                    qWarning() << "uploadVariationFeed: gunzip failed on result document";
                } else {
                    resultJson = decompressed;
                    qDebug() << "uploadVariationFeed: decompressed to" << resultJson.size() << "bytes";
                }
            }

            if (dlStatus != 200 || resultJson.isEmpty()) {
                qWarning() << "uploadVariationFeed: result document download failed"
                           << "HTTP" << dlStatus << resultJson.left(200);
                resultSummary = QStringLiteral("result-download-failed(HTTP %1)").arg(dlStatus);
            } else {
                const QJsonObject root = QJsonDocument::fromJson(resultJson).object();
                const QJsonObject summary = root.value(QStringLiteral("summary")).toObject();
                const int accepted = summary.value(QStringLiteral("messagesAccepted")).toInt();
                const int invalid  = summary.value(QStringLiteral("messagesInvalid")).toInt();
                const int errors   = summary.value(QStringLiteral("errors")).toInt();

                resultSummary = QStringLiteral("accepted=%1 invalid=%2 errors=%3")
                                    .arg(accepted).arg(invalid).arg(errors);

                // Log first few issues for visibility
                const QJsonArray issues = root.value(QStringLiteral("issues")).toArray();
                for (int i = 0; i < qMin(5, (int)issues.size()); ++i) {
                    const QJsonObject iss = issues.at(i).toObject();
                    qWarning() << "Feed issue:" << iss.value(QStringLiteral("sku")).toString()
                               << iss.value(QStringLiteral("code")).toString()
                               << iss.value(QStringLiteral("message")).toString();
                    resultSummary += QStringLiteral(" | [%1] %2: %3")
                        .arg(iss.value(QStringLiteral("sku")).toString(),
                             iss.value(QStringLiteral("code")).toString(),
                             iss.value(QStringLiteral("message")).toString());
                }
            }
        }
    }

    *resultOut = QStringLiteral("DONE | feedId: %1 | %2").arg(feedId, resultSummary);
    co_return;
}

// ---------------------------------------------------------------------------
// fetchParentSku — GET /listings/…/{childSku}?includedData=relationships
// Virtual parent ASINs never appear in listing reports; the only reliable
// way to get the parent SKU is to read the child listing's relationships.
// ---------------------------------------------------------------------------

// fetchAsinGtin — GET /catalog/2022-04-01/items/{asin}?includedData=identifiers
// Picks EAN > UPC > GTIN14. Tries all marketplaces in the primary region first
// (single call), then falls back to the other region. Sets both out-params to
// empty strings if no non-ASIN identifier is found anywhere.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::fetchAsinGtin(QString asin, QString marketplaceId,
                                                   QString* gtin, QString* gtinType)
{
    gtin->clear();
    gtinType->clear();

    static const QStringList kEuMarkets{
        QStringLiteral("A1F83G8C2ARO7P"), // UK
        QStringLiteral("A1PA6795UKMFR9"), // DE
        QStringLiteral("A13V1IB3VIYZZH"), // FR
        QStringLiteral("A1RKKUPIHCS9HS"), // ES
        QStringLiteral("APJ6JRA9NG5V4"),  // IT
    };
    static const QStringList kNaMarkets{
        QStringLiteral("ATVPDKIKX0DER"),  // US
        QStringLiteral("A2EUQ1WTGCTBG2"), // CA
        QStringLiteral("A1AM78C64UM0Y8"), // MX
    };
    static const QStringList kPriority{
        QStringLiteral("EAN"), QStringLiteral("UPC"), QStringLiteral("GTIN14"),
    };
    static const QHash<QString, QString> kTypeMap{
        {QStringLiteral("EAN"),    QStringLiteral("ean")},
        {QStringLiteral("UPC"),    QStringLiteral("upc")},
        {QStringLiteral("GTIN14"), QStringLiteral("gtin14")},
    };

    // Scans all identifier groups in a response; returns true if a GTIN was found.
    auto parseIdentifiers = [&](const QByteArray &data) -> bool {
        const QJsonArray groups =
            QJsonDocument::fromJson(data).object()
                .value(QStringLiteral("identifiers")).toArray();
        for (const QJsonValue &gv : groups) {
            const QJsonObject grp = gv.toObject();
            const QJsonArray ids = grp.value(QStringLiteral("identifiers")).toArray();
            for (const QString &wantedType : kPriority) {
                for (const QJsonValue &iv : ids) {
                    const QJsonObject id = iv.toObject();
                    if (id.value(QStringLiteral("identifierType")).toString() == wantedType) {
                        *gtin     = id.value(QStringLiteral("identifier")).toString();
                        *gtinType = kTypeMap.value(wantedType);
                        const QString mpFound = grp.value(QStringLiteral("marketplaceId")).toString();
                        qDebug() << "fetchAsinGtin:" << asin << "→" << *gtinType << *gtin
                                 << "from marketplace" << mpFound;
                        return true;
                    }
                }
            }
        }
        return false;
    };

    const bool primaryIsNa = lwaRegionForMarketplace(marketplaceId) == QLatin1String("NA");
    const QStringList& primaryMarkets  = primaryIsNa ? kNaMarkets : kEuMarkets;
    const QStringList& fallbackMarkets = primaryIsNa ? kEuMarkets : kNaMarkets;
    const QString primaryAnchor  = primaryIsNa
        ? QStringLiteral("ATVPDKIKX0DER")  : QStringLiteral("A1PA6795UKMFR9");
    const QString fallbackAnchor = primaryIsNa
        ? QStringLiteral("A1PA6795UKMFR9") : QStringLiteral("ATVPDKIKX0DER");

    // First try: all marketplaces in the primary region (one API call).
    {
        QString token;
        co_await _getAccessToken(lwaRegionForMarketplace(primaryAnchor), &token);
        if (!token.isEmpty()) {
            QUrlQuery query;
            for (const QString &mpId : primaryMarkets)
                query.addQueryItem(QStringLiteral("marketplaceIds"), mpId);
            query.addQueryItem(QStringLiteral("includedData"), QStringLiteral("identifiers"));

            QUrl url;
            url.setScheme(QStringLiteral("https"));
            url.setHost(endpointForMarketplace(primaryAnchor));
            url.setPath(QStringLiteral("/catalog/2022-04-01/items/%1").arg(asin));
            url.setQuery(query);

            QNetworkRequest req(url);
            req.setRawHeader("x-amz-access-token", token.toUtf8());
            req.setRawHeader("accept", "application/json");

            QNetworkReply* reply = _nam()->get(req);
            co_await qCoro(reply).waitForFinished();
            const QByteArray data = reply->readAll();
            reply->deleteLater();

            if (parseIdentifiers(data)) co_return;
        }
    }

    // Second try: the other region.
    {
        QString token;
        co_await _getAccessToken(lwaRegionForMarketplace(fallbackAnchor), &token);
        if (!token.isEmpty()) {
            QUrlQuery query;
            for (const QString &mpId : fallbackMarkets)
                query.addQueryItem(QStringLiteral("marketplaceIds"), mpId);
            query.addQueryItem(QStringLiteral("includedData"), QStringLiteral("identifiers"));

            QUrl url;
            url.setScheme(QStringLiteral("https"));
            url.setHost(endpointForMarketplace(fallbackAnchor));
            url.setPath(QStringLiteral("/catalog/2022-04-01/items/%1").arg(asin));
            url.setQuery(query);

            QNetworkRequest req(url);
            req.setRawHeader("x-amz-access-token", token.toUtf8());
            req.setRawHeader("accept", "application/json");

            QNetworkReply* reply = _nam()->get(req);
            co_await qCoro(reply).waitForFinished();
            const QByteArray data = reply->readAll();
            reply->deleteLater();

            if (parseIdentifiers(data)) co_return;
        }
    }

    qWarning() << "fetchAsinGtin:" << asin << "no EAN/UPC/GTIN14 found in any marketplace";
}

// ---------------------------------------------------------------------------
QCoro::Task<void> AmazonCatalogApi::fetchParentSku(QString marketplaceId,
                                                    QString childSku,
                                                    QString* parentSkuOut,
                                                    QString* rawResponseOut)
{
    parentSkuOut->clear();

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) co_return;

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, childSku));

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    // Request both relationships AND attributes so we have two independent sources:
    // relationships[].relationships[].parentSku  (nested Listings Items API format)
    // attributes.child_parent_sku_relationships[].parent_sku  (always stored on the child)
    q.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("relationships,attributes"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data   = reply->readAll();
    const int httpStatus    = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus != 200) {
        qWarning() << "AmazonCatalogApi: fetchParentSku HTTP" << httpStatus
                   << "for child" << childSku << QString::fromUtf8(data.left(200));
        co_return;
    }

    // The Listings Items API nests relationships two levels deep:
    //   relationships[] → { marketplaceId, relationships[] → { parentSku, type, … } }
    // We try the flat level first (future-proofing), then descend into the inner array.
    const QJsonObject root  = QJsonDocument::fromJson(data).object();
    const QJsonArray  outer = root.value(QStringLiteral("relationships")).toArray();

    qDebug() << "AmazonCatalogApi: fetchParentSku" << childSku
             << "response:" << QString::fromUtf8(data.left(500));

    for (const QJsonValue &outerVal : outer) {
        const QJsonObject outerObj = outerVal.toObject();

        // Flat layout — parentSkus is an array (e.g. ["P-CAFTAN-CJYD2315867"])
        const QJsonArray flatArr = outerObj.value(QStringLiteral("parentSkus")).toArray();
        if (!flatArr.isEmpty()) {
            const QString flat = flatArr.first().toString();
            if (!flat.isEmpty()) {
                *parentSkuOut = flat;
                qDebug() << "AmazonCatalogApi: fetchParentSku" << childSku
                         << "→ parentSku (flat) =" << flat;
                co_return;
            }
        }

        // Nested layout: each outer element has its own "relationships" array
        const QJsonArray inner = outerObj.value(QStringLiteral("relationships")).toArray();
        for (const QJsonValue &innerVal : inner) {
            const QJsonArray nestedArr = innerVal.toObject()
                                            .value(QStringLiteral("parentSkus")).toArray();
            if (!nestedArr.isEmpty()) {
                const QString nested = nestedArr.first().toString();
                if (!nested.isEmpty()) {
                    *parentSkuOut = nested;
                    qDebug() << "AmazonCatalogApi: fetchParentSku" << childSku
                             << "→ parentSku (nested) =" << nested;
                    co_return;
                }
            }
        }
    }

    // Fallback: read child_parent_sku_relationships from the listing's attributes.
    // This attribute is always stored on the child even when the relationship appears
    // broken in some marketplaces, and is independent of the relationships field.
    const QJsonArray relAttr = root.value(QStringLiteral("attributes"))
                                   .toObject()
                                   .value(QStringLiteral("child_parent_sku_relationships"))
                                   .toArray();
    for (const QJsonValue &av : relAttr) {
        const QString attrParentSku = av.toObject()
                                        .value(QStringLiteral("parent_sku")).toString();
        if (!attrParentSku.isEmpty()) {
            *parentSkuOut = attrParentSku;
            qDebug() << "AmazonCatalogApi: fetchParentSku" << childSku
                     << "→ parentSku (from attributes) =" << attrParentSku;
            co_return;
        }
    }

    const QString rawResponse = QString::fromUtf8(data);
    if (rawResponseOut) *rawResponseOut = rawResponse;
    qDebug() << "AmazonCatalogApi: fetchParentSku" << childSku
             << "— no parentSku found. Full response:" << rawResponse;
    co_return;
}

// ---------------------------------------------------------------------------
// patchListingImageUrls — PATCH main + other_product_image_locator_1..8 via CDN
// ---------------------------------------------------------------------------

QCoro::Task<void>
AmazonCatalogApi::patchListingImageUrls(QString marketplaceId, QString sku,
                                        QString productType, QStringList imageUrls,
                                        bool* success)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    if (imageUrls.isEmpty()) {
        m_lastError = QStringLiteral("patchListingImageUrls called with no URLs");
        co_return;
    }

    // Amazon supports main + 8 "other" slots. Anything beyond is clipped.
    const int maxSlots = qMin(imageUrls.size(), 9);

    QJsonArray patches;
    for (int i = 0; i < maxSlots; ++i) {
        const QString &url = imageUrls.at(i);
        if (url.isEmpty()) continue;

        const QString attrPath = (i == 0)
            ? QStringLiteral("/attributes/main_product_image_locator")
            : QStringLiteral("/attributes/other_product_image_locator_%1").arg(i);

        const QJsonObject value{
            {QStringLiteral("media_location"), url},
            {QStringLiteral("marketplace_id"), marketplaceId},
        };
        patches.append(QJsonObject{
            {QStringLiteral("op"),    QStringLiteral("replace")},
            {QStringLiteral("path"),  attrPath},
            {QStringLiteral("value"), QJsonArray{value}},
        });
    }

    if (patches.isEmpty()) {
        m_lastError = QStringLiteral("patchListingImageUrls: all URLs empty");
        co_return;
    }

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     patches},
    };
    const QByteArray jsonBody = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString urlPath  = QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(urlPath);
    url.setQuery(query);

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

    qDebug() << "AmazonCatalogApi: PATCH(images)" << url.toString()
             << "slots:" << patches.size()
             << "body:" << jsonBody.left(400);
    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data   = reply->readAll();
    const int httpStatus    = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    const QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "AmazonCatalogApi: PATCH(images)" << urlPath
             << "HTTP" << httpStatus
             << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
             << "response:" << QString::fromUtf8(data.left(400));

    if (httpStatus >= 400 || netErr != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("HTTP %1 for SKU %2: %3")
                          .arg(httpStatus).arg(sku, QString::fromUtf8(data.left(300)));
        qWarning() << "AmazonCatalogApi: PATCH(images) failed for" << sku << ":" << m_lastError;

        const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        const QString diagPath = QStringLiteral("/tmp/sp-api-patch-images-%1-%2.txt").arg(sku, ts);
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
    QByteArray fbRelBody;
    QStringList fbRelChildren;

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

    // Step 1 fallback: when the primary marketplace returns no relationships, probe
    // FR → UK → US and UNION all results. Amazon partitions variation families
    // differently per storefront — DE may give parent A while US gives parent B, and
    // each may list a different subset of children. We collect every unique parent ASIN
    // so Step 2 can retrieve the complete child set.
    if (parentAsins.isEmpty() && childAsins.isEmpty()) {
        static const QStringList kStep1Fallbacks = {
            QStringLiteral("A13V1IB3VIYZZH"), // FR
            QStringLiteral("A1F83G8C2ARO7P"), // UK
            QStringLiteral("ATVPDKIKX0DER"),  // US
        };
        for (const QString &fbMp : kStep1Fallbacks) {
            if (fbMp == marketplaceId) continue;
            fbRelBody.clear();
            QStringList fbParents, fbChildren;
            co_await _doGet(fbMp, asin, kRelOnly, &fbRelBody);
            _parseRelationships(fbRelBody, &fbParents, &fbChildren);
            for (const QString &p : fbParents)
                if (!parentAsins.contains(p)) parentAsins.append(p);
            for (const QString &c : fbChildren)
                if (!c.isEmpty() && !childAsins.contains(c)) childAsins.append(c);
        }
        if (!parentAsins.isEmpty() || !childAsins.isEmpty())
            qDebug() << "AmazonCatalogApi: step-1 fallback for" << asin
                     << "parentAsins:" << parentAsins
                     << "childAsins count:" << childAsins.size();
    }

    qDebug() << "AmazonCatalogApi: ASIN" << asin
             << "parentAsins:" << parentAsins
             << "childAsins:" << childAsins;

    // Use the first found parent as the canonical parent ASIN for folder naming.
    if (!parentAsins.isEmpty()) {
        parentAsinToUse = parentAsins.first();
    } else {
        parentAsinToUse = asin;
    }
    family.parentAsin = parentAsinToUse;

    // Step 2: collect children for every parent ASIN found (there can be more than one
    // when Amazon uses different parent ASINs on different storefronts for the same
    // logical product family). For each parent, try the primary marketplace first and
    // fall through to FR → UK → US if it returns nothing. Union all results.
    {
        const QStringList parentsToQuery = parentAsins.isEmpty() ? QStringList{asin} : parentAsins;
        static const QStringList kChildFallbacks = {
            QStringLiteral("A13V1IB3VIYZZH"), // FR
            QStringLiteral("A1F83G8C2ARO7P"), // UK
            QStringLiteral("ATVPDKIKX0DER"),  // US
        };
        for (const QString &pa : parentsToQuery) {
            fbRelBody.clear();
            fbRelChildren.clear();
            // If the entered ASIN is itself the parent and we already have its children
            // from Step 1, reuse them directly without an extra round-trip.
            if (pa == asin && !childAsins.isEmpty()) {
                fbRelChildren = childAsins;
            } else {
                co_await _doGet(marketplaceId, pa, kRelOnly, &fbRelBody);
                _parseRelationships(fbRelBody, &unusedParents, &fbRelChildren);
                if (fbRelChildren.isEmpty()) {
                    for (const QString &fbMp : kChildFallbacks) {
                        if (fbMp == marketplaceId) continue;
                        fbRelBody.clear();
                        fbRelChildren.clear();
                        co_await _doGet(fbMp, pa, kRelOnly, &fbRelBody);
                        _parseRelationships(fbRelBody, &unusedParents, &fbRelChildren);
                        if (!fbRelChildren.isEmpty()) {
                            qDebug() << "AmazonCatalogApi: children fallback to" << fbMp
                                     << "for parent" << pa
                                     << "found" << fbRelChildren.size() << "child(ren)";
                            break;
                        }
                    }
                }
            }
            for (const QString &c : fbRelChildren)
                if (!c.isEmpty() && !finalChildAsins.contains(c)) finalChildAsins.append(c);
        }
        if (!finalChildAsins.isEmpty())
            qDebug() << "AmazonCatalogApi: total" << finalChildAsins.size()
                     << "unique child(ren) across" << parentsToQuery.size() << "parent ASIN(s)";
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

    // Step 3c: normalise colour names to English (UK marketplace).
    // Amazon returns the colour attribute in the listing language of the queried
    // marketplace, so FR gives "Blanc et or jaune" while UK gives "White and Yellow Gold".
    // Always override with the UK English name so that element IDs built from colour
    // names are stable across sessions. If the product doesn't exist on UK or the EU
    // token is absent the original name is silently kept.
    static const QString kUkMarket = QStringLiteral("A1F83G8C2ARO7P");
    if (marketplaceId != kUkMarket) {
        for (auto& child : family.children) {
            if (child.color.isEmpty()) continue;
            AsinItem ukItem;
            co_await _fetchAsinItem(child.asin, kUkMarket, &ukItem);
            if (!ukItem.color.isEmpty())
                child.color = ukItem.color;
            if (child.allImageUrls.isEmpty() && !ukItem.allImageUrls.isEmpty()) {
                child.allImageUrls = std::move(ukItem.allImageUrls);
                if (child.mainImageUrl.isEmpty())
                    child.mainImageUrl = ukItem.mainImageUrl;
            }
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

        // If still no images, try other marketplaces — some products only expose
        // images on specific storefronts (e.g. FR or US but not DE/UK).
        if (family.children.first().allImageUrls.isEmpty()) {
            static const QStringList kImageFallbackMarkets = {
                QStringLiteral("A13V1IB3VIYZZH"), // FR
                QStringLiteral("A1PA6795UKMFR9"), // DE
                QStringLiteral("A1F83G8C2ARO7P"), // UK
                QStringLiteral("ATVPDKIKX0DER"),  // US
            };
            for (const QString &fbMp : kImageFallbackMarkets) {
                if (fbMp == marketplaceId) continue;
                AsinItem fbItem;
                co_await _fetchAsinItem(family.parentAsin, fbMp, &fbItem);
                if (!fbItem.allImageUrls.isEmpty()) {
                    qDebug() << "AmazonCatalogApi: found" << fbItem.allImageUrls.size()
                             << "image(s) via fallback marketplace" << fbMp;
                    family.children.first().allImageUrls = std::move(fbItem.allImageUrls);
                    if (family.children.first().mainImageUrl.isEmpty())
                        family.children.first().mainImageUrl = fbItem.mainImageUrl;
                    break;
                }
            }
        }
    }

    // Step 5: standalone product — no children found anywhere. Fetch the ASIN itself
    // as the sole child so the rest of the pipeline (attributesFetched, working dir
    // creation, A+ content, size chart) works without special-casing.
    if (family.children.isEmpty()) {
        qDebug() << "AmazonCatalogApi: no children found for" << asin
                 << "— treating as standalone, fetching own attributes";
        AsinItem self;
        co_await _fetchAsinItem(asin, marketplaceId, &self);
        if (self.asin.isEmpty()) self.asin = asin;
        family.children.append(std::move(self));
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

    if (m_imgbbApiKey.isEmpty()) {
        m_lastError = QStringLiteral("No ImgBB API key configured — cannot host listing image");
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    const QString endpoint = endpointForMarketplace(marketplaceId);

    // Single token variable reused across all SP-API calls in this method
    // (GCC 13 ICE workaround — see #pragma at top of file).
    QString token;

    // ---------------------------------------------------------------
    // Step 1: upload the JPEG to imgbb.com to obtain a public URL
    // ---------------------------------------------------------------
    QUrl imgbbUrl(QStringLiteral("https://api.imgbb.com/1/upload"));
    {
        QUrlQuery imgbbQuery;
        imgbbQuery.addQueryItem(QStringLiteral("key"), m_imgbbApiKey);
        imgbbUrl.setQuery(imgbbQuery);
    }

    // imgbb expects the base64-encoded image in the `image` form field of an
    // application/x-www-form-urlencoded body. The base64 bytes must themselves
    // be percent-encoded so that '+' / '/' / '=' survive the form decoder.
    const QByteArray imgbbBody =
        QByteArray("image=")
        + QUrl::toPercentEncoding(QString::fromLatin1(jpegData.toBase64()));

    QNetworkRequest imgbbReq(imgbbUrl);
    imgbbReq.setHeader(QNetworkRequest::ContentTypeHeader,
                       "application/x-www-form-urlencoded");
    imgbbReq.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: POST imgbb upload (" << jpegData.size()
             << "bytes JPEG,"      << imgbbBody.size() << "bytes form body)";
    QNetworkReply* imgbbReply = _nam()->post(imgbbReq, imgbbBody);
    co_await qCoro(imgbbReply).waitForFinished();

    const QByteArray imgbbData = imgbbReply->readAll();
    const int imgbbStatus = imgbbReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError imgbbError = imgbbReply->error();
    imgbbReply->deleteLater();

    qDebug() << "AmazonCatalogApi: imgbb HTTP" << imgbbStatus
             << "response:" << QString::fromUtf8(imgbbData.left(300));

    if (imgbbStatus != 200 || imgbbError != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("imgbb upload HTTP %1 for SKU %2: %3")
                          .arg(imgbbStatus).arg(sku, QString::fromUtf8(imgbbData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    const QJsonObject imgbbDoc = QJsonDocument::fromJson(imgbbData).object();
    const QJsonObject imgbbDataObj = imgbbDoc.value(QStringLiteral("data")).toObject();
    const QString publicUrl = imgbbDataObj.value(QStringLiteral("url")).toString();
    if (publicUrl.isEmpty()) {
        m_lastError = QStringLiteral("imgbb upload succeeded but no data.url in response for SKU %1: %2")
                          .arg(sku, QString::fromUtf8(imgbbData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }
    qDebug() << "AmazonCatalogApi: imgbb public URL =" << publicUrl;

    // ---------------------------------------------------------------
    // Step 2: detect the target image slot (only if caller didn't pin one).
    //         -1 = append at next empty slot, -2 = replace the last filled.
    // ---------------------------------------------------------------
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
                const QString attrLookupName =
                    QStringLiteral("other_product_image_locator_%1").arg(i + 1);
                if (!attrs.value(attrLookupName).toArray().isEmpty())
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

    // ---------------------------------------------------------------
    // Step 3: PATCH the listing using the Submit-Media format. The stored
    //         listing GET shows the actual schema is { marketplace_id,
    //         media_location } — NOT the legacy { images: [{link, variant}] }
    //         shape that the docs sometimes show.
    // ---------------------------------------------------------------
    const int slotNumber = targetIndex + 1; // 1-based
    const QString attrName = QStringLiteral("other_product_image_locator_%1").arg(slotNumber);

    const QJsonObject imageValue{
        {QStringLiteral("marketplace_id"), marketplaceId},
        {QStringLiteral("media_location"), publicUrl}
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
             << "slot:" << slotNumber << attrName
             << "media_location:" << publicUrl
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
            s << "=== IMGBB UPLOAD ===\n"
              << "Public URL: " << publicUrl << "\n\n"
              << "=== REQUEST ===\n"
              << "PATCH " << patchUrl.toString() << "\n\n"
              << "Request body:\n" << QString::fromUtf8(patchBodyBytes) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << patchStatus << "\n"
              << "x-amzn-RequestId: " << requestId << "\n\n"
              << QString::fromUtf8(patchData) << "\n";
            qDebug() << "AmazonCatalogApi: diagnostic written to" << diagPath;
        }
        co_return;
    }

    // Always write a success diagnostic too, so we can inspect the public URL
    // that was sent if Amazon later rejects the asset asynchronously.
    {
        const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        const QString diagPath = QStringLiteral("/tmp/sp-api-img-patch-%1-%2.txt").arg(sku, ts);
        QFile f(diagPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "=== IMGBB UPLOAD ===\n"
              << "Public URL: " << publicUrl << "\n\n"
              << "=== REQUEST ===\n"
              << "PATCH " << patchUrl.toString() << "\n\n"
              << "Request body:\n" << QString::fromUtf8(patchBodyBytes) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << patchStatus << "\n"
              << "x-amzn-RequestId: " << requestId << "\n\n"
              << QString::fromUtf8(patchData) << "\n";
        }
    }

    *success = true;
    co_return;
}

// ---------------------------------------------------------------------------
// patchListingMainImage — upload JPEG via imgBB, PATCH main_product_image_locator
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonCatalogApi::patchListingMainImage(QString marketplaceId,
                                                          QString sku,
                                                          QString productType,
                                                          QByteArray jpegData,
                                                          bool* success)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    if (m_imgbbApiKey.isEmpty()) {
        m_lastError = QStringLiteral("No ImgBB API key configured — cannot host main product image");
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    const QString endpoint = endpointForMarketplace(marketplaceId);
    QString token;

    // ---------------------------------------------------------------
    // Step 1: upload JPEG to imgBB to obtain a public URL
    // ---------------------------------------------------------------
    QUrl imgbbUrl(QStringLiteral("https://api.imgbb.com/1/upload"));
    {
        QUrlQuery imgbbQuery;
        imgbbQuery.addQueryItem(QStringLiteral("key"), m_imgbbApiKey);
        imgbbUrl.setQuery(imgbbQuery);
    }

    const QByteArray imgbbBody =
        QByteArray("image=")
        + QUrl::toPercentEncoding(QString::fromLatin1(jpegData.toBase64()));

    QNetworkRequest imgbbReq(imgbbUrl);
    imgbbReq.setHeader(QNetworkRequest::ContentTypeHeader,
                       "application/x-www-form-urlencoded");
    imgbbReq.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi: POST imgbb upload (main, " << jpegData.size()
             << "bytes JPEG,"      << imgbbBody.size() << "bytes form body)";
    QNetworkReply* imgbbReply = _nam()->post(imgbbReq, imgbbBody);
    co_await qCoro(imgbbReply).waitForFinished();

    const QByteArray imgbbData = imgbbReply->readAll();
    const int imgbbStatus = imgbbReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError imgbbError = imgbbReply->error();
    imgbbReply->deleteLater();

    qDebug() << "AmazonCatalogApi: imgbb (main) HTTP" << imgbbStatus
             << "response:" << QString::fromUtf8(imgbbData.left(300));

    if (imgbbStatus != 200 || imgbbError != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("imgbb upload HTTP %1 for SKU %2: %3")
                          .arg(imgbbStatus).arg(sku, QString::fromUtf8(imgbbData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }

    const QJsonObject imgbbDoc = QJsonDocument::fromJson(imgbbData).object();
    const QJsonObject imgbbDataObj = imgbbDoc.value(QStringLiteral("data")).toObject();
    const QString publicUrl = imgbbDataObj.value(QStringLiteral("url")).toString();
    if (publicUrl.isEmpty()) {
        m_lastError = QStringLiteral("imgbb upload succeeded but no data.url in response for SKU %1: %2")
                          .arg(sku, QString::fromUtf8(imgbbData.left(300)));
        qWarning() << "AmazonCatalogApi:" << m_lastError;
        co_return;
    }
    qDebug() << "AmazonCatalogApi: imgbb (main) public URL =" << publicUrl;

    // ---------------------------------------------------------------
    // Step 2: PATCH main_product_image_locator with the public URL
    // ---------------------------------------------------------------
    const QJsonObject imageValue{
        {QStringLiteral("marketplace_id"), marketplaceId},
        {QStringLiteral("media_location"), publicUrl}
    };

    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/main_product_image_locator")},
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

    qDebug() << "AmazonCatalogApi: PATCH main image" << patchUrl.toString()
             << "media_location:" << publicUrl
             << "body:" << patchBodyBytes.left(200);
    QNetworkReply* patchReply = _nam()->sendCustomRequest(patchReq, "PATCH", patchBodyBytes);
    co_await qCoro(patchReply).waitForFinished();

    const QByteArray patchData = patchReply->readAll();
    const int patchStatus = patchReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(patchReply, "x-amzn-RequestId");
    const QNetworkReply::NetworkError patchError = patchReply->error();
    patchReply->deleteLater();

    qDebug() << "AmazonCatalogApi: PATCH main image HTTP" << patchStatus
             << "RequestId:" << (requestId.isEmpty() ? QStringLiteral("(none)") : requestId)
             << "response:" << QString::fromUtf8(patchData.left(300));

    if (patchStatus >= 400 || patchError != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("HTTP %1 patching main image for SKU %2: %3")
                          .arg(patchStatus).arg(sku, QString::fromUtf8(patchData.left(300)));
        qWarning() << "AmazonCatalogApi: main image PATCH failed for" << sku << ":" << m_lastError;
        const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
        const QString diagPath = QStringLiteral("/tmp/sp-api-main-img-patch-%1-%2.txt").arg(sku, ts);
        QFile f(diagPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "=== IMGBB UPLOAD ===\n"
              << "Public URL: " << publicUrl << "\n\n"
              << "=== REQUEST ===\n"
              << "PATCH " << patchUrl.toString() << "\n\n"
              << "Request body:\n" << QString::fromUtf8(patchBodyBytes) << "\n\n"
              << "=== RESPONSE ===\n"
              << "HTTP " << patchStatus << "\n"
              << "x-amzn-RequestId: " << requestId << "\n\n"
              << QString::fromUtf8(patchData) << "\n";
            qDebug() << "AmazonCatalogApi: diagnostic written to" << diagPath;
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
                                                           QHash<QString, QString>* asinToSku,
                                                           QHash<QString, int>* asinToInventory,
                                                           QHash<QString, QPair<QString,QString>>* asinToGtin)
{
    asinToSku->clear();
    if (asinToInventory) asinToInventory->clear();
    if (asinToGtin) asinToGtin->clear();

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
        qDebug() << "AmazonCatalogApi: TSV headers found:" << headers;

        const int skuCol  = headers.indexOf(QStringLiteral("seller-sku"));
        // asin1Col: dedicated ASIN column (always the ASIN regardless of product-id-type).
        const int asin1Col = headers.indexOf(QStringLiteral("asin1"));
        // product-id: external identifier (ASIN if product-id-type=1, else EAN/UPC/etc.)
        const int productIdCol = headers.indexOf(QStringLiteral("product-id"));
        // Fall back: if no asin1, use product-id as ASIN source (type must be 1)
        const int asinCol  = (asin1Col >= 0) ? asin1Col : productIdCol;
        // "product-id-type" column: 1=ASIN, 3=UPC, 4=EAN, 5=JAN, 8=GTIN14
        const int typeCol = headers.indexOf(QStringLiteral("product-id-type"));
        const int qtyCol  = headers.indexOf(QStringLiteral("quantity"));

        // product-id-type → SP-API gtinType string (only EAN/UPC/GTIN14 are kept)
        static const QHash<QString, QString> kReportTypeMap{
            {QStringLiteral("3"), QStringLiteral("upc")},
            {QStringLiteral("4"), QStringLiteral("ean")},
            {QStringLiteral("5"), QStringLiteral("ean")},   // JAN = Japanese EAN
            {QStringLiteral("8"), QStringLiteral("gtin14")},
        };

        if (skuCol < 0 || asinCol < 0) {
            m_lastError = QStringLiteral("Report TSV: could not find seller-sku/asin1 columns. Headers: ")
                          + lines.at(0).left(300);
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        int skippedRows = 0;
        int nonAsinRows = 0;
        int gtinRows = 0;
        const QRegularExpression asinRegex("^[A-Z0-9]{10}$");

        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
            if (cols.size() <= qMax(skuCol, asinCol)) {
                skippedRows++;
                continue;
            }

            const QString typeVal = (typeCol >= 0 && typeCol < cols.size())
                ? cols.at(typeCol).trimmed() : QString{};
            const QString sku  = cols.at(skuCol).trimmed();
            const QString asin = cols.at(asinCol).trimmed();

            // product-id-type: 1=ASIN. If empty, check if product-id looks like ASIN.
            bool isAsin = (typeVal == QStringLiteral("1"));
            if (!isAsin && typeVal.isEmpty() && asinRegex.match(asin).hasMatch())
                isAsin = true;
            // If asin1 is a separate column, the asin variable is always the real ASIN.
            if (asin1Col >= 0) isAsin = true;

            if (isAsin) {
                if (!sku.isEmpty() && !asin.isEmpty()) {
                    if (!asinToSku->contains(asin))
                        asinToSku->insert(asin, sku);
                    if (asinToInventory && qtyCol >= 0 && qtyCol < cols.size())
                        (*asinToInventory)[asin] += cols.at(qtyCol).trimmed().toInt();

                    // When asin1 is a dedicated column, product-id may be the external ID (EAN etc.)
                    if (asinToGtin && asin1Col >= 0 && productIdCol >= 0
                            && productIdCol < cols.size()
                            && !asinToGtin->contains(asin)) {
                        const QString gtinVal = cols.at(productIdCol).trimmed();
                        const QString gtinTy  = kReportTypeMap.value(typeVal);
                        if (!gtinTy.isEmpty() && !gtinVal.isEmpty()) {
                            asinToGtin->insert(asin, {gtinVal, gtinTy});
                            ++gtinRows;
                        }
                    }
                } else {
                    skippedRows++;
                }
            } else {
                nonAsinRows++;
                if (asinRegex.match(asin).hasMatch()) {
                    qDebug() << "AmazonCatalogApi: skipped row with ASIN-like product-id"
                             << asin << "because type =" << (typeVal.isEmpty() ? "(empty)" : typeVal);
                }
            }
        }

        qDebug() << "AmazonCatalogApi: fetchAllSkusViaReport complete."
                 << "Total lines:" << lines.size()
                 << "Extracted ASINs:" << asinToSku->size()
                 << "GTINs from report:" << gtinRows
                 << "Skipped rows:" << skippedRows
                 << "Non-ASIN rows:" << nonAsinRows;
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

// ---------------------------------------------------------------------------
// fetchListingAttributes — public single-SKU wrapper
// ---------------------------------------------------------------------------
QCoro::Task<void> AmazonCatalogApi::fetchListingAttributes(
    QString marketplaceId, QString sku, StoreItem* item, QStringList allMarketplaceIds)
{
    co_await _fetchListingAttributesFull(std::move(marketplaceId), std::move(sku), item,
                                         std::move(allMarketplaceIds));
}

// ---------------------------------------------------------------------------
// _fetchListingAttributesFull — GET /listings/{sku}?includedData=summaries,attributes
// Fills title (itemName from summaries), category (productType from summaries),
// brand (attributes.brand[0].value), gender (attributes.target_gender[0].value),
// age (attributes.age_range_description[0].value).
// If allMarketplaceIds is non-empty, passes all IDs in one query and populates
// item->existsInMarketplaces with the set of marketplaces where the SKU is found.
// ---------------------------------------------------------------------------
QCoro::Task<void> AmazonCatalogApi::_fetchListingAttributesFull(
    QString marketplaceId, QString sku, StoreItem* item, QStringList allMarketplaceIds)
{
    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) co_return;

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) co_return;

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpointForMarketplace(marketplaceId));
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));

    QUrlQuery q;
    // Use all same-region marketplace IDs when provided (single call, multi-marketplace existence)
    const QString mpIdsParam = allMarketplaceIds.isEmpty()
        ? marketplaceId
        : allMarketplaceIds.join(QLatin1Char(','));
    q.addQueryItem(QStringLiteral("marketplaceIds"), mpIdsParam);
    q.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("summaries,attributes"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonCatalogApi::_fetchListingAttributesFull GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data   = reply->readAll();
    const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (status != 200 || data.isEmpty()) {
        qWarning() << "AmazonCatalogApi::_fetchListingAttributesFull: HTTP"
                   << status << "for SKU" << sku;
        co_return;
    }

    const QJsonObject root = QJsonDocument::fromJson(data).object();

    // summaries — one entry per marketplace where the SKU is listed.
    // If marketplaceId field is present, use it to track existence per marketplace.
    // Use the primary marketplace's summary for title/category/mainImage/createdDate;
    // fall back to the first available summary if the primary isn't found.
    const QJsonArray summaries = root.value(QStringLiteral("summaries")).toArray();
    const QJsonObject primarySummary = [&]() -> QJsonObject {
        QJsonObject first;
        for (const QJsonValue &sv : summaries) {
            const QJsonObject s = sv.toObject();
            const QString mpId = s.value(QStringLiteral("marketplaceId")).toString();
            if (!mpId.isEmpty())
                item->existsInMarketplaces.insert(mpId);
            if (first.isEmpty())
                first = s;
            if (mpId == marketplaceId)
                return s;
        }
        return first; // primary not found — use first available
    }();

    if (!primarySummary.isEmpty()) {
        item->title    = primarySummary.value(QStringLiteral("itemName")).toString();
        item->category = primarySummary.value(QStringLiteral("productType")).toString();
        const QJsonObject img = primarySummary.value(QStringLiteral("mainImage")).toObject();
        item->mainImageUrl = img.value(QStringLiteral("link")).toString();
        const QString dateStr = primarySummary.value(QStringLiteral("createdDate")).toString();
        if (!dateStr.isEmpty())
            item->createdDate = QDate::fromString(dateStr.left(10), QStringLiteral("yyyy-MM-dd"));
    }

    // If no marketplaceId fields were present in summaries (single-marketplace response),
    // at least mark the queried marketplace as existing.
    if (item->existsInMarketplaces.isEmpty() && !summaries.isEmpty())
        item->existsInMarketplaces.insert(marketplaceId);

    // attributes → brand, target_gender, age_range_description, color, size
    const QJsonObject attrs = root.value(QStringLiteral("attributes")).toObject();

    auto firstValue = [&](const QString& key) -> QString {
        const QJsonArray arr = attrs.value(key).toArray();
        return arr.isEmpty() ? QString{}
                             : arr.first().toObject().value(QStringLiteral("value")).toString();
    };

    item->isParent = (firstValue(QStringLiteral("parentage_level")) == QStringLiteral("parent"));

    item->brand  = firstValue(QStringLiteral("brand"));
    item->gender = firstEnglishAttrValue(attrs, QStringLiteral("target_gender"));
    item->age    = firstEnglishAttrValue(attrs, QStringLiteral("age_range_description"));

    item->color = firstValue(QStringLiteral("color_name"));
    if (item->color.isEmpty())
        item->color = firstValue(QStringLiteral("color"));

    item->sizeValue = firstValue(QStringLiteral("size"));
    if (item->sizeValue.isEmpty())
        item->sizeValue = firstValue(QStringLiteral("size_name"));


    qDebug() << "AmazonCatalogApi::_fetchListingAttributesFull: SKU" << sku
             << "→ brand=" << item->brand << "color=" << item->color
             << "size=" << item->sizeValue
             << "exists in" << item->existsInMarketplaces.size() << "marketplace(s)";
    co_return;
}

// ---------------------------------------------------------------------------
// fetchSalesReport — Reports API GET_FLAT_FILE_ALL_ORDERS_DATA_BY_ORDER_DATE_GENERAL
// Aggregates units shipped per ASIN across all given marketplace IDs.
// ---------------------------------------------------------------------------
QCoro::Task<void> AmazonCatalogApi::fetchSalesReport(
    QStringList marketplaceIds,
    QDateTime dataStartTime,
    QDateTime dataEndTime,
    QHash<QString, QHash<QString,int>>* mpToAsinUnits)
{
    mpToAsinUnits->clear();
    if (marketplaceIds.isEmpty()) co_return;

    const QString primaryId = marketplaceIds.first();
    const QString endpoint  = endpointForMarketplace(primaryId);
    const QString lwaReg    = lwaRegionForMarketplace(primaryId);

    QString token;
    co_await _getAccessToken(lwaReg, &token);
    if (token.isEmpty()) co_return;

    // --- Step 1: Create the report ---
    {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(QStringLiteral("/reports/2021-06-30/reports"));

        QJsonArray mpArray;
        for (const QString &mpId : marketplaceIds)
            mpArray.append(mpId);

        QJsonObject body;
        body[QStringLiteral("reportType")]    = QStringLiteral("GET_FLAT_FILE_ALL_ORDERS_DATA_BY_ORDER_DATE_GENERAL");
        body[QStringLiteral("marketplaceIds")] = mpArray;
        body[QStringLiteral("dataStartTime")]  = dataStartTime.toString(Qt::ISODate);
        body[QStringLiteral("dataEndTime")]    = dataEndTime.toString(Qt::ISODate);

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        qDebug() << "AmazonCatalogApi: requesting sales report for" << marketplaceIds;
        QNetworkReply* reply = _nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        co_await qCoro(reply).waitForFinished();

        const QByteArray data   = reply->readAll();
        const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (status != 202) {
            m_lastError = QStringLiteral("Sales report creation failed: HTTP %1 — %2")
                          .arg(status).arg(QString::fromUtf8(data.left(400)));
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        const QString reportId = QJsonDocument::fromJson(data).object()
                                     .value(QStringLiteral("reportId")).toString();
        if (reportId.isEmpty()) {
            m_lastError = QStringLiteral("Sales report: no reportId — ") + QString::fromUtf8(data.left(300));
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        // --- Step 2: Poll until DONE ---
        static const int kMaxPolls = 36;
        static const int kPollMs   = 5000;
        QString reportDocumentId;

        for (int poll = 0; poll < kMaxPolls; ++poll) {
            QTimer timer;
            timer.setSingleShot(true);
            timer.start(kPollMs);
            co_await qCoro(&timer).waitForTimeout();

            co_await _getAccessToken(lwaReg, &token);
            if (token.isEmpty()) co_return;

            QUrl pollUrl;
            pollUrl.setScheme(QStringLiteral("https"));
            pollUrl.setHost(endpoint);
            pollUrl.setPath(QStringLiteral("/reports/2021-06-30/reports/") + reportId);

            QNetworkRequest pollReq(pollUrl);
            pollReq.setRawHeader("x-amz-access-token", token.toUtf8());
            pollReq.setRawHeader("accept", "application/json");

            QNetworkReply* pollReply = _nam()->get(pollReq);
            co_await qCoro(pollReply).waitForFinished();

            const QByteArray pollData   = pollReply->readAll();
            const int        pollStatus = pollReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            pollReply->deleteLater();

            if (pollStatus != 200) { continue; }

            const QJsonObject pollObj  = QJsonDocument::fromJson(pollData).object();
            const QString     procStat = pollObj.value(QStringLiteral("processingStatus")).toString();
            qDebug() << "AmazonCatalogApi: sales report poll" << (poll + 1) << procStat;

            if (procStat == QStringLiteral("DONE")) {
                reportDocumentId = pollObj.value(QStringLiteral("reportDocumentId")).toString();
                break;
            }
            if (procStat == QStringLiteral("FATAL") || procStat == QStringLiteral("CANCELLED")) {
                m_lastError = QStringLiteral("Sales report failed: ") + procStat;
                qWarning() << "AmazonCatalogApi:" << m_lastError;
                co_return;
            }
        }

        if (reportDocumentId.isEmpty()) {
            m_lastError = QStringLiteral("Sales report did not complete within 3 minutes");
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        // --- Step 3: Get download URL ---
        co_await _getAccessToken(lwaReg, &token);
        if (token.isEmpty()) co_return;

        QUrl docUrl;
        docUrl.setScheme(QStringLiteral("https"));
        docUrl.setHost(endpoint);
        docUrl.setPath(QStringLiteral("/reports/2021-06-30/documents/") + reportDocumentId);

        QNetworkRequest docReq(docUrl);
        docReq.setRawHeader("x-amz-access-token", token.toUtf8());
        docReq.setRawHeader("accept", "application/json");

        QNetworkReply* docReply = _nam()->get(docReq);
        co_await qCoro(docReply).waitForFinished();

        const QByteArray docData   = docReply->readAll();
        const int        docStatus = docReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        docReply->deleteLater();

        if (docStatus != 200) {
            m_lastError = QStringLiteral("Sales report document fetch failed: HTTP %1").arg(docStatus);
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        const QJsonObject docObj      = QJsonDocument::fromJson(docData).object();
        const QString     downloadUrl = docObj.value(QStringLiteral("url")).toString();
        const QString     compression = docObj.value(QStringLiteral("compressionAlgorithm")).toString();

        if (downloadUrl.isEmpty()) {
            m_lastError = QStringLiteral("Sales report: no download URL");
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        // --- Step 4: Download ---
        QNetworkReply* dlReply = _nam()->get(QNetworkRequest{QUrl(downloadUrl)});
        co_await qCoro(dlReply).waitForFinished();

        QByteArray tsvData  = dlReply->readAll();
        const int  dlStatus = dlReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        dlReply->deleteLater();

        if (dlStatus != 200) {
            m_lastError = QStringLiteral("Sales report download failed: HTTP %1").arg(dlStatus);
            qWarning() << "AmazonCatalogApi:" << m_lastError;
            co_return;
        }

        if (compression == QStringLiteral("GZIP")) {
            const QByteArray decompressed = gunzip(tsvData);
            if (decompressed.isEmpty()) {
                m_lastError = QStringLiteral("Sales report decompression failed");
                qWarning() << "AmazonCatalogApi:" << m_lastError;
                co_return;
            }
            tsvData = decompressed;
        }

        // --- Step 5: Parse TSV ---
        const QString     text    = QString::fromUtf8(tsvData);
        const QStringList lines   = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty()) co_return;

        // Build sales-channel → marketplaceId map from AmazonMarketplace::all()
        QHash<QString, QString> channelToMp;
        for (const AmazonMarketplace &mp : AmazonMarketplace::all())
            channelToMp.insert(mp.salesChannelName(), mp.marketplaceId());

        const QStringList headers  = lines.at(0).split(QLatin1Char('\t'));
        const int asinCol    = headers.indexOf(QStringLiteral("asin"));
        const int qtyCol     = headers.indexOf(QStringLiteral("quantity"));
        const int statusCol  = headers.indexOf(QStringLiteral("order-status"));
        const int channelCol = headers.indexOf(QStringLiteral("sales-channel"));

        if (asinCol < 0 || qtyCol < 0) {
            qWarning() << "AmazonCatalogApi: sales report TSV missing columns. Headers:" << headers;
            co_return;
        }

        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
            if (cols.size() <= qMax(asinCol, qtyCol)) continue;

            if (statusCol >= 0 && statusCol < cols.size()) {
                const QString status = cols.at(statusCol).trimmed();
                if (status.compare(QStringLiteral("Canceled"),  Qt::CaseInsensitive) == 0
                 || status.compare(QStringLiteral("Cancelled"), Qt::CaseInsensitive) == 0
                 || status.compare(QStringLiteral("Pending"),   Qt::CaseInsensitive) == 0)
                    continue;
            }

            const QString asin = cols.at(asinCol).trimmed();
            const int     qty  = cols.at(qtyCol).trimmed().toInt();
            if (asin.isEmpty() || qty <= 0) continue;

            QString mpId;
            if (channelCol >= 0 && channelCol < cols.size())
                mpId = channelToMp.value(cols.at(channelCol).trimmed());

            if (!mpId.isEmpty())
                (*mpToAsinUnits)[mpId][asin] += qty;
        }

        qDebug() << "AmazonCatalogApi: sales report parsed."
                 << "Marketplaces with data:" << mpToAsinUnits->size();
    }
    co_return;
}
