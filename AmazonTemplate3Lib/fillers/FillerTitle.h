#ifndef FILLERTITLE_H
#define FILLERTITLE_H

#include "AbstractFiller.h"

class FillerTitle : public AbstractFiller
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
            , const Attribute *attribute, const QString &productType
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
    void _fixTitleFormat(QString &titleFull) const;
    QString _get_sizeCountry(TemplateFiller *templateFiller, const QString &countryCodeTo, const QString &productType, Gender gender, Age age) const;
    QHash<QString, QSet<QString>> _get_titleFrom_skus(
            const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
            , const QString &fieldIdFrom) const;
};

#endif // FILLERTITLE_H
