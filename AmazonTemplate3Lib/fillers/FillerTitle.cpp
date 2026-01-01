#include "../../common/openai/OpenAi2.h"

#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"
#include "FillerSize.h"

#include "FillerTitle.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

bool FillerTitle::canFill(
        const TemplateFiller *templateFiller, const QString &marketplace, const QString &fieldId) const
{
    return fieldId.startsWith("item_name");
}

// Static helper to avoid ICE in coroutine
static QSharedPointer<OpenAi2::StepMultipleAskAi> createTranslationStep(
        const QString &title,
        const QString &langCodeTo)
{
    QSharedPointer<OpenAi2::StepMultipleAskAi> stepTranslation(new OpenAi2::StepMultipleAskAi);
    stepTranslation->id = "FillerTitle_translation_" + title + "_" + langCodeTo;
    stepTranslation->name = "Translate product title";
    stepTranslation->cachingKey = stepTranslation->id;
    stepTranslation->neededReplies = 2;
    stepTranslation->gptModel = "gpt-5.2";

    // First prompt: Translate
    stepTranslation->getPrompt = [title, langCodeTo](int nAttempts) -> QString
    {
        Q_UNUSED(nAttempts)
        return QString("Translate the following title to language '%1'. "
                       "Each word must start with a capital letter.\n"
                       "Title: '%2'").arg(langCodeTo, title);
    };

    // Validation for individual replies
    stepTranslation->validate = [](
            const QString &gptReply, const QString &lastWhy) -> bool
    {
        Q_UNUSED(lastWhy)
        return !gptReply.trimmed().isEmpty();
    };

    // Second prompt: Choose the best translation
    stepTranslation->getPromptGetBestReply = [](
            int nAttempts, const QList<QString> &gptValidReplies) -> QString
    {
        Q_UNUSED(nAttempts)
        QString prompt = "Here are several translations:\n";
        for (const auto &reply : gptValidReplies)
        {
            prompt += QString("- %1\n").arg(reply);
        }
        prompt += "Please select the best translation and output ONLY a valid JSON object with the key 'translation' containing the selected text.\n"
                  "Example: {\"translation\": \"Selected Text\"}";
        return prompt;
    };

    stepTranslation->validateBestReply = [](
            const QString &gptReply, const QString &lastWhy) -> bool
    {
        Q_UNUSED(lastWhy)
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(gptReply.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            return false;
        }
        if (!doc.isObject())
        {
            return false;
        }
        if (!doc.object().contains("translation"))
        {
             return false;
        }
        return !doc.object().value("translation").toString().trimmed().isEmpty();
    };
    
    return stepTranslation;
}

