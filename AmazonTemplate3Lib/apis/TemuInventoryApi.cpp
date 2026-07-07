#pragma GCC optimize("O1")
#include "TemuInventoryApi.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <QNetworkProxy>

#include <QCoro/QCoroNetworkReply>

namespace {
static QString jsonValueToStringForSign(const QJsonValue &val)
{
    if (val.isString()) {
        return val.toString();
    } else if (val.isDouble()) {
        double d = val.toDouble();
        if (d == static_cast<int>(d)) {
            return QString::number(static_cast<int>(d));
        } else {
            return QString::number(d, 'f', 6).replace(QRegularExpression(QStringLiteral("\\.?0+$")), QString());
        }
    } else if (val.isBool()) {
        return val.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    } else if (val.isNull()) {
        return QStringLiteral("null");
    } else if (val.isArray()) {
        QJsonDocument doc(val.toArray());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    } else if (val.isObject()) {
        QJsonDocument doc(val.toObject());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    return QString();
}

static QString generateSign(const QJsonObject &params, const QString &appSecret)
{
    QStringList keys = params.keys();
    std::sort(keys.begin(), keys.end());

    QString baseString;
    for (const QString &key : keys) {
        baseString += key + jsonValueToStringForSign(params.value(key));
    }

    QString signString = appSecret + baseString + appSecret;
    QByteArray hash = QCryptographicHash::hash(signString.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(hash.toHex().toUpper());
}
} // namespace

TemuInventoryApi::TemuInventoryApi(QString appKey, QString appSecret, QString accessToken, QObject *parent)
    : QObject(parent)
    , m_appKey(std::move(appKey))
    , m_appSecret(std::move(appSecret))
    , m_accessToken(std::move(accessToken))
{
}

TemuInventoryApi::TemuInventoryApi(QString appKey, QString appSecret, QString accessToken,
                                   QString proxyHost, int proxyPort, QString proxyUser, QString proxyPassword,
                                   QObject *parent)
    : QObject(parent)
    , m_appKey(std::move(appKey))
    , m_appSecret(std::move(appSecret))
    , m_accessToken(std::move(accessToken))
    , m_proxyHost(std::move(proxyHost))
    , m_proxyPort(proxyPort)
    , m_proxyUser(std::move(proxyUser))
    , m_proxyPassword(std::move(proxyPassword))
{
}

QNetworkAccessManager *TemuInventoryApi::_nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(30'000); // 30 s
        if (!m_proxyHost.isEmpty()) {
            QNetworkProxy proxy(QNetworkProxy::HttpProxy, m_proxyHost, m_proxyPort, m_proxyUser, m_proxyPassword);
            m_nam->setProxy(proxy);
        }
    }
    return m_nam;
}

QCoro::Task<void> TemuInventoryApi::_postRequest(const QString &method, const QJsonObject &businessParams, QJsonObject *resultOut)
{
    *resultOut = QJsonObject();
    m_lastError.clear();

    QJsonObject reqObj = businessParams;
    QString apiVersion = QStringLiteral("V1");
    if (reqObj.contains(QStringLiteral("_version_override"))) {
        apiVersion = reqObj.value(QStringLiteral("_version_override")).toString();
        reqObj.remove(QStringLiteral("_version_override"));
    } else {
        QRegularExpression verReg(QStringLiteral("\\.v([1-9]+)\\."));
        auto match = verReg.match(method);
        if (match.hasMatch()) {
            apiVersion = QStringLiteral("V") + match.captured(1).toUpper();
        } else if (method == QStringLiteral("bg.goods.sales.get")) {
            apiVersion = QStringLiteral("V2");
        }
    }

    reqObj.insert(QStringLiteral("type"), method);
    reqObj.insert(QStringLiteral("app_key"), m_appKey);
    reqObj.insert(QStringLiteral("access_token"), m_accessToken);
    reqObj.insert(QStringLiteral("data_type"), QStringLiteral("JSON"));
    reqObj.insert(QStringLiteral("version"), apiVersion);

    qint64 timestamp = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    reqObj.insert(QStringLiteral("timestamp"), timestamp);

    // Generate sign
    QString sign = generateSign(reqObj, m_appSecret);
    reqObj.insert(QStringLiteral("sign"), sign);

    qDebug() << "Temu API POST request:" << method << "Payload:" << QJsonDocument(reqObj).toJson(QJsonDocument::Compact);

    QUrl url(QStringLiteral("https://openapi-b-eu.temu.com/openapi/router"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonDocument doc(reqObj);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkReply *reply = _nam()->post(req, payload);
    co_await qCoro(reply).waitForFinished();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    qDebug() << "Temu API POST response status:" << status << "Data size:" << data.size();
    if (data.size() < 4000) {
        qDebug() << "Temu API POST response body:" << data;
    } else {
        qDebug() << "Temu API POST response body (truncated):" << data.left(4000) << "...";
    }

    if (status != 200) {
        m_lastError = QStringLiteral("HTTP %1: %2").arg(status).arg(QString::fromUtf8(data.left(800)));
        co_return;
    }

    QJsonParseError parseErr;
    QJsonDocument respDoc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        m_lastError = QStringLiteral("JSON parse error: %1").arg(parseErr.errorString());
        qDebug() << "Temu API JSON parse error:" << m_lastError;
        co_return;
    }

    QJsonObject respObj = respDoc.object();
    QJsonObject responseVal = respObj.value(QStringLiteral("response")).toObject();
    if (responseVal.isEmpty()) {
        responseVal = respObj;
    }

    bool success = responseVal.value(QStringLiteral("success")).toBool(false);
    if (!success) {
        QString errCode = responseVal.value(QStringLiteral("errorCode")).toVariant().toString();
        QString errMsg = responseVal.value(QStringLiteral("errorMsg")).toString();
        m_lastError = QStringLiteral("API Error %1: %2").arg(errCode, errMsg);
        qDebug() << "Temu API Error returned:" << m_lastError;
        co_return;
    }

    // Some endpoints (bg.logistics.companies.get) return "result" as an
    // array — wrap it so callers can read it under the "result" key.
    const QJsonValue resultVal = responseVal.value(QStringLiteral("result"));
    if (resultVal.isArray()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("result"), resultVal.toArray());
        *resultOut = wrap;
    } else {
        *resultOut = resultVal.toObject();
    }
    qDebug() << "Temu API success. Result keys:" << resultOut->keys();
    co_return;
}

QCoro::Task<void> TemuInventoryApi::_scanSkuStocks(QHash<QString, SkuStockInfo> *out,
                                                    std::function<void(const QString&)> onProgress)
{
    out->clear();

    auto log = [&onProgress](const QString &msg) {
        if (onProgress) onProgress(msg);
        qDebug() << "Temu _scanSkuStocks:" << msg;
    };

    // bg.local.goods.sku.list.query requires skuSearchType as an INTEGER.
    // Verified live (2026-07): type 2 = in-stock SKUs, type 3 = out-of-stock
    // SKUs — both return real per-SKU "stock" plus skuSn/skuId/goodsId.
    // Type 7 lists every SKU but its "stock" is always 0 (unusable).
    // This is the ONLY place per-SKU stock is exposed; the goods-level
    // "quantity" from bg.local.goods.list.query disagrees with the per-SKU
    // values for multi-SKU goods, so don't use it.
    const QList<int> searchTypes = { 2, 3 };
    const int pageSize = 50;
    const int kMaxPages = 50; // safety net: some search types repeat pages forever

    for (int searchType : searchTypes) {
        int typeCount = 0;
        for (int page = 1; page <= kMaxPages; ++page) {
            QJsonObject params;
            params.insert(QStringLiteral("pageNo"), page);
            params.insert(QStringLiteral("pageSize"), pageSize);
            params.insert(QStringLiteral("skuSearchType"), searchType);

            QJsonObject result;
            m_lastError.clear();
            co_await _postRequest(QStringLiteral("bg.local.goods.sku.list.query"), params, &result);
            if (!m_lastError.isEmpty()) {
                log(QStringLiteral("  ✗ SKU scan failed (skuSearchType=%1 page=%2): %3")
                    .arg(searchType).arg(page).arg(m_lastError));
                co_return;
            }

            const QJsonArray skuList = result.value(QStringLiteral("skuList")).toArray();
            if (skuList.isEmpty())
                break;

            for (const QJsonValue &sVal : skuList) {
                const QJsonObject s = sVal.toObject();
                const QString skuSn = s.value(QStringLiteral("skuSn")).toString();
                if (skuSn.isEmpty())
                    continue;
                SkuStockInfo info;
                info.skuSn   = skuSn;
                info.goodsId = s.value(QStringLiteral("goodsId")).toVariant().toLongLong();
                info.skuId   = s.value(QStringLiteral("skuId")).toVariant().toLongLong();
                info.stock   = s.value(QStringLiteral("stock")).toInt(0);
                out->insert(skuSn.toLower(), info);
                ++typeCount;
            }

            if (skuList.size() < pageSize)
                break;
        }
        qDebug() << "Temu _scanSkuStocks: skuSearchType" << searchType
                 << "->" << typeCount << "SKUs";
    }

    log(QStringLiteral("  → SKU scan complete: %1 SKU(s) with stock info").arg(out->size()));
    co_return;
}

QCoro::Task<void> TemuInventoryApi::fetchInventory(QStringList skus, QHash<QString, int> *out)
{
    qDebug() << "Temu fetchInventory starting for SKUs:" << skus;
    out->clear();
    // Do NOT pre-fill with 0: a SKU absent from the store must stay absent
    // from *out so the table shows "-" instead of a fake 0.

    QHash<QString, SkuStockInfo> stocks;
    co_await _scanSkuStocks(&stocks);
    if (!m_lastError.isEmpty())
        co_return;

    if (skus.isEmpty()) {
        // No filter: return every SKU listed in this store (original casing).
        for (const auto &info : stocks)
            out->insert(info.skuSn, info.stock);
    } else {
        for (const QString &s : skus) {
            const auto it = stocks.constFind(s.toLower());
            if (it == stocks.constEnd())
                continue;
            out->insert(s, it->stock);
            qDebug() << "Temu fetchInventory: matched SKU" << s << "stock" << it->stock;
        }
    }

    qDebug() << "Temu fetchInventory finished. Final inventory map:" << *out;
    co_return;
}

QCoro::Task<void> TemuInventoryApi::fetchSales(QStringList skus, int days, QHash<QString, int> *out)
{
    qDebug() << "Temu fetchSales starting for SKUs:" << skus << "over" << days << "days";
    out->clear();
    for (const QString &sku : skus) {
        out->insert(sku, 0); // Initialize all to 0
    }

    qint64 nowSecs = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    qint64 startSecs = QDateTime::currentDateTimeUtc().addDays(-days).toSecsSinceEpoch();

    int page = 1;
    const int pageSize = 100;
    bool hasMore = true;

    while (hasMore) {
        QJsonObject businessParams;
        businessParams.insert(QStringLiteral("pageNo"), page);
        businessParams.insert(QStringLiteral("pageNumber"), page);
        businessParams.insert(QStringLiteral("page"), page);
        businessParams.insert(QStringLiteral("pageSize"), pageSize);
        businessParams.insert(QStringLiteral("createAfter"), startSecs);
        businessParams.insert(QStringLiteral("createBefore"), nowSecs);

        QJsonObject result;
        co_await _postRequest(QStringLiteral("bg.order.list.v2.get"), businessParams, &result);

        if (!m_lastError.isEmpty()) {
            qDebug() << "Temu fetchSales error on page" << page << ":" << m_lastError;
            break;
        }

        QJsonArray orderList = result.value(QStringLiteral("orderList")).toArray();
        if (orderList.isEmpty()) orderList = result.value(QStringLiteral("order_list")).toArray();
        if (orderList.isEmpty()) orderList = result.value(QStringLiteral("pageItems")).toArray();
        if (orderList.isEmpty()) orderList = result.value(QStringLiteral("page_items")).toArray();
        if (orderList.isEmpty()) orderList = result.value(QStringLiteral("list")).toArray();
        if (orderList.isEmpty()) {
            if (result.value(QStringLiteral("data")).isArray()) {
                orderList = result.value(QStringLiteral("data")).toArray();
            } else if (result.value(QStringLiteral("data")).isObject()) {
                QJsonObject dataObj = result.value(QStringLiteral("data")).toObject();
                orderList = dataObj.value(QStringLiteral("orderList")).toArray();
                if (orderList.isEmpty()) orderList = dataObj.value(QStringLiteral("order_list")).toArray();
                if (orderList.isEmpty()) orderList = dataObj.value(QStringLiteral("pageItems")).toArray();
                if (orderList.isEmpty()) orderList = dataObj.value(QStringLiteral("page_items")).toArray();
                if (orderList.isEmpty()) orderList = dataObj.value(QStringLiteral("list")).toArray();
            }
        }

        qDebug() << "Temu fetchSales page" << page << "retrieved" << orderList.size() << "orders.";

        if (orderList.isEmpty()) {
            hasMore = false;
            break;
        }

        for (const QJsonValue &parentVal : orderList) {
            QJsonObject parentObj = parentVal.toObject();

            // parentObj is a parent order map from pageItems.
            // Retrieve its inner orderList (which contains the sub-orders / items).
            QJsonArray subOrders = parentObj.value(QStringLiteral("orderList")).toArray();
            if (subOrders.isEmpty()) subOrders = parentObj.value(QStringLiteral("order_list")).toArray();
            if (subOrders.isEmpty()) {
                // Flat structure fallback
                subOrders.append(parentObj);
            }

            for (const QJsonValue &subVal : subOrders) {
                QJsonObject itemObj = subVal.toObject();

                // Try top-level SKU in order item
                QString sku = itemObj.value(QStringLiteral("skuExtCode")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("extCode")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("skuCode")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("sku")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("productSkuId")).toVariant().toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("outerSkuId")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("outer_sku_id")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("sellerSkuCode")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("seller_sku_code")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("outSkuCode")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("out_sku_code")).toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("skuId")).toVariant().toString();
                if (sku.isEmpty()) sku = itemObj.value(QStringLiteral("sku_id")).toVariant().toString();

                // Try nested productList in order item
                QJsonArray prodList = itemObj.value(QStringLiteral("productList")).toArray();
                if (prodList.isEmpty()) prodList = itemObj.value(QStringLiteral("product_list")).toArray();

                QStringList nestedSkus;
                for (const QJsonValue &prodVal : prodList) {
                    QJsonObject prodObj = prodVal.toObject();
                    QString pSku = prodObj.value(QStringLiteral("extCode")).toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("skuExtCode")).toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("skuCode")).toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("sku")).toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("productSkuId")).toVariant().toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("outerSkuId")).toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("sellerSkuCode")).toString();
                    if (pSku.isEmpty()) pSku = prodObj.value(QStringLiteral("productId")).toVariant().toString();
                    if (!pSku.isEmpty()) {
                        nestedSkus.append(pSku);
                    }
                }

                int qty = itemObj.value(QStringLiteral("quantity")).toInt();
                if (qty <= 0) qty = itemObj.value(QStringLiteral("goodsCount")).toInt();
                if (qty <= 0) qty = itemObj.value(QStringLiteral("count")).toInt();
                if (qty <= 0) qty = 1;

                QString orderSn = itemObj.value(QStringLiteral("orderSn")).toString();
                if (orderSn.isEmpty()) orderSn = parentObj.value(QStringLiteral("parentOrderSn")).toString();

                if (!sku.isEmpty()) {
                    qDebug() << "Temu fetchSales: found SKU" << sku << "ordered quantity" << qty << "in order" << orderSn;
                    QString matchedSku;
                    for (const QString &s : skus) {
                        if (s.compare(sku, Qt::CaseInsensitive) == 0) {
                            matchedSku = s;
                            break;
                        }
                    }
                    if (!matchedSku.isEmpty()) {
                        (*out)[matchedSku] += qty;
                    }
                }

                for (const QString &nSku : nestedSkus) {
                    qDebug() << "Temu fetchSales: found nested SKU" << nSku << "ordered quantity" << qty << "in order" << orderSn;
                    QString matchedSku;
                    for (const QString &s : skus) {
                        if (s.compare(nSku, Qt::CaseInsensitive) == 0) {
                            matchedSku = s;
                            break;
                        }
                    }
                    if (!matchedSku.isEmpty()) {
                        (*out)[matchedSku] += qty;
                    }
                }
            }
        }

        if (orderList.size() < pageSize) {
            hasMore = false;
        } else {
            page++;
        }
    }

    qDebug() << "Temu fetchSales finished. Final sales map:" << *out;
    co_return;
}

