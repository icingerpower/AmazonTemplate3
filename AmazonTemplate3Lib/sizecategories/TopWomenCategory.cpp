#include "TopWomenCategory.h"

#include <QObject>

QString TopWomenCategory::displayName() const
{
    return QObject::tr("Top – Women");
}

QList<MeasurementField> TopWomenCategory::measurementFields() const
{
    // Bust only — no waist or hip rows.
    return {
        {QStringLiteral("bust"), QObject::tr("Bust (cm)"), 3.0, {}},
    };
}
