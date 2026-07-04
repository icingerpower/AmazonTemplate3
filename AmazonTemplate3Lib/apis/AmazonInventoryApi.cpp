// GCC 13 ICE workaround: coroutines with non-trivially-destructible locals in
// the frame trigger a bug in build_special_member_call (cp/call.cc:11096).
// Forcing O1 avoids the affected code path in the coroutine lowering pass.
// In addition, every coroutine in this translation unit returns
// QCoro::Task<void> and communicates its result via an output parameter, to
// avoid co_awaiting a Task<T> whose T is non-trivially destructible (another
// form of the same GCC 13 bug).
#pragma GCC optimize("O1")
#include "AmazonInventoryApi.h"

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
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QDebug>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroTimer>

#include <zlib.h>
#include <functional>

namespace {
const QString kEuEndpoint = QStringLiteral("sellingpartnerapi-eu.amazon.com");
}

static QByteArray gunzip(const QByteArray &data)
{
    if (data.isEmpty()) return {};
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK)
        return {};
    zs.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    QByteArray out;
    QByteArray buf(65536, '\0');
    int ret = Z_OK;
    do {
        zs.next_out  = reinterpret_cast<Bytef *>(buf.data());
        zs.avail_out = static_cast<uInt>(buf.size());
        ret = inflate(&zs, Z_NO_FLUSH);
        out.append(buf.data(), buf.size() - static_cast<int>(zs.avail_out));
    } while (ret == Z_OK);
    inflateEnd(&zs);
    return out;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AmazonInventoryApi::AmazonInventoryApi(const QString &lwaClientId,
                                       const QString &lwaClientSecret,
                                       const QString &lwaRefreshToken,
                                       const QString &sellerId,
                                       const QString &marketplaceId,
                                       QObject *parent)
    : QObject(parent)
    , m_lwaClientId(lwaClientId)
    , m_lwaClientSecret(lwaClientSecret)
    , m_lwaRefreshToken(lwaRefreshToken)
    , m_sellerId(sellerId)
    , m_marketplaceId(marketplaceId)
{
}

QNetworkAccessManager *AmazonInventoryApi::_nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(30'000); // 30 s — aborts silently-hanging requests
    }
    return m_nam;
}

