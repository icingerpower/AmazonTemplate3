#include "../../common/openai/OpenAi2.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "Attribute.h"
#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"

#include "FillerText.h"
#include "AiFailureTable.h"

#include "FillerCopy.h"
#include "FillerPrice.h"
#include "FillerSize.h"
#include "FillerSelectable.h"
#include "FillerBulletPoints.h"
#include "FillerTitle.h"
#include "FillerKeywords.h"

bool FillerText::canFill(
        const TemplateFiller *templateFiller
        , const Attribute *attribute
        , const QString &marketplaceFrom
        , const QString &fieldIdFrom) const
{
    QList<const AbstractFiller *> otherFillers;
    const FillerCopy fillerCopy;
    otherFillers << &fillerCopy;
    const FillerPrice fillerPrice;
    otherFillers << &fillerPrice;
    const FillerSize fillerSize;
    otherFillers << &fillerSize;
    const FillerBulletPoints fillerBulletPoints;
    otherFillers << &fillerBulletPoints;
    const FillerSelectable fillerSelectable;
    otherFillers << &fillerSelectable;
    const FillerTitle fillerTitle;
    otherFillers << &fillerTitle;
    const FillerKeywords fillerKeywords;
    otherFillers << &fillerKeywords;
    for (const auto &filler : otherFillers)
    {
        if (filler->canFill(templateFiller,
                            attribute,
                            marketplaceFrom,
                            fieldIdFrom))
        {
            return false;
        }
    }
    return true;
}

const QHash<QString, int> FillerText::FIELD_ID_MAX_CHAR
{
    {"color#1.value", 40}
    , {"item_type_name#1.value", 40}
    , {"fabric_type#1.value", 100}
};

