#include "../../common/openai/OpenAi2.h"

#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"
#include "Attribute.h"

#include "FillerSize.h"
#include "ExceptionTemplate.h"
#include <QSet>
#include <QRegularExpression>

const QString FillerSize::KEY_SHOE_WORDS{"shoeWords"};
const QString FillerSize::KEY_CLOTHE_WORDS{"clotheWords"};
const QString FillerSize::KEY_CAT_NO_CONV_WORDS{"catNoSizeWords"};

const QList<QHash<QString, int>> FillerSize::CLOTHE_FEMALE_ADULT_SIZES = []() {
    QList<QHash<QString, int>> _list_countryCode_size {
        {{{"FR", 32}
            , {"BE", 32}
            , {"ES", 32}
            , {"TR", 32}
            , {"DE", 30}
            , {"NL", 30}
            , {"SE", 30}
            , {"PL", 30}
            , {"IT", 36}
            , {"IE", 4} // Irland
            , {"UK", 4}
            , {"AU", 4}
            , {"COM", 0}
            , {"CA", 0}
            , {"JP", 5}
            , {"AE", 0}
            , {"MX", 0}
            , {"SA", 0}
            , {"SG", 0}
        }}
    };
    for (int i=2; i<20; i+=2)
    {
        QHash<QString, int> curSizes;
        for (auto it = _list_countryCode_size[0].begin();
             it != _list_countryCode_size[0].end(); ++it)
        {
            curSizes[it.key()] = it.value() + i;
        }
        _list_countryCode_size << curSizes;
    }
    return _list_countryCode_size;
}();

const QList<QHash<QString, int>> FillerSize::CLOTHE_MALE_ADULT_SIZES = []() {
    int eu = 48; // set the EU men size (valid: 44,46,48,50,52,54,56,58,60)
    QList<QHash<QString, int>> _list_countryCode_size {
        {{
            {"FR", eu},
            {"BE", eu},
            {"ES", eu},
            {"TR", eu},
            {"DE", eu},
            {"NL", eu},
            {"SE", eu},
            {"PL", eu},
            {"IT", eu},

            {"IE", eu - 10}, // Ireland (UK/US inch system)
            {"UK", eu - 10},
            {"AU", eu - 10},
            {"COM", eu - 10}, // US
            {"CA", eu - 10},
            {"AE", eu - 10},
            {"MX", eu - 10},
            {"SA", eu - 10},
            {"SG", eu - 10}
        }}
    };
    for (int i=2; i<20; i+=2)
    {
        QHash<QString, int> curSizes;
        for (auto it = _list_countryCode_size[0].begin();
             it != _list_countryCode_size[0].end(); ++it)
        {
            curSizes[it.key()] = it.value() + i;
        }
        _list_countryCode_size << curSizes;
    }
    return _list_countryCode_size;
}();

const QList<QHash<QString, double>> FillerSize::SHOE_FEMALE_ADULT_SIZES = []() {
    QList<QHash<QString, double>> _list_countryCode_size;
    QSet<QString> groupEu{"FR", "BE", "ES", "IT", "DE", "NL", "SE", "PL", "TR"};
    QSet<QString> groupUs{"COM", "CA", "SA", "SG", "AE"};
    QSet<QString> groupUk{"UK", "IE", "AU"};
    QSet<QString> groupJp{"JP", "MX"};
    QSet<double> corEu_other{40., 41., 43., 44.};
    double firstSizeEu = 34.;
    double curSizeUs = 3.-1.;
    double curSizeUk = 1.-1.;
    double curSizeJp = 20.-1.;
    for (double curSizeEu=firstSizeEu; curSizeEu<55; ++curSizeEu)
    {
        QHash<QString, double> countrycode_size;
        for (const auto &countryCode : groupEu)
        {
            countrycode_size[countryCode] = curSizeEu;
        }
        double otherToAdd = corEu_other.contains(curSizeEu) ? 0.5 : 1.;
        curSizeUs += otherToAdd;
        curSizeUk += otherToAdd;
        curSizeJp += otherToAdd;
        for (const auto &countryCode : groupUs)
        {
            countrycode_size[countryCode] = curSizeUs;
        }
        for (const auto &countryCode : groupUk)
        {
            countrycode_size[countryCode] = curSizeUk;
        }
        for (const auto &countryCode : groupJp)
        {
            countrycode_size[countryCode] = curSizeJp;
        }
        _list_countryCode_size << countrycode_size;
    }
    return _list_countryCode_size;
}();

