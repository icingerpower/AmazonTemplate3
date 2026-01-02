
#include <algorithm>
#include <QMap>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../common/openai/OpenAi2.h"

#include "Attribute.h"
#include "TemplateFiller.h"
#include "AttributeEquivalentTable.h"
#include "AttributeFlagsTable.h"
#include "ExceptionTemplate.h"

#include "FillerSelectable.h"
#include "AiFailureTable.h"


bool FillerSelectable::canFill(
        const TemplateFiller *templateFiller
        , const Attribute *attribute
        , const QString &marketplaceFrom
        , const QString &fieldIdFrom) const
{
    return attribute->isChoice(marketplaceFrom);
}

QCoro::Task<void> FillerSelectable::fill(
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
    const auto &possibleValues = attribute->possibleValues(
                marketplaceTo, countryCodeTo, langCodeTo, productTypeTo);
    if (possibleValues.size() == 1)
    {
        const auto &uniquePossibleValue = *possibleValues.cbegin();
        for (auto it = sku_fieldId_fromValues.cbegin();
             it != sku_fieldId_fromValues.cend(); ++it)
        {
            const auto &sku = it.key();
            sku_fieldId_toValues[sku][fieldIdTo] = uniquePossibleValue;
        }
    }
    else if (possibleValues.size() > 0)
    {
        if (countryCodeFrom == countryCodeTo && langCodeFrom == langCodeTo)
        {
            co_await _fillSameLangCountry(
                        templateFiller
                        , parentSku_variation_skus
                        , marketplaceFrom
                        , marketplaceTo
                        , fieldIdFrom
                        , fieldIdTo
                        , attribute
                        , productTypeFrom
                        , productTypeTo
                        , countryCodeFrom
                        , langCodeFrom
                        , countryCodeTo
                        , langCodeTo
                        , countryCode_langCode_keywords
                        , skuPattern_countryCode_langCode_keywords
                        , gender
                        , age
                        , sku_fieldId_fromValues
                        , sku_attribute_valuesForAi
                        , sku_fieldId_toValuesFrom
                        , sku_fieldId_toValueslangCommon
                        , sku_fieldId_toValues
                        );
        }
        else
        {
            co_await _fillDifferentLangCountry(
                        templateFiller
                        , parentSku_variation_skus
                        , marketplaceFrom
                        , marketplaceTo
                        , fieldIdFrom
                        , fieldIdTo
                        , attribute
                        , productTypeFrom
                        , productTypeTo
                        , countryCodeFrom
                        , langCodeFrom
                        , countryCodeTo
                        , langCodeTo
                        , countryCode_langCode_keywords
                        , skuPattern_countryCode_langCode_keywords
                        , gender
                        , age
                        , sku_fieldId_fromValues
                        , sku_attribute_valuesForAi
                        , sku_fieldId_toValuesFrom
                        , sku_fieldId_toValueslangCommon
                        , sku_fieldId_toValues
                        );

        }
    }
    else
    {
        ExceptionTemplate exception;
        exception.setInfos(
                    QObject::tr("No possible values")
                    , QObject::tr("No possible value for the field %1 / %2").arg(marketplaceTo, fieldIdTo));
        exception.raise();
    }
    co_return;
}

void FillerSelectable::fillVariationsParents(
        const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
        , QHash<QString, QString> &sku_parentSku
        , QHash<QString, QString> &sku_variation)
{
    for (auto itParent = parentSku_variation_skus.begin();
         itParent != parentSku_variation_skus.end(); ++itParent)
    {
        const auto &skuParent = itParent.key();
        for (auto itVar = itParent.value().begin();
             itVar != itParent.value().end(); ++itVar)
        {
            const auto &variation = itVar.key();
            for (const auto &sku : itVar.value())
            {
                sku_parentSku[sku] = skuParent;
                sku_variation[sku] = variation;
            }
        }
    }
}

