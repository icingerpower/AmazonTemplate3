
#include <algorithm>
#include <QMap>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../common/openai/OpenAi2.h"
#include "../../common/openai/ExceptionOpenAiError.h"

#include "Attribute.h"
#include "TemplateFiller.h"
#include "AttributeEquivalentTable.h"
#include "AttributeFlagsTable.h"
#include "ExceptionTemplate.h"

#include "FillerPrice.h"
#include "FillerSize.h"
#include "FillerSelectable.h"
#include "AiFailureTable.h"



FillerSelectable::EditCallback FillerSelectable::EDIT_MISSING_CALLBACK = nullptr;
FillerSelectable::SelectValueCallback FillerSelectable::SELECT_VALUE_CALLBACK = nullptr;

void FillerSelectable::recordEditCallback(EditCallback callback)
{
    EDIT_MISSING_CALLBACK = callback;
}

void FillerSelectable::recordSelectValueCallback(SelectValueCallback callback)
{
    SELECT_VALUE_CALLBACK = callback;
}

bool FillerSelectable::canFill(
        const TemplateFiller *templateFiller
        , const Attribute *attribute
        , const QString &marketplaceFrom
        , const QString &fieldIdFrom) const
{
    QList<const AbstractFiller *> otherFillers;
    const FillerPrice fillerPrice;
    otherFillers << &fillerPrice;
    const FillerSize fillerSize;
    otherFillers << &fillerSize;
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
    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    qDebug() << "FillerSelectable::fill...BEGIN";
    const auto &possibleValues = attribute->possibleValues(
                marketplaceTo, countryCodeTo, langCodeTo, productTypeTo);
    qDebug() << "FillerSelectable::fill Possible Values Size:" << possibleValues.size();
    if (possibleValues.size() == 1)
    {
        qDebug() << "FillerSelectable::fill Single Value Path. Value:" << *possibleValues.cbegin();
        const auto &uniquePossibleValue = *possibleValues.cbegin();
        for (auto it = sku_fieldId_fromValues.cbegin();
             it != sku_fieldId_fromValues.cend(); ++it)
        {
            const auto &sku = it.key();
            bool isParent = parentSku_variation_skus.contains(sku);
            bool childOnly = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::ChildOnly);
            if (!childOnly || !isParent)
            {
                sku_fieldId_toValues[sku][fieldIdTo] = uniquePossibleValue;
            }
        }
        qDebug() << "FillerSelectable::fill Single Value Path DONE.";
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
            // Fallback for SKUs where no from-value existed (field absent from source):
            // _fillSameLangCountry only acts on SKUs still empty after the above.
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
    }
    else
    {
        qDebug() << "FillerSelectable::fill No Possible Values. Raising Exception.";
        ExceptionTemplate exception;
        exception.setInfos(
                    QObject::tr("No possible values")
                    , QObject::tr("No possible value for the field %1 / %2 / %3 / %4").arg(
                        marketplaceTo, countryCodeTo, langCodeTo, fieldIdTo));
        throw exception;
    }
    qDebug() << "FillerSelectable::fill...END CO_RETURN";
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
    Q_ASSERT(valuesForAi.size() > 0);
    QSharedPointer<OpenAi2::StepMultipleAsk> step(new OpenAi2::StepMultipleAsk);
    step->id = id;
    step->name = "Select value for " + fieldId;
    step->cachingKey = step->id;
    step->gptModel = "gpt-5.2";
    step->maxRetries = 8;

    step->getPrompt = [marketplace, id, fieldId, valuesForAi, possibleValues](int nAttempts) -> QString
    {
        Q_UNUSED(nAttempts)
        QString prompt = QString("Marketplace: %1\n").arg(marketplace);
        prompt += QString("Field: %1\n").arg(fieldId);
        if (valuesForAi.size() > 0)
        {
            prompt += "Product Attributes:\n";
            for (auto it = valuesForAi.begin(); it != valuesForAi.end(); ++it)
            {
                if (!it.value().isEmpty())
                {
                    prompt += QString("- %1: %2\n").arg(it.key(), it.value());
                }
            }
        }
        prompt += "\nPossible Values:\n";
        QList<QString> sortedValues = possibleValues.values();
        std::sort(sortedValues.begin(), sortedValues.end());
        //Q_ASSERT(possibleValues.size() < 50); // Shold not happen
        for (const auto &val : sortedValues)
        {
            prompt += QString("- %1\n").arg(val);
        }
        prompt += "\nInstruction: Select the most appropriate value from the 'Possible Values' list that matches the product attributes. Reply ONLY with a valid JSON object with key \"value\" containing the exact selected value. Example: {\"value\": \"Selected Value\"}. If no value matches, suggest the closest one.";
        qDebug() << "\n--\nFillerSelectable getPrompt:" << prompt;
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

QString FillerSelectable::_getValueId(
        const QString &marketplaceTo
        , const QString &countryCodeTo
        , const QString &langCodeTo
        , bool allSameValue
        , bool childSameValue
        , const QString &parentSku
        , const QString &variation
        , const QString &fieldIdTo) const
{
    QString valueId = "all_" + marketplaceTo + "_" + countryCodeTo + "_" + langCodeTo;
    if (!allSameValue && childSameValue)
    {
        valueId += "_" + parentSku;
    }
    if (!allSameValue && !childSameValue)
    {
        valueId += "_" + parentSku + "_" + variation;
    }
    // Add fieldIdTo to unique ID to avoid collisions between fields
    valueId += "_" + fieldIdTo;
    return valueId;
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
    bool childOnly = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::ChildOnly);
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_fromValues = it.value();
        if (fieldId_fromValues.contains(fieldIdFrom) && !fieldId_fromValues[fieldIdFrom].isEmpty()
                && (!sku_fieldId_toValues.contains(sku)
                    || !sku_fieldId_toValues[sku].contains(fieldIdTo)
                    || sku_fieldId_toValues[sku][fieldIdTo].isEmpty()))
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

    QList<QSharedPointer<QCoro::Task<void>>> tasks;
    QSet<QString> scheduledValueIds;
    AttributeEquivalentTable *equivalentTable = templateFiller->attributeEquivalentTable();
    QHash<QString, QSharedPointer<QString>> valueId_lastInvalidValue;
    const bool fillIfPresent = attributeFlagsTable->hasFlag(
                marketplaceFrom, fieldIdFrom, Attribute::FillIfPresent);

    auto parseValue = [](const QString &json) -> QString {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject())
        {
            return doc.object().value("value").toString();
        }
        return QString();
    };

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        bool isParent = parentSku_variation_skus.contains(sku);
        if (!isParent || !childOnly)
        {
            if (fillIfPresent)
            {
                const auto &fromValues = it.value();
                if (!fromValues.contains(fieldIdFrom) || fromValues[fieldIdFrom].isEmpty())
                    continue;
            }
            const auto &fieldId_toValues = sku_fieldId_toValues[sku];
            if (!fieldId_toValues.contains(fieldIdTo) || fieldId_toValues[fieldIdTo].isEmpty())
            {
                const QMap<QString, QString> &valuesForAi = sku_attribute_valuesForAi[sku];
                Q_ASSERT(valuesForAi.size() > 0);
                Q_ASSERT(valuesForAi.contains("0_ai_description"));
                const QString &valueId = _getValueId(
                            marketplaceTo
                            , countryCodeTo
                            , langCodeTo
                            , allSameValue
                            , childSameValue
                            , sku_parentSku[sku]
                            , sku_variation[sku]
                            , fieldIdTo
                            );

                bool cacheValid = false;
                // Check cache
                if (templateFiller->hasAiValue(settingsFileName, valueId))
                {
                    const QString &cachedReply = templateFiller->getAiReply(settingsFileName, valueId);
                    // Validate cached reply using the step creation logic (re-using createSelectStep for validation logic)
                    if (possibleValues.size() > 50)
                    {
                        QList<QString> sortedValues = possibleValues.values();
                        std::sort(sortedValues.begin(), sortedValues.end());
                        ExceptionTemplate exception;
                        exception.setInfos(
                                    QObject::tr("Too much possible values")
                                    , QObject::tr("For field id %1 / %2 has too much possible values (%3 / %4): %5")
                                    .arg(fieldIdFrom, fieldIdTo, valueId, sku, sortedValues.join(", ")));
                        exception.raise();
                    }
                    auto step = ::createSelectStep(valueId, marketplaceTo, fieldIdTo, valuesForAi, possibleValues);
                    if (step->validate(cachedReply, ""))
                    {
                        const QString &val = parseValue(cachedReply);
                        if (possibleValues.contains(val))
                        {
                            cacheValid = true;
                        }
                    }
                }

                if (!cacheValid)
                {
                    Q_ASSERT(valuesForAi.size() > 0);
                    Q_ASSERT(valuesForAi.contains("0_ai_description"));
                    if (scheduledValueIds.contains(valueId))
                    {
                        continue;
                    }
                    scheduledValueIds.insert(valueId);

                    auto lastInvalidValue = QSharedPointer<QString>::create();
                    valueId_lastInvalidValue[valueId] = lastInvalidValue;

                    auto task = [=]() -> QCoro::Task<void> {

                        QString selectedValue;
                        bool found = false;

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
                        step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, lastInvalidValue](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
                        {
                            QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                                    .arg(QString::number(networkError), reply, lastWhy);
                            templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, errorMsg);
                            QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
                            if (doc.isObject()) {
                                const QString v = doc.object().value("value").toString();
                                if (!v.isEmpty()) *lastInvalidValue = v;
                            }
                            return true;
                        };

                        for (const auto &step : steps)
                        {
                        }

                        try {
                            co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
                        } catch (const ExceptionOpenAiError &e) {
                            qWarning() << "_fillSameLangCountry Phase 1 hard-failed for" << fieldIdTo << ":" << e.error();
                            // fall through to Phase 2
                        }

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
                            step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, lastInvalidValue](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
                            {
                                QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                                        .arg(QString::number(networkError), reply, lastWhy);
                                templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, fieldIdTo, errorMsg);
                                QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
                                if (doc.isObject()) {
                                    const QString v = doc.object().value("value").toString();
                                    if (!v.isEmpty()) *lastInvalidValue = v;
                                }
                                return true;
                            };
                            // Lenient validate for Phase 2: accept any well-formed {"value":"<non-empty>"}
                            // so the AI's "closest value" suggestions don't exhaust retries.
                            // Exact-match checking is done after apply().
                            step->validate = [](const QString &gptReply, const QString &) -> bool {
                                QJsonParseError err;
                                QJsonDocument doc = QJsonDocument::fromJson(gptReply.toUtf8(), &err);
                                if (err.error != QJsonParseError::NoError || !doc.isObject())
                                    return false;
                                return !doc.object().value("value").toString().isEmpty();
                            };

                            for (const auto &step : steps)
                            {
                            }
                            try {
                                co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
                            } catch (const ExceptionOpenAiError &e) {
                                qWarning() << "_fillSameLangCountry Phase 2 hard-failed for" << fieldIdTo << ":" << e.error();
                                co_return;
                            }

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
                                else if (!val.isEmpty() && val != "UNKNOWN")
                                {
                                    // AI suggested a value that isn't an exact match — record it
                                    // so the third pass can look it up via equivalences (or prompt
                                    // the user to add one with a useful hint).
                                    *lastInvalidValue = val;
                                }
                            }
                        }
                        co_return;
                    };
                    tasks << QSharedPointer<QCoro::Task<void>>::create(task());

                    // Batching check
                    if (tasks.size() >= 50)
                    {
                        for (auto &task : tasks)
                        {
                            co_await *task;
                        }
                        tasks.clear();
                    }
                }
            }
        }
    }

    // Process remaining tasks
    for (auto &task : tasks)
    {
        co_await *task;
    }

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        bool isParent = parentSku_variation_skus.contains(sku);
        if (!isParent || !childOnly)
        {
            if (fillIfPresent)
            {
                const auto &fromValues = it.value();
                if (!fromValues.contains(fieldIdFrom) || fromValues[fieldIdFrom].isEmpty())
                    continue;
            }
            const auto &fieldId_toValues = sku_fieldId_toValues[sku];
            if (!fieldId_toValues.contains(fieldIdTo) || fieldId_toValues[fieldIdTo].isEmpty())
            {
                const QMap<QString, QString> &valuesForAi = sku_attribute_valuesForAi[sku];
                Q_ASSERT(valuesForAi.size() > 0);
                const QString &valueId = _getValueId(
                            marketplaceTo
                            , countryCodeTo
                            , langCodeTo
                            , allSameValue
                            , childSameValue
                            , sku_parentSku[sku]
                            , sku_variation[sku]
                            , fieldIdTo
                            );

                if (templateFiller->hasAiValue(settingsFileName, valueId))
                {
                    QString cachedReply = templateFiller->getAiReply(settingsFileName, valueId);
                    // Validate cached reply using the step creation logic (re-using createSelectStep for validation logic)
                    auto step = ::createSelectStep(valueId, marketplaceTo, fieldIdTo, valuesForAi, possibleValues);
                    if (step->validate(cachedReply, ""))
                    {
                        QString val = parseValue(cachedReply);
                        if (possibleValues.contains(val))
                        {
                            sku_fieldId_toValues[sku][fieldIdTo] = val;
                        }
                    }
                }
            }
        }
    }

    // Third pass: for SKUs still empty after both AI phases, show dialog until user resolves it.
    // The expected fix is to add an equivalence in the dialog linking the AI's invalid reply
    // (e.g. "Correa triple") to a valid allowed value (e.g. "Correa doble").
    for (auto it = sku_fieldId_fromValues.cbegin(); it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        bool isParent = parentSku_variation_skus.contains(sku);
        if (!isParent || !childOnly)
        {
            if (fillIfPresent)
            {
                const auto &fromValues = it.value();
                if (!fromValues.contains(fieldIdFrom) || fromValues[fieldIdFrom].isEmpty())
                    continue;
            }
            const QString &valueId = _getValueId(
                        marketplaceTo, countryCodeTo, langCodeTo,
                        allSameValue, childSameValue,
                        sku_parentSku[sku], sku_variation[sku], fieldIdTo);

            bool rejectedIfHelpAsked = false;
            while (!rejectedIfHelpAsked)
            {
                if (!sku_fieldId_toValues[sku][fieldIdTo].isEmpty())
                    break;

                const QString lastAiValue = valueId_lastInvalidValue.contains(valueId)
                                             ? *valueId_lastInvalidValue[valueId]
                                             : QString();
                if (!lastAiValue.isEmpty()
                        && equivalentTable->hasEquivalent(fieldIdToV02, lastAiValue, possibleValues))
                {
                    sku_fieldId_toValues[sku][fieldIdTo] = equivalentTable->getEquivalentValue(
                                fieldIdToV02, lastAiValue, possibleValues);
                    break;
                }

                auto possibleValuesList = possibleValues.values();
                possibleValuesList.sort();
                QString title = QObject::tr("No AI value selected");
                QString description = QObject::tr(
                    "For field id %1 (%2), AI could not select a value for %3 / %4 / %5.\n"
                    "Possible values: %6")
                        .arg(fieldIdFrom, sku, fieldIdTo, countryCodeTo, langCodeTo,
                             possibleValuesList.join(", "));
                if (!lastAiValue.isEmpty())
                    description += QObject::tr(
                        "\n\nAI kept returning \"%1\" which is not in the allowed values list.\n"
                        "Tip: add an equivalence linking \"%2\" to the correct allowed value.")
                            .arg(lastAiValue, lastAiValue);

                if (SELECT_VALUE_CALLBACK)
                {
                    // Build the same prompt the AI would receive so the user can copy it
                    // into an external AI with the product image attached.
                    QString aiPrompt;
                    if (sku_attribute_valuesForAi.contains(sku))
                    {
                        auto tempStep = ::createSelectStep(
                            valueId, marketplaceTo, fieldIdTo,
                            sku_attribute_valuesForAi[sku], possibleValues);
                        aiPrompt = tempStep->getPrompt(0);
                    }
                    const QString imagePath =
                        templateFiller->sku_imagePreviewFilePath().value(sku);
                    const QString selected = co_await SELECT_VALUE_CALLBACK(
                        templateFiller, title, description, aiPrompt, imagePath, possibleValues);
                    if (!selected.isEmpty())
                    {
                        // Propagate to sibling SKUs so they don't re-open the dialog.
                        // Propagation scope is determined by the flags:
                        //   allSameValue  → all SKUs in this fill call
                        //   childSameValue → all children that share the same parent SKU
                        //   neither        → only exact valueId match (same variation)
                        const QString currentParentSku = sku_parentSku.value(sku);
                        for (auto it2 = sku_fieldId_fromValues.cbegin();
                             it2 != sku_fieldId_fromValues.cend(); ++it2)
                        {
                            const QString &sku2 = it2.key();
                            bool isParent2 = parentSku_variation_skus.contains(sku2);
                            if (!isParent2 || !childOnly)
                            {
                                if (fillIfPresent)
                                {
                                    const auto &fv = it2.value();
                                    if (!fv.contains(fieldIdFrom) || fv[fieldIdFrom].isEmpty())
                                        continue;
                                }
                                if (sku_fieldId_toValues[sku2][fieldIdTo].isEmpty())
                                {
                                    bool shouldPropagate = false;
                                    if (allSameValue)
                                    {
                                        shouldPropagate = true;
                                    }
                                    else if (childSameValue && !currentParentSku.isEmpty())
                                    {
                                        shouldPropagate =
                                            (sku_parentSku.value(sku2) == currentParentSku);
                                    }
                                    else
                                    {
                                        const QString vid2 = _getValueId(
                                            marketplaceTo, countryCodeTo, langCodeTo,
                                            allSameValue, childSameValue,
                                            sku_parentSku.value(sku2), sku_variation.value(sku2),
                                            fieldIdTo);
                                        shouldPropagate = (vid2 == valueId);
                                    }
                                    if (shouldPropagate)
                                        sku_fieldId_toValues[sku2][fieldIdTo] = selected;
                                }
                            }
                        }
                        // Cache so the AI isn't asked again for this valueId.
                        QJsonObject cacheObj;
                        cacheObj["value"] = selected;
                        templateFiller->saveAiValue(settingsFileName, valueId,
                            QJsonDocument(cacheObj).toJson(QJsonDocument::Compact));
                        break;
                    }
                    else
                    {
                        // User cancelled — stop filling for this field.
                        ExceptionTemplate exception;
                        exception.setInfos(title, description);
                        exception.raise();
                    }
                }
                else if (EDIT_MISSING_CALLBACK)
                {
                    rejectedIfHelpAsked = ! co_await EDIT_MISSING_CALLBACK(
                                templateFiller, title, description);
                    // Safety net: if AI gave no specific suggestion there is nothing
                    // for the equivalence table to resolve on the next iteration.
                    if (lastAiValue.isEmpty())
                        rejectedIfHelpAsked = true;
                }
                else
                {
                    ExceptionTemplate exception;
                    exception.setInfos(title, description);
                    exception.raise();
                }
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
    qDebug() << "FillerSelectable::_fillDifferentLangCountry START. TemplateFiller:" << templateFiller << "Thread:" << QThread::currentThreadId();
    if (!templateFiller)
    {
        qDebug() << "CRITICAL: templateFiller is null!";
        co_return;
    }
    
    const QString &fieldIdToV02 = templateFiller->attributeFlagsTable()->getFieldId(
                marketplaceTo, fieldIdTo, Attribute::AMAZON_V02);
    AttributeEquivalentTable *equivalentTable
            = templateFiller->attributeEquivalentTable();
    const auto &possibleValues = attribute->possibleValues(
                marketplaceTo, countryCodeTo, langCodeTo, productTypeTo);
    QList<QSharedPointer<QCoro::Task<void>>> tasks;
    QSet<QString> processedValues;

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        const auto &fieldId_toValuesFrom = sku_fieldId_toValuesFrom[sku];
        if (fieldId_toValuesFrom.contains(fieldIdFrom) && !fieldId_toValuesFrom[fieldIdFrom].isEmpty())
        {
            const auto &fromValue = fieldId_toValuesFrom[fieldIdFrom];
            if (processedValues.contains(fromValue))
            {
                continue;
            }

            if (!equivalentTable->hasEquivalent(fieldIdToV02, fromValue, possibleValues))
            {
                processedValues.insert(fromValue);
                qDebug() << "FillerSelectable::_fillDifferentLangCountry launching task for" << fieldIdToV02 << fromValue;
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

                // Batching check
                if (tasks.size() >= 50)
                {
                    for (auto &task : tasks)
                    {
                        co_await *task;
                    }
                    tasks.clear();
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
        bool rejectedIfHelpAsked = false; // In case we ask help from the callback that will usually trigger the UI
        while (!rejectedIfHelpAsked)
        {
            if (fieldId_toValuesFrom.contains(fieldIdFrom)
                    && !fieldId_toValuesFrom[fieldIdFrom].isEmpty())
            {
                const auto &fromValue = fieldId_toValuesFrom[fieldIdFrom];
                if (equivalentTable->hasEquivalent(fieldIdToV02, fromValue, possibleValues))
                {
                    const QString &toValue = equivalentTable->getEquivalentValue(fieldIdToV02, fromValue, possibleValues);
                    sku_fieldId_toValues[sku][fieldIdTo] = toValue;
                    break;
                }
                else
                {
                    auto possibleValuesList = possibleValues.values();
                    possibleValuesList.sort();
                    QString title = QObject::tr("No equivalent value");
                    QString description = QObject::tr("For field id %1 with value %2 (%3), we don't have an equivalent value to %4 / %5 / %6 in following possible values: %7"
                                                      ).arg(fieldIdFrom, sku, fromValue, fieldIdTo, countryCodeTo, langCodeTo, possibleValuesList.join(", "));
                    if (!rejectedIfHelpAsked && EDIT_MISSING_CALLBACK)
                    {
                        rejectedIfHelpAsked = ! co_await EDIT_MISSING_CALLBACK(templateFiller, title, description);
                    }
                    else
                    {
                        ExceptionTemplate exception;
                        exception.setInfos(
                                    title
                                    , description);
                        exception.raise();
                    }
                }
            }
            else
            {
                rejectedIfHelpAsked = true;
            }
        }
    }
    co_return;
}
