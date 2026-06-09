#include "ClothingWomenCategory.h"
#include "fillers/FillerSize.h"

#include <QObject>

QString ClothingWomenCategory::displayName() const
{
    return QObject::tr("Clothing – Women");
}

QList<MeasurementField> ClothingWomenCategory::measurementFields() const
{
    return {
        {QStringLiteral("bust"),  QObject::tr("Bust (cm)"),  3.0, {}},
        {QStringLiteral("waist"), QObject::tr("Waist (cm)"), 3.0, {}},
        {QStringLiteral("hip"),   QObject::tr("Hip (cm)"),   3.0, {}},
    };
}

QList<CountryGroup> ClothingWomenCategory::countryGroups() const
{
    return {
        {QStringLiteral("FR/BE/ES/TR"), QStringLiteral("FR"),  false, false},
        {QStringLiteral("IT"),          QStringLiteral("IT"),  false, false},
        {QStringLiteral("DE/NL/SE/PL"), QStringLiteral("DE"),  false, false},
        {QStringLiteral("UK/IE/AU"),    QStringLiteral("UK"),  false, false, true},
        {QStringLiteral("US/CA"),       QStringLiteral("COM"), false, false, true},
        {QStringLiteral("JP"),          QStringLiteral("JP"),  false, false},
    };
}

QList<QHash<QString,double>> ClothingWomenCategory::sizeRows() const
{
    QList<QHash<QString,double>> result;
    for (const auto &row : FillerSize::CLOTHE_FEMALE_ADULT_SIZES) {
        QHash<QString,double> conv;
        for (auto it = row.cbegin(); it != row.cend(); ++it)
            conv.insert(it.key(), double(it.value()));
        result << conv;
    }
    return result;
}

QStringList ClothingWomenCategory::letterSizes() const {
    return {QStringLiteral("XXS"),  QStringLiteral("XS"),  QStringLiteral("S"),
            QStringLiteral("M"),    QStringLiteral("L"),   QStringLiteral("XL"),
            QStringLiteral("XXL"),  QStringLiteral("XXXL"),QStringLiteral("4XL"),
            QStringLiteral("5XL"),  QStringLiteral("6XL")};
}

QString ClothingWomenCategory::letterToKey(const QString &letter) const {
    static const QHash<QString,QString> map{
        {QStringLiteral("XXS"),  QStringLiteral("32")},
        {QStringLiteral("XS"),   QStringLiteral("34")},
        {QStringLiteral("S"),    QStringLiteral("36")},
        {QStringLiteral("M"),    QStringLiteral("38")},
        {QStringLiteral("L"),    QStringLiteral("40")},
        {QStringLiteral("XL"),   QStringLiteral("42")},
        {QStringLiteral("XXL"),  QStringLiteral("44")},
        {QStringLiteral("XXXL"), QStringLiteral("46")},
        {QStringLiteral("4XL"),  QStringLiteral("48")},
        {QStringLiteral("5XL"),  QStringLiteral("50")},
        {QStringLiteral("6XL"),  QStringLiteral("52")},
    };
    return map.value(letter);
}
