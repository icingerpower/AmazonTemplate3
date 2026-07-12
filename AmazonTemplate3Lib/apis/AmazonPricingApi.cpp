// GCC 13 ICE workaround: coroutines with non-trivially-destructible frame locals
// miscompile at -O2/-O3.
#pragma GCC optimize("O1")
#include "AmazonPricingApi.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QtMath>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroTimer>

// ---------------------------------------------------------------------------
// Routing helpers
// ---------------------------------------------------------------------------

QString AmazonPricingApi::endpointForMarketplace(const QString &marketplaceId)
{
    static const QStringList naIds = {
        "ATVPDKIKX0ER", "ATVPDKIKX0DER",
        "A2EUQ1WTGCTBG2", "A1AM78C64UM0Y8", "A2Q3Y263D00KWC",
    };
    return naIds.contains(marketplaceId)
        ? QStringLiteral("sellingpartnerapi-na.amazon.com")
        : QStringLiteral("sellingpartnerapi-eu.amazon.com");
}

QString AmazonPricingApi::lwaRegionForMarketplace(const QString &marketplaceId)
{
    static const QStringList naIds = {
        "ATVPDKIKX0ER", "ATVPDKIKX0DER",
        "A2EUQ1WTGCTBG2", "A1AM78C64UM0Y8", "A2Q3Y263D00KWC",
    };
    return naIds.contains(marketplaceId) ? QStringLiteral("NA") : QStringLiteral("EU");
}

QString AmazonPricingApi::sellerIdForMarketplace(const QString &marketplaceId) const
{
    return (lwaRegionForMarketplace(marketplaceId) == "NA") ? m_sellerIdNa : m_sellerIdEu;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AmazonPricingApi::AmazonPricingApi(const QString &lwaClientId,
                                   const QString &lwaClientSecret,
                                   const QString &lwaRefreshTokenEu,
                                   const QString &lwaRefreshTokenNa,
                                   const QString &sellerIdEu,
                                   const QString &sellerIdNa,
                                   QObject *parent)
    : QObject(parent)
    , m_lwaClientId(lwaClientId)
    , m_lwaClientSecret(lwaClientSecret)
    , m_lwaRefreshTokenEu(lwaRefreshTokenEu)
    , m_lwaRefreshTokenNa(lwaRefreshTokenNa)
    , m_sellerIdEu(sellerIdEu)
    , m_sellerIdNa(sellerIdNa)
{
}

QNetworkAccessManager *AmazonPricingApi::_nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(30'000);
    }
    return m_nam;
}

