#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

#include <QDate>
#include <QHash>
#include <QString>
#include <QSharedPointer>

class Attribute
{
public:
    enum Flag{
        ChildOnly = 1
        , Mandatory = 2
        , NoAI = 4
        , SameValue = 8
        , ChildSameValue = 16
        , ForCustomInstructions = 32
    };
    bool hasParentLine(const QString &marketplaceId);
    bool hasFlag(const QString &marketplaceId, Flag flag) const;
    bool isChoice(const QString &marketplaceId) const;
    const QSet<QString> &possibleValues(const QString &marketplaceId
                                        , const QString &countryCode
                                        , const QString &langCode
                                        , const QString &category) const;
    const QString &getEquivalentValue(const QString &marketPlaceId
                                      , const QString &countryCode
                                      , const QString &langCode
                                      , const QString &category
                                      , const QString &fromValue) const;

private:
    QHash<QString, QSet<QString>> m_marketplaceId_ids;
    Flag flag;
    QHash<QString, Flag> m_marketplaceId_flag;
    QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>> m_marketplaceId_countryCode_langCode_possibleValues;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>> m_marketplaceId_countryCode_langCode_category_possibleValues;
    QList<QSet<QString>> m_equivalences; // {{"Femme", "Women"}, {"Homme", "Men"}}
    static QHash<QString, QHash<QString, QSharedPointer<Attribute>>> marketplaceId_attributeId_attributeInfos;
};
// Will be saved in the following CSV tab format
// amazon.fr / deparment / fr / fr / femme / homme
// amazon.fr / deparment / cat=shoes / fr / fr / femmes / hommes

#endif // ATTRIBUTE_H