// ---------------------------------------------------------------------------
// LWA access token (single-region EU variant)
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonInventoryApi::_getAccessToken(QString *out)
{
    if (!m_accessToken.isEmpty() && m_accessTokenExpiry.isValid()
        && QDateTime::currentDateTimeUtc() < m_accessTokenExpiry) {
        *out = m_accessToken;
        co_return;
    }

    if (m_lwaRefreshToken.isEmpty()) {
        qDebug() << "AmazonInventoryApi: no refresh token configured - skipping";
        co_return; // *out stays empty
    }

    QUrl url(QStringLiteral("https://api.amazon.com/auth/o2/token"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem("grant_type", "refresh_token");
    body.addQueryItem("refresh_token", m_lwaRefreshToken);
    body.addQueryItem("client_id", m_lwaClientId);
    body.addQueryItem("client_secret", m_lwaClientSecret);
    const QByteArray payload = body.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply *reply = _nam()->post(req, payload);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject obj = doc.object();
    m_accessToken = obj.value("access_token").toString();
    if (m_accessToken.isEmpty()) {
        const QString errCode = obj.value("error").toString();
        const QString errDesc = obj.value("error_description").toString();
        m_lastError = errDesc.isEmpty() ? errCode : errDesc;
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("LWA token exchange failed");
        qWarning() << "AmazonInventoryApi: LWA token exchange failed:" << m_lastError
                   << "Response:" << QString::fromUtf8(data.left(500));
    } else {
        qDebug() << "AmazonInventoryApi: LWA token obtained, expires_in ="
                 << obj.value("expires_in").toInt();
    }
    const int expiresIn = obj.value("expires_in").toInt(3600);
    const int cacheSecs = qMin(expiresIn - 300, 55 * 60);
    m_accessTokenExpiry = QDateTime::currentDateTimeUtc().addSecs(qMax(cacheSecs, 60));
    *out = m_accessToken;
    co_return;
}

// ---------------------------------------------------------------------------
// FBA inventory
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonInventoryApi::fetchFbaInventory(QStringList skus,
                                                        QList<InventorySummary> *out,
                                                        std::function<void(const QString &)> onProgress)
{
    auto progress = [&onProgress](const QString &msg) {
        if (onProgress) onProgress(msg);
    };

    if (skus.isEmpty())
        co_return;

    QString token;
    co_await _getAccessToken(&token);
    if (token.isEmpty()) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("No LWA access token available");
        qWarning() << "AmazonInventoryApi::fetchFbaInventory:" << m_lastError;
        co_return;
    }

    static const int kChunk = 50; // API limit per call
    for (int start = 0; start < skus.size(); start += kChunk) {
        const QStringList chunk = skus.mid(start, kChunk);

        QUrl url(QStringLiteral("https://%1/fba/inventory/v1/summaries").arg(kEuEndpoint));
        QUrlQuery query;
        query.addQueryItem("details", "true");
        query.addQueryItem("granularityType", "Marketplace");
        query.addQueryItem("granularityId", m_marketplaceId);
        query.addQueryItem("marketplaceIds", m_marketplaceId); // required by the API
        for (const QString &sku : chunk)
            query.addQueryItem("sellerSkus", sku);
        url.setQuery(query);

        for (int attempt = 0; attempt < 3; ++attempt) {
            QNetworkRequest req(url);
            req.setRawHeader("x-amz-access-token", token.toUtf8());
            req.setRawHeader("Accept", "application/json");

            QNetworkReply *reply = _nam()->get(req);
            co_await qCoro(reply).waitForFinished();

            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            reply->deleteLater();

            if (status == 429) {
                qWarning() << "AmazonInventoryApi::fetchFbaInventory: 429 throttled, attempt"
                           << (attempt + 1);
                if (attempt < 2) {
                    QTimer timer;
                    timer.setSingleShot(true);
                    timer.start(2000);
                    co_await qCoro(&timer).waitForTimeout();
                    continue;
                }
                m_lastError = QStringLiteral("FBA inventory throttled (429)");
                co_return;
            }

            if (status != 200) {
                const QString body = QString::fromUtf8(data.left(800));
                m_lastError = QStringLiteral("HTTP %1 — %2").arg(status).arg(body);
                qWarning() << "AmazonInventoryApi::fetchFbaInventory: HTTP" << status
                           << "Response:" << body;
                co_return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonObject payload = doc.object().value("payload").toObject();
            const QJsonArray summaries = payload.value("inventorySummaries").toArray();
            for (const QJsonValue &v : summaries) {
                const QJsonObject o = v.toObject();
                InventorySummary s;
                s.sku  = o.value("sellerSku").toString();
                s.asin = o.value("asin").toString();
                const QJsonObject det = o.value("inventoryDetails").toObject();
                const QJsonObject res = det.value("reservedQuantity").toObject();
                const int fulfillable = det.value("fulfillableQuantity").toInt();
                const int fcTransfer  = res.value("pendingTransshipmentQuantity").toInt();
                const int custOrders  = res.value("pendingCustomerOrderQuantity").toInt();
                const int fcProcessing = res.value("fcProcessingQuantity").toInt();
                const int researching = det.value("researchingQuantity").toObject()
                                           .value("totalResearchingQuantity").toInt();
                // FC-transfer units stay sellable (they're only moving between
                // fulfillment centres), so count them as available.
                s.available = fulfillable + fcTransfer;
                s.inbound   = det.value("inboundWorkingQuantity").toInt()
                            + det.value("inboundShippedQuantity").toInt()
                            + det.value("inboundReceivingQuantity").toInt();
                if (fcTransfer > 0 || custOrders > 0 || fcProcessing > 0 || researching > 0)
                    progress(QStringLiteral("    %1: fulfillable %2 + FC transfer %3 = %4 "
                                            "(customer orders %5, FC processing %6, researching %7)")
                             .arg(s.sku).arg(fulfillable).arg(fcTransfer).arg(s.available)
                             .arg(custOrders).arg(fcProcessing).arg(researching));
                out->append(s);
            }
            break; // chunk done
        }
    }
    co_return;
}

// ---------------------------------------------------------------------------
// FBA inventory via Reports API
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonInventoryApi::fetchFbaInventoryReport(
    QStringList filterSkus,
    QList<InventorySummary> *out,
    std::function<void(const QString &)> onProgress)
{
    auto progress = [&onProgress](const QString &msg) {
        if (onProgress) onProgress(msg);
    };

    // Steps 1+2: create report and poll, with up to 3 attempts total.
    // Amazon occasionally returns FATAL transiently; we retry after 30 s.
    QString token;
    co_await _getAccessToken(&token);
    if (token.isEmpty()) {
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("No LWA access token");
        co_return;
    }

    QString reportDocumentId;
    static const int kMaxReportAttempts = 3;
    for (int outerAttempt = 0; outerAttempt < kMaxReportAttempts; ++outerAttempt) {
        if (outerAttempt > 0) {
            progress(QStringLiteral("  Retrying report creation in 30 s… (attempt %1/%2)")
                .arg(outerAttempt + 1).arg(kMaxReportAttempts));
            QTimer retryTimer;
            retryTimer.setSingleShot(true);
            retryTimer.start(30000);
            co_await qCoro(&retryTimer).waitForTimeout();
            co_await _getAccessToken(&token);
        }

        // Step 1: create report
        QUrl createUrl(QStringLiteral("https://%1/reports/2021-06-30/reports").arg(kEuEndpoint));
        QNetworkRequest createReq(createUrl);
        createReq.setRawHeader("x-amz-access-token", token.toUtf8());
        createReq.setRawHeader("Accept", "application/json");
        createReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        const QJsonDocument bodyDoc(QJsonObject{
            {QStringLiteral("reportType"),     QStringLiteral("GET_FBA_MYI_UNSUPPRESSED_INVENTORY_DATA")},
            {QStringLiteral("marketplaceIds"), QJsonArray{m_marketplaceId}}
        });
        QNetworkReply *createReply = _nam()->post(createReq, bodyDoc.toJson());
        co_await qCoro(createReply).waitForFinished();

        const int createStatus = createReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray createData = createReply->readAll();
        createReply->deleteLater();

        if (createStatus != 202) {
            m_lastError = QStringLiteral("Report create HTTP %1 — %2")
                .arg(createStatus).arg(QString::fromUtf8(createData.left(500)));
            co_return;
        }

        const QString reportId = QJsonDocument::fromJson(createData).object()
            .value(QStringLiteral("reportId")).toString();
        if (reportId.isEmpty()) {
            m_lastError = QStringLiteral("Report create: no reportId in response");
            co_return;
        }
        progress(QStringLiteral("  Report created (id: %1), polling…").arg(reportId));

        // Step 2: poll until DONE (max ~5 min per attempt)
        bool shouldRetry = false;
        QUrl pollUrl(QStringLiteral("https://%1/reports/2021-06-30/reports/%2")
                     .arg(kEuEndpoint, reportId));
        for (int attempt = 0; attempt < 30; ++attempt) {
            QTimer pollTimer;
            pollTimer.setSingleShot(true);
            pollTimer.start(10000);
            co_await qCoro(&pollTimer).waitForTimeout();

            co_await _getAccessToken(&token);
            QNetworkRequest pollReq(pollUrl);
            pollReq.setRawHeader("x-amz-access-token", token.toUtf8());
            pollReq.setRawHeader("Accept", "application/json");

            QNetworkReply *pollReply = _nam()->get(pollReq);
            co_await qCoro(pollReply).waitForFinished();
            const QByteArray pollData = pollReply->readAll();
            pollReply->deleteLater();

            const QJsonObject reportObj = QJsonDocument::fromJson(pollData).object();
            const QString ps = reportObj.value(QStringLiteral("processingStatus")).toString();

            if (ps == QStringLiteral("DONE")) {
                reportDocumentId = reportObj.value(QStringLiteral("reportDocumentId")).toString();
                break;
            }
            if (ps == QStringLiteral("FATAL") || ps == QStringLiteral("CANCELLED")) {
                progress(QStringLiteral("  Report returned %1").arg(ps));
                // FATAL reports usually carry an error document explaining why
                // (e.g. generation quota exceeded) — fetch and log it.
                const QString errDocId = reportObj.value(QStringLiteral("reportDocumentId")).toString();
                if (!errDocId.isEmpty()) {
                    QUrl errDocUrl(QStringLiteral("https://%1/reports/2021-06-30/documents/%2")
                                   .arg(kEuEndpoint, errDocId));
                    QNetworkRequest errDocReq(errDocUrl);
                    errDocReq.setRawHeader("x-amz-access-token", token.toUtf8());
                    errDocReq.setRawHeader("Accept", "application/json");
                    QNetworkReply *errDocReply = _nam()->get(errDocReq);
                    co_await qCoro(errDocReply).waitForFinished();
                    const QJsonObject errDocObj = QJsonDocument::fromJson(errDocReply->readAll()).object();
                    errDocReply->deleteLater();
                    const QString errUrl = errDocObj.value(QStringLiteral("url")).toString();
                    if (!errUrl.isEmpty()) {
                        QUrl errDlUrl(errUrl);
                        QNetworkRequest errDlReq(errDlUrl);
                        QNetworkReply *errDlReply = _nam()->get(errDlReq);
                        co_await qCoro(errDlReply).waitForFinished();
                        QByteArray errContent = errDlReply->readAll();
                        errDlReply->deleteLater();
                        if (errDocObj.value(QStringLiteral("compressionAlgorithm")).toString()
                                .compare(QStringLiteral("GZIP"), Qt::CaseInsensitive) == 0)
                            errContent = gunzip(errContent);
                        progress(QStringLiteral("  FATAL details: %1")
                                 .arg(QString::fromUtf8(errContent.left(400))));
                    }
                }
                m_lastError = QStringLiteral("Report ended with status: %1").arg(ps);
                shouldRetry = (ps == QStringLiteral("FATAL")); // CANCELLED is final
                break;
            }
            progress(QStringLiteral("  Report %1, polling… (%2/30)").arg(ps).arg(attempt + 1));
        }

        if (!reportDocumentId.isEmpty()) break; // success
        if (!shouldRetry) co_return;            // CANCELLED or HTTP error — give up
    }

    if (reportDocumentId.isEmpty()) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("Report timed out or failed after %1 attempts")
                .arg(kMaxReportAttempts);
        co_return;
    }

    // Step 3: get download URL
    co_await _getAccessToken(&token);
    const QUrl docUrl(QStringLiteral("https://%1/reports/2021-06-30/documents/%2")
                      .arg(kEuEndpoint, reportDocumentId));
    QNetworkRequest docReq(docUrl);
    docReq.setRawHeader("x-amz-access-token", token.toUtf8());
    docReq.setRawHeader("Accept", "application/json");

    QNetworkReply *docReply = _nam()->get(docReq);
    co_await qCoro(docReply).waitForFinished();
    const QByteArray docMeta = docReply->readAll();
    docReply->deleteLater();

    const QJsonObject docObj = QJsonDocument::fromJson(docMeta).object();
    const QString downloadUrl     = docObj.value(QStringLiteral("url")).toString();
    const QString compressionAlgo = docObj.value(QStringLiteral("compressionAlgorithm")).toString();

    if (downloadUrl.isEmpty()) {
        m_lastError = QStringLiteral("Report document: no download URL");
        co_return;
    }
    progress(QStringLiteral("  Downloading report document…"));

    // Step 4: download (no auth header — URL is pre-signed)
    // Two-statement form avoids GCC 13 coroutine frame "most vexing parse" ICE.
    QUrl dlUrl(downloadUrl);
    QNetworkRequest dlReq(dlUrl);
    QNetworkReply *dlReply = _nam()->get(dlReq);
    co_await qCoro(dlReply).waitForFinished();
    QByteArray content = dlReply->readAll();
    dlReply->deleteLater();

    if (compressionAlgo.compare(QStringLiteral("GZIP"), Qt::CaseInsensitive) == 0)
        content = gunzip(content);

    // Diagnostic dump: keep the raw TSV so missing-SKU issues can be inspected.
    {
        const QString dumpPath = QStringLiteral("/tmp/fba-myi-report-%1.tsv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        QFile dumpFile(dumpPath);
        if (dumpFile.open(QIODevice::WriteOnly)) {
            dumpFile.write(content);
            dumpFile.close();
            progress(QStringLiteral("  Raw report saved to %1").arg(dumpPath));
        }
    }

    // Step 5: parse TSV
    const QStringList lines = QString::fromUtf8(content).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        m_lastError = QStringLiteral("Report document: empty content");
        co_return;
    }

    const QStringList headers = lines.first().split(QLatin1Char('\t'));
    // Amazon uses "sku" in this report; try both just in case
    int skuCol  = headers.indexOf(QStringLiteral("sku"));
    if (skuCol < 0) skuCol = headers.indexOf(QStringLiteral("seller-sku"));
    const int asinCol = headers.indexOf(QStringLiteral("asin"));
    const int qtyCol  = headers.indexOf(QStringLiteral("afn-fulfillable-quantity"));
    // Full quantity breakdown, for the Inbound column and diagnostics.
    const int warehouseCol   = headers.indexOf(QStringLiteral("afn-warehouse-quantity"));
    const int reservedCol    = headers.indexOf(QStringLiteral("afn-reserved-quantity"));
    const int unsellableCol  = headers.indexOf(QStringLiteral("afn-unsellable-quantity"));
    const int researchingCol = headers.indexOf(QStringLiteral("afn-researching-quantity"));
    const int inbWorkingCol  = headers.indexOf(QStringLiteral("afn-inbound-working-quantity"));
    const int inbShippedCol  = headers.indexOf(QStringLiteral("afn-inbound-shipped-quantity"));
    const int inbReceivingCol = headers.indexOf(QStringLiteral("afn-inbound-receiving-quantity"));

    if (skuCol < 0 || asinCol < 0) {
        m_lastError = QStringLiteral("Report TSV: unexpected column layout (headers: %1)")
            .arg(lines.first().left(200));
        co_return;
    }

    // Case-insensitive filter: the whitelist may come from Temu SKU listings
    // whose casing can differ from the Amazon seller SKU.
    QSet<QString> filterSet;
    for (const QString &s : filterSkus)
        filterSet.insert(s.toLower());
    const int maxCol = qMax(skuCol, qMax(asinCol, qtyCol));

    for (int i = 1; i < lines.size(); ++i) {
        const QStringList cols = lines[i].split(QLatin1Char('\t'));
        if (cols.size() <= maxCol) continue;

        const QString sku = cols[skuCol].trimmed();
        if (!filterSet.isEmpty() && !filterSet.contains(sku.toLower())) continue;

        auto colInt = [&cols](int c) {
            return (c >= 0 && c < cols.size()) ? cols[c].trimmed().toInt() : 0;
        };

        InventorySummary s;
        s.sku       = sku;
        s.asin      = cols[asinCol].trimmed();
        s.available = colInt(qtyCol);
        s.inbound   = colInt(inbWorkingCol) + colInt(inbShippedCol) + colInt(inbReceivingCol);

        // Diagnostic: Seller Central's "On-hand" is afn-warehouse-quantity,
        // which includes reserved/unsellable/researching units. When nothing
        // is fulfillable but on-hand stock exists, show where the units are.
        const int warehouse = colInt(warehouseCol);
        if (s.available == 0 && (warehouse > 0 || s.inbound > 0)) {
            progress(QStringLiteral("  ℹ %1: fulfillable 0 — on-hand %2 (reserved %3, "
                                    "unsellable %4, researching %5), inbound %6 "
                                    "(reserved = pending orders / FC transfer / FC processing)")
                     .arg(sku).arg(warehouse)
                     .arg(colInt(reservedCol)).arg(colInt(unsellableCol))
                     .arg(colInt(researchingCol)).arg(s.inbound));
        }
        out->append(s);
    }
    progress(QStringLiteral("  Parsed %1 row(s) from report (%2 total rows)")
             .arg(out->size()).arg(lines.size() - 1));

    // Diagnose whitelist SKUs absent from the report: exact match failed, so
    // scan every row case-insensitively to distinguish "not in report at all"
    // from "present but the SKU string differs".
    QSet<QString> foundSkus;
    for (const auto &s : *out)
        foundSkus.insert(s.sku.toLower());
    for (const QString &wanted : filterSkus) {
        if (foundSkus.contains(wanted.toLower())) continue;
        QString nearMiss;
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines[i].split(QLatin1Char('\t'));
            if (cols.size() <= skuCol) continue;
            const QString rowSku = cols[skuCol].trimmed();
            if (rowSku.compare(wanted, Qt::CaseInsensitive) == 0
                || rowSku.contains(wanted, Qt::CaseInsensitive)
                || wanted.contains(rowSku, Qt::CaseInsensitive)) {
                nearMiss = rowSku;
                break;
            }
        }
        if (nearMiss.isEmpty())
            progress(QStringLiteral("  ⚠ %1: NOT in the report at all").arg(wanted));
        else
            progress(QStringLiteral("  ⚠ %1: not matched exactly, but report has similar SKU \"%2\"")
                     .arg(wanted, nearMiss));
    }

    // Live cross-check: the MYI report lags reality — notably during FC
    // transfers every afn-* column reads 0 while Seller Central shows stock.
    // Re-verify every whitelist SKU the report shows as 0 or missing against
    // the live FBA Inventory API and prefer its numbers.
    if (!filterSkus.isEmpty()) {
        QHash<QString, int> idxBySkuLower;
        for (int i = 0; i < out->size(); ++i)
            idxBySkuLower.insert(out->at(i).sku.toLower(), i);

        QStringList toCheck;
        for (const QString &wanted : filterSkus) {
            const auto it = idxBySkuLower.constFind(wanted.toLower());
            if (it == idxBySkuLower.constEnd() || out->at(it.value()).available == 0)
                toCheck.append(wanted);
        }

        if (!toCheck.isEmpty()) {
            progress(QStringLiteral("  Cross-checking %1 zero/missing SKU(s) against the live FBA Inventory API…")
                     .arg(toCheck.size()));
            const QString savedError = m_lastError;
            m_lastError.clear();
            QList<InventorySummary> live;
            co_await fetchFbaInventory(toCheck, &live, onProgress);
            for (const auto &ls : live) {
                const auto it = idxBySkuLower.constFind(ls.sku.toLower());
                if (it == idxBySkuLower.constEnd()) {
                    if (ls.available > 0 || ls.inbound > 0) {
                        progress(QStringLiteral("  ℹ %1: not in report — live API says avail %2, inbound %3 (using live)")
                                 .arg(ls.sku).arg(ls.available).arg(ls.inbound));
                        out->append(ls);
                    }
                } else if (ls.available != out->at(it.value()).available) {
                    InventorySummary &s = (*out)[it.value()];
                    progress(QStringLiteral("  ℹ %1: report said avail %2, live API says %3 (using live)")
                             .arg(ls.sku).arg(s.available).arg(ls.available));
                    s.available = ls.available;
                    s.inbound   = qMax(s.inbound, ls.inbound);
                }
            }
            // A cross-check failure must not fail the whole report load.
            m_lastError = savedError;
        }
    }
    co_return;
}

