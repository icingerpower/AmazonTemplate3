#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"
#include "Attribute.h"

#include "FillerCopy.h"

bool FillerCopy::canFill(
        const TemplateFiller *templateFiller
        , const Attribute *attribute
        , const QString &marketplaceFrom
        , const QString &fieldIdFrom) const
{
    if (templateFiller->attributeFlagsTable()
            ->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::Copy))
    {
        return true;
    }
    return false;
}

QCoro::Task<void> FillerCopy::fill(
        TemplateFiller *templateFiller
        , const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
        , const QString &marketplaceFrom
        , const QString &marketplaceTo
        , const QString &fieldIdFrom
        , const QString &fieldIdTo
        , const Attribute *attribute
        , const QString &productTypeFrom
        , const QString &productTypeTo
        , const QString &countryCodeFrom
        , const QString &langCodeFrom
        , const QString &countryCodeTo
        , const QString &langCodeTo
        , const QHash<QString, QHash<QString, QString>> &countryCode_langCode_keywords
        , const QHash<QString, QHash<QString, QHash<QString, QString>>> &skuPattern_countryCode_langCode_keywords
        , Gender gender
        , Age age
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
        , const QHash<QString, QMap<QString, QString>> &sku_attribute_valuesForAi
        , const QHash<QString, QHash<QString, QString> > &sku_fieldId_toValuesFrom
        , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValueslangCommon
        , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues) const
{
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_fromValues = it.value();
        if (fieldId_fromValues.contains(fieldIdFrom) && !fieldId_fromValues[fieldIdFrom].isEmpty())
        {
            sku_fieldId_toValues[sku][fieldIdTo] = it.value()[fieldIdFrom];
        }
    }
    co_return;
}