QCoro::Task<void> TemuInventoryApi::updateInventory(const QHash<QString,int> &qtyBySku,
                                                     std::function<void(const QString&)> onProgress)
{
    m_lastError.clear();

    auto log = [&onProgress](const QString &msg) {
        if (onProgress) onProgress(msg);
        qDebug() << "Temu updateInventory:" << msg;
    };

    // Phase 1: per-SKU stock scan (skuSn → goodsId/skuId/current stock).
    // Works for multi-SKU goods too — stock is read and edited per SKU.
    log(QStringLiteral("  Scanning SKU stock…"));
    QHash<QString, SkuStockInfo> stocks;
    co_await _scanSkuStocks(&stocks, onProgress);
    if (!m_lastError.isEmpty())
        co_return;

    // Phase 2: push a stock diff for each target SKU found in the store.
    int updatedCount = 0;
    int notFoundCount = 0;

    for (auto it = qtyBySku.constBegin(); it != qtyBySku.constEnd(); ++it) {
        const QString &sku  = it.key();
        const int targetQty = it.value();

        const auto sIt = stocks.constFind(sku.toLower());
        if (sIt == stocks.constEnd()) {
            log(QStringLiteral("  ⚠ %1: not listed in this store, skipping").arg(sku));
            ++notFoundCount;
            continue;
        }
        const SkuStockInfo &info = *sIt;

        // bg.local.goods.stock.edit is DIFF-based: stockDiff = target − current.
        // Verified live (2026-07): {goodsId, skuStockChangeList:[{skuId, stockDiff}]}.
        // A stockDiff of 0 returns success=true but operateResult=false, so skip no-ops.
        const int diff = targetQty - info.stock;
        if (diff == 0) {
            log(QStringLiteral("  = %1 already at %2 units, nothing to do")
                .arg(sku).arg(targetQty));
            ++updatedCount;
            continue;
        }

        log(QStringLiteral("  → %1 (goodsId=%2, skuId=%3): %4 → %5 (diff %6%7)")
            .arg(sku)
            .arg(info.goodsId).arg(info.skuId)
            .arg(info.stock).arg(targetQty)
            .arg(diff > 0 ? QStringLiteral("+") : QString()).arg(diff));

        QJsonObject change;
        change.insert(QStringLiteral("skuId"), info.skuId);
        change.insert(QStringLiteral("stockDiff"), diff);
        QJsonArray changeList;
        changeList.append(change);

        QJsonObject editParams;
        editParams.insert(QStringLiteral("goodsId"), info.goodsId);
        editParams.insert(QStringLiteral("skuStockChangeList"), changeList);

        QJsonObject editResult;
        m_lastError.clear();
        co_await _postRequest(QStringLiteral("bg.local.goods.stock.edit"), editParams, &editResult);

        if (!m_lastError.isEmpty()) {
            log(QStringLiteral("  ✗ stock.edit failed for %1: %2").arg(sku, m_lastError));
            continue;
        }

        // success=true is not enough — check operateResult + per-SKU status.
        const bool operateResult = editResult.value(QStringLiteral("operateResult")).toBool(false);
        bool skuOk = false;
        QString skuErr;
        const QJsonArray statusList =
            editResult.value(QStringLiteral("skuStockEditStatusInfoList")).toArray();
        for (const QJsonValue &stVal : statusList) {
            const QJsonObject st = stVal.toObject();
            if (st.value(QStringLiteral("skuId")).toVariant().toLongLong() == info.skuId) {
                skuOk   = st.value(QStringLiteral("stockEditStatus")).toBool(false);
                skuErr  = st.value(QStringLiteral("errorMsg")).toString();
                if (skuErr.isEmpty())
                    skuErr = st.value(QStringLiteral("errorCode")).toVariant().toString();
            }
        }

        if (operateResult && skuOk) {
            log(QStringLiteral("  ✓ %1 updated to %2 units").arg(sku).arg(targetQty));
            ++updatedCount;
        } else {
            log(QStringLiteral("  ✗ %1: API accepted the call but edit was rejected "
                               "(operateResult=%2, skuStatus=%3%4) — raw: %5")
                .arg(sku)
                .arg(operateResult ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(skuOk ? QStringLiteral("ok") : QStringLiteral("failed"))
                .arg(skuErr.isEmpty() ? QString() : QStringLiteral(", error: ") + skuErr)
                .arg(QString::fromUtf8(QJsonDocument(editResult).toJson(QJsonDocument::Compact).left(400))));
        }
    }

    log(QStringLiteral("  → Done: %1 SKU(s) up to date, %2 not listed in this store")
        .arg(updatedCount).arg(notFoundCount));
    co_return;
}

QCoro::Task<QList<TemuInventoryApi::TemuOrder>> TemuInventoryApi::fetchUnshippedOrders()
{
    QList<TemuOrder> out;
    qint64 nowSecs = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    // Go back 30 days to find unshipped orders
    qint64 startSecs = QDateTime::currentDateTimeUtc().addDays(-30).toSecsSinceEpoch();

    int page = 1;
    const int pageSize = 100;
    bool hasMore = true;

    while (hasMore) {
        QJsonObject businessParams;
        businessParams.insert(QStringLiteral("pageNo"), page);
        businessParams.insert(QStringLiteral("pageNumber"), page);
        businessParams.insert(QStringLiteral("page"), page);
        businessParams.insert(QStringLiteral("pageSize"), pageSize);
        businessParams.insert(QStringLiteral("createAfter"), startSecs);
        businessParams.insert(QStringLiteral("createBefore"), nowSecs);
        // Request pending shipment orders specifically
        businessParams.insert(QStringLiteral("orderStatus"), 2);
        QJsonArray statusList;
        statusList.append(2);
        businessParams.insert(QStringLiteral("orderStatusList"), statusList);

        QJsonObject result;
        co_await _postRequest(QStringLiteral("bg.order.list.v2.get"), businessParams, &result);

        if (!m_lastError.isEmpty()) {
            qWarning() << "Temu fetchUnshippedOrders error on page" << page << ":" << m_lastError;
            break;
        }

        QJsonArray orderList = result.value(QStringLiteral("orderList")).toArray();
        if (orderList.isEmpty()) orderList = result.value(QStringLiteral("order_list")).toArray();
        if (orderList.isEmpty()) orderList = result.value(QStringLiteral("pageItems")).toArray();

        if (orderList.isEmpty()) {
            hasMore = false;
            break;
        }

        for (const QJsonValue &parentVal : orderList) {
            QJsonObject parentObj = parentVal.toObject();
            
            QJsonArray subOrders = parentObj.value(QStringLiteral("orderList")).toArray();
            if (subOrders.isEmpty()) subOrders = parentObj.value(QStringLiteral("order_list")).toArray();
            if (subOrders.isEmpty()) {
                subOrders.append(parentObj);
            }

            for (const QJsonValue &subVal : subOrders) {
                QJsonObject itemObj = subVal.toObject();

                // Read items
                TemuOrder order;
                order.orderSn = itemObj.value(QStringLiteral("orderSn")).toString();
                order.parentOrderSn = itemObj.value(QStringLiteral("parentOrderSn")).toString();
                if (order.parentOrderSn.isEmpty()) {
                    order.parentOrderSn = parentObj.value(QStringLiteral("parentOrderMap")).toObject()
                                                .value(QStringLiteral("parentOrderSn")).toString();
                }
                if (order.parentOrderSn.isEmpty()) {
                    order.parentOrderSn = order.orderSn;
                }

                int parentStatus = parentObj.value(QStringLiteral("parentOrderMap")).toObject()
                                           .value(QStringLiteral("parentOrderStatus")).toInt();
                int itemStatus = itemObj.value(QStringLiteral("orderStatus")).toInt();

                int finalStatus = (itemStatus > 0 ? itemStatus : parentStatus);
                if (finalStatus != 2) {
                    continue;
                }

                order.status = QString::number(finalStatus);
                order.quantity = itemObj.value(QStringLiteral("quantity")).toInt();
                if (order.quantity <= 0) order.quantity = itemObj.value(QStringLiteral("goodsCount")).toInt();

                // shipment.v2.confirm expects the order-level goodsId/skuId
                // (productList's productId/productSkuId are different IDs and
                // trigger 20006 "The order and Order item ID do not match").
                order.goodsId = itemObj.value(QStringLiteral("goodsId")).toVariant().toLongLong();
                order.skuId = itemObj.value(QStringLiteral("skuId")).toVariant().toLongLong();

                QJsonArray prodList = itemObj.value(QStringLiteral("productList")).toArray();
                if (!prodList.isEmpty()) {
                    QJsonObject prodObj = prodList.first().toObject();
                    if (order.goodsId == 0)
                        order.goodsId = prodObj.value(QStringLiteral("productId")).toVariant().toLongLong();
                    if (order.skuId == 0)
                        order.skuId = prodObj.value(QStringLiteral("productSkuId")).toVariant().toLongLong();
                    order.sku = prodObj.value(QStringLiteral("extCode")).toString();
                    if (order.sku.isEmpty()) order.sku = prodObj.value(QStringLiteral("skuExtCode")).toString();
                }
                if (order.sku.isEmpty()) {
                    order.sku = itemObj.value(QStringLiteral("skuExtCode")).toString();
                }

                out.append(order);
            }
        }

        if (orderList.size() < pageSize) {
            hasMore = false;
        } else {
            page++;
        }
    }

    co_return out;
}

QCoro::Task<void> TemuInventoryApi::fetchOrderAddress(const QString &parentOrderSn, QJsonObject *out)
{
    *out = QJsonObject();
    QJsonObject params;
    params.insert(QStringLiteral("parentOrderSn"), parentOrderSn);
    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.order.shippinginfo.v2.get"), params, &result);
    if (m_lastError.isEmpty())
        *out = result;
    co_return;
}

QCoro::Task<QJsonArray> TemuInventoryApi::fetchLogisticsCompanies()
{
    // bg.shiporder.logistics.get does not exist on the EU gateway (3000003).
    // bg.logistics.companies.get requires an int regionId and returns
    // result: [{logisticsServiceProviderId, logisticsServiceProviderName,
    // logisticsBrandName}]. Provider IDs are globally consistent, so merge
    // the lists of the regions that carry the carriers we care about
    // (swiship/Amazon variants live in 69/76/90/98; EU nationals in 13/20/32).
    static const int kRegionIds[] = {13, 20, 32, 69, 76, 90, 98};

    QJsonArray merged;
    QSet<qint64> seen;
    QString firstError;
    for (int regionId : kRegionIds) {
        m_lastError.clear();
        QJsonObject businessParams;
        businessParams.insert(QStringLiteral("regionId"), regionId);
        QJsonObject result;
        co_await _postRequest(QStringLiteral("bg.logistics.companies.get"), businessParams, &result);
        if (!m_lastError.isEmpty()) {
            if (firstError.isEmpty()) firstError = m_lastError;
            continue;
        }
        const QJsonArray list = result.value(QStringLiteral("result")).toArray();
        for (const QJsonValue &v : list) {
            const QJsonObject c = v.toObject();
            const qint64 id = c.value(QStringLiteral("logisticsServiceProviderId")).toVariant().toLongLong();
            if (id == 0 || seen.contains(id))
                continue;
            seen.insert(id);
            merged.append(c);
        }
    }

    m_lastError.clear();
    if (merged.isEmpty() && !firstError.isEmpty())
        m_lastError = firstError;
    co_return merged;
}

QCoro::Task<bool> TemuInventoryApi::shipOrder(const QString &parentOrderSn, const QString &orderSn,
                                              qint64 goodsId, qint64 skuId, int quantity,
                                              const QString &trackingNumber, const QString &carrierName,
                                              const QString &countryCode,
                                              std::function<void(const QString&)> onProgress)
{
    m_lastError.clear();

    // 1. Find matched carrier ID
    qint64 carrierId = 0;
    QString matchedName;
    QStringList availableNames;
    QList<QPair<QString, qint64>> carriers; // name → providerId
    QJsonArray companies = co_await fetchLogisticsCompanies();
    for (const QJsonValue &cVal : companies) {
        QJsonObject cObj = cVal.toObject();
        qint64 id = cObj.value(QStringLiteral("logisticsServiceProviderId")).toVariant().toLongLong();
        QString name = cObj.value(QStringLiteral("logisticsBrandName")).toString();
        if (name.isEmpty()) name = cObj.value(QStringLiteral("logisticsServiceProviderName")).toString();
        if (id == 0 || name.isEmpty())
            continue;
        availableNames.append(name);
        carriers.append({name, id});

        if (carrierId == 0 && !carrierName.isEmpty() &&
            (name.compare(carrierName, Qt::CaseInsensitive) == 0 ||
             name.contains(carrierName, Qt::CaseInsensitive) ||
             carrierName.contains(name, Qt::CaseInsensitive))) {
            carrierId = id;
            matchedName = name;
        }
    }

    // Amazon MCF ships as "Amazon Logistics"; on Temu the matching carrier is
    // "swiship(CC)" (Swiship is Amazon's MCF tracking site), else "Amazon
    // shiping(CC)" / "Amazon Shipping (CC)".
    if (carrierId == 0 && carrierName.contains(QLatin1String("amazon"), Qt::CaseInsensitive)
        && !countryCode.isEmpty()) {
        const QString ccTag = QStringLiteral("(%1)").arg(countryCode.toLower());
        for (const auto &[name, id] : carriers) {
            const QString nl = name.toLower();
            if (nl.contains(QLatin1String("swiship")) && nl.contains(ccTag)) {
                carrierId = id; matchedName = name; break;
            }
        }
        if (carrierId == 0) {
            for (const auto &[name, id] : carriers) {
                const QString nl = name.toLower();
                if (nl.contains(QLatin1String("amazon")) && nl.contains(ccTag)) {
                    carrierId = id; matchedName = name; break;
                }
            }
        }
    }

    if (carrierId != 0) {
        if (onProgress)
            onProgress(QStringLiteral("Carrier '%1' matched to Temu carrier '%2' (ID %3)")
                           .arg(carrierName, matchedName).arg(carrierId));
    } else {
        // No match — refuse rather than shipping with a wrong carrier.
        m_lastError = companies.isEmpty()
            ? QStringLiteral("Temu returned no logistics companies (%1)").arg(m_lastError)
            : QStringLiteral("Carrier '%1' not found among Temu carriers: %2")
                  .arg(carrierName, availableNames.join(QStringLiteral(", ")));
        qWarning() << "Temu shipOrder:" << m_lastError;
        co_return false;
    }

    // 2. Build SendRequest item
    QJsonObject sendRequest;
    sendRequest.insert(QStringLiteral("carrierId"), carrierId);
    sendRequest.insert(QStringLiteral("trackingNumber"), trackingNumber);

    QJsonObject itemInfo;
    itemInfo.insert(QStringLiteral("parentOrderSn"), parentOrderSn);
    itemInfo.insert(QStringLiteral("orderSn"), orderSn);
    itemInfo.insert(QStringLiteral("goodsId"), goodsId);
    itemInfo.insert(QStringLiteral("skuId"), skuId);
    itemInfo.insert(QStringLiteral("quantity"), quantity);

    QJsonArray orderSendInfoList;
    orderSendInfoList.append(itemInfo);
    sendRequest.insert(QStringLiteral("orderSendInfoList"), orderSendInfoList);

    QJsonArray sendRequestList;
    sendRequestList.append(sendRequest);

    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("sendType"), 0); // 0 = single shipping package
    businessParams.insert(QStringLiteral("sendRequestList"), sendRequestList);

    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.logistics.shipment.v2.confirm"), businessParams, &result);

    if (!m_lastError.isEmpty()) {
        qWarning() << "Temu shipOrder failed:" << m_lastError;
        co_return false;
    }

    co_return true;
}