// ---------------------------------------------------------------------------
// Sales units
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonInventoryApi::fetchSalesUnits(QString sku, int days,
                                                       QStringList marketplaceIds, int *out)
{
    *out = -1;

    if (marketplaceIds.isEmpty())
        marketplaceIds = QStringList{m_marketplaceId};

    QString token;
    co_await _getAccessToken(&token);
    if (token.isEmpty()) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("No LWA access token available");
        co_return;
    }

    const QDateTime endDt   = QDateTime::currentDateTimeUtc();
    const QDateTime startDt = endDt.addDays(-days);
    const QString fmt      = QStringLiteral("yyyy-MM-ddThh:mm:ssZ");
    const QString interval = startDt.toString(fmt) + QStringLiteral("--") + endDt.toString(fmt);

    int total   = 0;
    bool anyOk  = false;

    for (const QString &mpId : marketplaceIds) {
        int status = 0;
        QByteArray data;

        for (int attempt = 0; attempt < 3; ++attempt) {
            co_await _getAccessToken(&token);

            QUrl url(QStringLiteral("https://%1/sales/v1/orderMetrics").arg(kEuEndpoint));
            QUrlQuery query;
            query.addQueryItem("marketplaceIds", mpId);
            query.addQueryItem("interval", interval);
            query.addQueryItem("granularity", "Total");
            query.addQueryItem("sku", sku);
            url.setQuery(query);

            QNetworkRequest req(url);
            req.setRawHeader("x-amz-access-token", token.toUtf8());
            req.setRawHeader("Accept", "application/json");

            QNetworkReply *reply = _nam()->get(req);
            co_await qCoro(reply).waitForFinished();

            status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            data = reply->readAll();
            reply->deleteLater();

            if (status == 429) {
                qWarning() << "AmazonInventoryApi::fetchSalesUnits: 429 throttled for" << mpId
                           << "attempt" << (attempt + 1);
                if (attempt < 2) {
                    QTimer timer;
                    timer.setSingleShot(true);
                    timer.start(3000);
                    co_await qCoro(&timer).waitForTimeout();
                    continue;
                }
            }
            break;
        }

        if (status != 200) {
            qWarning() << "AmazonInventoryApi::fetchSalesUnits: HTTP" << status
                       << "for" << mpId << QString::fromUtf8(data.left(200));
            continue; // skip this marketplace, try the rest
        }

        const QJsonArray payload = QJsonDocument::fromJson(data).object()
                                       .value("payload").toArray();
        if (!payload.isEmpty()) {
            total += payload.first().toObject().value("unitCount").toInt();
            anyOk = true;
        } else {
            anyOk = true; // valid response, just 0 sales
        }
    }

    if (anyOk)
        *out = total;
    co_return;
}

