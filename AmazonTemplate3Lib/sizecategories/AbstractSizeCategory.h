#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QPair>
#include <QMap>
#include <QImage>

class QStandardItemModel;
class QObject;

struct MeasurementField {
    QString id;
    QString label;
    double  defaultStep = 2.0;
    QString derivedKey;
};

struct CountryGroup {
    QString     label;
    QString     key;
    bool        isCm      = false;
    bool        isFloat   = false;
    bool        isEnglish = false;
    QStringList codes;  // explicit lowercase country codes belonging to this group
};

struct MeasurementInput {
    double refValue  = 0.0;
    double step      = 2.0;
    double rangeVal  = 0.0;
};

class AbstractSizeCategory {
public:
    virtual ~AbstractSizeCategory() = default;
    virtual QString                      displayName()       const = 0;
    virtual bool                         isApparel()         const { return true; }
    // Returns true when all country-group rows must always be visible together
    // (e.g. shoes, where EU/US/UK/JP all appear on the same chart).
    // When true, _renderAndSaveChart skips group-row filtering for translated charts.
    virtual bool                         allGroupsAlwaysVisible() const { return false; }
    virtual QList<MeasurementField>      measurementFields() const = 0;
    virtual QList<CountryGroup>          countryGroups()     const = 0;
    virtual QString                      referenceKey()      const { return QStringLiteral("FR"); }
    virtual QList<QHash<QString,double>> sizeRows()          const = 0;

    virtual QStringList letterSizes() const { return {}; }
    virtual QString letterToKey(const QString &letter) const { Q_UNUSED(letter); return {}; }

    QStringList          referenceKeys()                                              const;
    QPair<QString,QString> guessRange(const QStringList &rawSizes)                   const;
    QStandardItemModel*  buildTable(const QString &keyFrom, const QString &keyTo,
                                    const QMap<QString,MeasurementInput> &measurements,
                                    QObject *parent = nullptr)                        const;
    QImage               renderImage(QStandardItemModel *model)                       const;

    QList<QPair<QString,QImage>> renderGroupImages(
        const QString &keyFrom,
        const QString &keyTo,
        const QMap<QString,MeasurementInput> &measurements,
        const QStringList &letterLabels = {}) const;

private:
    int    _findIndex(const QString &key)                                             const;
    static QString _formatVal(double val, bool isCm, bool isFloat);
    static QImage  _renderRows(const QList<QPair<QString,QStringList>> &rows);
};
