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
    // Structured context of a "no equivalent value" error, so the callback can
    // offer the choices directly and record the fix in the equivalence table.
    struct MissingValueInfo {
        QString fieldIdAmzV02;      // Key used by AttributeEquivalentTable
        QString fromValue;          // Value that has no known equivalent
        QSet<QString> possibleValues; // Allowed values on the target marketplace
    };
    using EditCallback = std::function<QCoro::Task<bool>(TemplateFiller*, const QString &error, const QString &message, const MissingValueInfo &info)>;
    static void recordEditCallback(EditCallback callback);

    using SelectValueCallback = std::function<QCoro::Task<QString>(
        TemplateFiller*,
        const QString &title,
        const QString &description,
        const QString &aiPrompt,
        const QString &imagePath,
        const QSet<QString> &possibleValues)>;
    static void recordSelectValueCallback(SelectValueCallback callback);

private:
    static EditCallback EDIT_MISSING_CALLBACK;
    static SelectValueCallback SELECT_VALUE_CALLBACK;
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
