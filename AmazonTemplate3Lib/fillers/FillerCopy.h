#ifndef FILLERCOPY_H
#define FILLERCOPY_H

#include "AbstractFiller.h"

class FillerCopy : public AbstractFiller
{
public:
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
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues
            ) const override;
};

#endif // FILLERCOPY_H
