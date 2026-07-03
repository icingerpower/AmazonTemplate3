// GCC 13 ICE workaround: coroutines with non-trivially-destructible locals in
// the frame trigger a bug in build_special_member_call (cp/call.cc:11096).
// Forcing O1 avoids the affected code path in the coroutine lowering pass.
// In addition, every coroutine in this translation unit returns
// QCoro::Task<void> and communicates its result via an output parameter, to
// avoid co_awaiting a Task<T> whose T is non-trivially destructible (another
// form of the same GCC 13 bug).
#pragma GCC optimize("O1")
#include "AmazonWarningsApi.h"

#include "../TableProductWarnings.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QTimer>
#include <QDebug>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroTimer>

#include <zlib.h>

static QByteArray gunzip(const QByteArray &data)
{
    if (data.isEmpty()) return {};
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK) return {};
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
            inflateEnd(&stream); return {};
        }
        result.append(chunk.constData(), chunkSize - static_cast<int>(stream.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&stream);
    return result;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QString AmazonWarningsApi::endpointForMarketplace(const QString& marketplaceId)
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

QString AmazonWarningsApi::lwaRegionForMarketplace(const QString& marketplaceId)
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

QString AmazonWarningsApi::sellerIdForMarketplace(const QString& marketplaceId) const
{
    const QString lwaReg = lwaRegionForMarketplace(marketplaceId);
    if (lwaReg == "NA") return m_sellerIdNa;
    if (lwaReg == "JP") return m_sellerIdJp;
    return m_sellerIdEu;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AmazonWarningsApi::AmazonWarningsApi(const QString& lwaClientId,
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

AmazonWarningsApi::~AmazonWarningsApi() = default;

QNetworkAccessManager* AmazonWarningsApi::_nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(30'000); // 30 s — aborts silently-hanging requests
    }
    return m_nam;
}

// ---------------------------------------------------------------------------
// LWA access token (same logic as AmazonCatalogApi)
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::_getAccessToken(QString lwaRegion, QString* out)
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
        qDebug() << "AmazonWarningsApi: no refresh token for region" << lwaRegion << "- skipping";
        co_return; // *out stays empty
    }

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
        qWarning() << "AmazonWarningsApi: LWA token exchange failed for region" << lwaRegion
                   << ":" << m_lastError
                   << "Response:" << QString::fromUtf8(data.left(500));
    } else {
        qDebug() << "AmazonWarningsApi: LWA token obtained for region" << lwaRegion
                 << ", expires_in =" << obj.value("expires_in").toInt();
    }
    const int expiresIn = obj.value("expires_in").toInt(3600);
    const int cacheSecs = qMin(expiresIn - 300, 55 * 60);
    *pExpiry = QDateTime::currentDateTimeUtc().addSecs(qMax(cacheSecs, 60));
    *out = *pToken;
    co_return;
}

// ---------------------------------------------------------------------------
// _fetchFbaAsinToSku — Step 1: enumerate FBA SKUs via
// GET_FBA_MYI_UNSUPPRESSED_INVENTORY_DATA report. Fills *out with ASIN→SKU.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::_fetchFbaAsinToSku(QString marketplaceId,
                                                        QHash<QString, QString>* out)
{
    out->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);

    // -------------------------------------------------------------------
    // Step 1: enumerate FBA SKUs via GET_FBA_MYI_UNSUPPRESSED_INVENTORY_DATA
    // report — FBA-only, always includes seller SKUs, no per-page rate limits.
    // -------------------------------------------------------------------
    emit logMessage(QStringLiteral("Step 1: Requesting FBA inventory report…"));
    emit progressChanged(0, 0);

    // 1a — create report
    QString reportId;
    {
        QString token;
        co_await _getAccessToken(lwaReg, &token);
        if (token.isEmpty()) co_return;

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(QStringLiteral("/reports/2021-06-30/reports"));

        QJsonObject body;
        body[QStringLiteral("reportType")]     = QStringLiteral("GET_FBA_MYI_UNSUPPRESSED_INVENTORY_DATA");
        body[QStringLiteral("marketplaceIds")] = QJsonArray{ marketplaceId };

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = _nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (status != 202) {
            m_lastError = QStringLiteral("FBA report creation failed: HTTP %1 — %2")
                          .arg(status).arg(QString::fromUtf8(data.left(400)));
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }

        reportId = QJsonDocument::fromJson(data).object()
                       .value(QStringLiteral("reportId")).toString();
        if (reportId.isEmpty()) {
            m_lastError = QStringLiteral("FBA report creation: no reportId — ")
                          + QString::fromUtf8(data.left(300));
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }
        emit logMessage(QStringLiteral("  Report created, waiting for it to complete…"));
    }

    // 1b — poll until DONE
    QString reportDocumentId;
    {
        constexpr int kMaxPolls = 36; // 36 × 5 s = 3 min
        constexpr int kPollMs   = 5000;

        for (int poll = 0; poll < kMaxPolls; ++poll) {
            QTimer pollTimer;
            pollTimer.setSingleShot(true);
            pollTimer.start(kPollMs);
            co_await qCoro(&pollTimer).waitForTimeout();

            QString token;
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

            const QByteArray pollData = pollReply->readAll();
            const int pollStatus = pollReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            pollReply->deleteLater();

            if (pollStatus != 200) { continue; }

            const QJsonObject pollObj    = QJsonDocument::fromJson(pollData).object();
            const QString     procStatus = pollObj.value(QStringLiteral("processingStatus")).toString();
            emit logMessage(QStringLiteral("  Poll %1: %2").arg(poll + 1).arg(procStatus));

            if (procStatus == QStringLiteral("DONE")) {
                reportDocumentId = pollObj.value(QStringLiteral("reportDocumentId")).toString();
                break;
            }
            if (procStatus == QStringLiteral("FATAL") || procStatus == QStringLiteral("CANCELLED")) {
                m_lastError = QStringLiteral("FBA report failed: ") + procStatus;
                qWarning() << "AmazonWarningsApi:" << m_lastError;
                emit logMessage(QStringLiteral("⚠ ") + m_lastError);
                co_return;
            }
        }

        if (reportDocumentId.isEmpty()) {
            m_lastError = QStringLiteral("FBA report did not complete within 3 minutes");
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }
    }

    // 1c — get download URL
    QString downloadUrl;
    QString compression;
    {
        QString token;
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

        const QByteArray docData = docReply->readAll();
        const int docStatus = docReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        docReply->deleteLater();

        if (docStatus != 200) {
            m_lastError = QStringLiteral("FBA report document fetch failed: HTTP %1 — %2")
                          .arg(docStatus).arg(QString::fromUtf8(docData.left(300)));
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }

        const QJsonObject docObj = QJsonDocument::fromJson(docData).object();
        downloadUrl = docObj.value(QStringLiteral("url")).toString();
        compression = docObj.value(QStringLiteral("compressionAlgorithm")).toString();

        if (downloadUrl.isEmpty()) {
            m_lastError = QStringLiteral("FBA report document: no download URL");
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }
    }

    // 1d — download and parse TSV
    {
        QNetworkReply* dlReply = _nam()->get(QNetworkRequest{QUrl(downloadUrl)});
        co_await qCoro(dlReply).waitForFinished();

        QByteArray tsvData = dlReply->readAll();
        const int dlStatus = dlReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        dlReply->deleteLater();

        if (dlStatus != 200) {
            m_lastError = QStringLiteral("FBA report download failed: HTTP %1").arg(dlStatus);
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }

        if (compression == QStringLiteral("GZIP")) {
            const QByteArray dec = gunzip(tsvData);
            if (dec.isEmpty()) {
                m_lastError = QStringLiteral("FBA report decompression failed");
                qWarning() << "AmazonWarningsApi:" << m_lastError;
                emit logMessage(QStringLiteral("⚠ ") + m_lastError);
                co_return;
            }
            tsvData = dec;
        }

        const QStringList lines = QString::fromUtf8(tsvData).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty()) {
            m_lastError = QStringLiteral("FBA report TSV is empty");
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }

        // FBA inventory report headers (tab-separated): sku, fnsku, asin, product-name, …
        const QStringList headers = lines.at(0).split(QLatin1Char('\t'));
        const int skuCol  = headers.indexOf(QStringLiteral("sku"));
        int       asinCol = headers.indexOf(QStringLiteral("asin"));
        if (asinCol < 0) asinCol = headers.indexOf(QStringLiteral("asin1"));

        if (skuCol < 0 || asinCol < 0) {
            m_lastError = QStringLiteral("FBA report TSV: cannot find sku/asin columns. Headers: ")
                          + lines.at(0).left(300);
            qWarning() << "AmazonWarningsApi:" << m_lastError;
            emit logMessage(QStringLiteral("⚠ ") + m_lastError);
            co_return;
        }

        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
            if (cols.size() <= qMax(skuCol, asinCol)) continue;
            const QString sku  = cols.at(skuCol).trimmed();
            const QString asin = cols.at(asinCol).trimmed();
            if (!sku.isEmpty() && !asin.isEmpty() && !out->contains(asin))
                out->insert(asin, sku);
        }

        emit logMessage(QStringLiteral("Step 1 complete: %1 FBA listing(s) found.").arg(out->size()));
        qDebug() << "AmazonWarningsApi: step1 complete via FBA report, ASINs=" << out->size();
    }
    co_return;
}

