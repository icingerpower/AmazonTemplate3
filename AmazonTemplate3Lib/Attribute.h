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
        NoFlag = 0
        , ChildOnly = 1
        , NoAI = 2
        , PutFirstValue = 4
        , Size = 4
        , SameValue = 8
        , ChildSameValue = 16
        , ForCustomInstructions = 32
        , ReadablePreviousTemplates = 64
        , Copy = 128
        , MandatoryAmazon = 256
        , MandatoryTemu = 512
    };
    static const QHash<Flag, QString> FLAG_STRING;
    static const QMap<QString, Flag> STRING_FLAG;
    static const QStringList MARKETPLACES;
    static const QHash<QString, bool> MARKETPLACES_HAS_PARENT_LINE;

    bool hasFlag(const QString &marketplace, Flag flag) const;
    bool isChoice(const QString &marketplace) const;
    const QSet<QString> &possibleValues(const QString &marketplace
                                        , const QString &countryCode
                                        , const QString &langCode
                                        , const QString &category) const;
    void addFlag(const QString &flagString);
    void setPossibleValues(const QString &marketplace
                           , const QString &countryCode
                           , const QString &langCode
                           , const QString &category
                           , const QSet<QString> &possibleValues);

    void setFlag(Flag newFlag);


    const QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>
    &marketplace_countryCode_langCode_category_possibleValues() const;

private:
    Flag m_flag;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>> m_marketplace_countryCode_langCode_category_possibleValues;
};

#endif // ATTRIBUTE_H
