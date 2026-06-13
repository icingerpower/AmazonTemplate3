#pragma once
#include "AbstractSizeCategory.h"

class ShoesMenCategory : public AbstractSizeCategory
{
public:
    QString                      displayName()       const override;
    QList<MeasurementField>      measurementFields() const override;
    QList<CountryGroup>          countryGroups()     const override;
    QList<QHash<QString,double>> sizeRows()          const override;
    bool                         allGroupsAlwaysVisible() const override { return true; }
};
