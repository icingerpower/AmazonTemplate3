#include "ShoesWomenCategory.h"
#include "fillers/FillerSize.h"

#include <QObject>

QString ShoesWomenCategory::displayName() const
{
    return QObject::tr("Shoes – Women");
}

QList<MeasurementField> ShoesWomenCategory::measurementFields() const
{
    return {{QStringLiteral("foot_length"), QObject::tr("Foot length (cm)"), 0.5, QStringLiteral("JP")}};
}

QList<CountryGroup> ShoesWomenCategory::countryGroups() const
{
    return {
        {QStringLiteral("EU"),         QStringLiteral("FR"),  false, true},
        {QStringLiteral("US/CA"),      QStringLiteral("COM"), false, true,  true},
        {QStringLiteral("UK/AU"),      QStringLiteral("UK"),  false, true,  true},
        {QStringLiteral("JP/MX (cm)"), QStringLiteral("JP"),  true,  false},
    };
}

QList<QHash<QString,double>> ShoesWomenCategory::sizeRows() const
{
    return FillerSize::SHOE_FEMALE_ADULT_SIZES;
}
