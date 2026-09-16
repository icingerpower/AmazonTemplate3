#pragma once

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <cmath>

namespace Pricing {
struct Market {
    QString id, country, currency, continent;
    double rate = -1; // EUR -> local currency; unknown rates never imply parity.
    bool enabled = true;
};
struct Listing {
    bool exists = false;
    double price = -1;
    QString productType, title, size, color, brand, gender, age, imageUrl, asin;
    QDate created;
    qint64 fetched = 0;
    QJsonObject raw;
};
struct Row {
    QString sku, asin, titleFr, titleEn, fallbackTitle, sizeFr, color, brand, productType;
    QString gender, age, imagePath;
    QDate created;
    int sales90 = -1, available = -1;
    QHash<QString, Listing> listings;
    QHash<QString, double> manualPrices; // exact marketplace identity, local currency
    QSet<QString> resetMarkets;
    QString title() const {
        return !titleFr.isEmpty() ? titleFr : !titleEn.isEmpty() ? titleEn : fallbackTitle;
    }
    int inventoryDays() const {
        if (sales90 == 0 || available == 0)
            return 0;
        if (available < 0 || sales90 < 0)
            return -1;
        return qRound(double(available) * 90 / sales90);
    }
};
enum class Direction { Increase, Decrease, Both };
inline double rounded(double n) {
    return std::round(n * 100.0) / 100.0;
}
inline bool validPrice(double n) {
    return std::isfinite(n) && n > 0 && n <= 100000000;
}
inline bool allowed(double oldPrice, double newPrice, Direction direction) {
    if (!validPrice(oldPrice) || !validPrice(newPrice))
        return false;
    const double difference = rounded(newPrice) - rounded(oldPrice);
    if (std::abs(difference) < 0.005)
        return false;
    return direction == Direction::Both ||
           (direction == Direction::Increase ? difference > 0 : difference < 0);
}
// French and foreign sizes use the application's existing conversion tables.
QString convertSize(const QString &size, const QString &from, const QString &to, const QString &gender,
                    const QString &age, const QString &productType);
double sizeOrder(const QString &size, bool *letters = nullptr);
bool sizeInRange(const QString &size, const QString &from, const QString &to);
Listing parseListing(const QJsonObject &body, const QString &marketplaceId, double price, bool exists);
void applyListing(Row &row, const Market &market, const Listing &listing);
QHash<QString, double> parseEuroRates(const QByteArray &xml, QDate *date);
} // namespace Pricing
