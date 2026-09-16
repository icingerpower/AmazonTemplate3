#include "AmazonDataCache.h"
#include "apis/AmazonPricingApi.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

namespace {
QString hash(const QString &s) {
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QJsonDocument load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    return error.error == QJsonParseError::NoError ? doc : QJsonDocument{};
}
} // namespace
AmazonDataCache::AmazonDataCache(QDir workingDir, QString accountIdentity)
    : m_working(workingDir), m_root(workingDir.filePath("cache/amazon/v1/" + hash(accountIdentity))) {}
QString AmazonDataCache::path(const QString &kind, const QString &key) const {
    return m_root.filePath(hash(kind) + '/' + hash(key) + ".json");
}
QString AmazonDataCache::listingKey(const QString &mp, const QString &sku) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{mp, sku}).toJson(QJsonDocument::Compact));
}
QJsonObject AmazonDataCache::read(const QString &kind, const QString &key) const {
    const auto o = load(path(kind, key)).object();
    if (o.value("schema").toInt() != 1 || o.value("kind").toString() != kind ||
        o.value("key").toString() != key)
        return {};
    return o;
}
QList<QJsonObject> AmazonDataCache::all(const QString &kind) const {
    QList<QJsonObject> records;
    QDir dir(m_root.filePath(hash(kind)));
    for (const auto &file : dir.entryList({"*.json"}, QDir::Files)) {
        const auto o = load(dir.filePath(file)).object();
        if (o.value("schema").toInt() == 1 && o.value("kind").toString() == kind)
            records << o;
    }
    return records;
}
bool AmazonDataCache::fresh(const QJsonObject &o, qint64 ttl, qint64 now) {
    const qint64 at = o.value("fetchedUtc").toInteger();
    return !o.isEmpty() && at > 0 && at <= now && now - at < ttl && o.value("payload").isObject();
}
bool AmazonDataCache::write(const QString &kind, const QString &key, const QJsonObject &payload, qint64 at,
                            QString *error) const {
    const QString filePath = path(kind, key);
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        if (error)
            *error = "Cannot create cache directory";
        return false;
    }
    QLockFile lock(filePath + ".lock");
    if (!lock.tryLock(1000)) {
        if (error)
            *error = "Cache record is in use";
        return false;
    }
    const auto previous = read(kind, key);
    if (previous.value("fetchedUtc").toInteger() > at)
        return true; // don't overwrite newer observations
    const auto bytes =
        QJsonDocument(
            QJsonObject{
                {"schema", 1}, {"kind", kind}, {"key", key}, {"fetchedUtc", at}, {"payload", payload}})
            .toJson(QJsonDocument::Compact);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