// ---------------------------------------------------------------------------
// fetchViolations — Step 1: enumerate FBA listings; Step 2: fetch each listing.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::fetchViolations(QString marketplaceId,
                                                     QList<WarningRow>* out,
                                                     int maxWarnings)
{
    out->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);
    const QString sellerId = sellerIdForMarketplace(marketplaceId);

    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonWarningsApi:" << m_lastError;
        emit logMessage(QStringLiteral("⚠ ") + m_lastError);
        co_return;
    }

    QHash<QString, QString> asinToSku;
    co_await _fetchFbaAsinToSku(marketplaceId, &asinToSku);
    if (asinToSku.isEmpty()) {
        emit logMessage(QStringLiteral("No FBA listings found for this marketplace."));
        emit progressChanged(1, 1);
        co_return;
    }

    // -------------------------------------------------------------------
    // Step 2: for each ASIN/SKU, fetch the listing and extract violations.
    // -------------------------------------------------------------------
    int processed = 0;
    const int totalListings = asinToSku.size();

    emit logMessage(QStringLiteral("Step 2: Checking %1 listing(s) for violations…").arg(totalListings));
    emit progressChanged(0, totalListings);

    for (auto it = asinToSku.constBegin(); it != asinToSku.constEnd(); ++it) {
        // Stop once we have enough violations.
        if (maxWarnings > 0 && out->size() >= maxWarnings) {
            emit logMessage(QStringLiteral("Target of %1 warning(s) reached — stopping.").arg(maxWarnings));
            break;
        }

        const QString asin = it.key();
        const QString sku  = it.value();

        emit logMessage(QStringLiteral("  [%1/%2] %3  %4").arg(processed + 1).arg(totalListings).arg(asin, sku));

        // Rate limit: enforce a 600 ms minimum interval between Step 2 calls.
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

        QString token;
        co_await _getAccessToken(lwaReg, &token);
        if (token.isEmpty()) co_return;

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(QStringLiteral("/listings/2021-08-01/items/") + sellerId + QStringLiteral("/") + sku);

        QUrlQuery q;
        q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
        q.addQueryItem(QStringLiteral("includedData"),
                       QStringLiteral("issues,summaries,attributes"));
        url.setQuery(q);

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");

        ++processed;
        qDebug() << "AmazonWarningsApi: fetchViolations[step2]" << processed << "/" << totalListings
                 << "ASIN" << asin << "SKU" << sku;

        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (status != 200) {
            qWarning() << "AmazonWarningsApi: listing HTTP" << status
                       << "for SKU" << sku
                       << "—" << QString::fromUtf8(data.left(300));
            emit logMessage(QStringLiteral("    ⚠ HTTP %1").arg(status));
            emit progressChanged(processed, totalListings);
            continue;
        }

        const QJsonObject root = QJsonDocument::fromJson(data).object();

        // --- Extract title + mainImage + productType from summaries[] matching marketplaceId ---
        QString itemTitle;
        QString mainImageUrl;
        QString itemProdType;
        {
            const QJsonArray summaries = root.value(QStringLiteral("summaries")).toArray();
            for (const QJsonValue& sv : summaries) {
                const QJsonObject sobj = sv.toObject();
                if (sobj.value(QStringLiteral("marketplaceId")).toString() == marketplaceId) {
                    itemTitle       = sobj.value(QStringLiteral("itemName")).toString();
                    mainImageUrl    = sobj.value(QStringLiteral("mainImage")).toObject()
                                         .value(QStringLiteral("link")).toString();
                    itemProdType = sobj.value(QStringLiteral("productType")).toString();
                    break;
                }
            }
        }

        // --- Attributes: resolve current value of each flagged field ---
        const QJsonObject attributesObj = root.value(QStringLiteral("attributes")).toObject();

        // Helper: extract a human-readable value from attributes[attributeId]
        auto resolveValue = [&attributesObj](const QString &attributeId) -> QString {
            const QJsonArray attrArr = attributesObj.value(attributeId).toArray();
            if (attrArr.isEmpty()) return {};
            const QJsonValue first = attrArr.first();
            if (!first.isObject()) return {};
            const QJsonObject firstObj = first.toObject();
            if (!firstObj.contains(QStringLiteral("value"))) return {};
            const QJsonValue valNode = firstObj.value(QStringLiteral("value"));
            if (valNode.isString())  return valNode.toString();
            if (valNode.isDouble())  return QString::number(valNode.toDouble());
            if (valNode.isBool())    return valNode.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            if (valNode.isObject())  return QString::fromUtf8(QJsonDocument(valNode.toObject()).toJson(QJsonDocument::Compact));
            if (valNode.isArray())   return QString::fromUtf8(QJsonDocument(valNode.toArray()).toJson(QJsonDocument::Compact));
            return {};
        };

        // --- Walk issues[]: accept ERROR and WARNING, handle empty attributeNames ---
        const QJsonArray issues = root.value(QStringLiteral("issues")).toArray();
        int violationCount = 0;
        for (const QJsonValue& iv : issues) {
            const QJsonObject iobj     = iv.toObject();
            const QString     severity = iobj.value(QStringLiteral("severity")).toString();
            if (severity != QStringLiteral("ERROR") && severity != QStringLiteral("WARNING"))
                continue;

            const QJsonArray attributeNames = iobj.value(QStringLiteral("attributeNames")).toArray();
            const QString    issueCode      = iobj.value(QStringLiteral("code")).toString();
            const QString    issueMessage   = iobj.value(QStringLiteral("message")).toString();

            if (attributeNames.isEmpty()) {
                // No specific attribute named — record the issue against the code itself
                // so "Product Attribute Misuse" (Bullet Point Removed, etc.) are captured.
                if (issueCode.isEmpty()) continue;
                WarningRow row;
                row.asin         = asin;
                row.sku          = sku;
                row.title        = itemTitle;
                row.attributeId  = issueCode;
                row.issueMessage = issueMessage;
                row.value        = issueMessage;
                row.aiValue      = QString();
                row.mainImageUrl = mainImageUrl;
                row.productType  = itemProdType;
                out->append(row);
                ++violationCount;
            } else {
                for (const QJsonValue& av : attributeNames) {
                    const QString attributeId = av.toString();
                    if (attributeId.isEmpty()) continue;

                    WarningRow row;
                    row.asin         = asin;
                    row.sku          = sku;
                    row.title        = itemTitle;
                    row.attributeId  = attributeId;
                    row.issueMessage = issueMessage;
                    row.value        = resolveValue(attributeId);
                    row.aiValue      = QString();
                    row.mainImageUrl = mainImageUrl;
                    row.productType  = itemProdType;

                    // For bullet_point violations, capture ALL current bullets so the
                    // AI prompt can present the full list for context.
                    if (attributeId == QStringLiteral("bullet_point")) {
                        const QJsonArray bulletsArr =
                            attributesObj.value(QStringLiteral("bullet_point")).toArray();
                        for (const QJsonValue &bv : bulletsArr) {
                            const QString bpVal = bv.toObject()
                                                    .value(QStringLiteral("value")).toString();
                            row.bulletPoints.append(bpVal);
                        }
                    }

                    out->append(row);
                    ++violationCount;
                }
            }
        }

        if (violationCount > 0)
            emit logMessage(QStringLiteral("    → %1 violation(s)").arg(violationCount));

        emit progressChanged(processed, totalListings);
    }

    emit logMessage(QStringLiteral("Done. %1 violation row(s) found.").arg(out->size()));
    emit progressChanged(totalListings, totalListings);

    qDebug() << "AmazonWarningsApi: fetchViolations complete, listings checked="
             << processed << "warning rows=" << out->size();
    co_return;
}