QCoro::Task<void> TemuInventoryApi::fetchComplianceEntities(int complianceInfoType,
                                                            QList<RepEntity> *out)
{
    out->clear();

    const int pageSize = 20; // documented maximum
    const int kMaxPages = 20;
    for (int page = 1; page <= kMaxPages; ++page) {
        QJsonObject businessParams;
        businessParams.insert(QStringLiteral("page"), page);
        businessParams.insert(QStringLiteral("size"), pageSize);
        businessParams.insert(QStringLiteral("complianceInfoType"), complianceInfoType);
        businessParams.insert(QStringLiteral("language"), QStringLiteral("en"));

        QJsonObject result;
        co_await _postRequest(QStringLiteral("bg.local.goods.compliance.info.fill.list.query"),
                              businessParams, &result);
        if (!m_lastError.isEmpty())
            co_return;

        const int total = result.value(QStringLiteral("total")).toInt();
        const QJsonArray list = result.value(QStringLiteral("authRepInfoList")).toArray();
        for (const QJsonValue &val : list) {
            const QJsonObject o = val.toObject();
            RepEntity e;
            e.repId = static_cast<qint64>(o.value(QStringLiteral("repId")).toDouble());
            e.name  = o.value(QStringLiteral("repName")).toString();
            const QJsonObject addr = o.value(QStringLiteral("repAddressInfo")).toObject();
            QStringList parts;
            for (const QString &key : {QStringLiteral("addressLineOne"),
                                       QStringLiteral("addressLineTwo"),
                                       QStringLiteral("city"),
                                       QStringLiteral("stateName"),
                                       QStringLiteral("regionName")}) {
                const QString part = addr.value(key).toString();
                if (!part.isEmpty())
                    parts << part;
            }
            e.address = parts.join(QStringLiteral(", "));
            out->append(e);
        }
        if (list.isEmpty() || out->size() >= total)
            co_return;
    }
}