bool AmazonDataCache::invalidate(const QString &kind, const QString &key, QString *error) const {
    const QString filePath = path(kind, key);
    if (!QFile::exists(filePath))
        return true;
    QLockFile lock(filePath + ".lock");
    if (!lock.tryLock(1000) || !QFile::remove(filePath)) {
        if (error)
            *error = "Cannot invalidate cache record";
        return false;
    }
    return true;
}
QList<Pricing::Row> AmazonDataCache::seedRows(const QList<Pricing::Market> &markets) const {
    QHash<QString, Pricing::Row> rows;
    QList<QDir> sources;
    QDir exports(m_working.filePath("cache/amazon/exports"));
    const auto names = exports.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (!names.isEmpty())
        sources << QDir(exports.filePath(names.last()));
    sources << m_working;
    for (const QDir &source : sources) {
        for (const auto &market : markets) {
            for (const auto &v : load(source.filePath("stores/" + market.id + ".json")).array()) {
                const auto o = v.toObject();
                const QString sku = o.value("sku").toString();
                if (sku.isEmpty())
                    continue;
                auto &row = rows[sku];
                row.sku = sku;
                if (row.asin.isEmpty())
                    row.asin = o.value("asin").toString();
                Pricing::Listing l;
                l.exists = true;
                l.asin = row.asin;
                l.title = o.value("title").toString();
                l.size = o.value("sizeValue").toString();
                l.color = o.value("color").toString();
                l.brand = o.value("brand").toString();
                l.gender = o.value("gender").toString();
                l.age = o.value("age").toString();
                if (!o.value("manuallyMoved").toBool())
                    l.productType = o.value("category").toString();
                l.imageUrl = o.value("mainImageUrl").toString();
                l.created = QDate::fromString(o.value("createdDate").toString(), Qt::ISODate);
                Pricing::applyListing(row, market, l); // hints only; price/fetched remain unknown
            }
            const auto map = load(source.filePath("sizing/sku_cache_" + market.id + ".json")).object();
            for (auto it = map.begin(); it != map.end(); ++it) {
                const QString sku = it.value().toString();
                if (sku.isEmpty() || sku.startsWith("amzn.gr."))
                    continue;
                auto &row = rows[sku];
                row.sku = sku;
                if (row.asin.isEmpty())
                    row.asin = it.key();
            }
        }
    }
    // Restore current, account-scoped listing observations over historical hints.
    for (const auto &record : all("listing")) {
        const auto o = record.value("payload").toObject();
        const QString sku = o.value("sku").toString(), mp = o.value("marketplace").toString();
        if (sku.isEmpty())
            continue;
        for (const auto &market : markets)
            if (market.id == mp) {
                auto &row = rows[sku];
                row.sku = sku;
                const auto body = o.value("body").toObject();
                auto l = Pricing::parseListing(body, mp,
                                               AmazonPricingApi::parseListingPrice(
                                                   QJsonDocument(body).toJson(QJsonDocument::Compact), mp),
                                               o.value("exists").toBool());
                l.fetched = record.value("fetchedUtc").toInteger();
                Pricing::applyListing(row, market, l);
            }
    }
    // Legacy stock figures are display hints only. They never satisfy fresh reads.
    QSettings settings(m_working.filePath("settings.ini"), QSettings::IniFormat);
    const auto available =
        QJsonDocument::fromJson(settings.value("PaneStoreStockCache/available").toString().toUtf8()).object();
    const auto sales =
        QJsonDocument::fromJson(settings.value("PaneStoreStockCache/sales90").toString().toUtf8()).object();
    const QSet<QString> legacySalesScope{"A1PA6795UKMFR9", "A13V1IB3VIYZZH", "APJ6JRA9NG5V4",
                                         "A1RKKUPIHCS9HS", "A1805IZSGTT6HS", "A2NODRKZP88ZB9",
                                         "A1C3SOZRARQ6R3", "AMEN7PMS3EDWL"};
    QSet<QString> wanted;
    bool mainlandOnly = true;
    for (const auto &m : markets) {
        if (!m.enabled)
            continue;
        wanted.insert(m.id);
        mainlandOnly &= m.continent == "Europe" && m.country != "UK" && m.country != "GB";
    }
    QHash<QString, int> lowerCounts;
    for (auto it = rows.begin(); it != rows.end(); ++it)
        ++lowerCounts[it.key().toLower()];
    for (auto &row : rows) {
        if (lowerCounts.value(row.sku.toLower()) == 1) {
            if (mainlandOnly && !wanted.isEmpty())
                row.available = available.value(row.sku.toLower()).toInt(-1);
            if (wanted == legacySalesScope)
                row.sales90 = sales.value(row.sku.toLower()).toInt(-1);
        }
        for (auto it = sources.crbegin(); it != sources.crend(); ++it) {
            const QString file = it->filePath("stores/thumbs/" + row.asin + ".jpg");
            if (QFile::exists(file)) {
                row.imagePath = file;
                break;
            }
        }
        const auto stats = read("rowStats", row.sku).value("payload").toObject();
        QSet<QString> stored;
        for (const auto &v : stats.value("scope").toArray())
            stored.insert(v.toString());
        if (!stats.isEmpty() && wanted == stored) {
            row.available = stats.value("available").toInt(-1);
            row.sales90 = stats.value("sales90").toInt(-1);
        }
        const QString cachedImage = stats.value("imagePath").toString();
        if (!cachedImage.isEmpty() && QFile::exists(cachedImage))
            row.imagePath = cachedImage;
    }
    auto result = rows.values();
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.sku < b.sku; });
    return result;
}
