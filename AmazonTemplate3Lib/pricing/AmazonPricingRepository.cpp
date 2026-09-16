// Keep the same coroutine optimization workaround as the existing SP-API clients.
#pragma GCC optimize("O1")
#include "AmazonPricingRepository.h"
#include "AmazonMarketplace.h"
#include "apis/AmazonCatalogApi.h"
#include "apis/AmazonInventoryApi.h"
#include "apis/AmazonPricingApi.h"
#include "apis/ReadRequestControl.h"
#include <QCoro/QCoroNetworkReply>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUuid>

AmazonPricingRepository::AmazonPricingRepository(QDir working, Credentials credentials,
                                                 InventoryFactory inventoryFactory,
                                                 PricingFactory pricingFactory)
    : m_inventoryFactory(std::move(inventoryFactory)), m_pricingFactory(std::move(pricingFactory)),
      m_credentials(std::move(credentials)), m_working(working),
      m_cache(working, m_credentials.sellerEu + "|" + m_credentials.sellerNa) {}
QList<Pricing::Row> AmazonPricingRepository::cachedRows(const QList<Pricing::Market> &markets) const {
    return m_cache.seedRows(markets);
}

QCoro::Task<void> AmazonPricingRepository::retrieve(QList<Pricing::Market> markets,
                                                    PricingFilterProxy::Filter filter,
                                                    std::shared_ptr<bool> cancelled, Log log,
                                                    std::function<void(const Pricing::Row &)> onRow,
                                                    Progress progress) {
    const auto &c = m_credentials;
    auto report = [&](const QString &phase, int done, int total, const QString &detail) {
        if (progress)
            progress(phase, done, total, detail);
    };
    report("Loading cache", 0, 0, "Reading saved observations");
    AmazonCatalogApi catalog(c.client, c.secret, c.tokenEu, c.tokenNa, QString(), c.sellerEu, c.sellerNa);
    AmazonPricingApi prices(c.client, c.secret, c.tokenEu, c.tokenNa, c.sellerEu, c.sellerNa);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    PricingFilterProxy filtering;
    filtering.setFilter(filter);
    QHash<QString, Pricing::Row> rows;
    for (const auto &r : cachedRows(markets))
        rows[r.sku] = r;
    QList<Pricing::Market> enabled;
    bool completeCredentials = true;
    for (const auto &m : markets)
        if (m.enabled) {
            const bool eu = m.continent == "Europe";
            if ((eu ? c.tokenEu.isEmpty() || c.sellerEu.isEmpty()
                    : c.tokenNa.isEmpty() || c.sellerNa.isEmpty())) {
                completeCredentials = false;
                log(QString("%1: missing seller credentials; disabled for this retrieval").arg(m.country));
                continue;
            }
            enabled << m;
        }
    if (enabled.isEmpty()) {
        log("No enabled marketplace has credentials.");
        co_return;
    }
    // One client per marketplace for the entire retrieval: retain token and connection pools.
    QHash<QString, std::shared_ptr<AmazonInventoryApi>> inventoryClients;
    auto clientFor = [&](const Pricing::Market &m) -> AmazonInventoryApi & {
        auto &client = inventoryClients[m.id];
        if (!client) {
            const bool eu = m.continent == "Europe";
            client =
                m_inventoryFactory
                    ? m_inventoryFactory(m)
                    : std::make_shared<AmazonInventoryApi>(c.client, c.secret, eu ? c.tokenEu : c.tokenNa,
                                                           eu ? c.sellerEu : c.sellerNa, m.id);
            client->setReadCancellation(cancelled);
            client->setReadProgress(log);
        }
        return *client;
    };
    int discoveryDone = 0;
    log("Discovering marketplace listings");
    // Discover all SKUs, preserving multiple offers for a single ASIN.
    for (const auto &m : enabled) {
        if (*cancelled)
            co_return;
        report("Discovering listings", discoveryDone++, enabled.size(), m.country);
        auto record = m_cache.read("discovery", m.id);
        QJsonArray listings;
        if (AmazonDataCache::fresh(record, 86400, now))
            listings = record.value("payload").toObject().value("listings").toArray();
        else {
            log("Discovering listings: " + m.country);
            QHash<QString, QString> asinMap;
            QList<AmazonCatalogApi::StoreItem> all;
            co_await catalog.fetchAllSkusViaReport(m.id, &asinMap, nullptr, nullptr, &all, cancelled);
            if (*cancelled)
                co_return;
            for (const auto &item : all)
                listings.append(QJsonObject{{"sku", item.sku}, {"asin", item.asin}});
            if (!all.isEmpty()) {
                QString error;
                if (!m_cache.write("discovery", m.id, {{"listings", listings}},
                                   QDateTime::currentSecsSinceEpoch(), &error))
                    log(error);
            } else
                log(m.country + ": listing discovery returned no data; retained cached SKU hints.");
        }
        for (const auto &v : listings) {
            const auto o = v.toObject();
            const QString sku = o.value("sku").toString();
            if (sku.isEmpty())
                continue;
            auto &row = rows[sku];
            row.sku = sku;
            if (row.asin.isEmpty())
                row.asin = o.value("asin").toString();
        }
    }
    // French metadata first; English is the fallback. All prices remain local.
    auto ordered = enabled;
    QSet<QString> enabledIds;
    for (const auto &m : enabled)
        enabledIds.insert(m.id);
    if ((filter.region.isEmpty() || filter.region == "Europe") && !c.tokenEu.isEmpty() &&
        !c.sellerEu.isEmpty()) {
        if (!enabledIds.contains("A13V1IB3VIYZZH"))
            ordered << Pricing::Market{"A13V1IB3VIYZZH", "FR", "EUR", "Europe", 1, false};
        if (!enabledIds.contains("A1F83G8C2ARO7P"))
            ordered << Pricing::Market{"A1F83G8C2ARO7P", "UK", "GBP", "Europe", -1, false};
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        auto rank = [](const QString &cc) { return cc == "FR" ? 0 : (cc == "UK" || cc == "US" ? 1 : 2); };
        return rank(a.country) < rank(b.country);
    });
    QStringList candidates = rows.keys();
    candidates.sort();
    int listingDone = 0;
    log(QString("Reading listing prices and metadata for %1 candidate SKUs").arg(candidates.size()));
    for (const QString &sku : candidates) {
        if (*cancelled)
            co_return;
        report("Listings", listingDone++, candidates.size(), sku);
        if (!sku.contains(filter.sku, Qt::CaseInsensitive))
            continue;
        auto &row = rows[sku];
        if (!filtering.mayMatchMetadata(row))
            continue;
        for (const auto &m : ordered) {
            if (!m.enabled && m.country != "FR" && !row.titleFr.isEmpty())
                continue;
            const auto key = AmazonDataCache::listingKey(m.id, sku);
            auto record = m_cache.read("listing", key);
            QJsonObject payload;
            if (AmazonDataCache::fresh(record, 3600, now))
                payload = record.value("payload").toObject();
            else {
                report("Listings", listingDone - 1, candidates.size(), sku + " / " + m.country);
                double price = -1;
                bool exists = false;
                QJsonObject body;
                co_await prices.fetchListingPrice(m.id, sku, &price, &exists, nullptr, nullptr, nullptr,
                                                  &body, cancelled);
                if (*cancelled)
                    co_return;
                if (!prices.lastError().isEmpty()) {
                    log(prices.lastError());
                    continue;
                }
                payload = {{"sku", sku},
                           {"marketplace", m.id},
                           {"price", price},
                           {"exists", exists},
                           {"body", body}};
                QString error;
                if (!m_cache.write("listing", key, payload, QDateTime::currentSecsSinceEpoch(), &error))
                    log(error);
                record = m_cache.read("listing", key);
            }
            const auto body = payload.value("body").toObject();
            auto l = Pricing::parseListing(
                body, m.id,
                AmazonPricingApi::parseListingPrice(QJsonDocument(body).toJson(QJsonDocument::Compact), m.id),
                payload.value("exists").toBool());
            l.fetched = record.value("fetchedUtc").toInteger();
            Pricing::applyListing(row, m, l);
            if (!filtering.mayMatchMetadata(row))
                break;
        }
        onRow(row);
    }
    QStringList eligible;
    for (const QString &sku : candidates)
        if (filtering.accepts(rows[sku], enabled, true))
            eligible << sku;
    // Mainland EU shares one pool; GB and American marketplaces stay separate.
    QHash<QString, Pricing::Market> pools;
    for (const auto &m : enabled)
        pools[m.continent == "Europe" && m.country != "UK" ? "eu-mainland" : m.id] = m;
    QHash<QString, QHash<QString, int>> quantities;
    int poolsDone = 0;
    log(QString("Reading inventory for %1 matching SKUs in %2 stock pools")
            .arg(eligible.size())
            .arg(pools.size()));
    for (auto it = pools.cbegin(); it != pools.cend(); ++it) {
        const auto m = it.value();
        auto &inventory = clientFor(m);
        QStringList missing;
        for (const auto &sku : eligible) {
            const auto key = AmazonDataCache::listingKey(it.key(), sku);
            const auto record = m_cache.read("inventory", key);
            if (AmazonDataCache::fresh(record, 86400, now))
                quantities[it.key()][sku] = record.value("payload").toObject().value("available").toInt(-1);
            else
                missing << sku;
        }
        log(QString("Inventory %1: %2 cached, %3 missing/expired")
                .arg(m.country)
                .arg(eligible.size() - missing.size())
                .arg(missing.size()));
        // Check Cancel and persist completed observations after every batch, not after the entire pool.
        for (int start = 0; start < missing.size(); start += 50) {
            if (*cancelled)
                co_return;
            report("Inventory", poolsDone * eligible.size() + eligible.size() - missing.size() + start,
                   pools.size() * eligible.size(),
                   m.country + QString(" · batch %1/%2").arg(start / 50 + 1).arg((missing.size() + 49) / 50));
            const QStringList batch = missing.mid(start, 50);
            QList<AmazonInventoryApi::InventorySummary> inventoryRows;
            co_await inventory.fetchFbaInventory(batch, &inventoryRows, log);
            for (const auto &r : inventoryRows) {
                quantities[it.key()][r.sku] = r.available;
                QString error;
                if (!m_cache.write("inventory", AmazonDataCache::listingKey(it.key(), r.sku),
                                   {{"available", r.available}, {"asin", r.asin}},
                                   QDateTime::currentSecsSinceEpoch(), &error))
                    log(error);
            }
            if (*cancelled)
                co_return;
            if (!inventory.lastError().isEmpty()) {
                log(m.country + ": " + inventory.lastError());
                break; // don't repeat a failed pool request for every remaining batch
            }
        }
        ++poolsDone;
    }
    QNetworkAccessManager images;
    images.setTransferTimeout(30000);
    int salesDone = 0, cachedSales = 0, requestedSales = 0, failedSales = 0;
    const int salesTotal = eligible.size() * enabled.size();
    log(QString("Reading 90-day sales for %1 SKUs across %2 marketplaces (%3 checks; fresh cache reused)")
            .arg(eligible.size())
            .arg(enabled.size())
            .arg(salesTotal));
    for (const auto &sku : eligible) {
        if (*cancelled)
            co_return;
        auto &row = rows[sku];
        int total = 0;
        bool complete = completeCredentials;
        for (const auto &m : enabled) {
            // Confirmed absent listings do not consume stock in this marketplace.
            if (!row.listings.value(m.id).exists && row.listings.value(m.id).fetched > 0) {
                ++salesDone;
                continue;
            }
            report("90-day sales", salesDone, salesTotal, sku + " / " + m.country);
            const auto key = AmazonDataCache::listingKey(m.id, sku);
            auto record = m_cache.read("sales90", key);
            int units = -1;
            if (AmazonDataCache::fresh(record, 86400, now)) {
                units = record.value("payload").toObject().value("units").toInt(-1);
                ++cachedSales;
            } else {
                ++requestedSales;
                auto &inventory = clientFor(m);
                const QStringList oneMarketplace{m.id};
                co_await inventory.fetchSalesUnits(sku, 90, oneMarketplace, &units);
                if (*cancelled)
                    co_return;
                if (units >= 0) {
                    QString error;
                    if (!m_cache.write("sales90", key,
                                       {{"units", units}, {"days", 90}, {"metric", "orderUnits"}},
                                       QDateTime::currentSecsSinceEpoch(), &error))
                        log(error);
                }
            }
            ++salesDone;
            if (units < 0) {
                complete = false;
                ++failedSales;
                log(sku + " / " + m.country + ": sales unavailable; " + clientFor(m).lastError());
            } else
                total += units;
            report("90-day sales", salesDone, salesTotal, sku + " / " + m.country);
        }
        row.sales90 = complete ? total : -1;
        row.available = 0;
        bool stockComplete = completeCredentials;
        for (auto it = pools.cbegin(); it != pools.cend(); ++it) {
            // Ignore pools where the SKU is confirmed absent in every selected marketplace.
            bool needed = false;
            for (const auto &m : enabled) {
                const QString pool = m.continent == "Europe" && m.country != "UK" ? "eu-mainland" : m.id;
                const auto l = row.listings.value(m.id);
                if (pool == it.key() && (l.exists || l.fetched == 0))
                    needed = true;
            }
            if (!needed)
                continue;
            const int n = quantities.value(it.key()).value(sku, -1);
            if (n < 0)
                stockComplete = false;
            else
                row.available += n;
        }
        if (!stockComplete)
            row.available = -1;
        if (row.imagePath.isEmpty()) {
            QString url;
            for (const auto &m : ordered)
                if (!row.listings.value(m.id).imageUrl.isEmpty()) {
                    url = row.listings.value(m.id).imageUrl;
                    break;
                }
            if (!url.isEmpty()) {
                report("90-day sales", salesDone, salesTotal, "Downloading image for " + sku);
                auto *reply = images.get(QNetworkRequest(QUrl(url)));
                co_await AmazonRead::wait(reply, cancelled);
                const QByteArray bytes = reply->readAll();
                const bool ok = reply->error() == QNetworkReply::NoError;
                reply->deleteLater();
                if (*cancelled)
                    co_return;
                QImage image;
                if (ok && image.loadFromData(bytes)) {
                    const QString imageDir = m_cache.root().filePath("images");
                    QDir().mkpath(imageDir);
                    const QString file = QDir(imageDir).filePath(
                        QString::fromLatin1(
                            QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex()) +
                        ".png");
                    QSaveFile output(file);
                    if (output.open(QIODevice::WriteOnly) &&
                        image.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            .save(&output, "PNG") &&
                        output.commit())
                        row.imagePath = file;
                }
            }
        }
        QString error;
        QJsonArray scope;
        for (const auto &m : markets)
            if (m.enabled)
                scope.append(m.id);
        if (!m_cache.write("rowStats", sku,
                           {{"sales90", row.sales90},
                            {"available", row.available},
                            {"scope", scope},
                            {"imagePath", row.imagePath}},
                           QDateTime::currentSecsSinceEpoch(), &error))
            log(error);
        onRow(row);
    }
    report("Complete", salesTotal, salesTotal,
           QString("%1 SKUs; %2 cached sales, %3 requests, %4 unavailable")
               .arg(eligible.size())
               .arg(cachedSales)
               .arg(requestedSales)
               .arg(failedSales));
    log(QString("Retrieval complete: %1 SKUs, %2 cached sales, %3 sales requests, %4 unavailable. Unknown "
                "values remain blank.")
            .arg(eligible.size())
            .arg(cachedSales)
            .arg(requestedSales)
            .arg(failedSales));
}

