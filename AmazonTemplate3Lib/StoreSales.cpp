#pragma GCC optimize("O1")
#include "StoreSales.h"
#include "AmazonMarketplace.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimeZone>
#include <limits>

void StoreSales::load(const QDir &directory, const QString &accountScope)
{
    m_directory = directory;
    m_scope = accountScope;
    m_end = {};
    m_expected.clear();
    m_slices = {};
    m_error.clear();
    QFile file(directory.filePath("stores/sales_v2.json"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("scope").toString() != accountScope || root.value("version").toInt() != 2) return;
    m_end = QDateTime::fromString(root.value("endUtc").toString(), Qt::ISODate);
    const auto slices = root.value("slices").toObject();
    for (auto it = slices.begin(); it != slices.end(); ++it) {
        bool valid = it.value().isObject();
        const auto counts = it.value().toObject();
        for (auto n = counts.begin(); n != counts.end(); ++n)
            valid = valid && n.value().isDouble() && n.value().toDouble() >= 0
                && n.value().toDouble() <= std::numeric_limits<int>::max()
                && n.value().toDouble() == n.value().toInteger();
        if (valid) m_slices.insert(it.key(), counts);
    }
    for (const auto &key : root.value("expected").toArray()) m_expected.append(key.toString());
}

bool StoreSales::fresh() const
{
    const auto age = m_end.secsTo(QDateTime::currentDateTimeUtc());
    return m_end.isValid() && age >= 0 && age <= 30 * 24 * 3600;
}

bool StoreSales::refreshSuggested() const
{
    return fresh() && m_end.secsTo(QDateTime::currentDateTimeUtc()) > 14 * 24 * 3600;
}

StoreSales::Total StoreSales::total(const QStringList &asins) const
{
    Total result;
    result.complete = fresh() && !m_expected.isEmpty();
    const QSet<QString> unique(asins.cbegin(), asins.cend());
    qint64 units = 0;
    bool any = false;
    for (const QString &key : m_expected) {
        if (!m_slices.contains(key)) { result.complete = false; continue; }
        any = true;
        const auto byAsin = m_slices.value(key).toObject();
        for (const QString &asin : unique) units += byAsin.value(asin).toInteger();
    }
    if (any && units <= std::numeric_limits<int>::max()) result.units = int(units);
    else result.complete = false;
    return result;
}

bool StoreSales::save()
{
    m_directory.mkpath("stores");
    QSaveFile file(m_directory.filePath("stores/sales_v2.json"));
    const auto data = QJsonDocument(QJsonObject{{"version", 2}, {"scope", m_scope},
        {"endUtc", m_end.toString(Qt::ISODate)}, {"expected", QJsonArray::fromStringList(m_expected)},
        {"slices", m_slices}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        m_error = QStringLiteral("Could not save the store sales cache: %1").arg(file.errorString());
        return false;
    }
    return true;
}

QCoro::Task<void> StoreSales::refresh(AmazonCatalogApi *api, QList<QStringList> regions,
                                     std::shared_ptr<bool> cancelled,
                                     std::function<void(const QString &)> progress,
                                     std::function<void(int, int)> progressCounts,
                                     bool forceRefresh)
{
    m_error.clear();
    if (forceRefresh || !fresh()) {
        m_end = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::UTC);
        m_slices = {};
    }
    // Marketplace sets are disjoint: never request the same country twice.
    QSet<QString> seen;
    QList<QStringList> uniqueRegions;
    for (const auto &region : regions) {
        QStringList ids;
        for (const auto &id : region) if (!seen.contains(id)) { seen.insert(id); ids.append(id); }
        ids.sort();
        if (!ids.isEmpty()) uniqueRegions.append(ids);
    }
    m_expected.clear();
    const auto start = m_end.addDays(-365);
    for (const auto &ids : uniqueRegions)
        for (auto from = start; from < m_end; from = from.addDays(30))
            m_expected.append(ids.join(',') + '/' + QString::number(from.toSecsSinceEpoch()));
    if (m_expected.isEmpty()) {
        m_error = QStringLiteral("No Amazon sales marketplaces configured.");
        co_return;
    }
    int completed = 0;
    for (const auto &key : m_expected) if (m_slices.contains(key)) ++completed;
    if (progressCounts) progressCounts(completed, m_expected.size());
    if (!save()) co_return;
    QStringList errors;
    for (const auto &ids : uniqueRegions) {
        QStringList countries;
        for (const auto &id : ids) {
            const auto *market = AmazonMarketplace::forMarketplaceId(id);
            countries.append(market ? market->countryCode() : id);
        }
        for (auto from = start; from < m_end; from = from.addDays(30)) {
            if (cancelled && *cancelled) { m_error = QStringLiteral("Canceled; completed periods have been saved."); co_return; }
            const QString key = ids.join(',') + '/' + QString::number(from.toSecsSinceEpoch());
            if (m_slices.contains(key)) continue;
            const auto to = qMin(from.addDays(30), m_end);
            const QString period = QStringLiteral("%1 — %2 to %3")
                .arg(countries.join(", "), from.date().toString(Qt::ISODate), to.date().toString(Qt::ISODate));
            if (progress) progress(QStringLiteral("Sales period %1/%2: %3")
                .arg(completed + 1).arg(m_expected.size()).arg(period));
            QHash<QString, QHash<QString, int>> counts;
            co_await api->fetchSalesReport(ids, from, to, &counts, cancelled,
                [progress, period](const QString &message) {
                    if (progress) progress(period + QStringLiteral(" — ") + message);
                });
            if (cancelled && *cancelled) { m_error = QStringLiteral("Canceled; completed periods have been saved."); co_return; }
            bool complete = api->lastError().isEmpty();
            for (const auto &id : ids) complete = complete && counts.contains(id);
            if (!complete) {
                const QString error = QStringLiteral("%1: %2").arg(countries.join(", "), api->lastError());
                errors.append(error);
                if (progress) progress(QStringLiteral("⚠ %1 — totals remain incomplete.").arg(error));
                break; // Try other regions; retry this region's missing periods next time.
            }
            QJsonObject byAsin;
            for (const auto &id : ids)
                for (auto it = counts[id].cbegin(); it != counts[id].cend(); ++it)
                    byAsin[it.key()] = byAsin.value(it.key()).toInteger() + it.value();
            m_slices.insert(key, byAsin);
            if (!save()) { if (progress) progress(m_error); co_return; }
            ++completed;
            if (progressCounts) progressCounts(completed, m_expected.size());
        }
    }
    m_error = errors.join('\n');
}
