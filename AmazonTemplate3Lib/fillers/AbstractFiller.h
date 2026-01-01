#ifndef ABSTRACTFILLER_H
#define ABSTRACTFILLER_H

#include <QString>

#include <QCoroTask>

class Attribute;

class TemplateFiller;

class AbstractFiller
{
public:
    enum Gender{
        Female,
        Male,
        Unisex,
        UndefinedGender
    };
    enum Age{
        Adult,
        Kid,
        Baby,
        UndefinedAge
    };
    static const QList<const AbstractFiller *> ALL_FILLERS_SORTED;
    static QCoro::Task<void> fillValuesForAi(
            TemplateFiller *templateFiller
            , const QString &productType
            , const QString &countryCodeFrom
            , const QString &langCodeFrom
            , Gender gender
            , Age age
            , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
            , QHash<QString, QMap<QString, QString>> sku_attribute_valuesForAi
            );
    static void recordAllMarketplace(
            const TemplateFiller *templateFiller
            , const QString &marketplace
            , const QString &fieldId
            , QHash<QString, QString> &fieldId_values
            , const QString &value);

    virtual bool canFill(const TemplateFiller *templateFiller
                         , const Attribute *attribute
                         , const QString &marketplace
                         , const QString &fieldId) const = 0;
    virtual QCoro::Task<void> fill(
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
            , const QString &keywords
            , Gender gender
            , Age age
            , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
            , const QHash<QString, QMap<QString, QString>> &sku_attribute_valuesForAi
            , const QHash<QString, QHash<QString, QString>> &sku_fieldId_toValuesFrom
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValueslangCommon
            , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues
            ) const = 0;
};

#endif // ABSTRACTFILLER_H
