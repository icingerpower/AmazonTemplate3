#ifndef FILLERSELECTABLE_H
#define FILLERSELECTABLE_H

#include "../../common/openai/OpenAi2.h"

#include "AbstractFiller.h"

class FillerSelectable : public AbstractFiller
{
public:
    bool canFill(const TemplateFiller *templateFiller
                 , const Attribute *attribute
                 , const QString &marketplaceFrom
                 , const QString &fieldIdFrom) const override;
    QCoro::Task<void> fill(
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
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues
            ) const override;
    static void fillVariationsParents(
            const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
            , QHash<QString, QString> &sku_parentSku
            , QHash<QString, QString> &sku_variation
            );
    using EditCallback = std::function<QCoro::Task<bool>(TemplateFiller*)>;
    static void recordEditCallback(EditCallback callback);
private:
    static EditCallback EDIT_MISSING_CALLBACK;
    QString _getValueId(
            const QString &marketplaceTo
            , const QString &countryCodeTo
            , const QString &langCodeTo
            , bool allSameValue
            , bool childSameValue
            , const QString &parentSku
            , const QString &variation
            , const QString &fieldIdTo
            ) const;

    QCoro::Task<void> _fillSameLangCountry(
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
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues
            ) const;
    QCoro::Task<void> _fillDifferentLangCountry(
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
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues
            ) const;

};

#endif // FILLERSELECTABLE_H
