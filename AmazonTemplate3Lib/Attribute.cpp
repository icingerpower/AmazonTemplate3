#include "Attribute.h"

const QString Attribute::AMAZON_V01{"Amazon V01"};
const QString Attribute::AMAZON_V02{"Amazon V02"};
const QString Attribute::TEMU_EN{"Temu"};
const QStringList Attribute::MARKETPLACES{
    Attribute::AMAZON_V01
            , Attribute::AMAZON_V02
            , Attribute::TEMU_EN};

const QHash<QString, bool> Attribute::MARKETPLACES_HAS_PARENT_LINE{
    {Attribute::AMAZON_V01, true}
    , {Attribute::AMAZON_V02, true}
    , {Attribute::TEMU_EN, false}
};

const QHash<Attribute::Flag, QString> Attribute::FLAG_STRING{
    {ChildOnly, "ChildOnly"}
    , {NoAI, "NoAI"}
    , {PutFirstValue, "PutFirstValue"}
    , {SameValue, "SameValue"}
    , {Size, "Size"}
    , {ChildSameValue, "ChildSameValue"}
    , {ForCustomInstructions, "ForCustomInstructions"}
    , {ReadablePreviousTemplates, "ReadablePreviousTemplates"}
    , {Copy, "Copy"}
    , {MandatoryAmazon, "MandatoryAmazon"}
    , {MandatoryTemu, "MandatoryTemu"}
};

const QMap<QString, Attribute::Flag> Attribute::STRING_FLAG
= [](){
    QMap<QString, Flag> _STRING_FLAG;
    for (auto it = FLAG_STRING.begin();
         it != FLAG_STRING.end(); ++it)
    {
        _STRING_FLAG[it.value()] = it.key();
    }
    return _STRING_FLAG;
}();

bool Attribute::hasFlag(const QString &marketplace, Flag flag) const
{
    const int mask = static_cast<int>(flag);

    const int value = static_cast<int>(m_flag);
    return (value & mask) == mask;
}

bool Attribute::isChoice(const QString &marketplace) const
{
    auto it = m_marketplace_countryCode_langCode_category_possibleValues.constFind(marketplace);
    return it != m_marketplace_countryCode_langCode_category_possibleValues.constEnd()
            && it.value().size() > 0;
}

const QSet<QString> &Attribute::possibleValues(
        const QString &marketplace
        , const QString &countryCode
        , const QString &langCode
        , const QString &category) const
{
    if (m_marketplace_countryCode_langCode_category_possibleValues.contains(marketplace)
            && m_marketplace_countryCode_langCode_category_possibleValues[marketplace].contains(countryCode)
            && m_marketplace_countryCode_langCode_category_possibleValues[marketplace][countryCode].contains(langCode)
            && m_marketplace_countryCode_langCode_category_possibleValues[marketplace][countryCode][langCode].contains(category)
            )
    {
        auto itMkt = m_marketplace_countryCode_langCode_category_possibleValues.constFind(marketplace);
        auto itCountry = itMkt.value().constFind(countryCode);
        auto itLang = itCountry.value().constFind(langCode);
        auto itCat = itLang.value().constFind(category);
        return itCat.value();
    }
    static QSet<QString> emptySet;
    return emptySet;
}

void Attribute::addFlag(const QString &flagString)
{
    if (STRING_FLAG.contains(flagString))
    {
        Flag f = STRING_FLAG.value(flagString);
        m_flag = static_cast<Flag>(static_cast<int>(m_flag) | static_cast<int>(f));
    }
}

void Attribute::setPossibleValues(
        const QString &marketplace
        , const QString &countryCode
        , const QString &langCode
        , const QString &category
        , const QSet<QString> &possibleValues)
{
    m_marketplace_countryCode_langCode_category_possibleValues
            [marketplace][countryCode][langCode][category] = possibleValues;
}

void Attribute::setFlag(Flag newFlag)
{
    m_flag = newFlag;
}

const QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>
&Attribute::marketplace_countryCode_langCode_category_possibleValues() const
{
    return m_marketplace_countryCode_langCode_category_possibleValues;
}