// ---------------------------------------------------------------------------
// LWA token (EU/NA dual-region, same pattern as AmazonWarningsApi)
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonPricingApi::_getAccessToken(QString lwaRegion, QString *out)
{
    const bool isNa = (lwaRegion == "NA");
    QString   *const pToken   = isNa ? &m_accessTokenNa   : &m_accessTokenEu;
    QDateTime *const pExpiry  = isNa ? &m_accessTokenExpiryNa : &m_accessTokenExpiryEu;
    const QString *const pRefresh = isNa ? &m_lwaRefreshTokenNa : &m_lwaRefreshTokenEu;

    if (!pToken->isEmpty() && pExpiry->isValid()
        && QDateTime::currentDateTimeUtc() < *pExpiry) {
        *out = *pToken;
        co_return;
    }

    if (pRefresh->isEmpty()) {
        qDebug() << "AmazonPricingApi: no refresh token for region" << lwaRegion;
        co_return;
    }

    QUrl lwaUrl(QStringLiteral("https://api.amazon.com/auth/o2/token"));
    QNetworkRequest req(lwaUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("grant_type",    "refresh_token");
    body.addQueryItem("refresh_token", *pRefresh);
    body.addQueryItem("client_id",     m_lwaClientId);
    body.addQueryItem("client_secret", m_lwaClientSecret);
    const QByteArray payload = body.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply *reply = _nam()->post(req, payload);
    co_await qCoro(reply).waitForFinished();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const QJsonObject obj = QJsonDocument::fromJson(data).object();
    *pToken = obj.value("access_token").toString();
    if (pToken->isEmpty()) {
        const QString errCode = obj.value("error").toString();
        const QString errDesc = obj.value("error_description").toString();
        m_lastError = errDesc.isEmpty() ? errCode : errDesc;
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("LWA token exchange failed");
        qWarning() << "AmazonPricingApi: LWA failed for" << lwaRegion
                   << ":" << m_lastError << QString::fromUtf8(data.left(400));
    }
    const int expiresIn = obj.value("expires_in").toInt(3600);
    const int cacheSecs = qMin(expiresIn - 300, 55 * 60);
    *pExpiry = QDateTime::currentDateTimeUtc().addSecs(qMax(cacheSecs, 60));
    *out = *pToken;
}

// ---------------------------------------------------------------------------
// Rate limiter (shared between fetch and patch)
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonPricingApi::_rateLimit()
{
    const qint64 elapsedMs = m_lastRequestTime.isValid()
        ? m_lastRequestTime.msecsTo(QDateTime::currentDateTimeUtc()) : 9999;
    if (elapsedMs < 300) {
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(static_cast<int>(300 - elapsedMs));
        co_await qCoro(timer).waitForTimeout();
    }
    m_lastRequestTime = QDateTime::currentDateTimeUtc();
}

// ---------------------------------------------------------------------------
// fetchListingPrice
// ---------------------------------------------------------------------------

// Parse price and product type from Listings Items API JSON body.
// Price path priority:
//   1. offers[i].price.listingPrice.amount
//   2. offers[i].listingPrice.amount
//   3. attributes.purchasable_offer[j].our_price[0].schedule[0].value_with_tax
//   4. attributes.list_price[0].value
static double parsePriceFromBody(const QByteArray &json, const QString &marketplaceId,
                                  QString *productTypeOut)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();

    // Product type from summaries
    if (productTypeOut) {
        for (const QJsonValue &sv : root.value("summaries").toArray()) {
            const QJsonObject s = sv.toObject();
            if (s.value("marketplaceId").toString() == marketplaceId) {
                *productTypeOut = s.value("productType").toString();
                break;
            }
        }
    }

    // 1 & 2: offers array
    for (const QJsonValue &v : root.value("offers").toArray()) {
        const QJsonObject offer = v.toObject();
        if (offer.value("marketplaceId").toString() != marketplaceId)
            continue;
        {
            const double amt = offer.value("price").toObject()
                                   .value("listingPrice").toObject()
                                   .value("amount").toDouble(-1.0);
            if (amt > 0.0) return amt;
        }
        {
            const double amt = offer.value("listingPrice").toObject()
                                   .value("amount").toDouble(-1.0);
            if (amt > 0.0) return amt;
        }
    }

    // 3: attributes.purchasable_offer
    const QJsonObject attrs = root.value("attributes").toObject();
    for (const QJsonValue &v : attrs.value("purchasable_offer").toArray()) {
        const QJsonObject po = v.toObject();
        if (po.value("marketplace_id").toString() != marketplaceId)
            continue;
        const double amt =
            po.value("our_price").toArray().first().toObject()
              .value("schedule").toArray().first().toObject()
              .value("value_with_tax").toDouble(-1.0);
        if (amt > 0.0) return amt;
    }

    // 4: attributes.list_price
    const double amt = attrs.value("list_price").toArray()
                            .first().toObject()
                            .value("value").toDouble(-1.0);
    return (amt > 0.0) ? amt : -1.0;
}

// Reads a named schedule-based sub-price (e.g. maximum_seller_allowed_price)
// from attributes.purchasable_offer for the given marketplace. Returns -1.0
// when absent.
static double parsePurchasableSubPrice(const QByteArray &json,
                                       const QString &marketplaceId,
                                       const char *field)
{
    const QJsonObject attrs = QJsonDocument::fromJson(json).object()
                                  .value("attributes").toObject();
    for (const QJsonValue &v : attrs.value("purchasable_offer").toArray()) {
        const QJsonObject po = v.toObject();
        if (po.value("marketplace_id").toString() != marketplaceId)
            continue;
        return po.value(QLatin1String(field)).toArray().first().toObject()
                 .value("schedule").toArray().first().toObject()
                 .value("value_with_tax").toDouble(-1.0);
    }
    return -1.0;
}