QCoro::Task<QJsonArray> AmazonInventoryApi::fetchFulfillmentOrders(const QDateTime &startDateTime)
{
    QString token;
    co_await _getAccessToken(&token);
    if (token.isEmpty()) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("No LWA access token available");
        qWarning() << "AmazonInventoryApi::fetchFulfillmentOrders:" << m_lastError;
        co_return {};
    }

    QUrl url(QStringLiteral("https://%1/fba/outbound/2020-07-01/fulfillmentOrders").arg(kEuEndpoint));
    QUrlQuery query;
    query.addQueryItem("queryStartDate", startDateTime.toString(Qt::ISODate));
    url.setQuery(query);

    for (int attempt = 0; attempt < 3; ++attempt) {
        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("Accept", "application/json");

        QNetworkReply *reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (status == 429) {
            qWarning() << "AmazonInventoryApi::fetchFulfillmentOrders: 429 throttled, attempt"
                       << (attempt + 1);
            if (attempt < 2) {
                QTimer timer;
                timer.setSingleShot(true);
                timer.start(2000);
                co_await qCoro(&timer).waitForTimeout();
                continue;
            }
            m_lastError = QStringLiteral("Fulfillment orders list throttled (429)");
            co_return {};
        }

        if (status != 200) {
            const QString body = QString::fromUtf8(data.left(800));
            if (status == 403) {
                m_lastError = QStringLiteral(
                    "MCF Outbound API returned 403 — the refresh token lacks the "
                    "'Amazon Fulfillment' SP-API role. Fix: 1) check the 'Amazon Fulfillment' "
                    "role in the developer profile AND the app's role list in "
                    "solutionproviderportal.amazon.com, 2) re-authorise the app for the EU "
                    "region to get a new refresh token, 3) update it in Settings.");
            } else {
                m_lastError = QStringLiteral("HTTP %1 — %2").arg(status).arg(body);
            }
            qWarning() << "AmazonInventoryApi::fetchFulfillmentOrders: HTTP" << status
                       << "Response:" << body;
            co_return {};
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        const QJsonObject obj = doc.object();
        const QJsonObject payload = obj.value(QStringLiteral("payload")).toObject();
        co_return payload.value(QStringLiteral("fulfillmentOrders")).toArray();
    }

    co_return {};
}

