#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

#include <QDate>
#include <QHash>
#include <QString>
#include <QSharedPointer>

class Attribute
{
public:
    static const QString AMAZON_V01;
    static const QString AMAZON_V02;
    static const QString TEMU_EN;
    enum Flag{
        NoFlag = 1
        , ChildOnly = 1
        , NoAI = 2
        , PutFirstValue = 4
        , Size = 4
        , SameValue = 8
        , ChildSameValue = 16
        , ForCustomInstructions = 32
        , ReadablePreviousTemplates = 64
        , MandatoryAmazon = 128
        , MandatoryTemu = 256
    };
    static const QHash<Flag, QString> FLAG_STRING;
    static const QMap<QString, Flag> STRING_FLAG;
    static const QStringList MARKETPLACES;
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
    QSet<QString> m_variousIds;
    Flag flag;
    QHash<QString, Flag> m_marketplaceId_flag;
    QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>> m_marketplaceId_countryCode_langCode_possibleValues;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>> m_marketplaceId_countryCode_langCode_category_possibleValues;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QHash<QString, QString>>>>> m_marketplaceId_countryCode_langCode_category_replacedValues; // When there is an error in an amazon template, a possible value is replace by another with the right value
    QList<QSet<QString>> m_equivalences; // {{"Femme", "Women"}, {"Homme", "Men"}}
    static QHash<QString, QHash<QString, QSharedPointer<Attribute>>> marketplaceId_attributeId_attributeInfos;
};
// Will be saved in the following CSV tab format
// amazon.fr / deparment / fr / fr / femme / homme
// amazon.fr / deparment / cat=shoes / fr / fr / femmes / hommes

#endif // ATTRIBUTE_H
