#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../../common/openai/OpenAi2.h"
#include "TemplateFiller.h"
#include "FillerBulletPoints.h"
#include "AiFailureTable.h"

static QSharedPointer<OpenAi2::StepMultipleAsk> createBulletPointsStep(
        const QString &id
        , const QString &langCodeTo
        , const QString &productType
        , const QMap<QString, QString> &attributes
        , const QStringList &existingBullets)
{
    QSharedPointer<OpenAi2::StepMultipleAsk> step(new OpenAi2::StepMultipleAsk);
    step->id = id;
    step->name = "Generate bullet points for " + productType;
    step->cachingKey = step->id;
    step->gptModel = "gpt-5.2";
    step->maxRetries = 3;
    step->neededReplies = 1;

    step->getPrompt = [langCodeTo, productType, attributes, existingBullets](int nAttempts) -> QString
    {
        Q_UNUSED(nAttempts)
        QString prompt = QString("You are an expert in writing amazon product pages. Generate 5 compelling bullet points for a product description in language '%1'.\n").arg(langCodeTo);
        prompt += QString("Product Type: %1\n").arg(productType);
        prompt += "Attributes:\n";
        for (auto it = attributes.begin(); it != attributes.end(); ++it)
        {
             if (!it.value().isEmpty())
             {
                 prompt += QString("- %1: %2\n").arg(it.key(), it.value());
             }
        }
        
        bool hasExisting = false;
        QString existingStr;
        for (int i=0; i<existingBullets.size(); ++i)
        {
            if (!existingBullets[i].isEmpty())
            {
                existingStr += QString("- %1\n").arg(existingBullets[i]);
                hasExisting = true;
            }
        }
        
        if (hasExisting)
        {
            if (langCodeTo == "EN")
            {
                prompt += "\nExisting Bullet Points (preserve meaning/translate if needed, complete the list to reach 5). If any measurement is in inches (in, inch, \", ″), keep the inches value and add the centimeters conversion right after in parentheses, e.g., 10 in (25.4 cm).\n";
            }
            else
            {
                prompt += "\nExisting Bullet Points (preserve meaning/translate if needed, complete the list to reach 5). If any measurement is in inches (in, inch, \", ″), convert it to centimeters and output only the cm value.\n";
            }

            //prompt += "\nExisting Bullet Points (preserve meaning/translate if needed, and complete the list to reach 5):\n";
            prompt += existingStr;
        }

        prompt += "\nWrite bullet points that increase perceived value while respecting Amazon policies. Do not invent information; only state verifiable facts that highlight the product. Also, ensure the bullets answer, when possible, the most important buyer questions that could block a purchase (e.g., compatibility/fit, dimensions, materials, what’s included, use cases, care instructions, limitations).";
        prompt += "\nInstruction: Output a valid JSON object with a single key \"bullet_points\" containing an array of exactly 5 strings. Example: {\"bullet_points\": [\"Point 1\", \"Point 2\", \"Point 3\", \"Point 4\", \"Point 5\"]}.";
        return prompt;
    };
    
    step->validate = [](const QString &gptReply, const QString &lastWhy) -> bool
    {
        Q_UNUSED(lastWhy)
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(gptReply.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }
        QJsonObject obj = doc.object();
        if (!obj.contains("bullet_points"))
        {
            return false;
        }
        QJsonArray arr = obj.value("bullet_points").toArray();
        if (arr.size() != 5)
        {
            return false;
        }
        for (const auto &val : arr)
        {
            if (!val.isString() || val.toString().trimmed().isEmpty())
            {
                return false;
            }
        }
        return true;
    };
    
    // Simple chooser for 1 reply
    step->chooseBest = [](const QList<QString> &replies) -> QString {
        if (replies.isEmpty()) return QString();
        return replies.first();
    };
    
    // apply managed in caller
    step->apply = [](const QString &) {};
    
    return step;
}

const QStringList FillerBulletPoints::BULLET_POINT_PATTERNS{
    "bullet_point%1", "bullet_point#%1.value"};
const QSet<QString> FillerBulletPoints::BULLET_POINT_IDS
= []() -> QSet<QString> {
        static QSet<QString> bulletPointIds;
        for (int i=1; i<=5; ++i)
        {
            for (const auto &pattern : BULLET_POINT_PATTERNS)
            {
                bulletPointIds.insert(pattern.arg(i));
            }
        }
        return bulletPointIds;
}();

bool FillerBulletPoints::canFill(
        const TemplateFiller *templateFiller
        , const Attribute *attribute
        , const QString &marketplaceFrom
        , const QString &fieldIdFrom) const
{
    return fieldIdFrom.contains("bullet_point");
}

