#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"
#include "Attribute.h"

#include "FillerCopy.h"

bool FillerCopy::canFill(
        const TemplateFiller *templateFiller
        , const QString &marketplace
        , const QString &fieldId) const
{
    if (templateFiller->attributeFlagsTable()
            ->hasFlag(marketplace, fieldId, Attribute::Copy))
    {
        return true;
    }
    return false;
}

QCoro::Task<void> FillerCopy::fill(
        TemplateFiller *templateFiller // TODO handle working space for reply saving / reloading
        , const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
        , const QString &marketplaceFrom
        , const QString &marketplaceTo
        , const QString &fieldIdFrom
        , const QString &fieldIdTo
        , const QString &productType
        , const QString &countryCodeFrom
        , const QString &langCodeFrom
        , const QString &countryCodeTo
        , const QString &langCodeTo
        , const QString &keywords
        , Gender gender
        , Age age
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
        , const QHash<QString, QMap<QString, QString>> &sku_attribute_valuesForAi
        , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValueslangCommon
        , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues) const
{
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        sku_fieldId_toValues[sku][fieldIdTo] = it.value()[fieldIdFrom];
    }
    co_return;
}
