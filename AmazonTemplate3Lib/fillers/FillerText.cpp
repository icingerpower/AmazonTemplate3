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

std::function<bool (const QString &, QString &)> FillerText::_makeParseAndValidate(const QString &fieldIdTo) const
{
    auto parseAndValidate = [fieldIdTo](const QString &reply, QString &valFormatted) -> bool
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
        valFormatted = obj["value"].toString().trimmed();
        if (FIELD_ID_MAX_CHAR.contains(fieldIdTo)
                && valFormatted.size() > FIELD_ID_MAX_CHAR[fieldIdTo])
        {
            return false;
        }
        if (valFormatted.isEmpty())
        {
            return false;
        }
        valFormatted[0] = valFormatted[0].toUpper();
        if (fieldIdTo.contains("description", Qt::CaseInsensitive))
        {
            static const QSet<QString> allowedTags = {
                "p", "/p",
                "br", "br/",
                "b", "/b",
                "ul", "/ul",
                "li", "/li"
            };

            // Find all tags like <...>
            static const QRegularExpression tagRe(R"(<\s*([^>]+)\s*>)");
            auto it = tagRe.globalMatch(valFormatted);
            while (it.hasNext())
            {
                const auto m = it.next();
                QString inside = m.captured(1).trimmed();

                if (inside.isEmpty())
                {
                    return false;
                }

                // Reject comments/doctype/etc.
                if (inside.startsWith("!--") || inside.startsWith("!") || inside.startsWith("?"))
                {
                    return false;
                }

                // Normalize: lowercase, collapse whitespace
                inside = inside.toLower();
                static const QRegularExpression reg(R"(\s+)");
                inside.replace(reg, " ");

                // Reject attributes: anything beyond the bare tag token (except self-closing br/)
                // Example: "p class=x" -> reject, "br /" -> normalize to "br/"
                const QString firstToken = inside.section(' ', 0, 0);
                QString tagToken = firstToken;

                // Handle "<br />" variants:
                // inside could be "br /" (after whitespace collapse), or "br/" already
                if (firstToken == "br" && inside.contains(" /"))
                {
                    // only allow exactly "br /" (no other tokens)
                    if (inside != "br /")
                    {
                        return false;
                    }
                    tagToken = "br/";
                }
                else if (firstToken == "br/" )
                {
                    // ok
                }
                else
                {
                    // If there is any space, it indicates attributes or extra tokens -> reject
                    if (inside.contains(' '))
                    {
                        return false;
                    }
                }

                if (!allowedTags.contains(tagToken))
                {
                    return false;
                }
            }
        }
        else if (fieldIdTo.contains("color", Qt::CaseInsensitive))
        {
            if (valFormatted.contains("("))
            {
                return false;
            }
            else if (valFormatted.contains(")"))
            {
                return false;
            }
        }
        else
        {
            if (valFormatted.contains("<"))
            {
                return false;
            }
            if (valFormatted.contains(">"))
            {
                return false;
            }
        }
        return true;
    };
    return parseAndValidate;
}