QString AmazonPricingRepository::describeUpdate(const Update &u) {
    const auto *market = AmazonMarketplace::forMarketplaceId(u.marketplace);
    return QString("SKU %1 | %2 [%3] | %4 → %5 %6 | %7")
        .arg(u.sku, market ? market->countryCode() : u.marketplace, u.marketplace,
             QString::number(u.before, 'f', 2), QString::number(u.after, 'f', 2), u.currency,
             u.after > u.before ? "INCREASE" : "DECREASE");
}

QCoro::Task<void> AmazonPricingRepository::update(QList<Update> updates, std::shared_ptr<bool> cancelled,
                                                  Log log,
                                                  std::function<void(const Update &, bool)> onResult) {
    // Preserve a timestamped audit independently of the dialog's lifetime.
    const QString folder = "pricing-update-logs";
    const QString filename = QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzzZ") + "_" +
                             QUuid::createUuid().toString(QUuid::WithoutBraces) + ".log";
    QFile audit(m_working.filePath(folder + "/" + filename));
    bool saving = m_working.mkpath(folder) && audit.open(QIODevice::WriteOnly | QIODevice::Text);
    auto record = [&](const QString &message) {
        // Keep every entry on one line even when a SKU or API error contains newlines.
        QString clean = message;
        clean.replace('\r', "\\r").replace('\n', "\\n");
        const QString line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " | " + clean;
        if (saving) {
            const auto bytes = (line + "\n").toUtf8();
            if (audit.write(bytes) != bytes.size() || !audit.flush()) {
                saving = false;
                log("WARNING: Could not save the audit log. Use Copy log before closing.");
            }
        }
        log(line);
    };
    record(saving ? "Audit log: " + audit.fileName()
                  : "WARNING: Could not create the audit log. Use Copy log before closing.");
    record(QString("Planned: %1 SKU/country changes. Only the entries below will be considered. "
                   "Cancel stops further work; sent changes cannot be undone.")
               .arg(updates.size()));
    for (int i = 0; i < updates.size(); ++i)
        record(
            QString("PLANNED [%1/%2] | %3").arg(i + 1).arg(updates.size()).arg(describeUpdate(updates[i])));

    const auto &c = m_credentials;
    auto prices = m_pricingFactory ? m_pricingFactory()
                                   : std::make_shared<AmazonPricingApi>(c.client, c.secret, c.tokenEu,
                                                                        c.tokenNa, c.sellerEu, c.sellerNa);
    int submitted = 0, skipped = 0, failed = 0, processed = 0;
    for (const auto &u : updates) {
        if (*cancelled)
            break;
        const QString detail =
            QString("[%1/%2] | %3").arg(processed + 1).arg(updates.size()).arg(describeUpdate(u));
        record("VERIFYING " + detail);
        double current = -1;
        bool exists = false;
        QString type;
        QJsonObject body;
        co_await prices->fetchListingPrice(u.marketplace, u.sku, &current, &exists, &type, nullptr, nullptr,
                                           &body, cancelled);
        if (*cancelled)
            break;
        QString reason;
        if (!prices->lastError().isEmpty())
            reason = "live price verification failed: " + prices->lastError();
        else if (!exists)
            reason = "listing does not exist";
        else if (!Pricing::validPrice(current))
            reason = "current seller price unavailable (no reference/list price fallback)";
        else if (std::abs(Pricing::rounded(current) - u.before) > 0.005)
            reason =
                QString("baseline changed; expected %1 %3, live %2 %3. Retrieve again.")
                    .arg(QString::number(u.before, 'f', 2), QString::number(current, 'f', 2), u.currency);
        else if (type.isEmpty())
            reason = "product type unavailable";
        const auto offers = body.value("attributes").toObject().value("purchasable_offer").toArray();
        if (reason.isEmpty() && offers.isEmpty())
            reason = "seller offer unavailable";
        if (!reason.isEmpty()) {
            ++skipped;
            ++processed;
            record("SKIPPED " + detail + " | No update sent: " + reason);
            onResult(u, false);
            continue;
        }
        record("SUBMITTING " + detail + " | Current price verified; sending price update.");
        // Let the dialog paint the before/after line and process an already queued Cancel.
        co_await AmazonRead::delay(1, cancelled);
        if (*cancelled)
            break;
        bool success = false;
        co_await prices->patchListingPrice(u.marketplace, u.sku, type, u.currency, u.after, &success, offers,
                                           cancelled);
        if (!success && prices->lastError() == "Cancelled before submission")
            break;
        ++processed;
        if (success) {
            ++submitted;
            QString error;
            if (!m_cache.invalidate("listing", AmazonDataCache::listingKey(u.marketplace, u.sku), &error))
                record("CACHE WARNING " + detail + " | " + error);
            record("SUBMITTED " + detail +
                   " | Accepted by Amazon; awaiting propagation, not yet verified live.");
        } else {
            ++failed;
            record("FAILED / UNCONFIRMED " + detail + " | " + prices->lastError() +
                   " | Check the live price before retrying; a sent change may have reached Amazon.");
        }
        onResult(u, success);
    }
    for (int i = processed; i < updates.size(); ++i)
        record("CANCELLED | " + describeUpdate(updates[i]) + " | No update sent.");
    record(QString("SUMMARY | %1 | Planned: %2; submitted: %3; skipped: %4; failed/unconfirmed: %5; "
                   "cancelled/not sent: %6. Submitted changes still require live verification.")
               .arg(*cancelled ? "Cancelled" : "Finished")
               .arg(updates.size())
               .arg(submitted)
               .arg(skipped)
               .arg(failed)
               .arg(updates.size() - processed));
}
