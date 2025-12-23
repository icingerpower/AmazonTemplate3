#include "Attribute.h"

bool Attribute::hasParentLine(const QString &marketplaceId)
{
    if (marketplaceId.contains("amazon"))
    {
        return true;
    }
    else if (marketplaceId.contains("temu"))
    {
        return false;
    }
    Q_ASSERT(false);
    return true;
}

bool Attribute::hasFlag(const QString &marketplaceId, Flag flag) const
{
    const int mask = static_cast<int>(flag);

    if (m_marketplaceId_flag.contains(marketplaceId))
    {
        const int value = static_cast<int>(m_marketplaceId_flag.value(marketplaceId));
        return (value & mask) == mask;
    }

    const int value = static_cast<int>(this->flag);
    return (value & mask) == mask;
}

bool Attribute::isChoice(const QString &marketplaceId) const
{
    auto it = m_marketplaceId_countryCode_langCode_category_possibleValues.constFind(marketplaceId);
    return it != m_marketplaceId_countryCode_langCode_category_possibleValues.constEnd()
            && it.value().size() > 0;
}

const QSet<QString> &Attribute::possibleValues(
        const QString &marketplaceId
        , const QString &countryCode
        , const QString &langCode
        , const QString &category) const
{
    if (m_marketplaceId_countryCode_langCode_category_possibleValues.contains(marketplaceId)
            && m_marketplaceId_countryCode_langCode_category_possibleValues[marketplaceId].contains(countryCode)
            && m_marketplaceId_countryCode_langCode_category_possibleValues[marketplaceId][countryCode].contains(langCode)
            && m_marketplaceId_countryCode_langCode_category_possibleValues[marketplaceId][countryCode][langCode].contains(category)
            )
    {
        auto itMkt = m_marketplaceId_countryCode_langCode_category_possibleValues.constFind(marketplaceId);
        auto itCountry = itMkt.value().constFind(countryCode);
        auto itLang = itCountry.value().constFind(langCode);
        auto itCat = itLang.value().constFind(category);
        return itCat.value();
    }
    if (m_marketplaceId_countryCode_langCode_possibleValues.contains(marketplaceId)
            && m_marketplaceId_countryCode_langCode_possibleValues[marketplaceId].contains(countryCode)
            && m_marketplaceId_countryCode_langCode_possibleValues[marketplaceId][countryCode].contains(langCode)
            )
    {
        auto itMkt = m_marketplaceId_countryCode_langCode_possibleValues.constFind(marketplaceId);
        auto itCountry = itMkt.value().constFind(countryCode);
        auto itLang = itCountry.value().constFind(langCode);
        return itLang.value();
    }
    static QSet<QString> emptySet;
    return emptySet;
}

const QString &Attribute::getEquivalentValue(
        const QString &marketPlaceId
        , const QString &countryCode
        , const QString &langCode
        , const QString &category, const QString &fromValue) const
{
    for (const auto &equivalentValues : m_equivalences)
    {
        if (equivalentValues.contains(fromValue))
        {
            const auto &_possibleValues = possibleValues(
                        marketPlaceId, countryCode, langCode, category);
            for (const auto &equivalent : equivalentValues)
            {
                if (_possibleValues.contains(equivalent))
                {
                    return equivalent;
                }
            }
        }
    }
    // TODO raise exception
    static QString empty;
    return empty;
}