void FillerText::_saveGptReplies(
        TemplateFiller *templateFiller, const QString &settingsFileName, const QHash<QString, QString> &fieldId_gptReplies) const
{
    templateFiller->saveAiValue(settingsFileName, fieldId_gptReplies);
}

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
    bool childOnly = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::ChildOnly);
    QHash<QString, QString> sku_parentSku;
    QHash<QString, QString> sku_variation;
    FillerSelectable::fillVariationsParents(parentSku_variation_skus, sku_parentSku, sku_variation);

    QList<QSharedPointer<QCoro::Task<void>>> tasks;
    
    // Group SKUs by valueId
    QHash<QString, QList<QString>> valueId_skus;
    QHash<QString, QMap<QString, QString>> valueId_valuesForAi;
    QHash<QString, QString> valueId_valueFrom;
    
    auto parseAndValidate = _makeParseAndValidate(fieldIdTo);
    QHash<QString, QString> fieldId_gptReplies;

    //QHash<QString, QString> loaded_valid_values;
    //static QSet<QString> allValueIds;
    QSet<QString> launchedValueIds;

    auto getVariationForValueId = [&](const QString &sku, bool isParent) -> QString
    {
        if (isParent)
        {
            if (allSameValue)
            {
                return *parentSku_variation_skus[sku].begin().value().begin(); // We take the first child sku
            }
        }
        else
        {
            return sku_variation[sku];
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
            if (sku_fieldId_toValueslangCommon.contains(sku)
                    && sku_fieldId_toValueslangCommon[sku].contains(fieldIdTo)
                    && !sku_fieldId_toValueslangCommon[sku].isEmpty())
            {
                sku_fieldId_toValues[sku][fieldIdTo] = sku_fieldId_toValueslangCommon[sku][fieldIdTo];
            }
            else
            {

                QString variationForValueId = getVariationForValueId(sku, isParent);

                const QString &valueId = getValueId(
                            marketplaceTo
                            , countryCodeTo
                            , langCodeTo
                            , allSameValue
                            , childSameValue
                            , isParent ? sku : sku_parentSku[sku]
                                         , variationForValueId
                            , fieldIdTo
                            );
                if (launchedValueIds.contains(valueId))
                {
                    continue;
                }

                bool hasAiValue = templateFiller->hasAiValue(settingsFileName, valueId);
                bool validAiValue = false;
                if (hasAiValue)
                {
                    QString val;
                    validAiValue = parseAndValidate(templateFiller->getAiReply(settingsFileName, valueId), val);
                }

                if (!validAiValue)
                {
                    launchedValueIds.insert(valueId);
                    QString valueFrom;
                    if (sku_fieldId_fromValues.contains(sku)
                            && sku_fieldId_fromValues[sku].contains(fieldIdFrom)
                            && !sku_fieldId_fromValues[sku][fieldIdFrom].isEmpty())
                    {
                        valueFrom = sku_fieldId_fromValues[sku][fieldIdFrom];
                    }

                    QMap<QString, QString> valuesForAi = sku_attribute_valuesForAi[sku];

                    auto task = [=, &fieldId_gptReplies]() -> QCoro::Task<void>
                    {
                        const auto &apply = [valueId, &fieldId_gptReplies](const QString &reply, const QString &valFormatted)
                        {
                            fieldId_gptReplies[valueId] = reply;
                        };

                        QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;

                        if (!valueFrom.isEmpty())
                        {
                            QString prompt = "Translate the following text from " + langCodeFrom + " to "
                                    + langCodeTo + ".\n"
                                    "Product Type: "
                                    + productTypeTo + "\n"
                                    "Field ID: "
                                    + fieldIdTo + "\n"
                                    "Text to translate: \""
                                    + valueFrom + "\"\n"
                                    "Output a JSON object with the key \"value\" containing the translated text.";
                            auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
                            step->getPrompt = [prompt](int){ return prompt; };
                            step->maxRetries = 5;
                            step->neededReplies = 1;
                            step->validate = [parseAndValidate](const QString &reply, const QString &)
                            {
                                QString val;
                                return parseAndValidate(reply, val);
                            };
                            step->chooseBest = [](const QList<QString> &replies) -> QString
                            {
                                return replies.isEmpty() ? QString() : replies.first();
                            };
                            step->apply = [parseAndValidate, apply](const QString &reply)
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
                            steps << step;
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
                                prompt += "Write a description that increases perceived value while respecting Amazon policies, strictly under 1800 characters. "
                                        "Do not invent information; only state verifiable facts. Simple HTML allowed ONLY with these tags: "
                                        "<p>, <br>, <b>, <ul>, <li>. "
                                        "Do NOT use any other tags or any HTML attributes (no class/style/id, no inline CSS). "
                                        "Answer important buyer questions (compatibility, dimensions, materials, what’s included, usage/care, warranty if provided).";
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
                                prompt += "No parenthesis.";
                            }
                            prompt += "\nOutput a JSON object with the key \"value\" containing the generated text.";

                            step->getPrompt = [prompt](int){ return prompt; };
                            step->maxRetries = isDescription ? 8 : 5;
                            step->neededReplies = 2; // Ask for 2 replies
                            step->validate = [parseAndValidate](const QString &reply, const QString &)
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
                            step->apply = [parseAndValidate, apply](const QString &reply) {
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
                            steps << step;
                        }
                        co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2"); // std::move(steps) not needed as it is passed by const reference
                        // co_return is not needed for void coroutine that falls off end
                    };
                    tasks << QSharedPointer<QCoro::Task<void>>::create(task());
                }

            }

        }
    }
    for (const auto &task : tasks)
    {
        co_await *task;
    }
    _saveGptReplies(templateFiller, settingsFileName, fieldId_gptReplies);
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();

        bool isParent = parentSku_variation_skus.contains(sku);
        if (!isParent || !childOnly)
        {
            if (!sku_fieldId_toValues.contains(sku)
                    || !sku_fieldId_toValues[sku].contains(fieldIdTo)
                    || sku_fieldId_toValues[sku][fieldIdTo].isEmpty())
            {
                QString variationForValueId = getVariationForValueId(sku, isParent);
                const QString &valueId = getValueId(
                            marketplaceTo
                            , countryCodeTo
                            , langCodeTo
                            , allSameValue
                            , childSameValue
                            , isParent ? sku : sku_parentSku[sku]
                                         , variationForValueId
                            , fieldIdTo
                            );
                if (templateFiller->hasAiValue(settingsFileName, valueId))
                {
                    QString valFormatted;
                    QString reply = templateFiller->getAiReply(settingsFileName, valueId);
                    if (parseAndValidate(reply, valFormatted))
                    {
                        sku_fieldId_toValues[sku][fieldIdTo] = valFormatted;
                        recordAllMarketplace(
                                    templateFiller, marketplaceTo, fieldIdTo, sku_fieldId_toValueslangCommon[sku], valFormatted);
                        sku_fieldId_toValues[sku][fieldIdTo] = valFormatted;
                    }
                }
                else
                {
                    qDebug() << "--\nFillerText::fill FAILURE - no templateFiller->hasAiValue :" << sku << valueId;
                }
            }
        }
    }



    co_return;
}
