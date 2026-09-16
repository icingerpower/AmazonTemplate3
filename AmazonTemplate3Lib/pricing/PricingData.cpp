#include "PricingData.h"
#include "fillers/FillerSize.h"
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <limits>

namespace Pricing {
QHash<QString, double> parseEuroRates(const QByteArray &bytes, QDate *date) {
    *date = {};
    QXmlStreamReader xml(bytes);
    QHash<QString, double> rates;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("Cube"))
            continue;
        const auto attrs = xml.attributes();
        if (attrs.hasAttribute("time"))
            *date = QDate::fromString(attrs.value("time").toString(), Qt::ISODate);
        if (attrs.hasAttribute("currency")) {
            bool ok = false;
            double rate = attrs.value("rate").toDouble(&ok);
            if (!ok || !validPrice(rate))
                return {};
            rates[attrs.value("currency").toString()] = rate;
        }
    }
    if (xml.hasError() || !date->isValid() || rates.isEmpty())
        return {};
    rates["EUR"] = 1;
    return rates;
}
double sizeOrder(const QString &size, bool *letters) {
    static const QStringList order = {"XXXS", "XXS", "XS",   "S",   "M",   "L",
                                      "XL",   "XXL", "XXXL", "4XL", "5XL", "6XL"};
    QString s = size.trimmed().toUpper();
    static const QHash<QString, QString> words{{"SMALL", "S"},      {"MEDIUM", "M"},      {"LARGE", "L"},
                                               {"X_SMALL", "XS"},   {"X_LARGE", "XL"},    {"XX_SMALL", "XXS"},
                                               {"XX_LARGE", "XXL"}, {"XXX_LARGE", "XXXL"}};
    s = words.value(s, s);
    if (s == "2XL")
        s = "XXL";
    if (s == "3XL")
        s = "XXXL";
    const int index = order.indexOf(s);
    if (letters)
        *letters = index >= 0;
    if (index >= 0)
        return index;
    bool ok = false;
    const double value = s.toDouble(&ok);
    return ok && std::isfinite(value) ? value : std::numeric_limits<double>::quiet_NaN();
}
bool sizeInRange(const QString &size, const QString &from, const QString &to) {
    if (from.trimmed().isEmpty() && to.trimmed().isEmpty())
        return true;
    // Combined sizes must fit wholly inside the requested inclusive range.
    const QStringList parts = size.split(QRegularExpression("[/–-]"), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    for (const QString &part : parts) {
        bool letter = false;
        const double value = sizeOrder(part, &letter);
        if (!std::isfinite(value))
            return false;
        for (bool lower : {true, false}) {
            const QString bound = (lower ? from : to).trimmed();
            if (bound.isEmpty())
                continue;
            bool boundLetter = false;
            const double limit = sizeOrder(bound, &boundLetter);
            if (!std::isfinite(limit) || letter != boundLetter || (lower ? value < limit : value > limit))
                return false;
        }
    }
    return true;
}
QString convertSize(const QString &size, const QString &from, const QString &to, const QString &gender,
                    const QString &age, const QString &productType) {
    if (from == to || size.trimmed().isEmpty())
        return size;
    bool letters = false;
    sizeOrder(size, &letters);
    if (letters)
        return size.toUpper();
    const bool adult = age.compare("adult", Qt::CaseInsensitive) == 0;
    const bool knownGender = gender.compare("male", Qt::CaseInsensitive) == 0 ||
                             gender.compare("female", Qt::CaseInsensitive) == 0;
    if (!adult || !knownGender)
        return {}; // no guessed gender/child-size conversions
    static const QRegularExpression shoes("SHOE|BOOT|SANDAL|SLIPPER|SNEAKER",
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression clothes(
        "APPAREL|DRESS|SHIRT|PANT|SKIRT|SWIM|SHAPEWEAR|COAT|JACKET|SHORT|SWEATER|UNDERWEAR",
        QRegularExpression::CaseInsensitiveOption);
    if (!productType.contains(shoes) && !productType.contains(clothes))
        return {};
    QStringList converted;
    for (const QString &part : size.split(QRegularExpression("[/–-]"), Qt::SkipEmptyParts)) {
        if (!std::isfinite(sizeOrder(part)))
            return {};
        auto countryKey = [](const QString &country) {
            return country == "US" ? QString("COM") : country == "GB" ? QString("UK") : country;
        };
        const QString source = countryKey(from), target = countryKey(to);
        const bool female = gender.compare("female", Qt::CaseInsensitive) == 0;
        const double number = part.trimmed().toDouble();
        QString mapped;
        auto lookup = [&](const auto &table) {
            for (const auto &entry : table) {
                if (entry.contains(source) && entry.contains(target) &&
                    qAbs(double(entry.value(source)) - number) < 0.00001) {
                    mapped = QString::number(entry.value(target), 'g', 4);
                    break;
                }
            }
        };
        if (productType.contains(shoes))
            lookup(female ? FillerSize::SHOE_FEMALE_ADULT_SIZES : FillerSize::SHOE_MALE_ADULT_SIZES);
        else
            lookup(female ? FillerSize::CLOTHE_FEMALE_ADULT_SIZES : FillerSize::CLOTHE_MALE_ADULT_SIZES);
        if (mapped.isEmpty())
            return {};
        converted << mapped;
    }
    return converted.join('/');
}
Listing parseListing(const QJsonObject &body, const QString &marketplaceId, double price, bool exists) {
    Listing result;
    result.raw = body;
    result.exists = exists;
    result.price = price;
    result.fetched = QDateTime::currentSecsSinceEpoch();
    for (const auto &v : body.value("summaries").toArray()) {
        const auto s = v.toObject();
        if (s.value("marketplaceId").toString() != marketplaceId)
            continue;
        result.title = s.value("itemName").toString();
        result.productType = s.value("productType").toString();
        result.asin = s.value("asin").toString();
        result.created = QDate::fromString(s.value("createdDate").toString().left(10), Qt::ISODate);
        result.imageUrl = s.value("mainImage").toObject().value("link").toString();
    }
    const auto attrs = body.value("attributes").toObject();
    auto attr = [&](const QString &key) {
        for (const auto &v : attrs.value(key).toArray()) {
            const auto o = v.toObject();
            const QString mp = o.value("marketplace_id").toString();
            if (mp.isEmpty() || mp == marketplaceId)
                return o.value("value").toVariant().toString();
        }
        return QString();
    };
    result.brand = attr("brand");
    result.gender = attr("target_gender");
    result.age = attr("age_range_description");
    result.color = attr("color_name");
    if (result.color.isEmpty())
        result.color = attr("color");
    result.size = attr("size");
    if (result.size.isEmpty())
        result.size = attr("size_name");
    for (const QString &key :
         {QString("apparel_size"), QString("shapewear_size"), QString("footwear_size")}) {
        if (!result.size.isEmpty())
            break;
        for (const auto &v : attrs.value(key).toArray()) {
            const auto o = v.toObject();
            if (!o.value("marketplace_id").toString().isEmpty() &&
                o.value("marketplace_id").toString() != marketplaceId)
                continue;
            result.size = o.value("size").toVariant().toString();
            result.size.remove(QRegularExpression("^numeric_"));
            result.size.replace("_point_", ".");
        }
    }
    return result;
}
void applyListing(Row &row, const Market &market, const Listing &listing) {
    row.listings[market.id] = listing;
    if (!listing.exists)
        return;
    if (row.asin.isEmpty())
        row.asin = listing.asin;
    if (market.country == "FR" && !listing.title.isEmpty())
        row.titleFr = listing.title;
    if ((market.country == "UK" || market.country == "GB" || market.country == "US") &&
        !listing.title.isEmpty())
        row.titleEn = listing.title;
    if (row.fallbackTitle.isEmpty())
        row.fallbackTitle = listing.title;
    const bool preferred = market.country == "FR";
    if (preferred || row.brand.isEmpty())
        row.brand = listing.brand;
    if (preferred || row.color.isEmpty())
        row.color = listing.color;
    if (preferred || row.productType.isEmpty())
        row.productType = listing.productType;
    if (preferred || row.gender.isEmpty())
        row.gender = listing.gender;
    if (preferred || row.age.isEmpty())
        row.age = listing.age;
    if (preferred || !row.created.isValid())
        row.created = listing.created;
    if (preferred || row.sizeFr.isEmpty()) {
        const QString size =
            convertSize(listing.size, market.country, "FR", listing.gender, listing.age, listing.productType);
        if (!size.isEmpty())
            row.sizeFr = size;
    }
}
} // namespace Pricing