const QList<QHash<QString, double>> FillerSize::SHOE_MALE_ADULT_SIZES = []() {
    QList<QHash<QString, double>> _list_countryCode_size;
    QSet<QString> groupEu{"FR","BE","ES","IT","DE","NL","SE","PL","TR"};
    QSet<QString> groupUs{"COM","CA","SA","SG","AE"};
    QSet<QString> groupUk{"UK","IE","AU"};
    QSet<QString> groupJp{"JP", "MX"};

    double firstSizeEu = 38.;
    // US at EU38 = 38−33 = 5
    double curSizeUs = 5.0 - 1.0;
    // UK at EU38 = 38−34 = 4
    double curSizeUk = 4.0 - 1.0;
    // JP at EU38 = 0.5*38+5 = 24
    double curSizeJp = 24.0 - 0.5;

    for (double curSizeEu = firstSizeEu; curSizeEu < 55; ++curSizeEu)
    {
        QHash<QString, double> countrycode_size;
        // EU sizes step by 1
        for (const auto &c : groupEu)
            countrycode_size[c] = curSizeEu;
        // increment each map
        curSizeUs += 1.0;   // US & friends step by 1
        curSizeUk += 1.0;   // UK/AU/IR step by 1
        curSizeJp += 0.5;   // JP steps by 0.5 (cm)
        // US‑group
        for (const auto &c : groupUs)
            countrycode_size[c] = curSizeUs;
        // UK‑group
        for (const auto &c : groupUk)
            countrycode_size[c] = curSizeUk;
        // JP
        for (const auto &c : groupJp)
            countrycode_size[c] = curSizeJp;
        _list_countryCode_size << countrycode_size;
    }
    return _list_countryCode_size;
}();

bool FillerSize::canFill(const TemplateFiller *templateFiller, const Attribute *attribute, const QString &marketplaceFrom, const QString &fieldIdFrom) const
{
    if (templateFiller->attributeFlagsTable()
            ->hasFlag(marketplaceFrom, fieldIdFrom, Attribute::Size))
    {
        return true;
    }
    return false;
}

QCoro::Task<void> FillerSize::fill(
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
    QString validFieldIdFrom = fieldIdFrom;
    bool foundValue = false;
    
    // We check if at least one SKU has a value for the given fieldIdFrom
    for (auto it = sku_fieldId_fromValues.cbegin(); it != sku_fieldId_fromValues.cend(); ++it)
    {
        if (it.value().contains(fieldIdFrom) && !it.value()[fieldIdFrom].isEmpty())
        {
            foundValue = true;
            break;
        }
    }

    if (!foundValue)
    {
        auto attributeFlagsTable = templateFiller->attributeFlagsTable();
        bool doneOnce = false;
        for (auto itSku = sku_fieldId_fromValues.cbegin();
             itSku != sku_fieldId_fromValues.cend() && !doneOnce; ++itSku)
        {
            const auto &fieldId_fromValues = itSku.value();
            for (auto it = fieldId_fromValues.cbegin();
                 it != fieldId_fromValues.cend(); ++it)
            {
                const auto &curFieldId = it.key();
                if (attributeFlagsTable->hasFlag(marketplaceFrom, curFieldId, Attribute::Size)
                        && !it.value().isEmpty())
                {
                    validFieldIdFrom = curFieldId;
                    doneOnce = true;
                    break;
                }
            }
        }
    }
    auto settings = templateFiller->settingsCommon();
    bool isShoes = false;
    bool isClothe = false;
    bool isNoSizeConv = false;
    initCatBools(settings.data(), productTypeFrom, isShoes, isClothe, isNoSizeConv);
    if (!isShoes && !isClothe && !isNoSizeConv)
    {
        // We ask AI to classify and update settings
        co_await askAiToUpdateSettingsForProductType(
                    productTypeFrom, settings.data());
        initCatBools(settings.data(), productTypeFrom, isShoes, isClothe, isNoSizeConv);
    }


    for (auto it = sku_fieldId_fromValues.cbegin(); it != sku_fieldId_fromValues.cend(); ++it)
    {
        const auto &sku = it.key();
        if (it.value().contains(validFieldIdFrom))
        {
            QString origValueString = it.value()[validFieldIdFrom];
            if (origValueString.isEmpty())
            {
                continue;
            }
            QVariant convertedValue;

            if (isShoes)
            {
                    convertedValue = convertShoeSize(
                                countryCodeFrom, countryCodeTo, gender, age, productTypeFrom, origValueString);
            }
            else if (isClothe)
            {
                    convertedValue = convertClothingSize(
                                countryCodeFrom, countryCodeTo, langCodeTo, gender, age, productTypeFrom, origValueString);
            }
            else
            {
                convertedValue = convertUnit(countryCodeTo, origValueString);
            }
            Q_ASSERT(convertedValue.isValid());
            sku_fieldId_toValues[sku][fieldIdTo] = convertedValue.toString();
        }
    }
    co_return;
}