QCoro::Task<bool> AmazonInventoryApi::createFulfillmentOrder(const QJsonObject &payload)
{
    m_lastError.clear();
    QString token;
    co_await _getAccessToken(&token);
    if (token.isEmpty()) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("No LWA access token available");
        qWarning() << "AmazonInventoryApi::createFulfillmentOrder:" << m_lastError;
        co_return false;
    }

    QUrl url(QStringLiteral("https://%1/fba/outbound/2020-07-01/fulfillmentOrders").arg(kEuEndpoint));
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    for (int attempt = 0; attempt < 3; ++attempt) {
        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("Accept", "application/json");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = _nam()->post(req, body);
        co_await qCoro(reply).waitForFinished();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (status == 429) {
            qWarning() << "AmazonInventoryApi::createFulfillmentOrder: 429 throttled, attempt"
                       << (attempt + 1);
            if (attempt < 2) {
                QTimer timer;
                timer.setSingleShot(true);
                timer.start(2000);
                co_await qCoro(&timer).waitForTimeout();
                continue;
            }
            m_lastError = QStringLiteral("Create fulfillment order throttled (429)");
            co_return false;
        }

        if (status != 200) {
            const QString respBody = QString::fromUtf8(data.left(800));
            m_lastError = QStringLiteral("HTTP %1 — %2").arg(status).arg(respBody);
            qWarning() << "AmazonInventoryApi::createFulfillmentOrder: HTTP" << status
                       << "Response:" << respBody;
            co_return false;
        }

        co_return true;
    }
    co_return false;
}