// _fetchAllListingsAsinToSku — Fallback step: enumerate ALL merchant listings
// (including inactive/sold-out) via GET_MERCHANT_LISTINGS_ALL_DATA report.
// Only adds entries for ASINs not already in *out (FBA data takes priority).
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::_fetchAllListingsAsinToSku(QString marketplaceId,
                                                                 QHash<QString, QString>* out)
{
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);

    qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku START marketplace=" << marketplaceId
             << "endpoint=" << endpoint << "lwaReg=" << lwaReg;
    emit logMessage(QStringLiteral("Step 1b: Requesting all-listings report (inactive/MFN products)…"));

    // 1a — create report
    QString reportId;
    {
        QString token;
        co_await _getAccessToken(lwaReg, &token);
        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku token isEmpty=" << token.isEmpty();
        if (token.isEmpty()) co_return;

        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(endpoint);
        url.setPath(QStringLiteral("/reports/2021-06-30/reports"));

        QJsonObject body;
        body[QStringLiteral("reportType")]     = QStringLiteral("GET_MERCHANT_LISTINGS_ALL_DATA");
        body[QStringLiteral("marketplaceIds")] = QJsonArray{ marketplaceId };

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = _nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku report creation HTTP=" << status
                 << "body=" << data.left(300);

        if (status != 202) {
            emit logMessage(QStringLiteral("  ⚠ All-listings report creation failed: HTTP %1 — %2")
                            .arg(status).arg(QString::fromUtf8(data.left(300))));
            co_return;
        }

        reportId = QJsonDocument::fromJson(data).object()
                       .value(QStringLiteral("reportId")).toString();
        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku reportId=" << reportId;
        if (reportId.isEmpty()) {
            emit logMessage(QStringLiteral("  ⚠ All-listings report: no reportId"));
            co_return;
        }
        emit logMessage(QStringLiteral("  Report created, waiting for it to complete…"));
    }

    // 1b — poll until DONE
    QString reportDocumentId;
    {
        constexpr int kMaxPolls = 36;
        constexpr int kPollMs   = 5000;

        for (int poll = 0; poll < kMaxPolls; ++poll) {
            QTimer pollTimer;
            pollTimer.setSingleShot(true);
            pollTimer.start(kPollMs);
            co_await qCoro(&pollTimer).waitForTimeout();

            QString token;
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

            const QByteArray pollData = pollReply->readAll();
            const int pollStatus = pollReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            pollReply->deleteLater();

            qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku poll" << poll + 1
                     << "HTTP=" << pollStatus;
            if (pollStatus != 200) continue;

            const QJsonObject pollObj    = QJsonDocument::fromJson(pollData).object();
            const QString     procStatus = pollObj.value(QStringLiteral("processingStatus")).toString();
            qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku procStatus=" << procStatus;
            emit logMessage(QStringLiteral("  Poll %1: %2").arg(poll + 1).arg(procStatus));

            if (procStatus == QStringLiteral("DONE")) {
                reportDocumentId = pollObj.value(QStringLiteral("reportDocumentId")).toString();
                qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku DONE reportDocumentId="
                         << reportDocumentId << "pollData=" << pollData.left(300);
                break;
            }
            if (procStatus == QStringLiteral("FATAL") || procStatus == QStringLiteral("CANCELLED")) {
                emit logMessage(QStringLiteral("  ⚠ All-listings report failed: ") + procStatus);
                co_return;
            }
        }

        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku after poll loop reportDocumentId=" << reportDocumentId;
        if (reportDocumentId.isEmpty()) {
            emit logMessage(QStringLiteral("  ⚠ All-listings report did not complete within 3 minutes"));
            co_return;
        }
    }

    // 1c — get download URL
    QString downloadUrl;
    QString compression;
    {
        QString token;
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

        const QByteArray docData = docReply->readAll();
        const int docStatus = docReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        docReply->deleteLater();

        if (docStatus != 200) {
            emit logMessage(QStringLiteral("  ⚠ All-listings document fetch failed: HTTP %1").arg(docStatus));
            co_return;
        }

        const QJsonObject docObj = QJsonDocument::fromJson(docData).object();
        downloadUrl = docObj.value(QStringLiteral("url")).toString();
        compression = docObj.value(QStringLiteral("compressionAlgorithm")).toString();

        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku docStatus=" << docStatus
                 << "downloadUrl empty=" << downloadUrl.isEmpty()
                 << "compression=" << compression
                 << "docData=" << docData.left(200);

        if (downloadUrl.isEmpty()) {
            emit logMessage(QStringLiteral("  ⚠ All-listings document: no download URL"));
            co_return;
        }
    }

    // 1d — download and parse TSV
    {
        QNetworkReply* dlReply = _nam()->get(QNetworkRequest{QUrl(downloadUrl)});
        co_await qCoro(dlReply).waitForFinished();

        QByteArray tsvData = dlReply->readAll();
        const int dlStatus = dlReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        dlReply->deleteLater();

        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku dlStatus=" << dlStatus
                 << "tsvData.size()=" << tsvData.size()
                 << "compression=" << compression;

        if (dlStatus != 200) {
            emit logMessage(QStringLiteral("  ⚠ All-listings report download failed: HTTP %1").arg(dlStatus));
            co_return;
        }

        if (compression == QStringLiteral("GZIP")) {
            const QByteArray dec = gunzip(tsvData);
            qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku after gunzip, dec.size()=" << dec.size();
            if (dec.isEmpty()) {
                emit logMessage(QStringLiteral("  ⚠ All-listings report decompression failed"));
                co_return;
            }
            tsvData = dec;
        }

        const QStringList lines = QString::fromUtf8(tsvData).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku lines.size()=" << lines.size()
                 << "first300=" << QString::fromUtf8(tsvData).left(300);
        if (lines.isEmpty()) {
            emit logMessage(QStringLiteral("  ⚠ All-listings report TSV is empty"));
            co_return;
        }

        // GET_MERCHANT_LISTINGS_ALL_DATA header columns vary by region:
        //   EU/FR: "seller-sku" + "product-id" (with "product-id-type" filter)
        //   UK/NA: "seller-sku" + "asin1"
        const QStringList headers = lines.at(0).split(QLatin1Char('\t'));
        const int skuCol   = headers.indexOf(QStringLiteral("seller-sku"));
        int       asinCol  = headers.indexOf(QStringLiteral("asin1"));
        int       ptypeCol = -1; // only needed when falling back to product-id
        if (asinCol < 0) {
            asinCol  = headers.indexOf(QStringLiteral("product-id"));
            ptypeCol = headers.indexOf(QStringLiteral("product-id-type"));
        }

        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku skuCol=" << skuCol
                 << "asinCol=" << asinCol << "ptypeCol=" << ptypeCol
                 << "totalLines=" << lines.size()
                 << "headers=" << lines.at(0).left(300);

        if (skuCol < 0 || asinCol < 0) {
            emit logMessage(QStringLiteral("  ⚠ All-listings report: unexpected TSV format (no seller-sku or asin1/product-id). Headers: ")
                            + lines.at(0).left(200));
            co_return;
        }

        static const QRegularExpression asinRegex(QStringLiteral("^[A-Z0-9]{10}$"));
        bool loggedFirstSkip = false;
        int added = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines.at(i).split(QLatin1Char('\t'));
            if (cols.size() <= qMax(skuCol, asinCol)) continue;
            const QString sku  = cols.at(skuCol).trimmed();
            const QString asin = cols.at(asinCol).trimmed();
            // When using product-id column, skip non-ASIN rows (EAN/UPC/ISBN).
            // Some FR exports leave product-id-type empty even for ASIN rows, so
            // accept an empty type when the value itself looks like an ASIN.
            if (ptypeCol >= 0 && cols.size() > ptypeCol) {
                const QString ptype = cols.at(ptypeCol).trimmed();
                const bool isAsinRow =
                    ptype == QStringLiteral("1") ||
                    ptype == QStringLiteral("ASIN") ||
                    (ptype.isEmpty() && asinRegex.match(asin).hasMatch());
                if (!isAsinRow) {
                    if (!loggedFirstSkip) {
                        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku skipping row, "
                                    "product-id-type=" << ptype << "asin=" << asin;
                        loggedFirstSkip = true;
                    }
                    continue;
                }
            }
            if (!sku.isEmpty() && !asin.isEmpty() && !out->contains(asin)) {
                qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku insert asin=" << asin << "sku=" << sku;
                out->insert(asin, sku);
                ++added;
            }
        }

        qDebug() << "AmazonWarningsApi: _fetchAllListingsAsinToSku complete, added=" << added
                 << "total=" << out->size();
        emit logMessage(QStringLiteral("Step 1b complete: %1 additional ASIN(s) found in all-listings report.")
                        .arg(added));
    }
    co_return;
}