// ---------------------------------------------------------------------------
// Product create/update
// ---------------------------------------------------------------------------

QCoro::Task<void> TemuInventoryApi::lookupGoods(const QStringList &outSkuSns, ExistingGoods *out)
{
    *out = ExistingGoods{};
    if (outSkuSns.isEmpty())
        co_return;

    QJsonArray snArr;
    for (const QString &sn : outSkuSns)
        snArr.append(sn);

    // "ALL" excludes just-created products still in INCOMPLETE/DRAFT — search
    // those statuses too so a freshly-published product is detected for update.
    QJsonArray goods;
    for (const QString &status : {QStringLiteral("ALL"), QStringLiteral("INCOMPLETE"),
                                  QStringLiteral("DRAFT")}) {
        QJsonObject businessParams;
        businessParams.insert(QStringLiteral("goodsSearchType"), status);
        businessParams.insert(QStringLiteral("outSkuSnList"), snArr);
        businessParams.insert(QStringLiteral("pageSize"), 20);
        QJsonObject result;
        co_await _postRequest(QStringLiteral("temu.local.goods.list.retrieve"), businessParams, &result);
        if (!m_lastError.isEmpty())
            co_return;
        goods = result.value(QStringLiteral("goodsList")).toArray();
        if (!goods.isEmpty())
            break;
    }
    if (goods.isEmpty())
        co_return;

    const QJsonObject g = goods.first().toObject();
    out->found   = true;
    out->goodsId = static_cast<qint64>(g.value(QStringLiteral("goodsId")).toVariant().toLongLong());
    out->catId   = g.value(QStringLiteral("catId")).toVariant().toString();
    for (const QJsonValue &sv : g.value(QStringLiteral("skuInfoList")).toArray()) {
        const QJsonObject s = sv.toObject();
        const QString sn = s.value(QStringLiteral("skuSn")).toString();
        const qint64 id  = static_cast<qint64>(s.value(QStringLiteral("skuId")).toVariant().toLongLong());
        if (!sn.isEmpty())
            out->skuIdBySkuSn.insert(sn, id);
    }

    // The category NAME is not in the list response; the detail query carries
    // it (catName = the full "A / B / Leaf" path) — cheap for a single goods.
    if (out->goodsId != 0) {
        QJsonObject detailParams;
        detailParams.insert(QStringLiteral("goodsId"), out->goodsId);
        QJsonObject detail;
        co_await _postRequest(QStringLiteral("bg.local.goods.detail.query"), detailParams, &detail);
        m_lastError.clear(); // the name is a nicety; ignore detail failures
        out->catName = detail.value(QStringLiteral("catName")).toString();
    }
    co_return;
}