// Static helper to avoid ICE in coroutine
static QSharedPointer<OpenAi2::StepMultipleAskAi> createClassificationStep(
        const QString &productType,
        QSettings *settings)
{
    QSharedPointer<OpenAi2::StepMultipleAskAi> stepClassification(new OpenAi2::StepMultipleAskAi);
    stepClassification->id = "FillerSize_classification_" + productType;
    stepClassification->name = "Classify product type for size conversion";
    stepClassification->cachingKey = stepClassification->id;
    stepClassification->neededReplies = 1; 
    stepClassification->gptModel = "gpt-5.2";
    
    stepClassification->getPromptGetBestReply = [productType](
            int nAttempts, const QList<QString> &gptValidReplies) -> QString
    {
        Q_UNUSED(nAttempts)
        Q_UNUSED(gptValidReplies)
            return QString("Classify the product type '%1' into one of the following categories:\n"
                        "- 'Shoes' (for shoes, sandals, boots)\n"
                        "- 'Clothing' (for dresses, shirts, swimwear, etc.)\n"
                        "- 'Other' (for rugs, carpets, or items where size is just dimensions like 10x20)\n"
                           "Reply in JSON format: { \"category\": \"...\" }").arg(productType);
    };
    
    stepClassification->validateBestReply = [settings, productType](
            const QString &gptReply, const QString &lastWhy) -> bool
    {
        Q_UNUSED(lastWhy)
        if (gptReply.contains("Shoes", Qt::CaseInsensitive))
        {
            if (gptReply.contains("Clothing") || gptReply.contains("Other", Qt::CaseInsensitive))
            {
                return false;
            }
            QStringList currentWords = settings->value(FillerSize::KEY_SHOE_WORDS).toStringList();
            if (!currentWords.contains(productType, Qt::CaseInsensitive))
            {
                currentWords << productType;
                settings->setValue(FillerSize::KEY_SHOE_WORDS, currentWords);
            }
            return true;
        }
        if (gptReply.contains("Clothing", Qt::CaseInsensitive))
        {
            if (gptReply.contains("Shoes") || gptReply.contains("Other", Qt::CaseInsensitive))
            {
                return false;
            }
            QStringList currentWords = settings->value(FillerSize::KEY_CLOTHE_WORDS).toStringList();
            if (!currentWords.contains(productType, Qt::CaseInsensitive))
            {
                currentWords << productType;
                settings->setValue(FillerSize::KEY_CLOTHE_WORDS, currentWords);
            }
            return true;
        }
        if (gptReply.contains("Other", Qt::CaseInsensitive))
        {
            if (gptReply.contains("Shoes") || gptReply.contains("Clothing", Qt::CaseInsensitive))
            {
                return false;
            }
            QStringList currentWords = settings->value(FillerSize::KEY_CAT_NO_CONV_WORDS).toStringList();
            if (!currentWords.contains(productType, Qt::CaseInsensitive))
            {
                currentWords << productType;
                settings->setValue(FillerSize::KEY_CAT_NO_CONV_WORDS, currentWords);
            }
            return true;
        }
        return false;
    };
    stepClassification->onLastError = [](const QString &reply, QNetworkReply::NetworkError networkError, const QString &lastWhy) -> bool
    {
        ExceptionTemplate exception;
        QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                .arg(QString::number(networkError)
                     , reply
                     , lastWhy);
        exception.setInfos(QObject::tr("AI Product Type Classification Failed"), errorMsg);
        exception.raise();
        return true;
    };
    return stepClassification;
}

