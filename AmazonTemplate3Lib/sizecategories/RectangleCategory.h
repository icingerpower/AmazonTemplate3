#pragma once
#include "AbstractSizeCategory.h"

// Fixed-dimension products (health book covers, carpets…) whose size is a
// plain "21x15 cm" dimension string, possibly different per variant.
// No size chart is generated for them; the category exists so the product
// records its type, uploads are not blocked on a size table, and A+ prompts
// get their own per-category set.
class RectangleCategory : public AbstractSizeCategory
{
public:
    QString                      displayName()        const override;
    bool                         isApparel()          const override { return false; }
    bool                         generatesSizeChart() const override { return false; }
    QList<MeasurementField>      measurementFields()  const override { return {}; }
    QList<CountryGroup>          countryGroups()      const override { return {}; }
    QList<QHash<QString,double>> sizeRows()           const override { return {}; }
};
