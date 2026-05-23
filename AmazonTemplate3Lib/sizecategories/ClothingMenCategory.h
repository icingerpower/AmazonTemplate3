#pragma once
#include "AbstractSizeCategory.h"

class ClothingMenCategory : public AbstractSizeCategory
{
public:
    QString                      displayName()       const override;
    QList<MeasurementField>      measurementFields() const override;
    QList<CountryGroup>          countryGroups()     const override;
    QList<QHash<QString,double>> sizeRows()          const override;
    QStringList                  letterSizes()                              const override;
    QString                      letterToKey(const QString &letter)        const override;
};
