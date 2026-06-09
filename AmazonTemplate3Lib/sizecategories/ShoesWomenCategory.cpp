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
        {QStringLiteral("EU"),         QStringLiteral("FR"),  false, true,  false,
         {QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es"),
          QStringLiteral("nl"), QStringLiteral("se"), QStringLiteral("pl"), QStringLiteral("be"),
          QStringLiteral("tr")}},
        {QStringLiteral("US/CA"),      QStringLiteral("COM"), false, true,  true,
         {QStringLiteral("us"), QStringLiteral("ca")}},
        {QStringLiteral("UK/AU"),      QStringLiteral("UK"),  false, true,  true,
         {QStringLiteral("uk"), QStringLiteral("au"), QStringLiteral("ie")}},
        {QStringLiteral("JP/MX (cm)"), QStringLiteral("JP"),  true,  false, false,
         {QStringLiteral("jp"), QStringLiteral("mx")}},
    };
}

QList<QHash<QString,double>> ShoesWomenCategory::sizeRows() const
{
    return FillerSize::SHOE_FEMALE_ADULT_SIZES;
}