// ---------------------------------------------------------------------------
// _fetchFbaInventoryApiAsinToSku — fallback via the paginated FBA Inventory API
// (GET /fba/inventory/v1/summaries). Unlike the FBA unsuppressed report, this
// endpoint includes items with 0 stock (sold-out FBA listings). Only new ASINs
// are added to *out — existing entries are never overwritten.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::_fetchFbaInventoryApiAsinToSku(QString marketplaceId,
                                                                     QHash<QString, QString>* out)
{
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);

    QString nextToken;
    int pageCount = 0;
    int added = 0;
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

        qDebug() << "AmazonWarningsApi: _fetchFbaInventoryApiAsinToSku page" << (pageCount + 1)
                 << url.toString();
        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (status != 200) {
            qWarning() << "AmazonWarningsApi: _fetchFbaInventoryApiAsinToSku HTTP" << status
                       << QString::fromUtf8(data.left(300));
            emit logMessage(QStringLiteral("  ⚠ FBA Inventory API failed: HTTP %1").arg(status));
            break;
        }

        const QJsonObject root = QJsonDocument::fromJson(data).object();
        const QJsonArray summaries = root.value(QStringLiteral("payload")).toObject()
                                         .value(QStringLiteral("inventorySummaries")).toArray();
        for (const QJsonValue& v : summaries) {
            const QJsonObject obj = v.toObject();
            const QString asin    = obj.value(QStringLiteral("asin")).toString();
            const QString sku     = obj.value(QStringLiteral("sellerSku")).toString();
            if (!asin.isEmpty() && !sku.isEmpty() && !out->contains(asin)) {
                qDebug() << "AmazonWarningsApi: _fetchFbaInventoryApiAsinToSku insert asin="
                         << asin << "sku=" << sku;
                out->insert(asin, sku);
                ++added;
            }
        }

        nextToken = root.value(QStringLiteral("pagination")).toObject()
                        .value(QStringLiteral("nextToken")).toString();
        ++pageCount;

        qDebug() << "AmazonWarningsApi: _fetchFbaInventoryApiAsinToSku page" << pageCount
                 << "got" << summaries.size() << "items, added so far" << added
                 << "total map" << out->size()
                 << (nextToken.isEmpty() ? "(last page)" : "(more pages)");

    } while (!nextToken.isEmpty() && pageCount < kMaxPages);

    qDebug() << "AmazonWarningsApi: _fetchFbaInventoryApiAsinToSku complete, pages=" << pageCount
             << "added=" << added << "total=" << out->size();
    emit logMessage(QStringLiteral("Step 1c complete: %1 additional ASIN(s) found via FBA Inventory API.")
                    .arg(added));
    co_return;
}