QCoro::Task<void> TemuInventoryApi::fetchCategoryTemplate(qint64 catId, QList<CategoryAttr> *out)
{
    out->clear();
    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("catId"), catId);
    businessParams.insert(QStringLiteral("language"), QStringLiteral("en"));

    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.template.get"), businessParams, &result);
    if (!m_lastError.isEmpty())
        co_return;

    const QJsonObject ti = result.value(QStringLiteral("templateInfo")).toObject();
    for (const QJsonValue &pv : ti.value(QStringLiteral("goodsProperties")).toArray()) {
        const QJsonObject p = pv.toObject();
        CategoryAttr attr;
        attr.pid               = static_cast<qint64>(p.value(QStringLiteral("pid")).toVariant().toLongLong());
        attr.templatePid       = static_cast<qint64>(p.value(QStringLiteral("templatePid")).toVariant().toLongLong());
        attr.refPid            = static_cast<qint64>(p.value(QStringLiteral("refPid")).toVariant().toLongLong());
        attr.name              = p.value(QStringLiteral("name")).toString();
        attr.required          = p.value(QStringLiteral("required")).toBool();
        attr.controlType       = p.value(QStringLiteral("controlType")).toInt(1);
        attr.parentTemplatePid = static_cast<qint64>(p.value(QStringLiteral("parentTemplatePid")).toVariant().toLongLong());
        for (const QJsonValue &vv : p.value(QStringLiteral("values")).toArray()) {
            const QJsonObject v = vv.toObject();
            attr.values.append({v.value(QStringLiteral("value")).toString(),
                                static_cast<qint64>(v.value(QStringLiteral("vid")).toVariant().toLongLong())});
        }
        out->append(attr);
    }
    co_return;
}

