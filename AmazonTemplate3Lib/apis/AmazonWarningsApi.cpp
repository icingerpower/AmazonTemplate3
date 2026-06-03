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

    // -------------------------------------------------------------------
    // Step 1: enumerate FBA SKUs via GET_FBA_MYI_UNSUPPRESSED_INVENTORY_DATA
    // report — FBA-only, always includes seller SKUs, no per-page rate limits.
    // -------------------------------------------------------------------
    emit logMessage(QStringLiteral("Step 1: Requesting FBA inventory report…"));
    emit progressChanged(0, 0);

    QHash<QString, QString> asinToSku;

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
            if (!sku.isEmpty() && !asin.isEmpty() && !asinToSku.contains(asin))
                asinToSku.insert(asin, sku);
        }

        emit logMessage(QStringLiteral("Step 1 complete: %1 FBA listing(s) found.").arg(asinToSku.size()));
        qDebug() << "AmazonWarningsApi: step1 complete via FBA report, ASINs=" << asinToSku.size();
    }

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

        // --- Extract title + mainImage from summaries[] matching marketplaceId ---
        QString itemTitle;
        QString mainImageUrl;
        {
            const QJsonArray summaries = root.value(QStringLiteral("summaries")).toArray();
            for (const QJsonValue& sv : summaries) {
                const QJsonObject sobj = sv.toObject();
                if (sobj.value(QStringLiteral("marketplaceId")).toString() == marketplaceId) {
                    itemTitle    = sobj.value(QStringLiteral("itemName")).toString();
                    mainImageUrl = sobj.value(QStringLiteral("mainImage")).toObject()
                                       .value(QStringLiteral("link")).toString();
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

    // Build patch payload: {"productType":pt, "patches":[{"op":"replace",
    //   "path":"/attributes/<attributeId>",
    //   "value":[{"value":<value>, "marketplace_id":<mp>}]}]}
    const QJsonObject attrValue{
        {QStringLiteral("value"),          value},
        {QStringLiteral("marketplace_id"), marketplaceId}
    };

    const QJsonObject patch{
        {QStringLiteral("op"),    QStringLiteral("replace")},
        {QStringLiteral("path"),  QStringLiteral("/attributes/") + attributeId},
        {QStringLiteral("value"), QJsonArray{attrValue}}
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
        *success = true;
        co_return;
    }

    m_lastError = QStringLiteral("HTTP %1 for SKU %2 / attr %3: %4")
                      .arg(httpStatus).arg(sku, attributeId, QString::fromUtf8(data.left(300)));
    qWarning() << "AmazonWarningsApi: PATCH failed for" << sku << "/" << attributeId
               << ":" << m_lastError;
    co_return;
}
