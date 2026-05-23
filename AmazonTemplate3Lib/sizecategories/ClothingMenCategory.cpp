#include "ClothingMenCategory.h"
#include "fillers/FillerSize.h"

#include <QObject>

QString ClothingMenCategory::displayName() const
{
    return QObject::tr("Clothing – Men");
}

QList<MeasurementField> ClothingMenCategory::measurementFields() const
{
    return {
        {QStringLiteral("chest"), QObject::tr("Chest (cm)"), 2.0, {}},
    };
}

QList<CountryGroup> ClothingMenCategory::countryGroups() const
{
    return {
        {QStringLiteral("EU (FR/DE/IT/...)"), QStringLiteral("FR"),  false, false},
        {QStringLiteral("UK/IE/AU"),          QStringLiteral("UK"),  false, false, true},
        {QStringLiteral("US/CA"),             QStringLiteral("COM"), false, false, true},
    };
}

QList<QHash<QString,double>> ClothingMenCategory::sizeRows() const
{
    QList<QHash<QString,double>> result;
    for (const auto &row : FillerSize::CLOTHE_MALE_ADULT_SIZES) {
        QHash<QString,double> conv;
        for (auto it = row.cbegin(); it != row.cend(); ++it)
            conv.insert(it.key(), double(it.value()));
        result << conv;
    }
    return result;
}

QStringList ClothingMenCategory::letterSizes() const {
    return {QStringLiteral("XS"),   QStringLiteral("S"),    QStringLiteral("M"),
            QStringLiteral("L"),    QStringLiteral("XL"),   QStringLiteral("XXL"),
            QStringLiteral("XXXL"), QStringLiteral("4XL"),  QStringLiteral("5XL"),
            QStringLiteral("6XL")};
}

QString ClothingMenCategory::letterToKey(const QString &letter) const {
    static const QHash<QString,QString> map{
        {QStringLiteral("XS"),   QStringLiteral("48")},
        {QStringLiteral("S"),    QStringLiteral("50")},
        {QStringLiteral("M"),    QStringLiteral("52")},
        {QStringLiteral("L"),    QStringLiteral("54")},
        {QStringLiteral("XL"),   QStringLiteral("56")},
        {QStringLiteral("XXL"),  QStringLiteral("58")},
        {QStringLiteral("XXXL"), QStringLiteral("60")},
        {QStringLiteral("4XL"),  QStringLiteral("62")},
        {QStringLiteral("5XL"),  QStringLiteral("64")},
        {QStringLiteral("6XL"),  QStringLiteral("66")},
    };
    return map.value(letter);
}
