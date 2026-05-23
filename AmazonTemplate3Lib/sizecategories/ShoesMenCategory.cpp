#include "ShoesMenCategory.h"
#include "fillers/FillerSize.h"

#include <QObject>

QString ShoesMenCategory::displayName() const
{
    return QObject::tr("Shoes – Men");
}

QList<MeasurementField> ShoesMenCategory::measurementFields() const
{
    return {{QStringLiteral("foot_length"), QObject::tr("Foot length (cm)"), 0.5, QStringLiteral("JP")}};
}

QList<CountryGroup> ShoesMenCategory::countryGroups() const
{
    return {
        {QStringLiteral("EU"),         QStringLiteral("FR"),  false, true},
        {QStringLiteral("US/CA"),      QStringLiteral("COM"), false, true,  true},
        {QStringLiteral("UK/AU"),      QStringLiteral("UK"),  false, true,  true},
        {QStringLiteral("JP/MX (cm)"), QStringLiteral("JP"),  true,  false},
    };
}

QList<QHash<QString,double>> ShoesMenCategory::sizeRows() const
{
    return FillerSize::SHOE_MALE_ADULT_SIZES;
}