// ---------------------------------------------------------------------------
// enrichPastedRows — Step 1: FBA report for SKU; Step 2: per-ASIN listing data.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::enrichPastedRows(QString marketplaceId,
                                                        QList<WarningRow>* rows)
{
    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);
    const QString sellerId = sellerIdForMarketplace(marketplaceId);

    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        emit logMessage(QStringLiteral("⚠ ") + m_lastError);
        co_return;
    }

    QHash<QString, QString> asinToSku;
    co_await _fetchFbaAsinToSku(marketplaceId, &asinToSku);
    qDebug() << "AmazonWarningsApi: enrichPastedRows FBA report done, asinToSku.size()=" << asinToSku.size();

    // Check which pasted ASINs are missing from the FBA report
    // (inactive / sold-out / MFN products). If any are missing, supplement
    // with GET_MERCHANT_LISTINGS_ALL_DATA which covers all listing states.
    {
        bool anyMissing = false;
        QStringList missingAsins;
        for (const WarningRow &row : *rows) {
            if (!asinToSku.contains(row.asin)) {
                anyMissing = true;
                if (!missingAsins.contains(row.asin)) missingAsins.append(row.asin);
            }
        }
        qDebug() << "AmazonWarningsApi: enrichPastedRows anyMissing=" << anyMissing
                 << "missingAsins=" << missingAsins;
        if (anyMissing) {
            emit logMessage(QStringLiteral("Some pasted ASINs not found in FBA report — fetching all-listings report…"));
            co_await _fetchAllListingsAsinToSku(marketplaceId, &asinToSku);
            // Also try FBA Inventory API for sold-out FBA items missed by the report.
            bool stillMissing = false;
            for (const WarningRow &row : *rows) {
                if (!asinToSku.contains(row.asin)) { stillMissing = true; break; }
            }
            if (stillMissing) {
                emit logMessage(QStringLiteral("Some ASINs still missing — trying FBA Inventory API…"));
                co_await _fetchFbaInventoryApiAsinToSku(marketplaceId, &asinToSku);
            }
        }

        // Summary: report any pasted ASINs that are still unresolved after all fallbacks.
        QStringList unresolved;
        for (const WarningRow &row : *rows) {
            if (!asinToSku.contains(row.asin) && !unresolved.contains(row.asin))
                unresolved.append(row.asin);
        }
        if (!unresolved.isEmpty()) {
            emit logMessage(QStringLiteral("⚠ %1 pasted ASIN(s) still have no SKU after all fallbacks: %2")
                                .arg(unresolved.size())
                                .arg(unresolved.join(QStringLiteral(", "))));
            qDebug() << "AmazonWarningsApi: enrichPastedRows unresolved ASINs=" << unresolved;
        }
    }

    if (asinToSku.isEmpty()) {
        emit logMessage(QStringLiteral("No listings found for this marketplace."));
        emit progressChanged(1, 1);
        co_return;
    }

    // Build ordered unique-ASIN list preserving row order
    QStringList uniqueAsins;
    {
        QSet<QString> seen;
        for (const WarningRow &row : *rows) {
            if (!seen.contains(row.asin)) {
                seen.insert(row.asin);
                uniqueAsins.append(row.asin);
            }
        }
    }

    int processed = 0;
    const int total = uniqueAsins.size();
    emit logMessage(QStringLiteral("Step 2: Fetching data for %1 ASIN(s)…").arg(total));
    emit progressChanged(0, total);

    for (int ai = 0; ai < uniqueAsins.size(); ++ai) {
        const QString asin = uniqueAsins.at(ai);
        ++processed;
        const QString sku = asinToSku.value(asin);

        emit logMessage(QStringLiteral("  [%1/%2] %3  %4")
            .arg(processed).arg(total).arg(asin,
                 sku.isEmpty() ? QStringLiteral("(not in FBA report)") : sku));

        if (!sku.isEmpty()) {
            // Rate limit: 600ms minimum between listings API calls
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

            QString token;
            co_await _getAccessToken(lwaReg, &token);
            if (token.isEmpty()) co_return;

            QUrl url;
            url.setScheme(QStringLiteral("https"));
            url.setHost(endpoint);
            url.setPath(QStringLiteral("/listings/2021-08-01/items/") + sellerId + QStringLiteral("/") + sku);

            QUrlQuery q;
            q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
            q.addQueryItem(QStringLiteral("includedData"), QStringLiteral("summaries,attributes"));
            url.setQuery(q);

            QNetworkRequest req(url);
            req.setRawHeader("x-amz-access-token", token.toUtf8());
            req.setRawHeader("accept", "application/json");

            QNetworkReply* reply = _nam()->get(req);
            co_await qCoro(reply).waitForFinished();

            const QByteArray data = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();

            if (status == 200) {
                const QJsonObject root = QJsonDocument::fromJson(data).object();

                QString itemTitle;
                QString mainImageUrl;
                QString itemProdType;
                {
                    const QJsonArray summaries = root.value(QStringLiteral("summaries")).toArray();
                    for (const QJsonValue& sv : summaries) {
                        const QJsonObject sobj = sv.toObject();
                        if (sobj.value(QStringLiteral("marketplaceId")).toString() == marketplaceId) {
                            itemTitle       = sobj.value(QStringLiteral("itemName")).toString();
                            mainImageUrl    = sobj.value(QStringLiteral("mainImage")).toObject()
                                                 .value(QStringLiteral("link")).toString();
                            itemProdType    = sobj.value(QStringLiteral("productType")).toString();
                            break;
                        }
                    }
                }

                const QJsonObject attributesObj = root.value(QStringLiteral("attributes")).toObject();

                auto resolveValue = [&attributesObj](const QString &attrId) -> QString {
                    const QJsonArray arr = attributesObj.value(attrId).toArray();
                    if (arr.isEmpty()) return {};
                    const QJsonObject obj = arr.first().toObject();
                    if (!obj.contains(QStringLiteral("value"))) return {};
                    const QJsonValue v = obj.value(QStringLiteral("value"));
                    if (v.isString())  return v.toString();
                    if (v.isDouble())  return QString::number(v.toDouble());
                    if (v.isBool())    return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
                    return {};
                };

                QStringList bulletPoints;
                {
                    const QJsonArray bulletsArr =
                        attributesObj.value(QStringLiteral("bullet_point")).toArray();
                    for (const QJsonValue &bv : bulletsArr) {
                        const QString bp =
                            bv.toObject().value(QStringLiteral("value")).toString();
                        if (!bp.isEmpty()) bulletPoints.append(bp);
                    }
                }

                for (WarningRow &row : *rows) {
                    if (row.asin != asin) continue;
                    row.sku          = sku;
                    if (row.title.isEmpty()) row.title = itemTitle;
                    row.mainImageUrl = mainImageUrl;
                    row.productType  = itemProdType;
                    row.value        = resolveValue(row.attributeId);
                    if (row.attributeId == QStringLiteral("bullet_point"))
                        row.bulletPoints = bulletPoints;
                }
            } else {
                emit logMessage(QStringLiteral("    ⚠ HTTP %1 for SKU %2").arg(status).arg(sku));
                for (WarningRow &row : *rows) {
                    if (row.asin == asin) row.sku = sku;
                }
            }
        }

        emit progressChanged(processed, total);
    }

    // Summary: how many pasted ASINs were matched vs. not found in the FBA report.
    QStringList missing;
    for (const QString &asin : uniqueAsins) {
        if (!asinToSku.contains(asin)) missing.append(asin);
    }
    const int matched = total - missing.size();
    emit logMessage(QStringLiteral("Enrichment complete: %1/%2 ASIN(s) matched in FBA report.")
                    .arg(matched).arg(total));
    if (!missing.isEmpty()) {
        emit logMessage(QStringLiteral("⚠ %1 ASIN(s) NOT found in FBA report "
                                       "(MFN listing, wrong marketplace, or report gap):")
                        .arg(missing.size()));
        for (const QString &asin : missing)
            emit logMessage(QStringLiteral("    • %1").arg(asin));
    }
}