QCoro::Task<void> TemuInventoryApi::fetchCategories(qint64 parentCatId, QList<CatNode> *out)
{
    out->clear();
    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("parentCatId"), parentCatId);
    businessParams.insert(QStringLiteral("language"), QStringLiteral("en"));

    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.cats.get"), businessParams, &result);
    if (!m_lastError.isEmpty())
        co_return;

    for (const QJsonValue &cv : result.value(QStringLiteral("goodsCatsList")).toArray()) {
        const QJsonObject c = cv.toObject();
        CatNode node;
        node.catId   = static_cast<qint64>(c.value(QStringLiteral("catId")).toVariant().toLongLong());
        node.catName = c.value(QStringLiteral("catName")).toString();
        node.leaf    = c.value(QStringLiteral("leaf")).toBool();
        out->append(node);
    }
    co_return;
}

QCoro::Task<void> TemuInventoryApi::recommendCategory(const QString &goodsName,
                                                      const QString &description,
                                                      const QString &imageUrl,
                                                      QList<qint64> *candidateCatIds)
{
    candidateCatIds->clear();

    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("goodsName"), goodsName);
    if (!description.isEmpty())
        businessParams.insert(QStringLiteral("description"), description);
    if (!imageUrl.isEmpty())
        businessParams.insert(QStringLiteral("imageUrl"), imageUrl);

    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.category.recommend"), businessParams, &result);
    if (!m_lastError.isEmpty())
        co_return;

    // catIdList is a list of candidate leaf categories (NOT a parent→child path).
    for (const QJsonValue &idv : result.value(QStringLiteral("catIdList")).toArray()) {
        const qint64 id = static_cast<qint64>(idv.toVariant().toLongLong());
        if (id != 0)
            candidateCatIds->append(id);
    }
    co_return;
}

