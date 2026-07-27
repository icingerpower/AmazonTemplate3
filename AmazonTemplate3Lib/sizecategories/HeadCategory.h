#pragma once
#include "AbstractSizeCategory.h"

// Head/fashion accessories that are worn but need no size table — hats, scarves,
// head covers, bandanas… They are fashion items, so A+ generation must show a
// model wearing them (isApparel() == true), but no size chart is produced and
// uploads are not blocked on a size table.
class HeadCategory : public AbstractSizeCategory
{
public:
    QString                      displayName()        const override;
    bool                         isApparel()          const override { return true; }
    bool                         generatesSizeChart() const override { return false; }
    QList<MeasurementField>      measurementFields()  const override { return {}; }
    QList<CountryGroup>          countryGroups()      const override { return {}; }
    QList<QHash<QString,double>> sizeRows()           const override { return {}; }
};