// ---------------------------------------------------------------------------
// fetchListingProductType — GET /listings/2021-08-01/items/{sellerId}/{sku}
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::fetchListingProductType(QString marketplaceId,
                                                              QString sku,
                                                              QString* productType)
{
    productType->clear();

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        qWarning() << "AmazonWarningsApi::fetchListingProductType: no seller ID for" << marketplaceId;
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

    qDebug() << "AmazonWarningsApi: fetchListingProductType GET" << url.toString();
    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus != 200 || data.isEmpty()) {
        qWarning() << "AmazonWarningsApi::fetchListingProductType: HTTP" << httpStatus
                   << "for SKU" << sku;
        co_return;
    }

    const QJsonArray summaries =
        QJsonDocument::fromJson(data).object().value(QStringLiteral("summaries")).toArray();
    if (summaries.isEmpty()) {
        qWarning() << "AmazonWarningsApi::fetchListingProductType: no summaries in response for" << sku;
        co_return;
    }

    *productType = summaries.first().toObject().value(QStringLiteral("productType")).toString();
    qDebug() << "AmazonWarningsApi::fetchListingProductType: SKU" << sku
             << "→ productType =" << *productType;
    co_return;
}

// ---------------------------------------------------------------------------
// fetchProductTypeFromCatalog — Catalog Items API fallback for inactive listings
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::fetchProductTypeFromCatalog(QString marketplaceId,
                                                                   QString asin,
                                                                   QString* productType,
                                                                   QString* classificationId,
                                                                   QString* classificationDisplayName)
{
    productType->clear();
    if (classificationId)          classificationId->clear();
    if (classificationDisplayName) classificationDisplayName->clear();
    if (asin.isEmpty()) co_return;

    const QString endpoint = endpointForMarketplace(marketplaceId);
    const QString lwaReg   = lwaRegionForMarketplace(marketplaceId);

    QString token;
    co_await _getAccessToken(lwaReg, &token);
    if (token.isEmpty()) co_return;

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/catalog/2022-04-01/items/") + asin);

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("marketplaceIds"), marketplaceId);
    q.addQueryItem(QStringLiteral("includedData"),   QStringLiteral("summaries"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setRawHeader("accept", "application/json");

    QNetworkReply* reply = _nam()->get(req);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data   = reply->readAll();
    const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (status != 200 || data.isEmpty()) {
        qWarning() << "AmazonWarningsApi::fetchProductTypeFromCatalog: HTTP" << status
                   << "for ASIN" << asin;
        co_return;
    }

    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QJsonArray summaries = root.value(QStringLiteral("summaries")).toArray();
    for (const QJsonValue &sv : summaries) {
        const QJsonObject sobj = sv.toObject();
        const QString pt = sobj.value(QStringLiteral("productType")).toString();
        if (!pt.isEmpty()) {
            *productType = pt;
            qDebug() << "AmazonWarningsApi::fetchProductTypeFromCatalog: ASIN" << asin
                     << "→ productType =" << pt;
            co_return;
        }
        // Extract classification info as fallback identifier
        if (classificationId && classificationId->isEmpty()) {
            const QJsonObject bc = sobj.value(QStringLiteral("browseClassification")).toObject();
            *classificationId = bc.value(QStringLiteral("classificationId")).toString();
            if (classificationDisplayName)
                *classificationDisplayName = bc.value(QStringLiteral("displayName")).toString();
        }
    }
    // Log the full response so we can diagnose why productType is missing.
    qWarning() << "AmazonWarningsApi::fetchProductTypeFromCatalog: no productType for ASIN"
               << asin << "— HTTP" << status
               << "— body:" << QString::fromUtf8(data.left(500));
    emit logMessage(QStringLiteral("    ⚠ Catalog API response for %1: %2")
                    .arg(asin, QString::fromUtf8(data.left(300))));
}

// ---------------------------------------------------------------------------
// fetchAttributeEnumValues — Product Type Definitions API + JSON Schema enum
// (same pattern as AmazonCatalogApi::fetchSizeChartAttributeName)
// ---------------------------------------------------------------------------

namespace {

// Recursive depth-limited search for the first "enum" array inside a JSON node.
// Returns the list of stringified enum entries, or an empty list if none found.
static QStringList extractEnumValues(const QJsonValue &node, int depth)
{
    if (depth > 8) return {};

    if (node.isObject()) {
        const QJsonObject obj = node.toObject();
        if (obj.contains(QStringLiteral("enum"))) {
            QStringList values;
            const QJsonArray arr = obj.value(QStringLiteral("enum")).toArray();
            for (const QJsonValue &v : arr) {
                if (v.isString()) {
                    const QString s = v.toString();
                    if (!s.isEmpty()) values.append(s);
                } else if (v.isDouble()) {
                    values.append(QString::number(v.toDouble()));
                } else if (v.isBool()) {
                    values.append(v.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
                }
            }
            if (!values.isEmpty()) return values;
        }
        // Recurse into all object values
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QStringList found = extractEnumValues(it.value(), depth + 1);
            if (!found.isEmpty()) return found;
        }
    } else if (node.isArray()) {
        const QJsonArray arr = node.toArray();
        for (const QJsonValue &v : arr) {
            const QStringList found = extractEnumValues(v, depth + 1);
            if (!found.isEmpty()) return found;
        }
    }
    return {};
}

} // namespace

QCoro::Task<void> AmazonWarningsApi::fetchAttributeEnumValues(QString marketplaceId,
                                                               QString productType,
                                                               QString attributeId,
                                                               QStringList* out)
{
    out->clear();

    if (productType.isEmpty() || attributeId.isEmpty()) {
        co_return;
    }

    const QString cacheKey = productType + QLatin1Char(':') + marketplaceId;

    // -------------------------------------------------------------------
    // Step 1 — fetch schema URL (skip if cached)
    // -------------------------------------------------------------------
    if (!m_schemaCache.contains(cacheKey)) {
        const QString endpoint = endpointForMarketplace(marketplaceId);
        const QString path =
            QStringLiteral("/definitions/2020-09-01/productTypes/%1").arg(productType);

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
        if (token.isEmpty()) {
            qWarning() << "AmazonWarningsApi::fetchAttributeEnumValues: no access token";
            emit logMessage(QStringLiteral("  \xe2\x9a\xa0 Schema fetch failed for product type %1")
                            .arg(productType));
            co_return;
        }

        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("accept", "application/json");

        qDebug() << "AmazonWarningsApi: fetchAttributeEnumValues GET" << url.toString();
        QNetworkReply* reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (httpStatus != 200 || data.isEmpty()) {
            qWarning() << "AmazonWarningsApi::fetchAttributeEnumValues: productTypes HTTP"
                       << httpStatus << "for type" << productType;
            emit logMessage(QStringLiteral("  \xe2\x9a\xa0 Schema fetch failed for product type %1")
                            .arg(productType));
            co_return;
        }

        const QString schemaUrl = QJsonDocument::fromJson(data).object()
            .value(QStringLiteral("schema")).toObject()
            .value(QStringLiteral("link")).toObject()
            .value(QStringLiteral("resource")).toString();

        if (schemaUrl.isEmpty()) {
            qWarning() << "AmazonWarningsApi::fetchAttributeEnumValues: no schema URL for"
                       << productType;
            emit logMessage(QStringLiteral("  \xe2\x9a\xa0 Schema fetch failed for product type %1")
                            .arg(productType));
            co_return;
        }

        // -------------------------------------------------------------------
        // Step 2 — download schema JSON from S3 (presigned URL, no auth)
        // -------------------------------------------------------------------
        const QUrl schemaUrlObj(schemaUrl);
        QNetworkRequest schemaReq(schemaUrlObj);
        qDebug() << "AmazonWarningsApi: downloading product type schema from S3";
        QNetworkReply* schemaReply = _nam()->get(schemaReq);
        co_await qCoro(schemaReply).waitForFinished();

        const QByteArray schemaData = schemaReply->readAll();
        const int schemaStatus = schemaReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        schemaReply->deleteLater();

        if (schemaStatus != 200 || schemaData.isEmpty()) {
            qWarning() << "AmazonWarningsApi::fetchAttributeEnumValues: schema download HTTP"
                       << schemaStatus;
            emit logMessage(QStringLiteral("  \xe2\x9a\xa0 Schema fetch failed for product type %1")
                            .arg(productType));
            co_return;
        }

        m_schemaCache.insert(cacheKey, schemaData);
    }

    // -------------------------------------------------------------------
    // Step 3 — parse the cached schema and locate enum values for attributeId
    // -------------------------------------------------------------------
    const QByteArray schemaData = m_schemaCache.value(cacheKey);
    const QJsonObject root = QJsonDocument::fromJson(schemaData).object();

    // Collect candidate "properties" maps (paths 1..4 — same as fetchSizeChartAttributeName)
    QList<QJsonObject> candidates;

    const QJsonObject topProps = root.value(QStringLiteral("properties")).toObject();
    const QJsonObject attrNode = topProps.value(QStringLiteral("attributes")).toObject();

    // Path 1
    const QJsonObject path1 = attrNode.value(QStringLiteral("properties")).toObject();
    if (!path1.isEmpty()) candidates << path1;

    // Path 2
    if (!topProps.isEmpty()) candidates << topProps;

    // Paths 3 + 4 — iterate allOf array
    const QJsonArray allOf = root.value(QStringLiteral("allOf")).toArray();
    for (const QJsonValue &v : allOf) {
        const QJsonObject entry = v.toObject();
        const QJsonObject ep = entry.value(QStringLiteral("properties")).toObject();
        const QJsonObject ea = ep.value(QStringLiteral("attributes")).toObject();
        const QJsonObject eap = ea.value(QStringLiteral("properties")).toObject();
        if (!eap.isEmpty()) candidates << eap;
        if (!ep.isEmpty())  candidates << ep;
    }

    // Find a case-insensitive key match for attributeId in each candidate, then
    // recursively extract the first "enum" array under that node.
    const QString attrLower = attributeId.toLower();
    bool foundKey = false;
    for (const QJsonObject &props : candidates) {
        for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
            if (it.key().compare(attributeId, Qt::CaseInsensitive) != 0)
                continue;
            foundKey = true;
            const QStringList values = extractEnumValues(it.value(), 0);
            if (!values.isEmpty()) {
                *out = values;
                const QStringList preview = values.mid(0, 5);
                emit logMessage(QStringLiteral("  Attribute %1: %2 value(s) — %3%4")
                                .arg(attributeId)
                                .arg(values.size())
                                .arg(preview.join(QStringLiteral(", ")))
                                .arg(values.size() > 5 ? QStringLiteral("…") : QString{}));
                co_return;
            }
        }
    }

    Q_UNUSED(attrLower);

    if (foundKey) {
        qDebug() << "AmazonWarningsApi::fetchAttributeEnumValues: attribute"
                 << attributeId << "found but contains no enum (free-text or $ref not resolved)";
    } else {
        qDebug() << "AmazonWarningsApi::fetchAttributeEnumValues: attribute"
                 << attributeId << "not present in schema for" << productType;
    }
    emit logMessage(QStringLiteral("  \xe2\x9a\xa0 Attribute %1: no enum values in schema "
                                    "(free-text or $ref not resolved)").arg(attributeId));
    co_return;
}