QCoro::Task<void> TemuInventoryApi::resolveCategoryPaths(const QList<qint64> &targetIds,
                                                         int maxCalls,
                                                         QHash<qint64, QString> *idToPath)
{
    idToPath->clear();
    QSet<qint64> remaining;
    for (qint64 id : targetIds) {
        if (m_catPathCache.contains(id))
            idToPath->insert(id, m_catPathCache.value(id));
        else
            remaining.insert(id);
    }
    if (remaining.isEmpty())
        co_return;

    // Breadth-first crawl from the root, tracking each node's path, until all
    // targets are found or the call budget is exhausted. Every node seen is
    // cached for future lookups.
    struct Item { qint64 id; QString path; };
    QList<Item> queue;
    queue.append({0, QString{}});
    int calls = 0;
    while (!queue.isEmpty() && !remaining.isEmpty() && calls < maxCalls) {
        const Item cur = queue.takeFirst();
        QList<CatNode> children;
        co_await fetchCategories(cur.id, &children);
        ++calls;
        if (!m_lastError.isEmpty()) { m_lastError.clear(); continue; }
        for (const CatNode &n : children) {
            const QString path = cur.path.isEmpty()
                ? n.catName : (cur.path + QStringLiteral(" › ") + n.catName);
            m_catPathCache.insert(n.catId, path);
            if (remaining.remove(n.catId))
                idToPath->insert(n.catId, path);
            if (!n.leaf)
                queue.append({n.catId, path});
        }
    }
    co_return;
}

QCoro::Task<QString> TemuInventoryApi::uploadImageToTemu(const QString &publicUrl)
{
    if (publicUrl.isEmpty())
        co_return QString{};

    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("fileUrl"), publicUrl);
    businessParams.insert(QStringLiteral("scalingType"), 0); // keep original

    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.image.upload"), businessParams, &result);
    if (!m_lastError.isEmpty())
        co_return QString{};

    // The processed image URL comes back under one of these keys.
    for (const QString &key : {QStringLiteral("url"), QStringLiteral("imageUrl"),
                               QStringLiteral("fileUrl"), QStringLiteral("processedUrl")}) {
        const QString u = result.value(key).toString();
        if (!u.isEmpty())
            co_return u;
    }
    co_return QString{};
}

