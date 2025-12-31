#ifndef FILLERSIZE_H
#define FILLERSIZE_H

#include <QSettings>

#include "AbstractFiller.h"

class FillerSize : public AbstractFiller
{
public:
    static const QString KEY_SHOE_WORDS;
    static const QString KEY_CLOTHE_WORDS;
    static const QString KEY_CAT_NO_CONV_WORDS;
    bool canFill(const TemplateFiller *templateFiller
                 , const QString &marketplace
                 , const QString &fieldId) const override;
    QCoro::Task<void> fill(
            TemplateFiller *templateFiller
            , const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
            , const QString &marketplaceFrom
            , const QString &marketplaceTo
            , const QString &fieldIdFrom
            , const QString &fieldIdTo
            , const Attribute *attribute
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
            , const QHash<QString, QHash<QString, QString>> &sku_fieldId_toValuesFrom
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValueslangCommon
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues
            ) const override;

private:
    QVariant convertClothingSize(
            const QString &countryFrom,
            const QString &countryTo,
            const QString &langTo,
            Gender targetGender,
            Age age_range_description,
            const QString &productType,
            const QVariant &origValue) const;

    QVariant convertShoeSize(
            const QString &countryFrom,
            const QString &countryTo,
            Gender targetGender,
            Age age_range_description,
            const QString &productType,
            const QVariant &origValue) const;

    QVariant convertUnit(
            const QString &countryTo,
            const QVariant &origValue) const;

    QCoro::Task<void> askAiToUpdateSettingsForProductType(
            QString productType,
            QSettings *settings) const;
    void _initCatBools(
            QSettings *settings
            , const QString &productType
            , bool &isShoes
            , bool &isClothe
            , bool &isNoSizeConv
            ) const;
};

#endif // FILLERSIZE_H
