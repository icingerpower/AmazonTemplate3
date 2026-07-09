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
    static void initCatBools(
            QSettings *settings
            , const QString &productType
            , bool &isShoes
            , bool &isClothe
            , bool &isNoSizeConv
            );

    static const QList<QHash<QString, int>> CLOTHE_FEMALE_ADULT_SIZES;
    static const QList<QHash<QString, int>> CLOTHE_MALE_ADULT_SIZES;
    static const QList<QHash<QString, double>> SHOE_FEMALE_ADULT_SIZES;
    static const QList<QHash<QString, double>> SHOE_MALE_ADULT_SIZES;

    // Convert a size value from one country to another using the static tables.
    // countryFrom/To: country code as used in the tables (e.g. "FR","DE","UK","US","COM").
    // gender: "female"/"male" (case-insensitive); defaults to female.
    // isShoes: use shoe tables instead of clothing tables.
    // Returns the converted size as a string, or origSize unchanged if no mapping found.
    static QString convertSize(const QString &origSize,
                               const QString &countryFrom,
                               const QString &countryTo,
                               const QString &gender = QStringLiteral("female"),
                               bool isShoes = false);

    // Localizes a physical dimension string to the target country's unit system:
    // metric countries → cm only (converts an inch value if that's all it has);
    // English/inch countries → "inch / cm". Values with no cm/inch unit (e.g. a
    // plain clothing size like "M" or "38") are returned unchanged. Pure (no
    // member state), so callable without an instance.
    static QVariant convertUnit(const QString &countryTo, const QVariant &origValue);


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

    QCoro::Task<void> askAiToUpdateSettingsForProductType(
            QString productType,
            QSettings *settings) const;
};

#endif // FILLERSIZE_H