// ---------------------------------------------------------------------------
// patchListingAttribute — PATCH /listings/2021-08-01/items/{sellerId}/{sku}
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::patchListingAttribute(QString marketplaceId,
                                                            QString sku,
                                                            QString productType,
                                                            QString attributeId,
                                                            QString value,
                                                            bool* success)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonWarningsApi:" << m_lastError;
        co_return;
    }

    // Build the attribute value array.
    // bullet_point is multi-valued: the AI stores bullets newline-separated,
    // and Amazon expects one array entry per bullet (each ≤ 700 chars).
    // All other attributes are single-valued.
    QJsonArray attrValues;
    if (attributeId == QStringLiteral("bullet_point")) {
        const QStringList bullets = value.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &bullet : bullets) {
            const QString trimmed = bullet.trimmed();
            if (trimmed.isEmpty()) continue;
            attrValues.append(QJsonObject{
                {QStringLiteral("value"),          trimmed},
                {QStringLiteral("marketplace_id"), marketplaceId}
            });
        }
        if (attrValues.isEmpty()) {
            m_lastError = QStringLiteral("No bullet points found in value for SKU %1").arg(sku);
            co_return;
        }
    } else {
        attrValues.append(QJsonObject{
            {QStringLiteral("value"),          value},
            {QStringLiteral("marketplace_id"), marketplaceId}
        });
    }

    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/") + attributeId},
        {QStringLiteral("value"), attrValues}
    };

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     QJsonArray{patch}}
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

    qDebug() << "AmazonWarningsApi: PATCH" << url.toString()
             << "body:" << jsonBody.left(200);
    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    qDebug() << "AmazonWarningsApi: PATCH" << urlPath
             << "HTTP" << httpStatus
             << "response:" << QString::fromUtf8(data.left(300));

    if (httpStatus == 200 || httpStatus == 202) {
        // Amazon can return HTTP 200 with "status":"INVALID" when the value
        // fails their validation (e.g. bullet_point > 700 chars). Check body.
        const QJsonObject respObj     = QJsonDocument::fromJson(data).object();
        const QString     listingStat = respObj.value(QStringLiteral("status")).toString();
        if (listingStat == QStringLiteral("INVALID")) {
            const QJsonArray issues = respObj.value(QStringLiteral("issues")).toArray();
            QStringList msgs;
            for (const QJsonValue &v : issues)
                msgs.append(v.toObject().value(QStringLiteral("message")).toString());
            m_lastError = QStringLiteral("Amazon INVALID for SKU %1 / attr %2: %3")
                              .arg(sku, attributeId, msgs.join(QStringLiteral("; ")));
            qWarning() << "AmazonWarningsApi: PATCH INVALID for" << sku << "/" << attributeId
                       << "issues:" << msgs;
            co_return; // *success remains false
        }
        *success = true;
        co_return;
    }

    m_lastError = QStringLiteral("HTTP %1 for SKU %2 / attr %3: %4")
                      .arg(httpStatus).arg(sku, attributeId, QString::fromUtf8(data.left(300)));
    qWarning() << "AmazonWarningsApi: PATCH failed for" << sku << "/" << attributeId
               << ":" << m_lastError;
    co_return;
}