QCoro::Task<void> TemuInventoryApi::fetchFreightTemplateId(QString *out)
{
    out->clear();
    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.freight.template.list.query"), QJsonObject{}, &result);
    if (!m_lastError.isEmpty())
        co_return;
    const QJsonArray list = result.value(QStringLiteral("templateList")).toArray();
    if (!list.isEmpty())
        *out = list.first().toObject().value(QStringLiteral("templateId")).toString();
}

QCoro::Task<void> TemuInventoryApi::generateSpecId(qint64 catId, qint64 parentSpecId,
                                                   const QString &childSpecName, qint64 *specIdOut)
{
    *specIdOut = 0;
    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("catId"), catId);
    businessParams.insert(QStringLiteral("parentSpecId"), parentSpecId);
    businessParams.insert(QStringLiteral("childSpecName"), childSpecName);
    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.spec.id.get"), businessParams, &result);
    if (!m_lastError.isEmpty())
        co_return;
    *specIdOut = static_cast<qint64>(result.value(QStringLiteral("specId")).toVariant().toLongLong());
}

QCoro::Task<qint64> TemuInventoryApi::publishGoods(const QJsonObject &payload, bool isUpdate, qint64 goodsId)
{
    QJsonObject businessParams = payload;
    const QString method = isUpdate
        ? QStringLiteral("bg.local.goods.update")
        : QStringLiteral("bg.local.goods.add");
    if (isUpdate)
        businessParams.insert(QStringLiteral("goodsId"), goodsId);

    QJsonObject result;
    co_await _postRequest(method, businessParams, &result);
    if (!m_lastError.isEmpty())
        co_return 0;

    const qint64 id = static_cast<qint64>(result.value(QStringLiteral("goodsId")).toVariant().toLongLong());
    co_return id != 0 ? id : goodsId;
}

QCoro::Task<qint64> TemuInventoryApi::publishGoodsV3(const QJsonObject &payload)
{
    QJsonObject result;
    co_await _postRequest(QStringLiteral("temu.local.goods.v3.add"), payload, &result);
    if (!m_lastError.isEmpty())
        co_return 0;
    co_return static_cast<qint64>(result.value(QStringLiteral("goodsId")).toVariant().toLongLong());
}

QCoro::Task<bool> TemuInventoryApi::updateGoodsPartial(qint64 goodsId, const QJsonObject &fields)
{
    QJsonObject businessParams = fields;
    businessParams.insert(QStringLiteral("goodsId"), goodsId);
    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.partial.update"), businessParams, &result);
    co_return m_lastError.isEmpty();
}

QCoro::Task<bool> TemuInventoryApi::submitCompliance(qint64 goodsId, qint64 manufacturerRepId,
                                                     qint64 gsprRepId,
                                                     const QString &productIdentifier)
{
    // GPSR compliance. Verified shapes (2026-07-07 against goodsId
    // 609458725987182/609527445430843: gpsrInfoList + templateId-51 status flip
    // 1 → 5 in bg.local.compliance.goods.list.query).
    //
    // Responsible persons — `gpsrInfo` with two SEPARATE lists (NOT the
    // `repInfoList` we used before, which the endpoint silently ignored):
    //   gpsrInfo.manufacturerList      = [{repType:3, repId}]
    //   gpsrInfo.responsiblePersonList = [{repType:2, repId}]
    //
    // Product Identification (templateId 51, refPid 1100100115) — the value goes
    // in `multiLineInputs[].name` ONLY. Adding the sibling "name" on the $value
    // object makes Temu reject every value with 150011031 "non-compliant". Free
    // text is accepted (a GTIN or a SKU/model reference both work).
    QJsonArray manufacturerList, responsiblePersonList;
    if (manufacturerRepId != 0) {
        QJsonObject o;
        o.insert(QStringLiteral("repType"), 3);
        o.insert(QStringLiteral("repId"), manufacturerRepId);
        manufacturerList.append(o);
    }
    if (gsprRepId != 0) {
        QJsonObject o;
        o.insert(QStringLiteral("repType"), 2);
        o.insert(QStringLiteral("repId"), gsprRepId);
        responsiblePersonList.append(o);
    }
    if (manufacturerList.isEmpty() && responsiblePersonList.isEmpty()
        && productIdentifier.trimmed().isEmpty())
        co_return true; // nothing to submit

    QJsonObject businessParams;
    businessParams.insert(QStringLiteral("goodsId"), goodsId);

    if (!manufacturerList.isEmpty() || !responsiblePersonList.isEmpty()) {
        QJsonObject gpsrInfo;
        gpsrInfo.insert(QStringLiteral("skip"), false);
        if (!manufacturerList.isEmpty())
            gpsrInfo.insert(QStringLiteral("manufacturerList"), manufacturerList);
        if (!responsiblePersonList.isEmpty())
            gpsrInfo.insert(QStringLiteral("responsiblePersonList"), responsiblePersonList);
        businessParams.insert(QStringLiteral("gpsrInfo"), gpsrInfo);
    }

    if (!productIdentifier.trimmed().isEmpty()) {
        QJsonObject line;
        line.insert(QStringLiteral("name"), productIdentifier.trimmed());
        QJsonObject value;
        value.insert(QStringLiteral("multiLineInputs"), QJsonArray{line});
        QJsonObject inputText;
        inputText.insert(QStringLiteral("1100100115"), value);
        QJsonObject detail;
        detail.insert(QStringLiteral("templateId"), 51);
        detail.insert(QStringLiteral("inputText"), inputText);
        QJsonObject extraTemplate;
        extraTemplate.insert(QStringLiteral("extraTemplateDetailList"), QJsonArray{detail});
        businessParams.insert(QStringLiteral("extraTemplate"), extraTemplate);
    }

    QJsonObject result;
    co_await _postRequest(QStringLiteral("bg.local.goods.compliance.edit"), businessParams, &result);
    co_return m_lastError.isEmpty();
}