QCoro::Task<void> FillerText::fill(
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
    if (langCodeFrom == langCodeTo)
    {
        for (auto it = sku_fieldId_fromValues.cbegin();
             it != sku_fieldId_fromValues.cend(); ++it)
        {
            const auto &sku = it.key();
            const auto &fieldId_fromValues = it.value();
            if (fieldId_fromValues.contains(fieldIdFrom) && !fieldId_fromValues[fieldIdFrom].isEmpty())
            {
                const auto &curValue = it.value()[fieldIdFrom];
                sku_fieldId_toValues[sku][fieldIdTo] = curValue;
                recordAllMarketplace(
                            templateFiller, marketplaceTo, fieldIdTo, sku_fieldId_toValueslangCommon[sku], curValue);
            }
        }
    }

    const QString settingsFileName{"filledTexts.ini"};
    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    bool childSameValue = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::ChildSameValue);
    bool allSameValue = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::SameValue);
    QHash<QString, QString> sku_parentSku;
    QHash<QString, QString> sku_variation;
    FillerSelectable::fillVariationsParents(parentSku_variation_skus, sku_parentSku, sku_variation);
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        if (sku_fieldId_toValueslangCommon.contains(fieldIdTo))
        {
            sku_fieldId_toValues[fieldIdTo] = sku_fieldId_toValueslangCommon[fieldIdTo];
        }
        const auto &fieldId_fromValues = it.value();
        QString valueId = "all_" + langCodeTo + "_" + fieldIdTo;
        if (!allSameValue && childSameValue)
        {
            valueId += "_" + sku_parentSku[sku];
        }
        if (!allSameValue && !childSameValue)
        {
            valueId += "_" + sku_parentSku[sku] + "_" + sku_variation[sku];
        }
        const QMap<QString, QString> &valuesForAi = sku_attribute_valuesForAi[sku];
        const auto &parseAndValidate = [&fieldIdTo](const QString &reply, QString &valFormatted) -> bool
        {
            QJsonParseError error;
            auto json = QJsonDocument::fromJson(reply.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError)
            {
                return false;
            }
            if (!json.isObject())
            {
                return false;
            }
            auto obj = json.object();
            if (!obj.contains("value"))
            {
                return false;
            }
            valFormatted = obj["value"].toString();
            if (FIELD_ID_MAX_CHAR.contains(fieldIdTo)
                    && valFormatted.size() > FIELD_ID_MAX_CHAR[fieldIdTo])
            {
                return false;
            }
            return !valFormatted.isEmpty();
        };

        const auto &apply = [&](const QString &reply, const QString &valFormatted)
        {
             templateFiller->saveAiValue(settingsFileName, valueId, reply);
             sku_fieldId_toValues[sku][fieldIdTo] = valFormatted;
             recordAllMarketplace(
                         templateFiller, marketplaceTo, fieldIdTo, sku_fieldId_toValueslangCommon[sku], valFormatted);
             sku_fieldId_toValues[sku][fieldIdTo] = valFormatted;
        };
        if (templateFiller->hasAiValue(settingsFileName, valueId))
        {
            QString valFormatted;
            QString reply = templateFiller->getAiReply(settingsFileName, valueId);
            if (parseAndValidate(reply, valFormatted))
            {
                apply(reply, valFormatted);
                continue;
            }
        }
        QString valueFrom;
        if (sku_fieldId_toValuesFrom.contains(sku) && sku_fieldId_toValuesFrom[sku].contains(fieldIdFrom) && !sku_fieldId_toValuesFrom[sku][fieldIdFrom].isEmpty())
        {
            valueFrom = sku_fieldId_toValuesFrom[sku][fieldIdFrom]; //If valueFrom we only translate
        }
        if (!valueFrom.isEmpty())
        {
            QString prompt = "Translate the following text from " + langCodeFrom + " to " + langCodeTo + ".\n"
                    "Product Type: " + productTypeTo + "\n"
                    "Field ID: " + fieldIdTo + "\n"
                    "Text to translate: \"" + valueFrom + "\"\n"
                    "Output a JSON object with the key \"value\" containing the translated text.";
            auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
            step->getPrompt = [prompt](int){ return prompt; };
            step->maxRetries = 5;
            step->neededReplies = 1;
            step->validate = [&](const QString &reply, const QString &)
            {
                QString val;
                return parseAndValidate(reply, val);
            };
            step->chooseBest = [](const QList<QString> &replies) -> QString
            {
                return replies.isEmpty() ? QString() : replies.first();
            };
            step->apply = [&](const QString &reply)
            {
                QString valFormatted;
                if (parseAndValidate(reply, valFormatted))
                {
                    apply(reply, valFormatted);
                }
            };
            step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo](
                    const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
            {
                QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                        .arg(QString::number(networkError), reply, lastWhy);
                templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, errorMsg);
                return true; 
            };
            QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;
            steps << step;
            qDebug() << "--\nFillerText::fill no valueFrom:" << step->getPrompt(0);
            co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
        }
        else
        {
            auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
            QString prompt;
            bool isDescription = fieldIdTo.contains("product_description", Qt::CaseInsensitive);
            prompt += "Lang: " + langCodeTo + ". Product Type: " + productTypeTo + ". Field: " + fieldIdTo + "\n";
            prompt += "Attributes: ";
            for (auto it = valuesForAi.begin(); it != valuesForAi.end(); ++it)
            {
                prompt += it.key() + ": " + it.value() + ", ";
            }
            prompt += "\n";

            if (isDescription)
            {
                prompt += "Write a description that increases perceived value while respecting Amazon policies, strictly under 2000 characters. "
                          "Do not invent information; only state verifiable facts. Simple HTML allowed. "
                          "Answer important buyer questions (compatibility, dimensions, materials, etc).";
            }
            else
            {
                if (FIELD_ID_MAX_CHAR.contains(fieldIdTo))
                {
                    prompt += "Write a short set of words for this field. Under " + QString::number(FIELD_ID_MAX_CHAR[fieldIdTo]) + " characters.";
                }
                else
                {
                    prompt += "Write a short text for this field.";
                }
            }
            prompt += "\nOutput a JSON object with the key \"value\" containing the generated text.";

            step->getPrompt = [prompt](int){ return prompt; };
            step->maxRetries = 5;
            step->neededReplies = 2; // Ask for 2 replies
            step->validate = [&](const QString &reply, const QString &)
            {
                QString val;
                return parseAndValidate(reply, val);
            };
            step->chooseBest = [isDescription](const QStringList &replies) -> QString
            {
                 QString bestReply;
                 if (replies.isEmpty())
                 {
                     return bestReply;
                 }
                 int bestLen = -1;
                 for (const auto &reply : replies)
                 {
                     QJsonParseError error;
                     auto doc = QJsonDocument::fromJson(reply.toUtf8(), &error);
                     if (error.error == QJsonParseError::NoError && doc.isObject())
                     {
                         QString val = doc.object()["value"].toString();
                         int len = val.length();
                         if (isDescription)
                         {
                             if (len < 1900 && len > bestLen)
                             {
                                 bestLen = len;
                                 bestReply = reply;
                             }
                         }
                         else
                         {
                             // Pick smallest
                             if (bestLen == -1 || len < bestLen)
                             {
                                 bestLen = len;
                                 bestReply = reply;
                             }
                         }
                     }
                 }
                 // Fallback if no specific criteria met (e.g. all > 1900), just return first valid
                 if (bestReply.isEmpty() && !replies.isEmpty())
                 {
                     return replies.first();
                 }
                 return bestReply;
            };
            step->apply = [&](const QString &reply) {
                QString valFormatted;
                if (parseAndValidate(reply, valFormatted))
                {
                    apply(reply, valFormatted);
                }
            };
            step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
            {
                QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                        .arg(QString::number(networkError), reply, lastWhy);
                templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, errorMsg);
                return true; 
            };
            QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;
            steps << step;
            qDebug() << "--\nFillerText::fill with valueFrom (translate):" << step->getPrompt(0);
            co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
        }
    }
    co_return;
}