static QSharedPointer<OpenAi2::StepMultipleAsk> createSelectStep(
        const QString &id
        , const QString &marketplace
        , const QString &fieldId
        , const QMap<QString, QString> &valuesForAi
        , const QSet<QString> &possibleValues)
{
    QSharedPointer<OpenAi2::StepMultipleAsk> step(new OpenAi2::StepMultipleAsk);
    step->id = id;
    step->name = "Select value for " + fieldId;
    step->cachingKey = step->id;
    step->gptModel = "gpt-5.2";
    step->maxRetries = 10;

    step->getPrompt = [marketplace, fieldId, valuesForAi, possibleValues](int nAttempts) -> QString
    {
        Q_UNUSED(nAttempts)
        QString prompt = QString("Marketplace: %1\n").arg(marketplace);
        prompt += QString("Field: %1\n").arg(fieldId);
        prompt += "Product Attributes:\n";
        for (auto it = valuesForAi.begin(); it != valuesForAi.end(); ++it)
        {
            if (!it.value().isEmpty())
            {
                prompt += QString("- %1: %2\n").arg(it.key(), it.value());
            }
        }
        prompt += "\nPossible Values:\n";
        Q_ASSERT(possibleValues.size() < 50); // Shold not happen
        QList<QString> sortedValues = possibleValues.values();
        std::sort(sortedValues.begin(), sortedValues.end());
        for (const auto &val : sortedValues)
        {
            prompt += QString("- %1\n").arg(val);
        }
        prompt += "\nInstruction: Select the most appropriate value from the 'Possible Values' list that matches the product attributes. Reply ONLY with a valid JSON object with key \"value\" containing the exact selected value. Example: {\"value\": \"Selected Value\"}. If no value matches, use \"value\": \"UNKNOWN\".";
        return prompt;
    };

    step->validate = [possibleValues](const QString &gptReply, const QString &lastWhy) -> bool
    {
        Q_UNUSED(lastWhy)
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(gptReply.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }
        QJsonObject obj = doc.object();
        if (!obj.contains("value"))
        {
            return false;
        }
        QString val = obj.value("value").toString();
        return possibleValues.contains(val) || val == "UNKNOWN";
    };

    // apply not used directly here, managed in caller
    step->apply = [](const QString &reply) {
        Q_UNUSED(reply)
    };

    return step;
}

QCoro::Task<void> FillerSelectable::_fillSameLangCountry(
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
    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    bool childSameValue = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::ChildSameValue);
    bool allSameValue = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::SameValue);
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_fromValues = it.value();
        if (fieldId_fromValues.contains(fieldIdFrom) && !fieldId_fromValues[fieldIdFrom].isEmpty())
        {
            sku_fieldId_toValues[sku][fieldIdTo] = it.value()[fieldIdFrom];
        }
    }

    const QString &fieldIdToV02 = templateFiller->attributeFlagsTable()->getFieldId(
                marketplaceTo, fieldIdTo, Attribute::AMAZON_V02);
    const auto &possibleValues = attribute->possibleValues(
                marketplaceTo, countryCodeTo, langCodeTo, productTypeTo);
    QHash<QString, QString> sku_parentSku;
    QHash<QString, QString> sku_variation;
    fillVariationsParents(parentSku_variation_skus, sku_parentSku, sku_variation);
    const QString settingsFileName{"selectedValues.ini"};

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_toValues = sku_fieldId_toValues[sku];
        if (!fieldId_toValues.contains(fieldIdTo) || fieldId_toValues[fieldIdTo].isEmpty())
        {
            const QMap<QString, QString> &valuesForAi = sku_attribute_valuesForAi[sku];
            QString valueId = "all_" + marketplaceTo + "_" + countryCodeTo + "_" + langCodeTo;
            if (!allSameValue && childSameValue)
            {
                valueId += "_" + sku_parentSku[sku];
            }
            if (!allSameValue && !childSameValue)
            {
                valueId += "_" + sku_parentSku[sku] + "_" + sku_variation[sku];
            }
            // Add fieldIdTo to unique ID to avoid collisions between fields
             valueId += "_" + fieldIdTo;

            QString selectedValue;
            bool found = false;
            
            // Helper to parse value from JSON
            auto parseValue = [](const QString &json) -> QString {
                 QJsonParseError error;
                 QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
                 if (error.error == QJsonParseError::NoError && doc.isObject())
                 {
                     return doc.object().value("value").toString();
                 }
                 return QString();
            };

            // Check cache
            if (templateFiller->hasAiValue(settingsFileName, valueId))
            {
                QString cachedReply = templateFiller->getAiReply(settingsFileName, valueId);
                // Validate cached reply using the step creation logic (re-using createSelectStep for validation logic)
                // We create a temp step just to use its validate function? Or just duplicate logic?
                // Re-creating step is safer to ensure consistency.
                auto step = ::createSelectStep(valueId, marketplaceTo, fieldIdTo, valuesForAi, possibleValues);
                if (step->validate(cachedReply, ""))
                {
                    QString val = parseValue(cachedReply);
                    if (possibleValues.contains(val))
                    {
                        selectedValue = val;
                        found = true;
                    }
                }
            }

            if (!found)
            {
                // Phase 1: Ask 2 times, agreement check
                auto step = ::createSelectStep(valueId + "_p1", marketplaceTo, fieldIdTo, valuesForAi, possibleValues);
                step->neededReplies = 2;
                step->chooseBest = OpenAi2::CHOOSE_ALL_SAME_OR_EMPTY; // Returns empty if not all same
                
                QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;
                steps.append(step);
                
                QString phase1Result;
                step->apply = [&](const QString &reply) {
                    phase1Result = reply;
                };
                step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
                {
                    QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                            .arg(QString::number(networkError), reply, lastWhy);
                    templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, errorMsg);
                    return true;
                };

                for (const auto &step : steps)
                {
                    qDebug() << "--\nFillerSelectable::fill PHASE 1:" << step->getPrompt(0);
                }
                co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
                 
                 // phase1Result is the JSON string if successful/agreed
                 if (!phase1Result.isEmpty())
                 {
                     QString val = parseValue(phase1Result);
                     if (possibleValues.contains(val))
                     {
                         selectedValue = val;
                         found = true;
                         // Save valid JSON reply to cache
                         templateFiller->saveAiValue(settingsFileName, valueId, phase1Result);
                     }
                 }
                 
                 if (!found)
                 {
                     // Phase 2: Ask 5 times, frequent
                     step->id = valueId + "_p2"; // Change ID to avoid cache collision
                     step->cachingKey = step->id;
                     step->neededReplies = 5;
                     step->chooseBest = OpenAi2::CHOOSE_MOST_FREQUENT;
                     step->maxRetries = 10;
                     
                     steps.clear();
                     steps.append(step);
                     
                     QString phase2Result;
                     step->apply = [&](const QString &reply) {
                        phase2Result = reply;
                     };
                     step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
                     {
                         QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                                 .arg(QString::number(networkError), reply, lastWhy);
                         templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, errorMsg);
                         return true;
                     };
                     
                     for (const auto &step : steps)
                     {
                         qDebug() << "--\nFillerSelectable::fill PHASE 2:" << step->getPrompt(0);
                     }
                     co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
                     
                     if (!phase2Result.isEmpty())
                     {
                         QString val = parseValue(phase2Result);
                         if (possibleValues.contains(val))
                         {
                             selectedValue = val;
                             found = true;
                             // Save valid JSON reply to cache
                             templateFiller->saveAiValue(settingsFileName, valueId, phase2Result);
                         }
                     }
                 }
            }

            if (found && !selectedValue.isEmpty())
            {
                 sku_fieldId_toValues[sku][fieldIdTo] = selectedValue;
            }
        }
    }
    co_return;
}

