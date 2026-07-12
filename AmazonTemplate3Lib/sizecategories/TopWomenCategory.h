#pragma once
#include "ClothingWomenCategory.h"

// Women's tops (T-shirts, blouses, tops…): identical sizing tables, country
// groups and letter mapping as Clothing – Women, but the generated size chart
// shows a single row — the bust measurement only.
class TopWomenCategory : public ClothingWomenCategory
{
public:
    QString                 displayName()       const override;
    QList<MeasurementField> measurementFields() const override;
};