QCoro::Task<void> FillerBulletPoints::fill(
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
    QStringList bulletPointsFrom{5};
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        for (int i=1; i<=5; ++i)
        {
            QString curFieldId;
            for (const auto &pattern : BULLET_POINT_PATTERNS)
            {
                curFieldId = pattern.arg(i);
                if (sku_fieldId_fromValues[sku].contains(curFieldId)
                        && !sku_fieldId_fromValues[sku][curFieldId].isEmpty())
                {
                    bulletPointsFrom[i-1]
                            = sku_fieldId_fromValues[sku][curFieldId];
                }
            }
        }
    }
    const QString &settingsFileName{"bulletPoints.ini"};
    bool hasAllBullets = true;
    QStringList bulletPoints{5};
    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        for (int i=1; i<=5; ++i)
        {
            QString curFieldId;
            for (const auto &pattern : BULLET_POINT_PATTERNS)
            {
                curFieldId = pattern.arg(i);
                if (sku_fieldId_fromValues[sku].contains(curFieldId)
                        && !sku_fieldId_fromValues[sku][curFieldId].isEmpty())
                {
                    recordAllMarketplace(
                                templateFiller
                                , marketplaceTo
                                , curFieldId
                                , sku_fieldId_toValueslangCommon[sku]
                                , sku_fieldId_fromValues[sku][curFieldId]);
                }
                if (sku_fieldId_toValueslangCommon[sku].contains(curFieldId)
                        && !sku_fieldId_toValueslangCommon[sku][curFieldId].isEmpty())
                {
                    break;
                }
            }
            if (curFieldId.isEmpty())
            {
                hasAllBullets = false;
            }
            else
            {
                bulletPoints[i-1] = sku_fieldId_toValueslangCommon[sku][curFieldId];
            }
        }
        if (!hasAllBullets)
        {
            const QString &stepId = "bullets_" + langCodeTo + "_" + sku;
            const auto &valuesForAi = sku_attribute_valuesForAi[sku];
            
            auto step = createBulletPointsStep(stepId, langCodeTo, productTypeFrom, valuesForAi, bulletPoints);
            
            auto parseAndSet = [&](const QString &jsonReply) -> bool {
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(jsonReply.toUtf8(), &error);
                if (error.error == QJsonParseError::NoError && doc.isObject())
                {
                    QJsonArray arr = doc.object().value("bullet_points").toArray();
                    if (arr.size() == 5)
                    {
                        for (int k=0; k<5; ++k)
                        {
                            bulletPoints[k] = arr.at(k).toString();
                            if (bulletPoints[k].isEmpty())
                            {
                                return false;
                            }
                        }
                        return true;
                    }
                }
                return false;
            };

            bool found = false;
            if (templateFiller->hasAiValue(settingsFileName, stepId))
            {
                QString cached = templateFiller->getAiReply(settingsFileName, stepId);
                // Validate cached reply
                if (step->validate(cached, ""))
                {
                    if (parseAndSet(cached))
                    {
                        found = true;
                    }
                }
            }
            
            if (!found)
            {
                QString finalReply;
                step->apply = [&](const QString &reply) {
                    finalReply = reply;
                };
                step->onLastError = [templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
                {
                    QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                            .arg(QString::number(networkError), reply, lastWhy);
                    for (int i=1; i<=5; ++i)
                    {
                        for (const auto &pattern : FillerBulletPoints::BULLET_POINT_PATTERNS)
                        {
                            templateFiller->aiFailureTable()->recordError(marketplaceTo, countryCodeTo, countryCodeFrom, pattern.arg(i), errorMsg);
                        }
                    }
                    return true;
                };
                
                QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;
                steps.append(step);
                qDebug() << "--\nAbstractFiller::fillValuesForAi:" << step->getPrompt(0);
                co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
                
                if (!finalReply.isEmpty() && step->validate(finalReply, ""))
                {
                    if (parseAndSet(finalReply))
                    {
                         templateFiller->saveAiValue(settingsFileName, stepId, finalReply);
                         found = true;
                    }
                }
            }
            
            if (found)
            {
                // Assign to sku_fieldId_toValueslangCommon
                for (int i=1; i<=5; ++i)
                {
                     // Find appropriate fieldId for this index
                     // The loop structure above iterates 1..5 but doesn't store the resulting fieldId easily for assignment unless we re-derive it or just pick the first pattern that makes sense?
                     // Actually logic above: checked all patterns. If found, assigned to sku_fieldId_toValueslangCommon.
                     // The inner loop iterates i=1..5. Inside it iterates patterns.
                     // We need to assign `bulletPoints[i-1]` to the correct key in `sku_fieldId_toValueslangCommon[sku]`.
                     // But which key? "bullet_point1" or "bullet_point#1.value"?
                     // Usually we should use the one that exists in the template (fieldIdTo side).
                     // But `fill` method here iterates `sku_fieldId_fromValues`.
                     // `sku_fieldId_toValues` is the output. `sku_fieldId_toValueslangCommon` is intermediate?
                     
                     // The requirement says: "Then save the 5 bullet points in sku_fieldId_toValueslangCommon[sku] for all pattern of BULLET_POINT_PATTERNS if the reply is valid"
                     
                     for (const auto &pattern : BULLET_POINT_PATTERNS)
                     {
                         const QString &bulletFieldid = pattern.arg(i);
                         // We assign to all patterns effectively ensuring whichever is used downstream picks it up?
                         // Or we should only assign if we know which one is valid?
                         // The prompt says "for all pattern of BULLET_POINT_PATTERNS".
                         
                         recordAllMarketplace(
                                     templateFiller, marketplaceTo, bulletFieldid, sku_fieldId_toValueslangCommon[sku], bulletPoints[i-1]);
                     }
                }
            }
        }
        for (int i=1; i<=5; ++i)
        {
            QString curFieldId;
            for (const auto &pattern : BULLET_POINT_PATTERNS)
            {
                curFieldId = pattern.arg(i);
                if (sku_fieldId_toValueslangCommon[sku].contains(curFieldId)
                        && !sku_fieldId_toValueslangCommon[sku][curFieldId].isEmpty())
                {
                    sku_fieldId_toValues[sku][curFieldId]
                            = sku_fieldId_toValueslangCommon[sku][curFieldId];
                }
            }
        }
    }
    co_return;
}
