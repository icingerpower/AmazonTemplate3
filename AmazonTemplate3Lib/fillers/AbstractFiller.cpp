#include "TemplateFiller.h"
#include "FillerCopy.h"

#include "AbstractFiller.h"

const QList<const AbstractFiller *> AbstractFiller::ALL_FILLERS_SORTED
= []() -> QList<const AbstractFiller *>
{
        QList<const AbstractFiller *> allFillers;
        static const FillerCopy fillerCopy;
        allFillers << &fillerCopy;
        return allFillers;
        }();

QCoro::Task<void> AbstractFiller::fillValuesForAi(TemplateFiller *templateFiller
        , const QString &productType
        , const QString &countryCodeFrom
        , const QString &langCodeFrom
        , Gender gender
        , Age age
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
        , QHash<QString, QMap<QString, QString> > sku_attribute_valuesForAi)
{
    co_return;
}