// ---------------------------------------------------------------------------
// patchListingAttributeJson — like patchListingAttribute but takes a pre-built
// attribute value array (used for structured GPSR attributes).
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonWarningsApi::patchListingAttributeJson(QString marketplaceId,
                                                               QString sku,
                                                               QString productType,
                                                               QString attributeId,
                                                               QJsonArray attrValues,
                                                               bool* success)
{
    *success = false;

    const QString sellerId = sellerIdForMarketplace(marketplaceId);
    if (sellerId.isEmpty()) {
        m_lastError = QStringLiteral("No seller ID configured for marketplace %1").arg(marketplaceId);
        qWarning() << "AmazonWarningsApi:" << m_lastError;
        co_return;
    }

    if (attrValues.isEmpty()) {
        m_lastError = QStringLiteral("Empty attribute value array for SKU %1 / attr %2").arg(sku, attributeId);
        co_return;
    }

    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/") + attributeId},
        {QStringLiteral("value"), attrValues}
    };

    const QJsonObject bodyObj{
        {QStringLiteral("productType"), productType},
        {QStringLiteral("patches"),     QJsonArray{patch}}
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

    qDebug() << "AmazonWarningsApi: PATCH" << url.toString()
             << "body:" << jsonBody.left(200);
    QNetworkReply* reply = _nam()->sendCustomRequest(req, "PATCH", jsonBody);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int httpStatus  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    qDebug() << "AmazonWarningsApi: PATCH" << urlPath
             << "HTTP" << httpStatus
             << "response:" << QString::fromUtf8(data.left(300));

    if (httpStatus == 200 || httpStatus == 202) {
        const QJsonObject respObj     = QJsonDocument::fromJson(data).object();
        const QString     listingStat = respObj.value(QStringLiteral("status")).toString();
        if (listingStat == QStringLiteral("INVALID")) {
            const QJsonArray issues = respObj.value(QStringLiteral("issues")).toArray();
            QStringList msgs;
            for (const QJsonValue &v : issues)
                msgs.append(v.toObject().value(QStringLiteral("message")).toString());
            m_lastError = QStringLiteral("Amazon INVALID for SKU %1 / attr %2: %3")
                              .arg(sku, attributeId, msgs.join(QStringLiteral("; ")));
            qWarning() << "AmazonWarningsApi: PATCH INVALID for" << sku << "/" << attributeId
                       << "issues:" << msgs;
            co_return; // *success remains false
        }
        *success = true;
        co_return;
    }

    m_lastError = QStringLiteral("HTTP %1 for SKU %2 / attr %3: %4")
                      .arg(httpStatus).arg(sku, attributeId, QString::fromUtf8(data.left(300)));
    qWarning() << "AmazonWarningsApi: PATCH failed for" << sku << "/" << attributeId
               << ":" << m_lastError;
    co_return;
}