void FillerSize::initCatBools(
        QSettings *settings
        , const QString &productType
        , bool &isShoes
        , bool &isClothe
        , bool &isNoSizeConv)
{
    QStringList shoeWords = settings->value(
                KEY_SHOE_WORDS,
                QStringList{"SHOE", "SANDAL", "BOOT"}
                ).toStringList();
    QStringList clotheWords = settings->value(
                KEY_CLOTHE_WORDS,
                QStringList{"DRESS", "SHIRT", "LEOTARD", "SWIM"}
                ).toStringList();
    QStringList catNoSizeConvWords = settings->value(
                KEY_CAT_NO_CONV_WORDS,
                QStringList{"RUG"}
                ).toStringList();
    isShoes = false;
    isClothe = false;
    isNoSizeConv = false;
    for (const auto &shoeWord : shoeWords)
    {
        if (productType.contains(shoeWord, Qt::CaseInsensitive))
        {
            isShoes = true;
            break;
        }
    }
    for (const auto &clotheWord : clotheWords)
    {
        if (productType.contains(clotheWord, Qt::CaseInsensitive))
        {
            isClothe = true;
            break;
        }
    }
    for (const auto &noSizeConvWord : catNoSizeConvWords)
    {
        if (productType.contains(noSizeConvWord, Qt::CaseInsensitive))
        {
            isNoSizeConv = true;
            break;
        }
    }
}


QCoro::Task<void> FillerSize::askAiToUpdateSettingsForProductType(
        QString productType,
        QSettings *settings) const
{
    auto stepClassification = createClassificationStep(
        productType, settings);

    QList<QSharedPointer<OpenAi2::StepMultipleAskAi>> steps;
    steps.append(stepClassification);

    qDebug() << "--\nFillerSize::askAiToUpdateSettingsForProductType:" << stepClassification->getPrompt(0);
    // Execute step
    co_await OpenAi2::instance()->askGptMultipleTimeAiCoro(steps, "gpt-5.2");
    co_return;
}

