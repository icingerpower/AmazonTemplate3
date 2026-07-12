#pragma once
#include "ClothingMenCategory.h"

// Men's tops (T-shirts, shirts, polos…): identical sizing tables, country
// groups and letter mapping as Clothing – Men, but the generated size chart
// shows a single row — the chest/bust measurement only.
class TopMenCategory : public ClothingMenCategory
{
public:
    QString                      displayName()       const override;
    QList<MeasurementField>      measurementFields() const override;
    // Men's tops start at EU size 44 (Clothing – Men starts at 48).
    QList<QHash<QString,double>> sizeRows()          const override;
};
