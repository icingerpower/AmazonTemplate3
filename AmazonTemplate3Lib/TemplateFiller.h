#ifndef TEMPLATEFILLER_H
#define TEMPLATEFILLER_H

#include <QString>
#include <QStringList>


class TemplateFiller
{
public:
    struct AttributeFound{
        QString id;
        QString marketplaceId;
        bool isAiGuessed;
    };
    static TemplateFiller *instance();
    void setTemplates(const QString &templateFromPath
                      , const QStringList &templateToPaths);
    void checkParentSkus(); // TODO raise exeption
    void checkKeywords(); // TODO raise exeption
    void checkPreviewImages(); // TODO raise exeption
    QStringList getImagePreviewFileNames() const;
    QString checkSkus() const; // check child / parent are correct without duplicates

     // Return all field with values or ask AI after reading field ids
    QList<AttributeFound> findAttributesMandatory(
            const QStringList &previousTemplatePaths) const;

    QStringList suggestAttributesChildOnly(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValues(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValueChild(
            const QStringList &previousTemplatePaths) const;

private:
    TemplateFiller();
    QString m_templateFromPath;
    QStringList m_templateToPaths;
    QString m_langCodeFrom;
    QString m_countryCodeFrom;
    QString _getCountryCode(const QString &templateFilePath) const;
    QString _getLangCode(const QString &templateFilePath) const;
    QString _getLangCodeFromText(const QString &langInfos) const;
};

#endif // TEMPLATEFILLER_H