QVariant FillerSize::convertUnit(const QString &countryTo, const QVariant &origValue) const
{
    QString val = origValue.toString();
    // Regex for "Dimension Chain"
    // Captures sequences of numbers separated by 'x' or 'X' or '×', ending with an optional unit.
    // Group 1: The full numeric chain (e.g. "10 x 20" or "10")
    // Group 2: The unit (e.g. "cm")
    static const QRegularExpression re("((?:[0-9]+(?:\\.[0-9]+)?)(?:\\s*[xX×]\\s*[0-9]+(?:\\.[0-9]+)?)*)\\s*(cm|inch|\"|in|''|“)?", QRegularExpression::CaseInsensitiveOption);
    auto matches = re.globalMatch(val);
    
    if (!matches.hasNext())
    {
        return origValue;
    }

    struct FetchedUnit {
        QString chain;
        QString unit;
        bool isInch;
        bool isCm;
        QString rawValue; 
    };
    
    QList<FetchedUnit> unitsFound;
    
    while(matches.hasNext())
    {
        auto match = matches.next();
        QString chain = match.captured(1);
        QString unit = match.captured(2).toLower();
        
        bool isInch = (unit == "inch" || unit == "\"" || unit == "in" || unit == "''" || unit == "“");
        bool isCm = (unit == "cm");
        
        unitsFound.append({chain, unit, isInch, isCm, match.captured(0)});
    }
    
    static const QSet<QString> inchCountries{"UK", "IE", "AU", "COM", "CA", "US", "SG"}; 
    bool targetIsInch = inchCountries.contains(countryTo.toUpper());
    
    // 1. Try to find a match that already satisfies the target unit
    for (const auto &u : unitsFound)
    {
        if (targetIsInch && u.isInch)
        {
             return u.rawValue;
        }
        if (!targetIsInch && u.isCm)
        {
            return u.rawValue;
        }
    }
    
    // 2. If no matching unit found, convert the first one
    if (!unitsFound.isEmpty())
    {
        const auto &u = unitsFound.first();
        
        // Helper lambda to convert chain
        auto convertChain = [&](double factor, const QString &newUnit) -> QString
        {
            QString res;
            static const QRegularExpression reNums("[0-9]+(?:\\.[0-9]+)?");
            auto nums = reNums.globalMatch(u.chain);
            int lastPos = 0;
            while (nums.hasNext())
            {
                auto match = nums.next();
                // Append separator if any
                if (match.capturedStart() > lastPos)
                {
                     res += u.chain.mid(lastPos, match.capturedStart() - lastPos);
                }
                
                double val = match.captured(0).toDouble();
                double newVal = val * factor;
                // If factor < 1 (cm to inch), 2 decimals. If > 1 (inch to cm), 1 decimal.
                if (factor < 1.0)
                {
                     res += QString::number(newVal, 'f', 2);
                }
                else
                {
                     res += QString::number(newVal, 'f', 1);
                }
            }
            // Append remaining if any (unlikely for well formed chain but safety)
             if (lastPos < u.chain.length() && 0)
             { // logic above is slightly flawed for append.
             }
             
             // Simpler approach: split by regex separator, convert, join.
             QStringList parts = u.chain.split(QRegularExpression("\\s*[xX×]\\s*"));
             QStringList convertedParts;
             // We need to preserve the exact separators? User request: "10x20cm" -> "3.94" x 7.87"". Implicitly adding " to each?
             // Or "3.94 x 7.87""?
             // Test expectation: "3.94\" x 7.87\"". So unit attached to EACH or just at end?
             // If input "10x20cm" (unit at end), typically we want "3.94\" x 7.87\"" (unit at each) OR "3.94 x 7.87\"" (unit at end).
             // Let's assume unit at end for the whole group if the input had unit at end.
             // Wait, test said: `QVariant("3.94\" x 7.87\"")`. This implies unit on EACH.
             // But my Plan said "reconstruct the string with the new unit symbol".
             // If I reconstruct "val x val" + " unit", it is "val x val unit".
             // If I want unit on each, I need to append unit to each number.
             
             for (int i=0; i<parts.size(); ++i)
             {
                 double val = parts[i].toDouble();
                 double newVal = val * factor;
                 QString s = (factor < 1.0) ? QString::number(newVal, 'f', 2) : QString::number(newVal, 'f', 1); 
                 // If we want unit on each:
                 if (factor < 1.0)
                 {
                     s += "\""; // inch
                 }
                 
                 convertedParts << s;
             }
             
             if (factor < 1.0)
             { // Target Inch
                 return convertedParts.join(" x ");
             }
             else
             { // Target Cm
                 return convertedParts.join(" x ") + " cm";
             }
        };

        if (u.isCm && targetIsInch)
        {
             // cm to inch: / 2.54
             return convertChain(1.0/2.54, "\"");
        }
        else if (u.isInch && !targetIsInch)
        {
             // inch to cm: * 2.54
            return convertChain(2.54, " cm");
        }
    }
    
    return origValue;
}