QCoro::Task<void> FillerSelectable::_fillDifferentLangCountry(
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
    const QString &fieldIdToV02 = templateFiller->attributeFlagsTable()->getFieldId(
                marketplaceTo, fieldIdTo, Attribute::AMAZON_V02);
    AttributeEquivalentTable *equivalentTable
            = templateFiller->attributeEquivalentTable();
    const auto &possibleValues = attribute->possibleValues(
                marketplaceTo, countryCodeTo, langCodeTo, productTypeTo);
    QList<QSharedPointer<QCoro::Task<void>>> tasks;
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_toValuesFrom = sku_fieldId_toValuesFrom[sku];
        if (fieldId_toValuesFrom.contains(fieldIdFrom) && !fieldId_toValuesFrom[fieldIdFrom].isEmpty())
        {
            const auto &fromValue = fieldId_toValuesFrom[fieldIdFrom];
            if (!equivalentTable->hasEquivalent(fieldIdToV02, fromValue, possibleValues))
            {
                if (equivalentTable->hasEquivalent(fieldIdToV02, fromValue)) // One value is missing
                {
                    auto task =  equivalentTable->askAiEquivalentValues(
                                fieldIdToV02, fromValue, langCodeFrom, langCodeTo, possibleValues);
                    tasks << QSharedPointer<QCoro::Task<void>>::create(std::move(task));
                }
                else
                {
                    auto task = equivalentTable->askAiEquivalentValues(
                                fieldIdToV02, fromValue, attribute);
                    tasks << QSharedPointer<QCoro::Task<void>>::create(std::move(task));
                }
            }
        }
    }
    for (auto &task : tasks)
    {
        co_await *task;
    }
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_toValuesFrom = sku_fieldId_toValuesFrom[sku];
        if (fieldId_toValuesFrom.contains(fieldIdFrom)
                && !fieldId_toValuesFrom[fieldIdFrom].isEmpty())
        {
            const auto &fromValue = fieldId_toValuesFrom[fieldIdFrom];
            if (equivalentTable->hasEquivalent(fieldIdToV02, fromValue, possibleValues))
            {
                const QString &toValue = equivalentTable->getEquivalentValue(fieldIdToV02, fromValue, possibleValues);
                sku_fieldId_toValues[sku][fieldIdTo] = toValue;
            }
            else
            {
                auto possibleValuesList = possibleValues.values();
                possibleValuesList.sort();
                ExceptionTemplate exception;
                exception.setInfos(
                            QObject::tr("No equivalent value")
                            , QObject::tr("For field id %1 with value %2, we don't have an equivalent value to %3 / %4 / %5 in following possible values: %6")
                            .arg(fieldIdFrom, fromValue, fieldIdTo, countryCodeTo, langCodeTo, possibleValuesList.join(", ")));
                exception.raise();
            }
        }
    }
    co_return;
}
