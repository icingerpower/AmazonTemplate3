#pragma once
#include "AbstractSizeCategory.h"

class ClothingKidCategory : public AbstractSizeCategory {
public:
    QString                      displayName()       const override;
    QList<MeasurementField>      measurementFields() const override;
    QList<CountryGroup>          countryGroups()     const override;
    QString                      referenceKey()      const override { return QStringLiteral("HEIGHT"); }
    QList<QHash<QString,double>> sizeRows()          const override;
};