QCoro::Task<void> FillerTitle::fill(
        TemplateFiller *templateFiller
        , const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
        , const QString &marketplaceFrom
        , const QString &marketplaceTo
        , const QString &fieldIdFrom
        , const QString &fieldIdTo
        , const Attribute *attribute
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
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_toValuesFrom
        , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValueslangCommon
        , QHash<QString, QHash<QString, QString>> &sku_fieldId_toValues) const
{
    const auto &titleFrom_skus = _get_titleFrom_skus(
                sku_fieldId_fromValues, fieldIdFrom);

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        if (it.value().contains(fieldIdFrom))
        {
            if (countryCodeFrom == countryCodeTo && langCodeFrom == langCodeTo)
            {
                sku_fieldId_toValues[sku][fieldIdTo] = sku_fieldId_fromValues[sku][fieldIdFrom];
            }
            QString titleFromFull = it.value()[fieldIdFrom].trimmed();
            _fixTitleFormat(titleFromFull);
            QString titleFrom = titleFromFull.split(" (")[0];
            if (!sku_fieldId_toValueslangCommon[sku].contains(fieldIdTo))
            {
                if (sku_fieldId_toValueslangCommon[sku].contains(fieldIdFrom))
                {
                    sku_fieldId_toValueslangCommon[sku][fieldIdTo]
                            = sku_fieldId_toValueslangCommon[sku][fieldIdFrom];
                }
                else if (langCodeFrom == langCodeTo)
                {
                    sku_fieldId_toValueslangCommon[sku][fieldIdTo] = titleFrom;
                }
            }
            if (!sku_fieldId_toValueslangCommon[sku].contains(fieldIdTo))
            {
                const QString settingsFileName = "aiTitleTranslations.ini";
                auto stepTranslation = createTranslationStep(titleFrom, langCodeTo);
                QString titleTranslated;
                
                auto parseAndSetTitle = [&](const QString &jsonReply) -> bool {
                    if (stepTranslation->validateBestReply(jsonReply, ""))
                    {
                        QJsonDocument doc = QJsonDocument::fromJson(jsonReply.toUtf8());
                        titleTranslated = doc.object().value("translation").toString().trimmed();
                        return true;
                    }
                    return false;
                };

                bool foundInCache = false;
                if (templateFiller->hasAiValue(settingsFileName, stepTranslation->id))
                {
                    QString cachedReply = templateFiller->getAiReply(settingsFileName, stepTranslation->id);
                    if (parseAndSetTitle(cachedReply))
                    {
                        foundInCache = true;
                    }
                }

                if (!foundInCache)
                {
                    stepTranslation->apply = [&](const QString &reply) {
                        if (parseAndSetTitle(reply))
                        {
                            templateFiller->saveAiValue(settingsFileName, stepTranslation->id, reply);
                        }
                    };
                    
                    QList<QSharedPointer<OpenAi2::StepMultipleAskAi>> steps;
                    steps.append(stepTranslation);
                    co_await OpenAi2::instance()->askGptMultipleTimeAiCoro(steps, "gpt-5.2");
                }
                
                // If succeeded (cache or AI), update SKUs
                if (!titleTranslated.isEmpty())
                {
                    const auto &skusSameTitle = titleFrom_skus[titleFrom];
                    for (const auto &curSku : skusSameTitle)
                    {
                        sku_fieldId_toValueslangCommon[curSku][fieldIdTo] = titleTranslated;
                    }
                }
            }
        }
    }
    
    auto attributeFlagTable = templateFiller->attributeFlagsTable();
    const auto &sizeFieldIds = attributeFlagTable->getSizeFieldIds();
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        if (sku_fieldId_toValueslangCommon.contains(sku)
                && sku_fieldId_toValueslangCommon[sku].contains(fieldIdTo))
        {
            QString titleFromFull = it.value()[fieldIdFrom].trimmed();
            _fixTitleFormat(titleFromFull);
            if (titleFromFull.contains(" ("))
            {
                auto titleEnd = titleFromFull.split(" (").last().split(")")[0];
                auto titleSizeParts = titleEnd.split(",");
                for (auto &titlePart : titleSizeParts)
                {
                    titlePart = titlePart.trimmed();
                }
                bool hasColor = false;
                QString newTitle = sku_fieldId_toValueslangCommon[sku][fieldIdTo];
                newTitle += " (";
                if (sku_fieldId_toValueslangCommon[sku].contains("color#1.value"))
                {
                    newTitle += sku_fieldId_toValueslangCommon[sku]["color#1.value"];
                    newTitle += ", ";
                }
                else if (sku_fieldId_toValueslangCommon[sku].contains("color_name"))
                {
                    newTitle += sku_fieldId_toValueslangCommon[sku]["color_name"];
                    newTitle += ", ";
                }
                if (titleSizeParts.size() > 0)
                {
                    const QString &sizePart = titleSizeParts.last();
                    QString labelSize;
                    if (sizePart.contains("="))
                    {
                        labelSize = sizePart.split("=")[0].trimmed();
                    }
                    else
                    {
                        if (sizePart.contains("-"))
                        {
                            labelSize = sizePart.split("-").last().trimmed();
                        }
                    }
                    QString size;
                    for (const auto &sizeFieldId : sizeFieldIds)
                    {
                        if (sku_fieldId_toValues[sku].contains(sizeFieldId)
                                && !sku_fieldId_toValues[sku][sizeFieldId].isEmpty())
                        {
                            size = sku_fieldId_toValues[sku][sizeFieldId];
                        }
                    }
                    if (labelSize != size)
                    {
                        newTitle += labelSize + "=" + _get_sizeCountry(templateFiller, countryCodeTo, productType, gender, age) + "-" + size;
                    }
                    else
                    {
                        newTitle += size;
                    }
                }
                newTitle += ")";
            }
            else
            {
                sku_fieldId_toValues[sku][fieldIdTo] = sku_fieldId_toValueslangCommon[sku][fieldIdTo]; // Parent title without color / size informations
            }
        }
    }
     co_return;
}

void FillerTitle::_fixTitleFormat(QString &titleFull) const
{
    while (titleFull.contains("  "))
    {
        titleFull.replace("  ", " ");
    }
    if (titleFull.contains("(") && !titleFull.contains(" ("))
    {
        titleFull.replace("(", " (");
    }
}

