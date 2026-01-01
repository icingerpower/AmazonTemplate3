#include "../../common/openai/OpenAi2.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "AttributesMandatoryTable.h"
#include "AttributeFlagsTable.h"
#include "TemplateFiller.h"
#include "FillerCopy.h"
#include "FillerPrice.h"
#include "FillerSize.h"
#include "FillerTitle.h"


#include "AbstractFiller.h"

const QList<const AbstractFiller *> AbstractFiller::ALL_FILLERS_SORTED
= []() -> QList<const AbstractFiller *>
{
        QList<const AbstractFiller *> allFillers;
        static const FillerCopy fillerCopy;
        allFillers << &fillerCopy;
        static const FillerPrice fillerPrice;
        allFillers << &fillerPrice;
        static const FillerSize fillerSize;
        allFillers << &fillerSize;
        static const FillerTitle fillerTitle;
        allFillers << &fillerTitle;
        return allFillers;
        }();

QCoro::Task<void> AbstractFiller::fillValuesForAi(
        TemplateFiller *templateFiller
        , const QString &productType
        , const QString &countryCodeFrom
        , const QString &langCodeFrom
        , Gender gender
        , Age age
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
        , QHash<QString, QMap<QString, QString>> sku_attribute_valuesForAi)
{
    const QString settingsFileName{"aiImageDescriptions.ini"};
    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    const auto &fieldIds = templateFiller->mandatoryAttributesTable()->getMandatoryIds();
    const auto &marketplaceFrom = templateFiller->marketplaceFrom();
    for (const auto &fieldId : fieldIds)
    {
        if (attributeFlagsTable->hasFlag(
                    marketplaceFrom, fieldId, Attribute::ForCustomInstructions))
        {
            for (auto itSku = sku_fieldId_fromValues.begin();
                 itSku != sku_fieldId_fromValues.end(); ++itSku)
            {
                const auto &sku = itSku.key();
                if (itSku.value().contains(fieldId))
                {
                    const auto &value = itSku.value()[fieldId];
                    if (!value.isEmpty())
                    {
                        sku_attribute_valuesForAi[sku][fieldId] = value;
                    }
                }
            }
        }
    }
    const auto &sku_imagePreviewFilePath = templateFiller->sku_imagePreviewFilePath();
    QHash<QString, QString> imagePath_attributesForAi;
    QHash<QString, QString> imagePath_aiReply;
    auto validateCallback = [](const QString &gptReply, const QString &lastWhy) -> bool{
        Q_UNUSED(lastWhy);
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(gptReply.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            return false;
        }
        if (!doc.isObject())
        {
            return false;
        }
        if (!doc.object().contains("description"))
        {
            return false;
        }
        return !doc.object()["description"].toString().isEmpty();
    };
    auto makeApplyCallback =
            [&imagePath_aiReply, &settingsFileName, templateFiller](const QString& imagePath, bool save = true)
            -> std::function<void(const QString&)>
    {
        return [&imagePath_aiReply, &settingsFileName, imagePath, save, templateFiller](const QString& gptReply)
        {
            const auto doc = QJsonDocument::fromJson(gptReply.toUtf8());
            imagePath_aiReply[imagePath] = doc.object().value("description").toString();
            if (save)
            {
                QString imageBaseName = QFileInfo{imagePath}.baseName();
                templateFiller->saveAiValue(settingsFileName, imageBaseName, gptReply);
            }
        };
    };


    for (auto it = sku_imagePreviewFilePath.begin();
         it != sku_imagePreviewFilePath.end(); ++it)
    {
        const auto &imagePath = it.value();
        QString imageBaseName = QFileInfo{imagePath}.baseName();
        if (templateFiller->hasAiValue(settingsFileName, imageBaseName))
        {
            const auto &aiReply = templateFiller->getAiReply(settingsFileName, imageBaseName);
            if (validateCallback(aiReply, QString{}))
            {
                auto applyCallback = makeApplyCallback(imagePath, false);
                applyCallback(aiReply);
            }
        }
        const auto &sku = it.key();
        QStringList attributesForAi;
        if (sku_attribute_valuesForAi.contains(sku))
        {
            for (auto itAttr = sku_attribute_valuesForAi[sku].begin();
                 itAttr != sku_attribute_valuesForAi[sku].end(); ++itAttr)
            {
                attributesForAi << itAttr.key() + ": " + itAttr.value();
            }
        }
        imagePath_attributesForAi[imagePath] = "Known attributes";
        if (langCodeFrom.toUpper() != "EN")
        {
            imagePath_attributesForAi[imagePath] += " (";
            imagePath_attributesForAi[imagePath] += langCodeFrom;
            imagePath_attributesForAi[imagePath] += " language)";
        }
        imagePath_attributesForAi[imagePath] += ": ";
        imagePath_attributesForAi[imagePath] += attributesForAi.join(", ");
    }
    QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;
    const auto &imagePaths = imagePath_attributesForAi.keys();
    for (const auto &imagePath : imagePaths)
    {
        if (!imagePath_aiReply.contains(imagePath))
        {
            auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
            step->name = "Ask product description from image";
            step->id = imagePath; // Unique ID for caching/tracking
            step->gptModel = "gpt-5.2";
            step->neededReplies = 1;
            step->imagePaths = QStringList{imagePath};
            step->maxRetries = 5;

            step->getPrompt = [imagePath, imagePath_attributesForAi](int nAttempts) -> QString{
                Q_UNUSED(nAttempts);
                QString prompt =
                    "Describe ONLY what is clearly visible in the product image. Be extremely detailed and precise. Reply in ENGLISH.\n"
                    "Goal: later we will fill Amazon attributes from this single description, so include every product details, including, but not limited too:\n"
                    "- materials/finish/texture (use the KNOWN ATTRIBUTES below as facts)\n"
                    "- colors, pattern, shine/matte, transparency\n"
                    "- cut/fit, coverage, length, neckline/collar, sleeves\n"
                    "- closures (zip/buttons/hooks), placement (front/back), seams\n"
                    "- decorations/hardware (rings/studs/straps), adjustability\n"
                    "- intended use/occasion/style keywords (only if strongly implied by design)\n"
                    "Do NOT guess hidden features (e.g., padding, lining, fabric weight). If something is not visible, explicitly say: 'not visible'.\n\n"
                    "KNOWN ATTRIBUTES (trusted facts): " + imagePath_attributesForAi[imagePath] + "\n\n"
                    "Reply in STRICT valid JSON with a single key 'description' and nothing else. "
                    "Example: {\"description\":\"A red cotton t-shirt...\"}";
                return prompt;
            };

            step->validate = validateCallback;
            step->apply = makeApplyCallback(imagePath);
            step->chooseBest = OpenAi2::CHOOSE_ALL_SAME_OR_EMPTY; // Only 1 reply needed effectively

            steps << step;
        }
    }

    if (!steps.isEmpty())
    {
        co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
    }

    for (auto it = sku_imagePreviewFilePath.begin();
         it != sku_imagePreviewFilePath.end(); ++it)
    {
        const auto &sku = it.key();
        const auto &imagePath = it.value();
        if (imagePath_aiReply.contains(imagePath) && !imagePath_aiReply[imagePath].isEmpty())
        {
            sku_attribute_valuesForAi[sku]["0_ai_description"] = imagePath_aiReply[imagePath];
        }
    }
    co_return;
}
