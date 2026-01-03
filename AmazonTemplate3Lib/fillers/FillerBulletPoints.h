#ifndef FILLERBULLETPOINTS_H
#define FILLERBULLETPOINTS_H

#include "AbstractFiller.h"

class FillerBulletPoints : public AbstractFiller
{
public:
    static const QSet<QString> BULLET_POINT_IDS;
    static const QStringList BULLET_POINT_PATTERNS;
    static const QString BULLET_POINT_PATTERN_MAIN;
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
};

#endif // FILLERBULLETPOINTS_H