QCoro::Task<void> AmazonPricingApi::fetchListingPrice(
    QString marketplaceId, QString sku,
    double *priceOut, bool *existsOut, QString *productTypeOut,
    double *minPriceOut, double *maxPriceOut)
{
    *priceOut  = -1.0;
    *existsOut = false;
    if (productTypeOut) productTypeOut->clear();
    if (minPriceOut) *minPriceOut = -1.0;
    if (maxPriceOut) *maxPriceOut = -1.0;
    m_lastError.clear();

    const QString region   = lwaRegionForMarketplace(marketplaceId);
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString sellerId = sellerIdForMarketplace(marketplaceId);

    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID for marketplace %1").arg(marketplaceId);
        co_return;
    }

    QString token;
    co_await _getAccessToken(region, &token);
    if (token.isEmpty()) co_return;

    co_await _rateLimit();

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    query.addQueryItem(QStringLiteral("includedData"),
                       QStringLiteral("offers,attributes,summaries"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    QNetworkReply *reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (status == 404) {
        co_return; // not listed
    }
    if (status != 200) {
        m_lastError = QStringLiteral("HTTP %1 for SKU %2 on %3")
                          .arg(status).arg(sku, marketplaceId);
        qWarning() << "AmazonPricingApi:" << m_lastError
                   << QString::fromUtf8(body.left(300));
        co_return;
    }

    *existsOut = true;
    *priceOut  = parsePriceFromBody(body, marketplaceId, productTypeOut);
    if (minPriceOut)
        *minPriceOut = parsePurchasableSubPrice(
            body, marketplaceId, "minimum_seller_allowed_price");
    if (maxPriceOut)
        *maxPriceOut = parsePurchasableSubPrice(
            body, marketplaceId, "maximum_seller_allowed_price");
}

// ---------------------------------------------------------------------------
// patchListingPrice
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonPricingApi::patchListingPrice(
    QString marketplaceId, QString sku,
    QString productType, QString currency,
    double newPrice, bool *success)
{
    *success = false;
    m_lastError.clear();

    if (productType.isEmpty()) {
        m_lastError = QStringLiteral("Cannot PATCH SKU %1: product type is unknown").arg(sku);
        qWarning() << "AmazonPricingApi:" << m_lastError;
        co_return;
    }

    const QString region   = lwaRegionForMarketplace(marketplaceId);
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString sellerId = sellerIdForMarketplace(marketplaceId);

    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID for marketplace %1").arg(marketplaceId);
        co_return;
    }

    QString token;
    co_await _getAccessToken(region, &token);
    if (token.isEmpty()) co_return;

    co_await _rateLimit();

    // Round to 2 decimal places (all currencies we support use cents)
    const double rounded = qRound(newPrice * 100.0) / 100.0;

    const QJsonObject offerValue{
        {QStringLiteral("audience"),       QStringLiteral("ALL")},
        {QStringLiteral("currency"),       currency},
        {QStringLiteral("marketplace_id"), marketplaceId},
        {QStringLiteral("our_price"), QJsonArray{
            QJsonObject{{QStringLiteral("schedule"), QJsonArray{
                QJsonObject{{QStringLiteral("value_with_tax"), rounded}}
            }}}
        }}
    };

    const QJsonObject patchBody{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"), QJsonArray{
            QJsonObject{
                {QStringLiteral("op"),    QStringLiteral("replace")},
                {QStringLiteral("path"),  QStringLiteral("/attributes/purchasable_offer")},
                {QStringLiteral("value"), QJsonArray{offerValue}}
            }
        }}
    };
    const QByteArray jsonBody = QJsonDocument(patchBody).toJson(QJsonDocument::Compact);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonPricingApi: PATCH" << sku << marketplaceId
             << "price=" << rounded << currency;

    QNetworkReply *reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray respData = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    qDebug() << "AmazonPricingApi: PATCH response HTTP" << httpStatus
             << QString::fromUtf8(respData.left(400));

    if (httpStatus != 200 && httpStatus != 202) {
        m_lastError = QStringLiteral("HTTP %1").arg(httpStatus);
        const QJsonObject errObj = QJsonDocument::fromJson(respData).object();
        const QString errMsg = errObj.value("message").toString();
        if (!errMsg.isEmpty())
            m_lastError += QStringLiteral(": ") + errMsg;
        co_return;
    }

    // Amazon can return HTTP 200 with "status":"INVALID" on validation failure
    const QJsonObject respObj = QJsonDocument::fromJson(respData).object();
    if (respObj.value("status").toString() == QStringLiteral("INVALID")) {
        const QJsonArray issues = respObj.value("issues").toArray();
        QStringList msgs;
        for (const QJsonValue &iv : issues)
            msgs << iv.toObject().value("message").toString();
        m_lastError = QStringLiteral("INVALID: ") + msgs.join("; ");
        co_return;
    }

    *success = true;
}

