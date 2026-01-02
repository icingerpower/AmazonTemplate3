#include "FillerKeywords.h"


bool FillerKeywords::canFill(
        const TemplateFiller *templateFiller
        , const Attribute *attribute
        , const QString &marketplaceFrom
        , const QString &fieldIdFrom) const
{
    return fieldIdFrom == "generic_keyword#1.value" || fieldIdFrom == "generic_keyword";
}

QCoro::Task<void> FillerKeywords::fill(
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
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_toValuesFrom
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
            QString keywordsForSku;
            bool found = false;
            QStringList patterns = skuPattern_countryCode_langCode_keywords.keys();
            patterns.sort();
            for (const auto &pattern : patterns)
            {
                 if (sku.contains(pattern, Qt::CaseInsensitive))
                 {
                     const auto &countryCode_langCode_keywords_local = skuPattern_countryCode_langCode_keywords[pattern];
                     if (countryCode_langCode_keywords_local.contains(countryCodeTo)
                             && countryCode_langCode_keywords_local[countryCodeTo].contains(langCodeTo))
                     {
                         keywordsForSku = countryCode_langCode_keywords_local[countryCodeTo][langCodeTo];
                         found = true;
                         break;
                     }
                 }
            }
            if (!found)
            {
                 if (countryCode_langCode_keywords.contains(countryCodeTo)
                         && countryCode_langCode_keywords[countryCodeTo].contains(langCodeTo))
                 {
                     keywordsForSku = countryCode_langCode_keywords[countryCodeTo][langCodeTo];
                 }
            }
            if (!keywordsForSku.isEmpty())
            {
                sku_fieldId_toValues[sku][fieldIdTo] = keywordsForSku;
            }
        }
    }
    co_return;
}
