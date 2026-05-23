#include "ClothingKidCategory.h"
#include <QObject>

QString ClothingKidCategory::displayName() const
{
    return QObject::tr("Clothing – Kid");
}

QList<MeasurementField> ClothingKidCategory::measurementFields() const
{
    return {
        {QStringLiteral("chest"), QObject::tr("Chest (cm)"), 2.0, {}},
        {QStringLiteral("waist"), QObject::tr("Waist (cm)"), 1.5, {}},
    };
}

QList<CountryGroup> ClothingKidCategory::countryGroups() const
{
    return {
        {QStringLiteral("Height (cm)"), QStringLiteral("HEIGHT"), false, false, false},
    };
}

QList<QHash<QString,double>> ClothingKidCategory::sizeRows() const
{
    // Standard EN 13402 heights: 50–164 cm in 6 cm steps
    static const QList<double> heights{
        50, 56, 62, 68, 74, 80, 86, 92, 98, 104,
        110, 116, 122, 128, 134, 140, 146, 152, 158, 164
    };
    QList<QHash<QString,double>> rows;
    rows.reserve(heights.size());
    for (double h : heights)
        rows << QHash<QString,double>{{QStringLiteral("HEIGHT"), h}};
    return rows;
}