// ---------------------------------------------------------------------------
// patchListingDiscount — schedule a time-boxed sale price (our_price kept,
// discounted_price added with a start/end schedule).
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonPricingApi::patchListingDiscount(
    QString marketplaceId, QString sku,
    QString productType, QString currency,
    double listPrice, double discountedPrice,
    QDateTime startAt, QDateTime endAt,
    bool *success)
{
    *success = false;
    m_lastError.clear();

    if (productType.isEmpty()) {
        m_lastError = QStringLiteral("Cannot PATCH SKU %1: product type is unknown").arg(sku);
        qWarning() << "AmazonPricingApi:" << m_lastError;
        co_return;
    }
    if (discountedPrice <= 0.0 || listPrice <= 0.0 || discountedPrice >= listPrice) {
        m_lastError = QStringLiteral("Invalid discount for SKU %1: list=%2 discounted=%3")
            .arg(sku).arg(listPrice).arg(discountedPrice);
        co_return;
    }

    const QString region   = lwaRegionForMarketplace(marketplaceId);
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString sellerId = sellerIdForMarketplace(marketplaceId);

    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID for marketplace %1").arg(marketplaceId);
        co_return;
    }

    QString token;
    co_await _getAccessToken(region, &token);
    if (token.isEmpty()) co_return;

    co_await _rateLimit();

    const double roundedList = qRound(listPrice * 100.0) / 100.0;
    const double roundedDisc = qRound(discountedPrice * 100.0) / 100.0;

    // SP-API expects ISO 8601 UTC timestamps for the discount schedule.
    const QString startIso = startAt.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
    const QString endIso   = endAt.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));

    // purchasable_offer is one of the two attributes that support the "merge"
    // op. Per Amazon's docs, a sub-attribute is REMOVED by merging an explicit
    // null (omitting it in a replace does NOT delete it — it is kept). So we
    // merge our_price + discounted_price and set minimum/maximum_seller_allowed_price
    // to null to erase any existing floor/ceiling.
    const QJsonObject offerValue{
        {QStringLiteral("audience"),       QStringLiteral("ALL")},
        {QStringLiteral("currency"),       currency},
        {QStringLiteral("marketplace_id"), marketplaceId},
        {QStringLiteral("our_price"), QJsonArray{
            QJsonObject{{QStringLiteral("schedule"), QJsonArray{
                QJsonObject{{QStringLiteral("value_with_tax"), roundedList}}
            }}}
        }},
        {QStringLiteral("discounted_price"), QJsonArray{
            QJsonObject{{QStringLiteral("schedule"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("value_with_tax"), roundedDisc},
                    {QStringLiteral("start_at"),       startIso},
                    {QStringLiteral("end_at"),         endIso}
                }
            }}}
        }},
        {QStringLiteral("minimum_seller_allowed_price"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("maximum_seller_allowed_price"), QJsonValue(QJsonValue::Null)},
    };

    const QJsonObject patchBody{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"), QJsonArray{
            QJsonObject{
                {QStringLiteral("op"),    QStringLiteral("merge")},
                {QStringLiteral("path"),  QStringLiteral("/attributes/purchasable_offer")},
                {QStringLiteral("value"), QJsonArray{offerValue}}
            }
        }}
    };
    const QByteArray jsonBody = QJsonDocument(patchBody).toJson(QJsonDocument::Compact);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/listings/2021-08-01/items/%1/%2").arg(sellerId, sku));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonPricingApi: PATCH discount" << sku << marketplaceId
             << "list=" << roundedList << "disc=" << roundedDisc << currency
             << startIso << "→" << endIso;

    QNetworkReply *reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray respData = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    qDebug() << "AmazonPricingApi: PATCH discount response HTTP" << httpStatus
             << QString::fromUtf8(respData.left(400));

    // Diagnostic dump so we can inspect exactly what was sent/returned
    // (esp. whether min/max removal was accepted). Attach to Amazon cases.
    {
        const QString dumpPath = QStringLiteral("/tmp/sp-api-discount-%1-%2-%3.txt")
            .arg(sku, marketplaceId,
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        QFile f(dumpPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("PATCH ");     f.write(url.toString().toUtf8());          f.write("\n\n");
            f.write("REQUEST BODY:\n"); f.write(jsonBody);                    f.write("\n\n");
            f.write(QStringLiteral("RESPONSE HTTP %1:\n").arg(httpStatus).toUtf8());
            f.write(respData);
            f.close();
            qDebug() << "AmazonPricingApi: discount dump saved to" << dumpPath;
        }
    }

    if (httpStatus != 200 && httpStatus != 202) {
        m_lastError = QStringLiteral("HTTP %1").arg(httpStatus);
        const QJsonObject errObj = QJsonDocument::fromJson(respData).object();
        const QString errMsg = errObj.value("message").toString();
        if (!errMsg.isEmpty())
            m_lastError += QStringLiteral(": ") + errMsg;
        co_return;
    }

    const QJsonObject respObj = QJsonDocument::fromJson(respData).object();
    if (respObj.value("status").toString() == QStringLiteral("INVALID")) {
        const QJsonArray issues = respObj.value("issues").toArray();
        QStringList msgs;
        for (const QJsonValue &iv : issues)
            msgs << iv.toObject().value("message").toString();
        m_lastError = QStringLiteral("INVALID: ") + msgs.join("; ");
        co_return;
    }

    *success = true;
}