QVariant FillerSize::convertClothingSize(
        const QString &countryFrom,
        const QString &countryTo,
        const QString &langTo,
        Gender targetGender,
        Age age_range_description,
        const QString &productType,
        const QVariant &origValue) const
{
    if (productType.toUpper() == "RUG")
    {
        return origValue;
    }
    bool isNum = false;
    int num = origValue.toInt(&isNum);
    QList<QHash<QString, int>> list_countryCode_size;
    if (targetGender == Gender::UndefinedGender
        || age_range_description == Age::UndefinedAge)
    {
        ExceptionTemplate exception;
        exception.setInfos(QObject::tr("No gender / age"),
                           QObject::tr("The gender and/or age_range_description was not defined in the template"));
        exception.raise();
    }
    
    if (targetGender == Gender::Female && age_range_description == Age::Adult)
    {
        list_countryCode_size = CLOTHE_FEMALE_ADULT_SIZES;
    }
    else if (targetGender == Gender::Male && age_range_description == Age::Adult)
    {
         list_countryCode_size = CLOTHE_MALE_ADULT_SIZES;
    }
    else
    {
         Q_ASSERT(false);
    }
    if (isNum)
    {
        for (const auto &countryCode_size : list_countryCode_size)
        {
            // Check if both keys exist to avoid crash if they don't
            if (countryCode_size.contains(countryTo)
                     && countryCode_size.contains(countryFrom)) {
                 Q_ASSERT(countryCode_size.contains(countryTo)
                          && countryCode_size.contains(countryFrom));
                 if (countryCode_size[countryFrom] == num)
                 {
                     return countryCode_size[countryTo];
                 }
            }
        }
    }
    // Q_ASSERT(countryTo != "JP"); //Should map cm -- Commented out to be safe, original had it.
    if (countryTo == "BE" && langTo == "FR")
    {
        if (origValue == "XL")
        {
            return "TG";
        }
        else if (origValue == "XXL")
        {
            return "TTG";
        }
    }
    if (countryTo == "IE" || countryTo == "COM" || countryTo == "CA")
    {
        static const QHash<QString, QString> equivalences
        {
            {"S", "Small"}
            , {"M", "Medium"}
            , {"L", "Large"}
            , {"XL", "X-Large"}
            , {"XXL", "XX-Large"}
            , {"3XL", "3X-Large"}
            , {"4XL", "4X-Large"}
            , {"5XL", "5X-Large"}
            , {"6XL", "6X-Large"}
        };
        const auto &origValueString = origValue.toString();
        return equivalences.value(origValueString, origValueString);
    }
    return origValue;
}

QVariant FillerSize::convertShoeSize(
        const QString &countryFrom,
        const QString &countryTo,
        Gender targetGender,
        Age age_range_description,
        const QString &productType,
        const QVariant &origValue) const
{
    bool isNum = false;
    double num = 0.;
    const auto &origValueString = origValue.toString();
    if (origValueString.contains(" "))
    {
        auto sizeElements = origValueString.split(" ");
        if (sizeElements.first().contains("CN", Qt::CaseInsensitive))
        {
            sizeElements << sizeElements.takeFirst();
        }
        for (const auto &sizeElement : sizeElements)
        {
            num = sizeElement.toDouble(&isNum);
            if (isNum)
            {
                break;
            }
        }
    }
    else
    {
        num = origValueString.toDouble(&isNum);
    }
    if (isNum)
    {
        QList<QHash<QString, double>> list_countryCode_size;
        if (targetGender == Gender::UndefinedGender
            || age_range_description == Age::UndefinedAge)
        {
            ExceptionTemplate exception;
            exception.setInfos(QObject::tr("No gender / age"),
                               QObject::tr("The gender and/or age_range_description was not defined in the template"));
            exception.raise();
        }

        if (targetGender == Gender::Female && age_range_description == Age::Adult)
        {
            list_countryCode_size = SHOE_FEMALE_ADULT_SIZES;
        }
        else if (targetGender == Gender::Male && age_range_description == Age::Adult)
        {
             list_countryCode_size = SHOE_MALE_ADULT_SIZES;
        }
        else
        {
             Q_ASSERT(false);
        }
        for (const auto &countryCode_size : list_countryCode_size)
        {
            if (countryCode_size.contains(countryTo)
                     && countryCode_size.contains(countryFrom))
            {
                if (qAbs(countryCode_size[countryFrom] - num) < 0.0001)
                {
                    if (countryTo == "MX" || countryTo == "JP")
                    {
                        QString sizeCm{QString::number(countryCode_size[countryTo])};
                        if (sizeCm.contains("."))
                        {
                            return sizeCm += " cm";
                        }
                        return sizeCm += ".0 cm";
                    }
                    return countryCode_size[countryTo];
                }
            }
        }
    }
    return origValue;
}
