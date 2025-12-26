#ifndef TEMPLATEFILLER_H
#define TEMPLATEFILLER_H

#include <QString>
#include <QStringList>
#include <QDir>

#include <xlsxdocument.h>

class TemplateFiller
{
public:
    TemplateFiller(const QString &templateFromPath
                      , const QStringList &templateToPaths);
    struct AttributeFound{
        QString id;
        QString marketplaceId;
        bool isAiGuessed;
    };
    enum VersionAmz{
        V01
        , V02
    };
    void setTemplates(const QString &templateFromPath
                      , const QStringList &templateToPaths);
    void checkParentSkus(); // TODO raise exeption
    void checkKeywords(); // TODO raise exeption
    void checkPreviewImages(); // TODO raise exeption
    QStringList getImagePreviewFileNames() const;
    QString checkSkus() const; // check child / parent are correct without duplicates

     // Return all field with values or ask AI after reading field ids
    QStringList findPreviousTemplatePath() const;
    QList<AttributeFound> findAttributesMandatory(
            const QStringList &previousTemplatePaths) const;

    QStringList suggestAttributesChildOnly(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValues(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValueChild(
            const QStringList &previousTemplatePaths) const;

private:
    QHash<QString, QHash<QString, QString>> m_countryCode_langCode_keywords;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_skuPattern_countryCode_langCode_keywords;
    QDir m_workingDir;
    QDir m_workingDirImage;
    QString m_templateFromPath;
    QStringList m_templateToPaths;
    QString m_langCodeFrom;
    QString m_countryCodeFrom;
    QString _getCountryCode(const QString &templateFilePath) const;
    QString _getLangCode(const QString &templateFilePath) const;
    QString _getLangCodeFromText(const QString &langInfos) const;
    void _selectTemplateSheet(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString> > _readKeywords(const QStringList &filePaths);
    TemplateFiller::VersionAmz _getDocumentVersion(QXlsx::Document &document) const;
    int _getRowFieldId(VersionAmz version) const;
    QHash<QString, int> _get_fieldId_index(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString>> _get_parentSku_skus(QXlsx::Document &doc) const;
    void _formatFieldId(QString &fieldId) const;
    int _getIndCol(const QHash<QString, int> &fieldId_index
                   , const QStringList &possibleValues) const;
    int _getIndColSku(const QHash<QString, int> &fieldId_index) const;
    int _getIndColSkuParent(const QHash<QString, int> &fieldId_index) const;
    int _getIndColColorName(const QHash<QString, int> &fieldId_index) const;
};

#endif // TEMPLATEFILLER_H
