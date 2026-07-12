#include "TopMenCategory.h"

#include <QObject>

QString TopMenCategory::displayName() const
{
    return QObject::tr("Top – Men");
}

QList<MeasurementField> TopMenCategory::measurementFields() const
{
    // Bust/chest only — no height or garment-length rows.
    return {
        {QStringLiteral("chest"), QObject::tr("Chest (cm)"), 2.0, {}},
    };
}

QList<QHash<QString,double>> TopMenCategory::sizeRows() const
{
    // Men's EU sizes 44..66 (Clothing – Men starts at 48; tops start at 44).
    // EU markets = the size; UK/US/IE/AU/… = size − 10 (matches Clothing – Men).
    static const QStringList euCodes{
        QStringLiteral("FR"), QStringLiteral("BE"), QStringLiteral("ES"),
        QStringLiteral("TR"), QStringLiteral("DE"), QStringLiteral("NL"),
        QStringLiteral("SE"), QStringLiteral("PL"), QStringLiteral("IT")};
    static const QStringList inchCodes{
        QStringLiteral("IE"), QStringLiteral("UK"), QStringLiteral("AU"),
        QStringLiteral("COM"), QStringLiteral("CA"), QStringLiteral("AE"),
        QStringLiteral("MX"), QStringLiteral("SA"), QStringLiteral("SG")};

    QList<QHash<QString,double>> result;
    for (int eu = 44; eu <= 66; eu += 2) {
        QHash<QString,double> row;
        for (const QString &c : euCodes)   row.insert(c, eu);
        for (const QString &c : inchCodes) row.insert(c, eu - 10);
        result << row;
    }
    return result;
}