QString FillerTitle::_get_sizeCountry(
        TemplateFiller *templateFiller
        , const QString &countryCodeTo
        , const QString &productType
        , AbstractFiller::Gender gender
        , AbstractFiller::Age age) const
{
    auto settings = templateFiller->settingsCommon();
    bool isShoes = false;
    bool isClothe = false;
    bool isNoSizeConv = false;
    FillerSize::initCatBools(settings.data(), productType, isShoes, isClothe, isNoSizeConv);

    if (isNoSizeConv)
    {
        return QString{};
    }

    // Helper lambda to determine region from a conversion table
    auto getRegion = [&](const auto &table) -> QString {
        if (table.isEmpty())
        {
            return countryCodeTo;
        }
        const auto &baseSizes = table.first();
        if (!baseSizes.contains(countryCodeTo))
        {
            return countryCodeTo;
        }

        if (baseSizes.contains("COM") && baseSizes[countryCodeTo] == baseSizes["COM"])
        {
            return "US";
        }
        if (baseSizes.contains("UK") && baseSizes[countryCodeTo] == baseSizes["UK"])
        {
            return "UK/AU";
        }
        
        // Check FR group
        if (baseSizes.contains("FR") && baseSizes[countryCodeTo] == baseSizes["FR"])
        {
            // Disambiguate if FR and DE are same (e.g. Men Clothes)
            if (baseSizes.contains("DE") && baseSizes["FR"] == baseSizes["DE"])
            {
                 // If specific country is DE-aligned in preference?
                 // Original logic roughly: TR, ES, BE -> FR. Others -> EU (DE).
                 // FR itself -> FR.
                 QSet<QString> frGroup{"FR", "BE", "ES", "TR"};
                 if (frGroup.contains(countryCodeTo))
                 {
                     return "FR";
                 }
                 return "EU";
            }
            return "FR";
        }

        // Check EU (DE) group
        if (baseSizes.contains("DE") && baseSizes[countryCodeTo] == baseSizes["DE"])
        {
            return "EU";
        }

        // Check IT
        if (baseSizes.contains("IT") && baseSizes[countryCodeTo] == baseSizes["IT"])
        {
            return "IT";
        }

        return countryCodeTo;
    };

    if (isShoes)
    {
        if (gender == AbstractFiller::Gender::Female && age == AbstractFiller::Age::Adult)
        {
             return getRegion(FillerSize::SHOE_FEMALE_ADULT_SIZES);
        }
        else if (gender == AbstractFiller::Gender::Male && age == AbstractFiller::Age::Adult)
        {
             return getRegion(FillerSize::SHOE_MALE_ADULT_SIZES);
        }
             
         // Fallback for other shoe types? Or assume EU/US mapping still holds?
         // For now, if undefined gender, defaulting to old behavior logic or trying generic maps?
         // Since we don't have generic maps, we can try Female Adult as a proxy for region mapping?
         // Or just fallback to "US"/"EU" hardcoded checks.
         // Let's fallback to manual checks if table not found.
         if (countryCodeTo == "CA" || countryCodeTo == "COM")
         {
             return "US";
         }
         if (countryCodeTo == "UK" || countryCodeTo == "AU")
         {
             return "UK/AU";
         }
         QSet<QString> euCountries{"FR", "TR", "BE", "IT", "ES", "DE", "PL", "NL", "SE", "IE"};
         if (euCountries.contains(countryCodeTo))
         {
             return "EU";
         }
    }
    else if (isClothe)
    {
         if (gender == AbstractFiller::Gender::Female && age == AbstractFiller::Age::Adult)
         {
             return getRegion(FillerSize::CLOTHE_FEMALE_ADULT_SIZES);
         }
         else if (gender == AbstractFiller::Gender::Male && age == AbstractFiller::Age::Adult)
         {
             return getRegion(FillerSize::CLOTHE_MALE_ADULT_SIZES);
         }
             
         // Fallback
         if (countryCodeTo == "CA" || countryCodeTo == "COM")
         {
             return "US";
         }
         if (countryCodeTo == "UK" || countryCodeTo == "AU")
         {
             return "UK/AU";
         }
         if (countryCodeTo == "TR"  || countryCodeTo == "ES"|| countryCodeTo == "BE")
         {
             return "FR";
         }
         QSet<QString> euCountries{"DE", "PL", "NL", "SE", "IE"};
         if (euCountries.contains(countryCodeTo))
         {
             return "EU";
         }
    }
    
    // Default fallback
    if (countryCodeTo == "CA" || countryCodeTo == "COM")
    {
        return "US";
    }
    if (countryCodeTo == "UK" || countryCodeTo == "AU")
    {
        return "UK/AU";
    }

    return countryCodeTo;
}

QHash<QString, QSet<QString>> FillerTitle::_get_titleFrom_skus(
        const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
        , const QString &fieldIdFrom) const
{
    QHash<QString, QSet<QString>> titleFrom_skus;
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        if (it.value().contains(fieldIdFrom))
        {
            QString titleFromFull = it.value()[fieldIdFrom].trimmed();
            _fixTitleFormat(titleFromFull);
            QString titleFrom = titleFromFull.split(" (")[0];
            titleFrom_skus[titleFrom].insert(sku);
        }
    }
    return titleFrom_skus;
}