QCoro::Task<QJsonObject> AmazonInventoryApi::getFulfillmentOrder(const QString &sellerFulfillmentOrderId)
{
    QString token;
    co_await _getAccessToken(&token);
    if (token.isEmpty()) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("No LWA access token available");
        qWarning() << "AmazonInventoryApi::getFulfillmentOrder:" << m_lastError;
        co_return {};
    }

    QUrl url(QStringLiteral("https://%1/fba/outbound/2020-07-01/fulfillmentOrders/%2")
                 .arg(kEuEndpoint)
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(sellerFulfillmentOrderId))));

    for (int attempt = 0; attempt < 3; ++attempt) {
        QNetworkRequest req(url);
        req.setRawHeader("x-amz-access-token", token.toUtf8());
        req.setRawHeader("Accept", "application/json");

        QNetworkReply *reply = _nam()->get(req);
        co_await qCoro(reply).waitForFinished();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (status == 429) {
            qWarning() << "AmazonInventoryApi::getFulfillmentOrder: 429 throttled, attempt"
                       << (attempt + 1);
            if (attempt < 2) {
                QTimer timer;
                timer.setSingleShot(true);
                timer.start(2000);
                co_await qCoro(&timer).waitForTimeout();
                continue;
            }
            m_lastError = QStringLiteral("Get fulfillment order throttled (429)");
            co_return {};
        }

        if (status != 200) {
            const QString body = QString::fromUtf8(data.left(800));
            if (status == 403) {
                m_lastError = QStringLiteral(
                    "MCF Outbound API returned 403 — the refresh token lacks the "
                    "'Amazon Fulfillment' SP-API role. Fix: 1) check the 'Amazon Fulfillment' "
                    "role in the developer profile AND the app's role list in "
                    "solutionproviderportal.amazon.com, 2) re-authorise the app for the EU "
                    "region to get a new refresh token, 3) update it in Settings.");
            } else {
                m_lastError = QStringLiteral("HTTP %1 — %2").arg(status).arg(body);
            }
            qWarning() << "AmazonInventoryApi::getFulfillmentOrder: HTTP" << status
                       << "Response:" << body;
            co_return {};
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        const QJsonObject obj = doc.object();
        co_return obj.value(QStringLiteral("payload")).toObject();
    }

    co_return {};
}
