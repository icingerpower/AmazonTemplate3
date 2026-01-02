#include "FillerPrice.h"

bool FillerPrice::canFill(const TemplateFiller *, const Attribute *attribute, const QString &marketplaceFrom, const QString &fieldIdFrom) const
{
    Q_UNUSED(marketplaceFrom)
    return fieldIdFrom.contains("price");
}

QCoro::Task<void> FillerPrice::fill(
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
    if (fieldIdTo.contains("currency"))
    {
        static QHash<QString, QString> country_currency{
            {"FR", "EUR"},
            {"DE", "EUR"},
            {"IT", "EUR"},
            {"ES", "EUR"},
            {"BE", "EUR"},
            {"NL", "EUR"},
            {"IE", "EUR"},
            {"UK", "GBP"},
            {"SE", "SEK"},
            {"PL", "PLN"},
            {"TR", "TRY"},
            {"COM", "USD"},
            {"CA", "CAD"},
            {"MX", "MXN"},
            {"JP", "JPY"},
            {"AU", "AUD"},
            {"AE", "AED"},
        };
        const auto &currency = country_currency[countryCodeTo];
        for (auto it = sku_fieldId_fromValues.cbegin();
             it != sku_fieldId_fromValues.cend(); ++it)
        {
            const auto &sku = it.key();
            if (!parentSku_variation_skus.contains(sku))
            {
                sku_fieldId_toValues[sku][fieldIdTo] = currency;
            }
        }
    }
    else
    {
        static QHash<QString, double> country_rate{
            {"FR", 1.}
            , {"DE", 1.}
            , {"IT", 1.}
            , {"ES", 1.}
            , {"BE", 1.}
            , {"NL", 1.}
            , {"IE", 1.}
            , {"UK", 0.86}
            , {"SE", 11.1435}
            , {"PL", 4.2675}
            , {"TR", 46.7776}
            , {"COM", 1.1527/1.15}   // US (USD) minus tax
            , {"CA", 1.5900/1.15}
            , {"MX", 21.6193}
            , {"JP", 170.96}
            , {"AU", 1.7775}
            , {"AE", 4.2333}     // via USD peg: 1.1527 * 3.6725
        };
        Q_ASSERT(country_rate.contains(countryCodeFrom));
        Q_ASSERT(country_rate.contains(countryCodeTo));
        QString fieldIdFromCorrected{fieldIdFrom};
        if (fieldIdFrom.contains("list_price")) // Difference between list_price#1.value and list_price#1.value_with_tax
        {
            bool doneOnce = false;
            for (auto itSku = sku_fieldId_fromValues.cbegin();
                 itSku != sku_fieldId_fromValues.cend() && !doneOnce; ++itSku)
            {
                const auto &fieldId_fromValues = itSku.value();
                for (auto it = fieldId_fromValues.cbegin();
                     it != fieldId_fromValues.cend(); ++it)
                {
                    const auto &curFieldId = it.key();
                    if (curFieldId.contains("list_price") && !it.value().isEmpty())
                    {
                        fieldIdFromCorrected = curFieldId;
                        doneOnce = true;
                        break;
                    }
                }
            }

        }
        for (auto it = sku_fieldId_fromValues.cbegin();
             it != sku_fieldId_fromValues.cend(); ++it)
        {
            const auto &sku = it.key();
            if (it.value().contains(fieldIdFromCorrected))
            {
                QString priceStringFrom = it.value()[fieldIdFromCorrected];
                if (!priceStringFrom.isEmpty())
                {
                    priceStringFrom.replace(",", ".");
                    bool isDouble = false;
                    double priceFrom = priceStringFrom.toDouble(&isDouble);
                    if (isDouble)
                    {
                        double newPrice = priceFrom / country_rate[countryCodeFrom] * country_rate[countryCodeTo];
                        QString stringNewPrice = QString::number(newPrice, 'f', 2);
                        if (stringNewPrice.contains("."))
                        {
                            stringNewPrice = stringNewPrice.split(".")[0];
                        }
                        if (countryCodeTo != "JP")
                        {
                            stringNewPrice += ".99";
                        }
                        sku_fieldId_toValues[sku][fieldIdTo] = stringNewPrice;
                    }
                }
            }
        }
    }
    co_return;
}
