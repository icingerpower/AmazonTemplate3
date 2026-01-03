#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "../../common/openai/OpenAi2.h"

#include "TemplateFiller.h"
#include "FillerBulletPoints.h"
#include "AiFailureTable.h"
#include "AttributeFlagsTable.h"
#include "FillerSelectable.h"

const QStringList FillerBulletPoints::BULLET_POINT_PATTERNS{
    "bullet_point%1", "bullet_point#%1.value"};

const QString FillerBulletPoints::BULLET_POINT_PATTERN_MAIN
= FillerBulletPoints::BULLET_POINT_PATTERNS[1];
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
    step->maxRetries = 5;
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

        prompt += "\nWrite bullet points that increase perceived value while respecting Amazon policies. Always add one and only one emoji in the begin of each bullet point. Do not invent information; only state verifiable facts that highlight the product. First, make sure you connect to the reader and remind for which occasions the product is. Then, ensure the bullets answer, when possible, the most important buyer questions that could block a purchase (e.g., compatibility/fit, dimensions, materials, what’s included, use cases, care instructions, limitations).";
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

    // 1. Prepare data and identify missing bullets
    QList<QSharedPointer<QCoro::Task<void>>> tasks;
    QHash<QString, QStringList> valueId_bulletPoints; // Stores valid bullet points if found
    QSet<QString> launchedValueIds;

    auto attributeFlagsTable = templateFiller->attributeFlagsTable();
    bool childSameValue = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::ChildSameValue);
    bool allSameValue = attributeFlagsTable->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::SameValue);

    QHash<QString, QString> sku_parentSku;
    QHash<QString, QString> sku_variation;
    FillerSelectable::fillVariationsParents(parentSku_variation_skus, sku_parentSku, sku_variation);

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

    QHash<QString, QString> valueId_gptReply;

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        bool isParent = parentSku_variation_skus.contains(sku);
        QString variationForValueId = getVariationForValueId(sku, isParent);
        const QString &valueId = getValueId(
                    marketplaceTo
                    , countryCodeTo
                    , langCodeTo
                    , allSameValue
                    , childSameValue
                    , isParent ? sku : sku_parentSku[sku]
                    , variationForValueId
                    , BULLET_POINT_PATTERN_MAIN
                    );

        bool hasAllBullets = true;
        QStringList bulletPoints{5};
        
        // Check existing values in sku_fieldId_toValueslangCommon (e.g. from previous runs or other sources)
        for (int i=1; i<=5; ++i)
        {
            bool found = false;
            QString curFieldId;
            for (const auto &pattern : BULLET_POINT_PATTERNS)
            {
                curFieldId = pattern.arg(i);
                
                // If we have a value from "from" values, propagate it (legacy logic preserved essentially)
                if (sku_fieldId_fromValues[sku].contains(curFieldId)
                        && !sku_fieldId_fromValues[sku][curFieldId].isEmpty())
                {
                    recordAllMarketplace(
                                templateFiller
                                , marketplaceTo
                                , curFieldId
                                , sku_fieldId_toValueslangCommon[sku]
                                , sku_fieldId_fromValues[sku][curFieldId]);
                    found = true;
                }
                
                if (sku_fieldId_toValueslangCommon[sku].contains(curFieldId)
                        && !sku_fieldId_toValueslangCommon[sku][curFieldId].isEmpty())
                {
                     // If we already have it in common, use it
                    found = true;
                }
            }
            if (!found)
            {
                hasAllBullets = false;
            }
            else
            {
                // Grab the value we have for this bullet index (just take one pattern that exists)
                for (const auto &pattern : BULLET_POINT_PATTERNS)
                {
                    curFieldId = pattern.arg(i);
                    if (sku_fieldId_toValueslangCommon[sku].contains(curFieldId)) {
                        bulletPoints[i-1] = sku_fieldId_toValueslangCommon[sku][curFieldId];
                        break;
                    }
                }
            }
        }

        if (!hasAllBullets)
        {
            if (launchedValueIds.contains(valueId))
            {
                continue;
            }

            auto parseAndSet = [&](const QString &jsonReply, QStringList &outBullets) -> bool {
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(jsonReply.toUtf8(), &error);
                if (error.error == QJsonParseError::NoError && doc.isObject())
                {
                    QJsonArray arr = doc.object().value("bullet_points").toArray();
                    if (arr.size() == 5)
                    {
                        for (int k=0; k<5; ++k)
                        {
                            outBullets[k] = arr.at(k).toString();
                            if (outBullets[k].isEmpty())
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
            if (templateFiller->hasAiValue(settingsFileName, valueId))
            {
                QString cached = templateFiller->getAiReply(settingsFileName, valueId);
                // Validate cached reply
                // We create a dummy step just to access validation logic or replicate it? 
                // Logic is simple enough: valid JSON with 5 strings
                QStringList cachedBullets{5};
                if (parseAndSet(cached, cachedBullets))
                {
                    found = true;
                    // Store for later application
                    // We can't apply immediately because we want to batch everything?
                    // But we can apply immediately for cached values actually, OR we treat them same as new values
                    // Let's store in valueId_bulletPoints map
                    valueId_bulletPoints[valueId] = cachedBullets;
                }
            }

            if (!found)
            {
                launchedValueIds.insert(valueId);
                const auto &valuesForAi = sku_attribute_valuesForAi[sku];

                auto task = [=, &valueId_gptReply]() -> QCoro::Task<void> {

                    // Create step
                    // Note: we pass bulletPoints which contains partial existing bullets if any
                    auto step = createBulletPointsStep(valueId, langCodeTo, productTypeFrom, valuesForAi, bulletPoints);
                     
                    step->apply = [valueId, &valueId_gptReply](const QString &reply) {
                        valueId_gptReply[valueId] = reply;
                    };

                    step->onLastError = [valueId, templateFiller, marketplaceTo, countryCodeTo, countryCodeFrom](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
                    {
                        QString errorMsg = QString("NetworkError: %1 | valueId:%2 | Reply: %3 | Error: %4")
                                .arg(QString::number(networkError), valueId, reply, lastWhy);
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
                    qDebug() << "--\nFillerBulletPoints::fill new task:" << step->getPrompt(0);
                    
                    co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
                };
                tasks << QSharedPointer<QCoro::Task<void>>::create(task());
            }
        }
    }

    // 2. Execute AI tasks
    for (const auto &task : tasks)
    {
        co_await *task;
    }

    // 3. Save new values
    if (!valueId_gptReply.isEmpty())
    {
        templateFiller->saveAiValue(settingsFileName, valueId_gptReply);
    }
    
    // 4. Apply values (cached and new) to all relevant SKUs
    // We re-iterate skus, calculate valueId, check if we have results in valueId_gptReply OR valueId_bulletPoints (from cache)
    // Note: valueId_gptReply contains raw JSON. We need to parse it to bullets.
    
    // Helper to parse raw JSON into list
    auto parseJsonToBullets = [](const QString &json) -> QStringList {
        QStringList bullets{5};
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QJsonArray arr = doc.object().value("bullet_points").toArray();
        for(int k=0; k<5; ++k) bullets[k] = arr.at(k).toString();
        return bullets;
    };

    for (auto it = valueId_gptReply.begin(); it != valueId_gptReply.end(); ++it)
    {
        // Populate valueId_bulletPoints with new results
        valueId_bulletPoints[it.key()] = parseJsonToBullets(it.value());
    }

    for (auto it = sku_fieldId_fromValues.cbegin();
         it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        
        bool isParent = parentSku_variation_skus.contains(sku);
        QString variationForValueId = getVariationForValueId(sku, isParent);
        const QString &valueId = getValueId(
                    marketplaceTo
                    , countryCodeTo
                    , langCodeTo
                    , allSameValue
                    , childSameValue
                    , isParent ? sku : sku_parentSku[sku]
                    , variationForValueId
                    , BULLET_POINT_PATTERN_MAIN
                    );
                    
        if (valueId_bulletPoints.contains(valueId))
        {
            const QStringList &pts = valueId_bulletPoints[valueId];
            if (pts.size() == 5)
            {
                for (int i=1; i<=5; ++i)
                {
                     for (const auto &pattern : BULLET_POINT_PATTERNS)
                     {
                         const QString &bulletFieldid = pattern.arg(i);
                         recordAllMarketplace(
                                     templateFiller, marketplaceTo, bulletFieldid, sku_fieldId_toValueslangCommon[sku], pts[i-1]);
                     }
                }
            }
        }
    
        // Final copy to output
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
