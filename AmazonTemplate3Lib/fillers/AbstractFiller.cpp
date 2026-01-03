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
#include "FillerSelectable.h"
#include "FillerBulletPoints.h"
#include "FillerKeywords.h"
#include "FillerText.h"
#include "FillerTitle.h"
#include "ExceptionTemplate.h"


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
        static const FillerBulletPoints fillerBulletPoints;
        allFillers << &fillerBulletPoints;
        static const FillerSelectable fillerSelectable;
        allFillers << &fillerSelectable;
        static const FillerKeywords fillerKeywords;
        allFillers << &fillerKeywords;
        static const FillerText fillerText;
        allFillers << &fillerText;
        static const FillerTitle fillerTitle;
        allFillers << &fillerTitle;
        return allFillers;
        }();

QCoro::Task<void> AbstractFiller::fillValuesForAi(
        TemplateFiller *templateFiller
        , const QHash<QString, QHash<QString, QSet<QString>>> &parentSku_variation_skus
        , const QString &productType
        , const QString &countryCodeFrom
        , const QString &langCodeFrom
        , Gender gender
        , Age age
        , const QMap<QString, QString> &skuPattern_customInstructions
        , const QHash<QString, QHash<QString, QString>> &sku_fieldId_fromValues
        , QHash<QString, QMap<QString, QString>> &sku_attribute_valuesForAi)
{
    const QString settingsFileName{"aiImageDescriptions.ini"};
    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    const auto &fieldIds = templateFiller->mandatoryAttributesTable()->getMandatoryIds();
    for (const auto &fieldId : fieldIds)
    {
        if (attributeFlagsTable->hasFlag(
                    Attribute::AMAZON_V02, fieldId, Attribute::ForCustomInstructions))
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
                for (auto itPattern = skuPattern_customInstructions.begin();
                     itPattern != skuPattern_customInstructions.end(); ++itPattern)
                {
                    const auto &pattern = itPattern.key();
                    if (sku.contains(pattern))
                    {
                        sku_attribute_valuesForAi[sku]["Note"] += itPattern.value();
                    }
                }
            }
        }
    }
    auto sku_imagePreviewFilePath = templateFiller->sku_imagePreviewFilePath();
    for (auto itParent = parentSku_variation_skus.begin();
         itParent != parentSku_variation_skus.end(); ++itParent)
    {
        QSet<QString> skusToUpdate;
        QString imagePreviewFilePath;
        for (auto itVar = itParent.value().begin();
             itVar != itParent.value().end(); ++itVar)
        {
            for (const auto &sku : itVar.value())
            {
                if (sku_imagePreviewFilePath.contains(sku))
                {
                    imagePreviewFilePath = sku_imagePreviewFilePath[sku];
                }
                else
                {
                    skusToUpdate.insert(sku);
                }
            }
        }
        Q_ASSERT(!imagePreviewFilePath.isEmpty());
        for (const auto &skuToUpdate : skusToUpdate)
        {
            sku_imagePreviewFilePath[skuToUpdate] = imagePreviewFilePath;
        }
    }
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
        imagePath_attributesForAi[imagePath] += attributesForAi.join(", "); // TODO cedric, not well retrieved / filled
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
            step->onLastError = [imagePath](const QString &, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool {
                ExceptionTemplate exception;
                exception.setInfos(QObject::tr("AI Error"),
                                   QObject::tr("The AI failed to describe the image: ") + imagePath + "\n" +
                                   QObject::tr("Network Error: %1").arg(networkError) + "\n" +
                                   QObject::tr("Last Reason: ") + lastWhy + "\n" +
                                   QObject::tr("If this error persists, it might mean we are blocked by the AI provider (too many queries). Please wait a few hours."));
                exception.raise();
                return true;
            };

            steps << step;
        }
    }

    if (!steps.isEmpty())
    {
        for (const auto &step : steps)
        {
            qDebug() << "--\nAbstractFiller::fillValuesForAi:" << step->getPrompt(0);
        }
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
        else
        {
            Q_ASSERT(false);
        }
    }
    for (auto itParent = parentSku_variation_skus.cbegin();
         itParent != parentSku_variation_skus.cend(); ++itParent)
    {
        const auto &skuParent = itParent.key();
        for (auto itVar = itParent.value().cbegin();
             itVar != itParent.value().cend(); ++itVar)
        {
            for (const auto &sku : itVar.value())
            {
                const auto &imagePath = sku_imagePreviewFilePath[sku];
                sku_attribute_valuesForAi[skuParent]["0_ai_description"]
                        = imagePath_aiReply[imagePath];
                break;
            }
            break;
        }
    }
    co_return;
}

void AbstractFiller::recordAllMarketplace(
        const TemplateFiller *templateFiller
        , const QString &marketplace
        , const QString &fieldId
        , QHash<QString, QString> &fieldId_values
        , const QString &value)
{
    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    const auto &marketplace_ids = attributeFlagsTable->get_marketplace_id(marketplace, fieldId);
    for (const auto &curFieldId : marketplace_ids)
    {
        fieldId_values[curFieldId] = value;
    }
}

QString AbstractFiller::getValueId(
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
